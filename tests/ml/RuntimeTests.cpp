/**
 * @file RuntimeTests.cpp
 * @brief BDD scenarios for mdux.ml.runtime (issue #62).
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The refusals are what matters here. `create()` succeeding is the easy half; the scenarios that
 * earn the "fails closed" claim are the ones that corrupt exactly one thing - a weight byte, a
 * golden bit, the scratch size - and assert the classifier is never constructed.
 *
 * The fixture generates its golden vectors by running the model through the runtime itself, which
 * is not a shortcut: it is the arrangement ADR-008 decision 1 describes, where the authoring path
 * and the device path are the same object code. The baker (issue #61) does the same thing for real
 * packages.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.ml.schema;
import mdux.ml.kernels;
import mdux.ml.runtime;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::ml;
namespace evidence = mdux::evidence;

// The model under test:
//   8 x 1 -> Conv1D k=3 s=1, 2 filters, relu -> 6 x 2
//         -> MaxPool1D k=2 s=2               -> 3 x 2
//         -> Flatten                         -> 6
//         -> Dense 6 -> 2, softmax           -> 2

constexpr std::uint32_t modelInputLength = 8;
constexpr std::uint32_t modelOutputLength = 2;
constexpr std::uint32_t modelScratchFloats = 24;  // 2 * 12, the conv layer's 6*2 activation

/// Float offsets into the weight blob, so the byte offsets below stay readable.
constexpr std::uint64_t convWeightsFloat = 0;   // [2,1,3] = 6
constexpr std::uint64_t convBiasFloat = 6;      // [2]
constexpr std::uint64_t denseWeightsFloat = 8;  // [2,6]  = 12
constexpr std::uint64_t denseBiasFloat = 20;    // [2]
constexpr std::size_t weightFloatCount = 22;

[[nodiscard]] std::vector<LayerDesc> modelLayers() {
    return {
        LayerDesc{.kind = LayerKind::Conv1d,
                  .activation = Activation::Relu,
                  .inLength = 8,
                  .inChannels = 1,
                  .outLength = 6,
                  .outChannels = 2,
                  .kernelSize = 3,
                  .stride = 1,
                  .weights = TensorRef{.byteOffset = convWeightsFloat * sizeof(float),
                                       .shape = {2, 1, 3},
                                       .rank = 3},
                  .bias = TensorRef{.byteOffset = convBiasFloat * sizeof(float),
                                    .shape = {2, 0, 0},
                                    .rank = 1}},
        LayerDesc{.kind = LayerKind::MaxPool1d,
                  .activation = Activation::None,
                  .inLength = 6,
                  .inChannels = 2,
                  .outLength = 3,
                  .outChannels = 2,
                  .kernelSize = 2,
                  .stride = 2,
                  .weights = TensorRef{},
                  .bias = TensorRef{}},
        LayerDesc{.kind = LayerKind::Flatten,
                  .activation = Activation::None,
                  .inLength = 3,
                  .inChannels = 2,
                  .outLength = 6,
                  .outChannels = 1,
                  .kernelSize = 0,
                  .stride = 0,
                  .weights = TensorRef{},
                  .bias = TensorRef{}},
        LayerDesc{.kind = LayerKind::Dense,
                  .activation = Activation::Softmax,
                  .inLength = 6,
                  .inChannels = 1,
                  .outLength = 2,
                  .outChannels = 1,
                  .kernelSize = 0,
                  .stride = 0,
                  .weights = TensorRef{.byteOffset = denseWeightsFloat * sizeof(float),
                                       .shape = {2, 6, 0},
                                       .rank = 2},
                  .bias = TensorRef{.byteOffset = denseBiasFloat * sizeof(float),
                                    .shape = {2, 0, 0},
                                    .rank = 1}},
    };
}

/// The same LCG the determinism suite uses, for the same reason: <random> is not reproducible
/// across standard library implementations.
[[nodiscard]] std::vector<float> generateWeights(std::uint32_t seed) {
    std::vector<float> weights(weightFloatCount, 0.0f);
    std::uint32_t state = seed;
    for (float& value : weights) {
        state = state * 1664525u + 1013904223u;
        value = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    return weights;
}

/**
 * @brief Owns a whole model - weights, layers, goldens - and hands out a ModelPackage over them.
 *
 * Every span in the package points into this object, so it has to outlive any classifier built
 * from it. That is the same contract a real caller has with its mmap or its linked blob.
 */
