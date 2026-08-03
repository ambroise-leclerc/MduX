/**
 * @file SchemaTests.cpp
 * @brief BDD scenarios for mdux.ml.schema (issue #57).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The rejections are the point. A schema whose validate() only ever succeeds is a comment claiming
 * there are invariants, so every SchemaError below has a case that produces exactly it - and the
 * `static_assert` at namespace scope is the evidence for the header-only `constexpr` claim in the
 * module comment: if validation could not run at compile time, this file would not compile.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.ml.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::ml;
namespace evidence = mdux::evidence;

// ---------------------------------------------------------------------------
// The reference architecture used throughout: the demonstrator's shape in miniature.
//
//   32 samples x 1 channel
//     -> Conv1D  k=5 s=1, 4 filters, relu   -> 28 x 4
//     -> MaxPool k=2 s=2                    -> 14 x 4
//     -> Flatten                            -> 56
//     -> Dense 56->3, softmax               -> 3
//
// It exercises every v1 layer kind, which is what makes it a useful base for mutation: each
// rejection below changes exactly one field of an otherwise-valid package.
// ---------------------------------------------------------------------------

constexpr std::uint64_t convWeightsOffset = 0;    // [4,1,5] = 20 floats
constexpr std::uint64_t convBiasOffset = 80;      // [4]     =  4 floats
constexpr std::uint64_t denseWeightsOffset = 96;  // [3,56]  = 168 floats
constexpr std::uint64_t denseBiasOffset = 768;    // [3]     =  3 floats
constexpr std::uint64_t weightsBytes = 780;

constexpr LayerDesc conv1d() noexcept {
    return LayerDesc{.kind = LayerKind::Conv1d,
                     .activation = Activation::Relu,
                     .inLength = 32,
                     .inChannels = 1,
                     .outLength = 28,
                     .outChannels = 4,
                     .kernelSize = 5,
                     .stride = 1,
                     .weights = TensorRef{.byteOffset = convWeightsOffset,
                                          .shape = {4, 1, 5},
                                          .rank = 3},
                     .bias = TensorRef{.byteOffset = convBiasOffset, .shape = {4, 0, 0}, .rank = 1}};
}

constexpr LayerDesc maxPool1d() noexcept {
    return LayerDesc{.kind = LayerKind::MaxPool1d,
                     .activation = Activation::None,
                     .inLength = 28,
                     .inChannels = 4,
                     .outLength = 14,
                     .outChannels = 4,
                     .kernelSize = 2,
                     .stride = 2,
                     .weights = TensorRef{},
                     .bias = TensorRef{}};
}

constexpr LayerDesc flatten() noexcept {
    return LayerDesc{.kind = LayerKind::Flatten,
                     .activation = Activation::None,
                     .inLength = 14,
                     .inChannels = 4,
                     .outLength = 56,
                     .outChannels = 1,
                     .kernelSize = 0,
                     .stride = 0,
                     .weights = TensorRef{},
                     .bias = TensorRef{}};
}

constexpr LayerDesc dense() noexcept {
    return LayerDesc{.kind = LayerKind::Dense,
                     .activation = Activation::Softmax,
                     .inLength = 56,
                     .inChannels = 1,
                     .outLength = 3,
                     .outChannels = 1,
                     .kernelSize = 0,
                     .stride = 0,
                     .weights = TensorRef{.byteOffset = denseWeightsOffset,
                                          .shape = {3, 56, 0},
                                          .rank = 2},
                     .bias =
                         TensorRef{.byteOffset = denseBiasOffset, .shape = {3, 0, 0}, .rank = 1}};
}

// ---------------------------------------------------------------------------
// Compile-time evidence for the constexpr claim
// ---------------------------------------------------------------------------

constexpr std::array<LayerDesc, 4> constLayers{conv1d(), maxPool1d(), flatten(), dense()};
constexpr std::array<std::uint32_t, 32> constGoldenInput{};
constexpr std::array<std::uint32_t, 3> constGoldenOutput{};
constexpr std::array<GoldenVector, 1> constGoldens{
    GoldenVector{.inputBits = constGoldenInput, .expectedOutputBits = constGoldenOutput}};

constexpr ModelPackage constPackage{.id = "ecg-demo",
                                    .schemaVersion = evidence::kSchemaVersion,
                                    .weightsDigest = evidence::Digest{},
                                    .weightsByteLength = weightsBytes,
                                    .layers = constLayers,
                                    .goldens = constGoldens,
                                    .inputLength = 32,
                                    .outputLength = 3,
                                    .maxScratchFloats = 224};

// The module promises a generated package can be validated at compile time and placed in read-only
// memory. This is that promise, mechanically checked.
static_assert(constPackage.validate().has_value(),
              "the reference package must validate at compile time");
static_assert(requiredScratchFloats(constLayers, 32) == 224,
              "the scratch formula is 2x the largest activation (28*4 floats here)");

// ---------------------------------------------------------------------------
// A mutable equivalent, so each rejection can change exactly one field
// ---------------------------------------------------------------------------

/// Owns everything `ModelPackage`'s spans point at, so a mutated fixture stays self-consistent.
struct Model {
    std::string id{"ecg-demo"};
    std::uint64_t schemaVersion{evidence::kSchemaVersion};
    std::uint64_t weightsByteLength{weightsBytes};
    std::vector<LayerDesc> layers{conv1d(), maxPool1d(), flatten(), dense()};
    std::vector<std::uint32_t> goldenInput = std::vector<std::uint32_t>(32, 0u);
    std::vector<std::uint32_t> goldenOutput = std::vector<std::uint32_t>(3, 0u);
    std::uint32_t inputLength{32};
    std::uint32_t outputLength{3};
    std::uint32_t maxScratchFloats{224};

    /// Rebuilt on demand so a test may resize the golden storage without dangling a span.
    [[nodiscard]] std::vector<GoldenVector> goldens() const {
        return {GoldenVector{.inputBits = goldenInput, .expectedOutputBits = goldenOutput}};
    }
};

/// The error a model validates to, or nullopt when it is valid. Keeps each case to one line.
std::optional<SchemaError> errorOf(const Model& model) {
    const std::vector<GoldenVector> goldens = model.goldens();
    const ModelPackage package{.id = model.id,
                               .schemaVersion = model.schemaVersion,
                               .weightsDigest = evidence::Digest{},
                               .weightsByteLength = model.weightsByteLength,
                               .layers = model.layers,
                               .goldens = goldens,
                               .inputLength = model.inputLength,
                               .outputLength = model.outputLength,
                               .maxScratchFloats = model.maxScratchFloats};
    auto result = package.validate();
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error();
}

/// One rejection case: what to break, and what validate() must say about it.
struct Rejection {
    std::string_view what;
    SchemaError expected;
    std::function<void(Model&)> breakIt;
};

const std::vector<Rejection>& rejections() {
    static const std::vector<Rejection> cases{
        {"schema version from the future", SchemaError::UnsupportedSchemaVersion,
         [](Model& m) { m.schemaVersion = evidence::kSchemaVersion + 1; }},
        {"empty id", SchemaError::EmptyId, [](Model& m) { m.id.clear(); }},
        {"no layers", SchemaError::NoLayers, [](Model& m) { m.layers.clear(); }},
        {"zero input length", SchemaError::ZeroInputLength, [](Model& m) { m.inputLength = 0; }},
        {"zero output length", SchemaError::ZeroOutputLength, [](Model& m) { m.outputLength = 0; }},
        {"layer kind outside the enum", SchemaError::UnknownLayerKind,
         [](Model& m) { m.layers[0].kind = static_cast<LayerKind>(9); }},
        {"activation outside the enum", SchemaError::UnknownActivation,
         [](Model& m) { m.layers[0].activation = static_cast<Activation>(9); }},
        {"rank 4 tensor", SchemaError::RankTooLarge,
         [](Model& m) { m.layers[0].weights.rank = 4; }},
        {"zero dimension within the rank", SchemaError::ZeroDimension,
         [](Model& m) { m.layers[0].weights.shape[0] = 0; }},
        {"zero channel count", SchemaError::ZeroLayerDimension,
         [](Model& m) { m.layers[0].inChannels = 0; }},
        {"tensor offset off the f32 grid", SchemaError::UnalignedTensor,
         [](Model& m) { m.layers[0].weights.byteOffset = 2; }},
        {"tensor past the end of the blob", SchemaError::TensorOutOfBounds,
         [](Model& m) { m.layers[0].weights.byteOffset = weightsBytes; }},
        {"a byte offset chosen so that offset + length wraps", SchemaError::TensorOutOfBounds,
         [](Model& m) {
             // The naive check `byteOffset + byteLength() > weightsByteLength` wraps here and
             // reports a tiny end offset, so the tensor validates and the runtime then reads far
             // off the end of the blob. Kept as a case because the bug is invisible by inspection.
             m.layers[0].weights.byteOffset = std::numeric_limits<std::uint64_t>::max() - 63;
         }},
        {"a shape whose element count overflows", SchemaError::TensorOutOfBounds,
         [](Model& m) {
             // 3 * 2^32-ish extents multiply past 2^64. Saturating rather than wrapping is what
             // turns this into a rejection instead of a small, plausible-looking byte length.
             m.layers[0].weights.shape = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
             m.layers[0].weights.byteOffset = 0;
         }},
        {"zero kernel size", SchemaError::ZeroKernelSize,
         [](Model& m) { m.layers[0].kernelSize = 0; }},
        {"zero stride", SchemaError::ZeroStride, [](Model& m) { m.layers[0].stride = 0; }},
        {"kernel wider than the input", SchemaError::KernelLargerThanInput,
         [](Model& m) { m.layers[0].kernelSize = 64; }},
        {"outLength disagreeing with the windowing", SchemaError::OutputLengthNotDerivable,
         [](Model& m) { m.layers[0].outLength = 27; }},
        {"pooling that changes the channel count", SchemaError::ChannelCountChanged,
         [](Model& m) { m.layers[1].outChannels = 2; }},
        {"dense without weights", SchemaError::MissingWeights,
         [](Model& m) { m.layers[3].weights.rank = 0; }},
        {"pooling carrying weights", SchemaError::UnexpectedWeights,
         [](Model& m) { m.layers[1].weights = conv1d().weights; }},
        {"dense without bias", SchemaError::MissingBias,
         [](Model& m) { m.layers[3].bias.rank = 0; }},
        {"weight matrix of the wrong width", SchemaError::WeightShapeMismatch,
         [](Model& m) { m.layers[3].weights.shape[1] = 55; }},
        {"bias of the wrong length", SchemaError::BiasShapeMismatch,
         [](Model& m) { m.layers[3].bias.shape[0] = 2; }},
        {"first layer not consuming inputLength", SchemaError::InputLengthMismatch,
         [](Model& m) { m.inputLength = 64; }},
        {"last layer not producing outputLength", SchemaError::OutputLengthMismatch,
         [](Model& m) { m.outputLength = 5; }},
        {"a break in the layer chain", SchemaError::LayerChainMismatch,
         [](Model& m) {
             // Kept internally consistent, so the dense layer itself still validates and only the
             // seam between it and flatten is wrong.
             m.layers[3].inLength = 55;
             m.layers[3].weights.shape[1] = 55;
         }},
        {"scratch below the worst case", SchemaError::ScratchTooSmall,
         [](Model& m) { m.maxScratchFloats = 10; }},
        {"golden input of the wrong length", SchemaError::GoldenInputLengthMismatch,
         [](Model& m) { m.goldenInput.resize(31); }},
        {"golden output of the wrong length", SchemaError::GoldenOutputLengthMismatch,
         [](Model& m) { m.goldenOutput.resize(4); }},
    };
    return cases;
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register referenceValidates{
    "The reference architecture validates", "evidence-unit", [] {
        return speclab::Test("ml-schema-reference-validates")
            .Given("a Conv1D/MaxPool1D/Flatten/Dense package covering every v1 layer kind", [] {})
            .When("it is validated", [] {})
            .Then("no invariant is reported broken",
                  [] {
                      mdux::spec::Checks checks;
                      const auto error = errorOf(Model{});
                      checks.expect(!error.has_value(),
                                    error.has_value()
                                        ? std::format("unexpected rejection: {}", describe(*error))
                                        : "valid");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wireSpellingsStable{
    "LayerKind and Activation wire spellings are stable", "evidence-unit", [] {
        return speclab::Test("ml-schema-wire-spellings-stable")
            .Given("the published model package format", [] {})
            .When("each enumerator is looked up by its numeric value", [] {})
            .Then("the wire spellings are the published ones",
                  [] {
                      // These strings are the published package format; renaming one silently
                      // invalidates every committed artifact that carries it.
                      mdux::spec::Checks checks;
                      checks.expect(layerKindWireValues[0] == "dense", "dense");
                      checks.expect(layerKindWireValues[1] == "conv1d", "conv1d");
                      checks.expect(layerKindWireValues[2] == "maxPool1d", "maxPool1d");
                      checks.expect(layerKindWireValues[3] == "avgPool1d", "avgPool1d");
                      checks.expect(layerKindWireValues[4] == "flatten", "flatten");
                      checks.expect(activationWireValues[0] == "none", "none");
                      checks.expect(activationWireValues[1] == "relu", "relu");
                      checks.expect(activationWireValues[2] == "sigmoid", "sigmoid");
                      checks.expect(activationWireValues[3] == "softmax", "softmax");
                      checks.expect(packageKind == "model", "packageKind");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyInvariantRejects{
    "Every schema invariant rejects the case that breaks it", "evidence-unit", [] {
        // Validation is a pure function of the package, so there is nothing for a When to stage:
        // the whole table runs in the Then, and Checks reports every mismatch rather than the
        // first. A shared State here would also trip a GCC 16 -Warray-bounds false positive on
        // make_shared of a struct holding a growing container.
        return speclab::Test("ml-schema-invariants-reject")
            .Given("the reference package and one mutation per invariant", [] {})
            .When("each mutated package is validated", [] {})
            .Then("each is rejected with its own diagnostic",
                  [] {
                      mdux::spec::Checks checks;
                      for (const Rejection& rejection : rejections()) {
                          Model model;
                          rejection.breakIt(model);
                          const auto actual = errorOf(model);
                          if (!actual.has_value()) {
                              checks.expect(false,
                                            std::format("{}: accepted, expected {}", rejection.what,
                                                        describe(rejection.expected)));
                          } else {
                              checks.expect(*actual == rejection.expected,
                                            std::format("{}: got {}, expected {}", rejection.what,
                                                        describe(*actual),
                                                        describe(rejection.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register tensorArithmetic{
    "TensorRef addresses the weight blob in f32 units", "evidence-unit", [] {
        return speclab::Test("ml-schema-tensor-arithmetic")
            .Given("present and absent tensor references", [] {})
            .When("their element counts and byte ranges are computed", [] {})
            .Then("an absent tensor occupies nothing and a present one is four bytes per element",
                  [] {
                      mdux::spec::Checks checks;
                      constexpr TensorRef absent{};
                      constexpr TensorRef conv{
                          .byteOffset = 128, .shape = {4, 1, 5}, .rank = 3};

                      checks.expect(!absent.present(), "rank 0 means absent");
                      checks.expect(absent.byteLength() == 0, "an absent tensor occupies nothing");

                      checks.expect(conv.present(), "rank 3 is present");
                      checks.expect(conv.elementCount() == 20, "4 * 1 * 5 elements");
                      checks.expect(conv.byteLength() == 80, "20 f32 is 80 bytes");
                      checks.expect(conv.byteEnd() == 208, "128 + 80");

                      // Dimensions past the rank are ignored, not multiplied in - a rank-1 bias
                      // whose trailing shape entries are stale must still count its own length.
                      constexpr TensorRef bias{.byteOffset = 0, .shape = {3, 7, 9}, .rank = 1};
                      checks.expect(bias.elementCount() == 3, "only the first rank dims count");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register windowingArithmetic{
    "Windowed output length and scratch follow one formula", "evidence-unit", [] {
        return speclab::Test("ml-schema-windowing-arithmetic")
            .Given("the v1 no-padding windowing rule", [] {})
            .When("output lengths and scratch requirements are computed", [] {})
            .Then("both agree with the values the baker and runtime must independently derive",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(windowedOutputLength(32, 5, 1) == 28, "k=5 s=1 over 32");
                      checks.expect(windowedOutputLength(28, 2, 2) == 14, "k=2 s=2 over 28");
                      checks.expect(windowedOutputLength(10, 3, 3) == 3,
                                    "a trailing partial window is not evaluated");
                      checks.expect(windowedOutputLength(4, 4, 1) == 1, "kernel exactly the input");

                      // Rejected shapes return 0 rather than underflowing, which is what lets
                      // validate() report KernelLargerThanInput instead of a vast outLength.
                      checks.expect(windowedOutputLength(4, 5, 1) == 0, "kernel wider than input");
                      checks.expect(windowedOutputLength(32, 0, 1) == 0, "zero kernel");
                      checks.expect(windowedOutputLength(32, 5, 0) == 0, "zero stride");

                      // Twice the largest activation the chain holds: conv1d's 28*4 = 112 here.
                      const std::array<LayerDesc, 4> layers{conv1d(), maxPool1d(), flatten(),
                                                            dense()};
                      checks.expect(requiredScratchFloats(layers, 32) == 224,
                                    "2 * 112 floats of scratch");

                      // The input can be the largest activation in a shrinking network.
                      const std::array<LayerDesc, 1> onlyDense{dense()};
                      checks.expect(requiredScratchFloats(onlyDense, 56) == 112,
                                    "2 * 56 when the input dominates");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
