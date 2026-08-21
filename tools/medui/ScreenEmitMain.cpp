/**
 * @file ScreenEmitMain.cpp
 * @brief `mdux-screenemit` entry point.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Its own executable rather than a mode of the compiler, for the reason `mdux-shaderemit` is its
 * own: compiling a `.medui` source and rendering a committed package are different jobs with
 * different inputs, and the second must remain runnable on an artifact whose source is not present.
 * That is what lets a device build regenerate its screens from the reviewed JSON alone.
 *
 * Diagnostics go through `cli::Diagnostic`, `cli::render` and `cli::exitStatus`, so `--format=json`
 * here is the same envelope every other MduX tool emits.
 */
import std;
import mdux.tools.cli;
import mdux.tools.medui.emit;

namespace {

namespace cli  = mdux::tools::cli;
namespace emit = mdux::tools::medui;

[[nodiscard]] std::string usage() {
    return std::format("usage:\n"
                       "  {} <package.json> <output-dir> [--format=json|text]\n"
                       "\n"
                       "Renders a committed screen package as a C++ module interface and a header,\n"
                       "both written into <output-dir>. The generated source carries a static_assert\n"
                       "that the screen satisfies its schema, so a malformed screen is a build error\n"
                       "in whatever links it.\n"
                       "\n"
                       "Generated code belongs in the build tree and is never committed; see\n"
                       "tools/medui/Emit.cppm.\n",
                       emit::emitToolName);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> positional;
    cli::Format                   format = cli::Format::Text;

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
            // Assigned rather than skipped, so that last flag wins.
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
        std::println(std::cerr, "expected exactly 2 arguments, got {}\n\n{}", positional.size(), usage());
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;
    std::string                  summary;
    if (auto outputs = emit::renderScreen(std::filesystem::path{positional[0]}, diagnostics); outputs.has_value()) {
        if (emit::writeScreen(*outputs, std::filesystem::path{positional[1]}, diagnostics)) {
            summary = std::format("{}: OK (emitted {} and {}.hpp)", emit::emitToolName, outputs->moduleName, outputs->stem);
        }
    }

    const std::string rendered = cli::render(diagnostics, format, emit::emitToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
