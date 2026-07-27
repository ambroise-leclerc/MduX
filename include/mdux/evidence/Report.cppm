/**
 * @file Report.cppm
 * @brief Governed-zone bake-report types: the shared shape of every `report.json`.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Part of MduXCore. Every baker - shaders (issue #13), fonts (#14), `.medui` screens (#15),
 * images (#17), ML models (#18) - emits a report through these types, so an auditor learns one
 * record shape rather than six.
 *
 * A report looks like this:
 *
 * ```json
 * {
 *   "schemaVersion": 1,
 *   "tool": "mdux-fontbake",
 *   "toolVersion": "0.2.0",
 *   "recipe":  { "path": "recipes/font/roboto-ui.toml", "sha256": "…" },
 *   "inputs":  [ { "path": "assets/fonts/Roboto-Regular.ttf", "sha256": "…" } ],
 *   "options": { "…fully resolved, defaults expanded…" },
 *   "outputs": [ { "path": "package.json", "sha256": "…" },
 *                { "path": "atlas.bin",    "sha256": "…" } ]
 * }
 * ```
 *
 * ## Two rules that are easy to get wrong
 *
 * **`options` is the fully resolved set, with defaults expanded** - not the recipe's literal
 * contents. Recording only what the recipe said means changing a default silently changes every
 * output while every report still looks unchanged, which is exactly the failure a byte-verified
 * pipeline exists to prevent. validate() cannot detect this for you; it is a discipline each
 * baker owes its own options struct.
 *
 * **Paths are repository-relative, with `/` separators.** An absolute path embeds the machine
 * that produced the artifact and breaks byte-identity between two developers immediately; a
 * backslash breaks it between Windows and Linux. validate() rejects both.
 *
 * ## No commit SHA anywhere in here, on purpose
 *
 * There is deliberately no `toolGitSha` or equivalent field. Baking happens at commit H0;
 * embedding H0 into `report.json` and then committing that file produces a *different* commit H1
 * (its tree now contains a file H0's didn't). CI re-bakes at H1 and gets H1, not H0 - the
 * byte-comparison this whole pipeline exists to run would then fail on every report, always,
 * regardless of whether the artifact itself changed. No commit's tree can correctly name its own
 * hash, because the hash is computed from the already-final tree. `toolVersion` (a deliberately,
 * manually bumped semantic version) is the only "which tooling produced this" identity here, and
 * it has no such problem. See ADR-007, decision 5, for the full writeup - including why this was
 * tried first and reverted, so it doesn't get re-proposed unread.
 */
module;

export module mdux.evidence.report;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;

export namespace mdux::evidence {

/// The schema version every package and report currently carries. Bump when a field's meaning
/// changes, not when one is added - a reader that ignores unknown members tolerates additions.
inline constexpr std::uint64_t kSchemaVersion = 1;

enum class ReportError : std::uint8_t {
    EmptyToolName,
    EmptyToolVersion,
    EmptyPath,
    AbsolutePath,
    BackslashInPath,
    ParentDirectoryInPath,
    EmptyId,
    EmptyKind,
    NoOutputs,
    DuplicateOutputPath,
    UnsupportedSchemaVersion,
    MalformedReport,        ///< parsed JSON did not have the expected shape
};

[[nodiscard]] std::string_view describe(ReportError error) noexcept;

/// A file the baker read or wrote, identified by a repository-relative path and its digest.
struct FileRecord {
    std::string path;
    Digest sha256{};
};

/**
 * @brief The header every `package.json` carries, so a reader can identify an artifact before
 *        it knows how to interpret the rest.
 */
struct PackageHeader {
    std::uint64_t schemaVersion{kSchemaVersion};
    std::string id;    ///< the `<id>` in `generated/<kind>/<id>/`, e.g. "roboto-ui"
    std::string kind;  ///< the `<kind>`, e.g. "font", "shader", "model"

    [[nodiscard]] mdux::core::ResultVoid<ReportError> validate() const noexcept;

    /// Serializes to canonical JSON members. Bakers merge these into their own package object
    /// rather than nesting them, so a package's header fields sit at its top level.
    [[nodiscard]] mdux::core::ResultVoid<json::Error> writeInto(json::Value& object) const noexcept;

    [[nodiscard]] static mdux::core::Result<PackageHeader, ReportError> readFrom(
        const json::Value& object) noexcept;
};

/**
 * @brief The audit record a baker writes alongside every artifact.
 */
struct BakeReport {
    std::uint64_t schemaVersion{kSchemaVersion};
    std::string tool;         ///< e.g. "mdux-fontbake"
    std::string toolVersion;  ///< the project version the tool was built from
    FileRecord recipe;
    std::vector<FileRecord> inputs;
    json::Value options;  ///< fully resolved, defaults expanded - see the module comment
    std::vector<FileRecord> outputs;

    /// Checks the invariants that would otherwise break byte-identity or auditability.
    [[nodiscard]] mdux::core::ResultVoid<ReportError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<json::Value, ReportError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, ReportError> write() const noexcept;

    /// Parses canonical `report.json` text. Strict: rejects a malformed shape and an
    /// unsupported schemaVersion, and validates the result.
    [[nodiscard]] static mdux::core::Result<BakeReport, ReportError> parse(
        std::string_view text) noexcept;
};

/// Lowercase-hex helper shared by report readers: parses 64 hex digits into a Digest.
[[nodiscard]] mdux::core::Result<Digest, ReportError> digestFromHex(std::string_view hex) noexcept;

}  // namespace mdux::evidence
