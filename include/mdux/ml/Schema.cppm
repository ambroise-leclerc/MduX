/**
 * @file Schema.cppm
 * @brief Governed-zone ML model types: the canonical shape of every model package.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Part of MduXCore. This module is canonical: the kernels (issue #58), the runtime (#62), the
 * golden-vector generator and `mdux-mlbake` (#61) all import it rather than restating its records.
 * Two files describing the same artifact will disagree eventually, and the disagreement surfaces as
 * a byte-comparison failure nobody can localise.
 *
 * Header-only by design - there is no `src/ml/Schema.cpp`. Everything here is `constexpr`, so a
 * generated `ModelPackage` can be validated at compile time and placed in read-only memory.
 *
 * ## Two design points worth defending in review
 *
 * **`TensorRef` holds a byte offset, not a pointer.** That is what lets `ModelPackage` be a
 * `constexpr` object while the weights live in a separately-supplied blob - an mmap, ROM, flash, or
 * a linked byte array. A pointer would make the package non-`constexpr` and force multi-megabyte
 * weights into generated source, which MSVC does not survive in reasonable time.
 *
 * **`GoldenVector` stores `u32` bit patterns, not floats.** Comparison is bitwise. Never decimal,
 * never an epsilon. An epsilon comparison would silently accept exactly the floating-point drift
 * this mechanism exists to detect, and a decimal round-trip is a lossy re-encoding of the thing
 * being checked. See ADR-008, decision 4.
 *
 * ## `f32` only
 *
 * v1 scope per ADR-008, decision 5. There is no dtype field to get wrong: every tensor is `f32`,
 * so `byteLength()` is `elementCount() * 4` and nothing here can express anything else. `int8`
 * quantisation needs its own ADR and its own determinism argument, not an enumerator.
 *
 * ## What this module deliberately does not check
 *
 * `validate()` does not reject weight tensors whose byte ranges overlap. Weight tying is a real
 * technique, and a package that shares one tensor between two layers is well-formed. The importer
 * (#60) rejects overlap in *safetensors input*, where it means a malformed file; that is the layer
 * where the check is meaningful.
 *
 * It also **accepts a package carrying no golden vectors**, which is worth defending because
 * ADR-008 decision 4 makes the goldens a mandatory fail-closed control. The control belongs at the
 * device boundary, not here: `Classifier1D::create()` (#62) rejects an empty golden set, because
 * that is the point where an unverified model would actually be executed. `validate()` is a
 * *structural* check, and it is deliberately usable on a half-assembled package - the baker calls
 * it to check an architecture against imported weights before it has generated any goldens, and
 * the golden generator needs a validated layer chain to run at all. Rejecting empty goldens here
 * would make that bootstrap impossible and would move the safety argument to the wrong layer.
 */
module;

export module mdux.ml.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.report;

