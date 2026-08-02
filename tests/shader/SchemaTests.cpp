/**
 * @file SchemaTests.cpp
 * @brief Tests for mdux.shader.schema.
 *
 * The rejections are the point. A schema whose validate() only ever succeeds is a comment claiming
 * there are invariants, so every SchemaError below has a case that produces exactly it - and the
 * round-trip test asserts that a package survives write() and parse() unchanged, which is the
 * property the whole evidence pipeline rests on.
 */
import std;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.shader.schema;
import mdux.test;

#include "../framework/MduXTest.hpp"

namespace {

using namespace mdux::shader;
namespace evidence = mdux::evidence;

evidence::Digest digestOf(std::string_view text) {
    return evidence::sha256(std::as_bytes(std::span{text.data(), text.size()}));
}

/// A minimal package that validates: one vertex module filling a 64-byte sidecar.
ShaderPackage validPackage() {
    ShaderPackage package;
    package.header.id = "mdux-ui";
    package.header.kind = "shader";
    package.sidecarPath = "shaders.spv";
    package.sidecarByteLength = 64;
    package.sidecarSha256 = digestOf("sidecar");
    package.modules.push_back(ShaderModule{.id = "ui.vert",
                                           .stage = Stage::Vertex,
                                           .entryPoint = "main",
                                           .byteOffset = 0,
                                           .byteLength = 64,
                                           .sha256 = digestOf("ui.vert")});
    return package;
}

/// The error a package validates to, or nullopt when it is valid. Keeps each case to one line.
std::optional<SchemaError> errorOf(const ShaderPackage& package) {
    auto result = package.validate();
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error();
}

}  // namespace

// ---------------------------------------------------------------------------
// Wire encoding
// ---------------------------------------------------------------------------

TEST_CASE("Stage and DescriptorKind wire spellings are stable", "evidence-unit") {
    // These strings are the published package format; renaming one silently invalidates every
    // committed artifact that carries it.
    CHECK(toWire(Stage::Vertex) == "vertex");
    CHECK(toWire(Stage::Fragment) == "fragment");
    CHECK(toWire(DescriptorKind::UniformBuffer) == "uniformBuffer");
    CHECK(toWire(DescriptorKind::StorageBuffer) == "storageBuffer");
    CHECK(toWire(DescriptorKind::CombinedImageSampler) == "combinedImageSampler");
    CHECK(toWire(DescriptorKind::SampledImage) == "sampledImage");
    CHECK(toWire(DescriptorKind::Sampler) == "sampler");
}

TEST_CASE("Every wire spelling round-trips through its enum", "evidence-unit") {
    for (std::size_t i = 0; i < kStageWireValues.size(); ++i) {
        auto stage = stageFromWire(kStageWireValues[i]);
        REQUIRE(stage.has_value());
        CHECK(static_cast<std::size_t>(*stage) == i);
    }
    for (std::size_t i = 0; i < kDescriptorKindWireValues.size(); ++i) {
        auto kind = descriptorKindFromWire(kDescriptorKindWireValues[i]);
        REQUIRE(kind.has_value());
        CHECK(static_cast<std::size_t>(*kind) == i);
    }
}

TEST_CASE("An unknown wire value is rejected rather than defaulted", "evidence-unit") {
    auto stage = stageFromWire("compute");
    REQUIRE(!stage.has_value());
    CHECK(stage.error() == SchemaError::UnknownStage);

    auto kind = descriptorKindFromWire("inputAttachment");
    REQUIRE(!kind.has_value());
    CHECK(kind.error() == SchemaError::UnknownDescriptorKind);
}

TEST_CASE("A stage mask is written in enumerator order, not assembly order", "evidence-unit") {
    // Two packages with the same stage set must produce identical bytes however the mask was
    // built, so the array order cannot depend on which bit was set first.
    const mdux::evidence::json::Value fromVertexFirst = stagesToJson(kVertexBit | kFragmentBit);
    const mdux::evidence::json::Value fromFragmentFirst = stagesToJson(kFragmentBit | kVertexBit);
    auto a = mdux::evidence::json::write(fromVertexFirst);
    auto b = mdux::evidence::json::write(fromFragmentFirst);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);

    auto mask = stagesFromJson(fromVertexFirst);
    REQUIRE(mask.has_value());
    CHECK(*mask == (kVertexBit | kFragmentBit));
}

