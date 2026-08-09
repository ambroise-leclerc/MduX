/**
 * @file Spirv.cppm
 * @brief Host-only SPIR-V reflection: enough of the binary format to state a shader's contract.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: never linked into MduXCore or MduX)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw; this returns)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## Why this is hand-written rather than linked against SPIRV-Reflect or glslang
 *
 * The same reason the TOML subset and the safetensors reader are hand-written: a dependency here
 * would be SOUP in the tool that produces every committed artifact, and its version would become
 * part of the byte-identity contract. SPIR-V's binary form is a five-word header followed by
 * length-prefixed instructions - small enough to walk directly, and walking it directly means the
 * bytes this repository commits depend on nothing but this file.
 *
 * It reads only what a shader *contract* needs: the entry point and its execution model, the
 * descriptor bindings and their kinds, and the push-constant range. It is not a validator and does
 * not attempt to be one; `spirv-val` exists and is a different job. What it does guarantee is that
 * it never reads outside the buffer it was given, and that anything it does not understand is a
 * refusal with a specific code rather than a guess.
 *
 * ## Deliberately unsupported
 *
 * A byte-swapped module (a magic number of `0x03022307`, produced on a foreign-endian host) is
 * rejected rather than swapped. Supporting it would mean the same source could bake to two
 * different committed artifacts depending on where the bake ran, which is precisely what the
 * evidence pipeline exists to make impossible. Every toolchain in CI is little-endian.
 *
 * Compute and the tessellation/geometry stages are rejected too - not because they are hard, but
 * because `mdux.shader.schema` has no `Stage` enumerator for them, and inventing one here would
 * put the schema's vocabulary in two places.
 */
module;

export module mdux.tools.spirv;

import std;
import mdux.core.result;
import mdux.shader.schema;

export namespace mdux::tools::spirv {

enum class ParseError : std::uint8_t {
    Empty,
    NotWordAligned,          ///< byte length is not a multiple of 4
    TooShort,                ///< fewer than the five header words
    BadMagic,
    ForeignEndianness,       ///< byte-swapped magic; see the module comment
    UnsupportedVersion,      ///< outside the supported SPIR-V version range
    ReservedSchemaNonZero,   ///< header word 4 is reserved and must be zero
    ZeroWordCount,           ///< an instruction claiming length 0 would not advance
    TruncatedInstruction,    ///< an instruction extends past the end of the module
    NoEntryPoint,
    MultipleEntryPoints,     ///< a module in an MduX package declares exactly one
    UnsupportedExecutionModel,
    EntryPointNameUnterminated,
    MissingDescriptorSet,    ///< a descriptor variable with no DescriptorSet decoration
    MissingBinding,
    UnknownDescriptorKind,   ///< a storage class or pointee type this tool cannot classify
    UnsupportedType,         ///< a type whose size this tool cannot compute
    MultiplePushConstantBlocks,
    PushConstantNotAStruct,
    UndeclaredId,            ///< an instruction referenced an id that was never defined
};

[[nodiscard]] std::string_view describe(ParseError error) noexcept;

/// What one SPIR-V module declares about its interface.
struct Reflection {
    shader::Stage stage{shader::Stage::Vertex};
    std::string entryPoint;
    std::uint32_t versionMajor{0};
    std::uint32_t versionMinor{0};

    /// Bindings this module uses, ordered by (set, binding). `stages` carries only this module's
    /// own bit; merging across the modules of a package is the baker's job.
    std::vector<shader::DescriptorBinding> descriptors;

    /// The module's push-constant block, if it declares one. SPIR-V permits at most one.
    std::optional<shader::PushConstantRange> pushConstant;
};

/// Walks `spirv` and returns what it declares. Reads nothing outside the span.
[[nodiscard]] mdux::core::Result<Reflection, ParseError> reflect(
    std::span<const std::byte> spirv) noexcept;

/// The lowest and highest SPIR-V version this tool accepts, as (major << 8) | minor.
inline constexpr std::uint32_t minVersion = (1u << 8) | 0u;
inline constexpr std::uint32_t maxVersion = (1u << 8) | 6u;

}  // namespace mdux::tools::spirv
