/**
 * @file PixelTestMain.cpp
 * @brief End-to-end rendered-truth execution through the production driver.
 *
 * Two fixtures, one device. `textless` passes and proves the locale-free render scope ADR-014
 * decision 3 requires; `overdrawn` fails and proves the diff image #255 attaches on a failure.
 *
 * `overdrawn` is a screen whose `Panel` is listed after the `SignalTrace` it covers half of, so the
 * trace's field is painted and then partly painted over. Its golden opts into both checks: `Bounds`
 * still holds, because the covering panel is not the ground either and the painted box is still the
 * whole rectangle, while `ColorHash` reports `ForeignColour` at the first covered pixel. That pairing
 * is deliberate - one held and one failed obligation in one scope - because a fixture where
 * everything failed could not show that the image marks the failure rather than the frame.
 *
 * The failing one is here rather than in `verify_ui_spec` because nothing about it is checkable
 * without a frame: the image is composed from a readback, and a test that handed `composeDiff()` an
 * array it painted itself - which `DiffTests.cpp` does, and should - could not notice that the
 * driver never called it, wrote it to the wrong place, or drew the previous scope's frame.
 */
import std;
import mdux.tools.verify.driver;

namespace {

void printDiagnostics(const mdux::tools::verify::RunResult& result, std::ostream& output) {
    for (std::size_t index = 0; index < result.diagnostics.size(); ++index)
        std::println(output, "{}", result.diagnostics[index].message);
}

/// Whether `path` begins with the eight bytes every PNG begins with.
[[nodiscard]] bool looksLikePng(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    std::array<char, 8> head{};
    if (!file.read(head.data(), head.size()))
        return false;
    constexpr std::array<unsigned char, 8> signature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (static_cast<unsigned char>(head[index]) != signature[index])
            return false;
    }
    return true;
}

}  // namespace

int main() {
    namespace vu = mdux::tools::verify;
    const std::filesystem::path root{MDUX_REPO_ROOT};
    const std::filesystem::path generated = root / "generated";

    const vu::RunResult passing = vu::run(root / "tests/verify_ui/fixtures/textless", generated);
    // 77 is CTest's skip status and must mean exactly one thing: this host has no Vulkan device.
    // Every other impossibility -- unreadable artifacts, digest drift, a renderer that refused the
    // fixture -- is a failure, and reporting it as a skip would let CI pass on a broken driver.
    if (passing.state == vu::RunState::NoRenderDevice) {
        printDiagnostics(passing, std::cout);
        return 77;
    }
    if (passing.state != vu::RunState::Passed || passing.renderCount != 1 || passing.obligations.size() != 2 || passing.outcomes.size() != 2) {
        printDiagnostics(passing, std::cerr);
        return 1;
    }
    if (!passing.diffImages.empty()) {
        std::println(std::cerr, "a passing run wrote {} diff image(s) with no destination configured", passing.diffImages.size());
        return 1;
    }
    std::println(std::cout, "mdux-verify-ui textless fixture: 2 obligations discharged in 1 render");

    // Outside the source tree, because the diff image is a measurement of the frame rather than a
    // property of the screen - ADR-014 decision 4 keeps it out of `generated/`, and the "no
    // source-tree writes" gate on every CI leg would catch it if this drifted.
    std::error_code             ignored;
    const std::filesystem::path diffDirectory = std::filesystem::temp_directory_path(ignored) / "mdux-verify-ui-diff";
    std::filesystem::remove_all(diffDirectory, ignored);

    const vu::RunResult failing =
        vu::run(root / "tests/verify_ui/fixtures/overdrawn", vu::RunOptions{.artifactRoot = generated, .diffImageDirectory = diffDirectory});
    if (failing.state != vu::RunState::ChecksFailed) {
        std::println(std::cerr, "the overdrawn fixture must fail its ColorHash obligation, not be impossible to run");
        printDiagnostics(failing, std::cerr);
        return 1;
    }
    if (failing.diffImages.size() != 1) {
        std::println(std::cerr, "expected one diff image for one failing render scope, got {}", failing.diffImages.size());
        printDiagnostics(failing, std::cerr);
        return 1;
    }
    // The name is asserted because CI uploads the directory: a scope whose spelling leaked into a
    // filename - `(locale-free)` - is a name people quote wrongly in the shell that fetches it.
    if (failing.diffImages[0] != diffDirectory / "overdrawn.locale-free.png") {
        std::println(std::cerr, "diff image written to unexpected path {}", failing.diffImages[0].generic_string());
        return 1;
    }
    if (!looksLikePng(failing.diffImages[0])) {
        std::println(std::cerr, "{} is not a PNG", failing.diffImages[0].generic_string());
        return 1;
    }
    std::filesystem::remove_all(diffDirectory, ignored);
    std::println(std::cout, "mdux-verify-ui overdrawn fixture: failure reported and drawn");
    return 0;
}
