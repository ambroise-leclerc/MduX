/**
 * @file CommittedPackageTests.cpp
 * @brief Contract scenarios that apply to every committed shader package, not to one of them.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-013 Verified Apple Silicon macOS toolchain
 *
 * UiPackageTests covers the `mdux-ui` package in depth. The properties asserted here are the ones
 * that must hold for *all* of them, and they live in their own file because the alternative -
 * copying an assertion into a per-package file - is how `triangle` ended up with no SPIR-V version
 * guard while `mdux-ui` had one, even though the two were re-baked in the same commit for the same
 * reason.
 *
 * The package set is discovered rather than listed, so adding a package under generated/shader/
 * puts it under these checks without anyone remembering to.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.shader.schema;
import mdux.tools.spirv;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace shader = mdux::shader;
namespace spirv = mdux::tools::spirv;

const std::filesystem::path generatedShaderDir =
    std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "shader";

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    // tellg() answers -1 on a stream error rather than throwing, and casting that to an unsigned
    // size asks for a vector of 2^64-1 bytes. See the same guard in UiPackageTests.cpp.
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    if (!file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return std::nullopt;
    }
    return bytes;
}

/// Every directory under generated/shader/ holding a package.json, in a stable order so a failure
/// names the same package on every machine.
[[nodiscard]] std::vector<std::filesystem::path> committedPackageDirs() {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{generatedShaderDir, ec}) {
        if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "package.json", ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::ranges::sort(dirs);
    return dirs;
}

const mdux::spec::Register everyPackageTargetsPortableSpirv{
    "Every committed shader package targets SPIR-V a Vulkan 1.2 implementation accepts",
    "evidence-unit", [] {
        struct Module {
            std::string package;
            std::string id;
            spirv::Reflection reflection;
        };
        struct State {
            std::vector<std::filesystem::path> dirs;
            std::vector<Module> modules;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-committed-packages-portable-spirv")
            .Given("every committed shader package",
                   [state] {
                       state->dirs = committedPackageDirs();
                       // A discovery-based test that discovers nothing passes every assertion
                       // below without checking anything. Renaming the directory, or running the
                       // binary with MDUX_REPO_ROOT pointing somewhere else, must fail here rather
                       // than report success over an empty set.
                       if (state->dirs.empty()) {
                           throw speclab::core::AssertionFailure(
                               std::format("no committed packages found under {}",
                                           generatedShaderDir.string()),
                               std::source_location::current());
                       }
                   })
            .When("each module in each package is reflected",
                  [state] {
                      for (const std::filesystem::path& dir : state->dirs) {
                          const std::string name = dir.filename().string();
                          auto text = readFile(dir / "package.json");
                          if (!text.has_value()) {
                              throw speclab::core::AssertionFailure(
                                  std::format("package '{}' could not be read", name),
                                  std::source_location::current());
                          }
                          auto parsed = shader::ShaderPackage::parse(std::string_view{
                              reinterpret_cast<const char*>(text->data()), text->size()});
                          if (!parsed.has_value()) {
                              throw speclab::core::AssertionFailure(
                                  std::format("package '{}' did not parse: {}", name,
                                              shader::describe(parsed.error())),
                                  std::source_location::current());
                          }
                          auto sidecar = readFile(dir / parsed->sidecarPath);
                          if (!sidecar.has_value()) {
                              throw speclab::core::AssertionFailure(
                                  std::format("package '{}' sidecar '{}' could not be read", name,
                                              parsed->sidecarPath),
                                  std::source_location::current());
                          }
                          for (const shader::ShaderModule& module : parsed->modules) {
                              // ShaderPackage::validate() already checked these ranges, but against
                              // the sidecar length *declared in package.json*. This span is over the
                              // bytes actually on disk, and a truncated sidecar is exactly the bad
                              // committed artifact this file exists to catch - so it has to fail
                              // here rather than read past the buffer. Subtraction rather than
                              // `byteOffset + byteLength`, which can wrap.
                              const std::size_t sidecarSize = sidecar->size();
                              if (module.byteOffset > sidecarSize ||
                                  module.byteLength > sidecarSize - module.byteOffset) {
                                  throw speclab::core::AssertionFailure(
                                      std::format("module '{}' of package '{}' spans [{}, {}) of a "
                                                  "sidecar that is {} bytes on disk",
                                                  module.id, name, module.byteOffset,
                                                  module.byteOffset + module.byteLength,
                                                  sidecarSize),
                                      std::source_location::current());
                              }
                              const std::span<const std::byte> range{
                                  sidecar->data() + module.byteOffset,
                                  static_cast<std::size_t>(module.byteLength)};
                              auto reflection = spirv::reflect(range);
                              if (!reflection.has_value()) {
                                  throw speclab::core::AssertionFailure(
                                      std::format("module '{}' of package '{}' did not reflect",
                                                  module.id, name),
                                      std::source_location::current());
                              }
                              state->modules.push_back({name, module.id, *reflection});
                          }
                      }
                  })
            .Then("each module is SPIR-V 1.5 or lower",
                  [state] {
                      // MoltenVK exposes Vulkan 1.2 semantics and rejects the SPIR-V 1.6 that
                      // `--target-env vulkan1.3` emits. The recipes under recipes/shader/ say so
                      // in a comment; this is the assertion that makes re-baking at 1.3 fail here
                      // rather than at runtime on a device - and only on the one CI leg that has
                      // a device.
                      mdux::spec::Checks checks;
                      for (const Module& module : state->modules) {
                          checks.expect(
                              module.reflection.versionMajor == 1 &&
                                  module.reflection.versionMinor <= 5,
                              std::format("module '{}' of package '{}' is SPIR-V {}.{}, which a "
                                          "Vulkan 1.2 implementation such as MoltenVK rejects; "
                                          "re-bake it with --target-env vulkan1.2",
                                          module.id, module.package,
                                          module.reflection.versionMajor,
                                          module.reflection.versionMinor));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
