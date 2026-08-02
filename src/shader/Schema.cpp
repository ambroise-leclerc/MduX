/**
 * @brief Implementation of the governed shader package types.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Every member written here goes through mdux.evidence.json's canonical writer, so a package's
 * bytes are identical on MSVC, libstdc++ and libc++. Note there is not a single floating-point
 * value in the format: a shader contract is offsets, sizes and identifiers, so the `{"bits": N}`
 * float encoding ADR-007 requires never comes up here.
 */
module;

module mdux.shader.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

namespace mdux::shader {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = mdux::evidence::json;

namespace {

/// Reads a required unsigned member that must fit in 32 bits.
[[nodiscard]] Result<std::uint32_t, SchemaError> requireUint32(const json::Value& object,
                                                               std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    const auto value = (*member)->asUInt();
    if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
        return err(SchemaError::MalformedPackage);
    }
    return static_cast<std::uint32_t>(*value);
}

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
    case SchemaError::WrongKind:            return "package kind is not \"shader\"";
    case SchemaError::EmptySidecarPath:     return "sidecar path is empty";
    case SchemaError::SidecarPathHasSeparator:
        return "sidecar path contains a path separator; it must be a bare filename";
    case SchemaError::NoModules:            return "package contains no shader modules";
    case SchemaError::EmptyModuleId:        return "module id is empty";
    case SchemaError::DuplicateModuleId:    return "two modules share an id";
    case SchemaError::EmptyEntryPoint:      return "module entry point name is empty";
    case SchemaError::EmptyModule:          return "module byte length is zero";
    case SchemaError::UnalignedModule:
        return "module byte offset or length is not a multiple of 4";
    case SchemaError::ModuleOutOfBounds:    return "module range extends past the sidecar";
    case SchemaError::OverlappingModules:   return "two module ranges overlap";
    case SchemaError::NoStages:             return "entry is visible to no stage";
    case SchemaError::ZeroDescriptorCount:  return "descriptor count is zero";
    case SchemaError::DuplicateDescriptorBinding:
        return "two descriptors share a (set, binding) pair";
    case SchemaError::EmptyPushConstantRange: return "push constant range size is zero";
    case SchemaError::UnalignedPushConstantRange:
        return "push constant offset or size is not a multiple of 4";
    case SchemaError::OverlappingPushConstants: return "two push constant ranges overlap";
    case SchemaError::UnsupportedSchemaVersion: return "unsupported schemaVersion";
    case SchemaError::UnknownStage:         return "unknown stage wire value";
    case SchemaError::UnknownDescriptorKind: return "unknown descriptor kind wire value";
    case SchemaError::MalformedPackage:     return "package JSON did not have the expected shape";
    case SchemaError::ReportRejected:       return "package header failed validation";
    }
    return "unknown shader schema error";
}

// ---------------------------------------------------------------------------
// Wire encoding
// ---------------------------------------------------------------------------

std::string_view toWire(Stage stage) noexcept {
    const auto index = static_cast<std::size_t>(stage);
    return index < stageWireValues.size() ? stageWireValues[index] : std::string_view{};
}

std::string_view toWire(DescriptorKind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < descriptorKindWireValues.size() ? descriptorKindWireValues[index]
                                                     : std::string_view{};
}

Result<Stage, SchemaError> stageFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < stageWireValues.size(); ++i) {
        if (stageWireValues[i] == wire) {
            return static_cast<Stage>(i);
        }
    }
    return err(SchemaError::UnknownStage);
}

Result<DescriptorKind, SchemaError> descriptorKindFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < descriptorKindWireValues.size(); ++i) {
        if (descriptorKindWireValues[i] == wire) {
            return static_cast<DescriptorKind>(i);
        }
    }
    return err(SchemaError::UnknownDescriptorKind);
}

