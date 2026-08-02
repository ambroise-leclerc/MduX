/**
 * @brief Tests for the host-only SPIR-V reflector.
 *
 * Fixtures come from SpirvFixtures.hpp, which assembles modules word by word - see its comment
 * for why that is better here than committing `.spv` files.
 */
import std;
import mdux.shader.schema;
import mdux.tools.spirv;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "SpirvFixtures.hpp"

namespace {

using namespace mdux::tools::spirv;
using namespace mdux::test::spirv;
namespace shader = mdux::shader;

}  // namespace

// ---------------------------------------------------------------------------
// Header validation
// ---------------------------------------------------------------------------

TEST_CASE("A minimal module reflects its stage and entry point", "evidence-unit") {
    // Guards every rejection below: if this failed they could all pass for the wrong reason.
    auto result = reflect(minimal().bytes());
    REQUIRE(result.has_value());
    CHECK(result->stage == shader::Stage::Vertex);
    CHECK(result->entryPoint == "main");
    CHECK(result->versionMajor == 1);
    CHECK(result->versionMinor == 3);
    CHECK(result->descriptors.empty());
    CHECK(!result->pushConstant.has_value());
}

TEST_CASE("An empty or misaligned module is rejected", "evidence-unit") {
    auto empty = reflect({});
    REQUIRE(!empty.has_value());
    CHECK(empty.error() == ParseError::Empty);

    const std::array<std::byte, 6> misaligned{};
    auto odd = reflect(misaligned);
    REQUIRE(!odd.has_value());
    CHECK(odd.error() == ParseError::NotWordAligned);
}

TEST_CASE("A module shorter than a header is rejected", "evidence-unit") {
    const std::array<std::byte, 8> tooShort{};
    auto result = reflect(tooShort);
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::TooShort);
}

TEST_CASE("A bad magic number is rejected", "evidence-unit") {
    Builder builder = minimal();
    builder.poke(0, 0xdeadbeefu);
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::BadMagic);
}

TEST_CASE("A byte-swapped module is refused rather than swapped", "evidence-unit") {
    // Accepting it would mean the same shader could bake to two different committed artifacts
    // depending on the endianness of the machine that ran the bake.
    Builder builder = minimal();
    builder.poke(0, 0x03022307u);
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::ForeignEndianness);
}

TEST_CASE("An unsupported SPIR-V version is rejected", "evidence-unit") {
    Builder tooNew = minimal();
    tooNew.poke(1, 0x00020000u);  // 2.0
    auto newer = reflect(tooNew.bytes());
    REQUIRE(!newer.has_value());
    CHECK(newer.error() == ParseError::UnsupportedVersion);

    Builder tooOld = minimal();
    tooOld.poke(1, 0x00000900u);  // 0.9
    auto older = reflect(tooOld.bytes());
    REQUIRE(!older.has_value());
    CHECK(older.error() == ParseError::UnsupportedVersion);
}

TEST_CASE("A non-zero reserved header word is rejected", "evidence-unit") {
    Builder builder = minimal();
    builder.poke(4, 1u);
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::ReservedSchemaNonZero);
}

// ---------------------------------------------------------------------------
// Instruction stream
// ---------------------------------------------------------------------------

TEST_CASE("An instruction claiming zero words is rejected rather than looping", "evidence-unit") {
    // A word count of zero would leave the cursor where it was; without this check the parser
    // would spin forever on a malformed file.
    Builder builder = minimal();
    builder.poke(5, opEntryPoint);  // word count 0 in the high half
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::ZeroWordCount);
}

TEST_CASE("An instruction extending past the module is rejected", "evidence-unit") {
    Builder builder = minimal();
    builder.poke(5, (99u << 16) | opEntryPoint);
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::TruncatedInstruction);
}

TEST_CASE("A module with no entry point is rejected", "evidence-unit") {
    const Builder builder;  // header only
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::NoEntryPoint);
}

TEST_CASE("A second entry point is rejected", "evidence-unit") {
    Builder builder = minimal();
    builder.opWithName(opEntryPoint, {executionModelFragment, 2u}, "other");
    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::MultipleEntryPoints);
}

TEST_CASE("An unsupported execution model is rejected", "evidence-unit") {
    // The schema has no Stage enumerator for compute, and inventing one here would put the
    // schema's vocabulary in two places.
    auto result = reflect(minimal(executionModelGLCompute).bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::UnsupportedExecutionModel);
}

TEST_CASE("A fragment module reflects as fragment", "evidence-unit") {
    auto result = reflect(minimal(executionModelFragment).bytes());
    REQUIRE(result.has_value());
    CHECK(result->stage == shader::Stage::Fragment);
}

TEST_CASE("A non-default entry point name is preserved", "evidence-unit") {
    auto result = reflect(minimal(executionModelVertex, "vertexMain").bytes());
    REQUIRE(result.has_value());
    CHECK(result->entryPoint == "vertexMain");
}

TEST_CASE("An entry point name whose length is a multiple of four decodes", "evidence-unit") {
    // The packing edge case: "abcd" fills one word exactly, so the NUL needs a word of its own.
    auto result = reflect(minimal(executionModelVertex, "abcd").bytes());
    REQUIRE(result.has_value());
    CHECK(result->entryPoint == "abcd");
}

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

