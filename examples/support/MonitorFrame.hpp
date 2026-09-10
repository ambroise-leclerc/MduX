/**
 * @file MonitorFrame.hpp
 * @brief Records one monitor frame from a settled `DemoState` (ADR-018 clause 6, steps 3-4).
 *
 * @compliance ADR-004 Trust zones in C++ (examples-support zone)
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 *
 * The frame path `updateMonitor()` feeds: every live binding - the ECG trace, the insufflation
 * pressure, the deterministic clock, the classifier state and the `patient-id` field - is assembled
 * here from one snapshot of caller-owned state, *after* the batch was resolved, so a capture never
 * shows input the operator has not seen resolved.
 *
 * Split out of `MedicalScreenMonitorExample.cpp` (#321, ADR-021) so the interactive window, the
 * headless smoke, the scenario replay and the `mdux-verify-scenario` capture verifier all record a
 * frame through **one** function rather than four copies of it. It is parameterised on the already
 * assembled `ScreenPackage` / `TextBinding` / `ImageBinding` rather than on a bundle type, so a
 * caller that loaded those from the committed artifacts on disk uses the same path as one holding
 * the generated `constexpr` screen.
 *
 * ## Include order
 *
 * Include this header **after** `import std;`, `import mdux.core.result;`, `import mdux.core.units;`,
 * `import mdux.draw;`, `import mdux.font.schema;`, `import mdux.image.schema;`,
 * `import mdux.text.schema;`, `import mdux.medui.input;`, `import mdux.medui.reading;`,
 * `import mdux.medui.schema;`, `import mdux.medui.screen;`, `import mdux.medui.trace;`,
 * `import mdux.render.vulkan;`, `import mdux.shader.generated.mdux_ui;`,
 * `import mdux.medui.generated.screen_endoscope_monitor;`, `#include "MonitorApp.hpp"` and the
 * embedded-blob headers (`dejavuUiPackageJson.hpp`, `dejavuUiAtlas.hpp`,
 * `endoscopeText{EnUs,FrFr}PackageJson.hpp`, `endoscopeText{EnUs,FrFr}Runs.hpp`,
 * `brandMarkPackageJson.hpp`, `brandMarkPixels.hpp`) - it names those without importing them, the
 * way `ScenarioReplay.hpp` names `MonitorApp.hpp`'s symbols. `MedicalScreenMonitorExample` and
 * `mdux-verify-scenario` (#321) both meet this contract.
 */
#pragma once

