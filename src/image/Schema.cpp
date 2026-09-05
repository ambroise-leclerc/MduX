/**
 * @file Schema.cpp
 * @brief Implementation of the governed baked-image package schema.
 */
module;

module mdux.image.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

namespace mdux::image {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = mdux::evidence::json;

namespace {

[[nodiscard]] ResultVoid<SchemaError> setMember(json::Value& object, std::string key, json::Value value) noexcept {
    if (!object.set(std::move(key), std::move(value)).has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return {};
}

[[nodiscard]] Result<std::uint64_t, SchemaError> requireUint(const json::Value& object, std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    const auto value = (*member)->asUInt();
    if (!value.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return *value;
}

[[nodiscard]] Result<std::string, SchemaError> requireString(const json::Value& object, std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    const auto value = (*member)->asString();
    if (!value.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return std::string{*value};
}

[[nodiscard]] json::Value digestJson(const evidence::Digest& digest) noexcept {
    const std::array<char, 64> hex = evidence::toHex(digest);
    return json::Value::string(std::string{hex.data(), hex.size()});
}

class DigestWriter {
public:
    void text(std::string_view value) noexcept {
        hasher.update(std::as_bytes(std::span{value.data(), value.size()}));
    }

    template <std::size_t Size>
    void text(const std::array<char, Size>& value) noexcept {
        hasher.update(std::as_bytes(std::span{value}));
    }

    void escaped(std::string_view value) noexcept {
        text("\"");
        for (const char character : value) {
            switch (character) {
                case '\"':
                    text("\\\"");
                    break;
                case '\\':
                    text("\\\\");
                    break;
                case '\b':
                    text("\\b");
                    break;
                case '\f':
                    text("\\f");
                    break;
                case '\n':
                    text("\\n");
                    break;
                case '\r':
                    text("\\r");
                    break;
                case '\t':
                    text("\\t");
                    break;
                default:
                    if (static_cast<unsigned char>(character) < 0x20) {
                        constexpr std::string_view digits = "0123456789abcdef";
                        const auto                 byte   = static_cast<unsigned char>(character);
                        const std::array<char, 6>  escapedCharacter{'\\', 'u', '0', '0', digits[(byte >> 4) & 0x0fu], digits[byte & 0x0fu]};
                        text(escapedCharacter);
                    } else {
                        text(std::array<char, 1>{character});
                    }
                    break;
            }
        }
        text("\"");
    }

    void unsignedInteger(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t          length = 0;
        do {
            digits[length++] = static_cast<char>('0' + value % 10);
            value           /= 10;
        } while (value != 0);
        while (length > 0) {
            text(std::array<char, 1>{digits[--length]});
        }
    }

    void digest(const evidence::Digest& value) noexcept {
        text("\"");
        text(evidence::toHex(value));
        text("\"");
    }

    [[nodiscard]] evidence::Digest finish() const noexcept {
        return hasher.finish();
    }

private:
    evidence::Sha256 hasher{};
};

}  // namespace

std::string_view describe(SchemaError error) noexcept {
    switch (error) {
        case SchemaError::WrongKind:
            return "package kind is not \"image\"";
        case SchemaError::UnsupportedSchemaVersion:
            return "unsupported schemaVersion";
        case SchemaError::EmptyId:
            return "image package id is empty";
        case SchemaError::ZeroExtent:
            return "image width and height must be non-zero";
        case SchemaError::PixelCountOverflow:
            return "image dimensions overflow the RGBA8 byte count";
        case SchemaError::SidecarSizeMismatch:
            return "sidecar byte length is not width * height * 4";
        case SchemaError::EmptySidecarPath:
            return "sidecar path is empty";
        case SchemaError::SidecarPathHasSeparator:
            return "sidecar path must be a bare filename";
        case SchemaError::SidecarPathHasControlCharacter:
            return "sidecar path contains a control character";
        case SchemaError::ReservedSidecarPath:
            return "sidecar path collides with package.json or report.json";
        case SchemaError::MalformedPackage:
            return "package JSON did not have the expected shape";
        case SchemaError::ReportRejected:
            return "package header failed validation";
    }
    return "unknown image schema error";
}

ResultVoid<SchemaError> ImagePackage::validate() const noexcept {
    if (!header.validate().has_value()) {
        return err(SchemaError::ReportRejected);
    }
    if (header.kind != packageKind) {
        return err(SchemaError::WrongKind);
    }
    if (header.schemaVersion != evidence::kSchemaVersion) {
        return err(SchemaError::UnsupportedSchemaVersion);
    }
    if (header.id.empty()) {
        return err(SchemaError::EmptyId);
    }
    if (width == 0 || height == 0) {
        return err(SchemaError::ZeroExtent);
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > std::numeric_limits<std::uint64_t>::max() / 4u) {
        return err(SchemaError::PixelCountOverflow);
    }
    if (sidecarByteLength != pixels * 4u) {
        return err(SchemaError::SidecarSizeMismatch);
    }
    if (sidecarPath.empty()) {
        return err(SchemaError::EmptySidecarPath);
    }
    if (sidecarPath.find('/') != std::string::npos || sidecarPath.find('\\') != std::string::npos) {
        return err(SchemaError::SidecarPathHasSeparator);
    }
    // A NUL terminates a pathname for every filesystem call the baker makes, so "package.json\0.rgba"
    // reads as distinct from "package.json" here and then truncates onto it at open() time. Nothing
    // legitimate names a bare artifact file with a control character, so the whole class is refused.
    if (std::ranges::any_of(sidecarPath, [](char character) {
            return static_cast<unsigned char>(character) < 0x20u || character == 0x7f;
        })) {
        return err(SchemaError::SidecarPathHasControlCharacter);
    }
    if (isReservedSidecarPath(sidecarPath)) {
        return err(SchemaError::ReservedSidecarPath);
    }
    return {};
}

Result<json::Value, SchemaError> ImagePackage::toJson() const noexcept {
    if (const auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    json::Value root = json::Value::emptyObject();
    if (!header.writeInto(root).has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    if (auto done = setMember(root, "height", json::Value::unsignedInteger(height)); !done.has_value())
        return err(done.error());
    if (auto done = setMember(root, "pixelFormat", json::Value::string("rgba8-srgb-straight")); !done.has_value())
        return err(done.error());
    json::Value sidecar = json::Value::emptyObject();
    if (auto done = setMember(sidecar, "byteLength", json::Value::unsignedInteger(sidecarByteLength)); !done.has_value())
        return err(done.error());
    if (auto done = setMember(sidecar, "path", json::Value::string(sidecarPath)); !done.has_value())
        return err(done.error());
    if (auto done = setMember(sidecar, "sha256", digestJson(sidecarSha256)); !done.has_value())
        return err(done.error());
    if (auto done = setMember(root, "sidecar", std::move(sidecar)); !done.has_value())
        return err(done.error());
    if (auto done = setMember(root, "width", json::Value::unsignedInteger(width)); !done.has_value())
        return err(done.error());
    return root;
}

Result<std::string, SchemaError> ImagePackage::write() const noexcept {
    auto document = toJson();
    if (!document.has_value())
        return err(document.error());
    auto output = json::write(*document);
    if (!output.has_value())
        return err(SchemaError::MalformedPackage);
    return *output;
}

Result<evidence::Digest, SchemaError> ImagePackage::canonicalSha256() const noexcept {
    if (const auto valid = validate(); !valid.has_value())
        return err(valid.error());
    DigestWriter writer;
    writer.text("{\n  \"height\": ");
    writer.unsignedInteger(height);
    writer.text(",\n  \"id\": ");
    writer.escaped(header.id);
    writer.text(",\n  \"kind\": ");
    writer.escaped(header.kind);
    writer.text(",\n  \"pixelFormat\": \"rgba8-srgb-straight\",");
    writer.text("\n  \"schemaVersion\": ");
    writer.unsignedInteger(header.schemaVersion);
    writer.text(",\n  \"sidecar\": {\n    \"byteLength\": ");
    writer.unsignedInteger(sidecarByteLength);
    writer.text(",\n    \"path\": ");
    writer.escaped(sidecarPath);
    writer.text(",\n    \"sha256\": ");
    writer.digest(sidecarSha256);
    writer.text("\n  },\n  \"width\": ");
    writer.unsignedInteger(width);
    writer.text("\n}\n");
    return writer.finish();
}

Result<ImagePackage, SchemaError> ImagePackage::parse(std::string_view text) noexcept {
    auto document = json::parse(text);
    if (!document.has_value() || document->kind() != json::Value::Kind::Object)
        return err(SchemaError::MalformedPackage);
    ImagePackage package;
    auto         header = evidence::PackageHeader::readFrom(*document);
    if (!header.has_value())
        return err(SchemaError::MalformedPackage);
    package.header   = std::move(*header);
    auto widthValue  = requireUint(*document, "width");
    auto heightValue = requireUint(*document, "height");
    auto format      = requireString(*document, "pixelFormat");
    if (!widthValue.has_value() || !heightValue.has_value() || !format.has_value() || *widthValue > std::numeric_limits<std::uint32_t>::max()
        || *heightValue > std::numeric_limits<std::uint32_t>::max() || *format != "rgba8-srgb-straight")
        return err(SchemaError::MalformedPackage);
    package.width      = static_cast<std::uint32_t>(*widthValue);
    package.height     = static_cast<std::uint32_t>(*heightValue);
    const auto sidecar = document->require("sidecar");
    if (!sidecar.has_value() || (*sidecar)->kind() != json::Value::Kind::Object)
        return err(SchemaError::MalformedPackage);
    auto path       = requireString(**sidecar, "path");
    auto length     = requireUint(**sidecar, "byteLength");
    auto digestText = requireString(**sidecar, "sha256");
    if (!path.has_value() || !length.has_value() || !digestText.has_value())
        return err(SchemaError::MalformedPackage);
    auto digest = evidence::digestFromHex(*digestText);
    if (!digest.has_value())
        return err(SchemaError::MalformedPackage);
    package.sidecarPath       = std::move(*path);
    package.sidecarByteLength = *length;
    package.sidecarSha256     = *digest;
    if (const auto valid = package.validate(); !valid.has_value())
        return err(valid.error());
    return package;
}

}  // namespace mdux::image
