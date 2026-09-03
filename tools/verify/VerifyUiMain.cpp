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

    // The bundle's own `generated/` is the artifact root, exactly as the single-argument `run()`
    // resolves it. Spelled here because this overload takes the whole option set, and a default that
    // silently disagreed with the one-argument form is the kind of drift #253 kept out of the driver.
    const std::filesystem::path root = invocation.screenDirectory.parent_path().parent_path();
    verify::RunResult           result =
        verify::run(invocation.screenDirectory, verify::RunOptions{.artifactRoot = root, .diffImageDirectory = invocation.diffImageDirectory});
    const std::string rendered = cli::render(result.diagnostics, invocation.format, verify::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    // Named on stdout rather than left for the reader to find: a CI log that says a failure was
    // drawn, and where, is what turns an uploaded artifact into one somebody opens.
    if (invocation.format == cli::Format::Text) {
        for (const std::filesystem::path& image : result.diffImages) {
            std::println(std::cout, "{}: wrote diff image {}", verify::toolName, image.generic_string());
        }
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
