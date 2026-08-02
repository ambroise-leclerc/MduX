/**
 * @file EmitTests.cpp
 * @brief Tests for the shader package C++ emitter.
 *
 * GeneratedTests covers what the emitter produces for the real package, by compiling it. This
 * file covers what it does with input it should refuse, and the rendering decisions that are
 * invisible from a single well-formed example - an empty contract, a package id that is not a
 * C++ identifier, and the digest check that stops unreviewed bytes reaching a binary.
 */
import std;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.cli;
import mdux.tools.shaderemit;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "SpirvFixtures.hpp"

namespace {

using namespace mdux::tools::shaderemit;
using namespace mdux::test::spirv;
namespace cli = mdux::tools::cli;
namespace shader = mdux::shader;
namespace evidence = mdux::evidence;

class TempDir {
public:
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("mdux-shaderemit-test-" + std::to_string(stamp) + "-" +
                 std::to_string(counter_++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    static inline int counter_ = 0;
};

void writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    writeBytes(path, std::as_bytes(std::span{text.data(), text.size()}));
}

/// Writes a package.json and its sidecar into `dir`, and returns the package path.
[[nodiscard]] std::filesystem::path writePackage(const std::filesystem::path& dir,
                                                 const shader::ShaderPackage& package,
                                                 std::span<const std::byte> sidecar) {
    auto text = package.write();
    REQUIRE(text.has_value());
    writeText(dir / "package.json", *text);
    writeBytes(dir / package.sidecarPath, sidecar);
    return dir / "package.json";
}

/// A one-module package over `sidecar`, consistent by construction.
[[nodiscard]] shader::ShaderPackage packageOver(std::span<const std::byte> sidecar,
                                                std::string id = "test-ui") {
    shader::ShaderPackage package;
    package.header.id = std::move(id);
    package.header.kind = "shader";
    package.sidecarPath = "shaders.spv";
    package.sidecarByteLength = sidecar.size();
    package.sidecarSha256 = evidence::sha256(sidecar);
    package.modules.push_back(
        shader::ShaderModule{.id = "only.vert",
                             .stage = shader::Stage::Vertex,
                             .entryPoint = "main",
                             .byteOffset = 0,
                             .byteLength = sidecar.size(),
                             .sha256 = evidence::sha256(sidecar)});
    return package;
}

[[nodiscard]] std::vector<std::string> codesOf(const std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<std::string> codes;
    for (const cli::Diagnostic& diagnostic : diagnostics) {
        codes.push_back(diagnostic.code);
    }
    return codes;
}

}  // namespace

TEST_CASE("identifierFor maps a package id to a C++ identifier", "evidence-unit") {
    CHECK(identifierFor("mdux-ui") == "mdux_ui");
    CHECK(identifierFor("a.b-c d") == "a_b_c_d");
    CHECK(identifierFor("already_fine") == "already_fine");
    // A package id may begin with a digit; a C++ identifier may not.
    CHECK(identifierFor("3d") == "_3d");
    CHECK(identifierFor("") == "");
}

TEST_CASE("A well-formed package renders both outputs", "evidence-unit") {
    const TempDir dir;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const std::filesystem::path packagePath =
        writePackage(dir.path(), packageOver(sidecar), sidecar);

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(packagePath, diagnostics);
    REQUIRE(outputs.has_value());
    CHECK(diagnostics.empty());

    CHECK(outputs->stem == "test_ui");
    CHECK(outputs->moduleName == "mdux.shader.generated.test_ui");
    // The module form declares the module; the header form must not, and must be include-guarded.
    CHECK(outputs->moduleSource.find("export module mdux.shader.generated.test_ui;") !=
          std::string::npos);
    CHECK(outputs->headerSource.find("export module") == std::string::npos);
    CHECK(outputs->headerSource.find("#pragma once") != std::string::npos);
    // Both must carry the same payload and the same contract, which is what GeneratedTests
    // asserts by compiling them. Here it is enough that the rendered body is literally shared.
    CHECK(outputs->moduleSource.find("kSpirvBytes[] = {") != std::string::npos);
    CHECK(outputs->headerSource.find("kSpirvBytes[] = {") != std::string::npos);
}

TEST_CASE("A package with no descriptors renders an empty span, not an empty array",
          "evidence-unit") {
    // A zero-length C array is ill-formed, so the empty case cannot use the same spelling as the
    // populated one. Both must still produce an accessor with the same type.
    const TempDir dir;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const std::filesystem::path packagePath =
        writePackage(dir.path(), packageOver(sidecar), sidecar);

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(packagePath, diagnostics);
    REQUIRE(outputs.has_value());
    CHECK(outputs->moduleSource.find("kDescriptors[] = {") == std::string::npos);
    CHECK(outputs->moduleSource.find(
              "std::span<const mdux::shader::DescriptorBinding> kDescriptors{}") !=
          std::string::npos);
    CHECK(outputs->moduleSource.find(
              "std::span<const mdux::shader::PushConstantRange> kPushConstants{}") !=
          std::string::npos);
}

TEST_CASE("An unreadable package is reported", "evidence-unit") {
    const TempDir dir;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(dir.path() / "package.json", diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHE001");
    CHECK(diagnostics[0].fixHint.find("mdux-bake-update") != std::string::npos);
}

TEST_CASE("A package that is not a shader package is reported with the reason", "evidence-unit") {
    const TempDir dir;
    writeText(dir.path() / "package.json", R"({"schemaVersion":1,"id":"x","kind":"font"})");

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(dir.path() / "package.json", diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHE002");
}

TEST_CASE("A missing sidecar is reported against the sidecar, not the package", "evidence-unit") {
    const TempDir dir;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const shader::ShaderPackage package = packageOver(sidecar);
    auto text = package.write();
    REQUIRE(text.has_value());
    writeText(dir.path() / "package.json", *text);  // no sidecar written

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(dir.path() / "package.json", diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHE003");
    CHECK(diagnostics[0].file.find("shaders.spv") != std::string::npos);
}

TEST_CASE("A sidecar that does not match the recorded digest is refused", "evidence-unit") {
    // The check that stops unreviewed bytes reaching a binary. Without it, a hand-edited sidecar
    // would be rendered into source and linked while every artifact check stayed green - the
    // digest under review would describe one thing and the compiled bytes another.
    const TempDir dir;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const std::filesystem::path packagePath =
        writePackage(dir.path(), packageOver(sidecar), sidecar);

    std::vector<std::byte> tampered = sidecar;
    tampered.back() = static_cast<std::byte>(std::to_integer<unsigned>(tampered.back()) ^ 0xffu);
    writeBytes(dir.path() / "shaders.spv", tampered);

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(packagePath, diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHE004");
    CHECK(diagnostics[0].fixHint.find("do not hand-edit") != std::string::npos);
}

TEST_CASE("A sidecar of the wrong length is refused", "evidence-unit") {
    const TempDir dir;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const std::filesystem::path packagePath =
        writePackage(dir.path(), packageOver(sidecar), sidecar);

    std::vector<std::byte> truncated{sidecar.begin(), sidecar.end() - 4};
    writeBytes(dir.path() / "shaders.spv", truncated);

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(packagePath, diagnostics);
    CHECK(!outputs.has_value());
    CHECK(codesOf(diagnostics) == std::vector<std::string>{"SHE004"});
}

TEST_CASE("write() creates both files and leaves an unchanged file untouched", "evidence-unit") {
    // Rewriting an unchanged file would restamp it and force every consumer to recompile on every
    // build, which for a few thousand bytes of shader is not free.
    const TempDir source;
    const std::vector<std::byte> sidecar = minimal().bytes();
    const std::filesystem::path packagePath =
        writePackage(source.path(), packageOver(sidecar), sidecar);

    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = render(packagePath, diagnostics);
    REQUIRE(outputs.has_value());

    const TempDir out;
    REQUIRE(write(*outputs, out.path(), diagnostics));
    const std::filesystem::path moduleFile = out.path() / "test_ui.cppm";
    const std::filesystem::path headerFile = out.path() / "test_ui.hpp";
    REQUIRE(std::filesystem::exists(moduleFile));
    REQUIRE(std::filesystem::exists(headerFile));

    const auto firstWrite = std::filesystem::last_write_time(moduleFile);
    // A timestamp comparison needs the two writes to be distinguishable; the file system's
    // resolution is coarser than this loop would be, so the content is checked instead.
    const auto before = std::filesystem::file_size(moduleFile);
    REQUIRE(write(*outputs, out.path(), diagnostics));
    CHECK(std::filesystem::file_size(moduleFile) == before);
    CHECK(std::filesystem::last_write_time(moduleFile) == firstWrite);
    CHECK(diagnostics.empty());
}
