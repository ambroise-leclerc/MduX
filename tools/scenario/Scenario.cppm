/**
 * @file Scenario.cppm
 * @brief The scenario baker's recipe model and bake/verify core, separated from `main()`.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * ## One code path for bake and verify
 *
 * `run()` produces `scenario.json` and `report.json` in memory; the caller then writes them
 * (`bake`) or compares them (`verify`). ADR-007's rule: a second implementation for verify could
 * drift from bake.
 *
 * ## A recipe
 *
 * ```toml
 * [package]
 * id     = "endoscope-monitor-basics"
 * source = "recipes/scenario/endoscope-monitor-basics/basics.scenario"
 * screen = "endoscope-monitor"
 * ```
 *
 * `screen` names the committed screen the scenario is scripted against; `run()` reads
 * `generated/screen/<screen>/package.json` to resolve `pointer <node>` directives and to reject a
 * directive naming a node the screen does not have.
 */
module;

export module mdux.tools.scenario;

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.medui.scenario;
import mdux.tools.cli;
import mdux.tools.scenario.script;

export namespace mdux::tools::scenario {

/// The tool name in every diagnostic and in `report.json`.
inline constexpr std::string_view toolName = "mdux-scenariobake";

/// A parsed recipe. Paths are repository-relative (the baker runs with the repo root as its
/// working directory), so `report.json` stays free of absolute paths.
struct Recipe {
    std::string id;      ///< the artifact slug: `generated/scenario/<id>/`
    std::string source;  ///< the `.scenario` file
    std::string screen;  ///< the committed screen id the script is scripted against
};

/// Everything a bake produces, held in memory so bake and verify share one code path.
struct BakeOutputs {
    std::string scenarioJson;  ///< canonical `scenario.json`
    std::string reportJson;    ///< canonical `report.json`
    std::string scenarioId;    ///< for the summary line
    std::size_t stepCount{0};
    std::size_t captureCount{0};
};

/// Reads a file as bytes; nullopt when it cannot be opened.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Compiles the `.scenario` the recipe names into `scenario.json` + `report.json`.
 *
 * @param recipe      the resolved recipe
 * @param recipePath  repository-relative, for `report.json`'s recipe record and diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory the recipe's paths resolve against - the repository root
 * @param diagnostics appended to; a stage that reports anything stops the bake
 *
 * Returns nullopt when the recipe, the script or the referenced screen cannot be read or is
 * refused.
 */
[[nodiscard]] std::optional<BakeOutputs> run(const Recipe&                 recipe,
                                             std::string_view              recipePath,
                                             std::span<const std::byte>    recipeBytes,
                                             const std::filesystem::path&  root,
                                             std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir`, creating it if needed. Both files, always.
[[nodiscard]] bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
                         std::vector<cli::Diagnostic>& diagnostics);

/// Compares `outputs` against the committed files, one diagnostic per mismatch.
[[nodiscard]] bool verify(const BakeOutputs& outputs, const std::filesystem::path& scenarioPath,
                          const std::filesystem::path& reportPath, std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief The screen's nodes as `scenario.json` emission needs them, read from a committed screen
 *        `package.json`. Exported so `mdux-scenarioemit` reuses one reader.
 */
[[nodiscard]] std::optional<std::vector<ScreenNode>> readScreenNodes(const std::filesystem::path& packagePath,
                                                                     std::vector<cli::Diagnostic>& diagnostics);

/// Builds the canonical `scenario.json` from a validated script. Exported for the emitter and the
/// round-trip test.
[[nodiscard]] mdux::core::Result<std::string, mdux::evidence::json::Error> renderScenarioJson(const Script& script);

/// Reads a committed `scenario.json` back into a `Script` (the owned form). The emitter renders
/// from the committed bytes, exactly as `mdux-screenemit` renders from `package.json`; nothing
/// re-runs the `.scenario` parser. `std::nullopt` with a `SCN0NN` diagnostic on a malformed file.
[[nodiscard]] std::optional<Script> readScenarioDoc(std::span<const std::byte> scenarioJson,
                                                    std::string_view scenarioPath,
                                                    std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::scenario
