/**
 * @file PixelTestMain.cpp
 * @brief End-to-end textless rendered-truth execution through the production driver.
 */
import std;
import mdux.tools.verify.driver;

namespace {

void printDiagnostics(const mdux::tools::verify::RunResult& result, std::ostream& output) {
    for (std::size_t index = 0; index < result.diagnostics.size(); ++index)
        std::println(output, "{}", result.diagnostics[index].message);
}

}  // namespace

int main() {
    namespace vu = mdux::tools::verify;
    const std::filesystem::path root{MDUX_REPO_ROOT};
    const vu::RunResult         result = vu::run(root / "tests/verify_ui/fixtures/textless", root / "generated");
    // 77 is CTest's skip status and must mean exactly one thing: this host has no Vulkan device.
    // Every other impossibility -- unreadable artifacts, digest drift, a renderer that refused the
    // fixture -- is a failure, and reporting it as a skip would let CI pass on a broken driver.
    if (result.state == vu::RunState::NoRenderDevice) {
        printDiagnostics(result, std::cout);
        return 77;
    }
    if (result.state != vu::RunState::Passed || result.renderCount != 1 || result.obligations.size() != 2 || result.outcomes.size() != 2) {
        printDiagnostics(result, std::cerr);
        return 1;
    }
    std::println(std::cout, "mdux-verify-ui textless fixture: 2 obligations discharged in 1 render");
    return 0;
}
