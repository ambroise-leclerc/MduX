/**
 * @file Schema.cpp
 * @brief Implementation of the governed text package types.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * Every member written here goes through `mdux.evidence.json`'s canonical writer, so a package's
 * bytes are identical on MSVC, libstdc++ and libc++. As with `mdux.shader.schema`, there is not a
 * single floating-point value in the format: a text contract is ids, offsets and sizes, so the
 * `{"bits": N}` float encoding ADR-007 requires never comes up here.
 */
module;

module mdux.text.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

namespace mdux::text {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = mdux::evidence::json;

namespace {

/// Reads a required unsigned member that must fit in 64 bits.
[[nodiscard]] Result<std::uint64_t, SchemaError> requireUint64(const json::Value& object,
                                                                std::string_view key) noexcept {
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

[[nodiscard]] Result<std::string, SchemaError> requireString(const json::Value& object,
                                                              std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    const auto text = (*member)->asString();
    if (!text.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return std::string{*text};
}

[[nodiscard]] Result<evidence::Digest, SchemaError> requireDigest(const json::Value& object,
                                                                   std::string_view key) noexcept {
    auto hex = requireString(object, key);
    if (!hex.has_value()) {
        return err(hex.error());
    }
    auto digest = evidence::digestFromHex(*hex);
    if (!digest.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return *digest;
}

[[nodiscard]] json::Value digestToJson(const evidence::Digest& digest) noexcept {
    const std::array<char, 64> hex = evidence::toHex(digest);
    return json::Value::string(std::string{hex.data(), hex.size()});
}

/// `object.set(key, value)`, collapsing the json error into MalformedPackage.
///
/// A json::Error out of set() means a duplicate key, which for a writer building a fresh object
/// from a fixed key list is a bug in this file rather than anything a caller did - so it is not
/// worth a distinct SchemaError a caller would have no way to act on.
[[nodiscard]] ResultVoid<SchemaError> setMember(json::Value& object, std::string key,
                                                 json::Value value) noexcept {
    if (auto done = object.set(std::move(key), std::move(value)); !done.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return {};
}

/// Allocation-free mirror of the canonical JSON writer for TextPackage's fixed schema.
class PackageDigestWriter {
public:
    void text(std::string_view value) noexcept {
        hasher_.update(std::as_bytes(std::span{value.data(), value.size()}));
    }

    template <std::size_t Size>
    void text(const std::array<char, Size>& value) noexcept {
        hasher_.update(std::as_bytes(std::span{value}));
    }

    void escaped(std::string_view value) noexcept {
        text("\"");
        for (const char character : value) {
            switch (character) {
                case '"':
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
                        const auto byte = static_cast<unsigned char>(character);
                        const std::array<char, 6> escapedCharacter{
                            '\\', 'u', '0', '0', digits[(byte >> 4) & 0x0fu], digits[byte & 0x0fu]
                        };
                        text(escapedCharacter);
                    } else {
                        const std::array<char, 1> raw{character};
                        text(raw);
                    }
                    break;
            }
        }
        text("\"");
    }

    void unsignedInteger(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t length = 0;
        do {
            digits[length++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        for (std::size_t index = length; index > 0; --index) {
            const std::array<char, 1> digit{digits[index - 1]};
            text(digit);
        }
    }

    void digest(const evidence::Digest& value) noexcept {
        const std::array<char, 64> hex = evidence::toHex(value);
        text("\"");
        text(hex);
        text("\"");
    }

    [[nodiscard]] evidence::Digest finish() const noexcept {
        return hasher_.finish();
    }

private:
    evidence::Sha256 hasher_{};
};

[[nodiscard]] ResultVoid<SchemaError> pushElement(json::Value& array, json::Value value) noexcept {
    if (auto done = array.push(std::move(value)); !done.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return {};
}

/// True when [aStart, aEnd) and [bStart, bEnd) share a byte.
[[nodiscard]] constexpr bool overlaps(std::uint64_t aStart, std::uint64_t aEnd,
                                       std::uint64_t bStart, std::uint64_t bEnd) noexcept {
    return aStart < bEnd && bStart < aEnd;
}

}  // namespace

// ---------------------------------------------------------------------------
// describe()
// ---------------------------------------------------------------------------

std::string_view describe(SchemaError error) noexcept {
    switch (error) {
    case SchemaError::WrongKind:               return "package kind is not \"text\"";
    case SchemaError::EmptyAtlasId:            return "atlas id is empty; a text package must reference a font package";
    case SchemaError::EmptyLocale:             return "locale is empty; an unlocalized text package is not meaningful";
    case SchemaError::EmptySidecarPath:        return "sidecar path is empty";
    case SchemaError::SidecarPathHasSeparator:
        return "sidecar path contains a path separator; it must be a bare filename";
    case SchemaError::EmptyRunId:              return "run id is empty";
    case SchemaError::DuplicateRunId:          return "two runs share an id";
    case SchemaError::UnalignedRun:
        return "run byte length is not a multiple of the run-record size";
    case SchemaError::RunOutOfBounds:          return "run range extends past the end of the sidecar";
    case SchemaError::OverlappingRuns:         return "two run ranges overlap";
    case SchemaError::UnsupportedSchemaVersion: return "unsupported schemaVersion";
    case SchemaError::MalformedPackage:        return "package JSON did not have the expected shape";
    case SchemaError::ReportRejected:         return "package header failed validation";
    }
    return "unknown text schema error";
}

// ---------------------------------------------------------------------------
// TextPackage::validate()
// ---------------------------------------------------------------------------

ResultVoid<SchemaError> TextPackage::validate() const noexcept {
    if (auto headerOk = header.validate(); !headerOk.has_value()) {
        return err(SchemaError::ReportRejected);
    }
    if (header.kind != packageKind) {
        return err(SchemaError::WrongKind);
    }
    if (header.schemaVersion != evidence::kSchemaVersion) {
        return err(SchemaError::UnsupportedSchemaVersion);
    }
    if (atlasId.empty()) {
        return err(SchemaError::EmptyAtlasId);
    }
    if (locale.empty()) {
        return err(SchemaError::EmptyLocale);
    }

    if (sidecarPath.empty()) {
        return err(SchemaError::EmptySidecarPath);
    }
    if (sidecarPath.find('/') != std::string::npos ||
        sidecarPath.find('\\') != std::string::npos) {
        return err(SchemaError::SidecarPathHasSeparator);
    }

    // No "no runs" rejection: an empty screen has zero runs, and zero runs is a valid (zero-byte)
    // sidecar. An empty sidecar with no runs is the smallest valid text package.

    // Bounds pass: check each run in isolation first. `byteEnd()` is `byteOffset + byteLength`,
    // which can wrap an unsigned when the run is huge; calling it here would be safe (any wrapping
    // run fails its own bounds check below), but reasoning about it should not have to. The bounds
    // check below is computed by subtraction from `sidecarByteLength`, which cannot overflow because
    // we have just established `byteOffset <= sidecarByteLength`. Only after every run passes its
    // bounds check does the pairwise overlap pass call `byteEnd()` on runs that are known to fit.
    for (const TextRun& run : runs) {
        if (run.id.empty()) {
            return err(SchemaError::EmptyRunId);
        }
        if (run.byteLength % recordSize != 0) {
            return err(SchemaError::UnalignedRun);
        }
        // Checked against the recorded sidecar length rather than by adding and hoping: the
        // subtraction cannot overflow, where byteOffset + byteLength could.
        if (run.byteOffset > sidecarByteLength ||
            run.byteLength > sidecarByteLength - run.byteOffset) {
            return err(SchemaError::RunOutOfBounds);
        }
    }

    // Pairwise overlap pass. `byteEnd()` is safe to call now, since every run's bounds check has
    // established `byteOffset + byteLength <= sidecarByteLength`, which cannot overflow a
    // `uint64_t` given a size the evidence pipeline can actually commit.
    for (std::size_t i = 0; i < runs.size(); ++i) {
        for (std::size_t j = i + 1; j < runs.size(); ++j) {
            if (runs[j].id == runs[i].id) {
                return err(SchemaError::DuplicateRunId);
            }
            if (overlaps(runs[i].byteOffset, runs[i].byteEnd(), runs[j].byteOffset,
                         runs[j].byteEnd())) {
                return err(SchemaError::OverlappingRuns);
            }
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// TextPackage::toJson() / write()
// ---------------------------------------------------------------------------

Result<json::Value, SchemaError> TextPackage::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }

    json::Value root = json::Value::emptyObject();
    if (auto written = header.writeInto(root); !written.has_value()) {
        return err(SchemaError::MalformedPackage);
    }

    if (auto done = setMember(root, "atlas", json::Value::string(atlasId)); !done.has_value()) {
        return err(done.error());
    }
    if (auto done = setMember(root, "locale", json::Value::string(locale)); !done.has_value()) {
        return err(done.error());
    }

    json::Value sidecar = json::Value::emptyObject();
    if (auto done = setMember(sidecar, "path", json::Value::string(sidecarPath));
        !done.has_value()) {
        return err(done.error());
    }
    if (auto done = setMember(sidecar, "byteLength",
                              json::Value::unsignedInteger(sidecarByteLength));
        !done.has_value()) {
        return err(done.error());
    }
    if (auto done = setMember(sidecar, "sha256", digestToJson(sidecarSha256)); !done.has_value()) {
        return err(done.error());
    }
    if (auto done = setMember(root, "sidecar", std::move(sidecar)); !done.has_value()) {
        return err(done.error());
    }

    json::Value runArray = json::Value::array({});
    for (const TextRun& run : runs) {
        json::Value entry = json::Value::emptyObject();
        if (auto done = setMember(entry, "id", json::Value::string(run.id));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "byteOffset",
                                  json::Value::unsignedInteger(run.byteOffset));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "byteLength",
                                  json::Value::unsignedInteger(run.byteLength));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "sha256", digestToJson(run.sha256)); !done.has_value()) {
            return err(done.error());
        }
        if (auto done = pushElement(runArray, std::move(entry)); !done.has_value()) {
            return err(done.error());
        }
    }
    if (auto done = setMember(root, "runs", std::move(runArray)); !done.has_value()) {
        return err(done.error());
    }

    return root;
}

Result<std::string, SchemaError> TextPackage::write() const noexcept {
    auto document = toJson();
    if (!document.has_value()) {
        return err(document.error());
    }
    auto text = json::write(*document);
    if (!text.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    return *text;
}

Result<evidence::Digest, SchemaError> TextPackage::canonicalSha256() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }

    PackageDigestWriter writer;
    writer.text("{\n  \"atlas\": ");
    writer.escaped(atlasId);
    writer.text(",\n  \"id\": ");
    writer.escaped(header.id);
    writer.text(",\n  \"kind\": ");
    writer.escaped(header.kind);
    writer.text(",\n  \"locale\": ");
    writer.escaped(locale);
    writer.text(",\n  \"runs\": ");
    if (runs.empty()) {
        writer.text("[]");
    } else {
        writer.text("[\n");
        for (std::size_t index = 0; index < runs.size(); ++index) {
            const TextRun& run = runs[index];
            writer.text("    {\n      \"byteLength\": ");
            writer.unsignedInteger(run.byteLength);
            writer.text(",\n      \"byteOffset\": ");
            writer.unsignedInteger(run.byteOffset);
            writer.text(",\n      \"id\": ");
            writer.escaped(run.id);
            writer.text(",\n      \"sha256\": ");
            writer.digest(run.sha256);
            writer.text(index + 1 < runs.size() ? "\n    },\n" : "\n    }\n");
        }
        writer.text("  ]");
    }
    writer.text(",\n  \"schemaVersion\": ");
    writer.unsignedInteger(header.schemaVersion);
    writer.text(",\n  \"sidecar\": {\n    \"byteLength\": ");
    writer.unsignedInteger(sidecarByteLength);
    writer.text(",\n    \"path\": ");
    writer.escaped(sidecarPath);
    writer.text(",\n    \"sha256\": ");
    writer.digest(sidecarSha256);
    writer.text("\n  }\n}\n");
    return writer.finish();
}

// ---------------------------------------------------------------------------
// TextPackage::parse()
// ---------------------------------------------------------------------------

Result<TextPackage, SchemaError> TextPackage::parse(std::string_view text) noexcept {
    auto document = json::parse(text);
    if (!document.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    if (document->kind() != json::Value::Kind::Object) {
        return err(SchemaError::MalformedPackage);
    }

    TextPackage package;

    auto header = evidence::PackageHeader::readFrom(*document);
    if (!header.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    package.header = std::move(*header);

    auto atlas = requireString(*document, "atlas");
    if (!atlas.has_value()) {
        return err(atlas.error());
    }
    package.atlasId = std::move(*atlas);

    auto locale = requireString(*document, "locale");
    if (!locale.has_value()) {
        return err(locale.error());
    }
    package.locale = std::move(*locale);

    const auto sidecar = document->require("sidecar");
    if (!sidecar.has_value() || (*sidecar)->kind() != json::Value::Kind::Object) {
        return err(SchemaError::MalformedPackage);
    }
    auto sidecarPath = requireString(**sidecar, "path");
    if (!sidecarPath.has_value()) {
        return err(sidecarPath.error());
    }
    package.sidecarPath = std::move(*sidecarPath);
    auto sidecarLength = requireUint64(**sidecar, "byteLength");
    if (!sidecarLength.has_value()) {
        return err(sidecarLength.error());
    }
    package.sidecarByteLength = *sidecarLength;
    auto sidecarDigest = requireDigest(**sidecar, "sha256");
    if (!sidecarDigest.has_value()) {
        return err(sidecarDigest.error());
    }
    package.sidecarSha256 = *sidecarDigest;

    const auto runArray = document->require("runs");
    if (!runArray.has_value() || (*runArray)->kind() != json::Value::Kind::Array) {
        return err(SchemaError::MalformedPackage);
    }
    for (const json::Value& entry : (*runArray)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::MalformedPackage);
        }
        TextRun run;
        auto id = requireString(entry, "id");
        if (!id.has_value()) {
            return err(id.error());
        }
        run.id = std::move(*id);
        auto offset = requireUint64(entry, "byteOffset");
        if (!offset.has_value()) {
            return err(offset.error());
        }
        run.byteOffset = *offset;
        auto length = requireUint64(entry, "byteLength");
        if (!length.has_value()) {
            return err(length.error());
        }
        run.byteLength = *length;
        auto digest = requireDigest(entry, "sha256");
        if (!digest.has_value()) {
            return err(digest.error());
        }
        run.sha256 = *digest;
        package.runs.push_back(std::move(run));
    }

    if (auto valid = package.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return package;
}

const TextRun* TextPackage::find(std::string_view id) const noexcept {
    for (const TextRun& run : runs) {
        if (run.id == id) {
            return &run;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// PackageView
// ---------------------------------------------------------------------------

const RunView* PackageView::find(std::string_view runId) const noexcept {
    for (const RunView& run : runs) {
        if (run.id == runId) {
            return &run;
        }
    }
    return nullptr;
}

std::span<const std::byte> PackageView::runBytes(std::string_view runId) const noexcept {
    const RunView* run = find(runId);
    if (run == nullptr) {
        return {};
    }
    // Bounds-checked even though the generated data is machine-written: a view can also be
    // assembled by hand in a test, and a span past the end of the sidecar is the one mistake
    // here that would not fail visibly. `RunView` carries `uint64_t` offsets (parity with the
    // owning `TextRun`); `runsBytes.size()` is `size_t`, so the comparison is widened rather
    // than narrowed, and the empty-span return is what catches an out-of-range view before the
    // subspan.
    if (run->byteOffset > runsBytes.size() ||
        run->byteLength > runsBytes.size() - run->byteOffset) {
        return {};
    }
    return runsBytes.subspan(static_cast<std::size_t>(run->byteOffset),
                              static_cast<std::size_t>(run->byteLength));
}

}  // namespace mdux::text
