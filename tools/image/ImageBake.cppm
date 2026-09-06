/**
 * @file ImageBake.cppm
 * @brief Host-only QOI image baker and deterministic artifact writer.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 */
module;

export module mdux.tools.imagebake;

import std;
import mdux.evidence.json;
import mdux.tools.cli;

export namespace mdux::tools::imagebake {

inline constexpr std::string_view toolName = "mdux-imagebake";

struct Recipe {
    std::string id;
    std::string source;
    std::string sidecar{"pixels.rgba"};

    [[nodiscard]] mdux::evidence::json::Value toOptions() const;
};

struct BakeOutputs {
    std::string            packageJson;
    std::string            reportJson;
    std::vector<std::byte> sidecar;
    std::string            sidecarName;
    std::string            packageId;
    std::uint32_t          width{0};
    std::uint32_t          height{0};
};

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);
[[nodiscard]] std::optional<BakeOutputs> run(const Recipe&                              recipe,
                                             std::string_view                           recipePath,
                                             std::span<const std::byte>                 recipeBytes,
                                             const std::filesystem::path&               root,
                                             std::vector<mdux::tools::cli::Diagnostic>& diagnostics);
[[nodiscard]] bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);
[[nodiscard]] bool verify(const BakeOutputs&                         outputs,
                          const std::filesystem::path&               packagePath,
                          const std::filesystem::path&               reportPath,
                          std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::imagebake