TEST_CASE("A combined image sampler is reflected with its set and binding", "evidence-unit") {
    Builder builder = minimal(executionModelFragment);
    addCombinedImageSampler(builder, 0, 3);
    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->descriptors.size() == 1);
    CHECK(result->descriptors[0].set == 0);
    CHECK(result->descriptors[0].binding == 3);
    CHECK(result->descriptors[0].kind == shader::DescriptorKind::CombinedImageSampler);
    CHECK(result->descriptors[0].count == 1);
    CHECK(result->descriptors[0].stages == shader::fragmentBit);
}

TEST_CASE("An array binding reports its element count once", "evidence-unit") {
    Builder builder = minimal(executionModelFragment);
    addCombinedImageSampler(builder, 0, 0, 4);
    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->descriptors.size() == 1);
    CHECK(result->descriptors[0].count == 4);
    CHECK(result->descriptors[0].kind == shader::DescriptorKind::CombinedImageSampler);
}

TEST_CASE("A uniform block is reflected as a uniform buffer", "evidence-unit") {
    constexpr std::uint32_t floatTypeId = 30;
    constexpr std::uint32_t structTypeId = 31;
    constexpr std::uint32_t pointerTypeId = 32;
    constexpr std::uint32_t variableId = 33;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opDecorate, {structTypeId, decorationBlock});
    builder.op(opTypePointer, {pointerTypeId, storageClassUniform, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassUniform});
    builder.op(opDecorate, {variableId, decorationDescriptorSet, 0u});
    builder.op(opDecorate, {variableId, decorationBinding, 0u});

    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->descriptors.size() == 1);
    CHECK(result->descriptors[0].kind == shader::DescriptorKind::UniformBuffer);
}

TEST_CASE("A descriptor missing its set is rejected", "evidence-unit") {
    // A binding the shader author forgot to decorate cannot be placed in a pipeline layout, and
    // guessing a set of 0 would produce a layout that silently disagrees with the shader.
    constexpr std::uint32_t imageTypeId = 10;
    constexpr std::uint32_t sampledTypeId = 11;
    constexpr std::uint32_t pointerTypeId = 13;
    constexpr std::uint32_t variableId = 14;

    Builder builder = minimal(executionModelFragment);
    builder.op(opTypeImage, {imageTypeId, 0u, 1u, 0u, 0u, 0u, 1u, 0u});
    builder.op(opTypeSampledImage, {sampledTypeId, imageTypeId});
    builder.op(opTypePointer, {pointerTypeId, storageClassUniformConstant, sampledTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassUniformConstant});
    builder.op(opDecorate, {variableId, decorationBinding, 0u});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::MissingDescriptorSet);
}

TEST_CASE("A descriptor missing its binding is rejected", "evidence-unit") {
    constexpr std::uint32_t imageTypeId = 10;
    constexpr std::uint32_t sampledTypeId = 11;
    constexpr std::uint32_t pointerTypeId = 13;
    constexpr std::uint32_t variableId = 14;

    Builder builder = minimal(executionModelFragment);
    builder.op(opTypeImage, {imageTypeId, 0u, 1u, 0u, 0u, 0u, 1u, 0u});
    builder.op(opTypeSampledImage, {sampledTypeId, imageTypeId});
    builder.op(opTypePointer, {pointerTypeId, storageClassUniformConstant, sampledTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassUniformConstant});
    builder.op(opDecorate, {variableId, decorationDescriptorSet, 0u});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::MissingBinding);
}

TEST_CASE("Descriptors are ordered by set then binding", "evidence-unit") {
    // Emitted out of order so the ordering cannot pass by accident. Without the sort the committed
    // package would depend on the id allocation order inside whatever compiler produced the
    // SPIR-V, which byte-identity cannot tolerate.
    constexpr std::uint32_t floatTypeId = 40;
    constexpr std::uint32_t structTypeId = 41;
    constexpr std::uint32_t pointerTypeId = 42;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opDecorate, {structTypeId, decorationBlock});
    builder.op(opTypePointer, {pointerTypeId, storageClassUniform, structTypeId});

    struct Placement {
        std::uint32_t id;
        std::uint32_t set;
        std::uint32_t binding;
    };
    const std::array<Placement, 3> placements{{{50, 1, 0}, {51, 0, 5}, {52, 0, 1}}};
    for (const Placement& placement : placements) {
        builder.op(opVariable, {pointerTypeId, placement.id, storageClassUniform});
        builder.op(opDecorate, {placement.id, decorationDescriptorSet, placement.set});
        builder.op(opDecorate, {placement.id, decorationBinding, placement.binding});
    }

    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->descriptors.size() == 3);
    CHECK(result->descriptors[0].set == 0);
    CHECK(result->descriptors[0].binding == 1);
    CHECK(result->descriptors[1].set == 0);
    CHECK(result->descriptors[1].binding == 5);
    CHECK(result->descriptors[2].set == 1);
    CHECK(result->descriptors[2].binding == 0);
}

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------

