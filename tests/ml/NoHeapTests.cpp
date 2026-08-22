/**
 * @file NoHeapTests.cpp
 * @brief Layer 1 of issue #63: predict() makes no `operator new` call, proved by interposition.
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * "No allocation in predict()" is easy to claim and easy to break silently - a `std::vector` in a
 * helper, a `std::function`, a `std::string` in an error path. Review discipline does not catch
 * that reliably, so this binary replaces the global `operator new` family with counting versions
 * and measures.
 *
 * It catches any allocation that goes through the global `operator new` family - including one
 * hidden inside a `std` call nobody expected to allocate, which is the case review never finds.
 * It does **not** see a direct `std::malloc`, a pool allocator, or anything else that bypasses
 * `operator new`; layer 2's symbol scan is what covers those, which is why the three layers are
 * not redundant. It is also the one that could
 * silently become worthless: if the interposition stopped taking effect - a linker change, an
 * inlined allocation, a static runtime - the counter would simply never move and every scenario
 * would pass. So the first scenario below deliberately allocates and asserts the counter *does*
 * move. Without it, this file would be a test that cannot fail.
 *
 * Its own binary rather than a case inside ml_spec, so that a failure names the property that
 * broke instead of appearing as one line among two dozen unrelated scenarios.
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

// ---------------------------------------------------------------------------
// The interposition
// ---------------------------------------------------------------------------

// The interposition itself lives in the shared header: replacing a global operator is a
// whole-program property, and a second copy of that code would be a second thing to keep right.
// `allocations()` and the deliberate-allocation scenario below come with it.
#include "../framework/CountingAllocations.hpp"

// ---------------------------------------------------------------------------
// The model under test - the same shape the runtime suite uses
// ---------------------------------------------------------------------------

namespace {

using namespace mdux::ml;
namespace evidence = mdux::evidence;

constexpr std::uint32_t modelInputLength   = 8;
constexpr std::uint32_t modelOutputLength  = 2;
constexpr std::uint32_t modelScratchFloats = 24;
constexpr std::size_t   weightFloatCount   = 22;

[[nodiscard]] std::vector<LayerDesc> modelLayers() {
    return {
        LayerDesc{   .kind        = LayerKind::Conv1d,
                  .activation  = Activation::Relu,
                  .inLength    = 8,
                  .inChannels  = 1,
                  .outLength   = 6,
                  .outChannels = 2,
                  .kernelSize  = 3,
                  .stride      = 1,
                  .weights     = TensorRef{.byteOffset = 0, .shape = {2, 1, 3}, .rank = 3},
                  .bias        = TensorRef{.byteOffset = 24, .shape = {2, 0, 0}, .rank = 1}},
        LayerDesc{.kind        = LayerKind::MaxPool1d,
                  .activation  = Activation::None,
                  .inLength    = 6,
                  .inChannels  = 2,
                  .outLength   = 3,
                  .outChannels = 2,
                  .kernelSize  = 2,
                  .stride      = 2,
                  .weights     = TensorRef{},
                  .bias        = TensorRef{}                                               },
        LayerDesc{  .kind        = LayerKind::Flatten,
                  .activation  = Activation::None,
                  .inLength    = 3,
                  .inChannels  = 2,
                  .outLength   = 6,
                  .outChannels = 1,
                  .kernelSize  = 0,
                  .stride      = 0,
                  .weights     = TensorRef{},
                  .bias        = TensorRef{}                                               },
        LayerDesc{    .kind        = LayerKind::Dense,
                  .activation  = Activation::Softmax,
                  .inLength    = 6,
                  .inChannels  = 1,
                  .outLength   = 2,
                  .outChannels = 1,
                  .kernelSize  = 0,
                  .stride      = 0,
                  .weights     = TensorRef{.byteOffset = 32, .shape = {2, 6, 0}, .rank = 2},
                  .bias        = TensorRef{.byteOffset = 80, .shape = {2, 0, 0}, .rank = 1}},
    };
}

/// Declared ahead of Harness, which bakes its goldens over this input at construction.
[[nodiscard]] std::array<float, modelInputLength> sampleInput() noexcept;

/// Everything a classifier needs, kept alive for the duration of a scenario.
struct Harness {
    std::vector<float>     weightStorage;
    std::vector<LayerDesc> layers  = modelLayers();
    std::vector<float>     scratch = std::vector<float>(modelScratchFloats, 0.0f);
    std::string            id{"noheap-fixture"};
    evidence::Digest       digest{};

    Harness() : weightStorage(weightFloatCount, 0.0f) {
        std::uint32_t state = 99001u;
        for (float& value : weightStorage) {
            state = state * 1664525u + 1013904223u;
            value = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        }
        digest = evidence::sha256(std::as_bytes(std::span{weightStorage}));
        bakeGoldens();
    }

    /**
     * @brief Fills the golden storage by running the chain straight through the kernels.
     *
     * create() refuses a package with nothing to self-test against, so the goldens cannot be
     * produced by predicting through a classifier - generating them is the step that happens
     * before a package can be verified. Same applyLayer() the runtime uses, so what is recorded
     * here is what create() will later reproduce.
     */
    void bakeGoldens() {
        const auto                            input = sampleInput();
        const std::span<const float>          weightFloats{weightStorage};
        const std::size_t                     width = modelScratchFloats / 2;
        std::array<float, modelScratchFloats> work{};
        std::span<float>                      bufferA{work.data(), width};
        std::span<float>                      bufferB{work.data() + width, width};

        for (std::size_t i = 0; i < input.size(); ++i) {
            bufferA[i] = input[i];
        }
        std::span<const float> current = bufferA.first(static_cast<std::size_t>(layers[0].inputFloats()));
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const LayerDesc&       layer = layers[i];
            std::span<const float> weights;
            std::span<const float> bias;
            if (layer.weights.present()) {
                weights = weightFloats.subspan(layer.weights.byteOffset / sizeof(float), static_cast<std::size_t>(layer.weights.elementCount()));
            }
            if (layer.bias.present()) {
                bias = weightFloats.subspan(layer.bias.byteOffset / sizeof(float), static_cast<std::size_t>(layer.bias.elementCount()));
            }
            std::span<float> destination = ((i % 2) == 0 ? bufferB : bufferA).first(static_cast<std::size_t>(layer.outputFloats()));
            if (!applyLayer(layer, current, weights, bias, destination)) {
                return;
            }
            current = destination;
        }

        for (float value : input) {
            goldenInputBits.push_back(std::bit_cast<std::uint32_t>(value));
        }
        for (std::size_t i = 0; i < modelOutputLength; ++i) {
            goldenOutputBits.push_back(std::bit_cast<std::uint32_t>(current[i]));
        }
        goldenViews.push_back(GoldenVector{.inputBits = goldenInputBits, .expectedOutputBits = goldenOutputBits});
    }

    /// Golden storage, filled by bakeGoldens() before any classifier is built.
    std::vector<std::uint32_t> goldenInputBits;
    std::vector<std::uint32_t> goldenOutputBits;
    std::vector<GoldenVector>  goldenViews;

    [[nodiscard]] ModelPackage package() const noexcept {
        return ModelPackage{.id                = id,
                            .schemaVersion     = evidence::kSchemaVersion,
                            .weightsDigest     = digest,
                            .weightsByteLength = weightStorage.size() * sizeof(float),
                            .layers            = layers,
                            .goldens           = goldenViews,
                            .inputLength       = modelInputLength,
                            .outputLength      = modelOutputLength,
                            .maxScratchFloats  = modelScratchFloats};
    }
};

