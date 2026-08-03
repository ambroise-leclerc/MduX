/**
 * @file KernelTests.cpp
 * @brief BDD scenarios for mdux.ml.kernels (issue #58).
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Every kernel case below uses values that are exact in `f32`, so the expectations are equality -
 * a tolerance there would hide the drift these kernels exist to make visible.
 *
 * Two scenarios deliberately do use a tolerance, and the distinction matters. `expF32` is compared
 * against `std::exp`, and softmax against a sum of 1.0: in both, the reference is something this
 * code is explicitly *not* required to match bit for bit. Where a comparison is bitwise, it is
 * written as a bit-pattern equality so nobody has to guess which kind it is.
 *
 * The scenario that matters most is `accumulationOrderIsNormative`. It does not freeze a bit
 * pattern taken from the implementation, which would bless whatever the code happened to do.
 * Instead it picks data where `f32` addition is provably non-associative, states the value the
 * documented order must produce, and separately asserts that the opposite order produces a
 * different one - so the test is demonstrably sensitive to the property it claims to check.
 */

import std;
import speclab;
import mdux.ml.schema;
import mdux.ml.kernels;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::ml;

/// Bit pattern of a float, so an expectation can talk about exact values.
[[nodiscard]] std::uint32_t bitsOf(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] bool sameBits(float a, float b) noexcept {
    return bitsOf(a) == bitsOf(b);
}

/// Formats a mismatch so a failure names both values and both bit patterns.
[[nodiscard]] std::string mismatch(std::string_view what, float actual, float expected) {
    return std::format("{}: got {} (0x{:08X}), expected {} (0x{:08X})", what, actual, bitsOf(actual),
                       expected, bitsOf(expected));
}

/// Checks a whole output buffer against hand-computed values, bit for bit.
void expectExact(mdux::spec::Checks& checks, std::string_view what, std::span<const float> actual,
                 std::span<const float> expected) {
    if (actual.size() != expected.size()) {
        checks.expect(false, std::format("{}: size {}, expected {}", what, actual.size(),
                                         expected.size()));
        return;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        checks.expect(sameBits(actual[i], expected[i]),
                      mismatch(std::format("{}[{}]", what, i), actual[i], expected[i]));
    }
}

constexpr TensorRef weightsRef(std::array<std::uint32_t, 3> shape, std::uint8_t rank) noexcept {
    return TensorRef{.byteOffset = 0, .shape = shape, .rank = rank};
}

// ---------------------------------------------------------------------------
// Dense
// ---------------------------------------------------------------------------

