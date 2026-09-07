/**
 * @file EvidenceContractTests.cpp
 * @brief The pinned `conformance/contracts` `evidence`-schema documents (#314).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 *
 * `schemas/evidence.schema.json` is the E01 identity envelope: 15 fields per obligation and per
 * report row, with hex patterns, closed enums for `profile`/`check`, and `$ref`/`$defs` for the
 * shared identity shape. Each `conformance/contracts` case whose `schema` is `evidence` carries a
 * `document` and a `valid` flag; `mdux.tools.schema` run against the pinned schema file must accept
 * exactly the documents the contract marks valid.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.tools.schema;
import mdux.tools.toml;

#include "../framework/SpecLabBridge.hpp"
#include "../conformance/CorpusFixture.hpp"

namespace {

namespace json   = mdux::evidence::json;
namespace schema = mdux::tools::schema;
using namespace mdux::conformance;

const mdux::spec::Register evidenceContractCases{
    "The schema validator agrees with every pinned evidence contract case",
    "conformance",
    [] {
        return speclab::Test("medui-evidence-contract")
            .Given("the conformance/contracts evidence cases in the pinned checkout", [] {})
            .When("each document is validated against schemas/evidence.schema.json", [] {})
            .Then("acceptance matches the case's `valid` flag",
                  [] {
                      mdux::spec::Checks checks;

                      const std::optional<std::filesystem::path> root = corpusRootOrSkip(checks);
                      if (!root) {
                          checks.raise();
                          return;
                      }
                      checks.expect(checkoutRevision(*root) == manifest().commit,
                                    "the checkout is at the revision medui-conformance.toml pins");

                      const json::Value evidenceSchema = pinnedSchema(*root, "evidence");
                      checks.expect(schema::checkSchema(evidenceSchema).empty(),
                                    "mdux.tools.schema can enforce every keyword schemas/evidence.schema.json uses");

                      const std::filesystem::path directory = *root / "conformance" / "contracts";
                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{directory}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);

                      std::size_t evidenceCases = 0;
                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          if (requireString(*document, "schema", path) != "evidence") {
                              continue;
                          }
                          ++evidenceCases;

                          const bool                     expectedValid = requireBool(*document, "valid", path);
                          const std::vector<std::string> problems      = schema::validate(member(*document, "document", path), evidenceSchema);
                          checks.expect(problems.empty() == expectedValid,
                                        std::format("{}: contract says valid={}, validator {}",
                                                    path.filename().string(),
                                                    expectedValid,
                                                    problems.empty() ? "accepted" : std::format("rejected ({})", problems.front())));
                      }

                      checks.expect(evidenceCases == 29,
                                    std::format("all 29 pinned evidence cases ran, saw {}", evidenceCases));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register negativeEvidenceDocumentsAreRejected{
    "The schema validator rejects a malformed evidence envelope",
    "conformance",
    [] {
        return speclab::Test("medui-evidence-schema-negative")
            .Given("committed evidence envelopes with one broken identity field each", [] {})
            .When("each is validated against schemas/evidence.schema.json", [] {})
            .Then("every one is rejected",
                  [] {
                      mdux::spec::Checks checks;

                      const std::optional<std::filesystem::path> root = corpusRootOrSkip(checks);
                      if (!root) {
                          checks.raise();
                          return;
                      }
                      const json::Value evidenceSchema = pinnedSchema(*root, "evidence");

                      const std::filesystem::path directory =
                          std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "conformance" / "fixtures" / "evidence-invalid";
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
                          checks.expect(!schema::validate(*document, evidenceSchema).empty(),
                                        std::format("{}: the validator rejects it", path.filename().string()));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
