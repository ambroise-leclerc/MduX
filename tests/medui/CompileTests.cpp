/**
 * @file CompileTests.cpp
 * @brief BDD scenarios for the `.medui` compiler driver (issue #198).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * The stages have their own suites; what is under test here is the driver's own contract - the order
 * they run in, the recipe being complete for the screen it names, and the three artifacts a compile
 * produces being exactly what the schema, the sidecar rule and the bake report require.
 *
 * The scenarios drive `run()`, `write()` and `verify()` as calls rather than spawning `mdux-meduic`,
 * which is why the tool is split library-and-executable: an assertion on an exit status could only
 * report that something was wrong, never what.
 */

import std;
import speclab;
import mdux.evidence.report;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.compile;
import mdux.tools.medui.package;
import mdux.tools.medui.parser;
import mdux.tools.medui.textbudget;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace ms  = mdux::medui;
namespace cli = mdux::tools::cli;

[[nodiscard]] std::filesystem::path repoRoot() {
    return std::filesystem::path{MDUX_REPO_ROOT};
}

[[nodiscard]] std::string fixture(std::string_view name) {
    std::ifstream in{repoRoot() / "tests" / "medui" / "fixtures" / name, std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("fixture {} could not be opened", name), std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] std::string firstCode(std::span<const cli::Diagnostic> diagnostics) {
    return diagnostics.empty() ? std::string{"<none>"} : diagnostics.front().code;
}

/// The fixture recipe, parsed. Every scenario that compiles starts here.
[[nodiscard]] md::Recipe textlessRecipe(std::vector<cli::Diagnostic>& diagnostics) {
    const std::optional<md::Recipe> recipe = md::parseRecipe(fixture("textless-screen.toml"), "tests/medui/fixtures/textless-screen.toml", diagnostics);
    if (!recipe.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture recipe did not parse: {}", firstCode(diagnostics)), std::source_location::current());
    }
    return *recipe;
}

/// A directory this scenario owns, removed when it is done with it.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view name) : path_{std::filesystem::temp_directory_path() / name} {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
        std::filesystem::create_directories(path_, code);
    }

    TemporaryDirectory(const TemporaryDirectory&)            = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

