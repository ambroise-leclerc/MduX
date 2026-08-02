/**
 * @brief Governed-zone shader package types: the canonical shape of every shader `package.json`.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Part of MduXCore. This module is canonical: `mdux-shaderbake` (issue #119), the generated-code
 * emitter (#121) and the renderer (#124) all import it rather than restating its records. That is
 * the Wave 2 lesson applied - two files describing the same artifact will disagree eventually, and
 * the disagreement surfaces as a byte-comparison failure nobody can localise.
 *
 * Note there are no Vulkan types here, deliberately. `DescriptorKind` and `Stage` are MduX's own
 * enumerations with their own wire spellings, translated to `VkDescriptorType` and
 * `VkShaderStageFlagBits` in the adapter zone. A governed module that named a Vk type could not
 * be linked into MduXCore at all - `mdux_verify_trust_zones()` fails the configure step - and the
 * package format would become unreadable by any tool that does not have the Vulkan headers.
 *
 * A shader package looks like this:
 *
 * ```json
 * {
 *   "schemaVersion": 1,
 *   "id": "mdux-ui",
 *   "kind": "shader",
 *   "sidecar":   { "path": "shaders.spv", "byteLength": 4096, "sha256": "…" },
 *   "modules": [
 *     { "id": "ui.vert", "stage": "vertex", "entryPoint": "main",
 *       "byteOffset": 0, "byteLength": 2048, "sha256": "…" },
 *     { "id": "ui.frag", "stage": "fragment", "entryPoint": "main",
 *       "byteOffset": 2048, "byteLength": 2048, "sha256": "…" }
 *   ],
 *   "descriptors":   [ { "set": 0, "binding": 0, "kind": "combinedImageSampler",
 *                        "count": 1, "stages": ["fragment"] } ],
 *   "pushConstants": [ { "offset": 0, "size": 16, "stages": ["vertex", "fragment"] } ]
 * }
 * ```
 *
 * ## One sidecar, not one file per module
 *
 * Every module's SPIR-V lives in a single binary sidecar addressed by `byteOffset`/`byteLength`,
 * rather than in a file each. The package then has exactly one payload whose digest can be checked
 * in one read, and the emitter (#121) produces one C array rather than one per stage. Per-module
 * digests are still recorded, so a corrupted range is attributable to the module it broke.
 *
 * ## Why the offsets are validated so aggressively
 *
 * `validate()` rejects overlapping module ranges, ranges past the end of the sidecar, and byte
 * lengths that are not a multiple of 4. A SPIR-V module is a sequence of 32-bit words; a length
 * that is not word-aligned cannot be one, and an overlap means two modules would report different
 * digests for bytes they share. These are cheap to check here and produce an actionable
 * diagnostic, where the same mistake reaching `vkCreateShaderModule` produces a device lost.
 */
module;

export module mdux.shader.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

export namespace mdux::shader {

/// The `<kind>` component of `generated/<kind>/<id>/`, and the value of a package's `kind` member.
///
/// Named packageKind rather than kind: MSVC raises C4459 ("declaration hides global declaration")
/// wherever a parameter is called `kind`, which for a type named DescriptorKind is most of them,
/// and warnings are errors here.
inline constexpr std::string_view packageKind = "shader";

/// The pipeline stage a module is compiled for. v1 is a graphics pipeline with two stages;
/// compute and the geometry stages are out of scope and need a schema version, not a quiet
/// enumerator.
enum class Stage : std::uint8_t { Vertex, Fragment };

/// Wire spellings for Stage. Order is load-bearing: an enumerator's numeric value is its index.
inline constexpr std::array<std::string_view, 2> stageWireValues{"vertex", "fragment"};

/// A set of stages, as a bitmask of `1u << static_cast<std::uint8_t>(Stage)`.
using StageMask = std::uint8_t;

inline constexpr StageMask vertexBit = 1u << 0;
inline constexpr StageMask fragmentBit = 1u << 1;
inline constexpr StageMask allStages = vertexBit | fragmentBit;

[[nodiscard]] constexpr StageMask stageBit(Stage stage) noexcept {
    return static_cast<StageMask>(1u << static_cast<std::uint8_t>(stage));
}

/// What a descriptor binding provides. MduX's own vocabulary, translated to `VkDescriptorType`
/// in the adapter zone - see the module comment for why this module names no Vulkan type.
enum class DescriptorKind : std::uint8_t {
    UniformBuffer,
    StorageBuffer,
    CombinedImageSampler,
    SampledImage,
    Sampler,
};

/// Wire spellings for DescriptorKind. Order is load-bearing, as for stageWireValues.
inline constexpr std::array<std::string_view, 5> descriptorKindWireValues{
    "uniformBuffer", "storageBuffer", "combinedImageSampler", "sampledImage", "sampler"};

enum class SchemaError : std::uint8_t {
    WrongKind,                  ///< `kind` is not "shader"
    EmptySidecarPath,
    SidecarPathHasSeparator,    ///< a sidecar sits beside package.json; it is a bare filename
    NoModules,
    EmptyModuleId,
    DuplicateModuleId,
    EmptyEntryPoint,
    EmptyModule,                ///< a module with a zero byte length is not a SPIR-V module
    UnalignedModule,            ///< byte offset or length is not a multiple of 4
    ModuleOutOfBounds,          ///< the range extends past the end of the sidecar
    OverlappingModules,
    NoStages,                   ///< a descriptor or push-constant range visible to no stage
    ZeroDescriptorCount,
    DuplicateDescriptorBinding, ///< two bindings with the same (set, binding)
    EmptyPushConstantRange,
    UnalignedPushConstantRange, ///< offset or size is not a multiple of 4
    OverlappingPushConstants,
    UnsupportedSchemaVersion,
    UnknownStage,               ///< a wire value outside stageWireValues
    UnknownDescriptorKind,
    MalformedPackage,           ///< parsed JSON did not have the expected shape
    ReportRejected,             ///< the embedded PackageHeader failed its own validate()
};

[[nodiscard]] std::string_view describe(SchemaError error) noexcept;

/// One descriptor a pipeline built from this package expects to be bound.
struct DescriptorBinding {
    std::uint32_t set{0};
    std::uint32_t binding{0};
    DescriptorKind kind{DescriptorKind::UniformBuffer};
    std::uint32_t count{1};  ///< array size; 1 for a non-array binding
    StageMask stages{0};