export namespace mdux::ml {

// The whole format is f32, and byteLength() computes from sizeof(float) rather than a constant, so
// the on-wire assumption is stated here rather than left implicit. A platform with a 64-bit float
// would silently double every computed byte range.
static_assert(sizeof(float) == 4, "mdux.ml's package format is f32; sizeof(float) must be 4");

/// The `<kind>` component of `generated/<kind>/<id>/`, and the value of a package's `kind` member.
inline constexpr std::string_view packageKind = "model";

/// The operations v1 can express. ADR-008, decision 5 caps this set deliberately: recurrent layers
/// and attention need a follow-up ADR, not a quiet enumerator.
enum class LayerKind : std::uint8_t { Dense, Conv1d, MaxPool1d, AvgPool1d, Flatten };

/// Wire spellings for LayerKind. Order is load-bearing: an enumerator's numeric value is its index.
inline constexpr std::array<std::string_view, 5> layerKindWireValues{"dense", "conv1d", "maxPool1d",
                                                                    "avgPool1d", "flatten"};

enum class Activation : std::uint8_t { None, Relu, Sigmoid, Softmax };

/// Wire spellings for Activation. Order is load-bearing, as for layerKindWireValues.
inline constexpr std::array<std::string_view, 4> activationWireValues{"none", "relu", "sigmoid",
                                                                     "softmax"};

/// The largest rank v1 accepts. A Conv1D weight tensor is `[outChannels, inChannels, kernelSize]`;
/// nothing in the v1 kernel set needs a fourth dimension.
inline constexpr std::uint8_t maxTensorRank = 3;

enum class SchemaError : std::uint8_t {
    UnsupportedSchemaVersion,
    EmptyId,
    NoLayers,
    ZeroInputLength,
    ZeroOutputLength,
    UnknownLayerKind,        ///< a wire value outside layerKindWireValues
    UnknownActivation,
    RankTooLarge,            ///< rank > maxTensorRank
    ZeroDimension,           ///< a shape entry within the rank is zero
    InputLengthMismatch,     ///< the first layer does not consume exactly inputLength floats
    OutputLengthMismatch,    ///< the last layer does not produce exactly outputLength floats
    LayerChainMismatch,      ///< layer i's output footprint is not layer i+1's input footprint
    ZeroLayerDimension,      ///< an in/out length or channel count is zero
    ZeroKernelSize,
    ZeroStride,
    KernelLargerThanInput,
    OutputLengthNotDerivable,  ///< declared outLength disagrees with the windowing arithmetic
    ChannelCountChanged,       ///< a pooling layer may not change the channel count
    MissingWeights,            ///< a layer that must carry weights declares rank 0
    UnexpectedWeights,         ///< a layer that carries none declares a tensor
    MissingBias,
    WeightShapeMismatch,       ///< a weight tensor's shape disagrees with the layer's dimensions
    BiasShapeMismatch,
    UnalignedTensor,           ///< a byte offset that is not a multiple of 4 cannot hold f32
    TensorOutOfBounds,         ///< the range extends past the end of the weight blob
    ScratchTooSmall,           ///< maxScratchFloats does not cover the worst-case footprint
    GoldenInputLengthMismatch,
    GoldenOutputLengthMismatch,
};

[[nodiscard]] constexpr std::string_view describe(SchemaError error) noexcept {
    switch (error) {
        case SchemaError::UnsupportedSchemaVersion: return "unsupported schema version";
        case SchemaError::EmptyId:                  return "package id is empty";
        case SchemaError::NoLayers:                 return "package declares no layers";
        case SchemaError::ZeroInputLength:          return "inputLength is zero";
        case SchemaError::ZeroOutputLength:         return "outputLength is zero";
        case SchemaError::UnknownLayerKind:         return "unknown layer kind";
        case SchemaError::UnknownActivation:        return "unknown activation";
        case SchemaError::RankTooLarge:             return "tensor rank exceeds 3";
        case SchemaError::ZeroDimension:            return "tensor shape has a zero dimension";
        case SchemaError::InputLengthMismatch:      return "first layer does not consume inputLength floats";
        case SchemaError::OutputLengthMismatch:     return "last layer does not produce outputLength floats";
        case SchemaError::LayerChainMismatch:       return "layer output footprint is not the next layer's input";
        case SchemaError::ZeroLayerDimension:       return "layer dimension or channel count is zero";
        case SchemaError::ZeroKernelSize:           return "kernel size is zero";
        case SchemaError::ZeroStride:               return "stride is zero";
        case SchemaError::KernelLargerThanInput:    return "kernel is wider than the input";
        case SchemaError::OutputLengthNotDerivable: return "declared outLength disagrees with the windowing arithmetic";
        case SchemaError::ChannelCountChanged:      return "a pooling layer may not change the channel count";
        case SchemaError::MissingWeights:           return "layer requires a weight tensor";
        case SchemaError::UnexpectedWeights:        return "layer carries weights it cannot use";
        case SchemaError::MissingBias:              return "layer requires a bias tensor";
        case SchemaError::WeightShapeMismatch:      return "weight shape disagrees with layer dimensions";
        case SchemaError::BiasShapeMismatch:        return "bias shape disagrees with layer dimensions";
        case SchemaError::UnalignedTensor:          return "tensor byte offset is not a multiple of 4";
        case SchemaError::TensorOutOfBounds:        return "tensor range extends past the weight blob";
        case SchemaError::ScratchTooSmall:          return "maxScratchFloats is below the worst-case footprint";
        case SchemaError::GoldenInputLengthMismatch:  return "golden input length is not inputLength";
        case SchemaError::GoldenOutputLengthMismatch: return "golden output length is not outputLength";
    }
    return "unknown schema error";
}

/**
 * @brief Where a tensor lives in the weight blob, and what shape it has.
 *
 * A byte offset rather than a pointer - see the module comment for why that is load-bearing.
 * `rank == 0` means "no tensor": a pooling layer's weights, or a layer declared without a bias.
 */
struct TensorRef {
    std::uint64_t byteOffset{0};
    std::array<std::uint32_t, maxTensorRank> shape{};
    std::uint8_t rank{0};