TEST_CASE("A vec4 push constant block reflects as offset 0 size 16", "evidence-unit") {
    Builder builder = minimal();
    addVec4PushConstant(builder);
    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->pushConstant.has_value());
    CHECK(result->pushConstant->offset == 0);
    CHECK(result->pushConstant->size == 16);
    CHECK(result->pushConstant->stages == shader::vertexBit);
}

TEST_CASE("A push constant block's size follows its member offsets", "evidence-unit") {
    // Two vec4s at offsets 0 and 16 make a 32-byte block. Deriving the size from the offsets is
    // what makes any padding the shader compiler inserted come out right.
    constexpr std::uint32_t floatTypeId = 60;
    constexpr std::uint32_t vec4TypeId = 61;
    constexpr std::uint32_t structTypeId = 62;
    constexpr std::uint32_t pointerTypeId = 63;
    constexpr std::uint32_t variableId = 64;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypeVector, {vec4TypeId, floatTypeId, 4u});
    builder.op(opTypeStruct, {structTypeId, vec4TypeId, vec4TypeId});
    builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
    builder.op(opMemberDecorate, {structTypeId, 1u, decorationOffset, 16u});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(result.has_value());
    REQUIRE(result->pushConstant.has_value());
    CHECK(result->pushConstant->offset == 0);
    CHECK(result->pushConstant->size == 32);
}

TEST_CASE("A push constant variable pointing at a non-struct is rejected", "evidence-unit") {
    constexpr std::uint32_t floatTypeId = 70;
    constexpr std::uint32_t pointerTypeId = 71;
    constexpr std::uint32_t variableId = 72;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, floatTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::PushConstantNotAStruct);
}

TEST_CASE("A struct with no member offsets has no computable size", "evidence-unit") {
    constexpr std::uint32_t floatTypeId = 80;
    constexpr std::uint32_t structTypeId = 81;
    constexpr std::uint32_t pointerTypeId = 82;
    constexpr std::uint32_t variableId = 83;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::UnsupportedType);
}

TEST_CASE("A bit width that is not a whole number of bytes is rejected", "evidence-unit") {
    // SPIR-V encodes widths this reflector cannot lay out: 1-bit integers are legal, and wider
    // odd widths are legal under capabilities not implemented here. `width / 8` answers 0 for
    // the first and truncates the second, so an unusable type would otherwise be reported as a
    // plausible size and flow into offsets and stride arithmetic.
    constexpr std::uint32_t floatTypeId = 100;
    constexpr std::uint32_t structTypeId = 101;
    constexpr std::uint32_t pointerTypeId = 102;
    constexpr std::uint32_t variableId = 103;
    constexpr std::uint32_t unalignedWidth = 12;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, unalignedWidth});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::UnsupportedType);
}

TEST_CASE("A zero bit width is rejected rather than sized as empty", "evidence-unit") {
    constexpr std::uint32_t floatTypeId = 110;
    constexpr std::uint32_t structTypeId = 111;
    constexpr std::uint32_t pointerTypeId = 112;
    constexpr std::uint32_t variableId = 113;

    Builder builder = minimal();
    builder.op(opTypeFloat, {floatTypeId, 0u});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::UnsupportedType);
}

TEST_CASE("A second push constant block is rejected", "evidence-unit") {
    constexpr std::uint32_t floatTypeId = 90;
    constexpr std::uint32_t structTypeId = 91;
    constexpr std::uint32_t pointerTypeId = 92;
    constexpr std::uint32_t variableId = 93;

    Builder builder = minimal();
    addVec4PushConstant(builder);
    builder.op(opTypeFloat, {floatTypeId, 32u});
    builder.op(opTypeStruct, {structTypeId, floatTypeId});
    builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
    builder.op(opTypePointer, {pointerTypeId, storageClassPushConstant, structTypeId});
    builder.op(opVariable, {pointerTypeId, variableId, storageClassPushConstant});

    auto result = reflect(builder.bytes());
    REQUIRE(!result.has_value());
    CHECK(result.error() == ParseError::MultiplePushConstantBlocks);
}

TEST_CASE("Every ParseError has its own description", "evidence-unit") {
    constexpr std::array<ParseError, 20> all{
        ParseError::Empty,
        ParseError::NotWordAligned,
        ParseError::TooShort,
        ParseError::BadMagic,
        ParseError::ForeignEndianness,
        ParseError::UnsupportedVersion,
        ParseError::ReservedSchemaNonZero,
        ParseError::ZeroWordCount,
        ParseError::TruncatedInstruction,
        ParseError::NoEntryPoint,
        ParseError::MultipleEntryPoints,
        ParseError::UnsupportedExecutionModel,
        ParseError::EntryPointNameUnterminated,
        ParseError::MissingDescriptorSet,
        ParseError::MissingBinding,
        ParseError::UnknownDescriptorKind,
        ParseError::UnsupportedType,
        ParseError::MultiplePushConstantBlocks,
        ParseError::PushConstantNotAStruct,
        ParseError::UndeclaredId,
    };
    std::vector<std::string_view> seen;
    for (const ParseError error : all) {
        const std::string_view text = describe(error);
        CHECK(!text.empty());
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}
