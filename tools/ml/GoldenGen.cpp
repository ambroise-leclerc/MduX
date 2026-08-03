/**
 * @file GoldenGen.cpp
 * @brief Implementation of golden-vector generation.
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 */
module;

module mdux.tools.ml.goldengen;

import std;
import mdux.core.result;
import mdux.ml.schema;
import mdux.ml.kernels;

namespace mdux::tools::ml {

using mdux::core::err;
namespace ml = mdux::ml;

std::string_view describe(GoldenError error) noexcept {
    switch (error) {
        case GoldenError::NoGoldens:       return "a model package must carry at least one golden";
        case GoldenError::NoLayers:        return "a model package must declare at least one layer";
        case GoldenError::ScratchTooSmall: return "maxScratchFloats does not cover the layer chain";
        case GoldenError::KernelRejected:  return "a kernel rejected the shapes the package implied";
    }
    return "unknown golden generation error";
}

namespace {

/// The generator named by goldenPrngAlgorithm. Constants written out rather than referenced, so
/// this file alone determines the bytes - see GoldenGen.cppm on why <random> is not used.
class Lcg {
public:
    explicit constexpr Lcg(std::uint32_t seed) noexcept : state_{seed} {}

    [[nodiscard]] constexpr std::uint32_t next() noexcept {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    /// A value in [-1, 1). Division by a power of two, numerator below 2^24, so both the
    /// conversion and the division are exact in f32 on any conforming implementation.
    [[nodiscard]] constexpr float nextUnit() noexcept {
        return static_cast<float>(next() >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

/// Fills `input` with golden pattern `index`. The fixed patterns come first; see GoldenGen.cppm.
void fillInput(std::span<float> input, std::size_t index, Lcg& rng) noexcept {
    switch (index) {
        case 0:
            std::ranges::fill(input, 0.0f);
            break;
        case 1:
            std::ranges::fill(input, 1.0f);
            break;
        case 2:
            // Alternating +/-1: the sharpest edge a 1-D kernel can see, so a convolution that
            // dropped a tap shows up immediately.
            for (std::size_t i = 0; i < input.size(); ++i) {
                input[i] = (i % 2 == 0) ? 1.0f : -1.0f;
            }
            break;
        case 3: {
            // A ramp over [-1, 1). The divisor is the length rather than length-1 so the step is
            // exact for a power-of-two length and the endpoint is never 1.0 by rounding.
            const float span = static_cast<float>(input.size());
            for (std::size_t i = 0; i < input.size(); ++i) {
                input[i] = static_cast<float>(i) / span * 2.0f - 1.0f;
            }
            break;
        }
        default:
            for (float& value : input) {
                value = rng.nextUnit();
            }
            break;
    }
}

}  // namespace

mdux::core::Result<std::vector<GeneratedGolden>, GoldenError> generateGoldens(
    std::span<const ml::LayerDesc> layers, std::span<const std::byte> weights,
    std::uint32_t inputLength, std::uint32_t outputLength, std::uint32_t maxScratchFloats,
    std::size_t count, std::uint32_t seed) {
    if (count == 0) {
        return err(GoldenError::NoGoldens);
    }
    // Guarded here rather than relying on the caller. With an empty span requiredScratchFloats()
    // returns 0, so the scratch check below passes, and the loop then indexes layers[0] - reading
    // off the end. Every current caller validates first; a function that is only safe because of
    // what its callers happen to do is one refactor from being unsafe.
    if (layers.empty()) {
        return err(GoldenError::NoLayers);
    }
    if (maxScratchFloats < ml::requiredScratchFloats(layers, inputLength)) {
        return err(GoldenError::ScratchTooSmall);
    }

    // Resolve every tensor to a float span over the blob, exactly as Classifier1D::create() does.
    // Host-tools zone, so this copies into an aligned vector rather than requiring the caller's
    // buffer to be aligned - the runtime's stricter contract is not needed here.
    // Assembled element by element rather than with std::memcpy. Two reasons: it matches how
    // Classifier1D reads the same blob, and GCC 16.0.1 ICEs (nonnull_arg_p) on a memcpy from a
    // span whose data() it cannot prove non-null. std::bit_cast of four bytes in memory order is
    // the same reinterpretation, spelled without a pointer the optimiser has to reason about.
    std::vector<float> weightFloats(weights.size() / sizeof(float), 0.0f);
    for (std::size_t i = 0; i < weightFloats.size(); ++i) {
        const std::array<std::byte, 4> quad{weights[i * 4], weights[i * 4 + 1],
                                            weights[i * 4 + 2], weights[i * 4 + 3]};
        weightFloats[i] = std::bit_cast<float>(quad);
    }

    struct Tensors {
        std::span<const float> weights;
        std::span<const float> bias;
    };
    std::vector<Tensors> tensors(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].weights.present()) {
            tensors[i].weights =
                std::span<const float>{weightFloats.data() + layers[i].weights.byteOffset / 4,
                                       static_cast<std::size_t>(layers[i].weights.elementCount())};
        }
        if (layers[i].bias.present()) {
            tensors[i].bias =
                std::span<const float>{weightFloats.data() + layers[i].bias.byteOffset / 4,
                                       static_cast<std::size_t>(layers[i].bias.elementCount())};
        }
    }

    const std::size_t width = maxScratchFloats / 2;
    std::vector<float> scratch(maxScratchFloats, 0.0f);
    std::span<float> bufferA{scratch.data(), width};
    std::span<float> bufferB{scratch.data() + width, width};

    Lcg rng{seed};
    std::vector<GeneratedGolden> goldens;
    goldens.reserve(count);

    for (std::size_t g = 0; g < count; ++g) {
        fillInput(bufferA.first(inputLength), g, rng);

        GeneratedGolden golden;
        golden.inputBits.reserve(inputLength);
        for (std::uint32_t i = 0; i < inputLength; ++i) {
            golden.inputBits.push_back(std::bit_cast<std::uint32_t>(bufferA[i]));
        }

        // The same ping-pong the runtime uses, through the same applyLayer().
        std::span<const float> current =
            bufferA.first(static_cast<std::size_t>(layers[0].inputFloats()));
        for (std::size_t i = 0; i < layers.size(); ++i) {
            std::span<float> destination =
                ((i % 2) == 0 ? bufferB : bufferA)
                    .first(static_cast<std::size_t>(layers[i].outputFloats()));
            if (!ml::applyLayer(layers[i], current, tensors[i].weights, tensors[i].bias,
                                destination)) {
                return err(GoldenError::KernelRejected);
            }
            current = destination;
        }

        golden.expectedOutputBits.reserve(outputLength);
        for (std::uint32_t i = 0; i < outputLength; ++i) {
            golden.expectedOutputBits.push_back(std::bit_cast<std::uint32_t>(current[i]));
        }
        goldens.push_back(std::move(golden));
    }

    return goldens;
}

}  // namespace mdux::tools::ml