const mdux::spec::Register everyRecipeKnobIsRequired{
    "Every recipe knob is required, because a defaulted one would not appear in the report",
    "evidence-unit",
    [] {
        return speclab::Test("medui-compile-recipe")
            .Given("the fixture recipe, and copies with one knob removed", [] {})
            .When("each is parsed", [] {})
            .Then("the complete one resolves and each incomplete one is refused",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      const md::Recipe recipe = textlessRecipe(diagnostics);
                      checks.expect(diagnostics.empty(), "the complete recipe parses without diagnostics");
                      checks.expect(recipe.id == "textless-screen", std::format("the artifact slug, got '{}'", recipe.id));
                      checks.expect(recipe.surfaceWidth == 400 && recipe.surfaceHeight == 300, "the declared surface");
                      checks.expect(recipe.budget.maxVertices == 4096 && recipe.budget.maxCommands == 256, "the declared budget");
                      checks.expect(recipe.fontPackage.empty() && recipe.textPackages.empty(), "a text-free screen names no locale inputs");

                      // ADR-007's rule, checked rather than described: a knob with a default would
                      // not appear in report.json, so a later change to that default would leave
                      // every report looking unchanged.
                      const std::string complete = fixture("textless-screen.toml");
                      for (const std::string_view knob : {"id ", "source ", "surfaceWidth ", "maxVertices ", "maxIndices "}) {
                          std::string       broken = complete;
                          const std::size_t at     = broken.find(knob);
                          if (at == std::string::npos) {
                              checks.expect(false, std::format("the fixture declares {}", knob));
                              continue;
                          }
                          const std::size_t lineEnd = broken.find('\n', at);
                          broken.erase(at, lineEnd - at);

                          std::vector<cli::Diagnostic> refused;
                          const auto                   parsed = md::parseRecipe(broken, "recipe.toml", refused);
                          checks.expect(!parsed.has_value(), std::format("a recipe without {}is refused", knob));
                          checks.expect(firstCode(refused) == "MEDUI-E002", std::format("reported as MEDUI-E002, got '{}'", firstCode(refused)));
                      }

                      std::vector<cli::Diagnostic> unparsed;
                      const auto                   broken = md::parseRecipe("this is not TOML {{", "recipe.toml", unparsed);
                      checks.expect(!broken.has_value(), "a recipe that is not TOML is refused");
                      checks.expect(firstCode(unparsed) == "MEDUI-E001", std::format("reported as MEDUI-E001, got '{}'", firstCode(unparsed)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aCompileProducesThreeArtifacts{
    "A compile produces the three artifacts ADR-012 fixes",
    "evidence-unit",
    [] {
        return speclab::Test("medui-compile-artifacts")
            .Given("a screen that draws no text", [] {})
            .When("the compiler runs every stage over it", [] {})
            .Then("the package validates, the goldens pin the positioned node, and the report records both",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      const md::Recipe  recipe     = textlessRecipe(diagnostics);
                      const std::string recipeText = fixture("textless-screen.toml");
                      const auto        outputs    = md::run(recipe, "tests/medui/fixtures/textless-screen.toml", asBytes(recipeText), repoRoot(), diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, std::format("the fixture screen compiles, first diagnostic '{}'", firstCode(diagnostics)));
                          checks.raise();
                          return;
                      }
                      checks.expect(diagnostics.empty(), "a clean compile reports nothing");

                      // The package is read back through the reader rather than inspected as the
                      // value that produced it, so this exercises the round trip a device's build
                      // would take.
                      const md::PackageReadResult read = md::readPackage(outputs->packageJson, "package.json");
                      checks.expect(read.ok(), std::format("the emitted package reads back, first diagnostic '{}'", firstCode(read.diagnostics)));
                      if (read.ok()) {
                          const ms::ScreenPackage package = read.document.package();
                          checks.expect(package.id == "textless-screen", "the package carries the artifact slug, not the screen name");
                          checks.expect(package.nodes.size() == 4, std::format("four compiled nodes, got {}", package.nodes.size()));
                          checks.expect(package.find("topbar-background") != nullptr, "the Row's synthetic background is compiled");
                          checks.expect(package.validate().has_value(), "the compiled screen satisfies its schema");
                      }

                      // One golden, for the positioned SignalTrace. An empty array here would have
                      // meant the sidecar proved nothing.
                      checks.expect(outputs->goldenCount == 1, std::format("one golden reference, got {}", outputs->goldenCount));
                      checks.expect(outputs->goldensJson.contains("\"nodeId\": \"ecg\""), "the positioned node is the one pinned");

                      const auto report = mdux::evidence::BakeReport::parse(outputs->reportJson);
                      checks.expect(report.has_value(), "the bake report parses");
                      if (report.has_value()) {
                          checks.expect(report->tool == "mdux-meduic", std::format("the report names the tool, got '{}'", report->tool));
                          // Two outputs, not three: a file cannot carry its own digest, which is the
                          // same reason ADR-007 gives for there being no commit SHA in a report.
                          checks.expect(report->outputs.size() == 2, std::format("package.json and goldens.json are recorded, got {}", report->outputs.size()));
                          checks.expect(report->inputs.size() == 1, std::format("the .medui source is the only input, got {}", report->inputs.size()));
                          checks.expect(report->validate().has_value(), "the report satisfies its own schema");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aScreenThatDrawsTextNeedsItsLocales{
    "A screen that draws text is refused when the recipe declares no locales",
    "evidence-unit",
    [] {
        return speclab::Test("medui-compile-requires-locales")
            .Given("a recipe with no [text] table, naming a screen full of text keys", [] {})
            .When("the compiler runs", [] {})
            .Then("it refuses rather than compiling a screen whose boxes nobody measured",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      // The hazard this rule closes: skipping the budget stage is safe only for a
                      // screen with nothing to measure. A screen with text and no approved locale
                      // would otherwise be certified against a set nobody approved - and the
                      // omitted translation is exactly the one that overflows.
                      md::Recipe recipe    = textlessRecipe(diagnostics);
                      recipe.source        = "tests/medui/fixtures/accepted-every-component.medui";
                      recipe.surfaceWidth  = 800;
                      recipe.surfaceHeight = 700;

                      const std::string recipeText = fixture("textless-screen.toml");
                      const auto        outputs    = md::run(recipe, "recipe.toml", asBytes(recipeText), repoRoot(), diagnostics);
                      checks.expect(!outputs.has_value(), "a text-drawing screen with no locales is refused");
                      checks.expect(firstCode(diagnostics) == "MEDUI-E002", std::format("reported as MEDUI-E002, got '{}'", firstCode(diagnostics)));

                      // And the predicate that decides it, asked directly: the driver must not be
                      // the second place that knows what counts as text.
                      md::ParseResult drawsText = md::parse(fixture("accepted-every-component.medui"), "every.medui");
                      md::ParseResult drawsNone = md::parse(fixture("accepted-textless.medui"), "textless.medui");
                      checks.expect(drawsText.screen.has_value() && md::needsTextBudget(*drawsText.screen), "a screen with keys needs the budget stage");
                      checks.expect(drawsNone.screen.has_value() && !md::needsTextBudget(*drawsNone.screen), "a screen without them does not");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRejectedStageStopsTheCompile{
    "A stage that rejects the screen stops the compile",
    "evidence-unit",
    [] {
        return speclab::Test("medui-compile-stops-at-the-cause")
            .Given("a recipe whose declared surface is not the one the screen was drawn for", [] {})
            .When("the compiler runs", [] {})
            .Then("it reports the disagreement and produces nothing",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      md::Recipe recipe   = textlessRecipe(diagnostics);
                      recipe.surfaceWidth = 640;

                      const std::string recipeText = fixture("textless-screen.toml");
                      const auto        outputs    = md::run(recipe, "recipe.toml", asBytes(recipeText), repoRoot(), diagnostics);
                      checks.expect(!outputs.has_value(), "a screen drawn for another panel is refused");
                      checks.expect(!diagnostics.empty(), "and the refusal is reported");
                      // Diagnostics are not accumulated across stages: a later stage reading a screen
                      // an earlier one rejected reports consequences rather than causes.
                      checks.expect(diagnostics.size() == 1, std::format("one diagnostic, at the cause, got {}", diagnostics.size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writingAndVerifyingAgree{
    "What a compile writes is what verifying it accepts",
    "evidence-unit",
    [] {
        return speclab::Test("medui-compile-write-verify")
            .Given("a compiled screen", [] {})
            .When("it is written, verified, and verified again after an edit", [] {})
            .Then("all three files appear, the untouched ones verify, and the edited one does not",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;
                      TemporaryDirectory           scratch{"mdux-meduic-write"};

                      const md::Recipe  recipe     = textlessRecipe(diagnostics);
                      const std::string recipeText = fixture("textless-screen.toml");
                      const auto        outputs    = md::run(recipe, "recipe.toml", asBytes(recipeText), repoRoot(), diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, "the fixture screen compiles");
                          checks.raise();
                          return;
                      }

                      checks.expect(md::write(*outputs, scratch.path(), diagnostics), "writing succeeds");
                      const std::filesystem::path package = scratch.path() / "package.json";
                      const std::filesystem::path goldens = scratch.path() / "goldens.json";
                      const std::filesystem::path report  = scratch.path() / "report.json";
                      // All three unconditionally: ADR-012 declares each a build output, so a
                      // compiler that skipped one would break the build rather than produce less.
                      checks.expect(std::filesystem::exists(package) && std::filesystem::exists(goldens) && std::filesystem::exists(report),
                                    "all three artifacts are written");
                      checks.expect(contentsOf(goldens) == outputs->goldensJson, "the sidecar holds what the compile produced");

                      std::vector<cli::Diagnostic> verified;
                      checks.expect(md::verify(*outputs, package, goldens, report, verified), "the freshly written artifacts verify");
                      checks.expect(verified.empty(), "and verifying reports nothing");

                      std::ofstream tamper{goldens, std::ios::binary | std::ios::trunc};
                      tamper << "[]\n";
                      tamper.close();

                      std::vector<cli::Diagnostic> mismatched;
                      checks.expect(!md::verify(*outputs, package, goldens, report, mismatched), "a hand-edited artifact does not verify");
                      checks.expect(!mismatched.empty(), "and the mismatch is reported");
                      checks.raise();
                  })
            .Execute();
    }};
