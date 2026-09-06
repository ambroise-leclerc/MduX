/**
 * @file MeduicMain.cpp
 * @brief `mdux-meduic` entry point: the `.medui` compiler as a baker.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Split library-and-executable like every other baker, so tests drive `run()`, `write()` and
 * `verify()` as calls. Spawning this process per case would make a failing assertion report that an
 * exit status was wrong, which is the least useful thing it could say about a compiler.
 *
 * It speaks the shared `bake`/`verify` grammar, so `mdux_bake_artifact()` registers a screen the
 * same way it registers a font or a shader, and `--format=json` produces the same envelope every
 * other MduX tool produces.
 *
 * ## Two informational modes that produce no artifact
 *
 * `--grammar` and `--explain` (#263) are handled here, *before* `cli::parse()`, rather than by
 * widening the shared grammar. That placement is the decision worth recording: `cli::Mode` is
 * `Bake` or `Verify` for six other bakers, and neither of these is either - they read no recipe,
 * write no file and produce no diagnostic. Adding them upstream would have put two modes that mean
 * nothing for a font or a shader into every one of those tools' usage text.
 *
 * Both answer from `mdux.tools.medui.grammar`, which reads the compiler's own tables. This file
 * holds only the argument shapes and the exit statuses.
 */
import std;
import mdux.tools.cli;
import mdux.tools.medui.compile;
import mdux.tools.medui.grammar;

