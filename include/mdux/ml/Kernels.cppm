/**
 * @file Kernels.cppm
 * @brief Governed-zone ML kernels: the arithmetic both the device runtime and the host baker run.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (noexcept throughout, no throwing)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The single most safety-relevant file in the ML subsystem, and the one imported by *both* the
 * device runtime (issue #62) and the host golden-vector generator (#61). Not a reference
 * implementation and a production implementation - one module, one object file, one set of compile
 * flags. If the host and the device disagree about what a model computes, that is the FPU or the
 * toolchain, because there is no second implementation that could have drifted. See ADR-008,
 * decision 1.
 *
 * ## The determinism rule
 *
 * Every accumulation happens in a single fixed order, using plain `f32` multiply-then-add:
 *
 * - **Conv1D** sums over input channel, then kernel tap.
 * - **Dense** sums over input feature.
 * - **AvgPool1D** sums over the window left to right, then divides by the window width.
 *
 * The accumulator starts at the bias where there is one, so the bias participates first. That is a
 * choice, not a convention inherited from anywhere - and because it is a choice it is **normative**:
 * moving the bias to the end changes the rounding and therefore the result.
 *
 * - **Never `std::fma`.** It rounds once where the scalar sequence rounds twice.
 * - **Never SIMD intrinsics.** Same rounding problem, plus lane-order dependence in the reduction.
 * - Plain readable loops. That is the specification, not a compromise pending optimisation.
 *
 * **Auto-vectorisation is not a hazard here, so do not "fix" it.** A compiler may not reorder a
 * floating-point reduction without `-ffast-math`; `-O2` and `-O3` are safe and nobody should be
 * lowering the optimisation level on these files out of superstition. What *is* a hazard is FP
 * contraction, which is on by default and would fuse `acc += w * x` into an FMA regardless of the
 * rule above - issue #59 sets `-ffp-contract=off` on this target for exactly that reason.
 *
 * ## Why there is a hand-written exponential
 *
 * `expF32()` exists because `std::exp` cannot carry the cross-toolchain claim. Neither the C++
 * standard nor IEEE 754 requires a correctly-rounded `expf`, and glibc and the UCRT are different
 * implementations - so a golden vector containing a `sigmoid` or `softmax` output could differ in
 * its low bits between the Linux and Windows CI legs while both compilers were behaving correctly.
 * That would break `ml.determinism.crossToolchain`, which is the strongest single piece of evidence
 * in epic #18.
 *
 * `expF32()` uses only IEEE-754 `f32` add, subtract, multiply, divide, `floor` and an exponent
 * construction by bit pattern - every one of which is exactly specified, so the result is identical
 * on any conforming toolchain. Accuracy is a few ULP, which is irrelevant to a classifier and is
 * emphatically not the property being maximised: a more accurate result that differs between host
 * and device is worse than a slightly less accurate one that is identical on both.
 *
 * ## Buffer layout, which the baker must match exactly
 *
 * Activations with channels are **channel-major**: element `(c, i)` of a `[channels][length]`
 * buffer is at `c * length + i`. Conv1D weights are `[outChannels][inChannels][kernelSize]`; dense
 * weights are `[outFeatures][inFeatures]`. `Flatten` is therefore a straight copy - the
 * channel-major buffer is already the flat vector a dense layer consumes, in that order.
 *
 * ## What these functions do and do not check
 *
 * Each takes a validated `LayerDesc` and checks only that the spans it was handed match the sizes
 * that descriptor implies, returning `false` if not. That guard runs once per layer, never in an
 * inner loop. It is a misuse check, not a validation layer: `ModelPackage::validate()` is what
 * establishes that the descriptor itself is coherent.
 *
 * No allocation, no recursion, no VLAs, `noexcept` throughout - so the whole file is usable under
 * `-fno-exceptions` and satisfies issue #63's no-heap requirement by construction.
 */
module;

export module mdux.ml.kernels;

import std;
import mdux.ml.schema;

