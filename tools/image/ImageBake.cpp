/**
 * @file ImageBake.cpp
 * @brief Implementation of the host-only QOI image baker.
 */
module;

module mdux.tools.imagebake;

import std;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.image.schema;
import mdux.tools.cli;
import mdux.tools.qoi;
import mdux.tools.toml;

namespace mdux::tools::imagebake {

namespace evidence = mdux::evidence;
namespace json     = mdux::evidence::json;
namespace cli      = mdux::tools::cli;
namespace qoi      = mdux::tools::qoi;
namespace toml     = mdux::tools::toml;

namespace {

constexpr std::string_view recipeUnparsed      = "IMB001";
constexpr std::string_view recipeMissingMember = "IMB002";
constexpr std::string_view sourceUnreadable    = "IMB003";
constexpr std::string_view sourceRejected      = "IMB004";
constexpr std::string_view packageInvalid      = "IMB005";
constexpr std::string_view outputUnwritable    = "IMB006";
constexpr std::string_view artifactMissing     = "IMB007";
constexpr std::string_view artifactDiffers     = "IMB008";
constexpr std::string_view pathEscapesRoot     = "IMB009";

/// Whether a recipe string carries a byte no artifact path or identifier may contain. A NUL ends a
/// pathname for every filesystem call below, so a sidecar named "package.json\0.rgba" clears the
/// reserved-name comparison and then truncates onto package.json when the bytes are written.
[[nodiscard]] bool hasControlCharacter(std::string_view text) noexcept {
    return std::ranges::any_of(text, [](char character) {
        return static_cast<unsigned char>(character) < 0x20u || character == 0x7f;
    });
}

void report(std::vector<cli::Diagnostic>& diagnostics,
            std::string                   file,
            std::size_t                   line,
            std::string_view              code,
            std::string                   message,
            std::string                   fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = std::move(file),
                                          .line     = line,
                                          .code     = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fixHint)});
}

[[nodiscard]] std::string reportPath(const std::filesystem::path& path) {
    return path.generic_string();
}

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path, std::span<const std::byte> bytes) {
    return {.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

[[nodiscard]] std::optional<std::filesystem::path> confine(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute()) {
        return std::nullopt;
    }
    const std::filesystem::path normalRoot = root.lexically_normal();
    const std::filesystem::path candidate  = (normalRoot / relative).lexically_normal();
    const auto                  lexical    = candidate.lexically_relative(normalRoot);
    if (lexical.empty() || *lexical.begin() == "..") {
        return std::nullopt;
    }

    // A lexically contained entry may still be a symlink to host state outside the repository.
    // Resolve both sides: the root itself may be reached through a symlink (notably /tmp on macOS),
    // so resolving only the candidate would also reject legitimate repository files.
    std::error_code error;
    const auto      canonicalRoot = std::filesystem::weakly_canonical(normalRoot, error);
    if (error) {
        return std::nullopt;
    }
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return std::nullopt;
    }
    const auto canonicalRelative = canonicalCandidate.lexically_relative(canonicalRoot);
    if (canonicalRelative.empty() || *canonicalRelative.begin() == "..") {
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] bool writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes, std::vector<cli::Diagnostic>& diagnostics) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "cannot open for writing");
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "write failed");
        return false;
    }
    return true;
}

[[nodiscard]] bool
compare(std::string_view label, std::span<const std::byte> produced, const std::filesystem::path& path, std::vector<cli::Diagnostic>& diagnostics) {
    const auto committed = readFile(path);
    if (!committed.has_value()) {
        report(diagnostics,
               reportPath(path),
               0,
               artifactMissing,
               std::string{label} + " is missing or unreadable",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return false;
    }
    const std::size_t common = std::min(produced.size(), committed->size());
    for (std::size_t index = 0; index < common; ++index) {
        if (produced[index] != (*committed)[index]) {
            report(diagnostics,
                   reportPath(path),
                   0,
                   artifactDiffers,
                   std::format("{} differs at byte {}", label, index),
                   "Re-bake the artifact and review the diff.");
            return false;
        }
    }
    if (produced.size() != committed->size()) {
        report(diagnostics,
               reportPath(path),
               0,
               artifactDiffers,
               std::format("{} length differs: produced {}, committed {}", label, produced.size(), committed->size()),
               "Re-bake the artifact and review the diff.");
        return false;
    }
    return true;
}

}  // namespace