namespace {

namespace cli   = mdux::tools::cli;
namespace medui = mdux::tools::medui;

/// Reads the recipe and runs every compiler stage. Shared by both modes: `verify` must produce the
/// same bytes `bake` would before it can compare them to what is committed.
[[nodiscard]] std::optional<medui::CompileOutputs> produce(const std::string& recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    const std::optional<std::vector<std::byte>> recipeBytes = medui::readFile(recipePath);
    if (!recipeBytes.has_value()) {
        // The one diagnostic this tool raises itself; every other one comes from a stage.
        cli::Diagnostic unreadable;
        unreadable.file     = recipePath;
        unreadable.code     = "MEDUI-E000";
        unreadable.severity = cli::Severity::Error;
        unreadable.message  = "the recipe file could not be opened";
        unreadable.fixHint  = "check the path passed on the command line, and that the file is readable";
        diagnostics.push_back(std::move(unreadable));
        return std::nullopt;
    }

    const std::string_view             recipeText{reinterpret_cast<const char*>(recipeBytes->data()), recipeBytes->size()};
    const std::optional<medui::Recipe> recipe = medui::parseRecipe(recipeText, recipePath, diagnostics);
    if (!recipe.has_value()) {
        return std::nullopt;
    }

    // The working directory is the repository root - `mdux_bake_artifact()` runs every baker that
    // way - so every path the recipe names and every path the report records stays repository
    // relative, which is what keeps a report free of absolute paths.
    return medui::run(*recipe, recipePath, *recipeBytes, std::filesystem::current_path(), diagnostics);
}

/**
 * @brief Runs the compile and returns the summary line, or nothing when a diagnostic was reported.
 *
 * The `try` is an outermost boundary for a *bug*, not for author input. Every recipe-controlled
 * failure - a malformed slug, an unusable budget, a locale the font does not approve - is a
 * diagnostic the driver reports, so an escaping exception means a gate was bypassed or an invariant
 * this compiler believes in does not hold. Even then the tool has to fail like a tool: a consumer
 * that asked for `--format=json` is entitled to an envelope, not a terminate handler's message.
 */
[[nodiscard]] std::string compile(const cli::Invocation& invocation, const std::string& recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    try {
        auto outputs = produce(recipePath, diagnostics);
        if (!outputs.has_value()) {
            return {};
        }

        bool ok = false;
        if (invocation.mode == cli::Mode::Bake) {
            ok = medui::write(*outputs, invocation.bake.outputDir, diagnostics);
        } else {
            // The shared grammar carries two output paths and a screen has three. The goldens
            // sidecar is not an independent fact: ADR-012 puts all three files in one directory, so
            // it is read from beside the package rather than named a second time on a command line
            // where the two could disagree.
            const std::filesystem::path packagePath{invocation.verify.packagePath};
            const std::filesystem::path goldensPath = packagePath.parent_path() / "goldens.json";
            ok                                      = medui::verify(*outputs, packagePath, goldensPath, invocation.verify.reportPath, diagnostics);
        }
        if (!ok) {
            return {};
        }
        return std::format("{}: OK ({} {}: {} nodes, {} golden references)",
                           medui::compilerToolName,
                           invocation.mode == cli::Mode::Bake ? "compiled" : "verified",
                           outputs->screenId,
                           outputs->nodeCount,
                           outputs->goldenCount);
    } catch (const std::exception& error) {
        cli::Diagnostic internal;
        internal.file     = recipePath;
        internal.code     = "MEDUI-E000";
        internal.severity = cli::Severity::Error;
        internal.message  = std::format("the compiler stopped on an internal error: {}", error.what());
        internal.fixHint  = "this is a defect in mdux-meduic rather than in the recipe; please report it with the recipe attached";
        diagnostics.push_back(std::move(internal));
        return {};
    }
}

/// The value after `--explain`, whether it was written joined or separate, or nothing.
[[nodiscard]] std::optional<std::string_view> explainArgument(std::span<const std::string_view> arguments) {
    constexpr std::string_view joined = "--explain=";
    if (arguments[0].starts_with(joined)) {
        return arguments[0].substr(joined.size());
    }
    return arguments.size() >= 2 ? std::optional{arguments[1]} : std::nullopt;
}

/**
 * @brief Handles `--grammar` and `--explain`, or reports that this was neither.
 *
 * Returns the exit status when it took the invocation, and nothing when the caller should carry on
 * to the shared bake/verify grammar.
 *
 * An unregistered code exits 2 - the shared usage status - rather than 0 with an empty line. That is
 * #263's own acceptance and the whole reason a registry exists: a tool that answered every input
 * with something would tell an agent that `MEDUI-E999` is a real code with nothing to say about it.
 */
[[nodiscard]] std::optional<int> informational(std::span<const std::string_view> arguments) {
    if (arguments.empty()) {
        return std::nullopt;
    }

    if (arguments[0] == "--grammar") {
        if (arguments.size() != 1) {
            std::println(std::cerr, "{}: --grammar takes no further arguments", medui::compilerToolName);
            return 2;
        }
        // Canonical JSON on stdout and nothing else, so `mdux-meduic --grammar > grammar.json` is
        // the whole publication step and the file it writes is the file this repository commits.
        std::print(std::cout, "{}", medui::grammarJson());
        return 0;
    }

    if (arguments[0] == "--explain" || arguments[0].starts_with("--explain=")) {
        const std::optional<std::string_view> code = explainArgument(arguments);
        if (!code.has_value() || code->empty() || (arguments[0] == "--explain" && arguments.size() != 2)) {
            std::println(std::cerr, "{}: --explain takes exactly one diagnostic code, for example MEDUI-E030", medui::compilerToolName);
            return 2;
        }
        const std::optional<std::string> explanation = medui::explain(*code);
        if (!explanation.has_value()) {
            std::println(std::cerr, "{}: '{}' is not a diagnostic code this compiler publishes", medui::compilerToolName, *code);
            return 2;
        }
        std::println(std::cout, "{}", *explanation);
        return 0;
    }

    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (const std::optional<int> status = informational(arguments); status.has_value()) {
        return *status;
    }

    cli::Invocation invocation;
    try {
        invocation = cli::parse(medui::compilerToolName, argc, argv);
    } catch (const cli::UsageError& error) {
        // The shared text plus this tool's own two modes. `cli::usage()` is six other bakers' as
        // well, so `--grammar` and `--explain` are appended here rather than added there - and they
        // are appended rather than left undocumented, because a flag an agent cannot discover from
        // `--help` is a flag that does not exist for the reader it was built for.
        std::println(std::cerr, "{}", error.what());
        std::println(std::cerr,
                     "\n{0} also answers two questions about the language itself:\n"
                     "  {0} --grammar              the .medui contract as canonical JSON\n"
                     "  {0} --explain <MEDUI-EXXX> what one diagnostic code means, and how to fix it",
                     medui::compilerToolName);
        return 2;
    }

    std::vector<cli::Diagnostic> diagnostics;

    const std::string& recipePath = invocation.mode == cli::Mode::Bake ? invocation.bake.recipe : invocation.verify.recipe;

    const std::string summary = compile(invocation, recipePath, diagnostics);

    // Both formats go to stdout, matching every other MduX tool: an agent should not have to know
    // which stream each one uses, which is what the shared envelope exists to remove.
    const std::string rendered = cli::render(diagnostics, invocation.format, medui::compilerToolName);
    if (!rendered.empty()) {
        std::print(std::cout, "{}", rendered);
    }
    if (invocation.format == cli::Format::Text && !summary.empty()) {
        std::println(std::cout, "{}", summary);
    }

    return cli::exitStatus(diagnostics);
}