namespace mdux::examples {

/// The two approved locales of the `endoscope-monitor` screen. The `--locale` flag and the
/// scenario verifier both walk this set.
enum class Locale { EnUs, FrFr };

[[nodiscard]] inline std::string_view localeTag(Locale locale) noexcept {
    return locale == Locale::FrFr ? "fr-FR" : "en-US";
}

/// An embedded blob's bytes as UTF-8 text, for the `*Package::parse()` calls.
[[nodiscard]] inline std::string_view asText(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// The near-black ground the screen is composited over. Shared so the offscreen clear colour a
/// capture is verified against is the one the window presents.
inline constexpr mdux::core::ColorRgba8 kClear{.r = 6, .g = 8, .b = 10, .a = 255};

/**
 * @brief The compiled `endoscope-monitor` screen and the committed packages a device joins it to.
 *
 * Held on the heap for the life of the program and never moved: `TextBinding` / `ImageBinding` keep
 * pointers into `font` / `text` / `image`, so a move would dangle them.
 */
struct BoundScreen {
    mdux::medui::ScreenPackage screen{mdux::medui::generated::screen_endoscope_monitor::package()};
    mdux::font::FontPackage    font{};
    mdux::text::TextPackage    text{};
    mdux::image::ImagePackage  image{};
    mdux::medui::TextBinding   textBinding{};
    mdux::medui::ImageBinding  imageBinding{};

    BoundScreen()                              = default;
    BoundScreen(const BoundScreen&)            = delete;
    BoundScreen& operator=(const BoundScreen&) = delete;

    /// The authored surface extent the screen was compiled for - the `UiRenderer` viewport.
    [[nodiscard]] mdux::core::Extent2D surface() const noexcept {
        return {screen.surfaceWidth, screen.surfaceHeight};
    }

    /// Parses the embedded font / text / image packages for `locale` and builds the two bindings, or
    /// prints why and returns `nullptr`.
    [[nodiscard]] static std::unique_ptr<BoundScreen> load(Locale locale) {
        namespace ms = mdux::medui;
        auto        bound = std::make_unique<BoundScreen>();

        const std::span<const std::byte> textJson =
            locale == Locale::FrFr ? endoscopeTextFrFrPackageJson() : endoscopeTextEnUsPackageJson();
        const std::span<const std::byte> textRuns =
            locale == Locale::FrFr ? endoscopeTextFrFrRuns() : endoscopeTextEnUsRuns();

        auto font  = mdux::font::FontPackage::parse(asText(dejavuUiPackageJson()));
        auto text  = mdux::text::TextPackage::parse(asText(textJson));
        auto image = mdux::image::ImagePackage::parse(asText(brandMarkPackageJson()));
        if (!font || !text || !image) {
            std::cerr << "monitor: a committed package did not parse\n";
            return nullptr;
        }
        bound->font  = std::move(*font);
        bound->text  = std::move(*text);
        bound->image = std::move(*image);

        auto textBinding = ms::TextBinding::create(bound->screen, bound->font, bound->text, textJson, textRuns);
        if (!textBinding) {
            std::cerr << "monitor: the committed text artifacts were refused: " << ms::describe(textBinding.error()) << '\n';
            return nullptr;
        }
        bound->textBinding = *textBinding;

        auto imageBinding = ms::ImageBinding::create(bound->screen, bound->image, brandMarkPackageJson(), brandMarkPixels());
        if (!imageBinding) {
            std::cerr << "monitor: the committed image artifacts were refused: " << ms::describe(imageBinding.error()) << '\n';
            return nullptr;
        }
        bound->imageBinding = *imageBinding;
        return bound;
    }
};

/// A `UiRenderer` for `context` with the committed font coverage atlas and the brand-mark image
/// atlas, so text and the logo draw as themselves rather than white blocks.
[[nodiscard]] inline mdux::core::Result<mdux::render::UiRenderer, mdux::render::RenderError>
makeRenderer(const mdux::render::VulkanRenderContext& context, const BoundScreen& bound) {
    return mdux::render::UiRenderer::createWithAtlases(context,
                                                      mdux::shader::generated::mdux_ui::package(),
                                                      bound.screen.budget,
                                                      dejavuUiAtlas(),
                                                      bound.font.atlas.width,
                                                      bound.font.atlas.height,
                                                      brandMarkPixels(),
                                                      bound.image.width,
                                                      bound.image.height);
}

/// Per-frame draw storage, sized once from the screen's own budget as a device would.
struct FrameStorage {
    std::vector<mdux::draw::UiVertex>    vertices;
    std::vector<mdux::draw::Index>       indices;
    std::vector<mdux::draw::DrawCommand> commands;

    explicit FrameStorage(const mdux::draw::DrawBudget& budget)
        : vertices(budget.maxVertices), indices(budget.maxIndices), commands(budget.maxCommands) {}
};

/**
 * @brief Builds this frame's `DrawList` from the screen and one snapshot of the demonstration state.
 *
 * @param screen       the compiled screen (generated `constexpr`, or one read from `generated/`)
 * @param textBinding  the authenticated text binding for the locale being rendered
 * @param imageBinding the authenticated brand-mark image binding
 * @param storage      caller-owned draw storage sized from `screen.budget`
 * @param state        the settled demonstration state - the ECG ring, the pressure reading, the
 *                     classifier position and the `patient-id` field value
 * @param clock        the deterministic clock, whose `now` the `Clock` node shows
 *
 * Allocation-free past `storage`. Returns the screen runtime's own `ScreenError` on a refused
 * binding or a budget overrun; the caller reports it.
 */
[[nodiscard]] inline mdux::core::Result<mdux::draw::DrawList, mdux::medui::ScreenError>
recordMonitorFrame(const mdux::medui::ScreenPackage& screen,
                   const mdux::medui::TextBinding&   textBinding,
                   const mdux::medui::ImageBinding&  imageBinding,
                   FrameStorage&                     storage,
                   const DemoState&                  state,
                   const MonitorClock&               clock) {
    namespace ms = mdux::medui;

    auto list = mdux::draw::DrawList::create(storage.vertices, storage.indices, storage.commands, screen.budget);
    if (!list) {
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    const ms::SampleRing                traceView = state.ecg.view();
    const std::array<ms::SignalSlot, 1> signalSlots{
        ms::SignalSlot{.streamSource = kTraceStream, .ring = &traceView, .style = monitorTraceStyle}
    };
    ms::SignalBinding signals{};
    if (auto made = ms::SignalBinding::create(screen, signalSlots); made) {
        signals = *made;
    } else {
        return mdux::core::err(made.error());
    }

    const std::array<ms::ReadingSlot, 1> readingSlots{
        ms::ReadingSlot{.nodeId = kPressureNode, .rendering = kPressureRendering, .value = state.pressureTenths}
    };
    ms::ReadingBinding readings{};
    if (auto made = ms::ReadingBinding::create(screen, readingSlots, &clock.now, kClockColorToken); made) {
        readings = *made;
    } else {
        return mdux::core::err(made.error());
    }

    const std::array<ms::StatusSlot, 1> statusSlots{
        ms::StatusSlot{.nodeId = kStatusNode, .state = state.classifierState}
    };
    ms::StatusBinding status{};
    if (auto made = ms::StatusBinding::create(screen, statusSlots); made) {
        status = *made;
    } else {
        return mdux::core::err(made.error());
    }

    std::array<ms::TextInputSlot, 1> inputSlots{};
    ms::TextInputBinding             inputs{};
    if (state.field) {
        inputSlots[0] = ms::TextInputSlot{.nodeId = state.field->nodeId(),
                                          .text   = state.field->value(),
                                          .caret  = state.field->caret()};
        auto made = ms::TextInputBinding::create(screen, inputSlots);
        if (!made) {
            // Fail closed rather than render a deferred field: a monitor that silently drops the
            // patient id it was asked to show is the wrong failure.
            return mdux::core::err(made.error());
        }
        inputs = *made;
    }

    const auto recorded = ms::render(screen, *list, textBinding, imageBinding, signals, readings, status, inputs);
    if (!recorded) {
        return mdux::core::err(recorded.error());
    }
    return std::move(*list);
}

}  // namespace mdux::examples
