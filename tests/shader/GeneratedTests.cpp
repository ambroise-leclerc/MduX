/**
 * @file GeneratedTests.cpp
 * @brief BDD scenarios for the generated module and header forms, converted from the Wave 3
 *        MduXTest suite (issue #141).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The generated module and header must describe identical bytes and an identical contract,
 * and both must agree with the committed artifact they were rendered from.
 *
 * Three things are being checked, and they are not the same thing:
 *
 *  1. The two generated forms agree with each other. If they ever diverge, whichever of #122 or
 *     #124 used the other one would be linking different shaders than the reviewer read.
 *  2. The generated data agrees with `generated/shader/mdux-ui/package.json`. The emitter is a
 *     mechanical rendering, so a disagreement means the rendering is wrong.
 *  3. The bytes really are the reviewed bytes - checked against the digest the committed package
 *     records, which is the number a reviewer actually looked at.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`).
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.shaderemit;

#include "../framework/SpecLabBridge.hpp"
#include "GeneratedConsumers.hpp"

namespace {

namespace shader = mdux::shader;
namespace evidence = mdux::evidence;
using mdux::test::generated::fromHeader;
using mdux::test::generated::fromModule;

const std::filesystem::path packageDir =
    std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "shader" / "mdux-ui";

[[nodiscard]] mdux::core::Result<shader::ShaderPackage, shader::SchemaError> committedPackage() {
    std::ifstream file{packageDir / "package.json", std::ios::binary | std::ios::ate};
    if (!file) {
        return mdux::core::err(shader::SchemaError::MalformedPackage);
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), size);
    return shader::ShaderPackage::parse(text);
}

/// Hard failure (REQUIRE-equivalent): the committed package must have parsed.
[[nodiscard]] shader::ShaderPackage requireCommitted(
    mdux::core::Result<shader::ShaderPackage, shader::SchemaError> result,
    std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        // `where` defaults at the call site, so the failure names the calling step; the
        // SchemaError names the parse or validation reason, which is what makes it actionable.
        throw speclab::core::AssertionFailure(
            std::format("the committed package did not parse: {}",
                        shader::describe(result.error())),
            where);
    }
    return std::move(*result);
}

const mdux::spec::Register moduleHeaderByteIdentical{
    "The module and header forms expose byte-identical SPIR-V", "evidence-unit", [] {
        struct State {
            shader::PackageView fromM;
            shader::PackageView fromH;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-module-header-byte-identical")
            .Given("both generated forms of the UI package",
                   [state] {
                       state->fromM = fromModule();
                       state->fromH = fromHeader();
                   })
            .When("their payloads are compared", [state] {
                if (state->fromM.spirv.size() != state->fromH.spirv.size()) {
                    throw speclab::core::AssertionFailure(
                        std::format("the module form has {} bytes, the header form {}",
                                    state->fromM.spirv.size(), state->fromH.spirv.size()),
                        std::source_location::current());
                }
            })
            .Then("the SPIR-V is identical elementwise and by digest",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::equal(state->fromM.spirv, state->fromH.spirv),
                                    "the bytes match elementwise");
                      // Compared by digest as well as elementwise: an elementwise loop over
                      // equal-length spans that were somehow the same span would pass vacuously,
                      // and the digest would not.
                      checks.expect(evidence::sha256(state->fromM.spirv) ==
                                        evidence::sha256(state->fromH.spirv),
                                    "the digests match");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleHeaderContractIdentical{
    "The module and header forms expose an identical contract", "evidence-unit", [] {
        struct State {
            shader::PackageView fromM;
            shader::PackageView fromH;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-module-header-contract-identical")
            .Given("both generated forms of the UI package",
                   [state] {
                       state->fromM = fromModule();
                       state->fromH = fromHeader();
                   })
            .When("their contracts are compared", [state] {
                if (state->fromM.modules.size() != state->fromH.modules.size() ||
                    state->fromM.descriptors.size() != state->fromH.descriptors.size() ||
                    state->fromM.pushConstants.size() != state->fromH.pushConstants.size()) {
                    throw speclab::core::AssertionFailure(
                        "the two forms declare different contract sizes",
                        std::source_location::current());
                }
            })
            .Then("the id, modules, descriptors and push constants all match",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->fromM.id == state->fromH.id, "the id");
                      checks.expect(std::ranges::equal(state->fromM.modules,
                                                      state->fromH.modules),
                                    "the modules");
                      checks.expect(std::ranges::equal(state->fromM.descriptors,
                                                      state->fromH.descriptors),
                                    "the descriptors");
                      checks.expect(std::ranges::equal(state->fromM.pushConstants,
                                                      state->fromH.pushConstants),
                                    "the push constants");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register generatedMatchesCommitted{
    "The generated package matches the committed artifact it was rendered from", "evidence-unit",
    [] {
        struct State {
            shader::ShaderPackage committed;
            shader::PackageView view;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-generated-matches-committed")
            .Given("the committed UI package and the generated module form",
                   [state] {
                       state->committed = requireCommitted(committedPackage());
                       state->view = fromModule();
                   })
            .When("the generated view is compared against the committed package", [state] {
                if (state->view.modules.size() != state->committed.modules.size() ||
                    state->view.descriptors.size() != state->committed.descriptors.size() ||
                    state->view.pushConstants.size() != state->committed.pushConstants.size()) {
                    throw speclab::core::AssertionFailure(
                        "the generated view and the committed package differ in size",
                        std::source_location::current());
                }
            })
            .Then("the id, bytes and every module, descriptor and push constant match",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->view.id == state->committed.header.id,
                                    "the id");
                      checks.expect(state->view.spirv.size() ==
                                        state->committed.sidecarByteLength,
                                    "the sidecar byte length");
                      // The digest a reviewer signed off on, against the bytes that will be
                      // linked into a device binary. This is the assertion that makes "generated
                      // code needs no review" defensible.
                      checks.expect(evidence::sha256(state->view.spirv) ==
                                        state->committed.sidecarSha256,
                                    "the sidecar digest");

                      for (std::size_t i = 0; i < state->view.modules.size(); ++i) {
                          checks.expect(state->view.modules[i].id ==
                                            state->committed.modules[i].id,
                                        std::format("module {} id", i));
                          checks.expect(state->view.modules[i].stage ==
                                            state->committed.modules[i].stage,
                                        std::format("module {} stage", i));
                          checks.expect(state->view.modules[i].entryPoint ==
                                            state->committed.modules[i].entryPoint,
                                        std::format("module {} entry point", i));
                          checks.expect(state->view.modules[i].byteOffset ==
                                            state->committed.modules[i].byteOffset,
                                        std::format("module {} offset", i));
                          checks.expect(state->view.modules[i].byteLength ==
                                            state->committed.modules[i].byteLength,
                                        std::format("module {} length", i));
                      }
                      for (std::size_t i = 0; i < state->view.descriptors.size(); ++i) {
                          checks.expect(state->view.descriptors[i] ==
                                            state->committed.descriptors[i],
                                        std::format("descriptor {}", i));
                      }
                      for (std::size_t i = 0; i < state->view.pushConstants.size(); ++i) {
                          checks.expect(state->view.pushConstants[i] ==
                                            state->committed.pushConstants[i],
                                        std::format("push constant {}", i));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register generatedRangesMatchDigests{
    "Each module's generated range matches the digest the package records", "evidence-unit", [] {
        struct State {
            shader::ShaderPackage committed;
            shader::PackageView view;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-generated-ranges-match-digests")
            .Given("the committed UI package and the generated module form",
                   [state] {
                       state->committed = requireCommitted(committedPackage());
                       state->view = fromModule();
                   })
            .When("each module's range is read from the view", [state] {
                for (const shader::ShaderModule& module : state->committed.modules) {
                    const std::span<const std::byte> bytes = state->view.moduleSpirv(module.id);
                    if (bytes.empty()) {
                        throw speclab::core::AssertionFailure(
                            std::format("module '{}' produced an empty range", module.id),
                            std::source_location::current());
                    }
                }
            })
            .Then("each range has the recorded length and digest",
                  [state] {
                      mdux::spec::Checks checks;
                      for (const shader::ShaderModule& module : state->committed.modules) {
                          const std::span<const std::byte> bytes =
                              state->view.moduleSpirv(module.id);
                          checks.expect(bytes.size() == module.byteLength,
                                        std::format("module '{}' length", module.id));
                          checks.expect(evidence::sha256(bytes) == module.sha256,
                                        std::format("module '{}' digest", module.id));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unknownIdEmptySpan{
    "moduleSpirv() returns an empty span for an unknown id", "evidence-unit", [] {
        struct State {
            shader::PackageView view;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-unknown-id-empty-span")
            .Given("the generated module form",
                   [state] { state->view = fromModule(); })
            .When("an unknown id is looked up", [] {})
            .Then("it reports a miss and an empty span",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->view.find("ui.geom") == nullptr,
                                    "find() reports a miss");
                      checks.expect(state->view.moduleSpirv("ui.geom").empty(),
                                    "moduleSpirv() returns an empty span");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register escapingRangeRefused{
    "moduleSpirv() refuses a range that escapes the payload", "evidence-unit", [] {
        return speclab::Test("shader-gen-escaping-range-refused")
            .Given("a hand-assembled view with a range past the payload", [] {})
            .When("that range is read", [] {})
            .Then("an empty span is returned",
                  [] {
                      // The generated data is machine-written, but a view can also be assembled by
                      // hand - as it is here - and a span past the end of the payload is the one
                      // mistake that would not fail visibly. Bounds are checked rather than
                      // assumed.
                      static constexpr std::array<std::byte, 8> payload{};
                      static constexpr std::array<shader::ModuleView, 1> modules{
                          shader::ModuleView{.id = "bad",
                                             .stage = shader::Stage::Vertex,
                                             .entryPoint = "",
                                             .byteOffset = 4,
                                             .byteLength = 16}};
                      const shader::PackageView view{.id = "hand-made",
                                                     .spirv = payload,
                                                     .modules = modules,
                                                     .descriptors = {},
                                                     .pushConstants = {}};

                      mdux::spec::Checks checks;
                      checks.expect(view.find("bad") != nullptr, "find() locates the module");
                      checks.expect(view.moduleSpirv("bad").empty(),
                                    "moduleSpirv() refuses the escaping range");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register identifierParity{
    "The generated identifier matches what the build predicts", "evidence-unit", [] {
        struct State {
            std::size_t compared{0};
            std::vector<std::string> mismatches;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-gen-identifier-parity")
            .Given("the identifier parity file the build wrote at configure time", [] {})
            .When("each CMake-computed identifier is compared against identifierFor()",
                  [state] {
                      // cmake/MduXShaderEmit.cmake derives the generated filenames from the
                      // package id and must agree with identifierFor() exactly; a mismatch
                      // surfaces as a build failure on a file nobody wrote, with nothing pointing
                      // at the cause.
                      //
                      // The comparison is against what CMake *actually computed*, written to a
                      // file at configure time by mdux_shader_identifier(). Asserting
                      // identifierFor()'s return values here - which is what this test used to do
                      // - pins one side of a two-side agreement and cannot observe the other
                      // drifting. It did drift: the CMake half never applied the
                      // leading-underscore rule.
                      std::ifstream parity{MDUX_IDENTIFIER_PARITY_FILE};
                      if (!parity.is_open()) {
                          throw speclab::core::AssertionFailure(
                              "the parity file could not be opened",
                              std::source_location::current());
                      }

                      std::string line;
                      while (std::getline(parity, line)) {
                          if (line.empty()) {
                              continue;
                          }
                          const std::size_t tab = line.find('\t');
                          if (tab == std::string::npos) {
                              throw speclab::core::AssertionFailure(
                                  "a parity line had no tab separator",
                                  std::source_location::current());
                          }
                          const std::string id = line.substr(0, tab);
                          const std::string fromCMake = line.substr(tab + 1);

                          const std::string fromCpp = mdux::tools::shaderemit::identifierFor(id);
                          if (fromCpp != fromCMake) {
                              state->mismatches.push_back(
                                  "package id '" + id + "': the build derives '" + fromCMake +
                                  "' but identifierFor() answers '" + fromCpp + "'");
                          }
                          ++state->compared;
                      }
                  })
            .Then("every identifier agrees and the file is non-empty",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->mismatches.empty(),
                                    "the identifiers agree (see the message for details)");
                      for (const std::string& mismatch : state->mismatches) {
                          checks.expect(false, mismatch);
                      }
                      // A file that went empty would otherwise pass this scenario silently.
                      checks.expect(state->compared >= 8, "the parity file is not empty");
                      checks.raise();
                  })
            .Execute();
    }};
}  // namespace
