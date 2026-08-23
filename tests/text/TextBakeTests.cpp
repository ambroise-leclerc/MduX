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
import mdux.font.schema;
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

// ---------------------------------------------------------------------------
// [strings]: parsing, and the pen walk that turns one into positioned records (#235)
// ---------------------------------------------------------------------------

namespace {

/// A three-glyph font package written to disk, so the baker reads a real committed-shape file
/// rather than a value a test handed it directly. Built through `mdux::font::FontPackage` and
/// `write()` rather than as raw JSON: `write()` validates, so a fixture that stops being a legal
/// package fails here instead of teaching the baker to accept one that is not.
///
/// The numbers are chosen so the pen arithmetic is checkable by hand. `unitsPerEm` is 1000 against
/// a `pixelSize` of 10, so one pixel is exactly 100 font units and the expected x of every record
/// below can be read off the advances without a calculator.
struct FixtureFont {
    static constexpr std::uint16_t unitsPerEm = 1000;
    static constexpr std::uint32_t pixelSize  = 10;
    static constexpr std::int16_t  kernAB     = -50;

    [[nodiscard]] static mdux::font::FontPackage package() {
        mdux::font::FontPackage font;
        font.id         = "fixture-ui";
        font.unitsPerEm = unitsPerEm;
        font.pixelSize  = pixelSize;
        font.locales    = {"en-US"};

        const std::vector<std::byte> sheet(64, std::byte{0});
        const auto                   hex = mdux::evidence::toHex(mdux::evidence::sha256(sheet));
        font.atlas.path             = "atlas.bin";
        font.atlas.width            = 8;
        font.atlas.height           = 8;
        font.atlas.byteLength       = sheet.size();
        font.atlas.sha256           = std::string{hex.data(), hex.size()};
        font.atlas.occupancyPercent = 75;

        // Sorted by code point, which is what `find()`'s binary search requires - and the order
        // that makes a record's packageIndex the glyph's position in this list.
        font.glyphs = {
            {.codePoint = U' ', .glyphIndex = 3, .advanceWidth = 250, .leftSideBearing = 0,
             .x = 0, .y = 0, .width = 0, .height = 0, .bitmapOriginX = 0, .bitmapOriginY = 0},
            {.codePoint = U'A', .glyphIndex = 4, .advanceWidth = 700, .leftSideBearing = 0,
             .x = 0, .y = 0, .width = 4, .height = 6, .bitmapOriginX = 0, .bitmapOriginY = 6},
            {.codePoint = U'B', .glyphIndex = 5, .advanceWidth = 650, .leftSideBearing = 0,
             .x = 4, .y = 0, .width = 4, .height = 6, .bitmapOriginX = 0, .bitmapOriginY = 6},
        };
        font.kerning           = {{.left = U'A', .right = U'B', .adjustment = kernAB}};
        font.restrictedCharset = {{.first = U' ', .last = U' '}, {.first = U'A', .last = U'B'}};
        return font;
    }

    /// Writes the package into `dir` and returns its bare filename.
    [[nodiscard]] static std::string writeInto(const std::filesystem::path& dir) {
        auto text = package().write();
        if (!text.has_value()) {
            throw speclab::core::AssertionFailure("the fixture font package is not valid",
                                                  std::source_location::current());
        }
        std::ofstream out{dir / "font.json", std::ios::binary | std::ios::trunc};
        out << *text;
        out.close();
        return "font.json";
    }
};

/// A text recipe naming `fontPath` under the fixture font, with whatever `[strings]` body is given.
[[nodiscard]] std::string textRecipeText(std::string_view fontPath, std::string_view stringsBody,
                                         std::string_view atlas = "fixture-ui", std::string_view locale = "en-US") {
    return std::format(R"([package]
id = "fixture-text"
atlas = "{}"
locale = "{}"
font = "{}"
sidecar = "runs.bin"

[strings]
{}
)",
                       atlas, locale, fontPath, stringsBody);
}

/// One byte of a v1 record, as hex, so a mismatch reads as bytes rather than as a length.
[[nodiscard]] std::string hexBytes(std::span<const std::byte> bytes) {
    std::string out;
    for (const std::byte b : bytes) {
        out += std::format("{:02x} ", std::to_integer<std::uint8_t>(b));
    }
    return out;
}

}  // namespace

