/**
 * @file VerifyUiMain.cpp
 * @brief Thin command-line boundary for mdux-verify-ui.
 */
import std;
import mdux.evidence.json;
import mdux.tools.cli;
import mdux.tools.verify.driver;
import mdux.tools.verify.medui_evidence;

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
    const std::filesystem::path root   = invocation.screenDirectory.parent_path().parent_path();
    verify::RunResult           result = verify::run(
        invocation.screenDirectory,
        verify::RunOptions{.artifactRoot = root, .diffImageDirectory = invocation.diffImageDirectory, .frameImageDirectory = invocation.frameImageDirectory});
    const std::string rendered = cli::render(result.diagnostics, invocation.format, verify::toolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    // Named on stdout rather than left for the reader to find: a CI log that says a failure was
    // drawn, and where, is what turns an uploaded artifact into one somebody opens.
    if (invocation.format == cli::Format::Text) {
        for (const std::filesystem::path& image : result.frameImages) {
            std::println(std::cout, "{}: wrote frame image {}", verify::toolName, image.generic_string());
        }
        for (const std::filesystem::path& image : result.diffImages) {
            std::println(std::cout, "{}: wrote diff image {}", verify::toolName, image.generic_string());
        }
    }
    // The derived MEDUI-PROFILE-RENDERED envelope, when asked for. An attachment for a reader and a
    // downstream consumer, never a committed artifact and never byte-compared (ADR-014 D4). A
    // failure to derive it is reported, not an exit-status change: the verdict is the checks.
    if (!invocation.meduiEvidenceDirectory.empty()) {
        namespace evj = mdux::evidence::json;
        // <root>/generated/screen/<id>  ->  <root>; an empty result (a relative --screen) leaves a
        // path relative to CWD, which every real invocation runs from the repo root.
        const std::filesystem::path manifest = invocation.screenDirectory.parent_path().parent_path().parent_path() / "medui-conformance.toml";
        const auto                  derived  = verify::deriveRenderedEvidence(result, invocation.screenDirectory.filename().string(), manifest);
        if (!derived.has_value()) {
            std::println(std::cerr, "{}: could not derive MEDUI evidence: {}", verify::toolName, verify::describe(derived.error()));
        } else if (const auto text = evj::write(derived->envelope); !text.has_value()) {
            std::println(std::cerr, "{}: could not serialize MEDUI evidence", verify::toolName);
        } else {
            std::error_code ec;
            std::filesystem::create_directories(invocation.meduiEvidenceDirectory, ec);
            const std::filesystem::path path =
                invocation.meduiEvidenceDirectory / (invocation.screenDirectory.filename().string() + ".medui-evidence.json");
            std::ofstream out{path, std::ios::binary};
            out << *text;
            if (invocation.format == cli::Format::Text) {
                std::println(std::cout, "{}: wrote MEDUI evidence {} ({} excluded, implementation-local)",
                             verify::toolName, path.generic_string(), derived->excludedOutcomes);
            }
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
