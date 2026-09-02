/**
 * @file VerifyBakeMain.cpp
 * @brief The second command in the screen bundle's production sequence: render, then serialize.
 *
 * `mdux-meduic bake <recipe> <dir>` compiles the screen and writes `package.json`, `goldens.json`
 * and the report naming those two. This runs next, over the bundle that command just produced, and
 * adds the fourth file plus the report members that name it. The argument shape is deliberately the
 * baker's own - `bake <recipe> <output-dir>` - so `mdux_bake_artifact()` invokes both tools the same
 * way and the sequence stays one registration rather than two.
 *
 * ## Why this is a separate process rather than a stage inside the compiler
 *
 * The compiler is what every screen goes through, and a screen compile that required a Vulkan
 * device would make a GPU a prerequisite of reading a `.medui` file. Verification needs one; the
 * compile does not, and fusing them would spread that requirement over `mdux-medui-check`, the
 * emit path and every test that drives a compile as a call.
 *
 * ## Why a failed check is not a failed bake
 *
 * A node in the wrong place and a missing Vulkan loader are different facts, and #254 requires the
 * artifact to distinguish them. An inability to run - no device, an unreadable input, a digest that
 * drifted - exits non-zero and writes nothing, so no committed file can claim a verification that
 * did not happen. A check that ran and did not hold is recorded as the finding it produced and the
 * bundle is written: that is evidence, and suppressing it would leave the tree with no record that
 * the screen fails. The gate that turns such an artifact red belongs to #255.
 */
import std;
import mdux.evidence.json;
import mdux.tools.cli;
import mdux.tools.verify.artifact;
import mdux.tools.verify.driver;

namespace {

namespace cli = mdux::tools::cli;
namespace vu  = mdux::tools::verify;

[[nodiscard]] std::string usage() {
    return std::format("usage:\n  {} bake <recipe.toml> <output-dir>\n\n"
                       "Renders the screen bundle in <output-dir>, writes verification.json beside it and\n"
                       "extends its report.json. The recipe is accepted for symmetry with the compiler and\n"
                       "is not re-read: what is verified is the bundle that was just produced.\n",
                       vu::artifactToolName);
}

void fail(std::string_view message) {
    std::println(std::cerr, "{}: error: {}", vu::artifactToolName, message);
}

[[nodiscard]] bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        fail(std::format("cannot write {}", path.generic_string()));
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in && !in.eof())
        return std::nullopt;
    return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> arguments{argv + (argc > 0 ? 1 : 0), argv + std::max(argc, 1)};
    if (arguments.size() == 1 && (arguments[0] == "--help" || arguments[0] == "-h")) {
        std::print(std::cout, "{}", usage());
        return 2;
    }
    if (arguments.size() != 3 || arguments[0] != "bake") {
        std::print(std::cerr, "{}", usage());
        return 2;
    }

    const std::filesystem::path bundle = arguments[2];

    // The artifact root is the committed `generated/` tree: the screen bundle being verified is the
    // freshly baked one in the build tree, while the shader, font and text packages it renders
    // against are the committed artifacts every other consumer reads. Passing it explicitly is what
    // keeps those two apart - the single-argument overload would look for all of them beside the
    // bundle, which during a bake is a build directory.
    const vu::RunResult result = vu::run(bundle, std::filesystem::path{"generated"});

    const std::string rendered = cli::render(result.diagnostics, cli::Format::Text, vu::toolName);
    if (!rendered.empty()) {
        std::print(std::cerr, "{}", rendered);
    }

    auto verification = vu::writeVerification(result, bundle.filename().generic_string());
    if (!verification.has_value()) {
        fail(std::format("cannot produce {}: {}", vu::verificationFileName, vu::describe(verification.error())));
        return 1;
    }

    const std::filesystem::path reportPath = bundle / "report.json";
    const auto                  reportText = readText(reportPath);
    if (!reportText.has_value()) {
        fail(std::format("cannot read {}", reportPath.generic_string()));
        return 1;
    }

    auto options = vu::verificationOptions(result, "generated");
    if (!options.has_value()) {
        fail(std::format("cannot resolve verification options: {}", vu::describe(options.error())));
        return 1;
    }
    auto extended = vu::extendReport(*reportText, *verification, *options);
    if (!extended.has_value()) {
        fail(std::format("cannot extend report.json: {}", vu::describe(extended.error())));
        return 1;
    }

    // Both files or neither: a bundle carrying verification.json whose report does not name it would
    // pass its own byte comparison while the report stopped describing the artifact beside it.
    if (!writeText(bundle / std::filesystem::path{vu::verificationFileName}, *verification) || !writeText(reportPath, *extended)) {
        return 1;
    }

    std::println(std::cout,
                 "{}: {} obligations recorded in {} render scope{}",
                 vu::artifactToolName,
                 result.outcomes.size(),
                 result.renderCount,
                 result.renderCount == 1 ? "" : "s");
    return 0;
}
