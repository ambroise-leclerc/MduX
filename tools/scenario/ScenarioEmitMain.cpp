/**
 * @file ScenarioEmitMain.cpp
 * @brief `mdux-scenarioemit` entry point: a committed `scenario.json` becomes `constexpr` C++.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-012 What a compiled artifact emits
 *
 * Its own executable for `mdux-screenemit`'s reason: rendering a committed artifact into C++ is a
 * different job from compiling a source, with different inputs. Usage:
 *   mdux-scenarioemit <scenario.json> <output-dir>
 */
import std;
import mdux.tools.cli;
import mdux.tools.scenarioemit;

namespace {
namespace cli = mdux::tools::cli;
namespace sc  = mdux::tools::scenario;
}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    if (args.size() != 2) {
        std::println(std::cerr, "usage: mdux-scenarioemit <scenario.json> <output-dir>");
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;
    const auto                   outputs = sc::renderScenario(std::filesystem::path{args[0]}, diagnostics);
    bool                         ok      = outputs.has_value();
    if (ok) {
        ok = sc::writeScenario(*outputs, std::filesystem::path{args[1]}, diagnostics);
    }

    const std::string rendered = cli::render(diagnostics, cli::Format::Text, "mdux-scenarioemit");
    if (!rendered.empty()) {
        std::print(std::cerr, "{}", rendered);
    }
    if (ok) {
        std::println(std::cout, "mdux-scenarioemit: OK (emitted {} and {}.hpp)", outputs->moduleName, outputs->stem);
    }
    return cli::exitStatus(diagnostics);
}