const mdux::spec::Register denseComputesHandCheckedValues{
    "Dense computes its hand-checked values exactly", "evidence-unit", [] {
        return speclab::Test("ml-kernels-dense")
            .Given("a 3->2 dense layer whose weights and inputs are exact in f32", [] {})
            .When("it is applied", [] {})
            .Then("every output matches the hand-computed value bit for bit",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Dense,
                                            .activation = Activation::None,
                                            .inLength = 3,
                                            .inChannels = 1,
                                            .outLength = 2,
                                            .outChannels = 1,
                                            .kernelSize = 0,
                                            .stride = 0,
                                            .weights = weightsRef({2, 3, 0}, 2),
                                            .bias = weightsRef({2, 0, 0}, 1)};

                      const std::array<float, 3> input{1.0f, 2.0f, 3.0f};
                      // Row-major [outFeatures][inFeatures].
                      const std::array<float, 6> weights{1.0f, 0.0f, -1.0f, 0.5f, 0.5f, 0.5f};
                      const std::array<float, 2> bias{10.0f, -1.0f};
                      std::array<float, 2> output{};

                      checks.expect(dense(layer, input, weights, bias, output), "dense accepted");

                      // out[0] = 10 + 1*1 + 0*2 + (-1)*3 = 8
                      // out[1] = -1 + 0.5*1 + 0.5*2 + 0.5*3 = 2
                      const std::array<float, 2> expected{8.0f, 2.0f};
                      expectExact(checks, "dense", output, expected);
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Conv1D
// ---------------------------------------------------------------------------

const mdux::spec::Register conv1dSingleChannel{
    "Conv1D slides a single-channel kernel", "evidence-unit", [] {
        return speclab::Test("ml-kernels-conv1d-single-channel")
            .Given("a length-5 signal and a 3-tap filter with stride 1", [] {})
            .When("the convolution is applied", [] {})
            .Then("each window matches the hand-computed value",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Conv1d,
                                            .activation = Activation::None,
                                            .inLength = 5,
                                            .inChannels = 1,
                                            .outLength = 3,
                                            .outChannels = 1,
                                            .kernelSize = 3,
                                            .stride = 1,
                                            .weights = weightsRef({1, 1, 3}, 3),
                                            .bias = weightsRef({1, 0, 0}, 1)};

                      const std::array<float, 5> input{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
                      const std::array<float, 3> weights{1.0f, 2.0f, 3.0f};
                      const std::array<float, 1> bias{0.5f};
                      std::array<float, 3> output{};

                      checks.expect(conv1d(layer, input, weights, bias, output), "conv1d accepted");

                      // 0.5 + (1*1 + 2*2 + 3*3) = 14.5
                      // 0.5 + (1*2 + 2*3 + 3*4) = 20.5
                      // 0.5 + (1*3 + 2*4 + 3*5) = 26.5
                      const std::array<float, 3> expected{14.5f, 20.5f, 26.5f};
                      expectExact(checks, "conv1d", output, expected);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register conv1dMultiChannelStrided{
    "Conv1D sums across channels and honours stride", "evidence-unit", [] {
        return speclab::Test("ml-kernels-conv1d-multi-channel")
            .Given("a 2-channel signal, a 2-tap filter and stride 2", [] {})
            .When("the convolution is applied", [] {})
            .Then("each output is the sum over both channels of its own window",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Conv1d,
                                            .activation = Activation::None,
                                            .inLength = 4,
                                            .inChannels = 2,
                                            .outLength = 2,
                                            .outChannels = 1,
                                            .kernelSize = 2,
                                            .stride = 2,
                                            .weights = weightsRef({1, 2, 2}, 3),
                                            .bias = weightsRef({1, 0, 0}, 1)};

                      // Channel-major: channel 0 then channel 1.
                      const std::array<float, 8> input{1.0f, 2.0f, 3.0f, 4.0f,
                                                       10.0f, 20.0f, 30.0f, 40.0f};
                      // [outChannel][inChannel][tap]
                      const std::array<float, 4> weights{1.0f, 1.0f, 0.5f, 0.5f};
                      const std::array<float, 1> bias{0.0f};
                      std::array<float, 2> output{};

                      checks.expect(conv1d(layer, input, weights, bias, output), "conv1d accepted");

                      // window 0: (1+2) + 0.5*(10+20) = 3 + 15 = 18
                      // window 1: (3+4) + 0.5*(30+40) = 7 + 35 = 42
                      const std::array<float, 2> expected{18.0f, 42.0f};
                      expectExact(checks, "conv1d strided", output, expected);
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The order test - the one that fails if anybody reorders an accumulation
// ---------------------------------------------------------------------------

const mdux::spec::Register accumulationOrderIsNormative{
    "Conv1D's accumulation order is normative and observable", "evidence-unit", [] {
        return speclab::Test("ml-kernels-accumulation-order")
            .Given("a window where f32 addition is provably non-associative", [] {})
            .When("the documented order and the reverse order are both evaluated", [] {})
            .Then("the kernel produces the documented order's value, and the orders differ",
                  [] {
                      mdux::spec::Checks checks;

                      // 1e-8f is far below ulp(1.0f)/2 = 5.96e-8, so adding it to 1.0f changes
                      // nothing. Seven of them summed first come to 7e-8, which is *above* that
                      // half-ulp and does round 1.0f up. The two orders therefore disagree, which
                      // is what makes this test sensitive rather than decorative.
                      constexpr float tiny = 1e-8f;
                      const LayerDesc layer{.kind = LayerKind::Conv1d,
                                            .activation = Activation::None,
                                            .inLength = 8,
                                            .inChannels = 1,
                                            .outLength = 1,
                                            .outChannels = 1,
                                            .kernelSize = 8,
                                            .stride = 1,
                                            .weights = weightsRef({1, 1, 8}, 3),
                                            .bias = weightsRef({1, 0, 0}, 1)};

                      const std::array<float, 8> input{1.0f, tiny, tiny, tiny,
                                                       tiny, tiny, tiny, tiny};
                      const std::array<float, 8> weights{1.0f, 1.0f, 1.0f, 1.0f,
                                                         1.0f, 1.0f, 1.0f, 1.0f};
                      const std::array<float, 1> bias{0.0f};
                      std::array<float, 1> output{};

                      checks.expect(conv1d(layer, input, weights, bias, output), "conv1d accepted");

                      // Increasing tap index, which is what Kernels.cppm declares normative: the
                      // large value lands first and every tiny addend is then absorbed.
                      float forward = bias[0];
                      for (std::size_t k = 0; k < input.size(); ++k) {
                          forward += weights[k] * input[k];
                      }

                      // The same arithmetic, taps descending.
                      float reverse = bias[0];
                      for (std::size_t k = input.size(); k-- > 0;) {
                          reverse += weights[k] * input[k];
                      }

                      checks.expect(sameBits(forward, 1.0f),
                                    mismatch("forward order", forward, 1.0f));
                      checks.expect(!sameBits(forward, reverse),
                                    std::format("the two orders must differ, both gave 0x{:08X}",
                                                bitsOf(forward)));
                      checks.expect(sameBits(output[0], forward),
                                    mismatch("kernel output", output[0], forward));
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Pooling and flatten
// ---------------------------------------------------------------------------

const mdux::spec::Register poolingKernels{
    "Pooling reduces each channel's windows independently", "evidence-unit", [] {
        return speclab::Test("ml-kernels-pooling")
            .Given("a 2-channel length-4 activation with a width-2 stride-2 window", [] {})
            .When("max and average pooling are applied", [] {})
            .Then("each channel is reduced on its own, in place",
                  [] {
                      mdux::spec::Checks checks;

                      const std::array<float, 8> input{1.0f, 5.0f, 3.0f, 2.0f,
                                                       9.0f, 7.0f, 8.0f, 6.0f};

                      LayerDesc layer{.kind = LayerKind::MaxPool1d,
                                      .activation = Activation::None,
                                      .inLength = 4,
                                      .inChannels = 2,
                                      .outLength = 2,
                                      .outChannels = 2,
                                      .kernelSize = 2,
                                      .stride = 2,
                                      .weights = TensorRef{},
                                      .bias = TensorRef{}};

                      std::array<float, 4> maxOut{};
                      checks.expect(maxPool1d(layer, input, maxOut), "maxPool1d accepted");
                      const std::array<float, 4> expectedMax{5.0f, 3.0f, 9.0f, 8.0f};
                      expectExact(checks, "maxPool1d", maxOut, expectedMax);

                      layer.kind = LayerKind::AvgPool1d;
                      std::array<float, 4> avgOut{};
                      checks.expect(avgPool1d(layer, input, avgOut), "avgPool1d accepted");
                      // (1+5)/2, (3+2)/2, (9+7)/2, (8+6)/2
                      const std::array<float, 4> expectedAvg{3.0f, 2.5f, 8.0f, 7.0f};
                      expectExact(checks, "avgPool1d", avgOut, expectedAvg);

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register flattenPreservesChannelMajorOrder{
    "Flatten preserves the channel-major order a dense layer expects", "evidence-unit", [] {
        return speclab::Test("ml-kernels-flatten")
            .Given("a 2-channel length-3 activation", [] {})
            .When("it is flattened", [] {})
            .Then("the flat vector is the buffer unchanged, channel by channel",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Flatten,
                                            .activation = Activation::None,
                                            .inLength = 3,
                                            .inChannels = 2,
                                            .outLength = 6,
                                            .outChannels = 1,
                                            .kernelSize = 0,
                                            .stride = 0,
                                            .weights = TensorRef{},
                                            .bias = TensorRef{}};

                      const std::array<float, 6> input{1.0f, 2.0f, 3.0f, 40.0f, 50.0f, 60.0f};
                      std::array<float, 6> output{};

                      checks.expect(flatten(layer, input, output), "flatten accepted");
                      expectExact(checks, "flatten", output, input);
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Activations
// ---------------------------------------------------------------------------

const mdux::spec::Register activationsBehave{
    "Activations behave at their defining points", "evidence-unit", [] {
        return speclab::Test("ml-kernels-activations")
            .Given("values around each activation's interesting points", [] {})
            .When("the activations are applied in place", [] {})
            .Then("the defining identities hold exactly",
                  [] {
                      mdux::spec::Checks checks;

                      std::array<float, 4> reluValues{-1.0f, 0.0f, 2.0f, -0.5f};
                      relu(reluValues);
                      const std::array<float, 4> expectedRelu{0.0f, 0.0f, 2.0f, 0.0f};
                      expectExact(checks, "relu", reluValues, expectedRelu);

                      // sigmoid(0) = 1/(1+exp(0)) = 1/2, exactly, whatever expF32 does elsewhere.
                      std::array<float, 1> sigmoidValues{0.0f};
                      sigmoid(sigmoidValues);
                      checks.expect(sameBits(sigmoidValues[0], 0.5f),
                                    mismatch("sigmoid(0)", sigmoidValues[0], 0.5f));

                      // Equal logits give a uniform distribution, and 1/3 is the same float three
                      // times over regardless of rounding.
                      std::array<float, 3> softmaxValues{1.0f, 1.0f, 1.0f};
                      softmax(softmaxValues);
                      checks.expect(sameBits(softmaxValues[0], softmaxValues[1]) &&
                                        sameBits(softmaxValues[1], softmaxValues[2]),
                                    "equal logits give equal probabilities");
                      const float total = (softmaxValues[0] + softmaxValues[1]) + softmaxValues[2];
                      checks.expect(std::fabs(total - 1.0f) < 1e-6f,
                                    std::format("softmax sums to {}", total));

                      // Softmax is shift-invariant, and the max subtraction is what makes a large
                      // shift survive at all rather than saturating expF32.
                      std::array<float, 3> shifted{101.0f, 101.0f, 101.0f};
                      softmax(shifted);
                      checks.expect(sameBits(shifted[0], softmaxValues[0]),
                                    mismatch("softmax shift invariance", shifted[0],
                                             softmaxValues[0]));

                      // Ordering must survive: the largest logit keeps the largest probability.
                      std::array<float, 3> ordered{0.0f, 2.0f, 1.0f};
                      softmax(ordered);
                      checks.expect(ordered[1] > ordered[2] && ordered[2] > ordered[0],
                                    "softmax preserves the ordering of its logits");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register deterministicExponential{
    "expF32 is accurate, saturating, and free of libm", "evidence-unit", [] {
        return speclab::Test("ml-kernels-exp")
            .Given("the hand-written f32 exponential", [] {})
            .When("it is evaluated across its range and at its limits", [] {})
            .Then("it tracks std::exp closely and saturates rather than producing a denormal",
                  [] {
                      mdux::spec::Checks checks;

                      // exp(0) = 1 exactly: the range reduction gives k = 0, r = 0, and the
                      // polynomial's constant term is 1.
                      checks.expect(sameBits(expF32(0.0f), 1.0f),
                                    mismatch("expF32(0)", expF32(0.0f), 1.0f));

                      // Accuracy is checked against std::exp as a reference, with a tolerance -
                      // this is the one place a tolerance is right, because the two are deliberately
                      // *not* required to agree bit for bit. See Kernels.cppm.
                      constexpr std::array<float, 9> samples{-20.0f, -7.5f, -2.5f, -0.333f, 0.1f,
                                                             1.0f,   3.7f,  7.125f, 20.0f};
                      for (float x : samples) {
                          const float actual = expF32(x);
                          const float reference = std::exp(x);
                          const float relative =
                              std::fabs(actual - reference) / std::fabs(reference);
                          checks.expect(relative < 1e-6f,
                                        std::format("expF32({}) = {} vs std::exp {} (rel {})", x,
                                                    actual, reference, relative));
                      }

                      checks.expect(std::isinf(expF32(100.0f)) && expF32(100.0f) > 0.0f,
                                    "expF32 saturates to +inf above its upper limit");
                      checks.expect(sameBits(expF32(-100.0f), 0.0f),
                                    mismatch("expF32(-100)", expF32(-100.0f), 0.0f));
                      checks.expect(std::isnan(expF32(std::numeric_limits<float>::quiet_NaN())),
                                    "NaN propagates");

                      // Monotonicity across the reduction boundaries, where a range-reduction bug
                      // would show up as a step in the wrong direction.
                      bool monotonic = true;
                      float previous = expF32(-5.0f);
                      for (float x = -5.0f + 0.125f; x <= 5.0f; x += 0.125f) {
                          const float current = expF32(x);
                          monotonic = monotonic && (current > previous);
                          previous = current;
                      }
                      checks.expect(monotonic, "expF32 is monotonic across reduction boundaries");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Misuse
// ---------------------------------------------------------------------------

const mdux::spec::Register mismatchedSpansRejected{
    "A span that disagrees with the descriptor is rejected", "evidence-unit", [] {
        return speclab::Test("ml-kernels-span-guard")
            .Given("a valid dense descriptor and buffers of the wrong size", [] {})
            .When("each kernel is called", [] {})
            .Then("it refuses rather than reading past the end",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Dense,
                                            .activation = Activation::None,
                                            .inLength = 3,
                                            .inChannels = 1,
                                            .outLength = 2,
                                            .outChannels = 1,
                                            .kernelSize = 0,
                                            .stride = 0,
                                            .weights = weightsRef({2, 3, 0}, 2),
                                            .bias = weightsRef({2, 0, 0}, 1)};

                      const std::array<float, 3> input{1.0f, 2.0f, 3.0f};
                      const std::array<float, 6> weights{};
                      const std::array<float, 2> bias{};
                      std::array<float, 2> output{};

                      const std::array<float, 2> shortInput{};
                      const std::array<float, 3> shortWeights{};
                      std::array<float, 1> shortOutput{};

                      checks.expect(!dense(layer, shortInput, weights, bias, output),
                                    "short input rejected");
                      checks.expect(!dense(layer, input, shortWeights, bias, output),
                                    "short weight matrix rejected");
                      checks.expect(!dense(layer, input, weights, bias, shortOutput),
                                    "short output rejected");

                      // A kernel called for the wrong layer kind refuses too, so a dispatch bug
                      // surfaces here rather than as silently wrong arithmetic.
                      checks.expect(!conv1d(layer, input, weights, bias, output),
                                    "conv1d refuses a dense descriptor");
                      checks.expect(dense(layer, input, weights, bias, output),
                                    "the correctly-sized call still succeeds");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register applyLayerDispatches{
    "applyLayer runs the kernel and then the activation", "evidence-unit", [] {
        return speclab::Test("ml-kernels-apply-layer")
            .Given("a dense layer carrying a relu", [] {})
            .When("applyLayer runs it", [] {})
            .Then("the activation has been applied to the kernel's output",
                  [] {
                      mdux::spec::Checks checks;

                      const LayerDesc layer{.kind = LayerKind::Dense,
                                            .activation = Activation::Relu,
                                            .inLength = 2,
                                            .inChannels = 1,
                                            .outLength = 2,
                                            .outChannels = 1,
                                            .kernelSize = 0,
                                            .stride = 0,
                                            .weights = weightsRef({2, 2, 0}, 2),
                                            .bias = weightsRef({2, 0, 0}, 1)};

                      const std::array<float, 2> input{1.0f, 1.0f};
                      // Row 0 sums to 3, row 1 sums to -3.
                      const std::array<float, 4> weights{1.0f, 2.0f, -1.0f, -2.0f};
                      const std::array<float, 2> bias{0.0f, 0.0f};
                      std::array<float, 2> output{};

                      checks.expect(applyLayer(layer, input, weights, bias, output),
                                    "applyLayer accepted");
                      const std::array<float, 2> expected{3.0f, 0.0f};
                      expectExact(checks, "applyLayer", output, expected);
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
