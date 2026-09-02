/**
 * @file PixelTestMain.cpp
 * @brief End-to-end textless rendered-truth execution through the production driver.
 */
import std;
import mdux.tools.verify.driver;

int main() {
    namespace vu = mdux::tools::verify;
    const std::filesystem::path root{MDUX_REPO_ROOT};
    const vu::RunResult         result = vu::run(root / "tests/verify_ui/fixtures/textless", root / "generated");
    if (result.state == vu::RunState::CouldNotRun) {
        for (const auto& diagnostic : result.diagnostics)
            std::println(std::cout, "{}", diagnostic.message);
        return 77;
    }
    if (result.state != vu::RunState::Passed || result.renderCount != 1 || result.obligations.size() != 2 || result.outcomes.size() != 2) {
        for (const auto& diagnostic : result.diagnostics)
            std::println(std::cerr, "{}", diagnostic.message);
        return 1;
    }
    std::println(std::cout, "mdux-verify-ui textless fixture: 2 obligations discharged in 1 render");
    return 0;
}