json::Value Recipe::toOptions() const {
    json::Value options = json::Value::emptyObject();
    static_cast<void>(options.set("id", json::Value::string(id)));
    static_cast<void>(options.set("pixelFormat", json::Value::string("rgba8-srgb-straight")));
    static_cast<void>(options.set("sidecar", json::Value::string(sidecar)));
    static_cast<void>(options.set("source", json::Value::string(source)));
    return options;
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file)
        return std::nullopt;
    std::vector<std::byte> bytes;
    std::array<char, 8192> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0) {
        const auto  count = static_cast<std::size_t>(file.gcount());
        const auto* begin = reinterpret_cast<const std::byte*>(buffer.data());
        bytes.insert(bytes.end(), begin, begin + count);
        if (file.eof())
            break;
    }
    if (file.bad())
        return std::nullopt;
    return bytes;
}

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document;
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeUnparsed, error.what());
        return std::nullopt;
    }
    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember, "recipe has no [package] table", "Add id, source and sidecar members.");
        return std::nullopt;
    }
    Recipe recipe;
    try {
        recipe.id     = package->require("id").asString();
        recipe.source = package->require("source").asString();
        if (const toml::Value* sidecar = package->find("sidecar"))
            recipe.sidecar = sidecar->asString();
    } catch (const toml::TomlError& error) {
        report(diagnostics,
               std::string{recipePath},
               error.line(),
               recipeMissingMember,
               error.what(),
               "[package] needs string members 'id' and 'source'; 'sidecar' is optional.");
        return std::nullopt;
    }
    if (recipe.id.empty() || recipe.source.empty() || recipe.sidecar.empty() || mdux::image::isReservedSidecarPath(recipe.sidecar)
        || recipe.sidecar.find('/') != std::string::npos || recipe.sidecar.find('\\') != std::string::npos) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember, "id/source must be non-empty and sidecar must be a bare, non-reserved filename");
        return std::nullopt;
    }
    if (hasControlCharacter(recipe.id) || hasControlCharacter(recipe.source) || hasControlCharacter(recipe.sidecar)) {
        report(diagnostics,
               std::string{recipePath},
               0,
               recipeMissingMember,
               "id/source/sidecar must not contain control characters",
               "A NUL truncates the path at write time, so a sidecar can be made to overwrite package.json.");
        return std::nullopt;
    }
    return recipe;
}

std::optional<BakeOutputs> run(const Recipe&                 recipe,
                               std::string_view              recipePath,
                               std::span<const std::byte>    recipeBytes,
                               const std::filesystem::path&  root,
                               std::vector<cli::Diagnostic>& diagnostics) {
    const auto sourcePath = confine(root, std::filesystem::path{recipe.source});
    if (!sourcePath.has_value()) {
        report(diagnostics, recipe.source, 0, pathEscapesRoot, "image source must resolve inside the repository root");
        return std::nullopt;
    }
    const auto source = readFile(*sourcePath);
    if (!source.has_value()) {
        report(diagnostics, recipe.source, 0, sourceUnreadable, "cannot read QOI source");
        return std::nullopt;
    }
    auto decoded = qoi::decode(*source);
    if (!decoded.has_value()) {
        report(diagnostics,
               recipe.source,
               0,
               sourceRejected,
               std::string{"QOI source was rejected: "} + std::string{qoi::describe(decoded.error())},
               "Export the source as QOI with RGB or RGBA channels; PNG is not accepted.");
        return std::nullopt;
    }
    if (decoded->colorSpace != 0) {
        report(diagnostics,
               recipe.source,
               0,
               sourceRejected,
               "QOI source declares linear colour channels, but the image package format is sRGB",
               "Export QOI with colorspace 0 (sRGB channels with linear alpha).");
        return std::nullopt;
    }

    BakeOutputs               outputs{.packageJson = {},
                                      .reportJson  = {},
                                      .sidecar     = std::move(decoded->rgba),
                                      .sidecarName = recipe.sidecar,
                                      .packageId   = recipe.id,
                                      .width       = decoded->width,
                                      .height      = decoded->height};
    mdux::image::ImagePackage package;
    package.header.id         = recipe.id;
    package.width             = outputs.width;
    package.height            = outputs.height;
    package.sidecarPath       = recipe.sidecar;
    package.sidecarByteLength = outputs.sidecar.size();
    package.sidecarSha256     = evidence::sha256(outputs.sidecar);
    auto packageJson          = package.write();
    if (!packageJson.has_value()) {
        report(diagnostics,
               std::string{recipePath},
               0,
               packageInvalid,
               std::string{"image package is invalid: "} + std::string{mdux::image::describe(packageJson.error())});
        return std::nullopt;
    }
    outputs.packageJson = std::move(*packageJson);

    evidence::BakeReport bakeReport;
    bakeReport.tool        = std::string{toolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe      = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs      = {fileRecord(recipe.source, *source)};
    bakeReport.options     = recipe.toOptions();
    bakeReport.outputs     = {fileRecord("package.json", asBytes(outputs.packageJson)), fileRecord(outputs.sidecarName, outputs.sidecar)};
    auto reportJson        = bakeReport.write();
    if (!reportJson.has_value()) {
        report(diagnostics,
               std::string{recipePath},
               0,
               packageInvalid,
               std::string{"image report is invalid: "} + std::string{evidence::describe(reportJson.error())});
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportJson);
    return outputs;
}

bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code error;
    std::filesystem::create_directories(outputDir, error);
    if (error) {
        report(diagnostics, reportPath(outputDir), 0, outputUnwritable, "cannot create output directory: " + error.message());
        return false;
    }
    bool ok = writeBytes(outputDir / "package.json", asBytes(outputs.packageJson), diagnostics);
    ok      = writeBytes(outputDir / "report.json", asBytes(outputs.reportJson), diagnostics) && ok;
    ok      = writeBytes(outputDir / outputs.sidecarName, outputs.sidecar, diagnostics) && ok;
    return ok;
}

bool verify(const BakeOutputs&            outputs,
            const std::filesystem::path&  packagePath,
            const std::filesystem::path&  reportPathValue,
            std::vector<cli::Diagnostic>& diagnostics) {
    bool ok = compare("package.json", asBytes(outputs.packageJson), packagePath, diagnostics);
    ok      = compare("report.json", asBytes(outputs.reportJson), reportPathValue, diagnostics) && ok;
    ok      = compare(outputs.sidecarName, outputs.sidecar, packagePath.parent_path() / outputs.sidecarName, diagnostics) && ok;
    return ok;
}

}  // namespace mdux::tools::imagebake
