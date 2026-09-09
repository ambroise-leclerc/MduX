/**
 * @file GeneratedScenarioTests.cpp
 * @brief The committed scenario as `constexpr` C++: it validates, and its steps survive emission.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: MduX::Core only, no host tools)
 * @compliance ADR-012 What a compiled artifact emits
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * The link line here is the standing evidence for #319's trust-zone claim: a device build reaching
 * a compiled scenario through generated code links no compiler and parses nothing. The generated
 * module's own `static_assert(scenario.validate())` is the first gate; these scenarios are the
 * second, checking the *contents* an emitter bug could quietly drop.
 */

import std;
import speclab;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.schema;
import mdux.medui.generated.scenario_endoscope_monitor_basics;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms = mdux::medui;
namespace gen = mdux::medui::generated::scenario_endoscope_monitor_basics;

constexpr ms::CompiledScenario compiled = gen::package();
static_assert(compiled.validate().has_value(), "the committed scenario's generated form validates");

const mdux::spec::Register theGeneratedScenarioValidates{
    "The committed endoscope-monitor-basics scenario, as generated C++, validates and keeps its shape",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-generated-validates")
            .Given("the constexpr CompiledScenario the emitter produced", [] {})
            .When("its identity, clock, requirements and steps are inspected", [] {})
            .Then("each survived emission",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(compiled.id == "endoscope-monitor-basics", "id");
                      checks.expect(compiled.screenId == "endoscope-monitor", "screen id");
                      checks.expect(compiled.schemaVersion == ms::currentScenarioSchemaVersion, "schema version is current");
                      checks.expect(compiled.pinnedClock.year == 2026 && compiled.pinnedClock.hour == 8, "pinned clock");
                      checks.expect(compiled.sampleSeed.beatPeriod == 60, "sample seed");
                      checks.expect(compiled.requirements.size() == 2, "two requirements cited");
                      checks.expect(compiled.captureNames.size() == 2, "two capture markers");
                      checks.expect(compiled.steps.size() == 28, std::format("28 steps, got {}", compiled.steps.size()));

                      std::size_t advances = 0;
                      std::size_t expects  = 0;
                      std::size_t captures = 0;
                      for (const ms::ScenarioStep& step : compiled.steps) {
                          advances += step.kind == ms::StepKind::Advance;
                          expects  += step.kind == ms::StepKind::Expect;
                          captures += step.kind == ms::StepKind::Capture;
                      }
                      checks.expect(advances == 5, std::format("five advances, got {}", advances));
                      checks.expect(expects == 12, std::format("twelve expectations, got {}", expects));
                      checks.expect(captures == 2, std::format("two captures, got {}", captures));

                      // The action expectation carries its requirement and its closed event.
                      const auto action = std::ranges::find_if(compiled.steps, [](const ms::ScenarioStep& s) {
                          return s.kind == ms::StepKind::Expect && s.expect.kind == ms::ExpectKind::Action;
                      });
                      checks.expect(action != compiled.steps.end(), "an action expectation is present");
                      if (action != compiled.steps.end()) {
                          checks.expect(action->expect.nodeId == "emergency-halt", "on emergency-halt");
                          checks.expect(action->expect.event == ms::SystemEvent::TriggerHalt, "resolving TriggerHalt");
                          checks.expect(action->expect.requirement == "REQ-EM-003", "traced to REQ-EM-003");
                      }

                      // The field expectation's value span points at real scalars.
                      const auto field = std::ranges::find_if(compiled.steps, [](const ms::ScenarioStep& s) {
                          return s.kind == ms::StepKind::Expect && s.expect.kind == ms::ExpectKind::Field;
                      });
                      checks.expect(field != compiled.steps.end(), "a field expectation is present");
                      if (field != compiled.steps.end()) {
                          checks.expect(field->expect.fieldValue.size() == 2 && field->expect.fieldValue[0] == U'A'
                                            && field->expect.fieldValue[1] == U'7',
                                        "the field is expected to hold \"A7\"");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aMalformedScenarioIsRefused{
    "CompiledScenario::validate() refuses a scenario that breaks a structural rule",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-validate-refusals")
            .Given("scenarios that each break one rule", [] {})
            .When("validate() runs on each", [] {})
            .Then("the matching ScenarioError comes back",
                  [] {
                      mdux::spec::Checks checks;

                      static constexpr ms::ScenarioStep advanceThenExpect[] = {
                          {.kind = ms::StepKind::Advance, .frames = 1},
                          {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow, .flag = false}},
                      };
                      ms::CompiledScenario good{.id = "s", .screenId = "x", .schemaVersion = ms::currentScenarioSchemaVersion,
                                                .pinnedClock = {.year = 2026, .month = 1, .day = 1},
                                                .steps = advanceThenExpect};
                      checks.expect(good.validate().has_value(), "a minimal valid scenario passes");

                      ms::CompiledScenario badVersion = good;
                      badVersion.schemaVersion = 99;
                      checks.expect(!badVersion.validate().has_value()
                                        && badVersion.validate().error() == ms::ScenarioError::UnsupportedSchemaVersion,
                                    "an unsupported schema version is refused");

                      static constexpr ms::ScenarioStep expectFirst[] = {
                          {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::Overflow}},
                          {.kind = ms::StepKind::Advance, .frames = 1},
                      };
                      ms::CompiledScenario earlyExpect = good;
                      earlyExpect.steps = expectFirst;
                      checks.expect(earlyExpect.validate().error() == ms::ScenarioError::ExpectationBeforeAdvance,
                                    "an expectation before the first advance is refused");

                      static constexpr ms::ScenarioStep noAdvance[] = {
                          {.kind = ms::StepKind::Pointer, .pointer = {.kind = ms::PointerKind::Down}},
                      };
                      ms::CompiledScenario neverAdvances = good;
                      neverAdvances.steps = noAdvance;
                      checks.expect(neverAdvances.validate().error() == ms::ScenarioError::NoAdvance,
                                    "a scenario that never advances is refused");

                      // An Expect wedged between queued events and their Advance: loadNextBatch()
                      // would walk past it, so validate() must reject the ordering rather than let
                      // the replay silently skip the obligation.
                      static constexpr ms::ScenarioStep expectInBatch[] = {
                          {.kind = ms::StepKind::Advance, .frames = 1},
                          {.kind = ms::StepKind::Text, .text = {.scalar = U'A'}},
                          {.kind = ms::StepKind::Expect, .expect = {.kind = ms::ExpectKind::RefusedEdits, .count = 1}},
                          {.kind = ms::StepKind::Advance, .frames = 1},
                      };
                      ms::CompiledScenario midBatchExpect = good;
                      midBatchExpect.steps = expectInBatch;
                      checks.expect(midBatchExpect.validate().error() == ms::ScenarioError::ExpectationInEventBatch,
                                    "an expectation after unadvanced events is refused");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