    [[nodiscard]] bool operator==(const DescriptorBinding&) const noexcept = default;
};

/// A push-constant range a pipeline built from this package expects to be provided.
struct PushConstantRange {
    std::uint32_t offset{0};
    std::uint32_t size{0};
    StageMask stages{0};

    [[nodiscard]] bool operator==(const PushConstantRange&) const noexcept = default;
};

/// One compiled SPIR-V module, addressed as a range of the package's binary sidecar.
struct ShaderModule {
    std::string id;          ///< stable within a package, e.g. "ui.vert"
    Stage stage{Stage::Vertex};
    std::string entryPoint;  ///< the `OpEntryPoint` name, conventionally "main"
    std::uint64_t byteOffset{0};
    std::uint64_t byteLength{0};
    evidence::Digest sha256{};

    /// One past the last byte of this module's range. Never overflows for a validated package,
    /// whose ranges are bounded by the sidecar length.
    [[nodiscard]] std::uint64_t byteEnd() const noexcept { return byteOffset + byteLength; }
};

/**
 * @brief The `package.json` a shader bake produces, and the renderer consumes.
 */
struct ShaderPackage {
    /// `kind` is defaulted so a hand-built package cannot forget it; every member is named
    /// because a partial designated initializer is a -Wmissing-field-initializers error here.
    /// `kind` is defaulted so a hand-built package cannot forget it; every member is named
    /// because a partial designated initializer is a -Wmissing-field-initializers error here.
    evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion,
                                   .id = {},
                                   .kind = std::string{packageKind}};
    std::string sidecarPath;  ///< bare filename beside package.json, e.g. "shaders.spv"
    std::uint64_t sidecarByteLength{0};
    evidence::Digest sidecarSha256{};
    std::vector<ShaderModule> modules;
    std::vector<DescriptorBinding> descriptors;
    std::vector<PushConstantRange> pushConstants;

    /// Checks every invariant a consumer is entitled to assume. See the module comment for why
    /// the range checks in particular are worth their cost.
    [[nodiscard]] mdux::core::ResultVoid<SchemaError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<evidence::json::Value, SchemaError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, SchemaError> write() const noexcept;

    /// Parses canonical `package.json` text. Strict: rejects a malformed shape, an unsupported
    /// schemaVersion and an unknown wire value, and validates the result.
    [[nodiscard]] static mdux::core::Result<ShaderPackage, SchemaError> parse(
        std::string_view text) noexcept;

    /// The module with this id, or nullptr. Linear: a package holds a handful of modules, and a
    /// map would cost more to build than every lookup it could ever serve.
    [[nodiscard]] const ShaderModule* find(std::string_view id) const noexcept;
};

/// Wire encoding helpers, exported because the emitter (#121) and the tests need the same
/// spellings the writer uses, and a second copy of them is a second thing to get wrong.
[[nodiscard]] std::string_view toWire(Stage stage) noexcept;
[[nodiscard]] std::string_view toWire(DescriptorKind kind) noexcept;
[[nodiscard]] mdux::core::Result<Stage, SchemaError> stageFromWire(std::string_view wire) noexcept;
[[nodiscard]] mdux::core::Result<DescriptorKind, SchemaError> descriptorKindFromWire(
    std::string_view wire) noexcept;

/// A stage mask as its canonical JSON array of wire spellings, in enumerator order.
[[nodiscard]] evidence::json::Value stagesToJson(StageMask stages) noexcept;
[[nodiscard]] mdux::core::Result<StageMask, SchemaError> stagesFromJson(
    const evidence::json::Value& array) noexcept;

}  // namespace mdux::shader