const mdux::spec::Register stringsPositionIntoRecords{
    "A [strings] table bakes into v1 records at the advances the font package declares", "evidence-unit", [] {
        struct State {
            std::filesystem::path        dir;
            std::optional<bake::Recipe>  recipe;
            std::optional<bake::BakeOutputs> outputs;
            std::vector<cli::Diagnostic> diagnostics;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-bake-strings-positioned")
            .Given("a font package on disk and a recipe naming two strings", [state] {
                state->dir            = freshTempDir("strings");
                const auto fontName   = FixtureFont::writeInto(state->dir);
                const std::string src = textRecipeText(fontName, R"(keys   = ["STR-AB", "STR-A"]
values = ["AB A",  "A"])");
                state->recipe = bake::parseRecipe(src, "fixture.toml", state->diagnostics);
                if (!state->recipe.has_value()) {
                    throw speclab::core::AssertionFailure("the recipe did not parse",
                                                          std::source_location::current());
                }
                const auto bytes = std::as_bytes(std::span{src.data(), src.size()});
                state->outputs   = bake::run(*state->recipe, "fixture.toml", bytes, state->dir, state->diagnostics);
                if (!state->outputs.has_value()) {
                    throw speclab::core::AssertionFailure(
                        std::format("the bake failed: {}", state->diagnostics.empty()
                                                               ? std::string{"(no diagnostic)"}
                                                               : state->diagnostics.back().message),
                        std::source_location::current());
                }
            })
            .When("the package and its sidecar are read back", [] {})
            .Then("each run holds one record per code point, at the expected pen positions",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        out = *state->outputs;

                      // The expected bytes, derived by hand from the fixture's own advances rather
                      // than from the implementation's formula - a test that recomputed the formula
                      // would agree with a wrong one. One pixel is 100 font units here.
                      //
                      //   'A' at pen 0                            -> x = 0
                      //   pen += 700, kern(A,B) = -50             -> pen 650
                      //   'B' at pen 650                          -> x = 7  (650/100, rounded)
                      //   pen += 650                              -> pen 1300
                      //   ' ' at pen 1300                         -> x = 13
                      //   pen += 250                              -> pen 1550
                      //   'A' at pen 1550                         -> x = 16 (15.5, rounded half up)
                      //
                      // packageIndex is the glyph's position in the package's sorted glyph list:
                      // space 0, 'A' 1, 'B' 2 - not the font's own glyphIndex (3, 4, 5).
                      const std::vector<std::uint8_t> expected{
                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // 'A' at x=0
                          0x02, 0x00, 0x07, 0x00, 0x00, 0x00,  // 'B' at x=7
                          0x00, 0x00, 0x0D, 0x00, 0x00, 0x00,  // ' ' at x=13
                          0x01, 0x00, 0x10, 0x00, 0x00, 0x00,  // 'A' at x=16
                      };

                      checks.expect(out.runCount == 2, std::format("two runs, got {}", out.runCount));
                      checks.expect(out.sidecar.size() == expected.size() + text::recordSize,
                                    std::format("{} sidecar bytes, got {}", expected.size() + text::recordSize,
                                                out.sidecar.size()));

                      const auto parsed = text::TextPackage::parse(out.packageJson);
                      checks.expect(parsed.has_value(), "the produced package.json parses");
                      if (parsed.has_value() && parsed->runs.size() == 2) {
                          checks.expect(parsed->runs[0].id == "STR-AB", "the first run keeps its key");
                          checks.expect(parsed->runs[1].id == "STR-A", "the second run keeps its key");
                          checks.expect(parsed->runs[0].byteOffset == 0, "the first run starts at zero");
                          checks.expect(parsed->runs[0].byteLength == expected.size(),
                                        std::format("the first run is {} bytes", expected.size()));
                          // Contiguous: the second run starts where the first ended, so every byte
                          // of the sidecar belongs to exactly one run.
                          checks.expect(parsed->runs[1].byteOffset == expected.size(),
                                        "the second run starts where the first ended");
                      }

                      if (out.sidecar.size() >= expected.size()) {
                          const std::span<const std::byte> first{out.sidecar.data(), expected.size()};
                          std::vector<std::byte>           want;
                          for (const std::uint8_t b : expected) {
                              want.push_back(static_cast<std::byte>(b));
                          }
                          checks.expect(std::ranges::equal(first, want),
                                        std::format("records are [{}], got [{}]", hexBytes(want), hexBytes(first)));
                      }

                      // The bake's one input is the font package, so re-baking the font invalidates
                      // this artifact rather than leaving it describing an atlas that moved.
                      const auto report = mdux::evidence::BakeReport::parse(out.reportJson);
                      checks.expect(report.has_value(), "the produced report.json parses");
                      if (report.has_value()) {
                          checks.expect(report->inputs.size() == 1,
                                        std::format("one input, got {}", report->inputs.size()));
                          if (report->inputs.size() == 1) {
                              checks.expect(report->inputs.front().path == "font.json",
                                            "the input is the font package");
                          }
                      }

                      std::error_code ignored;
                      std::filesystem::remove_all(state->dir, ignored);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register stringsRejections{
    "A malformed or unbakeable [strings] table reports its own stable code", "evidence-unit", [] {
        return speclab::Test("text-bake-strings-rejections")
            .Given("a corpus of broken [strings] tables", [] {})
            .When("each is parsed and baked against the fixture font", [] {})
            .Then("each reports its own code",
                  [] {
                      struct Case {
                          std::string_view what;
                          std::string_view expectedCode;
                          std::string      recipe;
                      };

                      mdux::spec::Checks checks;
                      const auto         dir      = freshTempDir("strings-bad");
                      const auto         fontName = FixtureFont::writeInto(dir);

                      // Built here rather than as a namespace-scope corpus because every recipe
                      // has to name the font package this scenario just wrote.
                      const std::vector<Case> cases{
                          {"parallel arrays of different lengths", "TXT020",
                           textRecipeText(fontName, R"(keys   = ["A", "B"]
values = ["only one"])")},
                          {"a [strings] table with no keys", "TXT020",
                           textRecipeText(fontName, R"(keys   = []
values = [])")},
                          {"an empty key", "TXT020",
                           textRecipeText(fontName, R"(keys   = [""]
values = ["A"])")},
                          // An empty translation bakes a zero-length run: it validates, draws
                          // nothing, and ships as a blank label. Refused at the recipe rather than
                          // discovered on a device.
                          {"an empty value", "TXT020",
                           textRecipeText(fontName, R"(keys   = ["STR-A"]
values = [""])")},
                          {"a key named twice", "TXT020",
                           textRecipeText(fontName, R"(keys   = ["STR-A", "STR-A"]
values = ["A",     "B"])")},
                          // The fixture bakes the space, 'A' and 'B'. ADR-010 leaves the runtime no
                          // fallback, so a character nobody baked is a bake failure rather than a
                          // substitution.
                          {"a character the font package cannot draw", "TXT024",
                           textRecipeText(fontName, R"(keys   = ["STR-C"]
values = ["C"])")},
                          {"a locale the font package does not approve", "TXT023",
                           textRecipeText(fontName, R"(keys   = ["STR-A"]
values = ["A"])",
                                          "fixture-ui", "de-DE")},
                          {"an atlas id that is not the one in the font package", "TXT022",
                           textRecipeText(fontName, R"(keys   = ["STR-A"]
values = ["A"])",
                                          "some-other-font")},
                          // std::filesystem's operator/ replaces the left operand when the right is
                          // absolute, so an unchecked join would read this and ignore the root.
                          {"a font package path that escapes the root", "TXT021",
                           textRecipeText("../../etc/passwd", R"(keys   = ["STR-A"]
values = ["A"])")},
                          {"a font package that is not there", "TXT021",
                           textRecipeText("absent.json", R"(keys   = ["STR-A"]
values = ["A"])")},
                      };

                      for (const Case& entry : cases) {
                          std::vector<cli::Diagnostic> diagnostics;
                          auto recipe = bake::parseRecipe(entry.recipe, "fixture.toml", diagnostics);
                          if (recipe.has_value()) {
                              const auto bytes = std::as_bytes(std::span{entry.recipe.data(), entry.recipe.size()});
                              auto       out   = bake::run(*recipe, "fixture.toml", bytes, dir, diagnostics);
                              checks.expect(!out.has_value(),
                                            std::format("{}: the bake succeeded unexpectedly", entry.what));
                          }
                          const bool sawCode = std::ranges::any_of(
                              diagnostics, [&entry](const cli::Diagnostic& d) { return d.code == entry.expectedCode; });
                          checks.expect(sawCode,
                                        std::format("{}: expected {}, got [{}]", entry.what, entry.expectedCode,
                                                    diagnostics.empty() ? std::string{"none"} : diagnostics.front().code));
                      }

                      std::error_code ignored;
                      std::filesystem::remove_all(dir, ignored);
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register halfARecipeIsRejected{
    "A recipe with strings and no font, or a font and no strings, is refused", "evidence-unit", [] {
        return speclab::Test("text-bake-strings-half-recipe")
            .Given("two half-written recipes", [] {})
            .When("each is parsed", [] {})
            .Then("both report TXT020, and the pre-#235 no-strings recipe still parses",
                  [] {
                      mdux::spec::Checks checks;
                      const std::string stringsOnly = R"([package]
id = "fixture-text"
atlas = "fixture-ui"
locale = "en-US"

[strings]
keys   = ["STR-A"]
values = ["A"]
)";
                      const std::string fontOnly = R"([package]
id = "fixture-text"
atlas = "fixture-ui"
locale = "en-US"
font = "font.json"
)";
                      checks.expect(parseCode(stringsOnly) == "TXT020",
                                    std::format("strings without a font: got {}", parseCode(stringsOnly)));
                      checks.expect(parseCode(fontOnly) == "TXT020",
                                    std::format("a font without strings: got {}", parseCode(fontOnly)));
                      // The shape every text recipe had before #235: neither table. Still valid,
                      // because a locale with nothing to say yet is a legitimate package.
                      checks.expect(parseCode(validRecipe()) == "ok",
                                    "a recipe with neither still parses");
                      checks.raise();
                  })
            .Execute();
    }};
