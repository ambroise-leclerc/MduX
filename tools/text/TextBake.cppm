/**
 * @brief The text baker's recipe model and bake/verify core, separated from `main()`.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping (the baker is one of the host-side enforcement points)
 *
 * ## What S1 (#157) ships, and what it does not
 *
 * The skeleton lands the schema, the baker library, and the host-tools wiring so the trust-zone
 * check continues to pass and `ctest -L evidence` continues to travel clean. The baker's `run()`
 * is wired through the evidenced-shared code path: it parses a recipe, validates it against
 * `mdux.text.schema`, and produces a no-run package whose sidecar is zero bytes. The first real
 * artifact is committed by S4 (#160), once the TrueType parser (S2 / #158) and rasteriser (S3 /
 * #159) are available; until then a recipe with no `[runs]` table is the only shape `run()`
 * produces, and that is by design - baking an artifact the parser pipeline cannot yet provide
 * would be premature.
 *
 * ## One code path for bake and verify
 *
 * `run()` produces the whole artifact set in memory - `package.json`, `report.json` and the binary
 * sidecar - and only then does the caller either write those bytes or compare them. ADR-007 asks
 * for this explicitly, and the reason is worth restating: if verify were a second implementation,
 * a CI check comparing a baker against a different baker would prove nothing about either.
 *
 * ## A recipe
 *
 * ```toml
 * [package]
 * id      = "label-welcome"
 * atlas   = "roboto-ui"     # references a font package id produced by S4 (#160)
 * locale  = "en-US"
 * sidecar = "runs.bin"
 * ```
 *
 * No `[runs]` table at S1: positioned glyph runs are produced by the `.medui` compiler (#15),
 * not by this baker. S4 (#160) extends the recipe with the font pipeline's inputs; S5 (#161)
 * adds the restricted-charset validation that turns "no shaping on device" into a build-time
 * error. Two parallel arrays rather than `[[runs]]` array-of-tables TOML would normally use:
 * `mdux.tools.toml` implements a deliberate subset with no arrays of tables - see
 * `tools/common/Toml.cppm:13`, where it is listed as unsupported on purpose.
 */
module;

export module mdux.tools.textbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.toml;

export namespace mdux::tools::textbake {

/// The tool name that appears in every diagnostic and in `report.json`.
inline constexpr std::string_view toolName = "mdux-textbake";

/// A parsed and resolved recipe. Every default is expanded here rather than at the point of use,
/// so `report.json`'s `options` records what the bake actually did - ADR-007's rule that a
/// silently changed default must not leave every report looking unchanged.
struct Recipe {
    std::string id;
    std::string atlas;
    std::string locale;
    std::string sidecar{"runs.bin"};

    /// The fully resolved options, as they are recorded in the report.
    [[nodiscard]] evidence::json::Value toOptions() const;
};

/**
 * @brief Everything a bake produces, held in memory so bake and verify can share one code path.
 *
 * Deliberately just bytes plus two summary fields, rather than also carrying the `text::TextPackage`
 * the JSON was rendered from. A caller that wants the structured package calls
 * `text::TextPackage::parse(packageJson)`, which is strictly better for a test: it exercises the
 * reader on the writer's own output instead of inspecting a value that never made the round trip.
 *
 * See `tools/shader/ShaderBake.cppm` for the GCC 15 / `std::optional<T>` note that applies to any
 * baker carrying a non-trivially-destructible type across a module boundary; this struct avoids
 * the issue by holding bytes and POD-like summaries only.
 */
struct BakeOutputs {
    std::string packageJson;         ///< canonical `package.json` text
    std::string reportJson;          ///< canonical `report.json` text
    std::vector<std::byte> sidecar;  ///< at S1: empty; S4 fills it from the rasteriser
    std::string sidecarName;         ///< the sidecar's bare filename
    std::string packageId;           ///< for the summary line; the package itself is in the JSON
    std::size_t runCount{0};
};

/// Reads a file as bytes. Returns nullopt when it cannot be opened or read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended to `diagnostics`; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text,
                                                std::string_view recipePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Produces every output byte for `recipe`.
 *
 * At S1: produces a no-run package whose sidecar is zero bytes. The first real baked artifact
 * lands in S4 (#160).
 *
 * @param recipe      the resolved recipe
 * @param recipePath  repository-relative, for `report.json`'s recipe record and for diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory any future source paths resolve against - the repository root
 * @param diagnostics appended to on any problem
 *
 * Returns nullopt when the recipe fails to build a package that passes its own `validate()`.
 */
[[nodiscard]] std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                                              std::span<const std::byte> recipeBytes,
                                              const std::filesystem::path& root,
                                              std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir`, creating it if needed. Appends a diagnostic and returns
/// false on any write failure.
[[nodiscard]] bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
                          std::vector<cli::Diagnostic>& diagnostics);

/// Compares `outputs` against the committed files, appending a diagnostic per mismatch.
[[nodiscard]] bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
                          const std::filesystem::path& reportPath,
                          std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::textbake
