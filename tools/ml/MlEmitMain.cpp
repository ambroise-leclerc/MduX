/**
 * @file MlEmitMain.cpp
 * @brief `mdux-mlemit` entry point.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 */
import std;
import mdux.tools.cli;
import mdux.tools.ml.emit;

namespace {

namespace cli  = mdux::tools::cli;
namespace emit = mdux::tools::ml;

/// @brief Builds the command-line usage text from the canonical tool name.
[[nodiscard]] std::string usage() {
    return std::format("usage:\n"
                       "  {} <package.json> <output-dir> [--format=json|text]\n\n"
                       "Renders a committed model package as a C++ module interface and a header. The\n"
                       "weights sidecar is not embedded; it remains caller-supplied data.\n",
                       emit::emitToolName);
}

}  // namespace

/// @brief Runs the model emitter command-line tool.
int main(int argc, char** argv) {
    std::vector<std::string_view> positional;
    cli::Format                   format = cli::Format::Text;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            std::print(std::cout, "{}", usage());
            return 0;
        }
        if (argument == "--format=json") {
            format = cli::Format::Json;
            continue;
        }
        if (argument == "--format=text") {
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
    if (auto outputs = emit::renderModel(std::filesystem::path{positional[0]}, diagnostics);
        outputs.has_value() && emit::writeModel(*outputs, std::filesystem::path{positional[1]}, diagnostics)) {
        summary = std::format("{}: OK (emitted {} and {}.hpp)", emit::emitToolName, outputs->moduleName, outputs->stem);
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
