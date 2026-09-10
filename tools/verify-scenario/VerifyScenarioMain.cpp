/**
 * @file VerifyScenarioMain.cpp
 * @brief Thin command-line boundary for mdux-verify-scenario - the verdict driver over the committed
 *        scenario bundle (#321, ADR-021).
 */
import std;
import mdux.tools.cli;
import mdux.tools.verify.scenario.driver;

int main(int argc, char** argv) {
    namespace cli = mdux::tools::cli;
    namespace vs  = mdux::tools::verify::scenario;

    vs::Invocation invocation;
    try {
        invocation = vs::parseArguments(argc, argv);
    } catch (const cli::UsageError& error) {
        std::println(std::cerr, "{}", error.what());
        return 2;
    }

    // The bundle's own `generated/` is the artifact root, exactly as the single-argument `run()`
    // resolves it - spelled here because this overload takes the whole option set.
    const std::filesystem::path root = invocation.scenarioDirectory.parent_path().parent_path();
    const vs::RunResult         result =
        vs::run(invocation.scenarioDirectory,
                vs::RunOptions{.artifactRoot         = root,
                               .frameImageDirectory  = invocation.frameImageDirectory,
                               .captureDigestPath    = invocation.captureDigestPath});

    const std::string rendered = cli::render(result.diagnostics, invocation.format, vs::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }

    if (invocation.format == cli::Format::Text) {
        for (const std::filesystem::path& image : result.frameImages) {
            std::println(std::cout, "{}: wrote frame image {}", vs::toolName, image.generic_string());
        }
        if (result.state == vs::RunState::Passed) {
            std::println(std::cout,
                         "{}: OK ({} obligations over {} capture render{})",
                         vs::toolName,
                         result.outcomes.size(),
                         result.renderCount,
                         result.renderCount == 1 ? "" : "s");
        }
    }
    return vs::exitStatus(result.state);
}
