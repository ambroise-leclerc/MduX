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
 * 4. **Weights are data.** The package metadata and corresponding weight path share one configured
 *    model id. The weight-swap test (tests/ml/WeightSwapTests.cpp) exercises the two committed
 *    packages dynamically without changing this device example.
 *
 * ## 5. One ring, two readers - which is what #257 came here to demonstrate
 *
 * This file used to say that "the `.medui` screen and the `SignalTrace` widget issue #64 describes
 * do not exist yet", and that when they landed the demonstration "is a matter of giving them the
 * same buffer". It landed, and that is exactly what happens below: `EndoscopeMonitor` - the
 * committed screen this repository compiles, whose `SignalTrace` is named `ECG_LEAD_II` - is bound
 * to the same `SampleRing` the classifier reads its window out of, and a frame is recorded from it.
 *
 * The demonstration is the *identity*, not the picture. Nothing copies the samples on the way to
 * either reader: `readWindow()` hands the classifier a contiguous ordered window and `view()` hands
 * the trace a description of the same storage, so there is no second buffer that could drift from
 * the first. A monitor showing a waveform beside a classification derived from different samples is
 * a specific and very bad failure, and this arrangement makes it unspellable rather than unlikely.
 *
 * Still no Vulkan and no window. A draw list is geometry - the vertices, indices and commands a
 * renderer would consume - and building one needs neither a device nor a surface, which is the
 * same thing `MedicalUiExample` demonstrates from the other end.
 *
 * ## One thing it deliberately does not do yet
 *
 * The classifier's output does not drive a `StatusIndicator`: that component is still deferred by
 * the runtime and arrives with #259.
 *
 * The package emitter is the equivalent of the shader pipeline's issue #121: the committed JSON is
 * mechanically rendered into build-tree source and validated by `static_assert`. Only the weight
 * blob remains runtime data.
 */

import std;
import mdux.core.units;
import mdux.draw;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.ml.schema;
import mdux.ml.runtime;
import mdux.ml.generated.model_ecg_demo;

#include "ecgModelWeights.hpp"

namespace {

namespace ml    = mdux::ml;
namespace medui = mdux::medui;
namespace draw  = mdux::draw;

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
    /// @brief Adds one sample, overwriting the oldest sample after the ring fills.
    void push(float sample) noexcept {
        samples_[head_] = sample;
        head_           = (head_ + 1) % Capacity;
        if (filled_ < Capacity) {
            ++filled_;
        }
    }

    /// @brief Reports whether the ring contains one complete classifier window.
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

    /// The same samples as the governed trace expansion reads them: a description, not a copy.
    ///
    /// `head_` is where the *next* sample goes, which is also the oldest live one once the ring has
    /// wrapped - and is not the oldest before it has, when the samples start at zero and stop at
    /// `filled_`. Both cases are spelled out rather than left to the full() path alone, because a
    /// trace is drawn from the first frame of a device's life and the classifier only from the
    /// moment its window is complete.
    [[nodiscard]] medui::SampleRing view() const noexcept {
        return medui::SampleRing{.storage = samples_, .oldest = full() ? head_ : 0, .count = filled_};
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

/// The committed screen, as generated code holds it: read-only data its own emitter `static_assert`ed.
constexpr medui::ScreenPackage endoscopeMonitor = medui::generated::screen_endoscope_monitor::package();
static_assert(endoscopeMonitor.validate().has_value(), "the committed screen's generated form validates");

/// Storage for one frame, sized once from the screen's own budget - and by it, so the two cannot
/// drift. This is the "pre-sized vertex budget" #257 writes into: made here, never grown, and large
/// enough for the trace because the budget the product declared says so.
struct Frame {
    std::array<draw::UiVertex, endoscopeMonitor.budget.maxVertices>    vertices{};
    std::array<draw::Index, endoscopeMonitor.budget.maxIndices>        indices{};
    std::array<draw::DrawCommand, endoscopeMonitor.budget.maxCommands> commands{};
};

/// The scale this demonstrator's synthetic samples are read against.
///
/// The screen cannot carry these numbers and should not: what a sample of `ECG_LEAD_II` means is a
/// property of the amplifier the host owns, not of the layout. Here the "amplifier" is
/// `syntheticSample()`, whose output lies in [-0.5, 1.0], and the band is widened a little either
/// side so an excursion is visible as an excursion rather than as a reading against the rail.
constexpr medui::TraceStyle ecgTrace{.minimum = -1.0F, .maximum = 1.5F, .strokeWidth = 2};

}  // namespace

/// @brief Runs the synthetic ECG classifier demonstrator.
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

    // 4. The same ring, read as a waveform by the governed screen runtime (#257).
    //
    // Nothing is copied between step 3 and this one. `ring` is the object the loop above pushed
    // samples into and `readWindow()` classified out of; `ring.view()` describes that same storage
    // to the trace expansion. Whatever the classifier just said, it said about these samples.
    std::println("");
    std::println("Frame from the committed screen '{}' ({}x{}):", endoscopeMonitor.id, endoscopeMonitor.surfaceWidth, endoscopeMonitor.surfaceHeight);

    const medui::SampleRing view = ring.view();
    const std::array<medui::SignalSlot, 1> slots{
        medui::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &view, .style = ecgTrace}
    };

    auto binding = medui::SignalBinding::create(endoscopeMonitor, slots);
    if (!binding.has_value()) {
        // Fail-closed here too, and for the same class of reason `Classifier1D::create()` is: a
        // stream name this screen does not carry is a trace that would silently stay empty.
        std::println(std::cerr, "signal binding refused: {}", medui::describe(binding.error()));
        return 1;
    }

    // Sized once, from the screen's own budget, and never grown - which is what makes the no-heap
    // property true of the frame rather than of an intention. Heap-allocated because a demonstrator
    // on a desktop has a heap; on a device this is a static object, and either way `render()` never
    // touches an allocator. See tests/medui/ScreenNoHeapTests.cpp.
    auto frame = std::make_unique<Frame>();
    auto list  = draw::DrawList::create(frame->vertices, frame->indices, frame->commands, endoscopeMonitor.budget);
    if (!list.has_value()) {
        std::println(std::cerr, "draw list refused: {}", draw::describe(list.error()));
        return 1;
    }

    const auto recorded = medui::render(endoscopeMonitor, *list, {}, {}, *binding);
    if (!recorded.has_value()) {
        std::println(std::cerr, "frame refused: {}", medui::describe(recorded.error()));
        return 1;
    }

    std::println("  samples bound  : {} (the window the classifier read, not a copy of it)", view.count);
    std::println("  traces expanded: {}", recorded->traces);
    std::println("  nodes / drawn  : {} visited, {} rectangles, {} deferred", recorded->nodes, recorded->rects, recorded->deferred);
    std::println("  budget used    : {}/{} vertices, {}/{} indices, {}/{} commands",
                 list->vertices().size(),
                 endoscopeMonitor.budget.maxVertices,
                 list->indices().size(),
                 endoscopeMonitor.budget.maxIndices,
                 list->commands().size(),
                 endoscopeMonitor.budget.maxCommands);
    std::println("");
    std::println("  The label, the image and the video surface are deferred: they need a bound text");
    std::println("  package and packages this repository does not yet bake. The waveform does not.");

    std::println("");
    std::println("Swapping these weights is a re-bake of the recipe and a change to the configured");
    std::println("model package - not this source file. See #64's test.");
    return 0;
}
