/**
 * @file DeterminismTests.cpp
 * @brief The cross-toolchain determinism contract for mdux.ml.kernels (issue #59).
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * ## What the frozen numbers below actually are
 *
 * They are not "the right answer" in any mathematical sense, and nothing here claims they are. They
 * are what the accumulation order specified in Kernels.cppm produces, recorded once, so that
 * **every toolchain must produce the same ones**. That is the whole claim: the Linux/GCC and
 * Windows/MSVC CI legs run this identical file, and if either one disagrees in a single bit, the
 * epic's central premise - that a golden-vector mismatch on device means the FPU or the toolchain -
 * has been falsified before any device is involved.
 *
 * So a failure here is never fixed by updating the constants. It means one of:
 *   - a kernel's accumulation order changed (see the order scenario in KernelTests.cpp),
 *   - FP contraction or fast-math reached the kernels (cmake/MduXDeterminism.cmake should have
 *     caught that at configure time - if it did not, the guard has a gap worth fixing),
 *   - expF32's range reduction or polynomial changed,
 *   - the compiler is miscompiling the kernels.
 *
 * Updating a constant to match new output converts the one loud signal in this subsystem into a
 * rubber stamp. Change the constants only alongside a deliberate, documented change to the
 * specified arithmetic - and then in the same commit as the ADR amendment that authorises it.
 *
 * ## Why the weights come from an LCG rather than <random>
 *
 * `<random>`'s distributions and default engine are not specified bit-for-bit across standard
 * library implementations, so using them here would break the very property being asserted. The
 * generator below is eight lines of exactly-specified integer arithmetic, and the float mapping
 * divides by a power of two so it is exact. ADR-008's implementation notes require this of the
 * baker's golden generation for the same reason.
 */

import std;
import speclab;
import mdux.ml.schema;
import mdux.ml.kernels;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::ml;

