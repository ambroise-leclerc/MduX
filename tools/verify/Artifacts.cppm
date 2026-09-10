/**
 * @file Artifacts.cppm
 * @brief Loading and authenticating the committed artifacts a rendered-truth verification reads.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * The screen driver (#253) and the scenario-capture driver (#321, ADR-021) verify against the
 * **same** committed shader, font, text and image packages, so they load them the same way: parse
 * the `package.json`, prove it is in canonical committed form, prove every declared digest and
 * length matches the sidecar on disk, and prove the identity the screen's approval manifest names.
 * A single implementation is what keeps the two gates from disagreeing about what "the committed
 * mdux-ui shader" or "the approved en-US text package" is.
 *
 * Every function returns owned assets - the bytes the verification actually parsed - and never a
 * filesystem path: ADR-007 decision 5 keeps a host-dependent path out of a byte-compared artifact,
 * so a caller records the digest of what it read, not where it read it from.
 */
module;

export module mdux.tools.verify.artifacts;

import std;
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.image.schema;
import mdux.medui.schema;
import mdux.shader.schema;
import mdux.text.schema;
import mdux.tools.cli;

export namespace mdux::tools::verify {

/// The whole file's bytes, or nothing when it cannot be opened or fully read.
[[nodiscard]] std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path);

/// The whole file's bytes as text, or nothing. An empty file is an empty string, not `nullopt`.
[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path);

/// The lowercase-hex SHA-256 of already-read artifact text, spelled the way every evidence record
/// spells a digest. Taken from the bytes the run parsed, not by re-reading the file.
[[nodiscard]] std::string hexDigest(std::string_view text);

/// The committed `mdux-ui` shader package, its SPIR-V sidecar and the module views a `PackageView`
/// needs.
struct ShaderAssets {
    mdux::shader::ShaderPackage           package;
    std::vector<std::byte>                sidecar;
    std::vector<mdux::shader::ModuleView> modules;
    std::string                           sha256;  ///< of package.json, for the evidence artifact

    [[nodiscard]] mdux::shader::PackageView view() const noexcept {
        return {.id = package.header.id, .spirv = sidecar, .modules = modules, .descriptors = package.descriptors, .pushConstants = package.pushConstants};
    }
};

/// One approved locale: its text package and runs, and the font package and coverage atlas the text
/// package names.
struct LocaleAssets {
    std::string             locale;
    std::string             textJson;
    mdux::text::TextPackage text;
    std::vector<std::byte>  runs;
    std::string             fontJson;
    mdux::font::FontPackage font;
    std::vector<std::byte>  atlas;
};

/// One approved image package and its RGBA pixels.
struct ImageAssets {
    std::string               imageJson;
    mdux::image::ImagePackage image;
    std::vector<std::byte>    pixels;
};

/**
 * @brief Loads and authenticates `<artifactRoot>/shader/mdux-ui/package.json` and its sidecar.
 *
 * Refuses an unreadable or non-canonical package, a sidecar whose length or digest disagrees with
 * the package, and a module byte range that does not fit this host. Diagnostics carry `VUI005`.
 */
[[nodiscard]] std::optional<ShaderAssets> loadShader(const std::filesystem::path& artifactRoot, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

/**
 * @brief Loads and authenticates the text package `approval` names, plus its runs, font package and
 *        coverage atlas.
 *
 * Refuses an unreadable or non-canonical package, a package whose id or locale disagrees with the
 * approval, a sidecar (runs, atlas) whose length or digest disagrees, and a font that does not
 * approve this locale. Diagnostics carry `VUI006`.
 */
[[nodiscard]] std::optional<LocaleAssets>
loadLocale(const mdux::medui::TextPackageApproval& approval, const std::filesystem::path& artifactRoot, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

/**
 * @brief Loads and authenticates the image package `approval` names and its pixels.
 *
 * Refuses an unreadable or non-canonical package, an identity / extent / digest that disagrees with
 * the approval, and a pixel sidecar whose length or digest disagrees. Diagnostics carry `VUI006`.
 */
[[nodiscard]] std::optional<ImageAssets>
loadImage(const mdux::medui::ImagePackageApproval& approval, const std::filesystem::path& artifactRoot, std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::verify
