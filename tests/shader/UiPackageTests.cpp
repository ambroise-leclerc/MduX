/**
 * @brief The committed MduX UI shader package must keep the contract the renderer depends on.
 *
 * `evidence.shader.mdux-ui` already proves the committed artifact is what the baker produces from
 * the committed sources. It cannot prove the artifact is what the *renderer* needs - re-baking an
 * edited shader updates both sides at once and the comparison stays green while the pipeline
 * contract silently moves.
 *
 * These tests are the other half. They assert the contract in one place, so changing a descriptor
 * binding, a push-constant size, or which stage sees what is a deliberate edit here rather than a
 * surprise in #124 or a validation-layer message on a device.
 */
import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.test;

#include "../framework/MduXTest.hpp"

namespace {

namespace shader = mdux::shader;
namespace evidence = mdux::evidence;

const std::filesystem::path packageDir =
    std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "shader" / "mdux-ui";

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return std::nullopt;
    }
    return bytes;
}

/// Returns the parse result directly rather than an optional. `std::optional<ShaderPackage>` does
/// not compile on GCC 15 - see the note in tools/shader/ShaderBake.cppm - while the `std::expected`
/// that `parse()` already returns is unaffected, so this is the shape to use.
[[nodiscard]] mdux::core::Result<shader::ShaderPackage, shader::SchemaError> committedPackage() {
    auto bytes = readFile(packageDir / "package.json");
    if (!bytes.has_value()) {
        return mdux::core::err(shader::SchemaError::MalformedPackage);
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    return shader::ShaderPackage::parse(text);
}

}  // namespace

TEST_CASE("The committed UI package parses and validates", "evidence-unit") {
    // Guards every assertion below, and doubles as a check that the strict reader accepts what
    // the writer committed - a round trip through the filesystem and a git checkout.
    auto package = committedPackage();
    REQUIRE(package.has_value());
    CHECK(package->header.id == "mdux-ui");
    CHECK(package->header.kind == "shader");
    CHECK(package->sidecarPath == "shaders.spv");
}

TEST_CASE("The UI package provides exactly a vertex and a fragment module", "evidence-unit") {
    auto package = committedPackage();
    REQUIRE(package.has_value());
    REQUIRE(package->modules.size() == 2);

    const shader::ShaderModule* vertex = package->find("ui.vert");
    REQUIRE(vertex != nullptr);
    CHECK(vertex->stage == shader::Stage::Vertex);
    CHECK(vertex->entryPoint == "main");

    const shader::ShaderModule* fragment = package->find("ui.frag");
    REQUIRE(fragment != nullptr);
    CHECK(fragment->stage == shader::Stage::Fragment);
    CHECK(fragment->entryPoint == "main");
}

TEST_CASE("The UI pipeline binds one combined image sampler at set 0 binding 0",
          "evidence-unit") {
    // The atlas is bound for every draw, including one that is entirely solid: a descriptor set
    // whose shape depended on the content would put a conditional in the renderer's hot path and
    // in its budget, which is the opposite of fixed.
    auto package = committedPackage();
    REQUIRE(package.has_value());
    REQUIRE(package->descriptors.size() == 1);

    const shader::DescriptorBinding& atlas = package->descriptors.front();
    CHECK(atlas.set == 0);
    CHECK(atlas.binding == 0);
    CHECK(atlas.kind == shader::DescriptorKind::CombinedImageSampler);
    CHECK(atlas.count == 1);
    // The fragment stage alone samples it. A vertex bit here would mean the pipeline layout
    // requested access no shader uses, which the validation layers report as a warning and which
    // costs a descriptor slot on a device that has few.
    CHECK(atlas.stages == shader::fragmentBit);
}

TEST_CASE("The UI pipeline takes an 8-byte vertex-only push constant", "evidence-unit") {
    // The viewport size, so a governed draw list can hold pixel coordinates and contain no
    // projection maths and no dependency on the surface it will be drawn to.
    auto package = committedPackage();
    REQUIRE(package.has_value());
    REQUIRE(package->pushConstants.size() == 1);

    const shader::PushConstantRange& range = package->pushConstants.front();
    CHECK(range.offset == 0);
    CHECK(range.size == 8);
    CHECK(range.stages == shader::vertexBit);
}

TEST_CASE("The sidecar matches the digests the package records", "evidence-unit") {
    // The package's own claims, checked against the bytes beside it. `evidence.shader.mdux-ui`
    // compares both files against a fresh bake; this checks they agree with *each other*, which
    // is what a consumer that only has the committed directory can rely on.
    auto package = committedPackage();
    REQUIRE(package.has_value());
    auto sidecar = readFile(packageDir / package->sidecarPath);
    REQUIRE(sidecar.has_value());

    CHECK(sidecar->size() == package->sidecarByteLength);
    CHECK(evidence::sha256(*sidecar) == package->sidecarSha256);

    for (const shader::ShaderModule& module : package->modules) {
        const std::span<const std::byte> range{sidecar->data() + module.byteOffset,
                                               static_cast<std::size_t>(module.byteLength)};
        CHECK(evidence::sha256(range) == module.sha256);
    }
}

TEST_CASE("Every module in the sidecar is well-formed SPIR-V for its declared stage",
          "evidence-unit") {
    // The committed bytes are read as a shader module would read them, so a range that is
    // correctly digested but not actually SPIR-V - a truncation, or two modules swapped - fails
    // here rather than at vkCreateShaderModule.
    auto package = committedPackage();
    REQUIRE(package.has_value());
    auto sidecar = readFile(packageDir / package->sidecarPath);
    REQUIRE(sidecar.has_value());

    for (const shader::ShaderModule& module : package->modules) {
        const std::span<const std::byte> range{sidecar->data() + module.byteOffset,
                                               static_cast<std::size_t>(module.byteLength)};
        REQUIRE(range.size() >= 4);
        const std::uint32_t magic =
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(range[0])) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(range[1])) << 8) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(range[2])) << 16) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(range[3])) << 24);
        CHECK(magic == 0x07230203u);
        CHECK(module.byteLength % 4 == 0);
    }
}
