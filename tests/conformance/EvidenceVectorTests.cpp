/**
 * @file EvidenceVectorTests.cpp
 * @brief The pinned `MEDUI-PROFILE-EVIDENCE` observation vectors (rules E01-E03).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-016 Locally versioned observation profiles
 *
 * `spec/profiles.md` rules E01-E03. Every `conformance/profiles` vector whose `inputs.operation`
 * is `aggregate-evidence` supplies an `obligations` array (each entry an identity) and a `rows`
 * array (each `{ identity, outcome }`), with a required aggregate `outcome` and optional
 * `rowOutcomes`. The adapter matches rows to obligations by fieldwise identity equality (E01,
 * key-order independent), requires a one-to-one mapping (E02), and aggregates row outcomes (E03) -
 * pure list logic, the same shape as the RENDERED adapters. `medui-conformance.toml` claims
 * `MEDUI-PROFILE-EVIDENCE`, so a missing corpus is a hard failure under `CI` and a claimed rule
 * with no vector fails the coverage check.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.tools.schema;
import mdux.tools.toml;

#include "../framework/SpecLabBridge.hpp"
#include "../conformance/CorpusFixture.hpp"

namespace {

namespace json = mdux::evidence::json;
using namespace mdux::conformance;

struct Aggregate {
    std::string              outcome;
    std::vector<std::string> rowOutcomes;
};

/// The identity constraint `evidence.schema.json` delegates to the harness ("ordering and ID
/// uniqueness are semantic identity constraints"): `assets` ids must be unique and strictly
/// ascending in ASCII byte order. `spec/profiles.md` E01: a malformed identity fails aggregation.
[[nodiscard]] bool identityMalformed(const json::Value& identity) {
    const json::Value* assets = identity.find("assets");
    if (assets == nullptr) {
        return false;  // a structurally absent field is the schema's concern, not this check's
    }
    if (assets->kind() != json::Value::Kind::Array) {
        return true;  // `assets` present but not an array is a malformed identity
    }
    std::string_view previous;
    for (std::size_t i = 0; i < assets->elements().size(); ++i) {
        const json::Value*              id = assets->elements()[i].find("id");
        std::optional<std::string_view> text;
        if (id != nullptr) {
            if (const auto value = id->asString()) {
                text = *value;
            }
        }
        if (!text) {
            return true;
        }
        if (i > 0 && !(previous < *text)) {  // strictly ascending also rules out a duplicate id
            return true;
        }
        previous = *text;
    }
    return false;
}

/// E02 + E03 over `obligations` (each an identity) and `rows` (each `{ identity, outcome }`).
[[nodiscard]] Aggregate aggregateEvidence(std::span<const json::Value> obligations,
                                          std::span<const json::Value> rows,
                                          const std::filesystem::path& path) {
    Aggregate aggregate;
    for (const json::Value& row : rows) {
        aggregate.rowOutcomes.emplace_back(requireString(row, "outcome", path));
    }

    // E03: an empty obligation set with no rows is not-run, never pass.
    if (obligations.empty() && rows.empty()) {
        aggregate.outcome = "not-run";
        return aggregate;
    }

    // E01: a malformed identity fails aggregation before any comparison.
    for (const json::Value& obligation : obligations) {
        if (identityMalformed(obligation)) {
            aggregate.outcome = "fail";
            return aggregate;
        }
    }
    for (const json::Value& row : rows) {
        if (identityMalformed(member(row, "identity", path))) {
            aggregate.outcome = "fail";
            return aggregate;
        }
    }

    // E02: exactly one matching row per obligation, and exactly one matching obligation per row.
    bool complete = obligations.size() == rows.size();
    if (complete) {
        for (const json::Value& obligation : obligations) {
            const auto matches = std::ranges::count_if(rows, [&](const json::Value& row) {
                return jsonEqual(member(row, "identity", path), obligation);
            });
            complete = complete && matches == 1;
        }
        for (const json::Value& row : rows) {
            const auto matches = std::ranges::count_if(obligations, [&](const json::Value& obligation) {
                return jsonEqual(member(row, "identity", path), obligation);
            });
            complete = complete && matches == 1;
        }
    }
    if (!complete) {
        aggregate.outcome = "fail";
        return aggregate;
    }

    // E03: pass only when every row's own outcome is pass.
    const bool allPassed = !rows.empty() && std::ranges::all_of(aggregate.rowOutcomes, [](const std::string& outcome) {
        return outcome == "pass";
    });
    aggregate.outcome = allPassed ? "pass" : "fail";
    return aggregate;
}

const mdux::spec::Register evidenceProfileVectors{
    "Every MEDUI-PROFILE-EVIDENCE vector aggregates as the contract expects",
    "conformance",
    [] {
        return speclab::Test("medui-profile-evidence-vectors")
            .Given("the aggregate-evidence vectors in the pinned MedUI checkout", [] {})
            .When("each is run through the E01-E03 list logic", [] {})
            .Then("the aggregate and row outcomes match, and every claimed rule ran",
                  [] {
                      mdux::spec::Checks checks;

                      const std::optional<std::filesystem::path> root = corpusRootOrSkip(checks);
                      if (!root) {
                          checks.raise();
                          return;
                      }
                      const Manifest pinned = manifest();
                      checks.expect(checkoutRevision(*root) == pinned.commit,
                                    "the checkout is at the revision medui-conformance.toml pins");
                      checks.expect(std::ranges::find(pinned.profiles, "MEDUI-PROFILE-EVIDENCE") != pinned.profiles.end(),
                                    "medui-conformance.toml claims MEDUI-PROFILE-EVIDENCE");

                      const std::filesystem::path vectorsDir = *root / "conformance" / "profiles";
                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{vectorsDir}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);

                      std::set<std::string> rulesExercised;
                      std::size_t           evidenceVectors = 0;

                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          const json::Value& inputs = member(*document, "inputs", path);
                          if (requireString(inputs, "operation", path) != "aggregate-evidence") {
                              continue;
                          }
                          ++evidenceVectors;

                          checks.expect(requireString(member(*document, "profile", path), "id", path) == "MEDUI-PROFILE-EVIDENCE",
                                        std::format("{} is a MEDUI-PROFILE-EVIDENCE vector", path.filename().string()));
                          for (const json::Value& rule : requireArray(*document, "rules", path)) {
                              rulesExercised.insert(std::string{rule.asString().value_or("?")});
                          }

                          const Aggregate     got = aggregateEvidence(requireArray(inputs, "obligations", path),
                                                                      requireArray(inputs, "rows", path),
                                                                      path);
                          const json::Value&  expected = member(*document, "expected", path);
                          checks.expect(got.outcome == requireString(expected, "outcome", path),
                                        std::format("{}: aggregate expected {}, adapter said {}",
                                                    path.filename().string(), requireString(expected, "outcome", path), got.outcome));
                          if (const json::Value* rowOutcomes = expected.find("rowOutcomes")) {
                              std::vector<std::string> wanted;
                              for (const json::Value& outcome : rowOutcomes->elements()) {
                                  wanted.emplace_back(outcome.asString().value_or("?"));
                              }
                              checks.expect(got.rowOutcomes == wanted,
                                            std::format("{}: row outcomes match the vector", path.filename().string()));
                          }
                      }

                      checks.expect(evidenceVectors == 31,
                                    std::format("all 31 pinned aggregate-evidence vectors ran, saw {}", evidenceVectors));
                      for (std::string_view rule : registryRulesFor(*root, "MEDUI-PROFILE-EVIDENCE")) {
                          checks.expect(rulesExercised.contains(std::string{rule}),
                                        std::format("claimed EVIDENCE rule {} was exercised by a vector that ran", rule));
                      }
                      checks.expect(!registryRulesFor(*root, "MEDUI-PROFILE-EVIDENCE").empty(),
                                    "registry.json lists rules for MEDUI-PROFILE-EVIDENCE");

                      std::cerr << std::format("MedUI EVIDENCE profile: {} vector(s) at {}\n", evidenceVectors, pinned.commit);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register evidenceGateCatchesAWrongOutcome{
    "The evidence gate fails a vector whose aggregate disagrees with the stated outcome",
    "conformance",
    [] {
        return speclab::Test("medui-profile-evidence-negative-outcome")
            .Given("a committed aggregate-evidence vector whose expected.outcome is deliberately wrong", [] {})
            .When("it is run through the same list logic the real gate uses", [] {})
            .Then("the adapter's answer differs from the fixture's",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::filesystem::path directory =
                          std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "conformance" / "fixtures" / "evidence-wrong-outcome";
                      checks.expect(std::filesystem::is_directory(directory), "the negative fixture tree is committed");

                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{directory}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);
                      checks.expect(!paths.empty(), "there is at least one negative fixture");

                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          const json::Value& inputs = member(*document, "inputs", path);
                          const Aggregate    got    = aggregateEvidence(requireArray(inputs, "obligations", path),
                                                                        requireArray(inputs, "rows", path), path);
                          const std::string  stated = requireString(member(*document, "expected", path), "outcome", path);
                          checks.expect(got.outcome != stated,
                                        std::format("{}: adapter says '{}', which differs from the wrong stated '{}'",
                                                    path.filename().string(), got.outcome, stated));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
