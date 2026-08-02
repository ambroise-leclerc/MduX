/**
 * @file GeneratedTests.cpp
 * @brief The generated module and header must describe identical bytes and an identical contract,
 *        and both must agree with the committed artifact they were rendered from.
 *
 * Three things are being checked, and they are not the same thing:
 *
 *  1. The two generated forms agree with each other. If they ever diverge, whichever of #122 or
 *     #124 used the other one would be linking different shaders than the reviewer read.
 *  2. The generated data agrees with `generated/shader/mdux-ui/package.json`. The emitter is a
 *     mechanical rendering, so a disagreement means the rendering is wrong.
 *  3. The bytes really are the reviewed bytes - checked against the digest the committed package
 *     records, which is the number a reviewer actually looked at.
 */
import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.shaderemit;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "GeneratedConsumers.hpp"

namespace {

namespace shader = mdux::shader;
namespace evidence = mdux::evidence;
using mdux::test::generated::fromHeader;
using mdux::test::generated::fromModule;

const std::filesystem::path kPackageDir =
    std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "shader" / "mdux-ui";

[[nodiscard]] mdux::core::Result<shader::ShaderPackage, shader::SchemaError> committedPackage() {
    std::ifstream file{kPackageDir / "package.json", std::ios::binary | std::ios::ate};
    if (!file) {
        return mdux::core::err(shader::SchemaError::MalformedPackage);
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), size);
    return shader::ShaderPackage::parse(text);
}

}  // namespace

TEST_CASE("The module and header forms expose byte-identical SPIR-V", "evidence-unit") {
    const shader::PackageView fromM = fromModule();
    const shader::PackageView fromH = fromHeader();

    REQUIRE(fromM.spirv.size() == fromH.spirv.size());
    CHECK(std::ranges::equal(fromM.spirv, fromH.spirv));
    // Compared by digest as well as elementwise: an elementwise loop over equal-length spans that
    // were somehow the same span would pass vacuously, and the digest would not.
    CHECK(evidence::sha256(fromM.spirv) == evidence::sha256(fromH.spirv));
}

TEST_CASE("The module and header forms expose an identical contract", "evidence-unit") {
    const shader::PackageView fromM = fromModule();
    const shader::PackageView fromH = fromHeader();

    CHECK(fromM.id == fromH.id);
    REQUIRE(fromM.modules.size() == fromH.modules.size());
    CHECK(std::ranges::equal(fromM.modules, fromH.modules));
    REQUIRE(fromM.descriptors.size() == fromH.descriptors.size());
    CHECK(std::ranges::equal(fromM.descriptors, fromH.descriptors));
    REQUIRE(fromM.pushConstants.size() == fromH.pushConstants.size());
    CHECK(std::ranges::equal(fromM.pushConstants, fromH.pushConstants));
}

TEST_CASE("The generated package matches the committed artifact it was rendered from",
          "evidence-unit") {
    auto committed = committedPackage();
    REQUIRE(committed.has_value());
    const shader::PackageView view = fromModule();

    CHECK(view.id == committed->header.id);
    CHECK(view.spirv.size() == committed->sidecarByteLength);
    // The digest a reviewer signed off on, against the bytes that will be linked into a device
    // binary. This is the assertion that makes "generated code needs no review" defensible.
    CHECK(evidence::sha256(view.spirv) == committed->sidecarSha256);

    REQUIRE(view.modules.size() == committed->modules.size());
    for (std::size_t i = 0; i < view.modules.size(); ++i) {
        CHECK(view.modules[i].id == committed->modules[i].id);
        CHECK(view.modules[i].stage == committed->modules[i].stage);
        CHECK(view.modules[i].entryPoint == committed->modules[i].entryPoint);
        CHECK(view.modules[i].byteOffset == committed->modules[i].byteOffset);
        CHECK(view.modules[i].byteLength == committed->modules[i].byteLength);
    }

    REQUIRE(view.descriptors.size() == committed->descriptors.size());
    for (std::size_t i = 0; i < view.descriptors.size(); ++i) {
        CHECK(view.descriptors[i] == committed->descriptors[i]);
    }
    REQUIRE(view.pushConstants.size() == committed->pushConstants.size());
    for (std::size_t i = 0; i < view.pushConstants.size(); ++i) {
        CHECK(view.pushConstants[i] == committed->pushConstants[i]);
    }
}

TEST_CASE("Each module's generated range matches the digest the package records",
          "evidence-unit") {
    auto committed = committedPackage();
    REQUIRE(committed.has_value());
    const shader::PackageView view = fromModule();

    for (const shader::ShaderModule& module : committed->modules) {
        const std::span<const std::byte> bytes = view.moduleSpirv(module.id);
        REQUIRE(!bytes.empty());
        CHECK(bytes.size() == module.byteLength);
        CHECK(evidence::sha256(bytes) == module.sha256);
    }
}

TEST_CASE("moduleSpirv() returns an empty span for an unknown id", "evidence-unit") {
    const shader::PackageView view = fromModule();
    CHECK(view.find("ui.geom") == nullptr);
    CHECK(view.moduleSpirv("ui.geom").empty());
}

TEST_CASE("moduleSpirv() refuses a range that escapes the payload", "evidence-unit") {
    // The generated data is machine-written, but a view can also be assembled by hand - as it is
    // here - and a span past the end of the payload is the one mistake that would not fail
    // visibly. Bounds are checked rather than assumed.
    static constexpr std::array<std::byte, 8> payload{};
    static constexpr std::array<shader::ModuleView, 1> modules{
        shader::ModuleView{.id = "bad", .byteOffset = 4, .byteLength = 16}};
    const shader::PackageView view{.id = "hand-made", .spirv = payload, .modules = modules};

    CHECK(view.find("bad") != nullptr);
    CHECK(view.moduleSpirv("bad").empty());
}

TEST_CASE("The generated identifier matches what the build predicts", "evidence-unit") {
    // cmake/MduXShaderEmit.cmake derives the generated filenames with a regex that must agree
    // with identifierFor(). A mismatch would surface as a missing file with no explanation, so
    // it is asserted rather than trusted.
    CHECK(mdux::tools::shaderemit::identifierFor("mdux-ui") == "mdux_ui");
    CHECK(mdux::tools::shaderemit::identifierFor("a.b-c") == "a_b_c");
    CHECK(mdux::tools::shaderemit::identifierFor("plain") == "plain");
    // A package id may begin with a digit; a C++ identifier may not.
    CHECK(mdux::tools::shaderemit::identifierFor("2d") == "_2d");
}