struct TestModel {
    std::vector<float> weightStorage;
    std::vector<LayerDesc> layers = modelLayers();
    std::vector<std::uint32_t> goldenInputBits;
    std::vector<std::uint32_t> goldenOutputBits;
    std::string id{"runtime-fixture"};
    std::uint32_t maxScratchFloats{modelScratchFloats};
    evidence::Digest digest{};

    explicit TestModel(std::uint32_t seed = 4242u) : weightStorage(generateWeights(seed)) {
        digest = evidence::sha256(weights());
    }

    /// float storage guarantees the blob is aligned for f32, which is what create() requires.
    [[nodiscard]] std::span<const std::byte> weights() const noexcept {
        return std::as_bytes(std::span{weightStorage});
    }

    [[nodiscard]] std::vector<GoldenVector> goldens() const {
        if (goldenInputBits.empty()) {
            return {};
        }
        return {GoldenVector{.inputBits = goldenInputBits,
                             .expectedOutputBits = goldenOutputBits}};
    }

    [[nodiscard]] ModelPackage package(std::span<const GoldenVector> goldenSpan) const noexcept {
        return ModelPackage{.id = id,
                            .schemaVersion = evidence::kSchemaVersion,
                            .weightsDigest = digest,
                            .weightsByteLength = weightStorage.size() * sizeof(float),
                            .layers = layers,
                            .goldens = goldenSpan,
                            .inputLength = modelInputLength,
                            .outputLength = modelOutputLength,
                            .maxScratchFloats = maxScratchFloats};
    }

    /// Recomputes the digest after a test has altered the weights.
    void reseal() noexcept { digest = evidence::sha256(weights()); }
};

/// A fixed, unremarkable input. Exact in f32 so nothing here depends on decimal parsing.
[[nodiscard]] std::array<float, modelInputLength> sampleInput() noexcept {
    return {0.5f, -0.25f, 1.0f, 0.75f, -1.0f, 0.125f, 0.0f, -0.5f};
}

/**
 * @brief Bakes the goldens into `model`, the way mdux-mlbake will.
 *
 * Runs the layer chain directly through `mdux.ml.kernels` rather than through a classifier. It
 * used to build a classifier with no goldens and predict through that, which is no longer possible
 * now that create() rejects a package with nothing to self-test against - and that is the right
 * outcome: generating the goldens is precisely the step that happens *before* a package can be
 * verified, so it cannot depend on verification.
 *
 * This is the same ping-pong the runtime and the baker's GoldenGen both use, over the same
 * applyLayer(), so the goldens it produces are the ones create() will later reproduce.
 */
[[nodiscard]] bool bakeGoldens(TestModel& model) {
    const auto input = sampleInput();
    const std::span<const float> weightFloats{model.weightStorage};

    const std::size_t width = modelScratchFloats / 2;
    std::array<float, modelScratchFloats> scratch{};
    std::span<float> bufferA{scratch.data(), width};
    std::span<float> bufferB{scratch.data() + width, width};

    for (std::size_t i = 0; i < input.size(); ++i) {
        bufferA[i] = input[i];
    }

    std::span<const float> current =
        bufferA.first(static_cast<std::size_t>(model.layers[0].inputFloats()));
    for (std::size_t i = 0; i < model.layers.size(); ++i) {
        const LayerDesc& layer = model.layers[i];
        std::span<const float> weights;
        std::span<const float> bias;
        if (layer.weights.present()) {
            weights = weightFloats.subspan(layer.weights.byteOffset / sizeof(float),
                                           static_cast<std::size_t>(layer.weights.elementCount()));
        }
        if (layer.bias.present()) {
            bias = weightFloats.subspan(layer.bias.byteOffset / sizeof(float),
                                        static_cast<std::size_t>(layer.bias.elementCount()));
        }
        std::span<float> destination = ((i % 2) == 0 ? bufferB : bufferA)
                                           .first(static_cast<std::size_t>(layer.outputFloats()));
        if (!applyLayer(layer, current, weights, bias, destination)) {
            return false;
        }
        current = destination;
    }

    model.goldenInputBits.clear();
    for (float value : input) {
        model.goldenInputBits.push_back(std::bit_cast<std::uint32_t>(value));
    }
    model.goldenOutputBits.clear();
    for (std::size_t i = 0; i < modelOutputLength; ++i) {
        model.goldenOutputBits.push_back(std::bit_cast<std::uint32_t>(current[i]));
    }
    return true;
}

/// The error a create() attempt produces, or nullopt when it succeeds.
[[nodiscard]] std::optional<MlError> createError(const TestModel& model,
                                                 std::span<float> scratch) {
    const std::vector<GoldenVector> goldens = model.goldens();
    auto classifier = Classifier1D::create(model.package(goldens), model.weights(), scratch);
    if (classifier.has_value()) {
        return std::nullopt;
    }
    return classifier.error();
}

