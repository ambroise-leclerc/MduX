/**
 * @file SafetensorsTests.cpp
 * @brief BDD scenarios for the host-side weight importer (issue #60).
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The corpus is built in memory rather than committed as binary fixtures. A malformed safetensors
 * file is a handful of bytes described by one line of code here; as a committed blob it would be an
 * opaque artifact whose intended defect a reviewer has to take on trust, and which the
 * source-tree-cleanliness gate would have to be taught about.
 *
 * Every case asserts a specific diagnostic code, not merely that parsing failed. A parser that
 * rejects everything with one generic error passes "it was rejected" and is useless to the author
 * who has to fix the file.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.ml.safetensors;
import mdux.tools.ml.archvalidate;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::tools::ml;
namespace cli = mdux::tools::cli;
namespace ml = mdux::ml;

/// Assembles a safetensors file: the 8-byte little-endian header length, the header, then data.
[[nodiscard]] std::vector<std::byte> buildFile(std::string_view header,
                                               std::span<const std::byte> data) {
    std::vector<std::byte> bytes;
    const std::uint64_t headerLength = header.size();
    for (std::size_t i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::byte>((headerLength >> (8 * i)) & 0xFFu));
    }
    for (char character : header) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    return bytes;
}

[[nodiscard]] std::vector<std::byte> floatBytes(std::span<const float> values) {
    std::vector<std::byte> bytes;
    for (float value : values) {
        const auto bits = std::bit_cast<std::array<std::byte, 4>>(value);
        bytes.insert(bytes.end(), bits.begin(), bits.end());
    }
    return bytes;
}

/// A well-formed two-tensor file: a [2,3] weight matrix and a [2] bias.
[[nodiscard]] std::vector<std::byte> validFile() {
    const std::array<float, 8> values{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.5f, -0.5f};
    const std::string header =
        R"({"bias":{"dtype":"F32","shape":[2],"data_offsets":[24,32]},)"
        R"("weight":{"dtype":"F32","shape":[2,3],"data_offsets":[0,24]}})";
    return buildFile(header, floatBytes(values));
}

/// The diagnostic code a file parses to, or "ok" when it parses.
[[nodiscard]] std::string codeOf(std::span<const std::byte> bytes) {
    auto parsed = parseSafetensors(bytes, "fixture.safetensors");
    if (parsed.has_value()) {
        return "ok";
    }
    return parsed.error().code;
}

/// One corpus entry: a deliberately broken file and the code it must produce.
struct Case {
    std::string_view what;
    std::string_view expectedCode;
    std::function<std::vector<std::byte>()> build;
};

const std::vector<Case>& corpus() {
    static const std::vector<Case> cases{
        {"a well-formed file", "ok", [] { return validFile(); }},

        // Real exporters pad the header to an alignment boundary, and the padding is counted in
        // the declared header length. These two cover the trimming that makes such a file readable
        // at all - added because the trimming shipped without a fixture, so nothing would have
        // caught it accepting spaces but not NULs, or trimming into the JSON itself.
        {"a header padded with spaces", "ok",
         [] {
             const std::array<float, 8> values{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.5f, -0.5f};
             std::string header =
                 R"({"bias":{"dtype":"F32","shape":[2],"data_offsets":[24,32]},)"
                 R"("weight":{"dtype":"F32","shape":[2,3],"data_offsets":[0,24]}})";
             header.append(13, ' ');
             return buildFile(header, floatBytes(values));
         }},

        {"a header padded with NULs", "ok",
         [] {
             const std::array<float, 8> values{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.5f, -0.5f};
             std::string header =
                 R"({"bias":{"dtype":"F32","shape":[2],"data_offsets":[24,32]},)"
                 R"("weight":{"dtype":"F32","shape":[2,3],"data_offsets":[0,24]}})";
             header.append(5, '\0');
             return buildFile(header, floatBytes(values));
         }},

        {"a file too short to hold a header length",
         "mdux.ml.safetensors.malformedTruncatedHeaderLength",
         [] { return std::vector<std::byte>(4, std::byte{0}); }},

        {"a header length longer than the file",
         "mdux.ml.safetensors.malformedHeaderOutOfBounds",
         [] {
             std::vector<std::byte> bytes = validFile();
             bytes[0] = std::byte{0xFF};  // header now claims far more than remains
             bytes[1] = std::byte{0xFF};
             return bytes;
         }},

        {"a header that is not JSON", "mdux.ml.safetensors.malformedHeaderJson",
         [] { return buildFile("{not json at all", {}); }},

        {"a header that is a JSON array", "mdux.ml.safetensors.malformedHeaderNotObject",
         [] { return buildFile("[]", {}); }},

        {"a tensor described by a string", "mdux.ml.safetensors.malformedTensorNotObject",
         [] { return buildFile(R"({"weight":"nonsense"})", {}); }},

        {"a tensor missing its shape", "mdux.ml.safetensors.malformedTensorFields",
         [] {
             return buildFile(R"({"weight":{"dtype":"F32","data_offsets":[0,4]}})",
                              floatBytes(std::array<float, 1>{1.0f}));
         }},

        {"an unrecognised dtype spelling", "mdux.ml.safetensors.malformedUnknownDtype",
         [] {
             return buildFile(R"({"weight":{"dtype":"F42","shape":[1],"data_offsets":[0,4]}})",
                              floatBytes(std::array<float, 1>{1.0f}));
         }},

        // Recognised, well-formed, and out of v1 scope - a different code on purpose, because the
        // author's file is fine and their model is not supported.
        {"an f16 tensor", "mdux.ml.safetensors.unsupportedDtype",
         [] {
             const std::vector<std::byte> data(4, std::byte{0});
             return buildFile(R"({"weight":{"dtype":"F16","shape":[2],"data_offsets":[0,4]}})",
                              data);
         }},

        {"a rank-4 tensor", "mdux.ml.safetensors.unsupportedRank",
         [] {
             const std::vector<std::byte> data(16, std::byte{0});
             return buildFile(
                 R"({"weight":{"dtype":"F32","shape":[1,1,2,2],"data_offsets":[0,16]}})", data);
         }},

        {"data_offsets that are not a pair", "mdux.ml.safetensors.malformedOffsets",
         [] {
             return buildFile(R"({"weight":{"dtype":"F32","shape":[1],"data_offsets":[0]}})",
                              floatBytes(std::array<float, 1>{1.0f}));
         }},

        {"a tensor that ends before it begins", "mdux.ml.safetensors.malformedOffsets",
         [] {
             return buildFile(R"({"weight":{"dtype":"F32","shape":[1],"data_offsets":[8,4]}})",
                              floatBytes(std::array<float, 4>{}));
         }},

        {"a range past the end of the data section",
         "mdux.ml.safetensors.malformedRangeOutOfBounds",
         [] {
             return buildFile(R"({"weight":{"dtype":"F32","shape":[8],"data_offsets":[0,32]}})",
                              floatBytes(std::array<float, 2>{1.0f, 2.0f}));
         }},

        {"a shape that disagrees with its byte range",
         "mdux.ml.safetensors.malformedShapeByteLength",
         [] {
             // [4] f32 needs 16 bytes; the range reserves 8.
             return buildFile(R"({"weight":{"dtype":"F32","shape":[4],"data_offsets":[0,8]}})",
                              floatBytes(std::array<float, 2>{1.0f, 2.0f}));
         }},

        {"a shape whose element count overflows 64 bits",
         "mdux.ml.safetensors.malformedShapeByteLength",
         [] {
             // Three near-2^32 extents multiply past 2^64. The wrapped product can land exactly on
             // a small declared byte range, so without the overflow guard this validates and the
             // baker then packs a tensor described by nonsense.
             const std::vector<std::byte> data(8, std::byte{0});
             return buildFile(
                 R"({"weight":{"dtype":"F32","shape":[4294967295,4294967295,4294967295],)"
                 R"("data_offsets":[0,8]}})",
                 data);
         }},

        {"two tensors sharing bytes", "mdux.ml.safetensors.malformedOverlappingTensors",
         [] {
             const std::string header =
                 R"({"a":{"dtype":"F32","shape":[2],"data_offsets":[0,8]},)"
                 R"("b":{"dtype":"F32","shape":[2],"data_offsets":[4,12]}})";
             return buildFile(header, floatBytes(std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f}));
         }},
    };
    return cases;
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register corpusProducesSpecificCodes{
    "Every malformed fixture produces its own diagnostic code", "evidence-unit", [] {
        return speclab::Test("ml-safetensors-corpus")
            .Given("a corpus of valid and deliberately malformed safetensors files", [] {})
            .When("each is parsed", [] {})
            .Then("each yields exactly the code an author would need to fix it",
                  [] {
                      mdux::spec::Checks checks;
                      for (const Case& entry : corpus()) {
                          const std::string actual = codeOf(entry.build());
                          checks.expect(actual == entry.expectedCode,
                                        std::format("{}: got '{}', expected '{}'", entry.what,
                                                    actual, entry.expectedCode));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register validFileIsReadCorrectly{
    "A well-formed file yields its tensors in a fixed order", "evidence-unit", [] {
        return speclab::Test("ml-safetensors-valid")
            .Given("a two-tensor file whose header lists them in a particular order", [] {})
            .When("it is parsed", [] {})
            .Then("both tensors are present, sorted by name, with absolute byte offsets",
                  [] {
                      mdux::spec::Checks checks;
                      const std::vector<std::byte> bytes = validFile();
                      auto parsed = parseSafetensors(bytes, "fixture.safetensors");

                      checks.expect(parsed.has_value(), "parsed");
                      if (!parsed.has_value()) {
                          checks.raise();
                          return;
                      }

                      checks.expect(parsed->tensors.size() == 2, "two tensors");
                      // Sorted by name, so the order is the baker's, not the exporter's - which is
                      // what keeps a bake byte-identical between two machines.
                      checks.expect(parsed->tensors[0].name == "bias", "bias sorts first");
                      checks.expect(parsed->tensors[1].name == "weight", "weight sorts second");

                      const TensorEntry* weight = parsed->find("weight");
                      checks.expect(weight != nullptr, "weight found by name");
                      if (weight == nullptr) {
                          checks.raise();
                          return;
                      }
                      checks.expect(weight->shape == std::vector<std::uint64_t>{2, 3}, "shape");
                      checks.expect(weight->elementCount() == 6, "element count");
                      checks.expect(weight->byteLength == 24, "byte length");

                      // Absolute, so a caller never has to remember to add the data-section start.
                      // The header length is read back out of the file the way the parser does it,
                      // rather than hardcoded - a literal here would silently stop testing anything
                      // the moment the fixture's header string changed length.
                      std::uint64_t headerLength = 0;
                      for (std::size_t i = 0; i < 8; ++i) {
                          headerLength |=
                              static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i]))
                              << (8 * i);
                      }
                      const std::uint64_t dataStart = 8 + headerLength;
                      checks.expect(weight->byteOffset >= dataStart,
                                    std::format("offset {} is absolute (data starts at {})",
                                                weight->byteOffset, dataStart));

                      // And the bytes really are the values that went in.
                      const auto first = std::bit_cast<float>(std::array<std::byte, 4>{
                          bytes[weight->byteOffset], bytes[weight->byteOffset + 1],
                          bytes[weight->byteOffset + 2], bytes[weight->byteOffset + 3]});
                      checks.expect(first == 1.0f, std::format("first weight is {}", first));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register architectureMustMatchWeights{
    "The declared architecture is checked against the imported tensors", "evidence-unit", [] {
        return speclab::Test("ml-archvalidate")
            .Given("a recipe architecture and the weights file it names", [] {})
            .When("the architecture is resolved", [] {})
            .Then("agreement resolves and every disagreement is reported",
                  [] {
                      mdux::spec::Checks checks;
                      const std::vector<std::byte> bytes = validFile();
                      auto parsed = parseSafetensors(bytes, "fixture.safetensors");
                      checks.expect(parsed.has_value(), "weights parsed");
                      if (!parsed.has_value()) {
                          checks.raise();
                          return;
                      }

                      // A single dense layer: 3 inputs -> 2 outputs, matching the [2,3] weight.
                      const auto denseSpec = [](std::uint32_t inLength, std::uint32_t outLength) {
                          ArchitectureSpec spec;
                          spec.id = "fixture";
                          spec.inputLength = inLength;
                          spec.outputLength = outLength;
                          spec.layers.push_back(LayerSpec{.kind = ml::LayerKind::Dense,
                                                          .activation = ml::Activation::Softmax,
                                                          .inLength = inLength,
                                                          .inChannels = 1,
                                                          .outLength = outLength,
                                                          .outChannels = 1,
                                                          .kernelSize = 0,
                                                          .stride = 0,
                                                          .weightsTensor = "weight",
                                                          .biasTensor = "bias"});
                          return spec;
                      };

                      {  // The agreeing case.
                          auto resolved = resolveArchitecture(denseSpec(3, 2), *parsed, bytes,
                                                              "fixture.toml");
                          checks.expect(resolved.has_value(),
                                        resolved.has_value()
                                            ? "resolved"
                                            : std::format("unexpected: {}",
                                                          resolved.error().front().message));
                          if (resolved.has_value()) {
                              checks.expect(resolved->weights.size() == 32,
                                            "blob holds both tensors packed in layer order");
                              // Derived rather than restated in the recipe: 2 * max(3, 2).
                              checks.expect(resolved->maxScratchFloats == 6,
                                            std::format("scratch derived as {}",
                                                        resolved->maxScratchFloats));
                          }
                      }

                      {  // Dimensions that disagree with the tensor the recipe named. This is the
                         // check that catches weights imported against the wrong architecture, and
                         // it comes from the governed schema rather than a host-side copy.
                          auto resolved = resolveArchitecture(denseSpec(4, 2), *parsed, bytes,
                                                              "fixture.toml");
                          checks.expect(!resolved.has_value(), "a mismatched width is rejected");
                          if (!resolved.has_value()) {
                              checks.expect(resolved.error().front().code ==
                                                "mdux.ml.arch.shapeMismatch",
                                            std::format("code is {}",
                                                        resolved.error().front().code));
                          }
                      }

                      {  // A tensor name that is not in the file.
                          ArchitectureSpec spec = denseSpec(3, 2);
                          spec.layers[0].weightsTensor = "not_present";
                          auto resolved =
                              resolveArchitecture(spec, *parsed, bytes, "fixture.toml");
                          checks.expect(!resolved.has_value(), "a missing tensor is rejected");
                          if (!resolved.has_value()) {
                              checks.expect(resolved.error().front().code ==
                                                "mdux.ml.arch.missingTensor",
                                            "code is missingTensor");
                          }
                      }

                      {  // A pooling layer that names weights it cannot use.
                          ArchitectureSpec spec;
                          spec.id = "fixture";
                          spec.inputLength = 4;
                          spec.outputLength = 2;
                          spec.layers.push_back(LayerSpec{.kind = ml::LayerKind::MaxPool1d,
                                                          .activation = ml::Activation::None,
                                                          .inLength = 4,
                                                          .inChannels = 1,
                                                          .outLength = 2,
                                                          .outChannels = 1,
                                                          .kernelSize = 2,
                                                          .stride = 2,
                                                          .weightsTensor = "weight",
                                                          .biasTensor = ""});
                          auto resolved =
                              resolveArchitecture(spec, *parsed, bytes, "fixture.toml");
                          checks.expect(!resolved.has_value(),
                                        "pooling with weights is rejected");
                          if (!resolved.has_value()) {
                              checks.expect(resolved.error().front().code ==
                                                "mdux.ml.arch.unexpectedTensor",
                                            "code is unexpectedTensor");
                          }
                      }

                      {  // An empty architecture.
                          ArchitectureSpec spec;
                          spec.id = "fixture";
                          auto resolved =
                              resolveArchitecture(spec, *parsed, bytes, "fixture.toml");
                          checks.expect(!resolved.has_value() &&
                                            resolved.error().front().code ==
                                                "mdux.ml.arch.noLayers",
                                        "an empty architecture is rejected");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