[[nodiscard]] std::array<float, modelInputLength> sampleInput() noexcept {
    return {0.5f, -0.25f, 1.0f, 0.75f, -1.0f, 0.125f, 0.0f, -0.5f};
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register counterIsLive{"The allocation counter actually observes allocations", "noheap", [] {
                                             return speclab::Test("ml-noheap-counter-is-live")
                                                 .Given("the replaced global operator new", [] {})
                                                 .When("an allocation is deliberately performed", [] {})
                                                 .Then("the counter moves, so a zero delta elsewhere means something",
                                                       [] {
                                                           // Without this scenario the whole file could pass while the interposition
                                                           // was not in effect at all - the counter would simply never move. This is
                                                           // the control that makes every other assertion here meaningful.
                                                           mdux::spec::Checks checks;

                                                           const std::size_t before = allocations();
                                                           // volatile so the allocation cannot be optimised away entirely.
                                                           auto* volatile probe       = new int(7);
                                                           const std::size_t afterNew = allocations();
                                                           delete probe;

                                                           checks.expect(afterNew > before,
                                                                         std::format("operator new is interposed: {} -> {}", before, afterNew));

                                                           const std::size_t   beforeVector = allocations();
                                                           std::vector<double> values(64, 1.0);
                                                           // Defeat any chance of the vector being elided.
                                                           values[0]                    += static_cast<double>(values.size());
                                                           const std::size_t afterVector = allocations();
                                                           checks.expect(afterVector > beforeVector, "container allocation is counted too");

                                                           // The over-aligned path is hand-written pointer arithmetic, so it is
                                                           // exercised here rather than left to chance. Without this the aligned
                                                           // operators could be quietly broken and every scenario would still pass,
                                                           // because nothing else in this binary allocates an over-aligned type.
                                                           struct alignas(64) Overaligned {
                                                               double values[8];
                                                           };
                                                           const std::size_t beforeAligned = allocations();
                                                           auto* volatile wide             = new Overaligned{};
                                                           const auto        address       = reinterpret_cast<std::uintptr_t>(wide);
                                                           const std::size_t afterAligned  = allocations();

                                                           checks.expect(afterAligned > beforeAligned, "over-aligned operator new is counted");
                                                           checks.expect(address % 64 == 0,
                                                                         std::format("over-aligned allocation is 64-aligned (got {})", address % 64));
                                                           // Freeing through the replaced aligned delete has to recover the original
                                                           // malloc pointer; if the stash were wrong this would corrupt the heap.
                                                           delete wide;
                                                           checks.raise();
                                                       })
                                                 .Execute();
                                         }};

const mdux::spec::Register predictAllocatesNothing{
    "predict() allocates nothing across a thousand calls",
    "noheap",
    [] {
        return speclab::Test("ml-noheap-predict")
            .Given("a constructed classifier and a warmed-up first call", [] {})
            .When("predict() runs a thousand times", [] {})
            .Then("the allocation counter has not moved at all",
                  [] {
                      mdux::spec::Checks checks;
                      Harness            harness;

                      auto classifier = Classifier1D::create(harness.package(), std::as_bytes(std::span{harness.weightStorage}), harness.scratch);
                      checks.expect(classifier.has_value(), "classifier created");
                      if (!classifier.has_value()) {
                          checks.raise();
                          return;
                      }

                      const auto                           input = sampleInput();
                      std::array<float, modelOutputLength> output{};

                      // One call outside the measurement, so anything one-time - a lazily
                      // initialised locale, a first-touch page - cannot be mistaken for a
                      // per-prediction allocation.
                      checks.expect(classifier->predict(input, output).has_value(), "warm-up prediction succeeded");

                      const std::size_t before             = allocations();
                      bool              everyCallSucceeded = true;
                      for (int i = 0; i < 1000; ++i) {
                          everyCallSucceeded = everyCallSucceeded && classifier->predict(input, output).has_value();
                      }
                      const std::size_t after = allocations();

                      checks.expect(everyCallSucceeded, "all 1000 predictions succeeded");
                      checks.expect(after == before, std::format("{} allocation(s) during 1000 predictions", after - before));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register failingPredictAllocatesNothing{
    "A refused prediction allocates nothing either",
    "noheap",
    [] {
        return speclab::Test("ml-noheap-predict-error-path")
            .Given("a classifier and inputs of the wrong shape", [] {})
            .When("predict() refuses them repeatedly", [] {})
            .Then("the error path is as allocation-free as the success path",
                  [] {
                      // The error path is where a std::string or a formatted diagnostic would
                      // most plausibly creep in, so it is measured separately rather than assumed
                      // to be covered by the success case.
                      mdux::spec::Checks checks;
                      Harness            harness;

                      auto classifier = Classifier1D::create(harness.package(), std::as_bytes(std::span{harness.weightStorage}), harness.scratch);
                      checks.expect(classifier.has_value(), "classifier created");
                      if (!classifier.has_value()) {
                          checks.raise();
                          return;
                      }

                      const std::array<float, 3>           wrongInput{};
                      std::array<float, modelOutputLength> output{};
                      (void)classifier->predict(wrongInput, output);  // warm-up

                      const std::size_t before           = allocations();
                      bool              everyCallRefused = true;
                      for (int i = 0; i < 1000; ++i) {
                          everyCallRefused = everyCallRefused && !classifier->predict(wrongInput, output).has_value();
                      }
                      const std::size_t after = allocations();

                      checks.expect(everyCallRefused, "all 1000 predictions were refused");
                      checks.expect(after == before, std::format("{} allocation(s) on the error path", after - before));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register selfTestAllocatesNothing{"create()'s golden self-test allocates nothing", "noheap", [] {
                                                        return speclab::Test("ml-noheap-create")
                                                            .Given("a package carrying golden vectors", [] {})
                                                            .When("create() runs its digest check and self-test", [] {})
                                                            .Then("no allocation occurs, so create() is usable before any heap exists",
                                                                  [] {
                                                                      // create() runs once at startup, so an allocation there would be far less
                                                                      // serious than one in predict(). It is still asserted: on a device that
                                                                      // brings the classifier up before its allocator, "bounded startup work"
                                                                      // needs to mean no heap at all.
                                                                      mdux::spec::Checks checks;
                                                                      Harness            harness;

                                                                      // The harness baked its goldens at construction, so the package handed
                                                                      // to create() below already carries a real self-test to run.
                                                                      const ModelPackage package = harness.package();
                                                                      const auto         weights = std::as_bytes(std::span{harness.weightStorage});

                                                                      const std::size_t before   = allocations();
                                                                      auto              verified = Classifier1D::create(package, weights, harness.scratch);
                                                                      const std::size_t after    = allocations();

                                                                      checks.expect(verified.has_value(), "self-tested classifier created");
                                                                      checks.expect(after == before,
                                                                                    std::format("{} allocation(s) during create()", after - before));
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

}  // namespace
