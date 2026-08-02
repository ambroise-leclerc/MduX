/**
 * @file emit_main.cpp
 * @brief `mdux-shaderemit` entry point.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## Why this is its own executable rather than an `emit` mode of mdux-shaderbake
 *
 * `mdux::tools::cli::parse` implements one grammar - `bake` and `verify` - shared by every baker,
 * and widening it would put a mode there that only some tools have. Emission is also genuinely
 * kind-specific in a way baking is not: a font atlas and an ML model will each want to generate
 * different types, so a shared `emit` verb would be a shared verb with nothing shared behind it.
 *
 * The part that *is* shared is the one that matters for an agent reading the output: diagnostics
 * go through `cli::Diagnostic`, `cli::render` and `cli::exitStatus`, so this tool's `--format=json`
 * is the same envelope every other MduX tool emits.
 */
import std;
import mdux.tools.cli;
import mdux.tools.shaderemit;

namespace {

namespace cli = mdux::tools::cli;
namespace emit = mdux::tools::shaderemit;

[[nodiscard]] std::string usage() {
    return std::string{"usage:\n  "} + std::string{emit::kToolName} +
           " <package.json> <output-dir> [--format=json|text]\n"
           "\n"
           "Renders a committed shader package as a C++ module interface and a header, both\n"
           "written into <output-dir>. The sidecar is read from beside <package.json>, under the\n"
           "name the package records.\n"
           "\n"
           "Generated code belongs in the build tree and is never committed; see\n"
           "tools/shader/Emit.cppm.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> positional;
    cli::Format format = cli::Format::Text;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--help" || argument == "-h") {
            std::print(std::cout, "{}", usage());
            return 0;
        }
        if (argument == "--format=json") {
            format = cli::Format::Json;
            continue;
        }
        if (argument == "--format=text") {
            continue;
        }
        if (argument.starts_with("-")) {
            std::println(std::cerr, "unrecognized option '{}'\n\n{}", argument, usage());
            return 2;
        }
        positional.push_back(argument);
    }

    if (positional.size() != 2) {
        std::println(std::cerr, "expected exactly 2 arguments, got {}\n\n{}", positional.size(),
                     usage());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;
    std::string summary;
    if (auto outputs = emit::render(std::filesystem::path{positional[0]}, diagnostics);
        outputs.has_value()) {
        if (emit::write(*outputs, std::filesystem::path{positional[1]}, diagnostics)) {
            summary = std::format("{}: OK (emitted {} and {}.hpp)", emit::kToolName,
                                  outputs->moduleName, outputs->stem);
        }
    }

    const std::string rendered = cli::render(diagnostics, format, emit::kToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
