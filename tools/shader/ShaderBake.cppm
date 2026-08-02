/**
 * @file ShaderBake.cppm
 * @brief The shader baker's recipe model and bake/verify core, separated from `main()`.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## One code path for bake and verify
 *
 * `run()` produces the whole artifact set in memory - `package.json`, `report.json` and the binary
 * sidecar - and only then does the caller either write those bytes or compare them. ADR-007 asks
 * for this explicitly, and the reason is worth restating: if verify were a second implementation,
 * a CI check comparing a baker against a different baker would prove nothing about either. Here
 * the two modes differ in what they do with `BakeOutputs`, and in nothing else.
 *
 * ## A recipe
 *
 * ```toml
 * [package]
 * id      = "mdux-ui"
 * sidecar = "shaders.spv"
 *
 * [modules]
 * ids     = ["ui.vert", "ui.frag"]
 * sources = ["generated/shader/mdux-ui/ui.vert.spv", "generated/shader/mdux-ui/ui.frag.spv"]
 * ```
 *
 * Two parallel arrays rather than the `[[module]]` array-of-tables TOML would normally use:
 * `mdux.tools.toml` implements a deliberate subset with no arrays of tables, and widening the
 * shared parser to suit one baker would be the wrong trade. `parseRecipe()` requires the arrays to
 * be the same length and says so by name when they are not.
 *
 * Modules are recorded in recipe order and the sidecar concatenates them in that order, so the
 * committed artifact is a function of the recipe rather than of directory iteration order - which
 * differs between filesystems and would break byte-identity between two developers.
 */
module;

export module mdux.tools.shaderbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.shader.schema;
import mdux.tools.cli;

export namespace mdux::tools::shaderbake {

/// The tool name that appears in every diagnostic and in `report.json`.
inline constexpr std::string_view kToolName = "mdux-shaderbake";

/// One `[[module]]` entry, in recipe order.
struct RecipeModule {
    std::string id;
    std::string source;  ///< repository-relative path to the compiled SPIR-V
};

/// A parsed and resolved recipe. Every default is expanded here rather than at the point of use,
/// so `report.json`'s `options` records what the bake actually did - ADR-007's rule that a
/// silently changed default must not leave every report looking unchanged.
struct Recipe {
    std::string id;
    std::string sidecar{"shaders.spv"};
    std::vector<RecipeModule> modules;

    /// The fully resolved options, as they are recorded in the report.
    [[nodiscard]] evidence::json::Value toOptions() const;
};

/**
 * @brief Everything a bake produces, held in memory so bake and verify can share one code path.
 *
 * Deliberately just bytes plus two summary fields, rather than also carrying the
 * `shader::ShaderPackage` the JSON was rendered from. A caller that wants the structured package
 * calls `shader::ShaderPackage::parse(packageJson)`, which is strictly better for a test: it
 * exercises the reader on the writer's own output instead of inspecting a value that never made
 * the round trip.
 *
 * There is a second, duller reason. GCC 15 rejects a struct exported from this module that embeds
 * `ShaderPackage` - a plain translation unit importing it gets "use of deleted function
 * ~ShaderPackage()" because the implicit destructor's exception specification is computed
 * inconsistently across the module boundary. `ShaderPackage` itself is fine in a plain TU that
 * imports mdux.shader.schema directly, so this is specifically about re-exporting a governed type
 * embedded in a host-tools one. Worth knowing before #121 and #124 design their own interfaces.
 */
struct BakeOutputs {
    std::string packageJson;         ///< canonical `package.json` text
    std::string reportJson;          ///< canonical `report.json` text
    std::vector<std::byte> sidecar;  ///< the concatenated SPIR-V
    std::string sidecarName;         ///< the sidecar's bare filename
    std::string packageId;           ///< for the summary line; the package itself is in the JSON
    std::size_t moduleCount{0};
};

/// Reads a file as bytes. Returns nullopt when it cannot be opened or read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended to `diagnostics`; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text,
                                                std::string_view recipePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Produces every output byte for `recipe`, reflecting each module's SPIR-V.
 *
 * @param recipe      the resolved recipe
 * @param recipePath  repository-relative, for `report.json`'s recipe record and for diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory module `source` paths resolve against - the repository root
 * @param diagnostics appended to on any problem
 *
 * Returns nullopt when any module fails to read, fails to reflect, or the assembled package fails
 * its own validate().
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

}  // namespace mdux::tools::shaderbake
