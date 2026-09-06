/**
 * @file UiPackageTests.cpp
 * @brief BDD scenarios for the committed MduX UI shader package, converted from the Wave 3
 *        MduXTest suite (issue #141).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * `evidence.shader.mdux-ui` already proves the committed artifact is what the baker produces from
 * the committed sources. It cannot prove the artifact is what the *renderer* needs - re-baking an
 * edited shader updates both sides at once and the comparison stays green while the pipeline
 * contract silently moves.
 *
 * These scenarios are the other half. They assert the contract in one place, so changing a
 * descriptor binding, a push-constant size, or which stage sees what is a deliberate edit here
 * rather than a surprise in #124 or a validation-layer message on a device.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`).
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.spirv;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace shader = mdux::shader;
namespace evidence = mdux::evidence;
namespace spirv = mdux::tools::spirv;

const std::filesystem::path packageDir =
    std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "shader" / "mdux-ui";

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    // tellg() answers -1 on a stream error rather than throwing. Casting that to an unsigned
    // size would ask for a vector of 2^64-1 bytes, so the failure has to be caught here - the
    // symptom otherwise is a bad_alloc or a kill, neither of which names the file.
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

/// Hard failure (REQUIRE-equivalent): the committed package must have parsed.
[[nodiscard]] shader::ShaderPackage requirePackage(
    mdux::core::Result<shader::ShaderPackage, shader::SchemaError> result, std::string_view what,
    std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        // `where` defaults at the call site, so a failure names the step that called this rather
        // than this line - and the SchemaError says *why* the committed artifact is unreadable,
        // which is the whole question when this fails.
        throw speclab::core::AssertionFailure(
            std::format("{}: the committed package did not parse: {}", what,
                        shader::describe(result.error())),
            where);
    }
    return std::move(*result);
}

const mdux::spec::Register committedParses{
    "The committed UI package parses and validates", "evidence-unit", [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-committed-parses")
            .Given("the committed UI package",
                   [state] { state->package = requirePackage(committedPackage(), "the package"); })
            .When("it is inspected", [] {})
            .Then("it parses with the expected header",
                  [state] {
                      // Guards every assertion below, and doubles as a check that the strict
                      // reader accepts what the writer committed - a round trip through the
                      // filesystem and a git checkout.
                      mdux::spec::Checks checks;
                      checks.expect(state->package->header.id == "mdux-ui", "the header id");
                      checks.expect(state->package->header.kind == "shader", "the header kind");
                      checks.expect(state->package->sidecarPath == "shaders.spv",
                                    "the sidecar path");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register vertexAndFragmentOnly{
    "The UI package provides exactly a vertex and a fragment module", "evidence-unit", [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
            const shader::ShaderModule* vertex{nullptr};
            const shader::ShaderModule* fragment{nullptr};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-vertex-and-fragment-only")
            .Given("the committed UI package",
                   [state] { state->package = requirePackage(committedPackage(), "the package"); })
            .When("its modules are looked up", [state] {
                if (state->package->modules.size() != 2) {
                    throw speclab::core::AssertionFailure(
                        std::format("expected 2 modules, got {}", state->package->modules.size()),
                        std::source_location::current());
                }
                state->vertex = state->package->find("ui.vert");
                state->fragment = state->package->find("ui.frag");
                if (state->vertex == nullptr || state->fragment == nullptr) {
                    throw speclab::core::AssertionFailure(
                        "one of the expected modules was not found",
                        std::source_location::current());
                }
            })
            .Then("it provides a vertex and a fragment module with entry point main",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->vertex->stage == shader::Stage::Vertex,
                                    "the vertex module stage");
                      checks.expect(state->vertex->entryPoint == "main",
                                    "the vertex module entry point");
                      checks.expect(state->fragment->stage == shader::Stage::Fragment,
                                    "the fragment module stage");
                      checks.expect(state->fragment->entryPoint == "main",
                                    "the fragment module entry point");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register twoCombinedImageSamplers{
    "The UI pipeline binds fixed coverage and RGBA samplers", "evidence-unit", [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-two-combined-image-samplers")
            .Given("the committed UI package",
                   [state] { state->package = requirePackage(committedPackage(), "the package"); })
            .When("its descriptors are inspected", [state] {
                if (state->package->descriptors.size() != 2) {
                    throw speclab::core::AssertionFailure(
                        std::format("expected 2 descriptors, got {}",
                                    state->package->descriptors.size()),
                        std::source_location::current());
                }
            })
            .Then("the atlas bindings have fixed sets, bindings, kinds, counts and stages",
                  [state] {
                      // The atlas is bound for every draw, including one that is entirely solid: a
                      // descriptor set whose shape depended on the content would put a conditional
                      // in the renderer's hot path and in its budget, which is the opposite of
                      // fixed.
                      const shader::DescriptorBinding& coverage = state->package->descriptors[0];
                      const shader::DescriptorBinding& rgba = state->package->descriptors[1];
                      mdux::spec::Checks checks;
                      checks.expect(coverage.set == 0 && rgba.set == 0, "both sets are 0");
                      checks.expect(coverage.binding == 0 && rgba.binding == 1,
                                    "coverage is binding 0 and RGBA is binding 1");
                      checks.expect(
                          coverage.kind == shader::DescriptorKind::CombinedImageSampler &&
                              rgba.kind == shader::DescriptorKind::CombinedImageSampler,
                          "both kinds are CombinedImageSampler");
                      checks.expect(coverage.count == 1 && rgba.count == 1,
                                    "both counts are 1");
                      // The fragment stage alone samples it. A vertex bit here would mean the
                      // pipeline layout requested access no shader uses, which the validation
                      // layers report as a warning and which costs a descriptor slot on a device
                      // that has few.
                      checks.expect(
                          coverage.stages == shader::fragmentBit &&
                              rgba.stages == shader::fragmentBit,
                          "both stages are fragment only");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register eightBytePushConstant{
    "The UI pipeline takes an 8-byte vertex-only push constant", "evidence-unit", [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-eight-byte-push-constant")
            .Given("the committed UI package",
                   [state] { state->package = requirePackage(committedPackage(), "the package"); })
            .When("its push constants are inspected", [state] {
                if (state->package->pushConstants.size() != 1) {
                    throw speclab::core::AssertionFailure(
                        std::format("expected 1 push constant range, got {}",
                                    state->package->pushConstants.size()),
                        std::source_location::current());
                }
            })
            .Then("the range is 8 bytes at offset 0 for the vertex stage",
                  [state] {
                      // The viewport size, so a governed draw list can hold pixel coordinates and
                      // contain no projection maths and no dependency on the surface it will be
                      // drawn to.
                      const shader::PushConstantRange& range = state->package->pushConstants.front();
                      mdux::spec::Checks checks;
                      checks.expect(range.offset == 0, "the offset is 0");
                      checks.expect(range.size == 8, "the size is 8 bytes");
                      checks.expect(range.stages == shader::vertexBit,
                                    "the stage is vertex only");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sidecarDigestsAgree{
    "The sidecar matches the digests the package records", "evidence-unit", [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
            std::optional<std::vector<std::byte>> sidecar;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-sidecar-digests-agree")
            .Given("the committed package and its sidecar",
                   [state] {
                       state->package =
                           requirePackage(committedPackage(), "the package");
                       state->sidecar = readFile(packageDir / state->package->sidecarPath);
                       if (!state->sidecar.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "the sidecar could not be read", std::source_location::current());
                       }
                   })
            .When("the recorded digests are compared against the bytes beside them", [] {})
            .Then("the sidecar and every module digest match the package records",
                  [state] {
                      // The package's own claims, checked against the bytes beside it.
                      // `evidence.shader.mdux-ui` compares both files against a fresh bake; this
                      // checks they agree with *each other*, which is what a consumer that only
                      // has the committed directory can rely on.
                      mdux::spec::Checks checks;
                      checks.expect(state->sidecar->size() == state->package->sidecarByteLength,
                                    "the sidecar length matches");
                      checks.expect(evidence::sha256(*state->sidecar) ==
                                        state->package->sidecarSha256,
                                    "the sidecar digest matches");

                      for (const shader::ShaderModule& module : state->package->modules) {
                          const std::span<const std::byte> range{
                              state->sidecar->data() + module.byteOffset,
                              static_cast<std::size_t>(module.byteLength)};
                          checks.expect(evidence::sha256(range) == module.sha256,
                                        std::format("the digest of module '{}' matches", module.id));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register modulesWellFormedSpirv{
    "Every module in the sidecar is well-formed SPIR-V for its declared stage", "evidence-unit",
    [] {
        struct State {
            std::optional<shader::ShaderPackage> package;
            std::optional<std::vector<std::byte>> sidecar;
            std::vector<mdux::tools::spirv::Reflection> reflections;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-ui-modules-well-formed-spirv")
            .Given("the committed package and its sidecar",
                   [state] {
                       state->package =
                           requirePackage(committedPackage(), "the package");
                       state->sidecar = readFile(packageDir / state->package->sidecarPath);
                       if (!state->sidecar.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "the sidecar could not be read", std::source_location::current());
                       }
                   })
            .When("each module range is reflected", [state] {
                for (const shader::ShaderModule& module : state->package->modules) {
                    // validate() bounded these ranges against the length package.json *declares*
                    // for the sidecar. This span is over the bytes actually read, so a truncated
                    // file still runs off the end - UB in place of the test failure this scenario
                    // exists to produce. Subtraction, because the sum can wrap.
                    const std::size_t sidecarSize = state->sidecar->size();
                    if (module.byteOffset > sidecarSize ||
                        module.byteLength > sidecarSize - module.byteOffset) {
                        throw speclab::core::AssertionFailure(
                            std::format("module '{}' spans [{}, {}) of a sidecar that is {} bytes "
                                        "on disk",
                                        module.id, module.byteOffset,
                                        module.byteOffset + module.byteLength, sidecarSize),
                            std::source_location::current());
                    }
                    const std::span<const std::byte> range{
                        state->sidecar->data() + module.byteOffset,
                        static_cast<std::size_t>(module.byteLength)};

                    // Reflecting the range rather than checking its first four bytes is what
                    // makes this test match its own name. The magic number only says the range
                    // starts like SPIR-V; reflection parses it and reports the execution model, so
                    // two modules swapped in the sidecar - both valid SPIR-V, both correctly
                    // digested, each under the other's id - fails here. That is the failure a
                    // magic-number check cannot see, and it reaches a device as a vertex shader
                    // bound to the fragment stage.
                    auto reflection = spirv::reflect(range);
                    if (!reflection.has_value()) {
                        throw speclab::core::AssertionFailure(
                            std::format("module '{}' did not reflect", module.id),
                            std::source_location::current());
                    }
                    state->reflections.push_back(*reflection);
                }
            })
            .Then("each reflects to its declared stage, entry point and portable SPIR-V version",
                  [state] {
                      mdux::spec::Checks checks;
                      for (std::size_t i = 0; i < state->package->modules.size(); ++i) {
                          const shader::ShaderModule& module = state->package->modules[i];
                          const mdux::tools::spirv::Reflection& reflection = state->reflections[i];
                          checks.expect(reflection.stage == module.stage,
                                        std::format("module '{}' reflects to its stage",
                                                    module.id));
                          checks.expect(reflection.entryPoint == module.entryPoint,
                                        std::format("module '{}' reflects to its entry point",
                                                    module.id));
                          checks.expect(reflection.versionMajor == 1 &&
                                            reflection.versionMinor <= 5,
                                        std::format(
                                            "module '{}' is compatible with Vulkan 1.2",
                                            module.id));
                          checks.expect(module.byteLength % 4 == 0,
                                        std::format("module '{}' is word aligned", module.id));
                      }
                      checks.raise();
                  })
            .Execute();
    }};
}  // namespace
