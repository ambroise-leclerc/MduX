/**
 * @file VerifyUiMain.cpp
 * @brief Thin command-line boundary for mdux-verify-ui.
 */
import std;
import mdux.tools.cli;
import mdux.tools.verify.driver;

int main(int argc, char** argv) {
    namespace cli    = mdux::tools::cli;
    namespace verify = mdux::tools::verify;

    verify::Invocation invocation;
    try {
        invocation = verify::parseArguments(argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    verify::RunResult result   = verify::run(invocation.screenDirectory);
    const std::string rendered = cli::render(result.diagnostics, invocation.format, verify::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && result.state == verify::RunState::Passed) {
        std::println(std::cout,
                     "{}: OK ({} obligations, {} render scope{})",
                     verify::toolName,
                     result.outcomes.size(),
                     result.renderCount,
                     result.renderCount == 1 ? "" : "s");
    }
    return verify::exitStatus(result.state);
}
