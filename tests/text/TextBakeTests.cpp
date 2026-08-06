/**
 * @file TextBakeTests.cpp
 * @brief BDD scenarios for the text baker's recipe parser and bake/write/verify core (#157).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * `text_spec` covers the governed schema; this file covers the host-tools baker that uses it,
 * so the ~300 lines of `mdux-textbake` core do not ship untested. The cases mirror the corpus
 * shape `SafetensorsTests.cpp` uses for the ML baker: each diagnostic path is pinned to its
 * stable code rather than checked merely for "it was rejected". `run()` here also exercises the
 * S1 contract - the produced `packageJson` parses via `TextPackage::parse()` and round-trips -
 * so a wave that breaks it fails this PR rather than silently changing the first committed
 * text package (#160).
 */

import std;
import speclab;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.textbake;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace cli = mdux::tools::cli;
namespace bake = mdux::tools::textbake;
namespace text = mdux::text;

/// A well-formed no-run recipe, the only shape `run()` produces at S1.
[[nodiscard]] std::string validRecipe() {
    return R"([package]
id = "label-welcome"
atlas = "roboto-ui"
locale = "en-US"
sidecar = "runs.bin"
)";
}

/// Parses `recipeText` and reports the first diagnostic code, or "ok" when it parses.
[[nodiscard]] std::string parseCode(std::string_view recipeText) {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = bake::parseRecipe(recipeText, "fixture.toml", diagnostics);
    if (recipe.has_value()) {
        return "ok";
    }
    if (diagnostics.empty()) {
        return "(no diagnostic)";
    }
    return diagnostics.front().code;
}

/// Runs `run()` against a valid recipe and returns the produced `BakeOutputs`, asserting that
/// `parseRecipe()` succeeded first. Throws an `AssertionFailure` if anything upstream failed;
/// the caller wraps the call inside a `Given` step.
[[nodiscard]] bake::BakeOutputs produceValid() {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = bake::parseRecipe(validRecipe(), "fixture.toml", diagnostics);
    if (!recipe.has_value()) {
        throw speclab::core::AssertionFailure(
            "valid recipe did not parse", std::source_location::current());
    }
    const std::string text = validRecipe();
    std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(text.data()), text.size()};
    auto outputs = bake::run(*recipe, "fixture.toml", bytes, std::filesystem::current_path(),
                              diagnostics);
    if (!outputs.has_value()) {
        throw speclab::core::AssertionFailure(
            "run() rejected a valid recipe", std::source_location::current());
    }
    return *outputs;
}

