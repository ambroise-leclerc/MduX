/**
 * @file GoldenGen.cppm
 * @brief Host-tools-zone golden-vector generation, through the governed kernels.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The golden vectors are produced by running the model through **`mdux.ml.kernels`** - the same
 * governed module the device runtime executes. Not a reimplementation, not a reference
 * implementation: the same object code, built with the same flags.
 *
 * That identity is the entire reason a golden-vector mismatch on device is *unambiguous* evidence
 * of an FPU or toolchain problem. If the goldens came from a separate host implementation, a
 * mismatch would have two possible causes and localising it would be guesswork. See ADR-008,
 * decision 1.
 *
 * ## Never `<random>`
 *
 * The pseudo-random inputs come from an explicit LCG whose constants are written out below, and
 * whose seed is recorded in the recipe and in `report.json`'s resolved options.
 *
 * `<random>`'s default engine and its distributions are **not specified bit-for-bit across
 * standard library implementations**. Using them would mean the Linux and Windows CI legs
 * generated different golden inputs, so the two `package.json` files would differ - breaking the
 * byte-identity comparison that is the entire point of baking. The generator has to be as
 * reproducible as the arithmetic it is exercising.
 */
module;

export module mdux.tools.ml.goldengen;

import std;
import mdux.core.result;
import mdux.ml.schema;

export namespace mdux::tools::ml {

/// The PRNG identifier recorded in the recipe and the report. A second algorithm would be a new
/// spelling here, never a silent change to the constants below.
inline constexpr std::string_view goldenPrngAlgorithm = "lcg-numerical-recipes-32";

/// One generated golden pair, owned - `mdux::ml::GoldenVector` is a view over storage like this.
struct GeneratedGolden {
    std::vector<std::uint32_t> inputBits;
    std::vector<std::uint32_t> expectedOutputBits;
};

enum class GoldenError : std::uint8_t {
    NoGoldens,        ///< a package with no goldens has no self-test, which v1 does not permit
    NoLayers,         ///< an empty layer chain computes nothing and cannot be indexed
    ScratchTooSmall,  ///< maxScratchFloats does not cover the chain
    KernelRejected,   ///< a kernel refused the shapes a validated package implied
};

[[nodiscard]] std::string_view describe(GoldenError error) noexcept;

/**
 * @brief Generates `count` golden vectors for `layers`.
 *
 * The first four inputs are fixed patterns - zeros, ones, an alternating square wave, and a ramp -
 * chosen because they exercise saturation, sign handling and monotone response in ways a random
 * draw reliably would not. Any beyond the fourth come from the LCG seeded with `seed`.
 *
 * @param maxScratchFloats must already satisfy `requiredScratchFloats(layers, inputLength)`
 */
[[nodiscard]] mdux::core::Result<std::vector<GeneratedGolden>, GoldenError> generateGoldens(
    std::span<const mdux::ml::LayerDesc> layers, std::span<const std::byte> weights,
    std::uint32_t inputLength, std::uint32_t outputLength, std::uint32_t maxScratchFloats,
    std::size_t count, std::uint32_t seed);

}  // namespace mdux::tools::ml
