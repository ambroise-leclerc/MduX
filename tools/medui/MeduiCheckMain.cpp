/**
 * @file MeduiCheckMain.cpp
 * @brief `mdux-medui-check` entry point: validate one `.medui` file, print diagnostics, exit.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Its own grammar rather than the shared `bake`/`verify` one, because this is neither: it takes a
 * file and produces no artifact. What it does share is the envelope - `cli::render` and
 * `cli::exitStatus` - so an agent parsing `--format=json` here parses what every other MduX tool
 * emits, which is the whole point of #118.
 *
 * Exit status is `cli::exitStatus`'s: non-zero when anything of error severity was reported, zero
 * otherwise. The notes about what a single file cannot cover do not fail a check; they are
 * statements about the run, and a run that could not check locales still checked everything else.
 */
import std;
import mdux.tools.cli;
import mdux.tools.medui.check;

namespace {

namespace cli   = mdux::tools::cli;
namespace medui = mdux::tools::medui;

[[nodiscard]] std::string usage() {
    return std::format("usage:\n"
                       "  {} <screen.medui> [--format=json|text]\n"
                       "\n"
                       "Validates one .medui file and prints what it finds. Writes nothing.\n"
                       "\n"
                       "Checked: the grammar, the component dictionary, field value domains,\n"
                       "hardcoded strings, theme tokens, the @safety_critical rules, and - when the\n"
                       "file declares a `surface:` - bounded layout and the golden set.\n"
                       "\n"
                       "Not checked, and reported as notes: text keys and text budgets, which need\n"
                       "the approved locales a recipe names. Compile through mdux-meduic for those.\n"
                       "\n"
                       "Exits non-zero when anything of error severity was found.\n",
                       medui::checkToolName);
}

[[nodiscard]] std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    // tellg() answers -1 on a stream error rather than throwing, and sizing the string before the
    // check would turn that into a request for 2^64-1 bytes.
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        file.read(text.data(), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> positional;
    cli::Format                   format = cli::Format::Text;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--help" || argument == "-h") {
            std::print(std::cout, "{}", usage());
            return 0;
        }
        if (argument == "--format=json") {
            format = cli::Format::Json;
            continue;
        }
        if (argument == "--format=text") {
            // Assigned rather than skipped, so that last flag wins.
            format = cli::Format::Text;
            continue;
        }
        if (argument.starts_with("-")) {
            std::println(std::cerr, "unrecognized option '{}'\n\n{}", argument, usage());
            return 2;
        }
        positional.push_back(argument);
    }

    if (positional.size() != 1) {
        std::println(std::cerr, "expected exactly one file, got {}\n\n{}", positional.size(), usage());
        return 2;
    }

    const std::string                path{positional[0]};
    const std::optional<std::string> source = readFile(path);
    if (!source.has_value()) {
        // Reported through the envelope rather than as a bare message, so a caller parsing JSON
        // gets a finding here exactly as it would for a syntax error.
        cli::Diagnostic unreadable;
        unreadable.file     = path;
        unreadable.code     = "MEDUI-E003";
        unreadable.severity = cli::Severity::Error;
        unreadable.message  = "the .medui source could not be opened";
        unreadable.fixHint  = "check the path, and that the file is readable";

        const std::array<cli::Diagnostic, 1> diagnostics{std::move(unreadable)};
        std::print(std::cout, "{}", cli::render(diagnostics, format, medui::checkToolName));
        return cli::exitStatus(diagnostics);
    }

    const medui::CheckResult result = medui::checkScreen(*source, path);

    const std::string rendered = cli::render(result.diagnostics, format, medui::checkToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (format == cli::Format::Text && result.ok()) {
        std::println(std::cout, "{}: OK ({})", medui::checkToolName, path);
    }

    return cli::exitStatus(result.diagnostics);
}
