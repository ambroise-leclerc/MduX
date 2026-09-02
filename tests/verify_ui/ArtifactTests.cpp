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
#include "../framework/TemporaryDirectory.hpp"

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

                      // Equal counts are not coverage. An outcome recorded against an obligation
                      // the run never enumerated is evidence for a claim nobody made, and it is
                      // invisible to a size check.
                      vu::RunResult mispaired      = completedRun();
                      mispaired.outcomes[1].nodeId = "a-node-no-obligation-named";
                      const auto refusedMispaired  = vu::writeVerification(mispaired, "demo");
                      checks.expect(!refusedMispaired.has_value() && refusedMispaired.error() == vu::ArtifactError::OutcomeMismatch,
                                    "an outcome that does not pair with its obligation is refused, not serialized");

                      vu::RunResult wrongScope     = completedRun();
                      wrongScope.outcomes[0].scope = "de-DE";
                      checks.expect(!vu::writeVerification(wrongScope, "demo").has_value(), "and so is one attributed to a scope the run did not render");

                      vu::RunResult wrongCheck     = completedRun();
                      wrongCheck.outcomes[0].check = "ColorHash";
                      checks.expect(!vu::writeVerification(wrongCheck, "demo").has_value(), "and one that would turn a bounds obligation into a tint claim");
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
                      const auto options = vu::verificationOptions(result);
                      if (!options.has_value()) {
                          checks.expect(false, "the resolved options are built");
                          checks.raise();
                          return;
                      }
                      const auto extended = vu::extendReport(*compiledText, "{}\n", *options, "9.9.9");
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
                      // BakeReport::validate() does not look inside `options`, so a path placed
                      // there is one nothing rejects. The locale set is the resolved option; the
                      // tree it was read from is not.
                      checks.expect(!extended->contains("artifactRoot"), "and no path smuggled in through the options object");
                      // The point of the stage record: the report's own `tool` is the compiler, so
                      // without this a reader attributes verification.json to a tool that never saw
                      // a frame - which is the one question ADR-007 exists to answer.
                      checks.expect(reread->tool == "mdux-meduic", "the report still names the tool the bake is registered to");
                      checks.expect(reread->stages.size() == 1 && reread->stages.front().tool == "mdux-verify-bake"
                                        && reread->stages.front().output == "verification.json" && reread->stages.front().toolVersion == "9.9.9",
                                    "and names the second tool, its version, and the one output it wrote");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFailedPromotionLeavesTheBundleAlone{
    "A bundle that cannot be published completely is not published at all",
    "evidence-unit",
    [] {
        return speclab::Test("verify-artifact-publish-rollback")
            .Given("a bundle whose second file cannot be promoted", [] {})
            .When("it is published", [] {})
            .Then("both files still hold exactly what they held before",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-verify-publish"};
                      const std::filesystem::path    first  = scratch.path() / "verification.json";
                      const std::filesystem::path    second = scratch.path() / "report.json";

                      const auto put = [](const std::filesystem::path& path, std::string_view text) {
                          std::ofstream out{path, std::ios::binary | std::ios::trunc};
                          out.write(text.data(), static_cast<std::streamsize>(text.size()));
                      };
                      const auto contents = [](const std::filesystem::path& path) {
                          std::ifstream      in{path, std::ios::binary};
                          std::ostringstream buffer;
                          buffer << in.rdbuf();
                          return buffer.str();
                      };
                      put(first, "old verification\n");
                      put(second, "old report\n");

                      const std::array files{
                          vu::BundleFile{ .path = first, .text = "new verification\n"},
                          vu::BundleFile{.path = second,       .text = "new report\n"}
                      };

                      // The failure is injected on the *second* promotion, after the first has
                      // already succeeded. That ordering is the whole point: it is the state in
                      // which a naive implementation leaves a new file beside a stale one.
                      std::size_t promotions   = 0;
                      const auto  refuseSecond = [&promotions](const std::filesystem::path& from, const std::filesystem::path& to) -> std::error_code {
                          if (++promotions == 3) {
                              return std::make_error_code(std::errc::io_error);
                          }
                          std::error_code failure;
                          std::filesystem::rename(from, to, failure);
                          return failure;
                      };

                      const auto published = vu::publishBundle(files, refuseSecond);
                      checks.expect(!published.has_value() && published.error() == vu::ArtifactError::PublishFailed,
                                    "publication fails rather than reporting a bundle it did not write");
                      checks.expect(contents(first) == "old verification\n", "the first file is back to what it held");
                      checks.expect(contents(second) == "old report\n", "and so is the second, which never changed");
                      checks.expect(!std::filesystem::exists(first.string() + ".staged") && !std::filesystem::exists(second.string() + ".staged"),
                                    "and no staged leftovers remain");
                      checks.expect(!std::filesystem::exists(first.string() + ".previous") && !std::filesystem::exists(second.string() + ".previous"),
                                    "nor any displaced originals");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aCompletePublicationReplacesBoth{"A bundle that publishes completely replaces every file", "evidence-unit", [] {
                                                                return speclab::Test("verify-artifact-publish-success")
                                                                    .Given("a bundle over files that already exist", [] {})
                                                                    .When("it is published with nothing failing", [] {})
                                                                    .Then("both hold the new bytes and nothing is left beside them",
                                                                          [] {
                                                                              mdux::spec::Checks             checks;
                                                                              mdux::test::TemporaryDirectory scratch{"mdux-verify-publish-ok"};
                                                                              const std::filesystem::path    first  = scratch.path() / "verification.json";
                                                                              const std::filesystem::path    second = scratch.path() / "report.json";
                                                                              {
                                                                                  std::ofstream out{second, std::ios::binary};
                                                                                  out << "old report\n";
                                                                              }

                                                                              const std::array files{
                                                                                  vu::BundleFile{ .path = first, .text = "new verification\n"},
                                                                                  vu::BundleFile{.path = second,       .text = "new report\n"}
                                                                              };
                                                                              checks.expect(vu::publishBundle(files).has_value(), "publication succeeds");

                                                                              const auto contents = [](const std::filesystem::path& path) {
                                                                                  std::ifstream      in{path, std::ios::binary};
                                                                                  std::ostringstream buffer;
                                                                                  buffer << in.rdbuf();
                                                                                  return buffer.str();
                                                                              };
                                                                              // The first file did not exist beforehand and the second did, so this covers
                                                                              // both branches of the displacement bookkeeping in one pass.
                                                                              checks.expect(contents(first) == "new verification\n",
                                                                                            "a file that did not exist is created");
                                                                              checks.expect(contents(second) == "new report\n", "and one that did is replaced");
                                                                              checks.expect(!std::filesystem::exists(second.string() + ".previous"),
                                                                                            "with no displaced original left behind");
                                                                              checks.raise();
                                                                          })
                                                                    .Execute();
                                                            }};

}  // namespace
