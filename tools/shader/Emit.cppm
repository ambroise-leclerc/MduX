/**
 * @brief Turns a committed shader package into C++ the renderer can hold as `constexpr` data.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## Generated code lives in the build tree, never in git
 *
 * The canonical artifact is `generated/shader/<id>/{package.json, shaders.spv}` - reviewed as
 * JSON and digests, byte-compared in CI. The C++ here is a mechanical rendering of exactly those
 * bytes, regenerated on every build, and is not committed.
 *
 * Committing it would put two representations of one artifact under review, and the second one is
 * the one nobody reads: a reviewer cannot meaningfully check three thousand hex bytes, so a diff
 * in generated source is noise that hides signal. Reviewing the JSON and the digests, and deriving
 * the C++ mechanically, is the arrangement where the thing a human checks is the thing that
 * matters.
 *
 * ## C arrays, not std::array
 *
 * Generated data is emitted as namespace-scope C arrays with `std::span` accessors. `std::array`
 * would be the more idiomatic choice and is specifically ruled out: a `std::array` of a few
 * thousand elements in a module interface triggers the GCC 15/16 modules ICE this repository has
 * already hit twice (see `tests/CMakeLists.txt`'s `-O0` workaround and `Units.cppm`'s note on
 * friend-defaulted comparisons). C arrays are not a compromise here; they are the form that
 * compiles.
 *
 * ## Two outputs describing one thing
 *
 * A module interface for consumers that `import`, and a header for consumers that cannot - the
 * examples include Vulkan and GLFW headers before importing anything, precisely because mixing
 * the two orders has been a source of GCC ICEs. Both are generated from one in-memory rendering
 * of the payload, so they cannot disagree about the bytes; `emitModule()` and `emitHeader()` share
 * `renderPayload()` for that reason rather than each formatting the array themselves.
 */
module;

export module mdux.tools.shaderemit;

import std;
import mdux.shader.schema;
import mdux.tools.cli;

export namespace mdux::tools::shaderemit {

inline constexpr std::string_view toolName = "mdux-shaderemit";

/// What to generate, resolved from the command line.
struct EmitRequest {
    std::string packagePath;  ///< generated/shader/<id>/package.json
    std::string outputDir;    ///< a directory in the build tree
};

/// The generated sources, held in memory so both outputs derive from one rendering.
struct EmitOutputs {
    std::string moduleName;    ///< e.g. "mdux.shader.generated.mdux_ui"
    std::string moduleSource;  ///< the .cppm text
    std::string headerSource;  ///< the .hpp text
    std::string stem;          ///< filename stem for both, e.g. "mdux_ui"
};

/**
 * @brief Reads the package and its sidecar, and renders both outputs.
 *
 * @param packagePath repository-relative path to `package.json`; the sidecar is read from beside
 *                    it, under the name the package itself records
 * @param diagnostics appended to on any problem
 *
 * Returns nullopt when the package cannot be read, does not parse, or does not agree with the
 * sidecar sitting next to it.
 */
[[nodiscard]] std::optional<EmitOutputs> render(const std::filesystem::path& packagePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir` as `<stem>.cppm` and `<stem>.hpp`, creating it if needed.
///
/// Writes only when the content differs from what is already there, so a rebuild that changes
/// nothing does not restamp the files and force every consumer to recompile.
[[nodiscard]] bool write(const EmitOutputs& outputs, const std::filesystem::path& outputDir,
                         std::vector<cli::Diagnostic>& diagnostics);

/// The C++ identifier a package id maps to: `mdux-ui` becomes `mdux_ui`. Exported because the
/// tests and the CMake integration both need to predict the generated filenames.
[[nodiscard]] std::string identifierFor(std::string_view packageId);

}  // namespace mdux::tools::shaderemit