    [[nodiscard]] constexpr bool present() const noexcept { return rank != 0; }

    /**
     * @brief Product of the first `rank` dimensions, saturating rather than wrapping.
     *
     * Three `uint32` extents multiply to as much as 2^96, so the product genuinely can exceed
     * `uint64`. Saturating at the maximum instead of wrapping is what lets validate()'s bounds
     * check stay a simple comparison: an absurd shape produces an absurd length, which is then
     * rejected. Wrapping would produce a *small* length and quietly validate.
     *
     * Returns 1 for an absent tensor, which keeps the arithmetic total - callers gate on present()
     * rather than relying on that value.
     */
    [[nodiscard]] constexpr std::uint64_t elementCount() const noexcept {
        constexpr std::uint64_t saturated = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t count = 1;
        for (std::uint8_t i = 0; i < rank && i < maxTensorRank; ++i) {
            if (shape[i] != 0 && count > saturated / shape[i]) {
                return saturated;
            }
            count *= shape[i];
        }
        return count;
    }

    /// Every tensor is f32 - there is no dtype to consult. Saturates for the same reason
    /// elementCount() does.
    [[nodiscard]] constexpr std::uint64_t byteLength() const noexcept {
        constexpr std::uint64_t saturated = std::numeric_limits<std::uint64_t>::max();
        if (!present()) {
            return 0;
        }
        const std::uint64_t count = elementCount();
        return count > saturated / sizeof(float) ? saturated : count * sizeof(float);
    }

    [[nodiscard]] constexpr std::uint64_t byteEnd() const noexcept {
        return byteOffset + byteLength();
    }

    [[nodiscard]] constexpr bool operator==(const TensorRef&) const noexcept = default;
};

/**
 * @brief One layer of the network, fully described.
 *
 * The four dimension fields describe the layer's activation footprint, not its weights:
 * `inLength * inChannels` floats in, `outLength * outChannels` floats out. `validate()` checks
 * that the weight tensor's shape agrees with them, which is the check that catches a weight file
 * imported against the wrong architecture.
 */
struct LayerDesc {
    LayerKind kind{LayerKind::Dense};
    Activation activation{Activation::None};
    std::uint32_t inLength{0};
    std::uint32_t inChannels{0};
    std::uint32_t outLength{0};
    std::uint32_t outChannels{0};
    std::uint32_t kernelSize{0};  ///< convolution and pooling only; 0 elsewhere
    std::uint32_t stride{0};      ///< convolution and pooling only; 0 elsewhere
    TensorRef weights{};
    TensorRef bias{};

    [[nodiscard]] constexpr std::uint64_t inputFloats() const noexcept {
        return static_cast<std::uint64_t>(inLength) * inChannels;
    }

    [[nodiscard]] constexpr std::uint64_t outputFloats() const noexcept {
        return static_cast<std::uint64_t>(outLength) * outChannels;
    }

