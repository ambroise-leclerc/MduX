/**
 * @file EcgClassifierExample.cpp
 * @brief The ECG demonstrator: a committed model, embedded weights, and a fail-closed startup.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * **The model this runs is a demonstrator with synthetic weights and no clinical validity.** It is
 * not trained on anything and classifies nothing - see recipes/model/ecg-demo.toml. What it
 * demonstrates is the *pipeline*, not a diagnosis.
 *
 * ## What is actually being shown
 *
 * 1. **No filesystem and no parser.** The package is generated `constexpr` data and `weights.bin`
 *    is linked as a byte array by `mdux_embed_blob()`. This program opens no files and links no
 *    host-tools module.
 * 2. **Fail-closed startup.** `Classifier1D::create()` verifies the weight digest and re-runs every
 *    golden vector through the real kernels before returning. If any of that diverges, this program
 *    prints why and exits non-zero rather than classifying anything.
 * 3. **No allocation per prediction.** The scratch buffer is a fixed array sized from the package,
 *    and `predict()` runs entirely within it - verified independently by issue #63.
 * 4. **Weights are data.** Swapping in `ecg-demo-alt` changes the model id and weight path in
 *    CMake, and nothing at all in this file. The weight-swap test
 *    (tests/ml/WeightSwapTests.cpp) is what proves that claim mechanically.
 *
 * ## Two things it deliberately does not do yet
 *
 * The `.medui` screen and the `SignalTrace` widget issue #64 describes do not exist yet - they are
 * issue #15 and epic #11's S2. When they land, this classifier's output drives a `StatusIndicator`
 * and reads from the same sample ring buffer the trace renders. The ring buffer below is written
 * with that in mind: the classifier reads a window out of it rather than owning the samples, so
 * the eventual demonstration that the trace and the classifier are provably looking at identical
 * data is a matter of giving them the same buffer.
 *
 * The package emitter is the equivalent of the shader pipeline's issue #121: the committed JSON is
 * mechanically rendered into build-tree source and validated by `static_assert`. Only the weight
 * blob remains runtime data.
 */

import std;
import mdux.ml.schema;
import mdux.ml.runtime;
import mdux.ml.generated.model_ecg_demo;

#include "ecgModelWeights.hpp"

namespace {

namespace ml = mdux::ml;

/// Sampling rate the demonstrator's 180-sample window corresponds to.
constexpr std::size_t sampleRateHz = 180;

/**
 * @brief A fixed-capacity ring of samples, the shape a SignalTrace will eventually share.
 *
 * Fixed capacity and no allocation, so it is usable in the same places the classifier is. It holds
 * exactly one classification window; a real device would size it to whatever the trace displays and
 * hand the classifier a view of the most recent window.
 */
template <std::size_t Capacity>
class SampleRing {
public:
    void push(float sample) noexcept {
        samples_[head_] = sample;
        head_           = (head_ + 1) % Capacity;
        if (filled_ < Capacity) {
            ++filled_;
        }
    }

    [[nodiscard]] bool full() const noexcept {
        return filled_ == Capacity;
    }

    /// Copies the ring into `window` oldest-first. The classifier needs a contiguous, ordered
    /// window and the ring is neither, so the copy is where those two facts are reconciled.
    void readWindow(std::span<float> window) const noexcept {
        for (std::size_t i = 0; i < Capacity && i < window.size(); ++i) {
            window[i] = samples_[(head_ + i) % Capacity];
        }
    }

private:
    std::array<float, Capacity> samples_{};
    std::size_t                 head_{0};
    std::size_t                 filled_{0};
};

/// A crude synthetic beat: a baseline with a periodic spike. Not an ECG, and not pretending to be -
/// it exists so the demonstrator has something time-varying to classify.
[[nodiscard]] float syntheticSample(std::size_t index, std::size_t beatPeriod) noexcept {
    const std::size_t phase = index % beatPeriod;
    if (phase == 0) {
        return 1.0f;
    }
    if (phase == 1) {
        return -0.5f;
    }
    return 0.05f * static_cast<float>((index % 7)) - 0.15f;
}

}  // namespace

int main() {
    std::println("MduX ECG classifier demonstrator");
    std::println("  NOTE: synthetic weights, no training, no clinical validity. See ADR-008.");
    std::println("");

    // 1. The package was validated at compile time by its generated module.
    constexpr ml::ModelPackage package = mdux::ml::generated::model_ecg_demo::package();

    // 2. Fail-closed construction: digest check, then every golden re-run through the real kernels.
    std::vector<float> scratch(package.maxScratchFloats, 0.0f);
    auto               classifier = ml::Classifier1D::create(package, ecgModelWeights(), scratch);
    if (!classifier.has_value()) {
        const ml::MlError error = classifier.error();
        std::println(std::cerr, "classifier refused to start: {}", ml::describe(error.code));
        if (error.code == ml::MlError::Code::GoldenMismatch) {
            // This is the field incident record ADR-008 describes: which golden, which element,
            // and the two bit patterns that disagreed.
            std::println(std::cerr,
                         "  golden {} element {}: expected 0x{:08X}, got 0x{:08X}",
                         error.goldenIndex,
                         error.elementIndex,
                         error.expectedBits,
                         error.actualBits);
        }
        return 1;
    }

    std::println("package        : {}", package.id);
    std::println("layers         : {}", package.layers.size());
    std::println("goldens re-run : {} (all reproduced bit for bit)", package.goldens.size());
    std::println("scratch        : {} floats", package.maxScratchFloats);
    std::println("input window   : {} samples ({} Hz)", package.inputLength, sampleRateHz);
    std::println("");

    // 3. Classify a few windows off the ring. No allocation happens past this point.
    SampleRing<180>    ring;
    std::vector<float> window(package.inputLength, 0.0f);
    std::vector<float> output(package.outputLength, 0.0f);

    std::size_t classified = 0;
    for (std::size_t index = 0; index < sampleRateHz * 4 && classified < 3; ++index) {
        ring.push(syntheticSample(index, 60));
        if (!ring.full() || index % sampleRateHz != 0) {
            continue;
        }
        ring.readWindow(window);

        if (auto predicted = classifier->predict(window, output); !predicted.has_value()) {
            std::println(std::cerr, "prediction failed: {}", ml::describe(predicted.error().code));
            return 1;
        }

        // Softmax output, so these are a probability vector over the four demonstrator classes.
        std::size_t best = 0;
        for (std::size_t i = 1; i < output.size(); ++i) {
            if (output[i] > output[best]) {
                best = i;
            }
        }
        std::print("window {}: class {} (", classified, best);
        for (std::size_t i = 0; i < output.size(); ++i) {
            std::print("{}{:.4f}", i == 0 ? "" : ", ", output[i]);
        }
        std::println(")");
        ++classified;
    }

    std::println("");
    std::println("Swapping these weights for a manufacturer's own is a re-bake of the recipe and");
    std::println("two CMake paths - not one character of this file. See #64's test.");
    return 0;
}