TEST_CASE("An empty stage list decodes to an empty mask", "evidence-unit") {
    auto mask = stagesFromJson(stagesToJson(0));
    REQUIRE(mask.has_value());
    CHECK(*mask == 0);
}

// ---------------------------------------------------------------------------
// validate()
// ---------------------------------------------------------------------------

TEST_CASE("The reference package validates", "evidence-unit") {
    // Guards every rejection below: if this failed, they could all pass for the wrong reason.
    CHECK(!errorOf(validPackage()).has_value());
}

TEST_CASE("A package of the wrong kind is rejected", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.header.kind = "font";
    CHECK(errorOf(package) == SchemaError::WrongKind);
}

TEST_CASE("A sidecar path must be a bare filename", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.sidecarPath.clear();
    CHECK(errorOf(package) == SchemaError::EmptySidecarPath);

    // A sidecar sits beside package.json. A path with a separator would let an artifact point
    // outside its own directory, which the bake/verify comparison could not follow.
    package.sidecarPath = "nested/shaders.spv";
    CHECK(errorOf(package) == SchemaError::SidecarPathHasSeparator);

    package.sidecarPath = "nested\\shaders.spv";
    CHECK(errorOf(package) == SchemaError::SidecarPathHasSeparator);
}

TEST_CASE("A package with no modules is rejected", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.modules.clear();
    CHECK(errorOf(package) == SchemaError::NoModules);
}

TEST_CASE("Module identity is required and must be unique", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.modules[0].id.clear();
    CHECK(errorOf(package) == SchemaError::EmptyModuleId);

    package = validPackage();
    package.modules[0].byteLength = 32;
    ShaderModule duplicate = package.modules[0];
    duplicate.byteOffset = 32;
    package.modules.push_back(duplicate);
    CHECK(errorOf(package) == SchemaError::DuplicateModuleId);
}

TEST_CASE("A module needs an entry point and a non-zero length", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.modules[0].entryPoint.clear();
    CHECK(errorOf(package) == SchemaError::EmptyEntryPoint);

    package = validPackage();
    package.modules[0].byteLength = 0;
    CHECK(errorOf(package) == SchemaError::EmptyModule);
}

TEST_CASE("Module ranges must be word-aligned", "evidence-unit") {
    // SPIR-V is a sequence of 32-bit words, so a length that is not a multiple of 4 cannot be a
    // module - and would be caught much later, by the driver, as a device loss.
    ShaderPackage package = validPackage();
    package.modules[0].byteLength = 62;
    CHECK(errorOf(package) == SchemaError::UnalignedModule);

    package = validPackage();
    package.modules[0].byteOffset = 2;
    package.modules[0].byteLength = 60;
    CHECK(errorOf(package) == SchemaError::UnalignedModule);
}

TEST_CASE("A module range may not extend past the sidecar", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.modules[0].byteLength = 68;
    CHECK(errorOf(package) == SchemaError::ModuleOutOfBounds);

    // An offset past the end, with a length that would wrap if the two were added naively.
    package = validPackage();
    package.modules[0].byteOffset = 64;
    package.modules[0].byteLength = 4;
    CHECK(errorOf(package) == SchemaError::ModuleOutOfBounds);
}

TEST_CASE("Two module ranges may not overlap", "evidence-unit") {
    // Overlapping ranges mean two modules report different digests for bytes they share, so at
    // most one of the two recorded digests can be right.
    ShaderPackage package = validPackage();
    package.modules[0].byteLength = 32;
    package.modules.push_back(ShaderModule{.id = "ui.frag",
                                           .stage = Stage::Fragment,
                                           .entryPoint = "main",
                                           .byteOffset = 16,
                                           .byteLength = 32,
                                           .sha256 = digestOf("ui.frag")});
    CHECK(errorOf(package) == SchemaError::OverlappingModules);
}

