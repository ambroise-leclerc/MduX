/**
 * @file ScenarioBakeMain.cpp
 * @brief `mdux-scenariobake` entry point: the `.scenario` compiler as a baker (#319, ADR-020).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Split library-and-executable like every other baker, so the tests drive `run()` / `write()` /
 * `verify()` as calls. It speaks the shared `bake` / `verify` grammar, so `mdux_bake_artifact()`
 * registers a scenario the same way it registers a screen.
 */
import std;
import mdux.tools.cli;
import mdux.tools.scenario;

namespace {

namespace cli = mdux::tools::cli;
namespace sc  = mdux::tools::scenario;

[[nodiscard]] std::optional<sc::BakeOutputs> produce(const std::string& recipePath,
                                                     std::vector<cli::Diagnostic>& diagnostics) {
    const auto recipeBytes = sc::readFile(recipePath);
    if (!recipeBytes.has_value()) {
        diagnostics.push_back(cli::Diagnostic{.file     = recipePath,
                                              .code     = "SCN020",
                                              .severity = cli::Severity::Error,
                                              .message  = "the recipe file could not be opened",
                                              .fixHint  = "Paths resolve against the working directory, which for a build is the repository root."});
        return std::nullopt;
    }
    const std::string_view text{reinterpret_cast<const char*>(recipeBytes->data()), recipeBytes->size()};
    const auto             recipe = sc::parseRecipe(text, recipePath, diagnostics);
    if (!recipe.has_value()) {
        return std::nullopt;
    }
    return sc::run(*recipe, recipePath, *recipeBytes, std::filesystem::current_path(), diagnostics);
}

}  // namespace

int main(int argc, char** argv) {
    cli::Invocation invocation;
    try {
        invocation = cli::parse("mdux-scenariobake", argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;
    const std::string&           recipePath = invocation.mode == cli::Mode::Bake ? invocation.bake.recipe : invocation.verify.recipe;

    std::string summary;
    try {
        if (auto outputs = produce(recipePath, diagnostics); outputs.has_value()) {
            const bool ok = invocation.mode == cli::Mode::Bake
                                ? sc::write(*outputs, invocation.bake.outputDir, diagnostics)
                                : sc::verify(*outputs, invocation.verify.packagePath, invocation.verify.reportPath, diagnostics);
            if (ok) {
                summary = std::format("mdux-scenariobake: OK ({} {}: {} steps, {} captures)",
                                      invocation.mode == cli::Mode::Bake ? "compiled" : "verified", outputs->scenarioId,
                                      outputs->stepCount, outputs->captureCount);
            }
        }
    } catch (const std::exception& error) {
        diagnostics.push_back(cli::Diagnostic{.file     = recipePath,
                                              .code     = "SCN028",
                                              .severity = cli::Severity::Error,
                                              .message  = std::format("the compiler stopped on an internal error: {}", error.what()),
                                              .fixHint  = "This is a defect in mdux-scenariobake; please report it with the recipe attached."});
    }

    const std::string rendered = cli::render(diagnostics, invocation.format, "mdux-scenariobake");
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }
    return cli::exitStatus(diagnostics);
}
