/**
 * @file MlBakeMain.cpp
 * @brief `mdux-mlbake` entry point.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Everything interesting is in mdux.tools.ml.mlbake; this file is the boundary where a command
 * line becomes a call and a diagnostic list becomes an exit status. Deliberately the same shape as
 * ShaderBakeMain.cpp - two bakers that differ only in their asset kind should differ only in their
 * asset kind.
 *
 * Paths resolve against the current working directory, which `mdux_bake_artifact()` sets to
 * `CMAKE_SOURCE_DIR`, so every path recorded in `report.json` is repository-relative rather than a
 * description of the machine that ran the bake.
 */
import std;
import mdux.tools.cli;
import mdux.tools.ml.mlbake;

namespace {

namespace cli = mdux::tools::cli;
namespace bake = mdux::tools::ml;

[[nodiscard]] std::optional<bake::BakeOutputs> produce(const std::string& recipePath,
                                                       std::vector<cli::Diagnostic>& diagnostics) {
    auto recipeBytes = bake::readFile(recipePath);
    if (!recipeBytes.has_value()) {
        diagnostics.push_back(
            cli::Diagnostic{.file = recipePath,
                            .code = "mdux.ml.bake.recipeUnreadable",
                            .severity = cli::Severity::Error,
                            .message = "cannot read recipe",
                            .fixHint = "Paths are resolved against the current directory, which "
                                       "for a build is the repository root."});
        return std::nullopt;
    }

    const std::string_view text{reinterpret_cast<const char*>(recipeBytes->data()),
                                recipeBytes->size()};
    auto recipe = bake::parseRecipe(text, recipePath, diagnostics);
    if (!recipe.has_value()) {
        return std::nullopt;
    }

    return bake::run(*recipe, recipePath, *recipeBytes, std::filesystem::current_path(),
                     diagnostics);
}

}  // namespace

int main(int argc, char** argv) {
    cli::Invocation invocation;
    try {
        invocation = cli::parse(bake::bakeToolName, argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;

    const std::string& recipePath =
        invocation.mode == cli::Mode::Bake ? invocation.bake.recipe : invocation.verify.recipe;

    std::string summary;
    if (auto outputs = produce(recipePath, diagnostics); outputs.has_value()) {
        const bool ok = invocation.mode == cli::Mode::Bake
                            ? bake::write(*outputs, invocation.bake.outputDir, diagnostics)
                            : bake::verify(*outputs, invocation.verify.packagePath,
                                           invocation.verify.reportPath, diagnostics);
        if (ok) {
            summary = std::format("{}: OK ({} {}: {} layers, {} goldens, {} weight bytes)",
                                  bake::bakeToolName,
                                  invocation.mode == cli::Mode::Bake ? "baked" : "verified",
                                  outputs->packageId, outputs->layerCount, outputs->goldenCount,
                                  outputs->weights.size());
        }
    }

    // Both formats go to stdout, matching every other MduX tool - see ShaderBakeMain.cpp for why
    // splitting text to stderr would defeat the shared envelope.
    const std::string rendered = cli::render(diagnostics, invocation.format, bake::bakeToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
