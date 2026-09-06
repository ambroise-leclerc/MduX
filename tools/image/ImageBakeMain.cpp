/**
 * @file ImageBakeMain.cpp
 * @brief Command-line entry point for mdux-imagebake.
 */
import std;
import mdux.tools.cli;
import mdux.tools.imagebake;

namespace {
namespace cli  = mdux::tools::cli;
namespace bake = mdux::tools::imagebake;

[[nodiscard]] std::optional<bake::BakeOutputs> produce(const std::string& recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    auto bytes = bake::readFile(recipePath);
    if (!bytes.has_value()) {
        diagnostics.push_back({.file     = recipePath,
                               .code     = "IMB000",
                               .severity = cli::Severity::Error,
                               .message  = "cannot read recipe",
                               .fixHint  = "Paths are resolved from the repository root."});
        return std::nullopt;
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto                   recipe = bake::parseRecipe(text, recipePath, diagnostics);
    if (!recipe.has_value())
        return std::nullopt;
    return bake::run(*recipe, recipePath, *bytes, std::filesystem::current_path(), diagnostics);
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
    const std::string&           recipePath = invocation.mode == cli::Mode::Bake ? invocation.bake.recipe : invocation.verify.recipe;
    std::string                  summary;
    if (auto outputs = produce(recipePath, diagnostics); outputs.has_value()) {
        const bool ok = invocation.mode == cli::Mode::Bake ? bake::write(*outputs, invocation.bake.outputDir, diagnostics)
                                                           : bake::verify(*outputs, invocation.verify.packagePath, invocation.verify.reportPath, diagnostics);
        if (ok)
            summary = std::format("{}: OK ({} {}: {}x{}, {} bytes)",
                                  bake::toolName,
                                  invocation.mode == cli::Mode::Bake ? "baked" : "verified",
                                  outputs->packageId,
                                  outputs->width,
                                  outputs->height,
                                  outputs->sidecar.size());
    }
    const std::string rendered = cli::render(diagnostics, invocation.format, bake::toolName);
    if (!rendered.empty())
        std::print(std::cout, "{}", rendered);
    if (invocation.format == cli::Format::Text && !summary.empty())
        std::println(std::cout, "{}", summary);
    return cli::exitStatus(diagnostics);
}
