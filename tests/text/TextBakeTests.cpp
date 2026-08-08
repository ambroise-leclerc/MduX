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
            {"an empty sidecar", "TXT010",
             R"([package]
id = "label-welcome"
atlas = "roboto-ui"
locale = "en-US"
sidecar = ""
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
            std::size_t flipIndex{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-verify-differs")
            .Given("a valid recipe's outputs and a directory with a same-length corrupted "
                   "committed package.json",
                   [state] {
                       state->outputs = produceValid();
                       state->dir = freshTempDir("verify-differs");
                       if (!bake::write(state->outputs, state->dir, state->writeDiagnostics)) {
                           throw speclab::core::AssertionFailure(
                               "write() failed for the diff setup",
                               std::source_location::current());
                       }
                       // Same-length corruption: read the committed package.json back, flip one
                       // byte near the middle, and rewrite the file. The byte is flipped to a
                       // value that is not what `run()` produced, so the first-differing-byte
                       // loop in `compareArtifact()` actually fires - the length-differs branch
                       // that a truncated file exercises is a separate path. This is the index
                       // arithmetic the review flagged as "wrong once and then wrong quietly", so
                       // the assertion below also pins the reported offset rather than merely the
                       // code.
                       const std::filesystem::path packagePath = state->dir / "package.json";
                       std::ifstream input{packagePath, std::ios::binary | std::ios::ate};
                       if (!input) {
                           throw speclab::core::AssertionFailure(
                               "could not open package.json for the diff setup",
                               std::source_location::current());
                       }
                       const auto size = input.tellg();
                       input.seekg(0);
                       std::string text(static_cast<std::size_t>(size), '\0');
                       input.read(text.data(), size);
                       input.close();
                       if (text.size() < 4) {
                           throw speclab::core::AssertionFailure(
                               "package.json is too short to flip a mid-file byte",
                               std::source_location::current());
                       }
                       // Flip byte at size/2. Choose a replacement value that is not the original,
                       // so the diff is real. `text[size / 2]` is well-defined because size >= 4.
                       const std::size_t flipIndex = text.size() / 2;
                       const char original = text[flipIndex];
                       char replacement = (original == 'X') ? 'Y' : 'X';
                       text[flipIndex] = replacement;
                       state->flipIndex = flipIndex;
                       std::ofstream output{packagePath, std::ios::binary | std::ios::trunc};
                       output.write(text.data(), static_cast<std::streamsize>(text.size()));
                   })
            .When("verify() is called against the corrupted committed bytes", [state] {
                state->ok = bake::verify(state->outputs, state->dir / "package.json",
                                          state->dir / "report.json", state->verifyDiagnostics);
            })
            .Then("verify fails, a TXT008 diagnostic is reported, and it names the flipped byte offset",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(!state->ok, "verify returns false on a diff");
                        bool foundDiffers = false;
                        bool offsetPinned = false;
                        for (const cli::Diagnostic& message : state->verifyDiagnostics) {
                            if (message.code == "TXT008" &&
                                message.file.find("package.json") != std::string::npos) {
                                foundDiffers = true;
                                // The diagnostic's message carries the byte offset, e.g.
                                // "package.json differs at byte 42: ...". Pin it against the
                                // index we flipped so the offset reporting is actually tested,
                                // not just the code.
                                const std::string needle =
                                    "at byte " + std::to_string(state->flipIndex) + ":";
                                offsetPinned = message.message.find(needle) != std::string::npos;
                                break;
                            }
                        }
                        checks.expect(foundDiffers, "a TXT008 diagnostic is reported for package.json");
                        checks.expect(offsetPinned,
                                       "the reported byte offset matches the flipped index");
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

// ---------------------------------------------------------------------------
// The font pipeline (#160, S4).
// ---------------------------------------------------------------------------

namespace {

/// The repository root, so a recipe's `source` path resolves the way the CLI resolves it.
/// `MDUX_REPO_ROOT` is set by tests/CMakeLists.txt, the same way shader_spec gets it.
[[nodiscard]] std::filesystem::path repositoryRoot() {
    return std::filesystem::path{MDUX_REPO_ROOT};
}

/// A minimal font recipe. `source` points at the committed DejaVu asset, which is the same file
/// the real recipe uses - a fixture font would prove the pipeline works on a fixture.
[[nodiscard]] std::string fontRecipeText(std::string_view charsetBody, std::int64_t pixelSize = 16,
                                         std::string_view localeIds = R"("en-US")",
                                         std::string_view sourcePath = "recipes/font/dejavu-ui/DejaVuSans.ttf") {
    return std::format(R"([package]
id = "fixture-font"
source = "{}"
sidecar = "atlas.bin"
pixelSize = {}

[locales]
ids = [{}]

[charset]
{}
)",
                       sourcePath, pixelSize, localeIds, charsetBody);
}

/// The printable-ASCII charset body, for cases whose defect is elsewhere in the recipe.
constexpr std::string_view asciiCharset = R"(names           = ["ascii"]
firstCodePoints = [32]
lastCodePoints  = [126])";

}  // namespace

const mdux::spec::Register fontRecipeBakesAnAtlas{
    "A font recipe bakes an atlas whose glyphs carry slots, advances and blanks",
    "evidence-unit",
    [] {
        // The end-to-end assertion for S4: parse a TrueType file, resolve a charset through cmap,
        // rasterise each outline, pack the results, and emit a package that describes all of it.
        // The committed artifact's byte-identity is checked separately by `evidence.font.dejavu-ui`;
        // what this scenario adds is the *shape* of the result, which a digest cannot describe.
        struct State {
            std::string                              text;
            std::optional<bake::Recipe>              recipe;
            std::optional<bake::BakeOutputs>         outputs;
            std::vector<mdux::tools::cli::Diagnostic> diagnostics;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-font-atlas")
            .Given("a recipe naming the printable ASCII range",
                   [state] {
                       state->text = fontRecipeText(R"(names           = ["ascii"]
firstCodePoints = [32]
lastCodePoints  = [126])");
                   })
            .When("it is parsed and baked",
                  [state] {
                      state->recipe = bake::parseRecipe(state->text, "fixture.toml", state->diagnostics);
                      if (!state->recipe.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("recipe did not parse: {}", state->diagnostics.empty()
                                                                          ? std::string{"(no diagnostic)"}
                                                                          : state->diagnostics.front().message),
                              std::source_location::current());
                      }
                      const auto bytes = std::as_bytes(std::span{state->text.data(), state->text.size()});
                      state->outputs   = bake::run(*state->recipe, "fixture.toml", bytes, repositoryRoot(), state->diagnostics);
                      if (!state->outputs.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("bake failed: {}", state->diagnostics.empty() ? std::string{"(no diagnostic)"}
                                                                                        : state->diagnostics.back().message),
                              std::source_location::current());
                      }
                  })
            .Then("95 glyphs land on a power-of-two sheet, and the space is blank but advances",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        out = *state->outputs;
                      checks.expect(out.glyphCount == 95, std::format("95 glyphs, got {}", out.glyphCount));
                      const auto isPow2 = [](std::uint32_t v) { return v != 0 && (v & (v - 1)) == 0; };
                      checks.expect(isPow2(out.atlasWidth) && isPow2(out.atlasHeight),
                                    std::format("power-of-two sheet, got {}x{}", out.atlasWidth, out.atlasHeight));
                      checks.expect(out.sidecar.size() == static_cast<std::size_t>(out.atlasWidth) * out.atlasHeight,
                                    "the sidecar is exactly one byte per texel");

                      auto package = mdux::evidence::json::parse(out.packageJson);
                      checks.expect(package.has_value(), "the package is valid canonical JSON");
                      if (!package.has_value()) {
                          checks.raise();
                          return;
                      }
                      const auto* glyphs = package->find("glyphs");
                      checks.expect(glyphs != nullptr && glyphs->kind() == mdux::evidence::json::Value::Kind::Array,
                                    "it carries a glyph array");
                      if (glyphs == nullptr || glyphs->kind() != mdux::evidence::json::Value::Kind::Array) {
                          checks.raise();
                          return;
                      }
                      // The space is the case the pipeline nearly got wrong: parseGlyph returns a
                      // blank outline and rasterise() rightly refuses an empty request, so a baker
                      // that forwarded it would fail on the most common character in any string.
                      bool sawSpace = false;
                      bool sawSolid = false;
                      for (const auto& entry : glyphs->elements()) {
                          const auto* point = entry.find("codePoint");
                          const auto* width = entry.find("width");
                          const auto* adv   = entry.find("advanceWidth");
                          if (point == nullptr || width == nullptr || adv == nullptr) {
                              continue;
                          }
                          const auto pointValue = point->asInt();
                          const auto widthValue = width->asInt();
                          const auto advValue   = adv->asInt();
                          if (!pointValue.has_value() || !widthValue.has_value() || !advValue.has_value()) {
                              continue;
                          }
                          if (*pointValue == 32) {
                              sawSpace = true;
                              checks.expect(*widthValue == 0, "the space has no coverage");
                              checks.expect(*advValue > 0, "the space still advances the pen");
                          }
                          if (*widthValue > 0) {
                              sawSolid = true;
                          }
                      }
                      checks.expect(sawSpace, "the space is present in the package, not omitted");
                      checks.expect(sawSolid, "at least one glyph carries coverage");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fontRecipeRejections{
    "A font recipe's failures are reported with their own TXT codes",
    "evidence-unit",
    [] {
        // Each refusal keeps a distinct code rather than collapsing into "bake failed", because an
        // author needs to know whether the charset, the font or the size is the problem.
        struct Case {
            std::string_view what;
            std::string_view code;
            std::string      recipe;
        };

        const std::vector<Case> cases{
            {"charset arrays of different lengths", "TXT011",
             fontRecipeText(R"(names           = ["a", "b"]
firstCodePoints = [32]
lastCodePoints  = [126])")},
            {"a code point the font has no glyph for", "TXT014",
             // U+4E2D is CJK; DejaVu Sans does not cover it. Fatal rather than skipped: silently
             // omitting it would produce a package whose charset is not what the recipe asked for.
             fontRecipeText(R"(names           = ["cjk"]
firstCodePoints = [20013]
lastCodePoints  = [20013])")},
            {"a pixelSize that wraps to a valid value when narrowed", "TXT018",
             // 4294967312 is 2^32 + 16. Cast to uint32 before checking, it becomes 16 and bakes
             // happily at a size nobody wrote - an artifact that looks deliberate. Validating the
             // int64 the parser actually produced is what refuses it.
             fontRecipeText(R"(names           = ["ascii"]
firstCodePoints = [32]
lastCodePoints  = [126])", 4294967312LL)},
            {"a negative pixelSize", "TXT018",
             fontRecipeText(R"(names           = ["ascii"]
firstCodePoints = [32]
lastCodePoints  = [126])", -16)},
            {"two charset ranges that overlap", "TXT011",
             // A code point covered twice is rasterised twice, takes two atlas slots and appears
             // twice in the glyph list, so a consumer indexing by code point gets an ambiguous
             // package. Same reasoning as a missing glyph being fatal: the package must describe
             // the charset that was asked for.
             fontRecipeText(R"(names           = ["upper", "hex"]
firstCodePoints = [65, 65]
lastCodePoints  = [90, 70])")},
            {"a charset range covering the UTF-16 surrogates", "TXT011",
             // U+D800..U+DFFF are surrogate code points, not scalar values - they exist only to
             // encode astral characters in pairs and can never be a character themselves.
             fontRecipeText(R"(names           = ["surrogates"]
firstCodePoints = [55296]
lastCodePoints  = [56320])")},
            {"an empty locale id", "TXT004",
             fontRecipeText(asciiCharset, 16, R"("")")},
            {"a duplicated locale id", "TXT004",
             fontRecipeText(asciiCharset, 16, R"("en-US", "en-US")")},
            {"a font path escaping the repository root", "TXT012",
             // std::filesystem's operator/ replaces the left operand when the right is absolute,
             // so an unchecked join would have read this path and ignored the root entirely.
             fontRecipeText(asciiCharset, 16, R"("en-US")", "/etc/shadow")},
            {"a font path climbing out with ..", "TXT012",
             fontRecipeText(asciiCharset, 16, R"("en-US")", "../../etc/shadow")},
            {"a descending code point range", "TXT011",
             fontRecipeText(R"(names           = ["backwards"]
firstCodePoints = [126]
lastCodePoints  = [32])")},
        };

        return speclab::Test("text-bake-font-rejections")
            .Given("a corpus of broken font recipes", [] {})
            .When("each is parsed and baked", [] {})
            .Then("each reports its own code",
                  [&cases] {
                      mdux::spec::Checks checks;
                      for (const Case& entry : cases) {
                          std::vector<mdux::tools::cli::Diagnostic> diagnostics;
                          auto recipe = bake::parseRecipe(entry.recipe, "fixture.toml", diagnostics);
                          if (recipe.has_value()) {
                              const auto bytes = std::as_bytes(std::span{entry.recipe.data(), entry.recipe.size()});
                              auto       out   = bake::run(*recipe, "fixture.toml", bytes, repositoryRoot(), diagnostics);
                              checks.expect(!out.has_value(), std::format("{}: the bake succeeded unexpectedly", entry.what));
                          }
                          const bool sawCode = std::ranges::any_of(
                              diagnostics, [&entry](const mdux::tools::cli::Diagnostic& d) { return d.code == entry.code; });
                          checks.expect(sawCode, std::format("{}: expected {}, got [{}]", entry.what, entry.code,
                                                             diagnostics.empty() ? std::string{"none"} : diagnostics.front().code));
                      }
                      checks.raise();
                  })
            .Execute();
    }};