json::Value stagesToJson(StageMask stages) noexcept {
    // Written in enumerator order rather than bit order so the array is stable regardless of how
    // the mask was assembled - two packages with the same stage set must produce the same bytes.
    json::Value array = json::Value::array({});
    for (std::size_t i = 0; i < stageWireValues.size(); ++i) {
        if ((stages & stageBit(static_cast<Stage>(i))) != 0) {
            // A fresh array cannot reject a push; the result is checked by the caller's writer.
            static_cast<void>(array.push(json::Value::string(std::string{stageWireValues[i]})));
        }
    }
    return array;
}

Result<StageMask, SchemaError> stagesFromJson(const json::Value& array) noexcept {
    if (array.kind() != json::Value::Kind::Array) {
        return err(SchemaError::MalformedPackage);
    }
    StageMask mask = 0;
    for (const json::Value& element : array.elements()) {
        const auto text = element.asString();
        if (!text.has_value()) {
            return err(SchemaError::MalformedPackage);
        }
        auto stage = stageFromWire(*text);
        if (!stage.has_value()) {
            return err(stage.error());
        }
        mask |= stageBit(*stage);
    }
    return mask;
}

// ---------------------------------------------------------------------------
// ShaderPackage::validate()
// ---------------------------------------------------------------------------

ResultVoid<SchemaError> ShaderPackage::validate() const noexcept {
    if (auto headerOk = header.validate(); !headerOk.has_value()) {
        return err(SchemaError::ReportRejected);
    }
    if (header.kind != kind) {
        return err(SchemaError::WrongKind);
    }
    if (header.schemaVersion != evidence::kSchemaVersion) {
        return err(SchemaError::UnsupportedSchemaVersion);
    }

    if (sidecarPath.empty()) {
        return err(SchemaError::EmptySidecarPath);
    }
    if (sidecarPath.find('/') != std::string::npos ||
        sidecarPath.find('\\') != std::string::npos) {
        return err(SchemaError::SidecarPathHasSeparator);
    }

    if (modules.empty()) {
        return err(SchemaError::NoModules);
    }

    for (std::size_t i = 0; i < modules.size(); ++i) {
        const ShaderModule& module = modules[i];
        if (module.id.empty()) {
            return err(SchemaError::EmptyModuleId);
        }
        if (module.entryPoint.empty()) {
            return err(SchemaError::EmptyEntryPoint);
        }
        if (module.byteLength == 0) {
            return err(SchemaError::EmptyModule);
        }
        if (module.byteOffset % 4 != 0 || module.byteLength % 4 != 0) {
            return err(SchemaError::UnalignedModule);
        }
        // Checked against the recorded sidecar length rather than by adding and hoping: the
        // subtraction cannot overflow, where byteOffset + byteLength could.
        if (module.byteOffset > sidecarByteLength ||
            module.byteLength > sidecarByteLength - module.byteOffset) {
            return err(SchemaError::ModuleOutOfBounds);
        }
        for (std::size_t j = i + 1; j < modules.size(); ++j) {
            if (modules[j].id == module.id) {
                return err(SchemaError::DuplicateModuleId);
            }
            if (overlaps(module.byteOffset, module.byteEnd(), modules[j].byteOffset,
                         modules[j].byteEnd())) {
                return err(SchemaError::OverlappingModules);
            }
        }
    }

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const DescriptorBinding& descriptor = descriptors[i];
        if (descriptor.stages == 0) {
            return err(SchemaError::NoStages);
        }
        if (descriptor.count == 0) {
            return err(SchemaError::ZeroDescriptorCount);
        }
        for (std::size_t j = i + 1; j < descriptors.size(); ++j) {
            if (descriptors[j].set == descriptor.set &&
                descriptors[j].binding == descriptor.binding) {
                return err(SchemaError::DuplicateDescriptorBinding);
            }
        }
    }

    for (std::size_t i = 0; i < pushConstants.size(); ++i) {
        const PushConstantRange& range = pushConstants[i];
        if (range.stages == 0) {
            return err(SchemaError::NoStages);
        }
        if (range.size == 0) {
            return err(SchemaError::EmptyPushConstantRange);
        }
        if (range.offset % 4 != 0 || range.size % 4 != 0) {
            return err(SchemaError::UnalignedPushConstantRange);
        }
        for (std::size_t j = i + 1; j < pushConstants.size(); ++j) {
            if (overlaps(range.offset, static_cast<std::uint64_t>(range.offset) + range.size,
                         pushConstants[j].offset,
                         static_cast<std::uint64_t>(pushConstants[j].offset) +
                             pushConstants[j].size)) {
                return err(SchemaError::OverlappingPushConstants);
            }
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// ShaderPackage::toJson() / write()
// ---------------------------------------------------------------------------

Result<json::Value, SchemaError> ShaderPackage::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }

    json::Value root = json::Value::emptyObject();
    if (auto written = header.writeInto(root); !written.has_value()) {
        return err(SchemaError::MalformedPackage);
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

    json::Value moduleArray = json::Value::array({});
    for (const ShaderModule& module : modules) {
        json::Value entry = json::Value::emptyObject();
        if (auto done = setMember(entry, "id", json::Value::string(module.id));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "stage",
                                  json::Value::string(std::string{toWire(module.stage)}));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "entryPoint", json::Value::string(module.entryPoint));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "byteOffset",
                                  json::Value::unsignedInteger(module.byteOffset));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "byteLength",
                                  json::Value::unsignedInteger(module.byteLength));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "sha256", digestToJson(module.sha256));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = pushElement(moduleArray, std::move(entry)); !done.has_value()) {
            return err(done.error());
        }
    }
    if (auto done = setMember(root, "modules", std::move(moduleArray)); !done.has_value()) {
        return err(done.error());
    }

    json::Value descriptorArray = json::Value::array({});
    for (const DescriptorBinding& descriptor : descriptors) {
        json::Value entry = json::Value::emptyObject();
        if (auto done = setMember(entry, "set", json::Value::unsignedInteger(descriptor.set));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done =
                setMember(entry, "binding", json::Value::unsignedInteger(descriptor.binding));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "kind",
                                  json::Value::string(std::string{toWire(descriptor.kind)}));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "count", json::Value::unsignedInteger(descriptor.count));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "stages", stagesToJson(descriptor.stages));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = pushElement(descriptorArray, std::move(entry)); !done.has_value()) {
            return err(done.error());
        }
    }
    if (auto done = setMember(root, "descriptors", std::move(descriptorArray));
        !done.has_value()) {
        return err(done.error());
    }

    json::Value pushArray = json::Value::array({});
    for (const PushConstantRange& range : pushConstants) {
        json::Value entry = json::Value::emptyObject();
        if (auto done = setMember(entry, "offset", json::Value::unsignedInteger(range.offset));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "size", json::Value::unsignedInteger(range.size));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = setMember(entry, "stages", stagesToJson(range.stages));
            !done.has_value()) {
            return err(done.error());
        }
        if (auto done = pushElement(pushArray, std::move(entry)); !done.has_value()) {
            return err(done.error());
        }
    }
    if (auto done = setMember(root, "pushConstants", std::move(pushArray)); !done.has_value()) {
        return err(done.error());
    }

    return root;
}

