/**
 * @file MeduiEvidenceTests.cpp
 * @brief End-to-end: a real verify run's derived MEDUI-PROFILE-RENDERED evidence envelope.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-014 What rendered-truth verification checks - decision 4
 * @compliance ADR-016 Locally versioned observation profiles
 *
 * `deriveRenderedEvidence()` turns a `RunResult` into MedUI's shared E01 envelope. The pure
 * derivation is covered by a host-only scenario over a hand-built `RunResult`; this one runs the
 * production driver against `generated/screen/endoscope-monitor` on a real Vulkan device and checks
 * the emitted envelope end to end: valid against the pinned `evidence.schema.json`, and aggregating
 * to `pass` through the same E01-E03 list logic `conformance_spec` runs. Labelled `verify`, so it
 * runs on the GPU-capable legs alongside the committed `verify.screen.<id>` gate. A device that has
 * vanished since the bake (which #254 made render) is a failure, not a skip.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.tools.schema;
import mdux.tools.toml;
import mdux.tools.verify.driver;
import mdux.tools.verify.medui_evidence;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "../conformance/CorpusFixture.hpp"

namespace {

namespace json = mdux::evidence::json;
namespace vu   = mdux::tools::verify;
using namespace mdux::conformance;

/// A hand-built passing `RunResult` over one node, with realistic digests so the derived envelope
/// validates against `evidence.schema.json`. `finding` chooses the golden check's outcome.
[[nodiscard]] vu::RunResult syntheticRun(mdux::verify::Finding boundsFinding) {
    const std::string sha64(64, 'a');
    const std::string sha64b(64, 'b');
    vu::RunResult      run;
    run.state         = vu::RunState::Passed;
    run.renderCount   = 1;
    run.backend       = "synthetic-test-device";
    run.surfaceWidth  = 32;
    run.surfaceHeight = 24;
    run.inputs        = {
        {.role = "screenPackage", .id = "demo", .locale = {}, .sha256 = std::string(64, 'c')},
        {.role = "goldens", .id = "demo", .locale = {}, .sha256 = sha64},
        {.role = "shaderPackage", .id = "mdux-ui", .locale = {}, .sha256 = sha64b},
        {.role = "fontPackage", .id = "roboto", .locale = "en-US", .sha256 = sha64},
        {.role = "textPackage", .id = "demo-en", .locale = "en-US", .sha256 = sha64b},
    };
    run.obligations = {
        {.kind = vu::ObligationKind::Golden, .nodeId = "title", .scope = "en-US", .check = "Bounds"},
        {.kind = vu::ObligationKind::Golden, .nodeId = "title", .scope = "en-US", .check = "ColorHash"},
    };
    run.outcomes = {
        {.finding = boundsFinding, .nodeId = "title", .scope = "en-US", .check = "Bounds", .profile = mdux::verify::extentEqualityProfile},
        {.finding = mdux::verify::Finding::Held, .nodeId = "title", .scope = "en-US", .check = "ColorHash", .profile = mdux::verify::tintCompositionProfile},
    };
    return run;
}

const std::filesystem::path manifestPath{std::filesystem::path{MDUX_REPO_ROOT} / "medui-conformance.toml"};

const mdux::spec::Register endoscopeMonitorEmitsAValidPassingEnvelope{
    "mdux-verify-ui derives a schema-valid, self-consistent RENDERED evidence envelope",
    "verify",
    [] {
        return speclab::Test("medui-evidence-endoscope-monitor")
            .Given("a real verify run of generated/screen/endoscope-monitor on this device", [] {})
            .When("its RunResult is passed to deriveRenderedEvidence()", [] {})
            .Then("the envelope validates against evidence.schema.json and aggregates to pass",
                  [] {
                      mdux::spec::Checks                 checks;
                      const std::filesystem::path       repo{MDUX_REPO_ROOT};
                      const vu::RunResult               result =
                          vu::run(repo / "generated" / "screen" / "endoscope-monitor", repo / "generated");

                      if (result.state == vu::RunState::NoRenderDevice) {
                          // The bake rendered this screen (#254), so a device provably existed at
                          // build time. One that is gone now is infrastructure, not an absent GPU.
                          checks.expect(false, "a device rendered this screen at bake time but is unavailable now");
                          checks.raise();
                          return;
                      }
                      checks.expect(result.state == vu::RunState::Passed,
                                    std::format("the committed screen still verifies (state {})", static_cast<int>(result.state)));

                      const auto derived = vu::deriveRenderedEvidence(result, "endoscope-monitor", repo / "medui-conformance.toml");
                      if (!derived.has_value()) {
                          checks.expect(false, std::format("deriveRenderedEvidence failed: {}", vu::describe(derived.error())));
                          checks.raise();
                          return;
                      }

                      // 3 Bounds + 2 ColorHash + 2 InkContainment map; the 2 LocalizedTextPresence
                      // outcomes are implementation-local (mdux.local/ink-coverage) and excluded.
                      checks.expect(derived->excludedOutcomes == 2,
                                    std::format("2 implementation-local outcomes excluded, saw {}", derived->excludedOutcomes));

                      const json::Value& envelope     = derived->envelope;
                      const json::Value* obligations  = envelope.find("obligations");
                      const json::Value* rows         = envelope.find("rows");
                      checks.expect(obligations != nullptr && rows != nullptr && obligations->elements().size() == 7 && rows->elements().size() == 7,
                                    "7 obligations and 7 rows");

                      // Valid against the shared schema.
                      const std::optional<std::filesystem::path> corpus = corpusRootOrSkip(checks);
                      if (!corpus) {
                          checks.raise();
                          return;
                      }
                      const json::Value evidenceSchema = pinnedSchema(*corpus, "evidence");
                      const std::vector<std::string> problems = mdux::tools::schema::validate(envelope, evidenceSchema);
                      checks.expect(problems.empty(),
                                    std::format("the envelope is valid against evidence.schema.json{}",
                                                problems.empty() ? "" : std::format(" (first: {})", problems.front())));

                      // Aggregates to pass through the same E01-E03 logic conformance_spec runs.
                      if (obligations != nullptr && rows != nullptr) {
                          const Aggregate aggregate = aggregateEvidence(
                              obligations->elements(), rows->elements(), identitySchemaFromEvidence(evidenceSchema), "<derived envelope>");
                          checks.expect(aggregate.outcome == "pass",
                                        std::format("aggregate-evidence over the derived envelope is pass, got {}", aggregate.outcome));
                          checks.expect(std::ranges::all_of(aggregate.rowOutcomes, [](const std::string& o) { return o == "pass"; }),
                                        "every row outcome is pass");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register derivationIsPureAndSchemaValid{
    "deriveRenderedEvidence maps checks and outcomes without a device",
    "evidence-unit",
    [] {
        return speclab::Test("medui-evidence-derivation")
            .Given("hand-built RunResults over one node", [] {})
            .When("deriveRenderedEvidence runs on each", [] {})
            .Then("the envelope is schema-valid and Finding maps to the row outcome",
                  [] {
                      mdux::spec::Checks checks;

                      // A held Bounds and a held ColorHash -> two rows, both pass, schema-valid.
                      const auto held = vu::deriveRenderedEvidence(syntheticRun(mdux::verify::Finding::Held), "demo", manifestPath);
                      if (!held.has_value()) {
                          checks.expect(false, std::format("derive failed: {}", vu::describe(held.error())));
                          checks.raise();
                          return;
                      }
                      checks.expect(held->excludedOutcomes == 0, "nothing excluded");
                      const json::Value& env = held->envelope;
                      checks.expect(env.find("obligations")->elements().size() == 2 && env.find("rows")->elements().size() == 2, "two obligations, two rows");

                      const std::optional<std::filesystem::path> corpus = corpusRootOrSkip(checks);
                      if (!corpus) {
                          checks.raise();
                          return;
                      }
                      const json::Value evidenceSchema = pinnedSchema(*corpus, "evidence");
                      checks.expect(mdux::tools::schema::validate(env, evidenceSchema).empty(), "held envelope is schema-valid");
                      const Aggregate heldAgg = aggregateEvidence(env.find("obligations")->elements(), env.find("rows")->elements(),
                                                                  identitySchemaFromEvidence(evidenceSchema), "<synthetic held>");
                      checks.expect(heldAgg.outcome == "pass", "held run aggregates to pass");

                      // NoBaseline -> missing-baseline; the aggregate then fails (not every row passed).
                      const auto noBaseline = vu::deriveRenderedEvidence(syntheticRun(mdux::verify::Finding::NoBaseline), "demo", manifestPath);
                      checks.expect(noBaseline.has_value(), "derive succeeds for a NoBaseline outcome");
                      if (noBaseline.has_value()) {
                          const auto& rows = noBaseline->envelope.find("rows")->elements();
                          checks.expect(rows.size() == 2 && rows[0].find("outcome")->asString().value_or("") == "missing-baseline",
                                        "the NoBaseline Bounds outcome becomes missing-baseline");
                          const Aggregate agg = aggregateEvidence(noBaseline->envelope.find("obligations")->elements(), rows,
                                                                  identitySchemaFromEvidence(evidenceSchema), "<synthetic no-baseline>");
                          checks.expect(agg.outcome == "fail", "a missing-baseline row makes the aggregate fail");
                      }

                      // A ForeignColour finding -> fail.
                      const auto foreign = vu::deriveRenderedEvidence(syntheticRun(mdux::verify::Finding::ForeignColour), "demo", manifestPath);
                      checks.expect(foreign.has_value(), "derive succeeds for a ForeignColour outcome");
                      if (foreign.has_value()) {
                          checks.expect(foreign->envelope.find("rows")->elements()[0].find("outcome")->asString().value_or("") == "fail",
                                        "a ForeignColour Bounds outcome becomes fail");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register derivationRefusesUnusableRuns{
    "deriveRenderedEvidence refuses a run with nothing shared to publish",
    "evidence-unit",
    [] {
        return speclab::Test("medui-evidence-derivation-refusals")
            .Given("a run that could not be made, and one with only implementation-local outcomes", [] {})
            .When("deriveRenderedEvidence runs on each", [] {})
            .Then("each is refused with the right error", [] {
                mdux::spec::Checks checks;

                vu::RunResult couldNotRun;
                couldNotRun.state = vu::RunState::CouldNotRun;
                const auto notRun = vu::deriveRenderedEvidence(couldNotRun, "demo", manifestPath);
                checks.expect(!notRun.has_value() && notRun.error() == vu::EvidenceError::NotRun, "a run that never evaluated is NotRun");

                vu::RunResult localOnly = syntheticRun(mdux::verify::Finding::Held);
                localOnly.obligations = {{.kind = vu::ObligationKind::Text, .nodeId = "title", .scope = "en-US", .check = "LocalizedTextPresence"}};
                localOnly.outcomes    = {{.finding = mdux::verify::Finding::Held, .nodeId = "title", .scope = "en-US", .check = "LocalizedTextPresence",
                                          .profile = mdux::verify::inkCoverageProfile}};
                const auto mapless = vu::deriveRenderedEvidence(localOnly, "demo", manifestPath);
                checks.expect(!mapless.has_value() && mapless.error() == vu::EvidenceError::NoMappableObligation,
                              "a run of only LocalizedTextPresence has no shared obligation");

                // A missing outcome (fewer than the enumerated obligations) is refused, not
                // published as a smaller-but-self-consistent envelope.
                vu::RunResult dropped = syntheticRun(mdux::verify::Finding::Held);
                dropped.outcomes.pop_back();
                const auto mismatch = vu::deriveRenderedEvidence(dropped, "demo", manifestPath);
                checks.expect(!mismatch.has_value() && mismatch.error() == vu::EvidenceError::OutcomeMismatch,
                              "an incomplete outcome set is OutcomeMismatch");

                // A substituted outcome for the wrong node is refused.
                vu::RunResult substituted   = syntheticRun(mdux::verify::Finding::Held);
                substituted.outcomes[0].nodeId = "someone-else";
                const auto substituteResult = vu::deriveRenderedEvidence(substituted, "demo", manifestPath);
                checks.expect(!substituteResult.has_value() && substituteResult.error() == vu::EvidenceError::OutcomeMismatch,
                              "an outcome paired to the wrong obligation is OutcomeMismatch");

                // An unrecognised check name is refused, not silently excluded.
                vu::RunResult unknown       = syntheticRun(mdux::verify::Finding::Held);
                unknown.obligations[0].check = "SomethingElse";
                unknown.outcomes[0].check    = "SomethingElse";
                const auto unknownResult    = vu::deriveRenderedEvidence(unknown, "demo", manifestPath);
                checks.expect(!unknownResult.has_value() && unknownResult.error() == vu::EvidenceError::UnknownCheck,
                              "an unrecognised check name is UnknownCheck");
                checks.raise();
            })
            .Execute();
    }};

}  // namespace
