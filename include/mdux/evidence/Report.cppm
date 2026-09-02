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
 * An artifact produced by more than one tool carries one further member, `stages`, naming the tool
 * and version behind each output the registered `tool` did not write. See `ToolStage` and ADR-007
 * decision 4b; it is absent from a single-tool report.
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
    MalformedReport,       ///< parsed JSON did not have the expected shape
    UnknownStageOutput,    ///< a stage claims an output the report does not list
    DuplicateStageOutput,  ///< two stages claim to have produced the same file
};

[[nodiscard]] std::string_view describe(ReportError error) noexcept;

/// A file the baker read or wrote, identified by a repository-relative path and its digest.
struct FileRecord {
    std::string path;
    Digest      sha256{};
};

/**
 * @brief The header every `package.json` carries, so a reader can identify an artifact before
 *        it knows how to interpret the rest.
 */
struct PackageHeader {
    std::uint64_t schemaVersion{kSchemaVersion};
    std::string   id;    ///< the `<id>` in `generated/<kind>/<id>/`, e.g. "roboto-ui"
    std::string   kind;  ///< the `<kind>`, e.g. "font", "shader", "model"

    [[nodiscard]] mdux::core::ResultVoid<ReportError> validate() const noexcept;

    /// Serializes to canonical JSON members. Bakers merge these into their own package object
    /// rather than nesting them, so a package's header fields sit at its top level.
    [[nodiscard]] mdux::core::ResultVoid<json::Error> writeInto(json::Value& object) const noexcept;

    [[nodiscard]] static mdux::core::Result<PackageHeader, ReportError> readFrom(const json::Value& object) noexcept;
};

/**
 * @brief Which tool produced one output, when more than one tool produced the artifact.
 *
 * Most artifacts are baked by one tool, and for those there are no stage records and the member is
 * absent from the file. A screen is not: `mdux-meduic` compiles it and `mdux-verify-bake` then
 * renders it and writes `verification.json` (ADR-014 decision 4). One artifact, one registration,
 * one report - so the report is where the chain has to be legible.
 *
 * Without this, a reader holding the screen's report sees `tool: "mdux-meduic"` and no mention of
 * the second executable, and attributes every output to the compiler. That is not a cosmetic
 * imprecision: ADR-007 exists to answer "which tool, at which version, from which input, produced
 * this artifact", and for one of the four outputs the unextended report answers it wrongly. A
 * digest says what was produced and a resolved option set says how it was configured; neither says
 * by whom.
 *
 * One record per output rather than one per tool with a list of outputs. It reads the same way
 * `outputs` does - a flat row per file - and it sidesteps a GCC 16 defect that corrupts this
 * module's BMI when an exported struct carries a `std::vector<std::string>`; `mdux-textbake` then
 * fails to read the module with "Bad file data". Verified by isolation: adding an otherwise unused
 * struct with such a member to this file reproduces it, and this shape does not.
 */
struct ToolStage {
    std::string tool;         ///< e.g. "mdux-verify-bake"
    std::string toolVersion;  ///< the project version that tool was built from
    std::string output;       ///< the output path it wrote, which `outputs` must also list
};

/**
 * @brief The audit record a baker writes alongside every artifact.
 */
struct BakeReport {
    std::uint64_t           schemaVersion{kSchemaVersion};
    std::string             tool;         ///< e.g. "mdux-fontbake"; the tool the bake is registered to
    std::string             toolVersion;  ///< the project version the tool was built from
    FileRecord              recipe;
    std::vector<FileRecord> inputs;
    json::Value             options;  ///< fully resolved, defaults expanded - see the module comment
    std::vector<FileRecord> outputs;

    /// Further tools in the production sequence, one record per output they wrote. Empty for a
    /// single-tool bake, and omitted from the file when empty - which is what let this member be
    /// added without re-baking six artifacts that have nothing to say about it.
    std::vector<ToolStage> stages;

    /// Checks the invariants that would otherwise break byte-identity or auditability.
    [[nodiscard]] mdux::core::ResultVoid<ReportError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<json::Value, ReportError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, ReportError> write() const noexcept;

    /// Parses canonical `report.json` text. Strict: rejects a malformed shape and an
    /// unsupported schemaVersion, and validates the result.
    [[nodiscard]] static mdux::core::Result<BakeReport, ReportError> parse(std::string_view text) noexcept;
};

/// Lowercase-hex helper shared by report readers: parses 64 hex digits into a Digest.
[[nodiscard]] mdux::core::Result<Digest, ReportError> digestFromHex(std::string_view hex) noexcept;

}  // namespace mdux::evidence