    [[nodiscard]] constexpr bool operator==(const LayerDesc&) const noexcept = default;
};

/// Whether a layer kind carries a weight tensor at all. Pooling and flatten do not.
[[nodiscard]] constexpr bool carriesWeights(LayerKind kind) noexcept {
    return kind == LayerKind::Dense || kind == LayerKind::Conv1d;
}

/// Whether a layer kind uses a windowing kernel, and therefore kernelSize/stride.
[[nodiscard]] constexpr bool isWindowed(LayerKind kind) noexcept {
    return kind == LayerKind::Conv1d || kind == LayerKind::MaxPool1d || kind == LayerKind::AvgPool1d;
}

/**
 * @brief The output length a windowed layer produces, with no padding.
 *
 * v1 has no padding mode: a window that does not fit is simply not evaluated. Returns 0 when the
 * kernel is wider than the input, which validate() reports as KernelLargerThanInput rather than
 * letting it underflow.
 */
[[nodiscard]] constexpr std::uint32_t windowedOutputLength(std::uint32_t inLength,
                                                           std::uint32_t kernelSize,
                                                           std::uint32_t stride) noexcept {
    if (kernelSize == 0 || stride == 0 || kernelSize > inLength) {
        return 0;
    }
    return (inLength - kernelSize) / stride + 1;
}

/**
 * @brief One golden input→output pair, as `u32` bit patterns.
 *
 * Bit patterns, not floats, and compared bitwise - see the module comment and ADR-008, decision 4.
 * `std::bit_cast<float>` recovers the value where a diagnostic needs to print one.
 */
struct GoldenVector {
    std::span<const std::uint32_t> inputBits;
    std::span<const std::uint32_t> expectedOutputBits;
};

/**
 * @brief A whole model as generated code exposes it and the runtime consumes it.
 *
 * Non-owning and `constexpr`-constructible throughout: the spans point at generated static data,
 * and the weights live in a blob the caller supplies separately, identified by `weightsDigest`.
 */
struct ModelPackage {
    std::string_view id;
    std::uint64_t schemaVersion{evidence::kSchemaVersion};
    evidence::Digest weightsDigest{};
    std::uint64_t weightsByteLength{0};
    std::span<const LayerDesc> layers;
    std::span<const GoldenVector> goldens;
    std::uint32_t inputLength{0};   ///< total input floats, channels included
    std::uint32_t outputLength{0};  ///< total output floats
    std::uint32_t maxScratchFloats{0};

