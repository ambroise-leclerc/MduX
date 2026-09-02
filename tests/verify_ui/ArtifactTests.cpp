/**
 * @file ArtifactTests.cpp
 * @brief What `verification.json` records, and the three things it must never record.
 *
 * Direct-library tests over a synthesized `RunResult`, so what a check asserts is the writer's
 * behaviour and not a rendered frame's. The end-to-end path - compile, render, serialize, compare -
 * is `evidence.screen.<id>`, which runs the real producer on every ADR-007 leg.
 */
import std;
import speclab;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.tools.verify.artifact;
import mdux.tools.verify.driver;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"

namespace {
namespace evj = mdux::evidence::json;
namespace mv  = mdux::verify;
namespace vu  = mdux::tools::verify;

/// A run that discharged one `Bounds` obligation and one text obligation, in one locale.
[[nodiscard]] vu::RunResult completedRun() {
    vu::RunResult result;
    result.state       = vu::RunState::ChecksFailed;
    result.renderCount = 1;
    result.obligations = {
        {.kind = vu::ObligationKind::Golden,  .nodeId = "dial", .scope = "en-US",         .check = "Bounds"},
        {  .kind = vu::ObligationKind::Text, .nodeId = "title", .scope = "en-US", .check = "InkContainment"}
    };
    result.outcomes = {
        {.finding = mv::Finding::NothingPainted,  .nodeId = "dial", .scope = "en-US",         .check = "Bounds"},
        {          .finding = mv::Finding::Held, .nodeId = "title", .scope = "en-US", .check = "InkContainment"}
    };
    result.inputs = {
        {.role = "screenPackage",       .id = "demo",      .locale = {}, .sha256 = std::string(64, 'a')},
        {  .role = "textPackage", .id = "demo-en-us", .locale = "en-US", .sha256 = std::string(64, 'b')}
    };
    return result;
}

const mdux::spec::Register outcomesAreScopedToOneObligation{
    "Each recorded outcome makes only its own obligation's claim",
    "evidence-unit",
    [] {
        return speclab::Test("verify-artifact-outcome-scope")
            .Given("a run over a node checked for bounds but not for tint", [] {})
            .When("the artifact is written", [] {})
            .Then("the node appears once, as a Bounds outcome, and nothing claims its tint was checked",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         text = vu::writeVerification(completedRun(), "demo");
                      if (!text.has_value()) {
                          checks.expect(false, "the artifact is written for a completed run");
                          checks.raise();
                          return;
                      }
                      const auto document = evj::parse(*text);
                      if (!document.has_value()) {
                          checks.expect(false, "the artifact is canonical JSON");
                          checks.raise();
                          return;
                      }
                      const evj::Value* outcomes = document->find("outcomes");
                      checks.expect(outcomes != nullptr && outcomes->kind() == evj::Value::Kind::Array, "it carries an outcomes array");
                      if (outcomes == nullptr || outcomes->kind() != evj::Value::Kind::Array) {
                          checks.raise();
                          return;
                      }
                      checks.expect(outcomes->elements().size() == 2, "one outcome per enumerated obligation, and no more");
                      // The claim that matters: a node carrying only Bounds must not appear as a
                      // tint check anywhere in the file, whatever else the writer adds later.
                      checks.expect(!text->contains("ColorHash"), "a node checked only for bounds is never described as tint-checked");
                      checks.expect(text->contains("NothingPainted") && text->contains("Held"),
                                    "each outcome carries its own finding rather than a shared summary");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register noMeasurementReachesTheArtifact{
    "No measurement, path or timestamp reaches the committed artifact",
    "evidence-unit",
    [] {
        return speclab::Test("verify-artifact-no-measurements")
            .Given("a run whose outcomes carry found rectangles and sampled colours", [] {})
            .When("the artifact is written", [] {})
            .Then("none of that appears, because a byte-compared file may not depend on the driver that produced the frame",
                  [] {
                      mdux::spec::Checks checks;
                      vu::RunResult      result = completedRun();
                      // The driver fills these so it can print a sentence a reader can act on.
                      // ADR-014 decision 4 keeps every one of them out of the file.
                      result.outcomes[0].foundValid      = true;
                      result.outcomes[0].found           = {.x = 3, .y = 4, .width = 5, .height = 6};
                      result.outcomes[0].foundColorValid = true;
                      result.outcomes[0].foundColor      = {.r = 12, .g = 34, .b = 56, .a = 255};

                      const auto text = vu::writeVerification(result, "demo");
                      if (!text.has_value()) {
                          checks.expect(false, "the artifact is written");
                          checks.raise();
                          return;
                      }
                      checks.expect(!text->contains("found"), "no measured rectangle or colour is recorded");
                      checks.expect(!text->contains("expected"), "not even the expectation's own numbers, which goldens.json already carries");
                      checks.expect(!text->contains("/") && !text->contains("\\\\"), "no path, absolute or otherwise");
                      checks.expect(!text->contains("duration") && !text->contains("elapsed"), "no duration");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRunThatCouldNotBeMadeIsNotWritable{
    "A run that could not be made produces no artifact at all",
    "evidence-unit",
    [] {
        return speclab::Test("verify-artifact-refuses-impossible-run")
            .Given("a run that never rendered, and one that rendered nothing", [] {})
            .When("each is offered to the writer", [] {})
            .Then("both are refused, so no committed file can claim a verification that did not happen",
                  [] {
                      mdux::spec::Checks checks;

                      vu::RunResult absentDevice = completedRun();
                      absentDevice.state         = vu::RunState::NoRenderDevice;
                      const auto refusedDevice   = vu::writeVerification(absentDevice, "demo");
                      checks.expect(!refusedDevice.has_value() && refusedDevice.error() == vu::ArtifactError::NotRun,
                                    "an absent render device fails production rather than committing an empty verification");

                      vu::RunResult couldNotRun = completedRun();
                      couldNotRun.state         = vu::RunState::CouldNotRun;
                      checks.expect(!vu::writeVerification(couldNotRun, "demo").has_value(), "and so does every other inability to run");

                      // Distinct from the above: this run *did* execute. ADR-014 decision 3 makes a
                      // verification of nothing a failure in its own right.
                      vu::RunResult empty = completedRun();
                      empty.obligations.clear();
                      empty.outcomes.clear();
                      const auto refusedEmpty = vu::writeVerification(empty, "demo");
                      checks.expect(!refusedEmpty.has_value() && refusedEmpty.error() == vu::ArtifactError::NoObligations,
                                    "a run that discharged nothing is not evidence");

                      vu::RunResult mismatched = completedRun();
                      mismatched.outcomes.pop_back();
                      checks.expect(!vu::writeVerification(mismatched, "demo").has_value(), "and neither is a run whose outcomes do not cover its obligations");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theReportGainsTheOutputAndItsOptions{
    "The screen's one report gains the new output and the resolved locale set",
    "evidence-unit",
    [] {
        return speclab::Test("verify-artifact-extends-report")
            .Given("the report mdux-meduic wrote for the two files it produced", [] {})
            .When("the verification artifact is added to the bundle", [] {})
            .Then("the same report names three outputs and records the locales the run resolved",
                  [] {
                      mdux::spec::Checks         checks;
                      const vu::RunResult        result = completedRun();
                      mdux::evidence::BakeReport compiled;
                      compiled.tool        = "mdux-meduic";
                      compiled.toolVersion = "0.6.0";
                      compiled.recipe      = {.path = "recipes/screen/demo.toml", .sha256 = {}};
                      compiled.options     = evj::Value::emptyObject();
                      compiled.outputs     = {
                          {.path = "goldens.json", .sha256 = {}},
                          {.path = "package.json", .sha256 = {}}
                      };

                      const auto compiledText = compiled.write();
                      if (!compiledText.has_value()) {
                          checks.expect(false, "the compiler's own report is valid");
                          checks.raise();
                          return;
                      }
                      const auto options = vu::verificationOptions(result, "generated");
                      if (!options.has_value()) {
                          checks.expect(false, "the resolved options are built");
                          checks.raise();
                          return;
                      }
                      const auto extended = vu::extendReport(*compiledText, "{}\n", *options);
                      if (!extended.has_value()) {
                          checks.expect(false, "the report is extended");
                          checks.raise();
                          return;
                      }
                      const auto reread = mdux::evidence::BakeReport::parse(*extended);
                      if (!reread.has_value()) {
                          checks.expect(false, "and the result is still a bake report");
                          checks.raise();
                          return;
                      }
                      checks.expect(reread->outputs.size() == 3, "the one report names three outputs");
                      checks.expect(reread->outputs.back().path == "verification.json", "the new one last, keeping the list sorted");
                      // The resolved set, not the `all` that asked for it: ADR-007 decision 4 exists
                      // so a narrowed run cannot look like a full one in the report.
                      checks.expect(extended->contains("\"verification\"") && extended->contains("en-US"), "and records the locales the run actually covered");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
