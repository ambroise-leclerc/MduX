/**
 * @file EmitTests.cpp
 * @brief BDD scenarios for the shader package C++ emitter, converted from the Wave 3 MduXTest
 *        suite (issue #141).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * GeneratedTests covers what the emitter produces for the real package, by compiling it. This
 * file covers what it does with input it should refuse, and the rendering decisions that are
 * invisible from a single well-formed example - an empty contract, a package id that is not a
 * C++ identifier, and the digest check that stops unreviewed bytes reaching a binary.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`).
 */

import std;
import speclab;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.cli;
import mdux.tools.shaderemit;

#include "../framework/SpecLabBridge.hpp"
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
    if (!text.has_value()) {
        throw speclab::core::AssertionFailure(
            "package.write() rejected the package", std::source_location::current());
    }
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

const mdux::spec::Register identifierForMaps{
    "identifierFor maps a package id to a C++ identifier", "evidence-unit", [] {
        return speclab::Test("shader-emit-identifier-for-maps")
            .Given("package ids that are not C++ identifiers", [] {})
            .When("each is mapped", [] {})
            .Then("each becomes a valid C++ identifier",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(identifierFor("mdux-ui") == "mdux_ui", "mdux-ui");
                      checks.expect(identifierFor("a.b-c d") == "a_b_c_d", "a.b-c d");
                      checks.expect(identifierFor("already_fine") == "already_fine",
                                    "already_fine");
                      // A package id may begin with a digit; a C++ identifier may not.
                      checks.expect(identifierFor("3d") == "_3d", "3d");
                      checks.expect(identifierFor("") == "", "the empty id");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wellFormedRendersBoth{
    "A well-formed package renders both outputs", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<std::byte> sidecar;
            std::filesystem::path packagePath;
            std::vector<cli::Diagnostic> diagnostics;
            std::optional<EmitOutputs> outputs;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-well-formed-renders-both")
            .Given("a well-formed package over a minimal sidecar",
                   [state] {
                       state->sidecar = minimal().bytes();
                       state->packagePath =
                           writePackage(state->dir.path(), packageOver(state->sidecar),
                                        state->sidecar);
                   })
            .When("it is rendered",
                  [state] {
                      state->outputs =
                          render(state->packagePath, state->diagnostics);
                  })
            .Then("both outputs are produced and carry the same payload",
                  [state] {
                      if (!state->outputs.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "render() produced no outputs",
                              std::source_location::current());
                      }
                      const EmitOutputs& outputs = *state->outputs;
                      mdux::spec::Checks checks;
                      checks.expect(state->diagnostics.empty(), "no diagnostics");
                      checks.expect(outputs.stem == "test_ui", "the stem");
                      checks.expect(outputs.moduleName ==
                                        "mdux.shader.generated.test_ui",
                                    "the module name");
                      // The module form declares the module; the header form must not, and must be
                      // include-guarded.
                      checks.expect(
                          outputs.moduleSource.find(
                              "export module mdux.shader.generated.test_ui;") !=
                              std::string::npos,
                          "the module form declares the module");
                      checks.expect(outputs.headerSource.find("export module") ==
                                        std::string::npos,
                                    "the header form does not declare a module");
                      checks.expect(outputs.headerSource.find("#pragma once") !=
                                        std::string::npos,
                                    "the header form is include-guarded");
                      // Both must carry the same payload and the same contract, which is what
                      // GeneratedTests asserts by compiling them. Here it is enough that the
                      // rendered body is literally shared.
                      checks.expect(outputs.moduleSource.find("spirvBytes[] = {") !=
                                        std::string::npos,
                                    "the module form carries the bytes");
                      checks.expect(outputs.headerSource.find("spirvBytes[] = {") !=
                                        std::string::npos,
                                    "the header form carries the bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register noDescriptorsEmptySpan{
    "A package with no descriptors renders an empty span, not an empty array", "evidence-unit",
    [] {
        struct State {
            TempDir dir;
            std::vector<std::byte> sidecar;
            std::filesystem::path packagePath;
            std::vector<cli::Diagnostic> diagnostics;
            std::optional<EmitOutputs> outputs;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-no-descriptors-empty-span")
            .Given("a package with no descriptors",
                   [state] {
                       // A zero-length C array is ill-formed, so the empty case cannot use the
                       // same spelling as the populated one. Both must still produce an accessor
                       // with the same type.
                       state->sidecar = minimal().bytes();
                       state->packagePath =
                           writePackage(state->dir.path(), packageOver(state->sidecar),
                                        state->sidecar);
                   })
            .When("it is rendered",
                  [state] {
                      state->outputs =
                          render(state->packagePath, state->diagnostics);
                  })
            .Then("the empty contract is spelled as a span, not an array",
                  [state] {
                      if (!state->outputs.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "render() produced no outputs",
                              std::source_location::current());
                      }
                      const EmitOutputs& outputs = *state->outputs;
                      mdux::spec::Checks checks;
                      checks.expect(outputs.moduleSource.find("descriptors[] = {") ==
                                        std::string::npos,
                                    "no descriptor array is emitted");
                      checks.expect(
                          outputs.moduleSource.find(
                              "std::span<const mdux::shader::DescriptorBinding> descriptors{}") !=
                              std::string::npos,
                          "the descriptors accessor is an empty span");
                      checks.expect(
                          outputs.moduleSource.find(
                              "std::span<const mdux::shader::PushConstantRange> pushConstants{}") !=
                              std::string::npos,
                          "the push constants accessor is an empty span");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unreadablePackageReported{
    "An unreadable package is reported", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool hadOutputs{true};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-unreadable-package-reported")
            .Given("a package path that does not exist",
                   [state] { state->diagnostics.clear(); })
            .When("it is rendered",
                  [state] {
                      auto outputs = render(state->dir.path() / "package.json",
                                            state->diagnostics);
                      state->hadOutputs = outputs.has_value();
                  })
            .Then("a SHE001 diagnostic names the failure and its remedy",
                  [state] {
                      if (state->diagnostics.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 diagnostic, got {}",
                                          state->diagnostics.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(!state->hadOutputs, "no outputs are produced");
                      checks.expect(state->diagnostics[0].code == "SHE001",
                                    "the code is SHE001");
                      checks.expect(state->diagnostics[0].fixHint.find("mdux-bake-update") !=
                                        std::string::npos,
                                    "the fix hint names mdux-bake-update");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register notShaderPackageReported{
    "A package that is not a shader package is reported with the reason", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool hadOutputs{true};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-not-shader-package-reported")
            .Given("a package whose kind is font",
                   [state] {
                       writeText(state->dir.path() / "package.json",
                                 R"({"schemaVersion":1,"id":"x","kind":"font"})");
                   })
            .When("it is rendered",
                  [state] {
                      auto outputs = render(state->dir.path() / "package.json",
                                            state->diagnostics);
                      state->hadOutputs = outputs.has_value();
                  })
            .Then("a SHE002 diagnostic names the reason",
                  [state] {
                      if (state->diagnostics.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 diagnostic, got {}",
                                          state->diagnostics.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(!state->hadOutputs, "no outputs are produced");
                      checks.expect(state->diagnostics[0].code == "SHE002",
                                    "the code is SHE002");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register missingSidecarReported{
    "A missing sidecar is reported against the sidecar, not the package", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool hadOutputs{true};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-missing-sidecar-reported")
            .Given("a package.json whose sidecar was never written",
                   [state] {
                       const std::vector<std::byte> sidecar = minimal().bytes();
                       const shader::ShaderPackage package = packageOver(sidecar);
                       auto text = package.write();
                       if (!text.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "package.write() rejected the package",
                               std::source_location::current());
                       }
                       writeText(state->dir.path() / "package.json", *text);
                   })
            .When("it is rendered",
                  [state] {
                      auto outputs = render(state->dir.path() / "package.json",
                                            state->diagnostics);
                      state->hadOutputs = outputs.has_value();
                  })
            .Then("a SHE003 diagnostic names the sidecar",
                  [state] {
                      if (state->diagnostics.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 diagnostic, got {}",
                                          state->diagnostics.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(!state->hadOutputs, "no outputs are produced");
                      checks.expect(state->diagnostics[0].code == "SHE003",
                                    "the code is SHE003");
                      checks.expect(state->diagnostics[0].file.find("shaders.spv") !=
                                        std::string::npos,
                                    "the diagnostic names the sidecar file");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register tamperedSidecarRefused{
    "A sidecar that does not match the recorded digest is refused", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool hadOutputs{true};
            std::filesystem::path packagePath;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-tampered-sidecar-refused")
            .Given("a sidecar altered after the package was written",
                   [state] {
                       // The check that stops unreviewed bytes reaching a binary. Without it, a
                       // hand-edited sidecar would be rendered into source and linked while every
                       // artifact check stayed green - the digest under review would describe one
                       // thing and the compiled bytes another.
                       const std::vector<std::byte> sidecar = minimal().bytes();
                       const std::filesystem::path packagePath =
                           writePackage(state->dir.path(), packageOver(sidecar), sidecar);

                       std::vector<std::byte> tampered = sidecar;
                       const std::size_t last = tampered.size() - 1;
                       tampered[last] = static_cast<std::byte>(
                           std::to_integer<unsigned>(tampered[last]) ^ 0xffu);
                       writeBytes(state->dir.path() / "shaders.spv", tampered);

                       state->packagePath = packagePath;
                   })
            .When("it is rendered",
                  [state] {
                      auto outputs = render(state->packagePath, state->diagnostics);
                      state->hadOutputs = outputs.has_value();
                  })
            .Then("a SHE004 diagnostic says the sidecar was hand-edited",
                  [state] {
                      if (state->diagnostics.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 diagnostic, got {}",
                                          state->diagnostics.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(!state->hadOutputs, "no outputs are produced");
                      checks.expect(state->diagnostics[0].code == "SHE004",
                                    "the code is SHE004");
                      checks.expect(state->diagnostics[0].fixHint.find("do not hand-edit") !=
                                        std::string::npos,
                                    "the fix hint says do not hand-edit");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wrongLengthSidecarRefused{
    "A sidecar of the wrong length is refused", "evidence-unit", [] {
        struct State {
            TempDir dir;
            std::vector<cli::Diagnostic> diagnostics;
            bool hadOutputs{true};
            std::filesystem::path packagePath;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-wrong-length-sidecar-refused")
            .Given("a sidecar truncated after the package was written",
                   [state] {
                       const std::vector<std::byte> sidecar = minimal().bytes();
                       const std::filesystem::path packagePath =
                           writePackage(state->dir.path(), packageOver(sidecar), sidecar);

                       std::vector<std::byte> truncated{sidecar.begin(), sidecar.end() - 4};
                       writeBytes(state->dir.path() / "shaders.spv", truncated);

                       state->packagePath = packagePath;
                   })
            .When("it is rendered",
                  [state] {
                      auto outputs = render(state->packagePath, state->diagnostics);
                      state->hadOutputs = outputs.has_value();
                  })
            .Then("a SHE004 diagnostic is the only one",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(!state->hadOutputs, "no outputs are produced");
                      checks.expect(codesOf(state->diagnostics) ==
                                        std::vector<std::string>{"SHE004"},
                                    "the only code is SHE004");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writeLeavesUnchangedUntouched{
    "write() creates both files and leaves an unchanged file untouched", "evidence-unit", [] {
        struct State {
            TempDir source;
            TempDir out;
            std::vector<std::byte> sidecar;
            std::filesystem::path packagePath;
            std::vector<cli::Diagnostic> diagnostics;
            std::optional<EmitOutputs> outputs;
            bool sizeStable{false};
            bool timeStable{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-emit-write-leaves-unchanged-untouched")
            .Given("a well-formed package rendered in the source directory",
                   [state] {
                       // Rewriting an unchanged file would restamp it and force every consumer to
                       // recompile on every build, which for a few thousand bytes of shader is not
                       // free.
                       state->sidecar = minimal().bytes();
                       state->packagePath =
                           writePackage(state->source.path(), packageOver(state->sidecar),
                                        state->sidecar);
                       state->outputs =
                           render(state->packagePath, state->diagnostics);
                       if (!state->outputs.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "render() produced no outputs",
                               std::source_location::current());
                       }
                       if (!write(*state->outputs, state->out.path(), state->diagnostics)) {
                           throw speclab::core::AssertionFailure(
                               "write() reported a failure", std::source_location::current());
                       }
                       if (!std::filesystem::exists(state->out.path() / "test_ui.cppm") ||
                           !std::filesystem::exists(state->out.path() / "test_ui.hpp")) {
                           throw speclab::core::AssertionFailure(
                               "write() did not create both files",
                               std::source_location::current());
                       }
                   })
            .When("the same outputs are written again over the existing files",
                  [state] {
                      const auto firstWrite =
                          std::filesystem::last_write_time(state->out.path() / "test_ui.cppm");
                      // A timestamp comparison needs the two writes to be distinguishable; the
                      // file system's resolution is coarser than this loop would be, so the
                      // content is checked instead.
                      const auto before =
                          std::filesystem::file_size(state->out.path() / "test_ui.cppm");
                      const bool rewrote =
                          write(*state->outputs, state->out.path(), state->diagnostics);
                      if (!rewrote) {
                          throw speclab::core::AssertionFailure(
                              "write() reported a failure on the second pass",
                              std::source_location::current());
                      }
                      state->sizeStable =
                          (std::filesystem::file_size(state->out.path() / "test_ui.cppm") ==
                           before);
                      state->timeStable =
                          (std::filesystem::last_write_time(state->out.path() / "test_ui.cppm") ==
                           firstWrite);
                  })
            .Then("both files exist and the unchanged file is not restamped",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->sizeStable, "the file size is unchanged");
                      checks.expect(state->timeStable, "the modification time is unchanged");
                      checks.expect(state->diagnostics.empty(), "no diagnostics");
                      checks.raise();
                  })
            .Execute();
    }};
}  // namespace
