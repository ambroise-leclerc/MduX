/**
 * @file SpirvTests.cpp
 * @brief BDD scenarios for the host-only SPIR-V reflector, converted from the Wave 3 MduXTest
 * suite (issue #141).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Fixtures come from SpirvFixtures.hpp, which assembles modules word by word - see its comment
 * for why that is better here than committing `.spv` files.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`). Sizes that guard indexing stay
 * hard, so a wrong size throws rather than letting the checks read out of bounds.
 */
import std;
import speclab;
import mdux.shader.schema;
import mdux.tools.spirv;

#include "../framework/SpecLabBridge.hpp"
#include "SpirvFixtures.hpp"

namespace {

using namespace mdux::tools::spirv;
using namespace mdux::test::spirv;
namespace shader = mdux::shader;

// ---------------------------------------------------------------------------
// Header validation
// ---------------------------------------------------------------------------

const mdux::spec::Register minimalReflects{
    "A minimal module reflects its stage and entry point", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-minimal-reflects")
            .Given("a minimal module", [state] { state->module = minimal().bytes(); })
            .When("it is reflected",
                  [state] {
                      // Guards every rejection below: if this failed they could all pass for the
                      // wrong reason.
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the minimal module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("it reports the vertex stage, the main entry point and version 1.3",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->stage == shader::Stage::Vertex,
                                    "the stage is Vertex");
                      checks.expect(state->reflection->entryPoint == "main",
                                    "the entry point is main");
                      checks.expect(state->reflection->versionMajor == 1,
                                    "the major version is 1");
                      checks.expect(state->reflection->versionMinor == 3,
                                    "the minor version is 3");
                      checks.expect(state->reflection->descriptors.empty(), "no descriptors");
                      checks.expect(!state->reflection->pushConstant.has_value(),
                                    "no push constant block");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register emptyOrMisalignedRejected{
    "An empty or misaligned module is rejected", "evidence-unit", [] {
        struct State {
            std::array<std::byte, 6> misaligned{};
            ParseError empty;
            ParseError odd;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-empty-or-misaligned-rejected")
            .Given("an empty buffer and a buffer whose length is not a multiple of four", [] {})
            .When("each is reflected",
                  [state] {
                      auto empty = reflect({});
                      if (empty.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "an empty module was accepted",
                              std::source_location::current());
                      }
                      state->empty = empty.error();

                      auto odd = reflect(state->misaligned);
                      if (odd.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a misaligned module was accepted",
                              std::source_location::current());
                      }
                      state->odd = odd.error();
                  })
            .Then("each is rejected with its own code",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->empty == ParseError::Empty,
                                    "the empty buffer is Empty");
                      checks.expect(state->odd == ParseError::NotWordAligned,
                                    "the misaligned buffer is NotWordAligned");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register shorterThanHeaderRejected{
    "A module shorter than a header is rejected", "evidence-unit", [] {
        struct State {
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-shorter-than-header-rejected")
            .Given("a buffer shorter than the five-word header", [] {})
            .When("it is reflected",
                  [state] {
                      const std::array<std::byte, 8> tooShort{};
                      auto result = reflect(tooShort);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module shorter than a header was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as TooShort",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::TooShort,
                                    "the error is TooShort");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register badMagicRejected{
    "A bad magic number is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-bad-magic-rejected")
            .Given("a module whose magic number is not the SPIR-V magic",
                   [state] {
                       Builder builder = minimal();
                       builder.poke(0, 0xdeadbeefu);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module with a bad magic number was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as BadMagic",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::BadMagic,
                                    "the error is BadMagic");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register byteSwappedRefused{
    "A byte-swapped module is refused rather than swapped", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-byte-swapped-refused")
            .Given("a module whose magic number is byte-swapped",
                   [state] {
                       // Accepting it would mean the same shader could bake to two different
                       // committed artifacts depending on the endianness of the machine that ran
                       // the bake.
                       Builder builder = minimal();
                       builder.poke(0, 0x03022307u);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a byte-swapped module was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is refused as ForeignEndianness",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::ForeignEndianness,
                                    "the error is ForeignEndianness");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unsupportedVersionRejected{
    "An unsupported SPIR-V version is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> tooNew;
            std::vector<std::byte> tooOld;
            ParseError newer;
            ParseError older;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-unsupported-version-rejected")
            .Given("a module at version 2.0 and one at version 0.9",
                   [state] {
                       Builder tooNew = minimal();
                       tooNew.poke(1, 0x00020000u);  // 2.0
                       state->tooNew = tooNew.bytes();

                       Builder tooOld = minimal();
                       tooOld.poke(1, 0x00000900u);  // 0.9
                       state->tooOld = tooOld.bytes();
                   })
            .When("each is reflected",
                  [state] {
                      auto newer = reflect(state->tooNew);
                      if (newer.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module at version 2.0 was accepted",
                              std::source_location::current());
                      }
                      state->newer = newer.error();

                      auto older = reflect(state->tooOld);
                      if (older.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module at version 0.9 was accepted",
                              std::source_location::current());
                      }
                      state->older = older.error();
                  })
            .Then("both are rejected as UnsupportedVersion",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->newer == ParseError::UnsupportedVersion,
                                    "version 2.0 is UnsupportedVersion");
                      checks.expect(state->older == ParseError::UnsupportedVersion,
                                    "version 0.9 is UnsupportedVersion");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register reservedHeaderWordRejected{
    "A non-zero reserved header word is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-reserved-header-word-rejected")
            .Given("a module whose reserved header word is non-zero",
                   [state] {
                       Builder builder = minimal();
                       builder.poke(4, 1u);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module with a non-zero reserved word was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as ReservedSchemaNonZero",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::ReservedSchemaNonZero,
                                    "the error is ReservedSchemaNonZero");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Instruction stream
// ---------------------------------------------------------------------------

const mdux::spec::Register zeroWordCountRejected{
    "An instruction claiming zero words is rejected rather than looping", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-zero-word-count-rejected")
            .Given("a module whose first instruction claims zero words",
                   [state] {
                       // A word count of zero would leave the cursor where it was; without this
                       // check the parser would spin forever on a malformed file.
                       Builder builder = minimal();
                       builder.poke(5, opEntryPoint);  // word count 0 in the high half
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "an instruction claiming zero words was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as ZeroWordCount",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::ZeroWordCount,
                                    "the error is ZeroWordCount");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register truncatedInstructionRejected{
    "An instruction extending past the module is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-truncated-instruction-rejected")
            .Given("a module whose first instruction extends past the end",
                   [state] {
                       Builder builder = minimal();
                       builder.poke(5, (99u << 16) | opEntryPoint);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a truncated instruction was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as TruncatedInstruction",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::TruncatedInstruction,
                                    "the error is TruncatedInstruction");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register noEntryPointRejected{
    "A module with no entry point is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-no-entry-point-rejected")
            .Given("a header-only module",
                   [state] {
                       const Builder builder;  // header only
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module with no entry point was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as NoEntryPoint",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::NoEntryPoint,
                                    "the error is NoEntryPoint");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register secondEntryPointRejected{
    "A second entry point is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-second-entry-point-rejected")
            .Given("a module declaring a second entry point",
                   [state] {
                       Builder builder = minimal();
                       builder.opWithName(opEntryPoint, {executionModelFragment, 2u}, "other");
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a module with two entry points was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as MultipleEntryPoints",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::MultipleEntryPoints,
                                    "the error is MultipleEntryPoints");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unsupportedExecutionModelRejected{
    "An unsupported execution model is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-unsupported-execution-model-rejected")
            .Given("a module declaring the compute execution model",
                   [state] {
                       // The schema has no Stage enumerator for compute, and inventing one here
                       // would put the schema's vocabulary in two places.
                       state->module = minimal(executionModelGLCompute).bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a compute module was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as UnsupportedExecutionModel",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::UnsupportedExecutionModel,
                                    "the error is UnsupportedExecutionModel");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fragmentModuleReflects{
    "A fragment module reflects as fragment", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-fragment-module-reflects")
            .Given("a fragment module",
                   [state] { state->module = minimal(executionModelFragment).bytes(); })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the fragment module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("its stage is Fragment",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->stage == shader::Stage::Fragment,
                                    "the stage is Fragment");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register nonDefaultEntryPointPreserved{
    "A non-default entry point name is preserved", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-non-default-entry-point-preserved")
            .Given("a module whose entry point is named vertexMain",
                   [state] { state->module = minimal(executionModelVertex, "vertexMain").bytes(); })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the entry point name is preserved",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->entryPoint == "vertexMain",
                                    "the entry point is vertexMain");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register entryPointNameMultipleOfFourDecodes{
    "An entry point name whose length is a multiple of four decodes", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-entry-point-name-multiple-of-four")
            .Given("a module whose entry point name is exactly one word long",
                   [state] {
                       // The packing edge case: "abcd" fills one word exactly, so the NUL needs a
                       // word of its own.
                       state->module = minimal(executionModelVertex, "abcd").bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the name decodes to abcd",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->entryPoint == "abcd",
                                    "the entry point is abcd");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

const mdux::spec::Register combinedImageSamplerReflected{
    "A combined image sampler is reflected with its set and binding", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-combined-image-sampler-reflected")
            .Given("a fragment module declaring a combined image sampler at set 0, binding 3",
                   [state] {
                       Builder builder = minimal(executionModelFragment);
                       addCombinedImageSampler(builder, 0, 3);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the sampler is reported with its set, binding, kind, count and stage",
                  [state] {
                      if (state->reflection->descriptors.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 descriptor, got {}",
                                          state->reflection->descriptors.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->descriptors[0].set == 0,
                                    "set 0");
                      checks.expect(state->reflection->descriptors[0].binding == 3,
                                    "binding 3");
                      checks.expect(state->reflection->descriptors[0].kind ==
                                        shader::DescriptorKind::CombinedImageSampler,
                                    "kind CombinedImageSampler");
                      checks.expect(state->reflection->descriptors[0].count == 1,
                                    "count 1");
                      checks.expect(state->reflection->descriptors[0].stages == shader::fragmentBit,
                                    "the stage bit is fragment");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register arrayBindingElementCountOnce{
    "An array binding reports its element count once", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-array-binding-element-count-once")
            .Given("a module declaring an arrayed combined image sampler of four elements",
                   [state] {
                       Builder builder = minimal(executionModelFragment);
                       addCombinedImageSampler(builder, 0, 0, 4);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("it reports the count once, on the single descriptor",
                  [state] {
                      if (state->reflection->descriptors.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 descriptor, got {}",
                                          state->reflection->descriptors.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->descriptors[0].count == 4,
                                    "the count is 4");
                      checks.expect(state->reflection->descriptors[0].kind ==
                                        shader::DescriptorKind::CombinedImageSampler,
                                    "kind CombinedImageSampler");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register uniformBlockReflected{
    "A uniform block is reflected as a uniform buffer", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-uniform-block-reflected")
            .Given("a module declaring a uniform block variable",
                   [state] {
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
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the block is reported as a UniformBuffer",
                  [state] {
                      if (state->reflection->descriptors.size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 descriptor, got {}",
                                          state->reflection->descriptors.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->descriptors[0].kind ==
                                        shader::DescriptorKind::UniformBuffer,
                                    "kind UniformBuffer");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register descriptorMissingSetRejected{
    "A descriptor missing its set is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-descriptor-missing-set-rejected")
            .Given("a module whose descriptor has a binding but no descriptor set",
                   [state] {
                       // A binding the shader author forgot to decorate cannot be placed in a
                       // pipeline layout, and guessing a set of 0 would produce a layout that
                       // silently disagrees with the shader.
                       constexpr std::uint32_t imageTypeId = 10;
                       constexpr std::uint32_t sampledTypeId = 11;
                       constexpr std::uint32_t pointerTypeId = 13;
                       constexpr std::uint32_t variableId = 14;

                       Builder builder = minimal(executionModelFragment);
                       builder.op(opTypeImage, {imageTypeId, 0u, 1u, 0u, 0u, 0u, 1u, 0u});
                       builder.op(opTypeSampledImage, {sampledTypeId, imageTypeId});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassUniformConstant, sampledTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassUniformConstant});
                       builder.op(opDecorate, {variableId, decorationBinding, 0u});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a descriptor without a set was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as MissingDescriptorSet",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::MissingDescriptorSet,
                                    "the error is MissingDescriptorSet");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register descriptorMissingBindingRejected{
    "A descriptor missing its binding is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-descriptor-missing-binding-rejected")
            .Given("a module whose descriptor has a set but no binding",
                   [state] {
                       constexpr std::uint32_t imageTypeId = 10;
                       constexpr std::uint32_t sampledTypeId = 11;
                       constexpr std::uint32_t pointerTypeId = 13;
                       constexpr std::uint32_t variableId = 14;

                       Builder builder = minimal(executionModelFragment);
                       builder.op(opTypeImage, {imageTypeId, 0u, 1u, 0u, 0u, 0u, 1u, 0u});
                       builder.op(opTypeSampledImage, {sampledTypeId, imageTypeId});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassUniformConstant, sampledTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassUniformConstant});
                       builder.op(opDecorate, {variableId, decorationDescriptorSet, 0u});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a descriptor without a binding was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as MissingBinding",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::MissingBinding,
                                    "the error is MissingBinding");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register descriptorsOrderedBySetThenBinding{
    "Descriptors are ordered by set then binding", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-descriptors-ordered-by-set-then-binding")
            .Given("a module declaring three descriptors out of order",
                   [state] {
                       // Emitted out of order so the ordering cannot pass by accident. Without
                       // the sort the committed package would depend on the id allocation order
                       // inside whatever compiler produced the SPIR-V, which byte-identity cannot
                       // tolerate.
                       constexpr std::uint32_t floatTypeId = 40;
                       constexpr std::uint32_t structTypeId = 41;
                       constexpr std::uint32_t pointerTypeId = 42;

                       Builder builder = minimal();
                       builder.op(opTypeFloat, {floatTypeId, 32u});
                       builder.op(opTypeStruct, {structTypeId, floatTypeId});
                       builder.op(opDecorate, {structTypeId, decorationBlock});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassUniform, structTypeId});

                       struct Placement {
                           std::uint32_t id;
                           std::uint32_t set;
                           std::uint32_t binding;
                       };
                       const std::array<Placement, 3> placements{
                           {{50, 1, 0}, {51, 0, 5}, {52, 0, 1}}};
                       for (const Placement& placement : placements) {
                           builder.op(opVariable,
                                      {pointerTypeId, placement.id, storageClassUniform});
                           builder.op(opDecorate,
                                      {placement.id, decorationDescriptorSet, placement.set});
                           builder.op(opDecorate,
                                      {placement.id, decorationBinding, placement.binding});
                       }
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the descriptors are sorted by set then binding",
                  [state] {
                      if (state->reflection->descriptors.size() != 3) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 3 descriptors, got {}",
                                          state->reflection->descriptors.size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->descriptors[0].set == 0,
                                    "first descriptor set 0");
                      checks.expect(state->reflection->descriptors[0].binding == 1,
                                    "first descriptor binding 1");
                      checks.expect(state->reflection->descriptors[1].set == 0,
                                    "second descriptor set 0");
                      checks.expect(state->reflection->descriptors[1].binding == 5,
                                    "second descriptor binding 5");
                      checks.expect(state->reflection->descriptors[2].set == 1,
                                    "third descriptor set 1");
                      checks.expect(state->reflection->descriptors[2].binding == 0,
                                    "third descriptor binding 0");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------

const mdux::spec::Register vec4PushConstantReflected{
    "A vec4 push constant block reflects as offset 0 size 16", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-vec4-push-constant-reflected")
            .Given("a module declaring a push constant block holding one vec4",
                   [state] {
                       Builder builder = minimal();
                       addVec4PushConstant(builder);
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the block is reported at offset 0 with size 16 and the vertex stage",
                  [state] {
                      if (!state->reflection->pushConstant.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "the module declared no push constant block",
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->pushConstant->offset == 0,
                                    "the offset is 0");
                      checks.expect(state->reflection->pushConstant->size == 16,
                                    "the size is 16");
                      checks.expect(state->reflection->pushConstant->stages == shader::vertexBit,
                                    "the stage bit is vertex");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register pushConstantSizeFollowsMemberOffsets{
    "A push constant block's size follows its member offsets", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            std::optional<Reflection> reflection;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-push-constant-size-follows-member-offsets")
            .Given("a module declaring a push constant block of two vec4s at offsets 0 and 16",
                   [state] {
                       // Two vec4s at offsets 0 and 16 make a 32-byte block. Deriving the size
                       // from the offsets is what makes any padding the shader compiler inserted
                       // come out right.
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
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, structTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the module was rejected: {}",
                                          describe(result.error())),
                              std::source_location::current());
                      }
                      state->reflection = std::move(*result);
                  })
            .Then("the block is reported at offset 0 with size 32",
                  [state] {
                      if (!state->reflection->pushConstant.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "the module declared no push constant block",
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->reflection->pushConstant->offset == 0,
                                    "the offset is 0");
                      checks.expect(state->reflection->pushConstant->size == 32,
                                    "the size is 32");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register pushConstantNonStructRejected{
    "A push constant variable pointing at a non-struct is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-push-constant-non-struct-rejected")
            .Given("a module whose push constant variable points at a float",
                   [state] {
                       constexpr std::uint32_t floatTypeId = 70;
                       constexpr std::uint32_t pointerTypeId = 71;
                       constexpr std::uint32_t variableId = 72;

                       Builder builder = minimal();
                       builder.op(opTypeFloat, {floatTypeId, 32u});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, floatTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a non-struct push constant was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as PushConstantNotAStruct",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::PushConstantNotAStruct,
                                    "the error is PushConstantNotAStruct");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register structWithNoOffsetsRejected{
    "A struct with no member offsets has no computable size", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-struct-with-no-offsets-rejected")
            .Given("a module whose push constant struct has no member offsets",
                   [state] {
                       constexpr std::uint32_t floatTypeId = 80;
                       constexpr std::uint32_t structTypeId = 81;
                       constexpr std::uint32_t pointerTypeId = 82;
                       constexpr std::uint32_t variableId = 83;

                       Builder builder = minimal();
                       builder.op(opTypeFloat, {floatTypeId, 32u});
                       builder.op(opTypeStruct, {structTypeId, floatTypeId});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, structTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a struct with no member offsets was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as UnsupportedType",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::UnsupportedType,
                                    "the error is UnsupportedType");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bitWidthNotWholeBytesRejected{
    "A bit width that is not a whole number of bytes is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-bit-width-not-whole-bytes-rejected")
            .Given("a module whose push constant struct uses a 12-bit float",
                   [state] {
                       // SPIR-V encodes widths this reflector cannot lay out: 1-bit integers are
                       // legal, and wider odd widths are legal under capabilities not implemented
                       // here. `width / 8` answers 0 for the first and truncates the second, so
                       // an unusable type would otherwise be reported as a plausible size and
                       // flow into offsets and stride arithmetic.
                       constexpr std::uint32_t floatTypeId = 100;
                       constexpr std::uint32_t structTypeId = 101;
                       constexpr std::uint32_t pointerTypeId = 102;
                       constexpr std::uint32_t variableId = 103;
                       constexpr std::uint32_t unalignedWidth = 12;

                       Builder builder = minimal();
                       builder.op(opTypeFloat, {floatTypeId, unalignedWidth});
                       builder.op(opTypeStruct, {structTypeId, floatTypeId});
                       builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, structTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a non-byte-aligned bit width was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as UnsupportedType",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::UnsupportedType,
                                    "the error is UnsupportedType");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register zeroBitWidthRejected{
    "A zero bit width is rejected rather than sized as empty", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-zero-bit-width-rejected")
            .Given("a module whose push constant struct uses a zero-width float",
                   [state] {
                       constexpr std::uint32_t floatTypeId = 110;
                       constexpr std::uint32_t structTypeId = 111;
                       constexpr std::uint32_t pointerTypeId = 112;
                       constexpr std::uint32_t variableId = 113;

                       Builder builder = minimal();
                       builder.op(opTypeFloat, {floatTypeId, 0u});
                       builder.op(opTypeStruct, {structTypeId, floatTypeId});
                       builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, structTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a zero-width type was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as UnsupportedType",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::UnsupportedType,
                                    "the error is UnsupportedType");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register secondPushConstantBlockRejected{
    "A second push constant block is rejected", "evidence-unit", [] {
        struct State {
            std::vector<std::byte> module;
            ParseError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-spirv-second-push-constant-block-rejected")
            .Given("a module declaring two push constant blocks",
                   [state] {
                       constexpr std::uint32_t floatTypeId = 90;
                       constexpr std::uint32_t structTypeId = 91;
                       constexpr std::uint32_t pointerTypeId = 92;
                       constexpr std::uint32_t variableId = 93;

                       Builder builder = minimal();
                       addVec4PushConstant(builder);
                       builder.op(opTypeFloat, {floatTypeId, 32u});
                       builder.op(opTypeStruct, {structTypeId, floatTypeId});
                       builder.op(opMemberDecorate, {structTypeId, 0u, decorationOffset, 0u});
                       builder.op(opTypePointer,
                                  {pointerTypeId, storageClassPushConstant, structTypeId});
                       builder.op(opVariable,
                                  {pointerTypeId, variableId, storageClassPushConstant});
                       state->module = builder.bytes();
                   })
            .When("it is reflected",
                  [state] {
                      auto result = reflect(state->module);
                      if (result.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "a second push constant block was accepted",
                              std::source_location::current());
                      }
                      state->error = result.error();
                  })
            .Then("it is rejected as MultiplePushConstantBlocks",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ParseError::MultiplePushConstantBlocks,
                                    "the error is MultiplePushConstantBlocks");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register parseErrorDescriptions{
    "Every ParseError has its own description", "evidence-unit", [] {
        return speclab::Test("shader-spirv-parse-error-descriptions")
            .Given("every ParseError enumerator", [] {})
            .When("each is described", [] {})
            .Then("each has a unique, non-empty description",
                  [] {
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
                      mdux::spec::Checks checks;
                      for (const ParseError error : all) {
                          const std::string_view text = describe(error);
                          checks.expect(!text.empty(), "a description exists");
                          checks.expect(std::ranges::find(seen, text) == seen.end(),
                                        "the description is unique");
                          seen.push_back(text);
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