TEST_CASE("Adjacent module ranges are not an overlap", "evidence-unit") {
    // The off-by-one guard on the overlap check: [0,32) and [32,64) share no byte.
    ShaderPackage package = validPackage();
    package.modules[0].byteLength = 32;
    package.modules.push_back(ShaderModule{.id = "ui.frag",
                                           .stage = Stage::Fragment,
                                           .entryPoint = "main",
                                           .byteOffset = 32,
                                           .byteLength = 32,
                                           .sha256 = digestOf("ui.frag")});
    CHECK(!errorOf(package).has_value());
}

TEST_CASE("A descriptor visible to no stage is rejected", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.descriptors.push_back(
        DescriptorBinding{.set = 0, .binding = 0, .kind = DescriptorKind::Sampler, .count = 1});
    CHECK(errorOf(package) == SchemaError::NoStages);
}

TEST_CASE("A descriptor needs a non-zero count and a unique binding", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.descriptors.push_back(DescriptorBinding{.set = 0,
                                                    .binding = 0,
                                                    .kind = DescriptorKind::Sampler,
                                                    .count = 0,
                                                    .stages = kFragmentBit});
    CHECK(errorOf(package) == SchemaError::ZeroDescriptorCount);

    package = validPackage();
    const DescriptorBinding binding{.set = 0,
                                    .binding = 1,
                                    .kind = DescriptorKind::Sampler,
                                    .count = 1,
                                    .stages = kFragmentBit};
    package.descriptors.push_back(binding);
    package.descriptors.push_back(binding);
    CHECK(errorOf(package) == SchemaError::DuplicateDescriptorBinding);
}

TEST_CASE("The same binding number in a different set is not a duplicate", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.descriptors.push_back(DescriptorBinding{
        .set = 0, .binding = 1, .kind = DescriptorKind::Sampler, .count = 1,
        .stages = kFragmentBit});
    package.descriptors.push_back(DescriptorBinding{
        .set = 1, .binding = 1, .kind = DescriptorKind::Sampler, .count = 1,
        .stages = kFragmentBit});
    CHECK(!errorOf(package).has_value());
}

