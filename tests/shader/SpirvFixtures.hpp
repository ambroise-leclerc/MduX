/**
 * @file SpirvFixtures.hpp
 * @brief A SPIR-V assembler for tests, shared by the reflector and baker suites.
 *
 * Modules are built word by word rather than loaded from committed `.spv` files. That costs this
 * header, and buys three things worth more: a malformed case can be constructed exactly - a
 * truncated instruction is one word count away from a valid one - the tests need no shader
 * compiler to run, and there is no binary in the tree whose provenance a reviewer has to take on
 * trust.
 *
 * A header rather than a module because it is test scaffolding, in the same spirit as
 * MduXTest.hpp: include it after `import std;`.
 */
#pragma once

namespace mdux::test::spirv {

// Opcodes and enumerants used by the fixtures, from the SPIR-V specification.
inline constexpr std::uint16_t kOpEntryPoint = 15;
inline constexpr std::uint16_t kOpTypeFloat = 22;
inline constexpr std::uint16_t kOpTypeVector = 23;
inline constexpr std::uint16_t kOpTypeImage = 25;
inline constexpr std::uint16_t kOpTypeSampledImage = 27;
inline constexpr std::uint16_t kOpTypeArray = 28;
inline constexpr std::uint16_t kOpTypeStruct = 30;
inline constexpr std::uint16_t kOpTypePointer = 32;
inline constexpr std::uint16_t kOpConstant = 43;
inline constexpr std::uint16_t kOpVariable = 59;
inline constexpr std::uint16_t kOpDecorate = 71;
inline constexpr std::uint16_t kOpMemberDecorate = 72;

inline constexpr std::uint32_t kExecutionModelVertex = 0;
inline constexpr std::uint32_t kExecutionModelFragment = 4;
inline constexpr std::uint32_t kExecutionModelGLCompute = 5;

inline constexpr std::uint32_t kStorageClassUniformConstant = 0;
inline constexpr std::uint32_t kStorageClassUniform = 2;
inline constexpr std::uint32_t kStorageClassPushConstant = 9;

inline constexpr std::uint32_t kDecorationBlock = 2;
inline constexpr std::uint32_t kDecorationBinding = 33;
inline constexpr std::uint32_t kDecorationDescriptorSet = 34;
inline constexpr std::uint32_t kDecorationOffset = 35;

/// Assembles a SPIR-V module word by word.
class Builder {
public:
    Builder()
        : words_{0x07230203u,  // magic
                 0x00010300u,  // version 1.3
                 0x00000000u,  // generator
                 200u,         // bound
                 0u} {}        // reserved

    void op(std::uint16_t opcode, std::span<const std::uint32_t> operands) {
        words_.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) | opcode);
        words_.insert(words_.end(), operands.begin(), operands.end());
    }

    void op(std::uint16_t opcode, std::initializer_list<std::uint32_t> operands) {
        op(opcode, std::span<const std::uint32_t>{operands.begin(), operands.size()});
    }

    /// An instruction whose trailing operand is a SPIR-V literal string.
    void opWithName(std::uint16_t opcode, std::initializer_list<std::uint32_t> leading,
                    std::string_view name) {
        std::vector<std::uint32_t> operands{leading};
        std::uint32_t word = 0;
        int shift = 0;
        for (const char character : name) {
            word |= static_cast<std::uint32_t>(static_cast<unsigned char>(character)) << shift;
            shift += 8;
            if (shift == 32) {
                operands.push_back(word);
                word = 0;
                shift = 0;
            }
        }
        // The terminating NUL is the zero already sitting in `word`; a name whose length is a
        // multiple of four still needs a whole word for it.
        operands.push_back(word);
        op(opcode, std::span<const std::uint32_t>{operands});
    }

    /// Overrides the word at `index`, for building a deliberately malformed module.
    void poke(std::size_t index, std::uint32_t value) { words_.at(index) = value; }

    [[nodiscard]] std::vector<std::byte> bytes() const {
        std::vector<std::byte> out;
        out.reserve(words_.size() * 4);
        for (const std::uint32_t word : words_) {
            out.push_back(static_cast<std::byte>(word & 0xffu));
            out.push_back(static_cast<std::byte>((word >> 8) & 0xffu));
            out.push_back(static_cast<std::byte>((word >> 16) & 0xffu));
            out.push_back(static_cast<std::byte>((word >> 24) & 0xffu));
        }
        return out;
    }

private:
    std::vector<std::uint32_t> words_;
};

/// A module declaring only its entry point - the smallest thing that reflects.
[[nodiscard]] inline Builder minimal(std::uint32_t executionModel = kExecutionModelVertex,
                                     std::string_view entryPoint = "main") {
    Builder builder;
    builder.opWithName(kOpEntryPoint, {executionModel, 1u}, entryPoint);
    return builder;
}

/// Declares a combined image sampler at (set, binding), optionally as an array of `count`.
inline void addCombinedImageSampler(Builder& builder, std::uint32_t set, std::uint32_t binding,
                                    std::uint32_t count = 1) {
    constexpr std::uint32_t kImage = 10;
    constexpr std::uint32_t kSampled = 11;
    constexpr std::uint32_t kArray = 12;
    constexpr std::uint32_t kPointer = 13;
    constexpr std::uint32_t kVariable = 14;
    constexpr std::uint32_t kLengthType = 15;
    constexpr std::uint32_t kLength = 16;

    // OpTypeImage: [resultId][sampledType][dim][depth][arrayed][ms][sampled][format]
    builder.op(kOpTypeImage, {kImage, 0u, 1u, 0u, 0u, 0u, 1u, 0u});
    builder.op(kOpTypeSampledImage, {kSampled, kImage});

    std::uint32_t pointee = kSampled;
    if (count != 1) {
        builder.op(kOpTypeFloat, {kLengthType, 32u});
        builder.op(kOpConstant, {kLengthType, kLength, count});
        builder.op(kOpTypeArray, {kArray, kSampled, kLength});
        pointee = kArray;
    }

    builder.op(kOpTypePointer, {kPointer, kStorageClassUniformConstant, pointee});
    builder.op(kOpVariable, {kPointer, kVariable, kStorageClassUniformConstant});
    builder.op(kOpDecorate, {kVariable, kDecorationDescriptorSet, set});
    builder.op(kOpDecorate, {kVariable, kDecorationBinding, binding});
}

/// Declares a push-constant block holding a single vec4, at offset 0.
inline void addVec4PushConstant(Builder& builder) {
    constexpr std::uint32_t kFloat = 20;
    constexpr std::uint32_t kVec4 = 21;
    constexpr std::uint32_t kStruct = 22;
    constexpr std::uint32_t kPointer = 23;
    constexpr std::uint32_t kVariable = 24;

    builder.op(kOpTypeFloat, {kFloat, 32u});
    builder.op(kOpTypeVector, {kVec4, kFloat, 4u});
    builder.op(kOpTypeStruct, {kStruct, kVec4});
    builder.op(kOpMemberDecorate, {kStruct, 0u, kDecorationOffset, 0u});
    builder.op(kOpTypePointer, {kPointer, kStorageClassPushConstant, kStruct});
    builder.op(kOpVariable, {kPointer, kVariable, kStorageClassPushConstant});
}

}  // namespace mdux::test::spirv
