/**
 * @file MlBake.cppm
 * @brief `mdux-mlbake`'s library half: recipe to committed model package.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Follows ADR-007's baker contract exactly, and the same `run()` / `write()` / `verify()` shape as
 * `mdux-shaderbake`, so the two are one pattern rather than two:
 *
 * ```sh
 * mdux-mlbake bake   recipes/model/ecg-demo.toml generated/model/ecg-demo/
 * mdux-mlbake verify recipes/model/ecg-demo.toml generated/model/ecg-demo/package.json \
 *                                                generated/model/ecg-demo/report.json
 * ```
 *
 * `run()` produces every output byte in memory; `write()` and `verify()` then either write them or
 * compare them. Nothing here writes into the source tree - `cmake --build . --target
 * mdux-bake-update` is the only path that stages an artifact into `generated/`.
 *
 * ## Outputs
 *
 * - `package.json` - layers, golden vectors as `u32` bit patterns, `weightsDigest`,
 *   `maxScratchFloats`
 * - `weights.bin` - the packed f32 blob, the sidecar the package's digest covers
 * - `report.json` - the shared `BakeReport`: semantic `toolVersion`, resolved options,
 *   repository-relative paths, and no commit SHA (ADR-007, decision 5)
 *
 * ## The blob is repacked, not copied
 *
 * `weights.bin` is not the safetensors file with its header removed. Tensors are repacked in layer
 * order by `resolveArchitecture()`, so the blob's layout is determined by the recipe rather than by
 * whichever order the exporter happened to write. Two exporters emitting the same tensors in
 * different orders therefore produce the same `weights.bin`.
 */
module;

export module mdux.tools.ml.mlbake;

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.ml.safetensors;
import mdux.tools.ml.archvalidate;
import mdux.tools.ml.goldengen;

export namespace mdux::tools::ml {

/// The name this tool reports itself as, in usage text and in `report.json`'s `tool` field.
inline constexpr std::string_view bakeToolName = "mdux-mlbake";

/// A parsed `recipes/model/<id>.toml`.
struct Recipe {
    std::string id;
    std::string weightsSource;  ///< repository-relative path to the safetensors file
    std::uint32_t inputLength{0};
    std::uint32_t outputLength{0};
    std::uint32_t maxScratchFloats{0};  ///< 0 means "derive from the layer chain"
    std::size_t goldenCount{0};
    std::uint32_t goldenSeed{0};
    std::vector<LayerSpec> layers;

    /// The fully resolved option set for `report.json`. Defaults are expanded here, not recorded
    /// as the recipe literally wrote them - ADR-007 is explicit that recording only what the
    /// recipe said lets a changed default alter every output while every report looks unchanged.
    [[nodiscard]] mdux::evidence::json::Value toOptions(std::uint32_t resolvedScratch) const;
};

/// Everything a bake produces, in memory.
struct BakeOutputs {
    std::string packageJson;
    std::string reportJson;
    std::vector<std::byte> weights;
    std::string weightsName;  ///< always "weights.bin"; a field so callers do not restate it
    std::string packageId;
    std::size_t layerCount{0};
    std::size_t goldenCount{0};
};

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Imports the weights, validates the architecture, generates the goldens, renders the JSON.
 *
 * @param recipePath  repository-relative, for `report.json`'s recipe record and for diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory the recipe's `source` path resolves against - the repo root
 *
 * Returns nullopt when the weights fail to read or parse, the architecture disagrees with them,
 * golden generation fails, or the assembled package fails its own validate().
 */
[[nodiscard]] std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                                             std::span<const std::byte> recipeBytes,
                                             const std::filesystem::path& root,
                                             std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir`, creating it if needed.
[[nodiscard]] bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
                         std::vector<cli::Diagnostic>& diagnostics);

/// Compares `outputs` against the committed files, appending a diagnostic per mismatch. The
/// sidecar is compared too, resolved beside `packagePath`.
[[nodiscard]] bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
                          const std::filesystem::path& reportPath,
                          std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::ml
