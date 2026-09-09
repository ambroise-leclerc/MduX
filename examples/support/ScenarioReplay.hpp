/**
 * @file ScenarioReplay.hpp
 * @brief Drives a compiled `CompiledScenario` through the monitor's real `updateMonitor()` loop
 *        (#320, ADR-020 §3).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone)
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * The glue between the governed `mdux::medui::ScenarioRunner` (which walks a scenario and checks
 * typed expectations but touches no application) and `examples/support/MonitorApp.hpp`'s
 * `updateMonitor()`. One replay turn: the runner fills the caller-owned `EventQueue` with the next
 * batch, the loop runs that batch's `updateMonitor()` calls, and the settled `DemoState` /
 * `PressLatch` / `MonitorUpdateOutcome` become a `ScenarioObservation` the runner checks. A
 * declared `capture` marker is handed to the caller's `CaptureFn` (an offscreen render in the
 * example, a counter in the GPU-free test).
 *
 * **No GPU is required.** `updateMonitor()` and the demonstration state are `std` and
 * `mdux.medui.*`; `frameObserver` is the caller's, and is the only thing that would need a device.
 *
 * ## Include order
 *
 * Include this header **after** `import std;`, `import mdux.core.units;`, `import mdux.font.schema;`,
 * `import mdux.medui.input;`, `import mdux.medui.reading;`, `import mdux.medui.schema;`,
 * `import mdux.medui.screen;`, `import mdux.medui.scenario;` and `#include "MonitorApp.hpp"`.
 */
#pragma once


namespace mdux::examples {

/// What the caller's capture callback is handed for a `capture <name>` marker.
struct ScenarioCaptureContext {
    std::string_view    name;
    const DemoState&    state;
    const MonitorClock& clock;
    std::uint64_t       frameIndex;  ///< 1-based, the frame the marker fell on
};

using ScenarioCaptureFn = std::function<void(const ScenarioCaptureContext&)>;

/// Optional: the `FrameCounts` for the settled frame - only needed if a scenario carries an
/// `expect frame <stat> <N>` step. The example wires `render()` into a `DrawList`; the GPU-free
/// test passes an empty function and the counts stay zero.
using ScenarioFrameObserver = std::function<mdux::medui::FrameCounts(const DemoState&, const MonitorClock&)>;

/**
 * @brief Replays `scenario` over `updateMonitor()`.
 *
 * @param scenario       the compiled scenario
 * @param screen         the committed screen it names
 * @param font           the committed font the `patient-id` `FieldEditor` binds against
 * @param outcomeStorage caller-owned; the returned `ReplayReport::outcomes` is a view into this
 * @param captureFn      invoked once per `capture` marker as the replay reaches it
 * @param frameObserver  optional; see `ScenarioFrameObserver`
 *
 * The report's whole-run verdict (`report.passed()`) is `false` on a failed expectation, a batch
 * that overflowed the queue, or a declared capture the replay never reached.
 */
[[nodiscard]] inline mdux::medui::ReplayReport replayMonitorScenario(
    const mdux::medui::CompiledScenario&    scenario,
    const mdux::medui::ScreenPackage&       screen,
    const mdux::font::FontPackage&          font,
    std::span<mdux::medui::StepOutcome>     outcomeStorage,
    const ScenarioCaptureFn&               captureFn,
    const ScenarioFrameObserver&           frameObserver = {}) {
    namespace ms = mdux::medui;

    DemoState state;
    state.ecg = MonitorSampleRing<180>{};
    for (std::uint32_t i = 0; i < scenario.sampleSeed.warmupFrames; ++i) {
        state.ecg.push(syntheticSample(static_cast<std::size_t>(i), scenario.sampleSeed.beatPeriod));
    }
    state.bindField(screen, font);

    MonitorClock clock{.now = scenario.pinnedClock};

    std::array<ms::InputEvent, ms::maxInputEvents> queueStorage{};
    ms::EventQueue                                 queue{queueStorage};
    ms::PressLatch                                 latch;
    std::uint64_t                                  sequence   = 0;
    std::uint64_t                                  frameIndex = 0;

    ms::ScenarioRunner runner{scenario, outcomeStorage};

    while (!runner.finished()) {
        const ms::ScenarioRunner::BatchLoad load   = runner.loadNextBatch(queue);
        const std::uint32_t                 frames = runner.framesThisAdvance();

        MonitorUpdateOutcome batchOutcome{};
        for (std::uint32_t f = 0; f < frames; ++f) {
            const bool overflowedThisFrame = f == 0 && load.queueTooSmall;
            const auto outcome = updateMonitor(queue, overflowedThisFrame, screen, state, latch, clock, sequence);
            if (f == 0) {
                batchOutcome = outcome;
            }
            ++frameIndex;
        }

        const std::array<ms::NamedReading, 1> readings{
            ms::NamedReading{.nodeId = kPressureNode, .value = state.pressureTenths}
        };
        const std::array<ms::NamedState, 1> states{
            ms::NamedState{.nodeId = kStatusNode, .index = state.classifierState}
        };

        ms::ScenarioObservation obs{};
        obs.clock = clock.now;
        if (state.field) {
            obs.fieldValue = state.field->value();
            obs.caret      = state.field->caret();
        }
        obs.refusedEdits = batchOutcome.refusedEdits;
        obs.action       = batchOutcome.criticalAction;
        obs.buttonSource =
            batchOutcome.buttonSource.has_value() ? std::string_view{*batchOutcome.buttonSource} : std::string_view{};
        obs.latchArmed = latch.armedNode();
        obs.frame      = frameObserver ? frameObserver(state, clock) : ms::FrameCounts{};
        obs.overflowed = batchOutcome.droppedBatch;
        obs.readings   = readings;
        obs.states     = states;

        runner.observe(obs);
        for (const std::string_view name : runner.capturesThisFrame()) {
            if (captureFn) {
                captureFn(ScenarioCaptureContext{.name = name, .state = state, .clock = clock, .frameIndex = frameIndex});
            }
            runner.markCaptured(name);
        }
    }
    runner.finish();
    return runner.report();
}

}  // namespace mdux::examples