[[nodiscard]] std::string describeOutcome(const std::optional<MlError>& error) {
    if (!error.has_value()) {
        return "created successfully";
    }
    return std::string{describe(error->code)};
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register createSucceedsAndPredicts{
    "A sound package creates a classifier that reproduces its goldens", "evidence-unit", [] {
        return speclab::Test("ml-runtime-create-and-predict")
            .Given("a validated package whose goldens were generated through the same kernels", [] {})
            .When("a classifier is created and asked to predict", [] {})
            .Then("creation succeeds and the prediction matches the golden bits exactly",
                  [] {
                      mdux::spec::Checks checks;
                      TestModel model;
                      checks.expect(bakeGoldens(model), "goldens baked");

                      std::array<float, modelScratchFloats> scratch{};
                      const std::vector<GoldenVector> goldens = model.goldens();
                      auto classifier =
                          Classifier1D::create(model.package(goldens), model.weights(), scratch);

                      checks.expect(classifier.has_value(),
                                    classifier.has_value()
                                        ? "created"
                                        : std::string{describe(classifier.error().code)});
                      if (!classifier.has_value()) {
                          checks.raise();
                          return;
                      }

                      checks.expect(classifier->inputLength() == modelInputLength, "input length");
                      checks.expect(classifier->outputLength() == modelOutputLength,
                                    "output length");

                      std::array<float, modelOutputLength> output{};
                      checks.expect(classifier->predict(sampleInput(), output).has_value(),
                                    "predict succeeded");
                      for (std::size_t i = 0; i < output.size(); ++i) {
                          checks.expect(std::bit_cast<std::uint32_t>(output[i]) ==
                                            model.goldenOutputBits[i],
                                        std::format("output[{}] reproduces its golden", i));
                      }

                      // A softmax, so this is a probability vector rather than arbitrary numbers.
                      const float total = output[0] + output[1];
                      checks.expect(std::fabs(total - 1.0f) < 1e-6f,
                                    std::format("outputs sum to {}", total));

                      // Same input, same bits, every time - no accumulated state anywhere.
                      std::array<float, modelOutputLength> again{};
                      checks.expect(classifier->predict(sampleInput(), again).has_value(),
                                    "second predict succeeded");
                      checks.expect(std::bit_cast<std::uint32_t>(again[0]) ==
                                        std::bit_cast<std::uint32_t>(output[0]),
                                    "predict is repeatable bit for bit");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register digestCheckIsLoadBearing{
    "Weights that do not match the digest are refused", "evidence-unit", [] {
        return speclab::Test("ml-runtime-digest-mismatch")
            .Given("a package whose weight blob has been altered by one value", [] {})
            .When("a classifier is created", [] {})
            .Then("it fails closed on digestMismatch rather than classifying with wrong weights",
                  [] {
                      // This is the scenario that makes "weights are data" safe. Without it, a
                      // caller-supplied blob would mean anything at all could be loaded, and the
                      // goldens would happily verify against the wrong model.
                      mdux::spec::Checks checks;
                      TestModel model;
                      checks.expect(bakeGoldens(model), "goldens baked");

                      // Alter a weight but keep the digest the package was baked with.
                      model.weightStorage[3] += 1.0f;

                      std::array<float, modelScratchFloats> scratch{};
                      const auto error = createError(model, scratch);
                      checks.expect(error.has_value() &&
                                        error->code == MlError::Code::DigestMismatch,
                                    std::format("expected digestMismatch, got {}",
                                                describeOutcome(error)));

                      // And with the digest brought back into agreement, the *goldens* must then
                      // fail - the weights really did change, so the recorded outputs cannot still
                      // be reproducible. Two independent controls, both firing.
                      model.reseal();
                      const auto goldenError = createError(model, scratch);
                      checks.expect(goldenError.has_value() &&
                                        goldenError->code == MlError::Code::GoldenMismatch,
                                    std::format("expected goldenMismatch, got {}",
                                                describeOutcome(goldenError)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register goldenMismatchCarriesEvidence{
    "A golden mismatch reports which element diverged and by what", "evidence-unit", [] {
        return speclab::Test("ml-runtime-golden-evidence")
            .Given("a package with one corrupted golden output bit", [] {})
            .When("a classifier is created", [] {})
            .Then("the error names the golden, the element and both bit patterns",
                  [] {
                      // MlError is the field incident record. A bare enum would discard exactly
                      // the information the report needs, so the indices are asserted here.
                      mdux::spec::Checks checks;
                      TestModel model;
                      checks.expect(bakeGoldens(model), "goldens baked");

                      const std::uint32_t original = model.goldenOutputBits[1];
                      const std::uint32_t corrupted = original ^ 0x1u;  // one low mantissa bit
                      model.goldenOutputBits[1] = corrupted;

                      std::array<float, modelScratchFloats> scratch{};
                      const auto error = createError(model, scratch);

                      checks.expect(error.has_value() &&
                                        error->code == MlError::Code::GoldenMismatch,
                                    std::format("expected goldenMismatch, got {}",
                                                describeOutcome(error)));
                      if (!error.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(error->goldenIndex == 0, "golden index");
                      checks.expect(error->elementIndex == 1, "element index");
                      checks.expect(error->expectedBits == corrupted, "expected bits recorded");
                      checks.expect(error->actualBits == original, "actual bits recorded");

                      // A single flipped mantissa bit is caught: the comparison is bitwise, and an
                      // epsilon would have accepted this difference silently.
                      checks.expect(error->expectedBits != error->actualBits,
                                    "the divergence is one bit and is still fatal");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register createRefusesUnsoundInputs{
    "create() refuses every unsound combination", "evidence-unit", [] {
        return speclab::Test("ml-runtime-create-refusals")
            .Given("a sound package, and one thing wrong at a time", [] {})
            .When("a classifier is created from each", [] {})
            .Then("each refusal names its own cause",
                  [] {
                      mdux::spec::Checks checks;
                      std::array<float, modelScratchFloats> scratch{};

                      {  // Scratch smaller than the package declares it needs.
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          std::array<float, 4> tinyScratch{};
                          const auto error = createError(model, tinyScratch);
                          checks.expect(error.has_value() &&
                                            error->code == MlError::Code::ScratchTooSmall,
                                        std::format("expected scratchTooSmall, got {}",
                                                    describeOutcome(error)));
                      }

                      {  // A blob shorter than the package declares. Handed to create() directly
                         // rather than by resizing the fixture, because the fixture derives
                         // weightsByteLength from its storage - growing that would move both sides
                         // of the comparison and test nothing. The length check runs before the
                         // digest, so this reports the specific cause rather than a generic
                         // mismatch.
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          const std::vector<GoldenVector> goldens = model.goldens();
                          const std::span<const std::byte> truncated =
                              model.weights().first(model.weights().size() - sizeof(float));
                          auto classifier =
                              Classifier1D::create(model.package(goldens), truncated, scratch);
                          checks.expect(!classifier.has_value() &&
                                            classifier.error().code ==
                                                MlError::Code::WeightsWrongSize,
                                        "a short weight blob is refused on length");
                      }

                      {  // A package that does not validate at all.
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          model.maxScratchFloats = 2;  // below the computed worst case
                          const auto error = createError(model, scratch);
                          checks.expect(error.has_value() &&
                                            error->code == MlError::Code::SchemaInvalid,
                                        std::format("expected schemaInvalid, got {}",
                                                    describeOutcome(error)));
                          checks.expect(error.has_value() &&
                                            error->schemaError == SchemaError::ScratchTooSmall,
                                        "the schema's own diagnostic is preserved");
                      }

                      {  // A package with nothing to self-test against. The loop over an empty
                         // golden set would succeed trivially, so this is the case that stops the
                         // strongest control in the subsystem from reporting a vacuous success.
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          const std::vector<GoldenVector> none;
                          auto classifier =
                              Classifier1D::create(model.package(none), model.weights(), scratch);
                          checks.expect(!classifier.has_value() &&
                                            classifier.error().code == MlError::Code::NoGoldens,
                                        "a package with no goldens is refused");
                      }

                      {  // A blob that is not f32-aligned gets its own code rather than borrowing
                         // ShapeMismatch, which would say a kernel rejected validated shapes.
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          const std::vector<GoldenVector> goldens = model.goldens();

                          // Offset the blob by one byte inside an oversized buffer, so the span is
                          // the right length but starts off the f32 grid.
                          std::vector<std::byte> shifted(model.weights().size() + 1);
                          const std::span<const std::byte> source = model.weights();
                          for (std::size_t i = 0; i < source.size(); ++i) {
                              shifted[i + 1] = source[i];
                          }
                          const std::span<const std::byte> misaligned{shifted.data() + 1,
                                                                      source.size()};
                          if ((reinterpret_cast<std::uintptr_t>(misaligned.data()) %
                               alignof(float)) != 0) {
                              auto classifier = Classifier1D::create(model.package(goldens),
                                                                     misaligned, scratch);
                              checks.expect(!classifier.has_value() &&
                                                classifier.error().code ==
                                                    MlError::Code::WeightsUnaligned,
                                            "a misaligned weight blob is refused by its own code");
                          }
                      }

                      {  // An unsupported schema version, rejected by validate().
                          TestModel model;
                          checks.expect(bakeGoldens(model), "goldens baked");
                          const std::vector<GoldenVector> goldens = model.goldens();
                          ModelPackage package = model.package(goldens);
                          package.schemaVersion = evidence::kSchemaVersion + 1;
                          auto classifier =
                              Classifier1D::create(package, model.weights(), scratch);
                          checks.expect(!classifier.has_value() &&
                                            classifier.error().code ==
                                                MlError::Code::SchemaVersion,
                                        "a future schema version is refused by its own code");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register predictValidatesItsSpans{
    "predict() refuses spans that are not the package's shape", "evidence-unit", [] {
        return speclab::Test("ml-runtime-predict-spans")
            .Given("a working classifier", [] {})
            .When("predict is called with the wrong input or output length", [] {})
            .Then("it refuses and writes nothing",
                  [] {
                      mdux::spec::Checks checks;
                      TestModel model;
                      checks.expect(bakeGoldens(model), "goldens baked");

                      std::array<float, modelScratchFloats> scratch{};
                      const std::vector<GoldenVector> goldens = model.goldens();
                      auto classifier =
                          Classifier1D::create(model.package(goldens), model.weights(), scratch);
                      checks.expect(classifier.has_value(), "created");
                      if (!classifier.has_value()) {
                          checks.raise();
                          return;
                      }

                      std::array<float, modelOutputLength> output{0.25f, 0.25f};
                      const std::array<float, 3> shortInput{};
                      auto shortInputResult = classifier->predict(shortInput, output);
                      checks.expect(!shortInputResult.has_value() &&
                                        shortInputResult.error().code ==
                                            MlError::Code::InputLength,
                                    "short input refused");
                      checks.expect(output[0] == 0.25f && output[1] == 0.25f,
                                    "output untouched by a refused prediction");

                      std::array<float, 5> wrongOutput{};
                      auto wrongOutputResult = classifier->predict(sampleInput(), wrongOutput);
                      checks.expect(!wrongOutputResult.has_value() &&
                                        wrongOutputResult.error().code ==
                                            MlError::Code::OutputLength,
                                    "wrong output length refused");

                      // A default-constructed classifier holds no layers and must refuse rather
                      // than dereferencing an empty span.
                      const Classifier1D empty;
                      std::array<float, modelOutputLength> ignored{};
                      checks.expect(!empty.predict(sampleInput(), ignored).has_value(),
                                    "a default-constructed classifier refuses to predict");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register goldensAreOptionalButChecked{
    "A package with several goldens checks every one of them", "evidence-unit", [] {
        return speclab::Test("ml-runtime-multiple-goldens")
            .Given("a package carrying three goldens, the last of which is wrong", [] {})
            .When("a classifier is created", [] {})
            .Then("the self-test reaches the third rather than stopping at the first pass",
                  [] {
                      // A self-test that only checked golden 0 would pass this and be worthless.
                      mdux::spec::Checks checks;
                      TestModel model;
                      checks.expect(bakeGoldens(model), "goldens baked");

                      std::vector<GoldenVector> goldens;
                      goldens.push_back(GoldenVector{.inputBits = model.goldenInputBits,
                                                     .expectedOutputBits = model.goldenOutputBits});
                      goldens.push_back(GoldenVector{.inputBits = model.goldenInputBits,
                                                     .expectedOutputBits = model.goldenOutputBits});

                      std::vector<std::uint32_t> wrongOutput = model.goldenOutputBits;
                      wrongOutput[0] ^= 0x1u;
                      goldens.push_back(GoldenVector{.inputBits = model.goldenInputBits,
                                                     .expectedOutputBits = wrongOutput});

                      std::array<float, modelScratchFloats> scratch{};
                      auto classifier =
                          Classifier1D::create(model.package(goldens), model.weights(), scratch);

                      checks.expect(!classifier.has_value(), "the third golden is not skipped");
                      if (classifier.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(classifier.error().code == MlError::Code::GoldenMismatch,
                                    "reported as a golden mismatch");
                      checks.expect(classifier.error().goldenIndex == 2,
                                    std::format("golden index is 2, got {}",
                                                classifier.error().goldenIndex));
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
