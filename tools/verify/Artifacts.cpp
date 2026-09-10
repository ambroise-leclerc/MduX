/**
 * @file Artifacts.cpp
 * @brief Implementation of the shared committed-artifact loaders.
 */
module;

module mdux.tools.verify.artifacts;

import std;
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.image.schema;
import mdux.medui.schema;
import mdux.shader.schema;
import mdux.text.schema;
import mdux.tools.cli;

namespace mdux::tools::verify {
namespace {

namespace cli = mdux::tools::cli;

void report(std::vector<cli::Diagnostic>& diagnostics, const std::filesystem::path& file, std::string code, std::string message) {
    diagnostics.push_back(cli::Diagnostic{.file     = file.generic_string(),
                                          .code     = std::move(code),
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = {}});
}

}  // namespace

std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

std::optional<std::string> readText(const std::filesystem::path& path) {
    auto bytes = readBytes(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    if (bytes->empty()) {
        return std::string{};
    }
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

std::string hexDigest(std::string_view text) {
    const auto bytes  = std::as_bytes(std::span{text.data(), text.size()});
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(bytes));
    return std::string{digest.data(), digest.size()};
}

std::optional<ShaderAssets> loadShader(const std::filesystem::path& artifactRoot, std::vector<cli::Diagnostic>& diagnostics) {
    const auto packagePath = artifactRoot / "shader" / "mdux-ui" / "package.json";
    auto       text        = readText(packagePath);
    if (!text) {
        report(diagnostics, packagePath, "VUI005", "cannot read the committed mdux-ui shader package");
        return std::nullopt;
    }
    auto package = mdux::shader::ShaderPackage::parse(*text);
    if (!package.has_value()) {
        report(diagnostics, packagePath, "VUI005", "invalid mdux-ui shader package: " + std::string{mdux::shader::describe(package.error())});
        return std::nullopt;
    }
    auto canonical = package->write();
    if (!canonical.has_value() || *canonical != *text) {
        report(diagnostics, packagePath, "VUI005", "mdux-ui package.json is not canonical");
        return std::nullopt;
    }
    const auto sidecarPath = packagePath.parent_path() / package->sidecarPath;
    auto       sidecar     = readBytes(sidecarPath);
    if (!sidecar || sidecar->size() != package->sidecarByteLength || mdux::evidence::sha256(*sidecar) != package->sidecarSha256) {
        report(diagnostics, sidecarPath, "VUI005", "the mdux-ui shader sidecar does not match package.json");
        return std::nullopt;
    }
    ShaderAssets assets{.package = std::move(*package), .sidecar = std::move(*sidecar), .modules = {}, .sha256 = hexDigest(*text)};
    assets.modules.reserve(assets.package.modules.size());
    for (const auto& module : assets.package.modules) {
        if (module.byteOffset > std::numeric_limits<std::size_t>::max() || module.byteLength > std::numeric_limits<std::size_t>::max()) {
            report(diagnostics, packagePath, "VUI005", "a shader module range does not fit this host");
            return std::nullopt;
        }
        assets.modules.push_back({.id         = module.id,
                                  .stage      = module.stage,
                                  .entryPoint = module.entryPoint,
                                  .byteOffset = static_cast<std::size_t>(module.byteOffset),
                                  .byteLength = static_cast<std::size_t>(module.byteLength)});
    }
    return assets;
}

std::optional<ImageAssets>
loadImage(const mdux::medui::ImagePackageApproval& approval, const std::filesystem::path& artifactRoot, std::vector<cli::Diagnostic>& diagnostics) {
    const auto packagePath = artifactRoot / "image" / approval.packageId / "package.json";
    auto       imageJson   = readText(packagePath);
    if (!imageJson) {
        report(diagnostics, packagePath, "VUI006", "cannot read approved image package");
        return std::nullopt;
    }
    auto image = mdux::image::ImagePackage::parse(*imageJson);
    if (!image.has_value()) {
        report(diagnostics, packagePath, "VUI006", "invalid approved image package: " + std::string{mdux::image::describe(image.error())});
        return std::nullopt;
    }
    const auto canonical = image->write();
    if (!canonical.has_value() || *canonical != *imageJson || image->header.id != approval.packageId || image->width != approval.width
        || image->height != approval.height || mdux::evidence::sha256(std::as_bytes(std::span{*imageJson})) != approval.packageSha256) {
        report(diagnostics, packagePath, "VUI006", "approved image identity, extent, digest, or canonical bytes disagree with the screen manifest");
        return std::nullopt;
    }
    const auto pixelsPath = packagePath.parent_path() / image->sidecarPath;
    auto       pixels     = readBytes(pixelsPath);
    if (!pixels || pixels->size() != image->sidecarByteLength || mdux::evidence::sha256(*pixels) != image->sidecarSha256) {
        report(diagnostics, pixelsPath, "VUI006", "image sidecar does not match its package");
        return std::nullopt;
    }
    return ImageAssets{.imageJson = std::move(*imageJson), .image = std::move(*image), .pixels = std::move(*pixels)};
}

std::optional<LocaleAssets>
loadLocale(const mdux::medui::TextPackageApproval& approval, const std::filesystem::path& artifactRoot, std::vector<cli::Diagnostic>& diagnostics) {
    const auto textPath = artifactRoot / "text" / approval.packageId / "package.json";
    auto       textJson = readText(textPath);
    if (!textJson) {
        report(diagnostics, textPath, "VUI006", "cannot read approved text package for locale '" + std::string{approval.locale} + "'");
        return std::nullopt;
    }
    auto text = mdux::text::TextPackage::parse(*textJson);
    if (!text.has_value()) {
        report(diagnostics, textPath, "VUI006", "invalid approved text package: " + std::string{mdux::text::describe(text.error())});
        return std::nullopt;
    }
    auto canonicalText = text->write();
    if (!canonicalText.has_value() || *canonicalText != *textJson || text->header.id != approval.packageId || text->locale != approval.locale
        || mdux::evidence::sha256(std::as_bytes(std::span{*textJson})) != approval.packageSha256) {
        report(diagnostics, textPath, "VUI006", "approved text package identity, digest, or canonical bytes disagree with the screen manifest");
        return std::nullopt;
    }
    const auto runsPath = textPath.parent_path() / text->sidecarPath;
    auto       runs     = readBytes(runsPath);
    if (!runs || runs->size() != text->sidecarByteLength || mdux::evidence::sha256(*runs) != text->sidecarSha256) {
        report(diagnostics, runsPath, "VUI006", "text sidecar does not match its package");
        return std::nullopt;
    }

    const auto fontPath = artifactRoot / "font" / text->atlasId / "package.json";
    auto       fontJson = readText(fontPath);
    if (!fontJson) {
        report(diagnostics, fontPath, "VUI006", "cannot read the font package named by the text package");
        return std::nullopt;
    }
    auto font = mdux::font::FontPackage::parse(*fontJson);
    if (!font.has_value()) {
        report(diagnostics, fontPath, "VUI006", "invalid font package: " + std::string{mdux::font::describe(font.error())});
        return std::nullopt;
    }
    auto canonicalFont = font->write();
    if (!canonicalFont.has_value() || *canonicalFont != *fontJson || font->id != text->atlasId
        || std::ranges::find(font->locales, approval.locale) == font->locales.end()) {
        report(diagnostics, fontPath, "VUI006", "font identity, locale approval, or canonical bytes disagree with the text package");
        return std::nullopt;
    }
    const auto atlasPath = fontPath.parent_path() / font->atlas.path;
    auto       atlas     = readBytes(atlasPath);
    if (!atlas || atlas->size() != font->atlas.byteLength) {
        report(diagnostics, atlasPath, "VUI006", "font atlas length does not match its package");
        return std::nullopt;
    }
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(*atlas));
    if (std::string_view{digest.data(), digest.size()} != font->atlas.sha256) {
        report(diagnostics, atlasPath, "VUI006", "font atlas digest does not match its package");
        return std::nullopt;
    }
    return LocaleAssets{.locale   = std::string{approval.locale},
                        .textJson = std::move(*textJson),
                        .text     = std::move(*text),
                        .runs     = std::move(*runs),
                        .fontJson = std::move(*fontJson),
                        .font     = std::move(*font),
                        .atlas    = std::move(*atlas)};
}

}  // namespace mdux::tools::verify
