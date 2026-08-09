/**
 * @file Report.cpp
 * @brief Bake-report serialization for the governed evidence zone.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Everything here goes through mdux.evidence.json's canonical writer. No number in a report is
 * a float, so no float ever reaches a formatter - but that is a property of the schema, not a
 * licence to relax the rule, which is why the evidence lint covers this file too.
 */
module;

module mdux.evidence.report;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;

namespace mdux::evidence {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;

namespace {

/// Path rules from ADR-007, applied identically to recipes, inputs and outputs.
///
/// These are not stylistic. An absolute path embeds the producing machine in a committed
/// artifact, so two developers baking the same input get different bytes. A backslash does the
/// same across Windows and Linux. A `..` component means the recorded path does not identify a
/// location under the repository root, which makes the digest unverifiable by a third party.
[[nodiscard]] ResultVoid<ReportError> validatePath(std::string_view path) noexcept {
    if (path.empty()) {
        return err(ReportError::EmptyPath);
    }
    if (path.find('\\') != std::string_view::npos) {
        return err(ReportError::BackslashInPath);
    }
    // POSIX absolute, plus the Windows drive-letter and UNC forms.
    if (path.front() == '/') {
        return err(ReportError::AbsolutePath);
    }
    if (path.size() >= 2 && path[1] == ':') {
        return err(ReportError::AbsolutePath);
    }
    // Component scan by hand rather than through views::split: the pattern-and-subrange dance
    // reads worse here than the loop does, and this has to be obviously correct.
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = (slash == std::string_view::npos) ? path.size() : slash;
        if (path.substr(start, end - start) == "..") {
            return err(ReportError::ParentDirectoryInPath);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return {};
}

[[nodiscard]] ResultVoid<ReportError> validateFileRecord(const FileRecord& record) noexcept {
    return validatePath(record.path);
}

/// A FileRecord as its canonical `{ "path": …, "sha256": … }` object.
[[nodiscard]] Result<json::Value, json::Error> fileRecordToJson(const FileRecord& record) noexcept {
    json::Value object = json::Value::emptyObject();
    if (auto set = object.set("path", json::Value::string(record.path)); !set.has_value()) {
        return err(set.error());
    }
    const std::array<char, 64> hex = toHex(record.sha256);
    if (auto set = object.set("sha256", json::Value::string(std::string{hex.data(), hex.size()}));
        !set.has_value()) {
        return err(set.error());
    }
    return object;
}

[[nodiscard]] Result<FileRecord, ReportError> fileRecordFromJson(const json::Value& object) noexcept {
    const auto path = object.require("path");
    if (!path.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto pathText = (*path)->asString();
    if (!pathText.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto sha = object.require("sha256");
    if (!sha.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto shaText = (*sha)->asString();
    if (!shaText.has_value()) {
        return err(ReportError::MalformedReport);
    }
    auto digest = digestFromHex(*shaText);
    if (!digest.has_value()) {
        return err(digest.error());
    }
    return FileRecord{.path = std::string{*pathText}, .sha256 = *digest};
}

[[nodiscard]] Result<json::Value, json::Error> fileRecordsToJson(
    std::span<const FileRecord> records) noexcept {
    json::Value array = json::Value::array({});
    for (const FileRecord& record : records) {
        auto object = fileRecordToJson(record);
        if (!object.has_value()) {
            return err(object.error());
        }
        if (auto pushed = array.push(std::move(*object)); !pushed.has_value()) {
            return err(pushed.error());
        }
    }
    return array;
}

[[nodiscard]] Result<std::vector<FileRecord>, ReportError> fileRecordsFromJson(
    const json::Value& array) noexcept {
    if (array.kind() != json::Value::Kind::Array) {
        return err(ReportError::MalformedReport);
    }
    std::vector<FileRecord> records;
    records.reserve(array.elements().size());
    for (const json::Value& element : array.elements()) {
        auto record = fileRecordFromJson(element);
        if (!record.has_value()) {
            return err(record.error());
        }
        records.push_back(std::move(*record));
    }
    return records;
}

/// Reads a required non-empty string member.
[[nodiscard]] Result<std::string, ReportError> requireString(const json::Value& object,
                                                              std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto text = (*member)->asString();
    if (!text.has_value()) {
        return err(ReportError::MalformedReport);
    }
    return std::string{*text};
}

}  // namespace

// ---------------------------------------------------------------------------
// describe()
// ---------------------------------------------------------------------------

std::string_view describe(ReportError error) noexcept {
    switch (error) {
    case ReportError::EmptyToolName:            return "tool name is empty";
    case ReportError::EmptyToolVersion:         return "tool version is empty";
    case ReportError::EmptyPath:                return "path is empty";
    case ReportError::AbsolutePath:             return "path is absolute";
    case ReportError::BackslashInPath:          return "path contains a backslash";
    case ReportError::ParentDirectoryInPath:    return "path contains a '..' component";
    case ReportError::EmptyId:                  return "package id is empty";
    case ReportError::EmptyKind:                return "package kind is empty";
    case ReportError::NoOutputs:                return "report lists no outputs";
    case ReportError::DuplicateOutputPath:      return "report lists an output path twice";
    case ReportError::UnsupportedSchemaVersion: return "unsupported schema version";
    case ReportError::MalformedReport:          return "report JSON has an unexpected shape";
    }
    return "unrecognized report error";
}

// ---------------------------------------------------------------------------
// digestFromHex()
// ---------------------------------------------------------------------------

Result<Digest, ReportError> digestFromHex(std::string_view hex) noexcept {
    if (hex.size() != 64) {
        return err(ReportError::MalformedReport);
    }
    Digest digest{};
    for (std::size_t i = 0; i < 32; ++i) {
        std::uint8_t byte = 0;
        for (std::size_t nibble = 0; nibble < 2; ++nibble) {
            const char c = hex[i * 2 + nibble];
            std::uint8_t value = 0;
            if (c >= '0' && c <= '9') {
                value = static_cast<std::uint8_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value = static_cast<std::uint8_t>(c - 'a' + 10);
            } else {
                // Uppercase is rejected rather than accepted: toHex() emits lowercase, so an
                // uppercase digit means the file was not written by this pipeline.
                return err(ReportError::MalformedReport);
            }
            byte = static_cast<std::uint8_t>((byte << 4) | value);
        }
        digest[i] = byte;
    }
    return digest;
}

// ---------------------------------------------------------------------------
// PackageHeader
// ---------------------------------------------------------------------------

ResultVoid<ReportError> PackageHeader::validate() const noexcept {
    if (schemaVersion != kSchemaVersion) {
        return err(ReportError::UnsupportedSchemaVersion);
    }
    if (id.empty()) {
        return err(ReportError::EmptyId);
    }
    if (kind.empty()) {
        return err(ReportError::EmptyKind);
    }
    return {};
}

ResultVoid<json::Error> PackageHeader::writeInto(json::Value& object) const noexcept {
    if (auto set = object.set("schemaVersion", json::Value::unsignedInteger(schemaVersion));
        !set.has_value()) {
        return set;
    }
    if (auto set = object.set("id", json::Value::string(id)); !set.has_value()) {
        return set;
    }
    if (auto set = object.set("kind", json::Value::string(kind)); !set.has_value()) {
        return set;
    }
    return {};
}

Result<PackageHeader, ReportError> PackageHeader::readFrom(const json::Value& object) noexcept {
    const auto version = object.require("schemaVersion");
    if (!version.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto versionValue = (*version)->asUInt();
    if (!versionValue.has_value()) {
        return err(ReportError::MalformedReport);
    }
    if (*versionValue != kSchemaVersion) {
        return err(ReportError::UnsupportedSchemaVersion);
    }

    auto id = requireString(object, "id");
    if (!id.has_value()) {
        return err(id.error());
    }
    auto kind = requireString(object, "kind");
    if (!kind.has_value()) {
        return err(kind.error());
    }

    PackageHeader header{
        .schemaVersion = *versionValue, .id = std::move(*id), .kind = std::move(*kind)};
    if (auto valid = header.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return header;
}

// ---------------------------------------------------------------------------
// BakeReport
// ---------------------------------------------------------------------------

ResultVoid<ReportError> BakeReport::validate() const noexcept {
    if (schemaVersion != kSchemaVersion) {
        return err(ReportError::UnsupportedSchemaVersion);
    }
    if (tool.empty()) {
        return err(ReportError::EmptyToolName);
    }
    if (toolVersion.empty()) {
        return err(ReportError::EmptyToolVersion);
    }
    if (auto valid = validateFileRecord(recipe); !valid.has_value()) {
        return valid;
    }
    for (const FileRecord& input : inputs) {
        if (auto valid = validateFileRecord(input); !valid.has_value()) {
            return valid;
        }
    }
    if (outputs.empty()) {
        // A baker that wrote nothing has nothing to verify, so a report with no outputs would
        // make an empty verification trivially pass.
        return err(ReportError::NoOutputs);
    }
    for (const FileRecord& output : outputs) {
        if (auto valid = validateFileRecord(output); !valid.has_value()) {
            return valid;
        }
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        for (std::size_t k = i + 1; k < outputs.size(); ++k) {
            if (outputs[i].path == outputs[k].path) {
                // Two records for one path means one of the two digests is wrong, and which one
                // a verifier picks would decide whether the check passes.
                return err(ReportError::DuplicateOutputPath);
            }
        }
    }
    return {};
}

Result<json::Value, ReportError> BakeReport::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }

    json::Value object = json::Value::emptyObject();

    // Every set() below is on a distinct literal key of a freshly-created object, so a
    // DuplicateKey is impossible; the errors are still checked rather than discarded, because a
    // future edit that repeats a key should fail loudly instead of silently dropping a field.
    auto setMember = [&object](std::string key, json::Value value) -> bool {
        return object.set(std::move(key), std::move(value)).has_value();
    };

    if (!setMember("schemaVersion", json::Value::unsignedInteger(schemaVersion)) ||
        !setMember("tool", json::Value::string(tool)) ||
        !setMember("toolVersion", json::Value::string(toolVersion))) {
        return err(ReportError::MalformedReport);
    }

    auto recipeJson = fileRecordToJson(recipe);
    if (!recipeJson.has_value() || !setMember("recipe", std::move(*recipeJson))) {
        return err(ReportError::MalformedReport);
    }

    auto inputsJson = fileRecordsToJson(inputs);
    if (!inputsJson.has_value() || !setMember("inputs", std::move(*inputsJson))) {
        return err(ReportError::MalformedReport);
    }

    // An options object the baker never populated is written as {} rather than as null, so the
    // member is always present and always the same kind - a reader needs no special case.
    json::Value resolvedOptions =
        options.kind() == json::Value::Kind::Object ? options : json::Value::emptyObject();
    if (!setMember("options", std::move(resolvedOptions))) {
        return err(ReportError::MalformedReport);
    }

    auto outputsJson = fileRecordsToJson(outputs);
    if (!outputsJson.has_value() || !setMember("outputs", std::move(*outputsJson))) {
        return err(ReportError::MalformedReport);
    }

    return object;
}

Result<std::string, ReportError> BakeReport::write() const noexcept {
    auto object = toJson();
    if (!object.has_value()) {
        return err(object.error());
    }
    auto text = json::write(*object);
    if (!text.has_value()) {
        return err(ReportError::MalformedReport);
    }
    return *text;
}

Result<BakeReport, ReportError> BakeReport::parse(std::string_view text) noexcept {
    const auto document = json::parse(text);
    if (!document.has_value()) {
        return err(ReportError::MalformedReport);
    }
    if (document->kind() != json::Value::Kind::Object) {
        return err(ReportError::MalformedReport);
    }

    const auto version = document->require("schemaVersion");
    if (!version.has_value()) {
        return err(ReportError::MalformedReport);
    }
    const auto versionValue = (*version)->asUInt();
    if (!versionValue.has_value()) {
        return err(ReportError::MalformedReport);
    }
    if (*versionValue != kSchemaVersion) {
        return err(ReportError::UnsupportedSchemaVersion);
    }

    BakeReport report;
    report.schemaVersion = *versionValue;

    for (const auto& [key, target] : std::initializer_list<
             std::pair<std::string_view, std::string*>>{{"tool", &report.tool},
                                                         {"toolVersion", &report.toolVersion}}) {
        auto value = requireString(*document, key);
        if (!value.has_value()) {
            return err(value.error());
        }
        *target = std::move(*value);
    }

    const auto recipeJson = document->require("recipe");
    if (!recipeJson.has_value()) {
        return err(ReportError::MalformedReport);
    }
    auto recipe = fileRecordFromJson(**recipeJson);
    if (!recipe.has_value()) {
        return err(recipe.error());
    }
    report.recipe = std::move(*recipe);

    const auto inputsJson = document->require("inputs");
    if (!inputsJson.has_value()) {
        return err(ReportError::MalformedReport);
    }
    auto inputs = fileRecordsFromJson(**inputsJson);
    if (!inputs.has_value()) {
        return err(inputs.error());
    }
    report.inputs = std::move(*inputs);

    const auto optionsJson = document->require("options");
    if (!optionsJson.has_value()) {
        return err(ReportError::MalformedReport);
    }
    if ((*optionsJson)->kind() != json::Value::Kind::Object) {
        return err(ReportError::MalformedReport);
    }
    report.options = **optionsJson;

    const auto outputsJson = document->require("outputs");
    if (!outputsJson.has_value()) {
        return err(ReportError::MalformedReport);
    }
    auto outputs = fileRecordsFromJson(**outputsJson);
    if (!outputs.has_value()) {
        return err(outputs.error());
    }
    report.outputs = std::move(*outputs);

    if (auto valid = report.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return report;
}

}  // namespace mdux::evidence
