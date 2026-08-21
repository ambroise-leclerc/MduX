/**
 * @file Emit.cppm
 * @brief Turns a committed screen package into C++ a device can hold as `constexpr` data.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * This is what lets a device build link no host-tools module and run no parser at startup. The
 * screen it draws is namespace-scope `constexpr` data in read-only memory, reached by name.
 *
 * ## Generated code lives in the build tree, never in git
 *
 * The canonical artifact is `generated/screen/<id>/{package.json, goldens.json, report.json}` -
 * reviewed as JSON, byte-compared in CI. The C++ here is a mechanical rendering of exactly those
 * bytes, regenerated on every build, and is not committed. `mdux_emit_shader_package()` established
 * both the arrangement and the argument for it: committing the rendering would put two
 * representations of one artifact under review, and the second is the one nobody reads.
 *
 * ## The static_assert is the point, not a decoration
 *
 * The generated source carries `static_assert(screen.validate().has_value())`. A screen that does
 * not satisfy its schema is therefore a **build failure in the consumer**, not a startup failure on
 * a device - and not a diagnostic from a tool somebody might not have run. That is only possible
 * because `mdux.medui.schema` is header-only and fully `constexpr`, which is the property #197
 * delivered rather than one that already existed.
 *
 * It is also a genuine second gate rather than a restatement of the first: `readPackage()` validates
 * what it read, so this is the *compiler* re-checking the *emitted* form. If the rendering below
 * ever dropped a required name or mis-spelled a colour token, the emitted screen would stop
 * validating and the build would say so at the file that carries the mistake.
 *
 * ## C arrays, not std::array
 *
 * Generated data is emitted as namespace-scope C arrays, exactly as the shader emitter does: a
 * `std::array` of any size in a module interface has twice triggered the GCC 15/16 modules ICE this
 * repository works around elsewhere. `std::span` conversion from a C array is what `ScreenPackage`
 * takes anyway, so nothing is lost.
 *
 * ## Two outputs describing one screen
 *
 * A module interface for consumers that `import`, and a header for consumers that cannot - the
 * examples include Vulkan and GLFW headers before importing anything, precisely because mixing the
 * two orders has been a source of GCC ICEs. Both come from one in-memory rendering, so they cannot
 * disagree; `renderBody()` is shared for that reason rather than each output formatting itself.
 */
module;

export module mdux.tools.medui.emit;

import std;
import mdux.tools.cli;

export namespace mdux::tools::medui {

inline constexpr std::string_view emitToolName = "mdux-screenemit";

/// The generated sources, held in memory so both outputs derive from one rendering.
struct EmitOutputs {
    std::string moduleName;    ///< e.g. `mdux.medui.generated.neurosense_500`
    std::string moduleSource;  ///< the .cppm text
    std::string headerSource;  ///< the .hpp text
    std::string stem;          ///< filename stem for both, e.g. `neurosense_500`
};

/**
 * @brief Reads a committed screen package and renders both outputs.
 *
 * @param packagePath repository-relative path to `package.json`
 * @param diagnostics appended to on any problem, including the reader's own `SCP0NN` codes
 *
 * Returns nullopt when the package cannot be read or does not describe a valid screen. The goldens
 * sidecar is deliberately not read: the runtime never sees a golden reference, so nothing about it
 * belongs in generated device code (ADR-012, decision 4).
 */
[[nodiscard]] std::optional<EmitOutputs> renderScreen(const std::filesystem::path& packagePath, std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir` as `<stem>.cppm` and `<stem>.hpp`, creating it if needed.
///
/// Writes only when the content differs from what is already there, so a rebuild that changes
/// nothing does not restamp the files and force every consumer to recompile.
[[nodiscard]] bool writeScreen(const EmitOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief The C++ identifier a package id maps to: `neurosense-500` becomes `screen_neurosense_500`.
 *
 * Prefixed unconditionally, because a package slug may be a keyword - `class` and `module` are both
 * legal ids - and an identifier-shaped answer that no compiler accepts is worse than a longer one.
 *
 * Exported because the tests and the CMake integration both have to predict the generated
 * filenames. `mdux_screen_identifier()` in `cmake/MduXScreenEmit.cmake` implements the same rule,
 * and `medui-screen-identifier-parity` runs both over the same ids and compares - an assertion on
 * this half alone cannot observe a CMake regex, which is exactly how the shader pair drifted.
 */
[[nodiscard]] std::string identifierForScreen(std::string_view packageId);

}  // namespace mdux::tools::medui
