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
 * frame through **one** function. It is parameterised on the already assembled `ScreenPackage` /
 * `TextBinding` / `ImageBinding`, so a caller that loaded those from the committed artifacts on disk
 * (the verifier, #321) uses the same path as one holding the generated `constexpr` screen (the
 * example). Nothing here names an embedded blob or a generated module - `BoundScreen` and
 * `makeRenderer`, which do, are in `MonitorScreen.hpp`, which only the example includes.
 *
 * ## Include order
 *
 * Include this header **after** `import std;`, `import mdux.core.result;`, `import mdux.core.units;`,
 * `import mdux.draw;`, `import mdux.medui.input;`, `import mdux.medui.reading;`,
 * `import mdux.medui.schema;`, `import mdux.medui.screen;`, `import mdux.medui.trace;` and
 * `#include "MonitorApp.hpp"` - it names those without importing them.
 */
#pragma once

namespace mdux::examples {

/// The two approved locales of the `endoscope-monitor` screen. The `--locale` flag and the
/// scenario verifier both walk this set.
enum class Locale { EnUs, FrFr };

[[nodiscard]] inline std::string_view localeTag(Locale locale) noexcept {
    return locale == Locale::FrFr ? "fr-FR" : "en-US";
}

/// The near-black ground the screen is composited over. Shared so the offscreen clear colour a
/// capture is verified against is the one the window presents.
inline constexpr mdux::core::ColorRgba8 kClear{.r = 6, .g = 8, .b = 10, .a = 255};

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