export namespace mdux::ml {

/**
 * @brief A deterministic `f32` exponential, bit-identical across conforming toolchains.
 *
 * See the module comment for why `std::exp` is not used. Range reduction `x = k*ln2 + r` followed
 * by a degree-7 Taylor polynomial on `|r| <= ln2/2`, reconstructed by building `2^k` directly as a
 * bit pattern.
 *
 * Saturates rather than producing a denormal: `x >= 88.0f` returns infinity and `x <= -88.0f`
 * returns zero. Flushing there keeps the exponent construction inside the normal range, where
 * building `2^k` as a bit pattern is exact.
 *
 * The lower limit is genuinely reachable and the saturation is load-bearing, not defensive: after
 * softmax subtracts the maximum every input is <= 0, but a confident model produces logits far
 * apart, so `{0, -100}` reduces to an input of -100. That saturates to zero, which is the correct
 * probability to four decimal places and the correct *bit pattern* on every toolchain - where a
 * denormal result would be neither. The upper limit is the unreachable one: after the subtraction
 * no input exceeds 0.
 *
 * NaN propagates.
 */
[[nodiscard]] float expF32(float x) noexcept;

/**
 * @brief `output[o] = bias[o] + sum over i of weights[o][i] * input[i]`.
 *
 * Accumulates over input feature in increasing order, starting from the bias. Normative - see the
 * module comment.
 *
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool dense(const LayerDesc& layer, std::span<const float> input,
                         std::span<const float> weights, std::span<const float> bias,
                         std::span<float> output) noexcept;

/**
 * @brief 1-D convolution with no padding.
 *
 * `output[oc][ox] = bias[oc] + sum over ic, then k, of weights[oc][ic][k] * input[ic][ox*stride+k]`.
 *
 * Accumulates over input channel first, then kernel tap, starting from the bias. Normative.
 *
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool conv1d(const LayerDesc& layer, std::span<const float> input,
                          std::span<const float> weights, std::span<const float> bias,
                          std::span<float> output) noexcept;

/**
 * @brief Windowed maximum, per channel, with no padding.
 *
 * Scans each window left to right keeping the first strictly-greater value, so a tie takes the
 * earlier element and the traversal order is fixed. A NaN in the window does not displace a
 * previous value, since every comparison against it is false.
 *
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool maxPool1d(const LayerDesc& layer, std::span<const float> input,
                             std::span<float> output) noexcept;

/**
 * @brief Windowed mean, per channel, with no padding.
 *
 * Sums the window left to right, then divides by the window width. Division rather than
 * multiplication by a reciprocal: IEEE division is correctly rounded, and `x * (1/n)` is not the
 * same value.
 *
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool avgPool1d(const LayerDesc& layer, std::span<const float> input,
                             std::span<float> output) noexcept;

/**
 * @brief Reshape `[channels][length]` to a flat vector.
 *
 * A straight copy: the channel-major buffer is already in the order a dense layer consumes. It
 * exists as a kernel rather than being elided so that the layer chain has one uniform shape, and
 * so a future non-contiguous layout has one place to change.
 *
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool flatten(const LayerDesc& layer, std::span<const float> input,
                           std::span<float> output) noexcept;

/// `max(0, x)` in place. A NaN is left as it is: the comparison is false, so nothing replaces it.
void relu(std::span<float> values) noexcept;

/// `1 / (1 + exp(-x))` in place, through expF32().
void sigmoid(std::span<float> values) noexcept;

/**
 * @brief Softmax in place over the whole span.
 *
 * Subtracts the maximum before exponentiating - the standard guard against overflow, and required
 * here because expF32() saturates. Sums in increasing index order, then divides.
 *
 * Applied over the entire output vector, which is only meaningful on a final dense layer. v1 has
 * no notion of a softmax axis.
 */
void softmax(std::span<float> values) noexcept;

/**
 * @brief Applies `activation` in place, dispatching to the functions above.
 *
 * `Activation::None` leaves the span untouched.
 */
void applyActivation(Activation activation, std::span<float> values) noexcept;

/**
 * @brief Runs one layer: the kernel for its kind, then its activation.
 *
 * The single entry point the runtime and the golden generator both call, so neither can dispatch
 * differently from the other.
 *
 * @param weights the layer's weight tensor as floats, empty for a layer that carries none
 * @param bias    the layer's bias tensor as floats, empty for a layer that carries none
 * @return false if any span disagrees with the sizes `layer` implies.
 */
[[nodiscard]] bool applyLayer(const LayerDesc& layer, std::span<const float> input,
                              std::span<const float> weights, std::span<const float> bias,
                              std::span<float> output) noexcept;

}  // namespace mdux::ml
