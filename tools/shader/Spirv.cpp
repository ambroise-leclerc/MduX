/**
 * @file Spirv.cpp
 * @brief Implementation of the host-only SPIR-V reflector.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Two passes. The first indexes every instruction that defines or decorates an id - types,
 * variables, constants, decorations - because SPIR-V permits forward references and a single pass
 * would have to guess. The second interprets that index.
 *
 * Every read goes through `wordAt()`, which is bounds-checked against the module length, and every
 * instruction's declared word count is checked against the words remaining before its operands are
 * touched. That is the one invariant worth stating plainly: this tool is normally fed files a
 * build produced, but it is also the first thing a malformed or truncated file reaches, and it
 * must fail with a diagnostic rather than read past its buffer.
 */
module;

module mdux.tools.spirv;

import std;
import mdux.core.result;
import mdux.shader.schema;

namespace mdux::tools::spirv {

using mdux::core::err;
using mdux::core::Result;

namespace {

constexpr std::uint32_t kMagic = 0x07230203u;
constexpr std::uint32_t kMagicSwapped = 0x03022307u;
constexpr std::size_t kHeaderWords = 5;

// Opcodes, from the SPIR-V specification's instruction table. Only the ones a contract needs.
enum Op : std::uint16_t {
    OpEntryPoint = 15,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeMatrix = 24,
    OpTypeImage = 25,
    OpTypeSampler = 26,
    OpTypeSampledImage = 27,
    OpTypeArray = 28,
    OpTypeRuntimeArray = 29,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpConstant = 43,
    OpVariable = 59,
    OpDecorate = 71,
    OpMemberDecorate = 72,
};

enum ExecutionModel : std::uint32_t {
    ExecutionModelVertex = 0,
    ExecutionModelFragment = 4,
};

enum StorageClass : std::uint32_t {
    StorageClassUniformConstant = 0,
    StorageClassUniform = 2,
    StorageClassPushConstant = 9,
    StorageClassStorageBuffer = 12,
};

enum Decoration : std::uint32_t {
    DecorationBlock = 2,
    DecorationBufferBlock = 3,
    DecorationArrayStride = 6,
    DecorationBinding = 33,
    DecorationDescriptorSet = 34,
    DecorationOffset = 35,
};

/// A type definition, indexed by result id.
struct TypeDef {
    std::uint16_t opcode{0};
    std::span<const std::uint32_t> operands;
};

struct VariableDef {
    std::uint32_t typeId{0};       ///< the OpTypePointer result id
    std::uint32_t storageClass{0};
};

struct Decorations {
    std::optional<std::uint32_t> descriptorSet;
    std::optional<std::uint32_t> binding;
    std::optional<std::uint32_t> arrayStride;
    bool block{false};
    bool bufferBlock{false};
};

/// Per-(struct id, member index) byte offsets, from OpMemberDecorate Offset.
using MemberOffsets = std::map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>>;

/// Reads a little-endian 32-bit word at `wordIndex`, or nullopt when out of range.
[[nodiscard]] std::optional<std::uint32_t> wordAt(std::span<const std::byte> bytes,
                                                  std::size_t wordIndex) noexcept {
    const std::size_t offset = wordIndex * 4;
    if (offset + 4 > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24);
}

/// Decodes a SPIR-V literal string: NUL-terminated UTF-8 packed four bytes per word, low byte
/// first. Returns nullopt when no terminator appears before the operands run out.
[[nodiscard]] std::optional<std::string> literalString(std::span<const std::uint32_t> operands,
                                                       std::size_t& index) noexcept {
    std::string text;
    while (index < operands.size()) {
        const std::uint32_t word = operands[index++];
        for (int shift = 0; shift < 32; shift += 8) {
            const auto byte = static_cast<char>((word >> shift) & 0xffu);
            if (byte == '\0') {
                return text;
            }
            text.push_back(byte);
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view describe(ParseError error) noexcept {
    switch (error) {
    case ParseError::Empty:            return "module is empty";
    case ParseError::NotWordAligned:   return "module length is not a multiple of 4";
    case ParseError::TooShort:         return "module is shorter than a SPIR-V header";
    case ParseError::BadMagic:         return "module does not start with the SPIR-V magic number";
    case ParseError::ForeignEndianness:
        return "module is byte-swapped; only little-endian SPIR-V is accepted";
    case ParseError::UnsupportedVersion: return "unsupported SPIR-V version";
    case ParseError::ReservedSchemaNonZero:
        return "reserved header word is not zero";
    case ParseError::ZeroWordCount:    return "instruction declares a word count of zero";
    case ParseError::TruncatedInstruction:
        return "instruction extends past the end of the module";
    case ParseError::NoEntryPoint:     return "module declares no entry point";
    case ParseError::MultipleEntryPoints:
        return "module declares more than one entry point";
    case ParseError::UnsupportedExecutionModel:
        return "execution model is not vertex or fragment";
    case ParseError::EntryPointNameUnterminated:
        return "entry point name is not NUL-terminated";
    case ParseError::MissingDescriptorSet:
        return "descriptor variable has no DescriptorSet decoration";
    case ParseError::MissingBinding:   return "descriptor variable has no Binding decoration";
    case ParseError::UnknownDescriptorKind:
        return "descriptor variable has a storage class or type this tool cannot classify";
    case ParseError::UnsupportedType:  return "type size cannot be computed";
    case ParseError::MultiplePushConstantBlocks:
        return "module declares more than one push constant block";
    case ParseError::PushConstantNotAStruct:
        return "push constant variable does not point at a struct";
    case ParseError::UndeclaredId:     return "instruction references an undeclared id";
    }
    return "unknown SPIR-V parse error";
}

namespace {

/// The index built by the first pass.
struct Module {
    std::map<std::uint32_t, TypeDef> types;
    std::map<std::uint32_t, VariableDef> variables;
    std::map<std::uint32_t, std::uint32_t> constants;  ///< id -> literal value (32-bit only)
    std::map<std::uint32_t, Decorations> decorations;
    MemberOffsets memberOffsets;
    std::optional<std::uint32_t> executionModel;
    std::string entryPoint;
    std::size_t entryPointCount{0};
};

/// Byte size of a type, following the layout rules a push-constant block uses.
///
/// Struct size is derived from member Offset decorations rather than by summing member sizes:
/// the offsets are what the shader compiler actually laid out, including any padding it inserted,
/// and re-deriving them here would be re-implementing std430 badly.
[[nodiscard]] Result<std::uint32_t, ParseError> typeSize(const Module& module, std::uint32_t id,
                                                          int depth = 0) noexcept {
    if (depth > 16) {
        return err(ParseError::UnsupportedType);
    }
    const auto found = module.types.find(id);
    if (found == module.types.end()) {
        return err(ParseError::UndeclaredId);
    }
    const TypeDef& type = found->second;
    const std::span<const std::uint32_t> operands = type.operands;

    switch (type.opcode) {
    case OpTypeInt:
    case OpTypeFloat:
        // operands: [resultId][width][signedness?]
        if (operands.size() < 2) {
            return err(ParseError::UnsupportedType);
        }
        return operands[1] / 8;

    case OpTypeVector:
    case OpTypeMatrix: {
        // operands: [resultId][componentTypeId][count]
        if (operands.size() < 3) {
            return err(ParseError::UnsupportedType);
        }
        auto component = typeSize(module, operands[1], depth + 1);
        if (!component.has_value()) {
            return err(component.error());
        }
        return *component * operands[2];
    }

    case OpTypeArray: {
        // operands: [resultId][elementTypeId][lengthConstantId]
        if (operands.size() < 3) {
            return err(ParseError::UnsupportedType);
        }
        const auto length = module.constants.find(operands[2]);
        if (length == module.constants.end()) {
            return err(ParseError::UnsupportedType);
        }
        // ArrayStride is authoritative when present: it is the padded element size.
        const auto decoration = module.decorations.find(id);
        if (decoration != module.decorations.end() &&
            decoration->second.arrayStride.has_value()) {
            return *decoration->second.arrayStride * length->second;
        }
        auto element = typeSize(module, operands[1], depth + 1);
        if (!element.has_value()) {
            return err(element.error());
        }
        return *element * length->second;
    }

    case OpTypeStruct: {
        // operands: [resultId][memberTypeIds...]
        const auto offsets = module.memberOffsets.find(id);
        if (offsets == module.memberOffsets.end()) {
            return err(ParseError::UnsupportedType);
        }
        std::uint32_t end = 0;
        for (std::size_t member = 1; member < operands.size(); ++member) {
            const auto offset = offsets->second.find(static_cast<std::uint32_t>(member - 1));
            if (offset == offsets->second.end()) {
                return err(ParseError::UnsupportedType);
            }
            auto size = typeSize(module, operands[member], depth + 1);
            if (!size.has_value()) {
                return err(size.error());
            }
            end = std::max(end, offset->second + *size);
        }
        return end;
    }

    default:
        return err(ParseError::UnsupportedType);
    }
}

/// Classifies a descriptor variable from its storage class and the type it points at.
[[nodiscard]] Result<shader::DescriptorKind, ParseError> classify(
    const Module& module, std::uint32_t storageClass, std::uint32_t pointeeId) noexcept {
    // Peel an array wrapper: `uniform sampler2D tex[4]` is one binding with a count, and its
    // pointee is the array rather than the sampled image.
    std::uint32_t id = pointeeId;
    for (int depth = 0; depth < 8; ++depth) {
        const auto found = module.types.find(id);
        if (found == module.types.end()) {
            return err(ParseError::UndeclaredId);
        }
        if (found->second.opcode != OpTypeArray && found->second.opcode != OpTypeRuntimeArray) {
            break;
        }
        if (found->second.operands.size() < 2) {
            return err(ParseError::UnsupportedType);
        }
        id = found->second.operands[1];
    }

    const auto found = module.types.find(id);
    if (found == module.types.end()) {
        return err(ParseError::UndeclaredId);
    }
    const std::uint16_t opcode = found->second.opcode;

    if (storageClass == StorageClassUniformConstant) {
        switch (opcode) {
        case OpTypeSampledImage: return shader::DescriptorKind::CombinedImageSampler;
        case OpTypeImage:        return shader::DescriptorKind::SampledImage;
        case OpTypeSampler:      return shader::DescriptorKind::Sampler;
        default:                 return err(ParseError::UnknownDescriptorKind);
        }
    }

    if (opcode != OpTypeStruct) {
        return err(ParseError::UnknownDescriptorKind);
    }
    if (storageClass == StorageClassStorageBuffer) {
        return shader::DescriptorKind::StorageBuffer;
    }
    if (storageClass == StorageClassUniform) {
        // Pre-1.3 GLSL spells a storage buffer as Uniform + BufferBlock.
        const auto decoration = module.decorations.find(id);
        if (decoration != module.decorations.end() && decoration->second.bufferBlock) {
            return shader::DescriptorKind::StorageBuffer;
        }
        return shader::DescriptorKind::UniformBuffer;
    }
    return err(ParseError::UnknownDescriptorKind);
}

/// The number of elements a binding declares, peeling one array wrapper.
[[nodiscard]] Result<std::uint32_t, ParseError> bindingCount(const Module& module,
                                                              std::uint32_t pointeeId) noexcept {
    const auto found = module.types.find(pointeeId);
    if (found == module.types.end()) {
        return err(ParseError::UndeclaredId);
    }
    if (found->second.opcode != OpTypeArray) {
        return 1u;
    }
    if (found->second.operands.size() < 3) {
        return err(ParseError::UnsupportedType);
    }
    const auto length = module.constants.find(found->second.operands[2]);
    if (length == module.constants.end()) {
        return err(ParseError::UnsupportedType);
    }
    return length->second;
}

}  // namespace

Result<Reflection, ParseError> reflect(std::span<const std::byte> spirv) noexcept {
    if (spirv.empty()) {
        return err(ParseError::Empty);
    }
    if (spirv.size() % 4 != 0) {
        return err(ParseError::NotWordAligned);
    }
    if (spirv.size() / 4 < kHeaderWords) {
        return err(ParseError::TooShort);
    }

    const std::uint32_t magic = *wordAt(spirv, 0);
    if (magic == kMagicSwapped) {
        return err(ParseError::ForeignEndianness);
    }
    if (magic != kMagic) {
        return err(ParseError::BadMagic);
    }

    const std::uint32_t versionWord = *wordAt(spirv, 1);
    const std::uint32_t major = (versionWord >> 16) & 0xffu;
    const std::uint32_t minor = (versionWord >> 8) & 0xffu;
    const std::uint32_t version = (major << 8) | minor;
    if (version < kMinVersion || version > kMaxVersion) {
        return err(ParseError::UnsupportedVersion);
    }
    if (*wordAt(spirv, 4) != 0) {
        return err(ParseError::ReservedSchemaNonZero);
    }

    // Materialise the word view once; every operand span below points into it.
    const std::size_t totalWords = spirv.size() / 4;
    std::vector<std::uint32_t> words;
    words.reserve(totalWords);
    for (std::size_t i = 0; i < totalWords; ++i) {
        words.push_back(*wordAt(spirv, i));
    }

    // ---- First pass: index definitions and decorations ----
    Module module;
    for (std::size_t cursor = kHeaderWords; cursor < totalWords;) {
        const std::uint32_t header = words[cursor];
        const auto wordCount = static_cast<std::uint16_t>(header >> 16);
        const auto opcode = static_cast<std::uint16_t>(header & 0xffffu);
        if (wordCount == 0) {
            return err(ParseError::ZeroWordCount);
        }
        if (cursor + wordCount > totalWords) {
            return err(ParseError::TruncatedInstruction);
        }
        const std::span<const std::uint32_t> operands{words.data() + cursor + 1,
                                                      static_cast<std::size_t>(wordCount - 1)};

        switch (opcode) {
        case OpEntryPoint: {
            // operands: [executionModel][entryPointId][name...][interfaceIds...]
            if (operands.size() < 3) {
                return err(ParseError::TruncatedInstruction);
            }
            ++module.entryPointCount;
            if (module.entryPointCount > 1) {
                return err(ParseError::MultipleEntryPoints);
            }
            module.executionModel = operands[0];
            std::size_t index = 2;
            auto name = literalString(operands, index);
            if (!name.has_value()) {
                return err(ParseError::EntryPointNameUnterminated);
            }
            module.entryPoint = std::move(*name);
            break;
        }

        case OpTypeInt:
        case OpTypeFloat:
        case OpTypeVector:
        case OpTypeMatrix:
        case OpTypeImage:
        case OpTypeSampler:
        case OpTypeSampledImage:
        case OpTypeArray:
        case OpTypeRuntimeArray:
        case OpTypeStruct:
        case OpTypePointer:
            if (operands.empty()) {
                return err(ParseError::TruncatedInstruction);
            }
            module.types[operands[0]] = TypeDef{.opcode = opcode, .operands = operands};
            break;

        case OpConstant:
            // operands: [resultTypeId][resultId][value...]; only 32-bit literals are indexed,
            // which is every array length a shader realistically declares.
            if (operands.size() >= 3) {
                module.constants[operands[1]] = operands[2];
            }
            break;

        case OpVariable:
            // operands: [resultTypeId][resultId][storageClass][initializer?]
            if (operands.size() < 3) {
                return err(ParseError::TruncatedInstruction);
            }
            module.variables[operands[1]] =
                VariableDef{.typeId = operands[0], .storageClass = operands[2]};
            break;

        case OpDecorate: {
            // operands: [targetId][decoration][literals...]
            if (operands.size() < 2) {
                return err(ParseError::TruncatedInstruction);
            }
            Decorations& decoration = module.decorations[operands[0]];
            switch (operands[1]) {
            case DecorationBlock:       decoration.block = true; break;
            case DecorationBufferBlock: decoration.bufferBlock = true; break;
            case DecorationBinding:
                if (operands.size() < 3) {
                    return err(ParseError::TruncatedInstruction);
                }
                decoration.binding = operands[2];
                break;
            case DecorationDescriptorSet:
                if (operands.size() < 3) {
                    return err(ParseError::TruncatedInstruction);
                }
                decoration.descriptorSet = operands[2];
                break;
            case DecorationArrayStride:
                if (operands.size() < 3) {
                    return err(ParseError::TruncatedInstruction);
                }
                decoration.arrayStride = operands[2];
                break;
            default:
                break;
            }
            break;
        }

        case OpMemberDecorate: {
            // operands: [structId][memberIndex][decoration][literals...]
            if (operands.size() < 3) {
                return err(ParseError::TruncatedInstruction);
            }
            if (operands[2] == DecorationOffset) {
                if (operands.size() < 4) {
                    return err(ParseError::TruncatedInstruction);
                }
                module.memberOffsets[operands[0]][operands[1]] = operands[3];
            }
            break;
        }

        default:
            break;
        }

        cursor += wordCount;
    }

    // ---- Second pass: interpret ----
    if (module.entryPointCount == 0) {
        return err(ParseError::NoEntryPoint);
    }

    Reflection reflection;
    reflection.versionMajor = major;
    reflection.versionMinor = minor;
    reflection.entryPoint = std::move(module.entryPoint);

    switch (*module.executionModel) {
    case ExecutionModelVertex:   reflection.stage = shader::Stage::Vertex; break;
    case ExecutionModelFragment: reflection.stage = shader::Stage::Fragment; break;
    default:                     return err(ParseError::UnsupportedExecutionModel);
    }
    const shader::StageMask stageMask = shader::stageBit(reflection.stage);

    for (const auto& [id, variable] : module.variables) {
        const auto pointer = module.types.find(variable.typeId);
        if (pointer == module.types.end() || pointer->second.opcode != OpTypePointer) {
            // A function-local variable's type may legitimately not be indexed as a pointer we
            // care about; only the storage classes below are interesting.
            if (variable.storageClass != StorageClassPushConstant &&
                variable.storageClass != StorageClassUniform &&
                variable.storageClass != StorageClassUniformConstant &&
                variable.storageClass != StorageClassStorageBuffer) {
                continue;
            }
            return err(ParseError::UndeclaredId);
        }
        // OpTypePointer operands: [resultId][storageClass][pointeeTypeId]
        if (pointer->second.operands.size() < 3) {
            return err(ParseError::TruncatedInstruction);
        }
        const std::uint32_t pointeeId = pointer->second.operands[2];

        if (variable.storageClass == StorageClassPushConstant) {
            if (reflection.pushConstant.has_value()) {
                return err(ParseError::MultiplePushConstantBlocks);
            }
            const auto pointee = module.types.find(pointeeId);
            if (pointee == module.types.end()) {
                return err(ParseError::UndeclaredId);
            }
            if (pointee->second.opcode != OpTypeStruct) {
                return err(ParseError::PushConstantNotAStruct);
            }
            auto size = typeSize(module, pointeeId);
            if (!size.has_value()) {
                return err(size.error());
            }
            // The block's offset is its lowest member offset, which is 0 for every block a
            // shader compiler emits but is read rather than assumed.
            std::uint32_t offset = 0;
            const auto offsets = module.memberOffsets.find(pointeeId);
            if (offsets != module.memberOffsets.end() && !offsets->second.empty()) {
                offset = offsets->second.begin()->second;
                for (const auto& [member, memberOffset] : offsets->second) {
                    static_cast<void>(member);
                    offset = std::min(offset, memberOffset);
                }
            }
            reflection.pushConstant = shader::PushConstantRange{
                .offset = offset, .size = *size - offset, .stages = stageMask};
            continue;
        }

        if (variable.storageClass != StorageClassUniform &&
            variable.storageClass != StorageClassUniformConstant &&
            variable.storageClass != StorageClassStorageBuffer) {
            continue;
        }

        const auto decoration = module.decorations.find(id);
        if (decoration == module.decorations.end() ||
            !decoration->second.descriptorSet.has_value()) {
            return err(ParseError::MissingDescriptorSet);
        }
        if (!decoration->second.binding.has_value()) {
            return err(ParseError::MissingBinding);
        }
        auto kind = classify(module, variable.storageClass, pointeeId);
        if (!kind.has_value()) {
            return err(kind.error());
        }
        auto count = bindingCount(module, pointeeId);
        if (!count.has_value()) {
            return err(count.error());
        }
        reflection.descriptors.push_back(
            shader::DescriptorBinding{.set = *decoration->second.descriptorSet,
                                      .binding = *decoration->second.binding,
                                      .kind = *kind,
                                      .count = *count,
                                      .stages = stageMask});
    }

    // Sorted so a package's descriptor list is a function of the shaders, not of id allocation
    // order inside the compiler that produced them - which byte-identity depends on.
    std::ranges::sort(reflection.descriptors, [](const shader::DescriptorBinding& lhs,
                                                 const shader::DescriptorBinding& rhs) {
        return std::tie(lhs.set, lhs.binding) < std::tie(rhs.set, rhs.binding);
    });

    return reflection;
}

}  // namespace mdux::tools::spirv
