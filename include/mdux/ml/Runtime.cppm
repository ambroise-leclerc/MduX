/**
 * @file Runtime.cppm
 * @brief Governed-zone ML runtime: the device-side classifier, and its fail-closed self-test.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * No file I/O, no dynamic graph construction, no allocation in `predict()`.
 *
 * ## Weights are caller-supplied, deliberately
 *
 * A real model is megabytes. A multi-megabyte `constexpr` array takes minutes to compile and can
 * exhaust MSVC, so the weights are never part of the package object: the caller mmaps them, reads
 * them from ROM or flash, or links a blob in via `mdux_embed_blob()` (issue #64). The runtime does
 * no I/O at all, which is what makes it usable on a device with no filesystem.
 *
 * ## create() fails closed, in this order
 *
 * 1. Validate the package against `mdux.ml.schema`, `schemaVersion` included.
 * 2. Verify `sha256(weights) == pkg.weightsDigest`. This is the mechanism that makes "weights are
 *    data" safe: without it, "the caller supplies the weights" would mean "anything can be loaded".
 * 3. Check `scratch.size() >= pkg.maxScratchFloats`.
 * 4. Require **at least one** golden vector.
 * 5. **Re-run every golden vector through the real kernels and compare bit patterns.**
 *
 * Step 4 exists because step 5 is vacuously satisfied by a package carrying no goldens, and a
 * vacuous safety control is worse than an absent one - it reports success. `mdux.ml.schema`
 * deliberately *accepts* an empty golden set, because the baker validates a package's architecture
 * before it has generated any; the requirement belongs here, at the point where an unverified model
 * would actually be executed.
 *
 * Step 4 is a genuine Class C safety control, not a unit test that happens to run late. It detects
 * toolchain miscompilation, target floating-point drift, and a corrupted or mismatched package
 * *before the device ever classifies a real signal*. It costs bounded startup work and nothing per
 * frame.
 *
 * Any failure returns an error and the `Classifier1D` is never constructed, so there is no
 * partially-trustworthy object a caller can hold. That is the difference between failing closed and
 * failing degraded.
 *
 * ## MlError carries evidence, not just a code
 *
 * When a device fails closed in the field, `MlError` *is* the determinism evidence record: which
 * layer, which golden, which element, and the two bit patterns that disagreed. A bare enum would
 * throw away exactly the information the incident report needs, and the divergence is not
 * reproducible on the bench by definition - if it were, CI would have caught it.
 */
module;

export module mdux.ml.runtime;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.ml.schema;

export namespace mdux::ml {

/**
 * @brief Why a classifier refused to be constructed, or a prediction refused to run.
 *
 * The index fields are only meaningful for the codes that set them; the rest stay at their
 * defaults. `describe()` renders the code, and the caller logs the indices alongside.
 */
struct MlError {
    enum class Code : std::uint8_t {
        SchemaInvalid,      ///< the package itself is malformed - see schemaError
        DigestMismatch,     ///< sha256(weights) is not pkg.weightsDigest
        WeightsWrongSize,   ///< the blob's length is not pkg.weightsByteLength
        WeightsUnaligned,   ///< the blob is not aligned for f32 access
        SchemaVersion,      ///< the package's schemaVersion is not one this runtime reads
        UnsupportedLayer,   ///< more layers than the runtime can hold; layerIndex is the first
        ShapeMismatch,      ///< a kernel refused the spans a validated package implied
        ScratchTooSmall,
        NoGoldens,          ///< a package with no self-test at all - see create()
        GoldenMismatch,     ///< a golden vector did not reproduce - the important one
        InputLength,        ///< predict() was handed the wrong input size
        OutputLength,
    };

    Code code{Code::SchemaInvalid};
    /// Set when code is SchemaInvalid, so a package rejection keeps its specific diagnostic.
    SchemaError schemaError{SchemaError::UnsupportedSchemaVersion};
    std::uint32_t layerIndex{0};
    std::uint32_t goldenIndex{0};
    std::uint32_t elementIndex{0};
    std::uint32_t expectedBits{0};
    std::uint32_t actualBits{0};

    [[nodiscard]] bool operator==(const MlError&) const noexcept = default;
};

[[nodiscard]] std::string_view describe(MlError::Code code) noexcept;

/// The package format's cap on layer count, exposed so the baker can reject early rather than
/// producing a package no runtime can load. Classifier1D sizes its tensor table from this.
inline constexpr std::size_t maxSupportedLayers = 32;

/**
 * @brief A 1-D classifier over a validated, self-tested model package.
 *
 * Holds only spans and PODs - see the static_assert below. Construct through create(); the
 * default constructor exists only because a `std::expected` needs one, and a default-constructed
 * instance has an empty layer span so predict() refuses.
 */
class Classifier1D {
public:
    Classifier1D() noexcept = default;

    /**
     * @brief Validates, verifies and self-tests, or fails closed.
     *
     * @param weights the whole weight blob: mmap, ROM, flash, or a linked array
     * @param scratch caller-supplied working memory, at least pkg.maxScratchFloats floats
     *
     * `pkg`, `weights` and `scratch` must all outlive the returned object - it stores spans over
     * them and copies nothing.
     */
    [[nodiscard]] static mdux::core::Result<Classifier1D, MlError> create(
        const ModelPackage& package, std::span<const std::byte> weights,
        std::span<float> scratch) noexcept;

    /**
     * @brief Runs the network. No allocation, no I/O; issue #63 verifies that three ways.
     *
     * `output` is written only on success.
     */
    [[nodiscard]] mdux::core::ResultVoid<MlError> predict(std::span<const float> input,
                                                          std::span<float> output) const noexcept;

    [[nodiscard]] std::uint32_t inputLength() const noexcept { return inputLength_; }
    [[nodiscard]] std::uint32_t outputLength() const noexcept { return outputLength_; }

private:
    /// The layer's weight and bias tensors as float spans over the blob. Resolved once in create()
    /// rather than per predict(), and the reason the blob must outlive the object.
    struct LayerTensors {
        std::span<const float> weights;
        std::span<const float> bias;
    };

    /**
     * @brief Runs every layer, with the input already staged in the first scratch buffer.
     *
     * Returns a span over whichever scratch half holds the final activations. Shared by predict()
     * and by create()'s golden self-test so that the self-test exercises the identical code path -
     * a self-test that ran a different path would be evidence about that path instead.
     *
     * `const` because scratch_ is a span: the buffer is the caller's, and writing through it does
     * not modify this object.
     */
    [[nodiscard]] mdux::core::Result<std::span<const float>, MlError> runFromScratch()
        const noexcept;

    std::span<const LayerDesc> layers_;
    std::span<float> scratch_;
    /// One entry per layer. Storing these in the caller's scratch is not possible (wrong type), so
    /// it is a fixed-capacity array: v1 packages are small, and a package with more layers is
    /// rejected rather than allocated for. Sized from the exported limit rather than repeating the
    /// number, so the capacity and the rejection threshold cannot drift apart.
    std::array<LayerTensors, maxSupportedLayers> tensors_{};
    std::uint32_t inputLength_{0};
    std::uint32_t outputLength_{0};
};

static_assert(std::is_trivially_destructible_v<Classifier1D>,
              "Classifier1D must own nothing: it holds spans into caller memory, so a destructor "
              "would mean something had been copied that should not have been");

}  // namespace mdux::ml
