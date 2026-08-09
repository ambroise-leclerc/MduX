/**
 * @file ShaderEmitMain.cpp
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

/// Built with std::format rather than by concatenating std::strings.
///
/// Not a style preference: GCC 15.3 at -O3 raises a false -Warray-bounds on the `std::string{…} +
/// …` chain this used to be, claiming a memcpy past the 32-byte short-string buffer that the
/// reallocation makes impossible. Warnings are errors here, so the build failed on the GCC 15 leg
/// alone while GCC 16 and MSVC were green. std::format sidesteps it and reads better anyway.
[[nodiscard]] std::string usage() {
    return std::format(
        "usage:\n"
        "  {} <package.json> <output-dir> [--format=json|text]\n"
        "\n"
        "Renders a committed shader package as a C++ module interface and a header, both\n"
        "written into <output-dir>. The sidecar is read from beside <package.json>, under the\n"
        "name the package records.\n"
        "\n"
        "Generated code belongs in the build tree and is never committed; see\n"
        "tools/shader/Emit.cppm.\n",
        emit::toolName);
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
            // Assigned rather than skipped: last flag wins, so a wrapper that appends
            // `--format=text` to a command line which already carried `--format=json` gets text.
            // Falling through here silently kept the earlier json and ignored the later request.
            format = cli::Format::Text;
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
            summary = std::format("{}: OK (emitted {} and {}.hpp)", emit::toolName,
                                  outputs->moduleName, outputs->stem);
        }
    }

    const std::string rendered = cli::render(diagnostics, format, emit::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
