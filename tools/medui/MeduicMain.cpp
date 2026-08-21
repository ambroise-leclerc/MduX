/**
 * @file MeduicMain.cpp
 * @brief `mdux-meduic` entry point: the `.medui` compiler as a baker.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Split library-and-executable like every other baker, so tests drive `run()`, `write()` and
 * `verify()` as calls. Spawning this process per case would make a failing assertion report that an
 * exit status was wrong, which is the least useful thing it could say about a compiler.
 *
 * It speaks the shared `bake`/`verify` grammar, so `mdux_bake_artifact()` registers a screen the
 * same way it registers a font or a shader, and `--format=json` produces the same envelope every
 * other MduX tool produces.
 */
import std;
import mdux.tools.cli;
import mdux.tools.medui.compile;

namespace {

namespace cli   = mdux::tools::cli;
namespace medui = mdux::tools::medui;

/// Reads the recipe and runs every compiler stage. Shared by both modes: `verify` must produce the
/// same bytes `bake` would before it can compare them to what is committed.
[[nodiscard]] std::optional<medui::CompileOutputs> produce(const std::string& recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    const std::optional<std::vector<std::byte>> recipeBytes = medui::readFile(recipePath);
    if (!recipeBytes.has_value()) {
        // The one diagnostic this tool raises itself; every other one comes from a stage.
        cli::Diagnostic unreadable;
        unreadable.file     = recipePath;
        unreadable.code     = "MEDUI-E000";
        unreadable.severity = cli::Severity::Error;
        unreadable.message  = "the recipe file could not be opened";
        unreadable.fixHint  = "check the path passed on the command line, and that the file is readable";
        diagnostics.push_back(std::move(unreadable));
        return std::nullopt;
    }

    const std::string_view             recipeText{reinterpret_cast<const char*>(recipeBytes->data()), recipeBytes->size()};
    const std::optional<medui::Recipe> recipe = medui::parseRecipe(recipeText, recipePath, diagnostics);
    if (!recipe.has_value()) {
        return std::nullopt;
    }

    // The working directory is the repository root - `mdux_bake_artifact()` runs every baker that
    // way - so every path the recipe names and every path the report records stays repository
    // relative, which is what keeps a report free of absolute paths.
    return medui::run(*recipe, recipePath, *recipeBytes, std::filesystem::current_path(), diagnostics);
}

}  // namespace

int main(int argc, char** argv) {
    cli::Invocation invocation;
    try {
        invocation = cli::parse(medui::compilerToolName, argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;

    const std::string& recipePath = invocation.mode == cli::Mode::Bake ? invocation.bake.recipe : invocation.verify.recipe;

    std::string summary;
    if (auto outputs = produce(recipePath, diagnostics); outputs.has_value()) {
        bool ok = false;
        if (invocation.mode == cli::Mode::Bake) {
            ok = medui::write(*outputs, invocation.bake.outputDir, diagnostics);
        } else {
            // The shared grammar carries two output paths and a screen has three. The goldens
            // sidecar is not an independent fact: ADR-012 puts all three files in one directory, so
            // it is read from beside the package rather than named a second time on a command line
            // where the two could disagree.
            const std::filesystem::path packagePath{invocation.verify.packagePath};
            const std::filesystem::path goldensPath = packagePath.parent_path() / "goldens.json";
            ok                                      = medui::verify(*outputs, packagePath, goldensPath, invocation.verify.reportPath, diagnostics);
        }
        if (ok) {
            summary = std::format("{}: OK ({} {}: {} nodes, {} golden references)",
                                  medui::compilerToolName,
                                  invocation.mode == cli::Mode::Bake ? "compiled" : "verified",
                                  outputs->screenId,
                                  outputs->nodeCount,
                                  outputs->goldenCount);
        }
    }

    // Both formats go to stdout, matching every other MduX tool: an agent should not have to know
    // which stream each one uses, which is what the shared envelope exists to remove.
    const std::string rendered = cli::render(diagnostics, invocation.format, medui::compilerToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
