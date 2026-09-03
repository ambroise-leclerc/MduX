/**
 * @file Emit.cppm
 * @brief Turns a committed model package into C++ a device can hold as `constexpr` data.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The canonical artifact remains `generated/model/<id>/package.json`; the generated source lives
 * only in the build tree. Weights deliberately remain outside it and are supplied as a separate
 * blob, so emitting a package never turns a multi-megabyte sidecar into a `constexpr` array.
 *
 * Both outputs share one rendering: a module interface for importing consumers and a header for
 * translation units that cannot import a generated module. Each carries a compile-time
 * `ModelPackage::validate()` assertion, making a malformed rendering a consumer build failure.
 *
 * The tables are namespace-scope C arrays rather than `std::array`. That follows the shader and
 * screen emitters: large `std::array` initialisers in module interfaces have triggered GCC 16
 * module ICEs, while both forms convert to the `std::span` members `ModelPackage` already exposes.
 */
module;

export module mdux.tools.ml.emit;

import std;
import mdux.tools.cli;

export namespace mdux::tools::ml {

inline constexpr std::string_view emitToolName = "mdux-mlemit";

/// The generated sources, held together so the two forms derive from one rendered body.
struct EmitOutputs {
    std::string moduleName;    ///< e.g. `mdux.ml.generated.model_ecg_demo`
    std::string moduleSource;  ///< the `.cppm` text
    std::string headerSource;  ///< the `.hpp` text
    std::string stem;          ///< filename stem for both, e.g. `model_ecg_demo`
};

/**
 * @brief Reads a committed model package and renders both generated forms.
 *
 * The weights sidecar is intentionally neither read nor rendered. Its length and digest remain in
 * the package, while the caller supplies the actual bytes to `Classifier1D::create()`.
 */
[[nodiscard]] std::optional<EmitOutputs> renderModel(const std::filesystem::path& packagePath, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

/// @brief Writes `<stem>.cppm` and `<stem>.hpp`, without restamping unchanged files.
[[nodiscard]] bool writeModel(const EmitOutputs& outputs, const std::filesystem::path& outputDir, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

/**
 * @brief Maps a package id to its generated C++ stem.
 *
 * The unconditional `model_` prefix keeps keyword ids valid. The matching CMake implementation is
 * `mdux_model_identifier()` in `cmake/MduXModelEmit.cmake`.
 */
[[nodiscard]] std::string identifierForModel(std::string_view packageId);

}  // namespace mdux::tools::ml
