/**
 * @file ScenarioVerifyTests.cpp
 * @brief Direct-library tests for the dynamic scenario-capture verifier (#321, ADR-021).
 *
 * The positive case is a real GPU replay of the committed `endoscope-monitor-basics` bundle. The
 * rest are pure: obligation enumeration, the fail-closed reconciliation the issue names (deliberately
 * wrong value, dropped outcome, duplicated outcome, omitted capture), the artifact writer's
 * refusals, and the substitution / narrowing rejections.
 */
import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.verify.driver;
import mdux.tools.verify.scenario.artifact;
import mdux.tools.verify.scenario.driver;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "../framework/TemporaryDirectory.hpp"

namespace {

namespace cli = mdux::tools::cli;
namespace vs  = mdux::tools::verify::scenario;
namespace vu  = mdux::tools::verify;
namespace mv  = mdux::verify;

const std::filesystem::path kRepo{MDUX_REPO_ROOT};
const std::filesystem::path kBundle = kRepo / "generated" / "scenario" / "endoscope-monitor-basics";

[[nodiscard]] bool hasCode(std::span<const cli::Diagnostic> diagnostics, std::string_view code) {
    return std::ranges::any_of(diagnostics, [code](const cli::Diagnostic& d) { return d.code == code; });
}

[[nodiscard]] vs::Obligation binding(std::string scope, std::size_t step, std::string kind) {
    return vs::Obligation{.kind = vs::ObligationKind::Binding, .scope = std::move(scope), .capture = {}, .stepIndex = step, .expectKind = std::move(kind), .nodeId = {}, .check = {}};
}
[[nodiscard]] vs::Outcome heldBinding(std::string scope, std::size_t step, std::string kind, bool held = true) {
    return vs::Outcome{.kind = vs::ObligationKind::Binding, .scope = std::move(scope), .capture = {}, .stepIndex = step, .expectKind = std::move(kind), .nodeId = {}, .check = {}, .held = held, .finding = mv::Finding::Held, .profile = {}};
}

// --- The real replay ------------------------------------------------------------------------------

const mdux::spec::Register committedBundleVerifies{
    "The committed endoscope-monitor-basics scenario verifies in both approved locales", "pixel", [] {
        return speclab::Test("verify-scenario-committed")
            .Given("a real replay of the committed scenario bundle on this device", [] {})
            .When("mdux-verify-scenario renders every capture and discharges every obligation", [] {})
            .Then("the run passes, with one held outcome per enumerated obligation",
                  [] {
                      mdux::spec::Checks checks;
                      const vs::RunResult result = vs::run(kBundle);
                      if (result.state == vs::RunState::NoRenderDevice) {
                          // The bake rendered these captures (#321), so a device provably existed at
                          // build time. One that is gone now is infrastructure, not an absent GPU.
                          checks.expect(false, "a device rendered the captures at bake time but is unavailable now");
                          checks.raise();
                          return;
                      }
                      checks.expect(result.state == vs::RunState::Passed,
                                    std::format("the committed bundle verifies (state {})", static_cast<int>(result.state)));
                      checks.expect(result.diagnostics.empty(), std::format("no diagnostics, first '{}'", result.diagnostics.empty() ? "" : result.diagnostics.front().message));
                      checks.expect(result.renderCount == 4, std::format("two captures times two locales rendered, got {}", result.renderCount));
                      checks.expect(result.outcomes.size() == result.obligations.size(),
                                    std::format("{} outcomes for {} obligations", result.outcomes.size(), result.obligations.size()));

                      const bool scopesCovered = std::ranges::any_of(result.obligations, [](const vs::Obligation& o) { return o.scope == "en-US"; })
                                              && std::ranges::any_of(result.obligations, [](const vs::Obligation& o) { return o.scope == "fr-FR"; });
                      checks.expect(scopesCovered, "both approved locales are covered");

                      const auto rendered = [](const vs::Obligation& o, std::string_view node, std::string_view check) {
                          return o.kind == vs::ObligationKind::Rendered && o.nodeId == node && o.check == check;
                      };
                      checks.expect(std::ranges::none_of(result.obligations, [&](const vs::Obligation& o) { return rendered(o, "insufflation-pressure", "ColorHash"); }),
                                    "the tint check is not raised on the scene-driven NumericDisplay");
                      checks.expect(std::ranges::any_of(result.obligations, [&](const vs::Obligation& o) { return rendered(o, "insufflation-pressure", "Bounds"); }),
                                    "the extent check still is");
                      checks.expect(std::ranges::any_of(result.obligations, [&](const vs::Obligation& o) { return rendered(o, "emergency-halt", "ColorHash"); }),
                                    "the tint check is raised on the static critical button");
                      checks.raise();
                  })
            .Execute();
    }};

// --- Obligation enumeration ----------------------------------------------------------------------

const mdux::spec::Register substitutedScenarioId{
    "A scenario.json whose id is not the committed one is rejected before any render", "evidence-unit", [] {
        return speclab::Test("verify-scenario-wrong-id")
            .Given("a directory named 'wrong-name' holding a copy of the committed scenario.json", [] {})
            .When("mdux-verify-scenario is pointed at it", [] {})
            .Then("VSC002 is reported and no device is created",
                  [] {
                      mdux::spec::Checks              checks;
                      mdux::test::TemporaryDirectory  scratch{"verify-scenario-wrong-id"};
                      const std::filesystem::path     dir = scratch.path() / "wrong-name";
                      std::filesystem::create_directories(dir);
                      std::filesystem::copy_file(kBundle / "scenario.json", dir / "scenario.json");

                      const vs::RunResult result = vs::run(dir);
                      checks.expect(result.state == vs::RunState::CouldNotRun, "an unrecognised scenario is an impossible run");
                      checks.expect(hasCode(result.diagnostics, "VSC002"), "the identity mismatch is named");
                      checks.expect(result.renderCount == 0, "nothing rendered");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register substitutedScreenPackage{
    "A digest-mismatched screen package is rejected before any render", "evidence-unit", [] {
        return speclab::Test("verify-scenario-tampered-screen")
            .Given("an artifact root whose committed screen package has been hand-edited", [] {})
            .When("mdux-verify-scenario resolves the screen the scenario names", [] {})
            .Then("VSC004 is reported and no device is created",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"verify-scenario-tampered"};
                      const std::filesystem::path   root = scratch.path();
                      std::filesystem::create_directories(root / "generated" / "scenario" / "endoscope-monitor-basics");
                      std::filesystem::create_directories(root / "generated" / "screen" / "endoscope-monitor");
                      std::filesystem::copy_file(kBundle / "scenario.json",
                                                 root / "generated" / "scenario" / "endoscope-monitor-basics" / "scenario.json");
                      // A non-canonical screen package: a trailing space is enough.
                      std::ifstream in{kRepo / "generated" / "screen" / "endoscope-monitor" / "package.json", std::ios::binary};
                      std::string   text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                      std::ofstream out{root / "generated" / "screen" / "endoscope-monitor" / "package.json", std::ios::binary};
                      out << text << " ";
                      out.close();

                      const vs::RunResult result = vs::run(root / "generated" / "scenario" / "endoscope-monitor-basics",
                                                           vs::RunOptions{.artifactRoot = root / "generated", .frameImageDirectory = {}, .captureDigestPath = {}});
                      checks.expect(result.state == vs::RunState::CouldNotRun, "a substituted package is an impossible run");
                      checks.expect(hasCode(result.diagnostics, "VSC004"), "the non-canonical screen package is named");
                      checks.raise();
                  })
            .Execute();
    }};

// --- Reconciliation (the issue's fail-closed set, pure) ----------------------------------------

const mdux::spec::Register reconcileHoldsWhenComplete{
    "reconcile passes only when every obligation has exactly one held outcome", "evidence-unit", [] {
        return speclab::Test("verify-scenario-reconcile-complete")
            .Given("three binding obligations and three matching held outcomes", [] {})
            .When("reconcile pairs them", [] {})
            .Then("the verdict is Passed and nothing is reported",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::array             obligations{binding("en-US", 1, "clock"), binding("en-US", 2, "state"), binding("fr-FR", 1, "clock")};
                      std::vector<vs::Outcome>     outcomes{heldBinding("en-US", 1, "clock"), heldBinding("en-US", 2, "state"), heldBinding("fr-FR", 1, "clock")};
                      std::vector<cli::Diagnostic> diagnostics;
                      const vs::RunState           state = vs::reconcile(obligations, outcomes, diagnostics);
                      checks.expect(state == vs::RunState::Passed, "a complete, all-held set passes");
                      checks.expect(diagnostics.empty(), "nothing is reported");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register reconcileRejectsWrongValue{
    "reconcile fails and names an obligation whose outcome did not hold", "evidence-unit", [] {
        return speclab::Test("verify-scenario-reconcile-wrong-value")
            .Given("a binding outcome the replay marked failed (a deliberately wrong pinned value)", [] {})
            .When("reconcile pairs it", [] {})
            .Then("the verdict is ChecksFailed and VSC101 names the step",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::array             obligations{binding("en-US", 3, "reading")};
                      std::vector<vs::Outcome>     outcomes{heldBinding("en-US", 3, "reading", /*held=*/false)};
                      std::vector<cli::Diagnostic> diagnostics;
                      const vs::RunState           state = vs::reconcile(obligations, outcomes, diagnostics);
                      checks.expect(state == vs::RunState::ChecksFailed, "a failed outcome fails the run");
                      checks.expect(hasCode(diagnostics, "VSC101"), "the obligation is named");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register reconcileRejectsMissingOutcome{
    "reconcile fails closed on an obligation with no outcome (a dropped batch, an omitted capture)", "evidence-unit", [] {
        return speclab::Test("verify-scenario-reconcile-missing")
            .Given("two obligations but only one outcome", [] {})
            .When("reconcile pairs them", [] {})
            .Then("the missing one becomes a failed outcome and VSC010 is reported",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::array             obligations{binding("en-US", 1, "clock"), binding("en-US", 2, "latch")};
                      std::vector<vs::Outcome>     outcomes{heldBinding("en-US", 1, "clock")};
                      std::vector<cli::Diagnostic> diagnostics;
                      const vs::RunState           state = vs::reconcile(obligations, outcomes, diagnostics);
                      checks.expect(state == vs::RunState::ChecksFailed, "a missing outcome is a failure, not an absence");
                      checks.expect(hasCode(diagnostics, "VSC010"), "the missing obligation is named");
                      checks.expect(outcomes.size() == 2, "a failed outcome was appended so the counts pair");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register reconcileRejectsDuplicate{
    "reconcile flags an obligation with more than one outcome", "evidence-unit", [] {
        return speclab::Test("verify-scenario-reconcile-duplicate")
            .Given("one obligation and two outcomes that pair with it", [] {})
            .When("reconcile pairs them", [] {})
            .Then("VSC011 is reported and the verdict is ChecksFailed",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::array             obligations{binding("en-US", 1, "clock")};
                      std::vector<vs::Outcome>     outcomes{heldBinding("en-US", 1, "clock"), heldBinding("en-US", 1, "clock")};
                      std::vector<cli::Diagnostic> diagnostics;
                      const vs::RunState           state = vs::reconcile(obligations, outcomes, diagnostics);
                      checks.expect(state == vs::RunState::ChecksFailed, "a duplicated outcome fails the run");
                      checks.expect(hasCode(diagnostics, "VSC011"), "the duplication is named");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register reconcileRejectsSurplusOutcome{
    "reconcile fails closed on an outcome that discharges no enumerated obligation", "evidence-unit", [] {
        return speclab::Test("verify-scenario-reconcile-surplus")
            .Given("one obligation and two outcomes, only one of which pairs with it", [] {})
            .When("reconcile pairs them", [] {})
            .Then("VSC014 is reported and the verdict is ChecksFailed",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::array             obligations{binding("en-US", 1, "clock")};
                      std::vector<vs::Outcome>     outcomes{heldBinding("en-US", 1, "clock"), heldBinding("fr-FR", 9, "reading")};
                      std::vector<cli::Diagnostic> diagnostics;
                      const vs::RunState           state = vs::reconcile(obligations, outcomes, diagnostics);
                      checks.expect(state == vs::RunState::ChecksFailed, "a surplus outcome fails the run - the verdict cannot stay Passed");
                      checks.expect(hasCode(diagnostics, "VSC014"), "the surplus outcome is named");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register substitutedScenarioSteps{
    "A scenario.json whose steps do not match the reviewed constexpr is rejected before any render", "evidence-unit", [] {
        return speclab::Test("verify-scenario-substituted-steps")
            .Given("a copy of the committed scenario.json with one advance step's frame count changed", [] {})
            .When("mdux-verify-scenario is pointed at it", [] {})
            .Then("VSC002 is reported and no device is created",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"verify-scenario-substituted-steps"};
                      const std::filesystem::path   dir = scratch.path() / "endoscope-monitor-basics";
                      std::filesystem::create_directories(dir);
                      std::ifstream in{kBundle / "scenario.json", std::ios::binary};
                      std::string   text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                      // The last `advance` in the committed scenario drives 27 frames; make it 26. The
                      // JSON still parses, but the step sequence no longer matches the constexpr.
                      const auto pos = text.find("\"frames\": 27");
                      checks.expect(pos != std::string::npos, "the committed scenario has the expected advance");
                      if (pos != std::string::npos) {
                          text.replace(pos, std::string_view{"\"frames\": 27"}.size(), "\"frames\": 26");
                      }
                      std::ofstream out{dir / "scenario.json", std::ios::binary};
                      out << text;
                      out.close();

                      const vs::RunResult result = vs::run(dir);
                      checks.expect(result.state == vs::RunState::CouldNotRun, "a step-substituted scenario is an impossible run");
                      checks.expect(hasCode(result.diagnostics, "VSC002"), "the substitution is named");
                      checks.expect(result.renderCount == 0, "nothing rendered");
                      checks.raise();
                  })
            .Execute();
    }};

// --- The artifact writer -----------------------------------------------------------------------

const mdux::spec::Register writerCanonicalises{
    "writeScenarioVerification serialises a completed run as canonical scenario JSON", "evidence-unit", [] {
        return speclab::Test("verify-scenario-writer-canonical")
            .Given("a RunResult with one binding obligation and its held outcome", [] {})
            .When("writeScenarioVerification runs", [] {})
            .Then("the text is canonical and names the artifact kind",
                  [] {
                      mdux::spec::Checks checks;
                      vs::RunResult      r;
                      r.state       = vs::RunState::Passed;
                      r.obligations = {binding("en-US", 1, "clock")};
                      r.outcomes    = {heldBinding("en-US", 1, "clock")};
                      r.inputs      = {vu::BoundArtifact{.role = "scenarioPackage", .id = "x", .locale = {}, .sha256 = "abc"}};

                      const auto text = vs::writeScenarioVerification(r, "x");
                      checks.expect(text.has_value(), "a completed run serialises");
                      if (text) {
                          checks.expect(text->find("\"kind\": \"scenario\"") != std::string::npos, "the artifact kind is recorded");
                          checks.expect(text->back() == '\n', "the canonical text ends with a newline");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writerRefusesNotRun{
    "writeScenarioVerification refuses a run that could not be made", "evidence-unit", [] {
        return speclab::Test("verify-scenario-writer-not-run")
            .Given("a RunResult still in its default CouldNotRun state", [] {})
            .When("writeScenarioVerification runs", [] {})
            .Then("it returns ArtifactError::NotRun",
                  [] {
                      mdux::spec::Checks checks;
                      vs::RunResult      r;  // state defaults to CouldNotRun
                      r.obligations = {binding("en-US", 1, "clock")};
                      const auto text = vs::writeScenarioVerification(r, "x");
                      checks.expect(!text.has_value() && text.error() == vs::ArtifactError::NotRun, "an impossible run produces no file");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writerRefusesBadProfile{
    "writeScenarioVerification refuses a rendered outcome carrying the wrong observation profile", "evidence-unit", [] {
        return speclab::Test("verify-scenario-writer-bad-profile")
            .Given("a rendered Bounds outcome tagged with the tint-composition profile", [] {})
            .When("writeScenarioVerification runs", [] {})
            .Then("it returns ArtifactError::RenderedProfileInvalid",
                  [] {
                      mdux::spec::Checks checks;
                      vs::RunResult      r;
                      r.state = vs::RunState::Passed;
                      const vs::Obligation o{.kind = vs::ObligationKind::Rendered, .scope = "en-US", .capture = "freeze-pressed", .stepIndex = 0, .expectKind = {}, .nodeId = "emergency-halt", .check = "Bounds"};
                      r.obligations = {o};
                      r.outcomes    = {vs::Outcome{.kind = vs::ObligationKind::Rendered, .scope = "en-US", .capture = "freeze-pressed", .stepIndex = 0, .expectKind = {}, .nodeId = "emergency-halt", .check = "Bounds", .held = true, .finding = mv::Finding::Held, .profile = mv::tintCompositionProfile}};
                      const auto text = vs::writeScenarioVerification(r, "x");
                      checks.expect(!text.has_value() && text.error() == vs::ArtifactError::RenderedProfileInvalid, "a mislabelled observation is refused");
                      checks.raise();
                  })
            .Execute();
    }};

// --- CLI policy --------------------------------------------------------------------------------

const mdux::spec::Register localesCannotNarrow{
    "the scenario verifier CLI refuses any locale selection but --locales=all", "evidence-unit", [] {
        return speclab::Test("verify-scenario-locales-cannot-narrow")
            .Given("a --locales= argument naming one locale", [] {})
            .When("the CLI boundary parses it", [] {})
            .Then("it throws a UsageError",
                  [] {
                      mdux::spec::Checks   checks;
                      constexpr std::array narrow{std::string_view{"--scenario=generated/scenario/x"}, std::string_view{"--locales=fr-FR"}};
                      bool                 threw = false;
                      try {
                          static_cast<void>(vs::parseArguments(narrow));
                      } catch (const cli::UsageError&) {
                          threw = true;
                      }
                      checks.expect(threw, "a narrowed manifest is refused");

                      constexpr std::array ok{std::string_view{"--scenario=generated/scenario/x"}, std::string_view{"--locales=all"}};
                      const auto           invocation = vs::parseArguments(ok);
                      checks.expect(invocation.scenarioDirectory == "generated/scenario/x", "the accepted form keeps the bundle path");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
