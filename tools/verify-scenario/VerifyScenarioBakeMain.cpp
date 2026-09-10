/**
 * @file VerifyScenarioBakeMain.cpp
 * @brief The `THEN_TOOLS` stage of the scenario bundle: replay, verify captures, serialize (#321).
 *
 * `mdux-scenariobake bake <recipe> <dir>` compiles the scenario and writes `scenario.json` and the
 * report naming it. This runs next, over that bundle, and adds `scenario-verification.json` plus the
 * report members that name it. The argument shape is the baker's own so `mdux_bake_artifact()`
 * invokes both the same way and the sequence stays one registration.
 *
 * ## Why a failed check is not a failed bake
 *
 * An obligation that ran and did not hold is recorded and the bundle written - that is evidence,
 * and suppressing it would leave the tree with no record that the scenario regressed. A run that
 * could not be *made* - no device, an unreadable input, a digest drift - exits non-zero and writes
 * nothing, so no committed file can claim a verification that did not happen. The gate that turns a
 * recorded failure red is `verify.scenario.<id>` and `evidence.scenario.<id>`.
 */
import std;
import mdux.tools.cli;
import mdux.tools.verify.artifact;
import mdux.tools.verify.scenario.artifact;
import mdux.tools.verify.scenario.driver;

namespace {

namespace cli = mdux::tools::cli;
namespace vs  = mdux::tools::verify::scenario;
namespace vu  = mdux::tools::verify;

[[nodiscard]] std::string usage() {
    return std::format("usage:\n  {} bake <recipe.toml> <output-dir>\n\n"
                       "Replays the scenario bundle in <output-dir>, writes {} beside it and extends its\n"
                       "report.json. The recipe is accepted for symmetry with the compiler and is not\n"
                       "re-read: what is verified is the bundle that was just produced.\n",
                       vs::artifactToolName,
                       vs::verificationFileName);
}

void fail(std::string_view message) {
    std::println(std::cerr, "{}: error: {}", vs::artifactToolName, message);
}

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in && !in.eof()) {
        return std::nullopt;
    }
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

    // Normalised so a trailing separator does not leave `filename()` empty - the scenario id passed
    // to `writeScenarioVerification()` below must be the bundle directory's name, and a `PackageHeader`
    // with an empty id would commit an artifact that `PackageHeader::readFrom()` then rejects.
    std::filesystem::path bundle = std::filesystem::path{arguments[2]}.lexically_normal();
    if (!bundle.has_filename()) {
        bundle = bundle.parent_path();
    }

    // The committed `generated/` tree is the artifact root: the scenario bundle being verified is the
    // freshly baked one in the build tree, while the screen, shader, font, text and image packages it
    // renders against are the committed artifacts every other consumer reads.
    const vs::RunResult result = vs::run(bundle, vs::RunOptions{.artifactRoot        = std::filesystem::path{"generated"},
                                                                .frameImageDirectory = {},
                                                                .captureDigestPath   = {}});

    const std::string rendered = cli::render(result.diagnostics, cli::Format::Text, vs::artifactToolName);
    if (!rendered.empty()) {
        std::print(std::cerr, "{}", rendered);
    }

    auto verification = vs::writeScenarioVerification(result, bundle.filename().generic_string());
    if (!verification.has_value()) {
        fail(std::format("cannot produce {}: {}", vs::verificationFileName, vs::describe(verification.error())));
        return 1;
    }

    const std::filesystem::path reportPath = bundle / "report.json";
    const auto                  reportText = readText(reportPath);
    if (!reportText.has_value()) {
        fail(std::format("cannot read {}", reportPath.generic_string()));
        return 1;
    }

    auto options = vs::verificationOptions(result);
    if (!options.has_value()) {
        fail(std::format("cannot resolve verification options: {}", vs::describe(options.error())));
        return 1;
    }
    auto extended = vs::extendScenarioReport(*reportText, *verification, *options, MDUX_TOOL_VERSION);
    if (!extended.has_value()) {
        fail(std::format("cannot extend report.json: {}", vs::describe(extended.error())));
        return 1;
    }

    const std::array bundleFiles{
        vu::BundleFile{.path = bundle / std::filesystem::path{vs::verificationFileName}, .text = *verification},
        vu::BundleFile{                                              .path = reportPath,       .text = *extended}
    };
    if (const auto published = vu::publishBundle(bundleFiles); !published.has_value()) {
        fail(std::string{vu::describe(published.error())});
        return 1;
    }

    std::println(std::cout,
                 "{}: {} obligations recorded over {} capture render{}",
                 vs::artifactToolName,
                 result.outcomes.size(),
                 result.renderCount,
                 result.renderCount == 1 ? "" : "s");
    return 0;
}
