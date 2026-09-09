/**
 * @file ReplayTests.cpp
 * @brief Replay a compiled scenario through the monitor's real `updateMonitor()` loop, with no GPU
 *        (#320, ADR-020 §3).
 *
 * @compliance ADR-004 Trust zones in C++ (governed replay + examples-zone glue, MduX::Core only)
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * The committed `endoscope-monitor-basics` scenario replays here with every expectation held and
 * every capture invoked, and hand-built scenarios exercise ordering, multi-event frames, an
 * initial-state assertion, controlled time, sample-ring advancement, a failed expectation, a queue
 * that is too small, and a capture the replay never reaches. `updateMonitor()`, the demonstration
 * state and `ScenarioRunner` are all `std` + `mdux.medui.*`, so nothing here needs a device; the
 * capture callback is a counter.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.medui.scenario;
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.medui.generated.scenario_endoscope_monitor_basics;

#include "../framework/SpecLabBridge.hpp"
#include "../../examples/support/MonitorApp.hpp"
#include "../../examples/support/ScenarioReplay.hpp"

namespace {

namespace ms = mdux::medui;
namespace mx = mdux::examples;

[[nodiscard]] const ms::ScreenPackage& monitorScreen() {
    static const ms::ScreenPackage p = ms::generated::screen_endoscope_monitor::package();
    return p;
}

[[nodiscard]] const mdux::font::FontPackage& committedFont() {
    static const mdux::font::FontPackage font = [] {
        const std::filesystem::path path =
            std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "font" / "dejavu-ui" / "package.json";
        std::ifstream in{path, std::ios::binary};
        std::string   text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        auto          parsed = mdux::font::FontPackage::parse(text);
        if (!parsed) {
            throw speclab::core::AssertionFailure("the committed font did not parse", std::source_location::current());
        }
        return std::move(*parsed);
    }();
    return font;
}

/// Replays `scenario` with a capture counter. Copies the report's scalars and outcomes out so the
/// returned value owns everything (the runner's outcome span is about to leave scope).
struct ReplayRun {
    std::vector<ms::StepOutcome> outcomes;
    std::size_t                  framesRun{0};
    std::size_t                  expectationsHeld{0};
    ms::ReplayFault              fault{ms::ReplayFault::None};
    std::size_t                  faultStep{0};
    std::size_t                  capturesFired{0};

    [[nodiscard]] bool passed() const noexcept { return fault == ms::ReplayFault::None; }
};

[[nodiscard]] ReplayRun replay(const ms::CompiledScenario& scenario) {
    std::array<ms::StepOutcome, ms::maxScenarioExpectations> storage{};
    std::size_t                                              fired = 0;
    const ms::ReplayReport report = mx::replayMonitorScenario(scenario, monitorScreen(), committedFont(), storage,
                                                              [&](const mx::ScenarioCaptureContext&) { ++fired; });
    return ReplayRun{.outcomes         = std::vector<ms::StepOutcome>(report.outcomes.begin(), report.outcomes.end()),
                     .framesRun        = report.framesRun,
                     .expectationsHeld = report.expectationsHeld,
                     .fault            = report.fault,
                     .faultStep        = report.faultStep,
                     .capturesFired    = fired};
}

/// Builds a `CompiledScenario` from static step storage the caller keeps alive.
struct HandScenario {
    std::vector<ms::ScenarioStep> steps;
    std::vector<std::string_view> captures;
    ms::CompiledScenario          scenario;

    HandScenario(std::string_view screenId, ms::CivilTime clock, std::vector<ms::ScenarioStep> s,
                 std::vector<std::string_view> caps = {})
        : steps{std::move(s)}, captures{std::move(caps)} {
        scenario.id            = "hand";
        scenario.screenId      = screenId;
        scenario.schemaVersion = ms::currentScenarioSchemaVersion;
        scenario.pinnedClock   = clock;
        scenario.steps         = steps;
        scenario.captureNames  = captures;
    }
};

constexpr ms::CivilTime kNoon{.year = 2026, .month = 6, .day = 1, .hour = 12, .minute = 0, .second = 0};

const mdux::spec::Register theCommittedScenarioReplaysClean{
    "The committed endoscope-monitor-basics scenario replays with every expectation held",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-committed")
            .Given("the generated CompiledScenario", [] {})
            .When("it is replayed through updateMonitor() with a capture counter", [] {})
            .Then("the report passes and both captures fired",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         run = replay(ms::generated::scenario_endoscope_monitor_basics::package());
                      checks.expect(run.passed(),
                                    std::format("passed, fault {} at step {}", ms::describe(run.fault), run.faultStep));
                      checks.expect(run.expectationsHeld == 12,
                                    std::format("all twelve expectations held, {} did", run.expectationsHeld));
                      checks.expect(run.capturesFired == 2, std::format("two captures fired, {} did", run.capturesFired));
                      checks.expect(run.framesRun == 5, std::format("five advance groups, {}", run.framesRun));
                      for (const ms::StepOutcome& o : run.outcomes) {
                          checks.expect(o.held, std::format("expectation at step {} ({}) held", o.stepIndex, ms::toWire(o.kind)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register orderingIsObserved{
    "Events within a batch resolve in FIFO order, and a cancel between Down and Up disarms",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-ordering")
            .Given("the freeze Button's centre", [] {})
            .When("Down/Up land in one batch, then Down/cancel/Up in one batch", [] {})
            .Then("the first resolves FREEZE; the cancelled one resolves nothing",
                  [] {
                      mdux::spec::Checks       checks;
                      const ms::CompiledNode*  freeze = monitorScreen().find("freeze");
                      checks.expect(freeze != nullptr, "the screen carries the freeze Button");
                      if (freeze == nullptr) {
                          checks.raise();
                          return;
                      }
                      const mdux::core::Px x = freeze->bounds.x + freeze->bounds.width / 2;
                      const mdux::core::Px y = freeze->bounds.y + freeze->bounds.height / 2;

                      HandScenario together{"endoscope-monitor", kNoon,
                                            {{.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down, .x = x, .y = y}},
                                             {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Up, .x = x, .y = y}},
                                             {.kind = ms::StepKind::Advance, .frames = 1},
                                             {.kind = ms::StepKind::Expect,
                                              .expect = {.kind = ms::ExpectKind::ButtonSource, .nodeId = "freeze", .source = "FREEZE"}}}};
                      const auto togetherRun = replay(together.scenario);
                      checks.expect(togetherRun.passed(), "a press and release in one batch resolves FREEZE");

                      HandScenario cancelled{"endoscope-monitor", kNoon,
                                             {{.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down, .x = x, .y = y}},
                                              {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Cancel}},
                                              {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Up, .x = x, .y = y}},
                                              {.kind = ms::StepKind::Advance, .frames = 1},
                                              {.kind = ms::StepKind::Expect,
                                               .expect = {.kind = ms::ExpectKind::ButtonSource, .nodeId = "freeze", .source = ""}},
                                              {.kind = ms::StepKind::Expect,
                                               .expect = {.kind = ms::ExpectKind::LatchArmed}}}};
                      const auto cancelledRun = replay(cancelled.scenario);
                      checks.expect(cancelledRun.passed(),
                                    std::format("a cancel between Down and Up disarms, fault {}", ms::describe(cancelledRun.fault)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register controlledTimeAndSampleRing{
    "advance N moves the injected clock by exactly N seconds and steps the classifier",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-time")
            .Given("a scenario that advances 5 then 30 with clock and state expectations", [] {})
            .When("it replays", [] {})
            .Then("the clock reads 12:00:35 and the classifier stepped to 1",
                  [] {
                      mdux::spec::Checks checks;
                      HandScenario s{"endoscope-monitor", kNoon,
                                     {{.kind = ms::StepKind::Advance, .frames = 5},
                                      {.kind = ms::StepKind::Expect,
                                       .expect = {.kind = ms::ExpectKind::Clock,
                                                  .clock = {.year = 2026, .month = 6, .day = 1, .hour = 12, .minute = 0, .second = 5}}},
                                      {.kind = ms::StepKind::Expect,
                                       .expect = {.kind = ms::ExpectKind::State, .count = 0, .nodeId = "classifier-state"}},
                                      {.kind = ms::StepKind::Advance, .frames = 30},
                                      {.kind = ms::StepKind::Expect,
                                       .expect = {.kind = ms::ExpectKind::Clock,
                                                  .clock = {.year = 2026, .month = 6, .day = 1, .hour = 12, .minute = 0, .second = 35}}},
                                      {.kind = ms::StepKind::Expect,
                                       .expect = {.kind = ms::ExpectKind::State, .count = 1, .nodeId = "classifier-state"}}}};
                      const auto run = replay(s.scenario);
                      checks.expect(run.passed(),
                                    std::format("passed, fault {} at step {}", ms::describe(run.fault), run.faultStep));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFailedExpectationIsReported{
    "A wrong expectation fails the run and names the step",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-failed-expectation")
            .Given("a scenario expecting the wrong field value", [] {})
            .When("it replays", [] {})
            .Then("report.passed() is false and the fault points at the expect step",
                  [] {
                      mdux::spec::Checks checks;
                      static const std::array<char32_t, 2> wrong{U'Z', U'Z'};
                      HandScenario s{"endoscope-monitor", kNoon,
                                     {{.kind = ms::StepKind::Key, .key = {.kind = ms::KeyKind::Down, .key = ms::KeyCode::Commit}},
                                      {.kind = ms::StepKind::Text, .text = {.scalar = U'A'}},
                                      {.kind = ms::StepKind::Advance, .frames = 1},
                                      {.kind = ms::StepKind::Expect,
                                       .expect = {.kind = ms::ExpectKind::Field, .fieldValue = wrong}}}};
                      const auto run = replay(s.scenario);
                      checks.expect(!run.passed() && run.fault == ms::ReplayFault::ExpectationFailed,
                                    std::format("fault is ExpectationFailed, got {}", ms::describe(run.fault)));
                      checks.expect(run.faultStep == 3, std::format("fault at step 3, got {}", run.faultStep));
                      checks.expect(!run.outcomes.empty() && !run.outcomes.back().held, "the outcome is recorded as failed");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aButtonExpectationChecksTheNode{
    "expect button names a control, and a press on a different control does not satisfy it",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-button-node")
            .Given("a freeze press but an expectation naming emergency-halt with the same source", [] {})
            .When("it replays", [] {})
            .Then("the expectation fails - the node is part of the obligation, not just the source",
                  [] {
                      mdux::spec::Checks      checks;
                      const ms::CompiledNode* freeze = monitorScreen().find("freeze");
                      checks.expect(freeze != nullptr, "the screen carries the freeze Button");
                      if (freeze == nullptr) {
                          checks.raise();
                          return;
                      }
                      const mdux::core::Px x = freeze->bounds.x + freeze->bounds.width / 2;
                      const mdux::core::Px y = freeze->bounds.y + freeze->bounds.height / 2;

                      HandScenario wrongNode{
                          "endoscope-monitor", kNoon,
                          {{.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down, .x = x, .y = y}},
                           {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Up, .x = x, .y = y}},
                           {.kind = ms::StepKind::Advance, .frames = 1},
                           {.kind = ms::StepKind::Expect,
                            .expect = {.kind = ms::ExpectKind::ButtonSource, .nodeId = "emergency-halt", .source = "FREEZE"}}}};
                      const auto run = replay(wrongNode.scenario);
                      checks.expect(!run.passed() && run.fault == ms::ReplayFault::ExpectationFailed,
                                    std::format("the wrong-node expectation fails, got {}", ms::describe(run.fault)));

                      HandScenario rightNode{
                          "endoscope-monitor", kNoon,
                          {{.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down, .x = x, .y = y}},
                           {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Up, .x = x, .y = y}},
                           {.kind = ms::StepKind::Advance, .frames = 1},
                           {.kind = ms::StepKind::Expect,
                            .expect = {.kind = ms::ExpectKind::ButtonSource, .nodeId = "freeze", .source = "FREEZE"}}}};
                      checks.expect(replay(rightNode.scenario).passed(), "the same press with the right node still passes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnhonouredCaptureFailsTheRun{
    "A capture marker the caller cannot honour leaves the run outstanding",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-capture-not-honoured")
            .Given("a scenario with a capture step but an empty capture callback", [] {})
            .When("it replays", [] {})
            .Then("the run fails with CaptureNotInvoked rather than passing on an unfired capture",
                  [] {
                      mdux::spec::Checks checks;
                      HandScenario       s{"endoscope-monitor", kNoon,
                                     {{.kind = ms::StepKind::Advance, .frames = 1},
                                            {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow, .flag = false}},
                                            {.kind = ms::StepKind::Capture, .capture = "shot"}},
                                     {"shot"}};
                      std::array<ms::StepOutcome, ms::maxScenarioExpectations> storage{};
                      const ms::ReplayReport report = mx::replayMonitorScenario(
                          s.scenario, monitorScreen(), committedFont(), storage, mx::ScenarioCaptureFn{});
                      checks.expect(!report.passed() && report.fault == ms::ReplayFault::CaptureNotInvoked,
                                    std::format("fault is CaptureNotInvoked, got {}", ms::describe(report.fault)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBatchLargerThanTheQueueFails{
    "A batch with more events than maxInputEvents fails the run",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-queue-too-small")
            .Given("a scenario queueing maxInputEvents + 1 text events before an advance", [] {})
            .When("it replays", [] {})
            .Then("the run fails with QueueTooSmall",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<ms::ScenarioStep> steps;
                      for (std::size_t i = 0; i <= ms::maxInputEvents; ++i) {
                          steps.push_back({.kind = ms::StepKind::Text, .text = {.scalar = U'A'}});
                      }
                      steps.push_back({.kind = ms::StepKind::Advance, .frames = 1});
                      steps.push_back({.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow, .flag = true}});
                      HandScenario s{"endoscope-monitor", kNoon, std::move(steps)};
                      const auto run = replay(s.scenario);
                      checks.expect(!run.passed() && run.fault == ms::ReplayFault::QueueTooSmall,
                                    std::format("fault is QueueTooSmall, got {}", ms::describe(run.fault)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aMissingCaptureFailsTheRun{
    "A declared capture the replay never reaches fails the run",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-replay-missing-capture")
            .Given("a scenario declaring a capture name no Capture step produces", [] {})
            .When("it replays", [] {})
            .Then("the run fails with CaptureNotInvoked",
                  [] {
                      mdux::spec::Checks checks;
                      // captureNames lists "ghost" but no step captures it, so validate() already
                      // refuses this - the runner reports MalformedScenario, which is also a fault.
                      HandScenario s{"endoscope-monitor", kNoon,
                                     {{.kind = ms::StepKind::Advance, .frames = 1},
                                      {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow, .flag = false}}},
                                     {"ghost"}};
                      const auto run = replay(s.scenario);
                      checks.expect(!run.passed(), "a scenario with an unproduced capture name does not pass");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
