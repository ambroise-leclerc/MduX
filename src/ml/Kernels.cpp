/**
 * @file Kernels.cpp
 * @brief The v1 kernel set. Read the module comment in Kernels.cppm before changing anything here.
 *
 * @compliance ADR-005 Error handling and exceptions policy (noexcept throughout, no throwing)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Every loop below is written in a fixed order on purpose. The plainness is the specification: a
 * reader has to be able to see the accumulation order, and a golden-vector mismatch has to mean
 * "the toolchain or the FPU differs", not "one of two implementations drifted".
 */
module;

module mdux.ml.kernels;

import std;
import mdux.ml.schema;

namespace mdux::ml {
namespace {

/// Number of floats a tensor holds, or 0 when the layer carries none.
[[nodiscard]] std::size_t tensorFloats(const TensorRef& tensor) noexcept {
    return tensor.present() ? static_cast<std::size_t>(tensor.elementCount()) : 0;
}

/// The size guard every kernel runs once, before touching an element. See Kernels.cppm on why this
/// is a misuse check rather than a validation layer.
[[nodiscard]] bool spansMatch(const LayerDesc& layer, std::span<const float> input,
                              std::span<const float> weights, std::span<const float> bias,
                              std::span<float> output) noexcept {
    return input.size() == layer.inputFloats() && output.size() == layer.outputFloats() &&
           weights.size() == tensorFloats(layer.weights) && bias.size() == tensorFloats(layer.bias);
}

// --- expF32 constants -------------------------------------------------------
//
// ln2 is split so the range reduction stays accurate: ln2Hi is exactly representable in f32 (its
// low mantissa bits are zero), so `kf * ln2Hi` is exact for the small integers k takes here, and
// the whole rounding error of the reduction lives in the much smaller ln2Lo term.

constexpr float invLn2 = 1.44269504088896340736f;
constexpr float ln2Hi = 0.693145751953125f;         // 0x3F317200, exact in f32
constexpr float ln2Lo = 1.42860676533018e-06f;      // the remainder of ln2
constexpr float expUpperLimit = 88.0f;              // above this an f32 result would overflow
constexpr float expLowerLimit = -88.0f;             // below this the result is flushed to zero

}  // namespace

float expF32(float x) noexcept {
    // NaN first, and not merely as an optimisation. Without this guard the comparisons below are
    // all false, so control would reach the range reduction, and `static_cast<int>` of a NaN is
    // undefined behaviour - not a harmless zero. NaN handling is part of the determinism story,
    // so it is decided here rather than left to whatever the cast happens to do.
    if (x != x) {
        return x;
    }
    if (x >= expUpperLimit) {
        return std::numeric_limits<float>::infinity();
    }
    if (x <= expLowerLimit) {
        return 0.0f;
    }

    // x = k*ln2 + r, with |r| <= ln2/2.
    //
    // The floor is done by integer truncation rather than std::floor. Both are exact, but
    // std::floor is a libm entry point, and ADR-008 claims this function removes libm from the
    // device-side dependency argument - a claim that would be false with a libm call in it. The
    // limits above bound the argument to roughly +/-127, so the conversion cannot overflow int.
    const float scaled = x * invLn2 + 0.5f;
    int k = static_cast<int>(scaled);  // truncates toward zero
    if (static_cast<float>(k) > scaled) {
        --k;  // trunc == floor for non-negative values; below zero it is one too high
    }
    const float kf = static_cast<float>(k);
    const float r = (x - kf * ln2Hi) - kf * ln2Lo;

    // exp(r) by Taylor series, Horner form, in this order. Degree 7 leaves the truncation error
    // near 5e-9 relative over |r| <= 0.3466, comfortably below f32 epsilon.
    constexpr float c2 = 1.0f / 2.0f;
    constexpr float c3 = 1.0f / 6.0f;
    constexpr float c4 = 1.0f / 24.0f;
    constexpr float c5 = 1.0f / 120.0f;
    constexpr float c6 = 1.0f / 720.0f;
    constexpr float c7 = 1.0f / 5040.0f;

    float poly = c7;
    poly = poly * r + c6;
    poly = poly * r + c5;
    poly = poly * r + c4;
    poly = poly * r + c3;
    poly = poly * r + c2;
    poly = poly * r + 1.0f;
    poly = poly * r + 1.0f;

    // 2^k built directly as a bit pattern rather than via std::ldexp, which is another libm entry
    // point this function exists to avoid depending on. k is bounded by the limits above, so the
    // biased exponent is always in the normal range and this is exact.
    const std::uint32_t biased = static_cast<std::uint32_t>(k + 127) << 23;
    const float scale = std::bit_cast<float>(biased);

    return poly * scale;
}

bool dense(const LayerDesc& layer, std::span<const float> input, std::span<const float> weights,
           std::span<const float> bias, std::span<float> output) noexcept {
    if (layer.kind != LayerKind::Dense || !spansMatch(layer, input, weights, bias, output)) {
        return false;
    }

    const std::size_t inFeatures = layer.inLength;
    const std::size_t outFeatures = layer.outLength;

    for (std::size_t o = 0; o < outFeatures; ++o) {
        // The accumulator starts at the bias, so the bias participates first. Normative.
        float acc = bias[o];
        const std::size_t row = o * inFeatures;
        // Sums over input feature, increasing. Normative.
        for (std::size_t i = 0; i < inFeatures; ++i) {
            acc += weights[row + i] * input[i];
        }
        output[o] = acc;
    }
    return true;
}

bool conv1d(const LayerDesc& layer, std::span<const float> input, std::span<const float> weights,
            std::span<const float> bias, std::span<float> output) noexcept {
    if (layer.kind != LayerKind::Conv1d || !spansMatch(layer, input, weights, bias, output)) {
        return false;
    }

    const std::size_t inLength = layer.inLength;
    const std::size_t inChannels = layer.inChannels;
    const std::size_t outLength = layer.outLength;
    const std::size_t outChannels = layer.outChannels;
    const std::size_t kernelSize = layer.kernelSize;
    const std::size_t stride = layer.stride;

    for (std::size_t oc = 0; oc < outChannels; ++oc) {
        const std::size_t filterBase = oc * inChannels * kernelSize;
        for (std::size_t ox = 0; ox < outLength; ++ox) {
            const std::size_t windowStart = ox * stride;
            // Bias first, then input channel, then kernel tap. Normative - see Kernels.cppm.
            float acc = bias[oc];
            for (std::size_t ic = 0; ic < inChannels; ++ic) {
                const std::size_t weightBase = filterBase + ic * kernelSize;
                const std::size_t inputBase = ic * inLength + windowStart;
                for (std::size_t k = 0; k < kernelSize; ++k) {
                    acc += weights[weightBase + k] * input[inputBase + k];
                }
            }
            output[oc * outLength + ox] = acc;
        }
    }
    return true;
}

bool maxPool1d(const LayerDesc& layer, std::span<const float> input,
               std::span<float> output) noexcept {
    if (layer.kind != LayerKind::MaxPool1d || !spansMatch(layer, input, {}, {}, output)) {
        return false;
    }

    const std::size_t inLength = layer.inLength;
    const std::size_t outLength = layer.outLength;
    const std::size_t kernelSize = layer.kernelSize;
    const std::size_t stride = layer.stride;

    for (std::size_t c = 0; c < layer.inChannels; ++c) {
        const std::size_t inputBase = c * inLength;
        for (std::size_t ox = 0; ox < outLength; ++ox) {
            const std::size_t windowStart = inputBase + ox * stride;
            float best = input[windowStart];
            // Strictly-greater keeps the earliest of equal values and leaves a NaN in place,
            // because every comparison against a NaN is false. Fixed left-to-right order.
            for (std::size_t k = 1; k < kernelSize; ++k) {
                const float candidate = input[windowStart + k];
                if (candidate > best) {
                    best = candidate;
                }
            }
            output[c * outLength + ox] = best;
        }
    }
    return true;
}

bool avgPool1d(const LayerDesc& layer, std::span<const float> input,
               std::span<float> output) noexcept {
    if (layer.kind != LayerKind::AvgPool1d || !spansMatch(layer, input, {}, {}, output)) {
        return false;
    }

    const std::size_t inLength = layer.inLength;
    const std::size_t outLength = layer.outLength;
    const std::size_t kernelSize = layer.kernelSize;
    const std::size_t stride = layer.stride;
    const float window = static_cast<float>(kernelSize);

    for (std::size_t c = 0; c < layer.inChannels; ++c) {
        const std::size_t inputBase = c * inLength;
        for (std::size_t ox = 0; ox < outLength; ++ox) {
            const std::size_t windowStart = inputBase + ox * stride;
            float acc = 0.0f;
            // Left to right. Normative.
            for (std::size_t k = 0; k < kernelSize; ++k) {
                acc += input[windowStart + k];
            }
            // Division, not multiplication by a reciprocal - see Kernels.cppm.
            output[c * outLength + ox] = acc / window;
        }
    }
    return true;
}

bool flatten(const LayerDesc& layer, std::span<const float> input,
             std::span<float> output) noexcept {
    if (layer.kind != LayerKind::Flatten || !spansMatch(layer, input, {}, {}, output)) {
        return false;
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = input[i];
    }
    return true;
}

void relu(std::span<float> values) noexcept {
    for (float& value : values) {
        // Not std::max: a NaN must survive, and max's argument order would decide that silently.
        if (value < 0.0f) {
            value = 0.0f;
        }
    }
}

void sigmoid(std::span<float> values) noexcept {
    for (float& value : values) {
        value = 1.0f / (1.0f + expF32(-value));
    }
}

void softmax(std::span<float> values) noexcept {
    if (values.empty()) {
        return;
    }

    // Subtract the maximum before exponentiating: the standard overflow guard, and required here
    // because expF32() saturates at +/-88.
    float largest = values[0];
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] > largest) {
            largest = values[i];
        }
    }

    float total = 0.0f;
    for (float& value : values) {
        value = expF32(value - largest);
        total += value;  // increasing index order, normative
    }

    if (total == 0.0f) {
        // Every input saturated to zero, which only happens for a degenerate span. A uniform
        // distribution is the honest answer and keeps the output a probability vector rather than
        // returning NaNs from a division by zero.
        const float uniform = 1.0f / static_cast<float>(values.size());
        for (float& value : values) {
            value = uniform;
        }
        return;
    }

    for (float& value : values) {
        value = value / total;
    }
}

void applyActivation(Activation activation, std::span<float> values) noexcept {
    switch (activation) {
        case Activation::None:
            break;
        case Activation::Relu:
            relu(values);
            break;
        case Activation::Sigmoid:
            sigmoid(values);
            break;
        case Activation::Softmax:
            softmax(values);
            break;
    }
}

bool applyLayer(const LayerDesc& layer, std::span<const float> input,
                std::span<const float> weights, std::span<const float> bias,
                std::span<float> output) noexcept {
    bool ok = false;
    switch (layer.kind) {
        case LayerKind::Dense:
            ok = dense(layer, input, weights, bias, output);
            break;
        case LayerKind::Conv1d:
            ok = conv1d(layer, input, weights, bias, output);
            break;
        case LayerKind::MaxPool1d:
            ok = maxPool1d(layer, input, output);
            break;
        case LayerKind::AvgPool1d:
            ok = avgPool1d(layer, input, output);
            break;
        case LayerKind::Flatten:
            ok = flatten(layer, input, output);
            break;
    }
    if (!ok) {
        return false;
    }
    applyActivation(layer.activation, output);
    return true;
}

}  // namespace mdux::ml
