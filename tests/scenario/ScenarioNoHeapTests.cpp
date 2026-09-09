/**
 * @file ScenarioNoHeapTests.cpp
 * @brief The no-heap property for `mdux.medui.scenario` (#319): building and validating a
 *        `CompiledScenario` over caller storage makes no `operator new` call.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no allocation)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * `CompiledScenario` is spans over caller-owned `constexpr` arrays and `validate()` is pure, so
 * "it allocates nothing" ought to be true by construction - this binary proves it the way
 * `input_noheap_spec` proves it for `EventQueue`: by replacing the global `operator new` family
 * with counting versions and measuring.
 */

import std;
import speclab;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.schema;

#include "../framework/SpecLabBridge.hpp"
#include "../framework/CountingAllocations.hpp"

namespace {

namespace ms = mdux::medui;

constexpr ms::ScenarioStep kSteps[] = {
    {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down, .x = 10, .y = 20}},
    {.kind = ms::StepKind::Advance, .frames = 3},
    {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow, .flag = false}},
    {.kind = ms::StepKind::Capture, .capture = "c"},
};
constexpr std::string_view kCaptures[] = {"c"};

const mdux::spec::Register validatingAScenarioAllocatesNothing{
    "Validating a CompiledScenario makes no operator new call",
    "noheap",
    [] {
        return speclab::Test("scenario-validate-no-heap")
            .Given("a CompiledScenario over static arrays", [] {})
            .When("validate() runs inside an allocation counter", [] {})
            .Then("the counter does not move",
                  [] {
                      mdux::spec::Checks checks;
                      const ms::CompiledScenario scenario{.id            = "s",
                                                          .screenId      = "x",
                                                          .schemaVersion = ms::currentScenarioSchemaVersion,
                                                          .pinnedClock   = {.year = 2026, .month = 1, .day = 1},
                                                          .captureNames  = kCaptures,
                                                          .steps         = kSteps};

                      const auto before = allocations();
                      const auto ok     = scenario.validate();
                      const auto after  = allocations();

                      checks.expect(ok.has_value(), "the scenario validates");
                      checks.expect(after == before, std::format("no allocation, {} -> {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFullReplayAllocatesNothing{
    "A full ScenarioRunner replay - accept and fail paths - makes no operator new call",
    "noheap",
    [] {
        return speclab::Test("scenario-runner-no-heap")
            .Given("a ScenarioRunner over caller storage and a hand-built observation", [] {})
            .When("a whole replay turn runs inside an allocation counter", [] {})
            .Then("the counter does not move on either the held or the failed expectation",
                  [] {
                      mdux::spec::Checks checks;
                      const ms::CompiledScenario scenario{.id            = "s",
                                                          .screenId      = "x",
                                                          .schemaVersion = ms::currentScenarioSchemaVersion,
                                                          .pinnedClock   = {.year = 2026, .month = 1, .day = 1},
                                                          .captureNames  = kCaptures,
                                                          .steps         = kSteps};

                      std::array<ms::StepOutcome, ms::maxScenarioExpectations> outcomes{};
                      std::array<char32_t, 2>                                  observedField{U'A', U'B'};
                      const std::array<ms::NamedReading, 1>                    readings{ms::NamedReading{.nodeId = "n", .value = 7}};
                      const std::array<ms::NamedState, 1>                      states{ms::NamedState{.nodeId = "n", .index = 1}};

                      ms::ScenarioObservation obs{};
                      obs.clock      = {.year = 2026, .month = 1, .day = 1};
                      obs.fieldValue = observedField;
                      obs.readings   = readings;
                      obs.states     = states;

                      const auto before = allocations();
                      ms::ScenarioRunner runner{scenario, outcomes};
                      while (!runner.finished()) {
                          std::array<ms::InputEvent, ms::maxInputEvents> qs{};
                          ms::EventQueue                                 queue{qs};
                          (void)runner.loadNextBatch(queue);
                          (void)runner.framesThisAdvance();
                          runner.observe(obs);
                          for (const std::string_view name : runner.capturesThisFrame()) {
                              runner.markCaptured(name);
                          }
                      }
                      runner.finish();
                      const ms::ReplayReport report = runner.report();
                      const auto after = allocations();

                      checks.expect(after == before, std::format("no allocation across a full replay, {} -> {}", before, after));
                      checks.expect(report.framesRun == 1, "one advance group ran");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