/// A unique directory per scenario, so concurrent test cases cannot share artifacts. Removed in
/// the `Then` step of each scenario that uses it; if a scenario throws before cleanup, the next
/// invocation reuses a fresh name.
[[nodiscard]] std::filesystem::path freshTempDir(std::string_view slug) {
    auto dir = std::filesystem::temp_directory_path() /
               ("mdux-textbake-" + std::string{slug} + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

// ---------------------------------------------------------------------------
// parseRecipe: success and rejection paths
// ---------------------------------------------------------------------------

const mdux::spec::Register validRecipeParses{
    "A well-formed recipe parses with every field populated", "evidence-unit", [] {
        struct State {
            std::optional<bake::Recipe> recipe;
            std::vector<cli::Diagnostic> diagnostics;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-recipe-valid")
            .Given("a well-formed no-run recipe", [state] {
                state->recipe =
                    bake::parseRecipe(validRecipe(), "fixture.toml", state->diagnostics);
            })
            .When("it is parsed", [] {})
            .Then("every field is populated and no diagnostic is reported",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->recipe.has_value(), "parse succeeded");
                        checks.expect(state->diagnostics.empty(), "no diagnostics");
                        if (state->recipe.has_value()) {
                            checks.expect(state->recipe->id == "label-welcome", "id");
                            checks.expect(state->recipe->atlas == "roboto-ui", "atlas");
                            checks.expect(state->recipe->locale == "en-US", "locale");
                            checks.expect(state->recipe->sidecar == "runs.bin", "sidecar");
                        }
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register parseRecipeRejections{
    "parseRecipe emits the right stable code per failure mode", "evidence-unit", [] {
        struct Case {
            std::string_view what;
            std::string_view expectedCode;
            std::string recipe;
        };

        const std::vector<Case> cases{
            // TOML syntax: the parser throws before `parseRecipe` inspects the result. The
            // line number comes from `TomlError::line()`; the code is the recipe-unparsed one.
            {"a TOML syntax error", "TXT001", R"([package
id = "x")"},
            // A missing [package] table is the most fundamental shape failure.
            {"no [package] table", "TXT002", R"([atlas]
id = "roboto-ui"
)"},
            // An empty `id`/`atlas`/`locale` is a recipe-authoring mistake the baker catches at
            // the recipe layer (with a fix hint) rather than letting fall through to schema
            // validation as a generic TXT005.
            {"empty id", "TXT009",
             R"([package]
id = ""
atlas = "roboto-ui"
locale = "en-US"
)"},
            {"empty atlas", "TXT003",
             R"([package]
id = "label-welcome"
atlas = ""
locale = "en-US"
)"},
            {"empty locale", "TXT004",
             R"([package]
id = "label-welcome"
atlas = "roboto-ui"
locale = ""
)"},
            {"sidecar with a '/' separator", "TXT010",
             R"([package]
id = "label-welcome"
atlas = "roboto-ui"
locale = "en-US"
sidecar = "nested/runs.bin"
)"},
        };

        return speclab::Test("text-bake-recipe-rejections")
            .Given("a corpus of deliberately broken recipes", [] {})
            .When("each is parsed", [] {})
            .Then("each yields exactly the code an author would need to fix it",
                   [&cases] {
                        mdux::spec::Checks checks;
                        for (const Case& entry : cases) {
                            const std::string actual = parseCode(entry.recipe);
                            checks.expect(actual == entry.expectedCode,
                                           std::format("{}: got '{}', expected '{}'",
                                                        entry.what, actual, entry.expectedCode));
                        }
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// run(): S1 produces a no-run package, and it survives a round trip
// ---------------------------------------------------------------------------

const mdux::spec::Register runProducesNoRunPackage{
    "run() produces a no-run package whose sidecar is zero bytes", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-run-no-run-package")
            .Given("a valid recipe", [state] { state->outputs = produceValid(); })
            .When("run() is called", [] {})
            .Then("the produced package has no runs and an empty sidecar",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->outputs.sidecar.empty(),
                                       "sidecar is empty (S1 produces no runs)");
                        checks.expect(state->outputs.runCount == 0, "runCount is zero");
                        checks.expect(state->outputs.packageId == "label-welcome",
                                       "packageId carries through");
                        checks.expect(!state->outputs.packageJson.empty(),
                                       "packageJson is non-empty");
                        checks.expect(!state->outputs.reportJson.empty(),
                                       "reportJson is non-empty");
                        checks.expect(state->outputs.sidecarName == "runs.bin", "sidecar name");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register runPackageRoundTrips{
    "run()'s packageJson parses back into a validating TextPackage", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::optional<text::TextPackage> parsed;
            bool bytesSurvive{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-run-round-trip")
            .Given("a valid recipe's outputs", [state] {
                state->outputs = produceValid();
                auto parsed = text::TextPackage::parse(state->outputs.packageJson);
                // Re-writing the parsed package must reproduce the same bytes. This is the
                // property the evidence pipeline's byte comparison depends on; a baker whose
                // JSON did not round-trip would silently drift against any committed artifact.
                if (!parsed.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "TextPackage::parse() rejected run()'s output",
                        std::source_location::current());
                }
                state->parsed = *parsed;
                auto rewritten = state->parsed->write();
                if (!rewritten.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "re-writing the parsed package failed",
                        std::source_location::current());
                }
                state->bytesSurvive = (*rewritten == state->outputs.packageJson);
            })
            .When("the package is parsed and re-serialized", [] {})
            .Then("it validates, has the recipe's id/atlas/locale, and is byte-identical",
                   [state] {
                        const text::TextPackage& package = *state->parsed;
                        mdux::spec::Checks checks;
                        checks.expect(package.header.id == "label-welcome", "header id");
                        checks.expect(package.header.kind == "text", "header kind");
                        checks.expect(package.atlasId == "roboto-ui", "atlas id");
                        checks.expect(package.locale == "en-US", "locale");
                        checks.expect(package.sidecarByteLength == 0, "sidecar byte length");
                        checks.expect(package.runs.empty(), "the package has no runs");
                        checks.expect(state->bytesSurvive,
                                       "re-serializing reproduces the same bytes");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register runReportCarriesRecipeAndTool{
    "run()'s report records the recipe, tool, and resolved options", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::optional<mdux::evidence::BakeReport> parsed;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-run-report-contents")
            .Given("a valid recipe's outputs", [state] {
                state->outputs = produceValid();
                auto parsed = mdux::evidence::BakeReport::parse(state->outputs.reportJson);
                if (!parsed.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "BakeReport::parse() rejected run()'s report.json",
                        std::source_location::current());
                }
                state->parsed = *parsed;
            })
            .When("the report is parsed", [] {})
            .Then("it names mdux-textbake, carries the recipe path, and lists package.json and "
                  "the sidecar as outputs",
                   [state] {
                        const mdux::evidence::BakeReport& report = *state->parsed;
                        mdux::spec::Checks checks;
                        checks.expect(report.tool == "mdux-textbake", "tool");
                        checks.expect(!report.toolVersion.empty(), "toolVersion is non-empty");
                        checks.expect(report.recipe.path == "fixture.toml", "recipe path");
                        checks.expect(report.outputs.size() == 2, "two outputs");
                        if (report.outputs.size() == 2) {
                            checks.expect(report.outputs[0].path == "package.json",
                                           "outputs[0] is package.json");
                            checks.expect(report.outputs[1].path == "runs.bin",
                                           "outputs[1] is the sidecar");
                        }
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// write() and verify(): committed-artifact comparison
// ---------------------------------------------------------------------------

const mdux::spec::Register writeProducesFiles{
    "write() produces package.json, report.json and the sidecar in the output directory",
    "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::filesystem::path dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool ok{false};
            bool hasPackage{false};
            bool hasReport{false};
            bool hasSidecar{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-write-files")
            .Given("a valid recipe's outputs and a fresh directory", [state] {
                state->outputs = produceValid();
                state->dir = freshTempDir("write");
                state->ok = bake::write(state->outputs, state->dir, state->diagnostics);
                state->hasPackage = std::filesystem::exists(state->dir / "package.json");
                state->hasReport = std::filesystem::exists(state->dir / "report.json");
                state->hasSidecar = std::filesystem::exists(state->dir / state->outputs.sidecarName);
            })
            .When("write() is called and the directory is inspected", [] {})
            .Then("write succeeds with no diagnostics and all three files are present",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->ok, "write returned true");
                        checks.expect(state->diagnostics.empty(), "no diagnostics");
                        checks.expect(state->hasPackage, "package.json exists");
                        checks.expect(state->hasReport, "report.json exists");
                        checks.expect(state->hasSidecar, "sidecar exists");
                        // Cleanup: leave the temp tree clean.
                        std::error_code code;
                        std::filesystem::remove_all(state->dir, code);
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register verifyReportsMissingArtifact{
    "verify() reports TXT007 when a committed package is missing", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::filesystem::path dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool ok{true};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-verify-missing")
            .Given("a valid recipe's outputs and a directory with no committed files", [state] {
                state->outputs = produceValid();
                state->dir = freshTempDir("verify-missing");
            })
            .When("verify() is called against the empty directory", [state] {
                state->ok = bake::verify(state->outputs, state->dir / "package.json",
                                          state->dir / "report.json", state->diagnostics);
            })
            .Then("verify fails and the first diagnostic is the missing-artifact code",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(!state->ok, "verify returns false on missing artifacts");
                        checks.expect(!state->diagnostics.empty(),
                                       "at least one diagnostic is reported");
                        if (!state->diagnostics.empty()) {
                            checks.expect(state->diagnostics.front().code == "TXT007",
                                           "the first diagnostic is TXT007");
                        }
                        std::error_code code;
                        std::filesystem::remove_all(state->dir, code);
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register verifyReportsByteDifference{
    "verify() reports TXT008 when a committed package's bytes differ", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::filesystem::path dir;
            std::vector<cli::Diagnostic> writeDiagnostics;
            std::vector<cli::Diagnostic> verifyDiagnostics;
            bool ok{true};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-verify-differs")
            .Given("a valid recipe's outputs and a directory with corrupted committed bytes",
                   [state] {
state->outputs = produceValid();
                       state->dir = freshTempDir("verify-differs");
                       if (!bake::write(state->outputs, state->dir, state->writeDiagnostics)) {
                           throw speclab::core::AssertionFailure(
                               "write() failed for the diff setup",
                               std::source_location::current());
                       }
                       // Overwrite the committed package.json with a single byte so the
                       // first-differing-byte path is exercised: the file is shorter than the
                       // produced bytes, so the comparison fails by length at byte 1.
                       std::ofstream file{state->dir / "package.json",
                                           std::ios::binary | std::ios::trunc};
                       file.write("{", 1);
                    })
            .When("verify() is called against the corrupted committed bytes", [state] {
                state->ok = bake::verify(state->outputs, state->dir / "package.json",
                                          state->dir / "report.json", state->verifyDiagnostics);
            })
            .Then("verify fails and a TXT008 diagnostic is reported",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(!state->ok, "verify returns false on a diff");
                        bool foundDiffers = false;
                        for (const cli::Diagnostic& message : state->verifyDiagnostics) {
                            if (message.code == "TXT008") {
                                foundDiffers = true;
                                break;
                            }
                        }
                        checks.expect(foundDiffers, "a TXT008 diagnostic is reported");
                        std::error_code code;
                        std::filesystem::remove_all(state->dir, code);
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register verifyAcceptsIdenticalCommitted{
    "verify() succeeds when the committed bytes are byte-identical", "evidence-unit", [] {
        struct State {
            bake::BakeOutputs outputs;
            std::filesystem::path dir;
            std::vector<cli::Diagnostic> writeDiagnostics;
            std::vector<cli::Diagnostic> verifyDiagnostics;
            bool ok{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-verify-identical")
            .Given("a valid recipe's outputs and a directory holding an identical copy", [state] {
                state->outputs = produceValid();
                state->dir = freshTempDir("verify-identical");
                if (!bake::write(state->outputs, state->dir, state->writeDiagnostics)) {
                    throw speclab::core::AssertionFailure(
                        "write() failed for the identical-copy setup",
                        std::source_location::current());
                }
            })
            .When("verify() is called against the just-written copy", [state] {
                state->ok = bake::verify(state->outputs, state->dir / "package.json",
                                          state->dir / "report.json", state->verifyDiagnostics);
            })
            .Then("verify succeeds with no diagnostics",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->ok, "verify returns true on identical bytes");
                        checks.expect(state->verifyDiagnostics.empty(),
                                       "no diagnostics on a matching verify");
                        std::error_code code;
                        std::filesystem::remove_all(state->dir, code);
                        checks.raise();
                   })
            .Execute();
    }};