TEST_CASE("Push constant ranges are checked for size, alignment and overlap", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.pushConstants.push_back(
        PushConstantRange{.offset = 0, .size = 0, .stages = kVertexBit});
    CHECK(errorOf(package) == SchemaError::EmptyPushConstantRange);

    package = validPackage();
    package.pushConstants.push_back(
        PushConstantRange{.offset = 2, .size = 16, .stages = kVertexBit});
    CHECK(errorOf(package) == SchemaError::UnalignedPushConstantRange);

    package = validPackage();
    package.pushConstants.push_back(
        PushConstantRange{.offset = 0, .size = 16, .stages = kVertexBit});
    package.pushConstants.push_back(
        PushConstantRange{.offset = 8, .size = 16, .stages = kFragmentBit});
    CHECK(errorOf(package) == SchemaError::OverlappingPushConstants);
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST_CASE("A package survives write() and parse() unchanged", "evidence-unit") {
    ShaderPackage package = validPackage();
    package.modules[0].byteLength = 32;
    package.modules.push_back(ShaderModule{.id = "ui.frag",
                                           .stage = Stage::Fragment,
                                           .entryPoint = "main",
                                           .byteOffset = 32,
                                           .byteLength = 32,
                                           .sha256 = digestOf("ui.frag")});
    package.descriptors.push_back(DescriptorBinding{.set = 0,
                                                    .binding = 0,
                                                    .kind = DescriptorKind::CombinedImageSampler,
                                                    .count = 1,
                                                    .stages = kFragmentBit});
    package.pushConstants.push_back(
        PushConstantRange{.offset = 0, .size = 16, .stages = kVertexBit | kFragmentBit});

    auto text = package.write();
    REQUIRE(text.has_value());

    auto parsed = ShaderPackage::parse(*text);
    REQUIRE(parsed.has_value());

    CHECK(parsed->header.id == package.header.id);
    CHECK(parsed->header.kind == package.header.kind);
    CHECK(parsed->sidecarPath == package.sidecarPath);
    CHECK(parsed->sidecarByteLength == package.sidecarByteLength);
    CHECK(parsed->sidecarSha256 == package.sidecarSha256);
    REQUIRE(parsed->modules.size() == package.modules.size());
    for (std::size_t i = 0; i < package.modules.size(); ++i) {
        CHECK(parsed->modules[i].id == package.modules[i].id);
        CHECK(parsed->modules[i].stage == package.modules[i].stage);
        CHECK(parsed->modules[i].entryPoint == package.modules[i].entryPoint);
        CHECK(parsed->modules[i].byteOffset == package.modules[i].byteOffset);
        CHECK(parsed->modules[i].byteLength == package.modules[i].byteLength);
        CHECK(parsed->modules[i].sha256 == package.modules[i].sha256);
    }
    CHECK(parsed->descriptors == package.descriptors);
    CHECK(parsed->pushConstants == package.pushConstants);

    // Re-writing the parsed package must reproduce the same bytes. This is the property the
    // evidence pipeline's byte comparison depends on; a round trip that merely preserved the
    // fields would not be enough.
    auto rewritten = parsed->write();
    REQUIRE(rewritten.has_value());
    CHECK(*rewritten == *text);
}

TEST_CASE("Parsing rejects malformed and semantically invalid text", "evidence-unit") {
    auto notJson = ShaderPackage::parse("{ this is not json");
    REQUIRE(!notJson.has_value());
    CHECK(notJson.error() == SchemaError::MalformedPackage);

    auto notAnObject = ShaderPackage::parse("[]");
    REQUIRE(!notAnObject.has_value());
    CHECK(notAnObject.error() == SchemaError::MalformedPackage);

    // Well-formed JSON that is not a shader package.
    auto wrongShape = ShaderPackage::parse(R"({"schemaVersion":1,"id":"x","kind":"shader"})");
    REQUIRE(!wrongShape.has_value());
    CHECK(wrongShape.error() == SchemaError::MalformedPackage);
}

TEST_CASE("Parsing runs validate(), so an invalid package cannot be read back", "evidence-unit") {
    // Written by hand rather than through write(), which validates: the point is that a file
    // someone edited into an invalid state is rejected on read rather than trusted.
    ShaderPackage package = validPackage();
    auto text = package.write();
    REQUIRE(text.has_value());

    std::string corrupted = *text;
    const std::size_t position = corrupted.find("\"byteLength\": 64");
    REQUIRE(position != std::string::npos);
    corrupted.replace(position, std::string_view{"\"byteLength\": 64"}.size(),
                      "\"byteLength\": 99");

    auto parsed = ShaderPackage::parse(corrupted);
    REQUIRE(!parsed.has_value());
    CHECK(parsed.error() == SchemaError::UnalignedModule);
}

TEST_CASE("find() locates a module by id and reports a miss", "evidence-unit") {
    const ShaderPackage package = validPackage();
    const ShaderModule* found = package.find("ui.vert");
    REQUIRE(found != nullptr);
    CHECK(found->stage == Stage::Vertex);
    CHECK(package.find("ui.frag") == nullptr);
}

TEST_CASE("Every SchemaError has its own description", "evidence-unit") {
    // A duplicated or empty description makes a diagnostic useless at exactly the moment it
    // matters, so the whole set is checked for distinctness rather than spot-checked.
    constexpr std::array<SchemaError, 22> all{
        SchemaError::WrongKind,
        SchemaError::EmptySidecarPath,
        SchemaError::SidecarPathHasSeparator,
        SchemaError::NoModules,
        SchemaError::EmptyModuleId,
        SchemaError::DuplicateModuleId,
        SchemaError::EmptyEntryPoint,
        SchemaError::EmptyModule,
        SchemaError::UnalignedModule,
        SchemaError::ModuleOutOfBounds,
        SchemaError::OverlappingModules,
        SchemaError::NoStages,
        SchemaError::ZeroDescriptorCount,
        SchemaError::DuplicateDescriptorBinding,
        SchemaError::EmptyPushConstantRange,
        SchemaError::UnalignedPushConstantRange,
        SchemaError::OverlappingPushConstants,
        SchemaError::UnsupportedSchemaVersion,
        SchemaError::UnknownStage,
        SchemaError::UnknownDescriptorKind,
        SchemaError::MalformedPackage,
        SchemaError::ReportRejected,
    };
    std::vector<std::string_view> seen;
    for (const SchemaError error : all) {
        const std::string_view text = describe(error);
        CHECK(!text.empty());
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}
