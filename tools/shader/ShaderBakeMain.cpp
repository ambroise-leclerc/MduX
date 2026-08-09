/**
 * @file ShaderBakeMain.cpp
 * @brief `mdux-shaderbake` entry point.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Everything interesting is in mdux.tools.shaderbake; this file is the boundary where a command
 * line becomes a call and a diagnostic list becomes an exit status. Keeping it that thin is what
 * lets the tests drive `run()`, `write()` and `verify()` directly rather than through a process.
 *
 * Paths are resolved against the current working directory, which `mdux_bake_artifact()` sets to
 * `CMAKE_SOURCE_DIR`. That is deliberate and is what makes every path recorded in `report.json`
 * repository-relative rather than a description of the machine that ran the bake.
 */
import std;
import mdux.tools.cli;
import mdux.tools.shaderbake;

namespace {

namespace cli = mdux::tools::cli;
namespace bake = mdux::tools::shaderbake;

/// Reads the recipe and produces every output byte, or reports why it could not.
[[nodiscard]] std::optional<bake::BakeOutputs> produce(const std::string& recipePath,
                                                       std::vector<cli::Diagnostic>& diagnostics) {
    auto recipeBytes = bake::readFile(recipePath);
    if (!recipeBytes.has_value()) {
        diagnostics.push_back(
            cli::Diagnostic{.file = recipePath,
                            .code = "SHB000",
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
        invocation = cli::parse(bake::toolName, argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;

    const std::string& recipePath = invocation.mode == cli::Mode::Bake
                                        ? invocation.bake.recipe
                                        : invocation.verify.recipe;

    std::string summary;
    if (auto outputs = produce(recipePath, diagnostics); outputs.has_value()) {
        const bool ok = invocation.mode == cli::Mode::Bake
                            ? bake::write(*outputs, invocation.bake.outputDir, diagnostics)
                            : bake::verify(*outputs, invocation.verify.packagePath,
                                           invocation.verify.reportPath, diagnostics);
        if (ok) {
            summary = std::format("{}: OK ({} {}: {} modules, {} sidecar bytes)", bake::toolName,
                                  invocation.mode == cli::Mode::Bake ? "baked" : "verified",
                                  outputs->packageId, outputs->moduleCount,
                                  outputs->sidecar.size());
        }
    }

    // Both formats go to stdout, matching mdux-docs-lint and mdux-evidence-lint. Splitting text
    // to stderr and JSON to stdout would be the more conventional choice, but it would mean an
    // agent had to know which stream each MduX tool used, which is exactly the per-tool knowledge
    // the shared envelope exists to remove.
    //
    // In JSON mode `render` emits the envelope even with no findings, so a consumer always has
    // something to parse; the human-readable summary is suppressed there for the same reason.
    const std::string rendered = cli::render(diagnostics, invocation.format, bake::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