Result<std::string, SchemaError> ShaderPackage::write() const noexcept {
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

// ---------------------------------------------------------------------------
// ShaderPackage::parse()
// ---------------------------------------------------------------------------

Result<ShaderPackage, SchemaError> ShaderPackage::parse(std::string_view text) noexcept {
    auto document = json::parse(text);
    if (!document.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    if (document->kind() != json::Value::Kind::Object) {
        return err(SchemaError::MalformedPackage);
    }

    ShaderPackage package;

    auto header = evidence::PackageHeader::readFrom(*document);
    if (!header.has_value()) {
        return err(SchemaError::MalformedPackage);
    }
    package.header = std::move(*header);

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

    const auto moduleArray = document->require("modules");
    if (!moduleArray.has_value() || (*moduleArray)->kind() != json::Value::Kind::Array) {
        return err(SchemaError::MalformedPackage);
    }
    for (const json::Value& entry : (*moduleArray)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::MalformedPackage);
        }
        ShaderModule module;
        auto id = requireString(entry, "id");
        if (!id.has_value()) {
            return err(id.error());
        }
        module.id = std::move(*id);
        auto stageText = requireString(entry, "stage");
        if (!stageText.has_value()) {
            return err(stageText.error());
        }
        auto stage = stageFromWire(*stageText);
        if (!stage.has_value()) {
            return err(stage.error());
        }
        module.stage = *stage;
        auto entryPoint = requireString(entry, "entryPoint");
        if (!entryPoint.has_value()) {
            return err(entryPoint.error());
        }
        module.entryPoint = std::move(*entryPoint);
        auto offset = requireUint64(entry, "byteOffset");
        if (!offset.has_value()) {
            return err(offset.error());
        }
        module.byteOffset = *offset;
        auto length = requireUint64(entry, "byteLength");
        if (!length.has_value()) {
            return err(length.error());
        }
        module.byteLength = *length;
        auto digest = requireDigest(entry, "sha256");
        if (!digest.has_value()) {
            return err(digest.error());
        }
        module.sha256 = *digest;
        package.modules.push_back(std::move(module));
    }

    const auto descriptorArray = document->require("descriptors");
    if (!descriptorArray.has_value() || (*descriptorArray)->kind() != json::Value::Kind::Array) {
        return err(SchemaError::MalformedPackage);
    }
    for (const json::Value& entry : (*descriptorArray)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::MalformedPackage);
        }
        DescriptorBinding descriptor;
        auto set = requireUint32(entry, "set");
        if (!set.has_value()) {
            return err(set.error());
        }
        descriptor.set = *set;
        auto binding = requireUint32(entry, "binding");
        if (!binding.has_value()) {
            return err(binding.error());
        }
        descriptor.binding = *binding;
        auto kindText = requireString(entry, "kind");
        if (!kindText.has_value()) {
            return err(kindText.error());
        }
        auto kind = descriptorKindFromWire(*kindText);
        if (!kind.has_value()) {
            return err(kind.error());
        }
        descriptor.kind = *kind;
        auto count = requireUint32(entry, "count");
        if (!count.has_value()) {
            return err(count.error());
        }
        descriptor.count = *count;
        const auto stagesValue = entry.require("stages");
        if (!stagesValue.has_value()) {
            return err(SchemaError::MalformedPackage);
        }
        auto stages = stagesFromJson(**stagesValue);
        if (!stages.has_value()) {
            return err(stages.error());
        }
        descriptor.stages = *stages;
        package.descriptors.push_back(descriptor);
    }

    const auto pushArray = document->require("pushConstants");
    if (!pushArray.has_value() || (*pushArray)->kind() != json::Value::Kind::Array) {
        return err(SchemaError::MalformedPackage);
    }
    for (const json::Value& entry : (*pushArray)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::MalformedPackage);
        }
        PushConstantRange range;
        auto offset = requireUint32(entry, "offset");
        if (!offset.has_value()) {
            return err(offset.error());
        }
        range.offset = *offset;
        auto size = requireUint32(entry, "size");
        if (!size.has_value()) {
            return err(size.error());
        }
        range.size = *size;
        const auto stagesValue = entry.require("stages");
        if (!stagesValue.has_value()) {
            return err(SchemaError::MalformedPackage);
        }
        auto stages = stagesFromJson(**stagesValue);
        if (!stages.has_value()) {
            return err(stages.error());
        }
        range.stages = *stages;
        package.pushConstants.push_back(range);
    }

    if (auto valid = package.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return package;
}

const ShaderModule* ShaderPackage::find(std::string_view id) const noexcept {
    for (const ShaderModule& module : modules) {
        if (module.id == id) {
            return &module;
        }
    }
    return nullptr;
}

}  // namespace mdux::shader
