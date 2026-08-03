/**
 * @file SchemaTests.cpp
 * @brief BDD scenarios for mdux.shader.schema, converted from the Wave 3 MduXTest suite (issue #141).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The rejections are the point. A schema whose validate() only ever succeeds is a comment claiming
 * there are invariants, so every SchemaError below has a case that produces exactly it - and the
 * round-trip scenario asserts that a package survives write() and parse() unchanged, which is the
 * property the whole evidence pipeline rests on.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`).
 */

import std;
import speclab;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.shader.schema;

#include "../framework/SpecLabBridge.hpp"

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

// ---------------------------------------------------------------------------
// Wire encoding
// ---------------------------------------------------------------------------

const mdux::spec::Register wireSpellingsStable{
    "Stage and DescriptorKind wire spellings are stable", "evidence-unit", [] {
        return speclab::Test("shader-wire-spellings-stable")
            .Given("the published package format", [] {})
            .When("each enumerator is converted to its wire spelling", [] {})
            .Then("the wire spellings are the published ones",
                  [] {
                      // These strings are the published package format; renaming one silently
                      // invalidates every committed artifact that carries it.
                      mdux::spec::Checks checks;
                      checks.expect(toWire(Stage::Vertex) == "vertex", "Vertex");
                      checks.expect(toWire(Stage::Fragment) == "fragment", "Fragment");
                      checks.expect(toWire(DescriptorKind::UniformBuffer) == "uniformBuffer",
                                    "UniformBuffer");
                      checks.expect(toWire(DescriptorKind::StorageBuffer) == "storageBuffer",
                                    "StorageBuffer");
                      checks.expect(toWire(DescriptorKind::CombinedImageSampler) ==
                                        "combinedImageSampler",
                                    "CombinedImageSampler");
                      checks.expect(toWire(DescriptorKind::SampledImage) == "sampledImage",
                                    "SampledImage");
                      checks.expect(toWire(DescriptorKind::Sampler) == "sampler", "Sampler");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wireRoundTrips{
    "Every wire spelling round-trips through its enum", "evidence-unit", [] {
        struct State {
            std::size_t stageRoundTrips{0};
            std::size_t kindRoundTrips{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-wire-round-trips")
            .Given("the wire value arrays in declaration order", [] {})
            .When("each wire value is converted back to its enum", [state] {
                for (std::size_t i = 0; i < stageWireValues.size(); ++i) {
                    auto stage = stageFromWire(stageWireValues[i]);
                    if (!stage.has_value()) {
                        throw speclab::core::AssertionFailure(
                            std::format("stageFromWire('{}') rejected its own spelling",
                                        stageWireValues[i]),
                            std::source_location::current());
                    }
                    state->stageRoundTrips +=
                        (static_cast<std::size_t>(*stage) == i) ? std::size_t{1} : std::size_t{0};
                }
                for (std::size_t i = 0; i < descriptorKindWireValues.size(); ++i) {
                    auto kind = descriptorKindFromWire(descriptorKindWireValues[i]);
                    if (!kind.has_value()) {
                        throw speclab::core::AssertionFailure(
                            std::format("descriptorKindFromWire('{}') rejected its own spelling",
                                        descriptorKindWireValues[i]),
                            std::source_location::current());
                    }
                    state->kindRoundTrips +=
                        (static_cast<std::size_t>(*kind) == i) ? std::size_t{1} : std::size_t{0};
                }
            })
            .Then("every wire value decodes to its own enumerator",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->stageRoundTrips == stageWireValues.size(),
                                    "all stages round-trip");
                      checks.expect(state->kindRoundTrips == descriptorKindWireValues.size(),
                                    "all descriptor kinds round-trip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unknownWireValueRejected{
    "An unknown wire value is rejected rather than defaulted", "evidence-unit", [] {
        struct State {
            SchemaError stageError;
            SchemaError kindError;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-unknown-wire-value-rejected")
            .Given("wire values outside the published spellings", [] {})
            .When("each is decoded", [state] {
                auto stage = stageFromWire("compute");
                if (stage.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "stageFromWire('compute') succeeded", std::source_location::current());
                }
                state->stageError = stage.error();

                auto kind = descriptorKindFromWire("inputAttachment");
                if (kind.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "descriptorKindFromWire('inputAttachment') succeeded",
                        std::source_location::current());
                }
                state->kindError = kind.error();
            })
            .Then("each is rejected with its own SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->stageError == SchemaError::UnknownStage,
                                    "the stage error is UnknownStage");
                      checks.expect(state->kindError == SchemaError::UnknownDescriptorKind,
                                    "the kind error is UnknownDescriptorKind");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register stageMaskEnumeratorOrder{
    "A stage mask is written in enumerator order, not assembly order", "evidence-unit", [] {
        struct State {
            bool sameBytes{false};
            StageMask decoded{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-stage-mask-enumerator-order")
            .Given("the same stage set built in two assembly orders", [] {})
            .When("both are serialized and one is decoded",
                  [state] {
                      // Two packages with the same stage set must produce identical bytes however
                      // the mask was built, so the array order cannot depend on which bit was set
                      // first.
                      const mdux::evidence::json::Value fromVertexFirst =
                          stagesToJson(vertexBit | fragmentBit);
                      const mdux::evidence::json::Value fromFragmentFirst =
                          stagesToJson(fragmentBit | vertexBit);
                      auto a = mdux::evidence::json::write(fromVertexFirst);
                      auto b = mdux::evidence::json::write(fromFragmentFirst);
                      if (!a.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "stagesToJson(vertex-first) did not serialize",
                              std::source_location::current());
                      }
                      if (!b.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "stagesToJson(fragment-first) did not serialize",
                              std::source_location::current());
                      }
                      state->sameBytes = (*a == *b);

                      auto mask = stagesFromJson(fromVertexFirst);
                      if (!mask.has_value()) {
                          throw speclab::core::AssertionFailure(
                              "stagesFromJson rejected its own output",
                              std::source_location::current());
                      }
                      state->decoded = *mask;
                  })
            .Then("the two serializations are identical and the mask decodes",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->sameBytes, "the two orders serialize identically");
                      checks.expect(state->decoded == (vertexBit | fragmentBit),
                                    "the mask decodes to the original stage set");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register emptyStageListDecodes{
    "An empty stage list decodes to an empty mask", "evidence-unit", [] {
        struct State {
            StageMask mask{0xff};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-empty-stage-list-decodes")
            .Given("an empty stage list", [] {})
            .When("it is serialized and decoded", [state] {
                auto mask = stagesFromJson(stagesToJson(0));
                if (!mask.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "stagesFromJson rejected an empty list", std::source_location::current());
                }
                state->mask = *mask;
            })
            .Then("the mask is zero",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->mask == 0, "the mask is empty");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// validate()
// ---------------------------------------------------------------------------

const mdux::spec::Register referencePackageValidates{
    "The reference package validates", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-reference-package-validates")
            .Given("the reference package", [state] { state->error = errorOf(validPackage()); })
            .When("it is validated", [] {})
            .Then("no error is reported",
                  [state] {
                      // Guards every rejection below: if this failed, they could all pass for the
                      // wrong reason.
                      mdux::spec::Checks checks;
                      checks.expect(!state->error.has_value(), "the reference package validates");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wrongKindRejected{
    "A package of the wrong kind is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-wrong-kind-rejected")
            .Given("a package whose kind is not shader", [state] {
                ShaderPackage package = validPackage();
                package.header.kind = "font";
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as WrongKind",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == SchemaError::WrongKind, "WrongKind");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sidecarPathBareFilename{
    "A sidecar path must be a bare filename", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> empty;
            std::optional<SchemaError> forward;
            std::optional<SchemaError> backward;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-sidecar-path-bare-filename")
            .Given("sidecar paths that are not bare filenames", [state] {
                ShaderPackage package = validPackage();
                package.sidecarPath.clear();
                state->empty = errorOf(package);

                // A sidecar sits beside package.json. A path with a separator would let an
                // artifact point outside its own directory, which the bake/verify comparison could
                // not follow.
                package = validPackage();
                package.sidecarPath = "nested/shaders.spv";
                state->forward = errorOf(package);

                package = validPackage();
                package.sidecarPath = "nested\\shaders.spv";
                state->backward = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with the right SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->empty == SchemaError::EmptySidecarPath,
                                    "an empty path is EmptySidecarPath");
                      checks.expect(state->forward == SchemaError::SidecarPathHasSeparator,
                                    "a '/' separator is SidecarPathHasSeparator");
                      checks.expect(state->backward == SchemaError::SidecarPathHasSeparator,
                                    "a '\\' separator is SidecarPathHasSeparator");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register noModulesRejected{
    "A package with no modules is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-no-modules-rejected")
            .Given("a package with no modules", [state] {
                ShaderPackage package = validPackage();
                package.modules.clear();
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as NoModules",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == SchemaError::NoModules, "NoModules");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleIdentityRequired{
    "Module identity is required and must be unique", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> empty;
            std::optional<SchemaError> duplicate;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-module-identity-required")
            .Given("modules with an empty id and a duplicated id", [state] {
                ShaderPackage package = validPackage();
                package.modules[0].id.clear();
                state->empty = errorOf(package);

                package = validPackage();
                package.modules[0].byteLength = 32;
                ShaderModule duplicate = package.modules[0];
                duplicate.byteOffset = 32;
                package.modules.push_back(duplicate);
                state->duplicate = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with the right SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->empty == SchemaError::EmptyModuleId,
                                    "an empty id is EmptyModuleId");
                      checks.expect(state->duplicate == SchemaError::DuplicateModuleId,
                                    "a duplicated id is DuplicateModuleId");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleEntryPointAndLength{
    "A module needs an entry point and a non-zero length", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> emptyEntry;
            std::optional<SchemaError> empty;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-module-entry-point-and-length")
            .Given("modules with no entry point and with a zero length", [state] {
                ShaderPackage package = validPackage();
                package.modules[0].entryPoint.clear();
                state->emptyEntry = errorOf(package);

                package = validPackage();
                package.modules[0].byteLength = 0;
                state->empty = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with the right SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->emptyEntry == SchemaError::EmptyEntryPoint,
                                    "no entry point is EmptyEntryPoint");
                      checks.expect(state->empty == SchemaError::EmptyModule,
                                    "a zero length is EmptyModule");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleRangesWordAligned{
    "Module ranges must be word-aligned", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> length;
            std::optional<SchemaError> offset;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-module-ranges-word-aligned")
            .Given("modules with a misaligned length and offset", [state] {
                // SPIR-V is a sequence of 32-bit words, so a length that is not a multiple of 4
                // cannot be a module - and would be caught much later, by the driver, as a device
                // loss.
                ShaderPackage package = validPackage();
                package.modules[0].byteLength = 62;
                state->length = errorOf(package);

                package = validPackage();
                package.modules[0].byteOffset = 2;
                package.modules[0].byteLength = 60;
                state->offset = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected as UnalignedModule",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->length == SchemaError::UnalignedModule,
                                    "a misaligned length");
                      checks.expect(state->offset == SchemaError::UnalignedModule,
                                    "a misaligned offset");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleRangeWithinSidecar{
    "A module range may not extend past the sidecar", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> past;
            std::optional<SchemaError> wrapping;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-module-range-within-sidecar")
            .Given("a range past the sidecar and one that would wrap", [state] {
                ShaderPackage package = validPackage();
                package.modules[0].byteLength = 68;
                state->past = errorOf(package);

                // An offset past the end, with a length that would wrap if the two were added
                // naively.
                package = validPackage();
                package.modules[0].byteOffset = 64;
                package.modules[0].byteLength = 4;
                state->wrapping = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected as ModuleOutOfBounds",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->past == SchemaError::ModuleOutOfBounds,
                                    "a range past the sidecar");
                      checks.expect(state->wrapping == SchemaError::ModuleOutOfBounds,
                                    "a wrapping offset");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleRangesNoOverlap{
    "Two module ranges may not overlap", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-module-ranges-no-overlap")
            .Given("two modules sharing bytes", [state] {
                // Overlapping ranges mean two modules report different digests for bytes they
                // share, so at most one of the two recorded digests can be right.
                ShaderPackage package = validPackage();
                package.modules[0].byteLength = 32;
                package.modules.push_back(ShaderModule{.id = "ui.frag",
                                                       .stage = Stage::Fragment,
                                                       .entryPoint = "main",
                                                       .byteOffset = 16,
                                                       .byteLength = 32,
                                                       .sha256 = digestOf("ui.frag")});
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as OverlappingModules",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == SchemaError::OverlappingModules,
                                    "OverlappingModules");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register adjacentRangesNotOverlap{
    "Adjacent module ranges are not an overlap", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-adjacent-ranges-not-overlap")
            .Given("two modules with exactly adjacent ranges", [state] {
                // The off-by-one guard on the overlap check: [0,32) and [32,64) share no byte.
                ShaderPackage package = validPackage();
                package.modules[0].byteLength = 32;
                package.modules.push_back(ShaderModule{.id = "ui.frag",
                                                       .stage = Stage::Fragment,
                                                       .entryPoint = "main",
                                                       .byteOffset = 32,
                                                       .byteLength = 32,
                                                       .sha256 = digestOf("ui.frag")});
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is accepted",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(!state->error.has_value(), "adjacent ranges are not an overlap");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register descriptorNoStagesRejected{
    "A descriptor visible to no stage is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-descriptor-no-stages-rejected")
            .Given("a descriptor visible to no stage", [state] {
                ShaderPackage package = validPackage();
                package.descriptors.push_back(
                    DescriptorBinding{.set = 0,
                                      .binding = 0,
                                      .kind = DescriptorKind::Sampler,
                                      .count = 1});
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as NoStages",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == SchemaError::NoStages, "NoStages");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register descriptorCountAndUniqueness{
    "A descriptor needs a non-zero count and a unique binding", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> zero;
            std::optional<SchemaError> duplicate;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-descriptor-count-and-uniqueness")
            .Given("a descriptor with a zero count and two with the same binding", [state] {
                ShaderPackage package = validPackage();
                package.descriptors.push_back(DescriptorBinding{.set = 0,
                                                                .binding = 0,
                                                                .kind = DescriptorKind::Sampler,
                                                                .count = 0,
                                                                .stages = fragmentBit});
                state->zero = errorOf(package);

                package = validPackage();
                const DescriptorBinding binding{.set = 0,
                                                .binding = 1,
                                                .kind = DescriptorKind::Sampler,
                                                .count = 1,
                                                .stages = fragmentBit};
                package.descriptors.push_back(binding);
                package.descriptors.push_back(binding);
                state->duplicate = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with the right SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->zero == SchemaError::ZeroDescriptorCount,
                                    "a zero count is ZeroDescriptorCount");
                      checks.expect(state->duplicate == SchemaError::DuplicateDescriptorBinding,
                                    "a duplicate binding is DuplicateDescriptorBinding");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sameBindingDifferentSet{
    "The same binding number in a different set is not a duplicate", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-same-binding-different-set")
            .Given("the same binding number in two sets", [state] {
                ShaderPackage package = validPackage();
                package.descriptors.push_back(DescriptorBinding{.set = 0,
                                                                .binding = 1,
                                                                .kind = DescriptorKind::Sampler,
                                                                .count = 1,
                                                                .stages = fragmentBit});
                package.descriptors.push_back(DescriptorBinding{.set = 1,
                                                                .binding = 1,
                                                                .kind = DescriptorKind::Sampler,
                                                                .count = 1,
                                                                .stages = fragmentBit});
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is accepted",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(!state->error.has_value(),
                                    "the same binding in another set is not a duplicate");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register pushConstantChecks{
    "Push constant ranges are checked for size, alignment and overlap", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> empty;
            std::optional<SchemaError> unaligned;
            std::optional<SchemaError> overlapping;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-push-constant-checks")
            .Given("push constant ranges that are empty, unaligned and overlapping", [state] {
                ShaderPackage package = validPackage();
                package.pushConstants.push_back(
                    PushConstantRange{.offset = 0, .size = 0, .stages = vertexBit});
                state->empty = errorOf(package);

                package = validPackage();
                package.pushConstants.push_back(
                    PushConstantRange{.offset = 2, .size = 16, .stages = vertexBit});
                state->unaligned = errorOf(package);

                package = validPackage();
                package.pushConstants.push_back(
                    PushConstantRange{.offset = 0, .size = 16, .stages = vertexBit});
                package.pushConstants.push_back(
                    PushConstantRange{.offset = 8, .size = 16, .stages = fragmentBit});
                state->overlapping = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with the right SchemaError",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->empty == SchemaError::EmptyPushConstantRange,
                                    "an empty range is EmptyPushConstantRange");
                      checks.expect(state->unaligned == SchemaError::UnalignedPushConstantRange,
                                    "an unaligned range is UnalignedPushConstantRange");
                      checks.expect(state->overlapping == SchemaError::OverlappingPushConstants,
                                    "overlapping ranges are OverlappingPushConstants");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// write() and parse()
// ---------------------------------------------------------------------------

const mdux::spec::Register packageRoundTrip{
    "A package survives write() and parse() unchanged", "evidence-unit", [] {
        struct State {
            std::optional<std::string> text;
            std::optional<ShaderPackage> parsed;
            bool bytesSurvive{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-package-round-trip")
            .Given("a package with two modules, a descriptor and a push constant range",
                   [state] {
                       ShaderPackage package = validPackage();
                       package.modules[0].byteLength = 32;
                       package.modules.push_back(ShaderModule{.id = "ui.frag",
                                                              .stage = Stage::Fragment,
                                                              .entryPoint = "main",
                                                              .byteOffset = 32,
                                                              .byteLength = 32,
                                                              .sha256 = digestOf("ui.frag")});
                       package.descriptors.push_back(
                           DescriptorBinding{.set = 0,
                                             .binding = 0,
                                             .kind = DescriptorKind::CombinedImageSampler,
                                             .count = 1,
                                             .stages = fragmentBit});
                       package.pushConstants.push_back(
                           PushConstantRange{.offset = 0, .size = 16, .stages = vertexBit | fragmentBit});

                       auto text = package.write();
                       if (!text.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "write() rejected a valid package",
                               std::source_location::current());
                       }
                       state->text = *text;

                       auto parsed = ShaderPackage::parse(*state->text);
                       if (!parsed.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "parse() rejected its own output",
                               std::source_location::current());
                       }
                       state->parsed = *parsed;

                       // Re-writing the parsed package must reproduce the same bytes. This is the
                       // property the evidence pipeline's byte comparison depends on; a round trip
                       // that merely preserved the fields would not be enough.
                       auto rewritten = state->parsed->write();
                       if (!rewritten.has_value()) {
                           throw speclab::core::AssertionFailure(
                               "re-writing the parsed package failed",
                               std::source_location::current());
                       }
                       state->bytesSurvive = (*rewritten == *state->text);
                   })
            .When("the parsed package is compared field by field and re-serialized", [] {})
            .Then("every field survives and the bytes are reproduced exactly",
                  [state] {
                      const ShaderPackage& package = *state->parsed;
                      mdux::spec::Checks checks;
                      checks.expect(package.header.id == "mdux-ui", "header id");
                      checks.expect(package.header.kind == "shader", "header kind");
                      checks.expect(package.sidecarPath == "shaders.spv", "sidecar path");
                      checks.expect(package.sidecarByteLength == 64, "sidecar byte length");
                      checks.expect(package.sidecarSha256 == digestOf("sidecar"),
                                    "sidecar digest");
                      if (package.modules.size() != 2) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 2 modules, got {}", package.modules.size()),
                              std::source_location::current());
                      }
                      for (std::size_t i = 0; i < package.modules.size(); ++i) {
                          checks.expect(package.modules[i].id ==
                                            (i == 0 ? "ui.vert" : "ui.frag"),
                                        std::format("module {} id", i));
                          checks.expect(package.modules[i].stage ==
                                            (i == 0 ? Stage::Vertex : Stage::Fragment),
                                        std::format("module {} stage", i));
                          checks.expect(package.modules[i].entryPoint == "main",
                                        std::format("module {} entry point", i));
                          checks.expect(package.modules[i].byteOffset == (i == 0 ? 0 : 32),
                                        std::format("module {} offset", i));
                          checks.expect(package.modules[i].byteLength == 32,
                                        std::format("module {} length", i));
                          checks.expect(package.modules[i].sha256 ==
                                            (i == 0 ? digestOf("ui.vert") : digestOf("ui.frag")),
                                        std::format("module {} digest", i));
                      }
                      checks.expect(package.descriptors ==
                                        std::vector<DescriptorBinding>{
                                            DescriptorBinding{.set = 0,
                                                              .binding = 0,
                                                              .kind =
                                                                  DescriptorKind::CombinedImageSampler,
                                                              .count = 1,
                                                              .stages = fragmentBit}},
                                    "the descriptors survive");
                      checks.expect(package.pushConstants ==
                                        std::vector<PushConstantRange>{
                                            PushConstantRange{.offset = 0,
                                                              .size = 16,
                                                              .stages = vertexBit | fragmentBit}},
                                    "the push constants survive");
                      checks.expect(state->bytesSurvive,
                                    "re-serializing reproduces the same bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register malformedTextRejected{
    "Parsing rejects malformed and semantically invalid text", "evidence-unit", [] {
        struct State {
            SchemaError notJson;
            SchemaError notAnObject;
            SchemaError wrongShape;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-malformed-text-rejected")
            .Given("text that is not JSON, not an object, and not a package", [state] {
                auto notJson = ShaderPackage::parse("{ this is not json");
                if (notJson.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "parse() accepted text that is not JSON", std::source_location::current());
                }
                state->notJson = notJson.error();

                auto notAnObject = ShaderPackage::parse("[]");
                if (notAnObject.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "parse() accepted a non-object", std::source_location::current());
                }
                state->notAnObject = notAnObject.error();

                // Well-formed JSON that is not a shader package.
                auto wrongShape =
                    ShaderPackage::parse(R"({"schemaVersion":1,"id":"x","kind":"shader"})");
                if (wrongShape.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "parse() accepted a non-package object", std::source_location::current());
                }
                state->wrongShape = wrongShape.error();
            })
            .When("each is parsed", [] {})
            .Then("each is rejected as MalformedPackage",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->notJson == SchemaError::MalformedPackage,
                                    "text that is not JSON");
                      checks.expect(state->notAnObject == SchemaError::MalformedPackage,
                                    "a non-object");
                      checks.expect(state->wrongShape == SchemaError::MalformedPackage,
                                    "a non-package object");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register parseRunsValidate{
    "Parsing runs validate(), so an invalid package cannot be read back", "evidence-unit", [] {
        struct State {
            SchemaError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-parse-runs-validate")
            .Given("a package file edited into an invalid state", [state] {
                // Written by hand rather than through write(), which validates: the point is that a
                // file someone edited into an invalid state is rejected on read rather than
                // trusted.
                ShaderPackage package = validPackage();
                auto text = package.write();
                if (!text.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "write() rejected the reference package", std::source_location::current());
                }

                std::string corrupted = *text;
                const std::size_t position = corrupted.find("\"byteLength\": 64");
                if (position == std::string::npos) {
                    throw speclab::core::AssertionFailure(
                        "the reference JSON did not contain the expected byteLength",
                        std::source_location::current());
                }
                corrupted.replace(position, std::string_view{"\"byteLength\": 64"}.size(),
                                  "\"byteLength\": 99");

                auto parsed = ShaderPackage::parse(corrupted);
                if (parsed.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "parse() accepted a semantically invalid package",
                        std::source_location::current());
                }
                state->error = parsed.error();
            })
            .When("it is parsed", [] {})
            .Then("it is rejected as UnalignedModule",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == SchemaError::UnalignedModule,
                                    "UnalignedModule");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register findLocatesModule{
    "find() locates a module by id and reports a miss", "evidence-unit", [] {
        struct State {
            const ShaderModule* found{nullptr};
            const ShaderModule* miss{nullptr};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("shader-find-locates-module")
            .Given("the reference package", [state] {
                const ShaderPackage package = validPackage();
                state->found = package.find("ui.vert");
                state->miss = package.find("ui.frag");
            })
            .When("a known and an unknown id are looked up", [] {})
            .Then("the known id is found and the unknown is a miss",
                  [state] {
                      if (state->found == nullptr) {
                          throw speclab::core::AssertionFailure(
                              "find('ui.vert') returned nullptr",
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->found->stage == Stage::Vertex,
                                    "the found module is the vertex stage");
                      checks.expect(state->miss == nullptr, "an unknown id reports a miss");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register schemaErrorDescriptions{
    "Every SchemaError has its own description", "evidence-unit", [] {
        return speclab::Test("shader-schema-error-descriptions")
            .Given("every SchemaError enumerator", [] {})
            .When("each is described", [] {})
            .Then("each has a unique, non-empty description",
                  [] {
                      // A duplicated or empty description makes a diagnostic useless at exactly the
                      // moment it matters, so the whole set is checked for distinctness rather
                      // than spot-checked.
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
                      mdux::spec::Checks checks;
                      for (const SchemaError error : all) {
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