[[nodiscard]] std::uint32_t bitsOf(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

/// Numerical Recipes' LCG constants. Chosen for being unambiguous and widely transcribed, not for
/// statistical quality - this is a reproducible fixture, not a source of randomness.
class Lcg {
public:
    explicit constexpr Lcg(std::uint32_t seed) noexcept : state_{seed} {}

    [[nodiscard]] constexpr std::uint32_t next() noexcept {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    /// A value in [-1, 1). The division is by a power of two and the numerator is below 2^24, so
    /// both the conversion and the division are exact in f32 on any conforming implementation.
    [[nodiscard]] constexpr float nextUnit() noexcept {
        const std::uint32_t bits = next() >> 8;  // 24 bits
        return static_cast<float>(bits) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

// The reference network. Small enough to state completely, and it exercises Conv1D, MaxPool1D,
// Flatten, Dense, Relu and Softmax - every part of the v1 set whose result depends on
// accumulation order.
//
//   16 x 1  -> Conv1D k=3 s=1, 2 filters, relu -> 14 x 2
//           -> MaxPool1D k=2 s=2               ->  7 x 2
//           -> Flatten                         -> 14
//           -> Dense 14 -> 3, softmax          ->  3

constexpr std::uint32_t inputLength = 16;
constexpr std::uint32_t convOutLength = 14;
constexpr std::uint32_t convChannels = 2;
constexpr std::uint32_t poolOutLength = 7;
constexpr std::uint32_t flatLength = 14;
constexpr std::uint32_t outputLength = 3;

[[nodiscard]] LayerDesc convLayer() noexcept {
    return LayerDesc{.kind = LayerKind::Conv1d,
                     .activation = Activation::Relu,
                     .inLength = inputLength,
                     .inChannels = 1,
                     .outLength = convOutLength,
                     .outChannels = convChannels,
                     .kernelSize = 3,
                     .stride = 1,
                     .weights = TensorRef{.byteOffset = 0, .shape = {2, 1, 3}, .rank = 3},
                     .bias = TensorRef{.byteOffset = 24, .shape = {2, 0, 0}, .rank = 1}};
}

[[nodiscard]] LayerDesc poolLayer() noexcept {
    return LayerDesc{.kind = LayerKind::MaxPool1d,
                     .activation = Activation::None,
                     .inLength = convOutLength,
                     .inChannels = convChannels,
                     .outLength = poolOutLength,
                     .outChannels = convChannels,
                     .kernelSize = 2,
                     .stride = 2,
                     .weights = TensorRef{},
                     .bias = TensorRef{}};
}

[[nodiscard]] LayerDesc flattenLayer() noexcept {
    return LayerDesc{.kind = LayerKind::Flatten,
                     .activation = Activation::None,
                     .inLength = poolOutLength,
                     .inChannels = convChannels,
                     .outLength = flatLength,
                     .outChannels = 1,
                     .kernelSize = 0,
                     .stride = 0,
                     .weights = TensorRef{},
                     .bias = TensorRef{}};
}

[[nodiscard]] LayerDesc denseLayer() noexcept {
    return LayerDesc{.kind = LayerKind::Dense,
                     .activation = Activation::Softmax,
                     .inLength = flatLength,
                     .inChannels = 1,
                     .outLength = outputLength,
                     .outChannels = 1,
                     .kernelSize = 0,
                     .stride = 0,
                     .weights = TensorRef{.byteOffset = 32, .shape = {3, 14, 0}, .rank = 2},
                     .bias = TensorRef{.byteOffset = 200, .shape = {3, 0, 0}, .rank = 1}};
}

/// Everything the reference network is run on, generated from one seed in one fixed order.
struct Fixture {
    std::array<float, inputLength> input{};
    std::array<float, 6> convWeights{};
    std::array<float, 2> convBias{};
    std::array<float, 42> denseWeights{};
    std::array<float, 3> denseBias{};

    Fixture() {
        Lcg rng{20260803u};
        for (float& value : input) {
            value = rng.nextUnit();
        }
        for (float& value : convWeights) {
            value = rng.nextUnit();
        }
        for (float& value : convBias) {
            value = rng.nextUnit();
        }
        for (float& value : denseWeights) {
            value = rng.nextUnit();
        }
        for (float& value : denseBias) {
            value = rng.nextUnit();
        }
    }
};

/// Runs the reference network, returning the conv, pooled and final activations.
struct Activations {
    std::array<float, convOutLength * convChannels> conv{};
    std::array<float, poolOutLength * convChannels> pooled{};
    std::array<float, outputLength> output{};
    bool ok{false};
};

[[nodiscard]] Activations runReferenceNetwork(const Fixture& fixture) {
    Activations result;
    std::array<float, flatLength> flat{};

    const bool convOk = applyLayer(convLayer(), fixture.input, fixture.convWeights,
                                   fixture.convBias, result.conv);
    const bool poolOk = applyLayer(poolLayer(), result.conv, {}, {}, result.pooled);
    const bool flatOk = applyLayer(flattenLayer(), result.pooled, {}, {}, flat);
    const bool denseOk = applyLayer(denseLayer(), flat, fixture.denseWeights, fixture.denseBias,
                                    result.output);

    result.ok = convOk && poolOk && flatOk && denseOk;
    return result;
}

/// Compares against a frozen table, reporting every divergence with both bit patterns.
void expectFrozen(mdux::spec::Checks& checks, std::string_view what, std::span<const float> actual,
                  std::span<const std::uint32_t> frozen) {
    if (actual.size() != frozen.size()) {
        checks.expect(false, std::format("{}: {} values, expected {}", what, actual.size(),
                                         frozen.size()));
        return;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        checks.expect(bitsOf(actual[i]) == frozen[i],
                      std::format("{}[{}]: got 0x{:08X} ({}), frozen 0x{:08X}", what, i,
                                  bitsOf(actual[i]), actual[i], frozen[i]));
    }
}

// ---------------------------------------------------------------------------
// The frozen contract. Read the file comment before touching any of these.
// ---------------------------------------------------------------------------

// Three of these six happen to be bit-identical to glibc's expf, which is a useful sanity signal -
// the polynomial is landing on the correctly-rounded result more often than not - but it is not
// something to assert. Agreement with any particular libm is explicitly not the contract.
constexpr std::array<std::uint32_t, 6> frozenExp{
    0x3F800000,  // expF32(0.0f)  = 1, exactly
    0x3FD3094C,  // expF32(0.5f)
    0x402DF854,  // expF32(1.0f)
    0x3DA81C2E,  // expF32(-2.5f)
    0x4221CA0B,  // expF32(3.7f)
    0x3A10FCDD,  // expF32(-7.5f)
};

// The first four Conv1D outputs, after relu. Channel-major, so these are all channel 0.
constexpr std::array<std::uint32_t, 4> frozenConvHead{
    0x3F9B537D,
    0x3EB2E16A,
    0x3FD01C97,
    0x3F52D0C2,
};

// The pooled head must be the windowed maxima of the conv head: pooled[0] is max(conv[0], conv[1])
// and pooled[1] is max(conv[2], conv[3]), which is why the first two values below repeat conv[0]
// and conv[2] exactly. That relationship is a free cross-check on the table itself.
constexpr std::array<std::uint32_t, 4> frozenPooledHead{
    0x3F9B537D,
    0x3FD01C97,
    0x3FF36075,
    0x3FCE46DD,
};

// A softmax, so these three sum to 1 to within rounding: ~0.02511 + ~0.97345 + ~0.00145.
constexpr std::array<std::uint32_t, outputLength> frozenOutput{
    0x3CCDA872,
    0x3F793401,
    0x3ABD75F4,
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register expIsFrozen{
    "expF32 produces the same bits on every toolchain", "determinism", [] {
        return speclab::Test("ml-determinism-exp")
            .Given("the hand-written exponential and a table of frozen bit patterns", [] {})
            .When("it is evaluated at each sample point", [] {})
            .Then("every result matches the frozen bits exactly",
                  [] {
                      mdux::spec::Checks checks;
                      const std::array<float, 6> samples{0.0f, 0.5f, 1.0f, -2.5f, 3.7f, -7.5f};
                      std::array<float, 6> actual{};
                      for (std::size_t i = 0; i < samples.size(); ++i) {
                          actual[i] = expF32(samples[i]);
                      }
                      expectFrozen(checks, "expF32", actual, frozenExp);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register networkIsFrozen{
    "The reference network produces the same bits on every toolchain", "determinism", [] {
        return speclab::Test("ml-determinism-reference-network")
            .Given("a Conv1D/MaxPool1D/Flatten/Dense network over LCG-generated weights", [] {})
            .When("it is evaluated through mdux.ml.kernels", [] {})
            .Then("every activation matches the frozen bits exactly",
                  [] {
                      mdux::spec::Checks checks;
                      const Fixture fixture;
                      const Activations activations = runReferenceNetwork(fixture);

                      checks.expect(activations.ok, "every layer accepted its spans");
                      expectFrozen(checks, "conv", std::span{activations.conv}.first(4),
                                   frozenConvHead);
                      expectFrozen(checks, "pooled", std::span{activations.pooled}.first(4),
                                   frozenPooledHead);
                      expectFrozen(checks, "output", activations.output, frozenOutput);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fixtureIsReproducible{
    "The LCG fixture itself is toolchain-independent", "determinism", [] {
        return speclab::Test("ml-determinism-fixture")
            .Given("the seeded LCG the reference network draws its weights from", [] {})
            .When("the first values are generated", [] {})
            .Then("they are the frozen ones, so a fixture drift cannot masquerade as kernel drift",
                  [] {
                      // Checked separately from the network so a failure distinguishes "the inputs
                      // changed" from "the arithmetic changed" - without this, an LCG mistake would
                      // look exactly like a miscompiled kernel.
                      mdux::spec::Checks checks;
                      Lcg rng{20260803u};
                      std::array<float, 4> values{};
                      for (float& value : values) {
                          value = rng.nextUnit();
                      }
                      constexpr std::array<std::uint32_t, 4> frozenFixture{
                          0xBE8FED58,
                          0xBE553420,
                          0xBF06348E,
                          0x3F6E419A,
                      };
                      expectFrozen(checks, "lcg", values, frozenFixture);
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