    /// Checks every invariant a consumer is entitled to assume, so the kernels can be written
    /// without defensive checks in their inner loops. See requiredScratchFloats() for the scratch
    /// rule in particular.
    [[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> validate() const noexcept;
};

/**
 * @brief The scratch floats `predict()` needs for this layer chain.
 *
 * `predict()` ping-pongs between two buffers, so the requirement is twice the largest activation
 * the chain ever holds - input, every intermediate, and output. Computing it here rather than in
 * the baker is what keeps the baker's `maxScratchFloats` and the runtime's check in agreement:
 * there is one formula, and both sides call it.
 *
 * Returns 0 for an empty layer span, which validate() has already rejected as NoLayers.
 */
[[nodiscard]] constexpr std::uint64_t requiredScratchFloats(std::span<const LayerDesc> layers,
                                                            std::uint32_t inputLength) noexcept {
    std::uint64_t largest = inputLength;
    for (const LayerDesc& layer : layers) {
        largest = std::max(largest, layer.outputFloats());
    }
    return largest * 2;
}

constexpr mdux::core::ResultVoid<SchemaError> ModelPackage::validate() const noexcept {
    using core::err;

    if (schemaVersion != evidence::kSchemaVersion) {
        return err(SchemaError::UnsupportedSchemaVersion);
    }
    if (id.empty()) {
        return err(SchemaError::EmptyId);
    }
    if (layers.empty()) {
        return err(SchemaError::NoLayers);
    }
    if (inputLength == 0) {
        return err(SchemaError::ZeroInputLength);
    }
    if (outputLength == 0) {
        return err(SchemaError::ZeroOutputLength);
    }

    for (const LayerDesc& layer : layers) {
        if (static_cast<std::uint8_t>(layer.kind) >= layerKindWireValues.size()) {
            return err(SchemaError::UnknownLayerKind);
        }
        if (static_cast<std::uint8_t>(layer.activation) >= activationWireValues.size()) {
            return err(SchemaError::UnknownActivation);
        }
        if (layer.inLength == 0 || layer.inChannels == 0 || layer.outLength == 0 ||
            layer.outChannels == 0) {
            return err(SchemaError::ZeroLayerDimension);
        }

        for (const TensorRef* tensor : {&layer.weights, &layer.bias}) {
            if (tensor->rank > maxTensorRank) {
                return err(SchemaError::RankTooLarge);
            }
            for (std::uint8_t i = 0; i < tensor->rank; ++i) {
                if (tensor->shape[i] == 0) {
                    return err(SchemaError::ZeroDimension);
                }
            }
            if (tensor->present()) {
                if (tensor->byteOffset % sizeof(float) != 0) {
                    return err(SchemaError::UnalignedTensor);
                }
                // Written as two subtractions rather than `byteEnd() > weightsByteLength`, which
                // is what it replaced. A generated or hand-edited package can name any offset and
                // any shape, and `byteOffset + byteLength()` wraps for large values - so the
                // obvious form lets an out-of-bounds tensor validate and then read off the end of
                // the weight blob. Neither expression below can overflow.
                if (tensor->byteOffset > weightsByteLength ||
                    tensor->byteLength() > weightsByteLength - tensor->byteOffset) {
                    return err(SchemaError::TensorOutOfBounds);
                }
            }
        }

        if (isWindowed(layer.kind)) {
            if (layer.kernelSize == 0) {
                return err(SchemaError::ZeroKernelSize);
            }
            if (layer.stride == 0) {
                return err(SchemaError::ZeroStride);
            }
            if (layer.kernelSize > layer.inLength) {
                return err(SchemaError::KernelLargerThanInput);
            }
            if (layer.outLength !=
                windowedOutputLength(layer.inLength, layer.kernelSize, layer.stride)) {
                return err(SchemaError::OutputLengthNotDerivable);
            }
        }

        switch (layer.kind) {
            case LayerKind::Dense:
                // A dense layer consumes a flat vector: inChannels/outChannels are 1, and the
                // weight matrix is [outLength, inLength].
                if (!layer.weights.present()) {
                    return err(SchemaError::MissingWeights);
                }
                if (layer.weights.rank != 2 || layer.weights.shape[0] != layer.outLength ||
                    layer.weights.shape[1] != layer.inLength || layer.inChannels != 1 ||
                    layer.outChannels != 1) {
                    return err(SchemaError::WeightShapeMismatch);
                }
                if (!layer.bias.present()) {
                    return err(SchemaError::MissingBias);
                }
                if (layer.bias.rank != 1 || layer.bias.shape[0] != layer.outLength) {
                    return err(SchemaError::BiasShapeMismatch);
                }
                break;

            case LayerKind::Conv1d:
                if (!layer.weights.present()) {
                    return err(SchemaError::MissingWeights);
                }
                if (layer.weights.rank != 3 || layer.weights.shape[0] != layer.outChannels ||
                    layer.weights.shape[1] != layer.inChannels ||
                    layer.weights.shape[2] != layer.kernelSize) {
                    return err(SchemaError::WeightShapeMismatch);
                }
                if (!layer.bias.present()) {
                    return err(SchemaError::MissingBias);
                }
                if (layer.bias.rank != 1 || layer.bias.shape[0] != layer.outChannels) {
                    return err(SchemaError::BiasShapeMismatch);
                }
                break;

            case LayerKind::MaxPool1d:
            case LayerKind::AvgPool1d:
                if (layer.weights.present() || layer.bias.present()) {
                    return err(SchemaError::UnexpectedWeights);
                }
                if (layer.outChannels != layer.inChannels) {
                    return err(SchemaError::ChannelCountChanged);
                }
                break;

            case LayerKind::Flatten:
                if (layer.weights.present() || layer.bias.present()) {
                    return err(SchemaError::UnexpectedWeights);
                }
                // Flatten reshapes without moving data, so its footprint must be preserved
                // exactly; the runtime implements it as a no-op on the activation buffer.
                if (layer.inputFloats() != layer.outputFloats() || layer.outChannels != 1) {
                    return err(SchemaError::WeightShapeMismatch);
                }
                break;
        }
    }

    if (layers.front().inputFloats() != inputLength) {
        return err(SchemaError::InputLengthMismatch);
    }
    if (layers.back().outputFloats() != outputLength) {
        return err(SchemaError::OutputLengthMismatch);
    }
    for (std::size_t i = 1; i < layers.size(); ++i) {
        if (layers[i - 1].outputFloats() != layers[i].inputFloats()) {
            return err(SchemaError::LayerChainMismatch);
        }
    }

    if (maxScratchFloats < requiredScratchFloats(layers, inputLength)) {
        return err(SchemaError::ScratchTooSmall);
    }

    for (const GoldenVector& golden : goldens) {
        if (golden.inputBits.size() != inputLength) {
            return err(SchemaError::GoldenInputLengthMismatch);
        }
        if (golden.expectedOutputBits.size() != outputLength) {
            return err(SchemaError::GoldenOutputLengthMismatch);
        }
    }

    return {};
}

}  // namespace mdux::ml
