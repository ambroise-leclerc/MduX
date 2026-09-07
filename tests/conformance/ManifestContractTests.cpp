/**
 * @file ManifestContractTests.cpp
 * @brief The pinned `conformance/contracts` consumer-manifest cases, run against MduX's validator.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 *
 * `schemas/consumer-manifest.schema.json` is the parsed shape of a consumer's
 * `medui-conformance.toml`. Every `conformance/contracts` case whose `schema` is `consumer-manifest`
 * carries a `document` and a `valid` flag; MduX's `validateConsumerManifest()` - the same one that
 * gates MduX's own manifest - must accept exactly the documents the contract marks valid. This is
 * what keeps the `profiles` key MduX now writes honest against the contract that defines it.
 *
 * The manifest MduX itself ships is validated by `CorpusFixture::manifest()`, exercised by every
 * other conformance scenario; this file adds the corpus's adversarial documents.
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
namespace toml = mdux::tools::toml;
using namespace mdux::conformance;

/// Runs one `medui-conformance.toml`-shaped string through the same path `manifest()` uses.
[[nodiscard]] std::optional<std::string> validateManifestText(std::string_view text) {
    const toml::Document document = toml::parse(text);
    return validateConsumerManifest(consumerManifestView(document.root(), "<fixture>"));
}

const mdux::spec::Register manifestContractCases{
    "MduX's consumer-manifest validator agrees with every pinned contract case",
    "conformance",
    [] {
        return speclab::Test("medui-consumer-manifest-contract")
            .Given("the conformance/contracts consumer-manifest cases in the pinned checkout", [] {})
            .When("each document is run through validateConsumerManifest()", [] {})
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

                      // The consumer-manifest schema is embedded (so `manifest()` needs no checkout);
                      // this is the guard that it has not drifted from the file the contract ships.
                      checks.expect(jsonEqual(consumerManifestSchema(), pinnedSchema(*root, "consumer-manifest")),
                                    "the embedded consumer-manifest schema equals schemas/consumer-manifest.schema.json");

                      const std::filesystem::path directory = *root / "conformance" / "contracts";
                      if (!std::filesystem::is_directory(directory)) {
                          checks.expect(false, "the pinned checkout has a conformance/contracts/ directory");
                          checks.raise();
                          return;
                      }

                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{directory}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);

                      std::size_t manifestCases = 0;
                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          if (requireString(*document, "schema", path) != "consumer-manifest") {
                              continue;
                          }
                          ++manifestCases;

                          const bool                       expectedValid = requireBool(*document, "valid", path);
                          const std::optional<std::string> error         = validateConsumerManifest(member(*document, "document", path));
                          checks.expect(error.has_value() != expectedValid,
                                        std::format("{}: contract says valid={}, validator {}{}",
                                                    path.filename().string(),
                                                    expectedValid,
                                                    error ? "rejected: " : "accepted",
                                                    error.value_or(std::string{})));
                      }

                      checks.expect(manifestCases == 10,
                                    std::format("all 10 pinned consumer-manifest cases ran, saw {}", manifestCases));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register manifestValidatorRejectsBrokenClaims{
    "The manifest validator rejects a malformed medui-conformance.toml",
    "conformance",
    [] {
        return speclab::Test("medui-consumer-manifest-negative")
            .Given("medui-conformance.toml strings with one broken field each", [] {})
            .When("each is run through the validator that gates MduX's real manifest", [] {})
            .Then("every one is rejected, and the good baseline is accepted",
                  [] {
                      mdux::spec::Checks checks;
                      constexpr std::string_view good =
                          "repository = \"https://github.com/Compliatory/MedUI\"\n"
                          "commit = \"9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb\"\n"
                          "capabilities = [\"syntax\"]\n"
                          "positions = \"full\"\n";
                      checks.expect(!validateManifestText(good).has_value(), "the well-formed baseline is accepted");

                      const std::array<std::pair<std::string_view, std::string>, 5> broken{{
                          {"an unknown key", std::string{good} + "profile = [\"MEDUI-PROFILE-RENDERED\"]\n"},
                          {"an empty profiles array", std::string{good} + "profiles = []\n"},
                          {"an unknown profile id", std::string{good} + "profiles = [\"MEDUI-PROFILE-BOGUS\"]\n"},
                          {"a duplicated profile", std::string{good} + "profiles = [\"MEDUI-PROFILE-RENDERED\", \"MEDUI-PROFILE-RENDERED\"]\n"},
                          {"a short commit",
                           "repository = \"https://github.com/Compliatory/MedUI\"\ncommit = \"abc123\"\ncapabilities = [\"syntax\"]\npositions = \"full\"\n"},
                      }};
                      for (const auto& [label, text] : broken) {
                          const std::optional<std::string> error = validateManifestText(text);
                          checks.expect(error.has_value(), std::format("{} is rejected", label));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
