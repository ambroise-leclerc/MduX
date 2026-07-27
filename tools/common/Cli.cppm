/**
 * @file Cli.cppm
 * @brief Shared argument parsing and the diagnostic envelope every baker presents.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: never linked into MduXCore or MduX)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## One interface for six bakers
 *
 * ```
 * mdux-<kind>bake bake   <recipe> <output-dir>
 * mdux-<kind>bake verify <recipe> <package.json> <report.json>
 *                        [--format=json|text]
 * ```
 *
 * `bake` and `verify` must run the *same* code path with different output handling. If verify
 * were a separate reimplementation it could drift from bake, and a CI check that compares a
 * baker against a different baker proves nothing. Baker authors: produce the artifact in memory
 * once, then either write it (bake) or compare it (verify).
 *
 * ## The diagnostic envelope
 *
 * `--format=json` emits `{file, line, code, severity, message, fixHint}` per finding - the first
 * instance of the stable envelope issue #19 (S3) extends to every tool. It is deliberately the
 * same shape `mdux-docs-lint` and `mdux-evidence-lint` already emit, so an agent parses one
 * schema for the whole repository rather than one per tool.
 *
 * Defining it here, once, is the point: a baker gets the envelope by using this module and
 * cannot accidentally invent its own.
 */
module;

export module mdux.tools.cli;

import std;

export namespace mdux::tools::cli {

/// A usage error: bad arguments, not a bad recipe. Rendered as usage text, not as a diagnostic.
class UsageError : public std::runtime_error {
public:
    explicit UsageError(std::string message) : std::runtime_error{std::move(message)} {}
};

enum class Mode : std::uint8_t {
    Bake,   ///< produce the artifact and write it into <output-dir>
    Verify, ///< produce the artifact and compare it against committed files, writing nothing
};

enum class Format : std::uint8_t { Text, Json };

enum class Severity : std::uint8_t { Error, Warning, Note };

[[nodiscard]] std::string_view describe(Severity severity) noexcept;

/**
 * @brief One finding, in the envelope every MduX tool shares.
 *
 * `line` is 1-based; 0 means "the finding is about the file as a whole". `code` is a short stable
 * identifier a tool can be grepped for, and must not change meaning once published - an agent
 * keying off it should not be broken by a message reword.
 */
struct Diagnostic {
    std::string file;
    std::size_t line{0};
    std::string code;
    Severity severity{Severity::Error};
    std::string message;
    std::string fixHint;
};

struct BakeArguments {
    std::string recipe;
    std::string outputDir;
};

struct VerifyArguments {
    std::string recipe;
    std::string packagePath;
    std::string reportPath;
};

/// A parsed command line. Exactly one of `bake`/`verify` is meaningful, per `mode`.
struct Invocation {
    Mode mode{Mode::Bake};
    Format format{Format::Text};
    BakeArguments bake;
    VerifyArguments verify;
};

/// Usage text for `toolName`, as printed on a usage error or `--help`.
[[nodiscard]] std::string usage(std::string_view toolName);

/**
 * @brief Parses `arguments` (the argv tail, *excluding* argv[0]).
 *
 * Throws UsageError with an actionable message on an unrecognized subcommand, a wrong argument
 * count, or an unknown option. `--help` also throws, carrying the usage text, so a caller has
 * one place to handle "did not run".
 */
[[nodiscard]] Invocation parse(std::string_view toolName,
                                std::span<const std::string_view> arguments);

/// Convenience wrapper for a `main(argc, argv)` tail.
[[nodiscard]] Invocation parse(std::string_view toolName, int argc, const char* const* argv);

/// Renders `diagnostics` in the requested format. `toolName` appears in the JSON envelope so a
/// consumer aggregating several tools' output can tell them apart.
[[nodiscard]] std::string render(std::span<const Diagnostic> diagnostics, Format format,
                                 std::string_view toolName);

/// The exit status a tool should return: 0 when no diagnostic is an error, 1 otherwise. A
/// warning alone does not fail a bake - only CI's byte-comparison decides that.
[[nodiscard]] int exitStatus(std::span<const Diagnostic> diagnostics) noexcept;

}  // namespace mdux::tools::cli
