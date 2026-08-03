/**
 * @file WeightSwapTests.cpp
 * @brief The end-to-end proof that "weights are data" (issue #64).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * ADR-008's headline claim is that replacing a demonstrator's weights with a manufacturer's own
 * qualified weights is a re-bake with **zero application source change**. That is a strong claim
 * and an easy one to assert without evidence, so this file tests it against the two committed
 * packages rather than against fixtures it built itself - the claim is about committed artifacts,
 * so the test has to use them.
 *
 * Four things are asserted, and the fourth is the one worth writing carefully:
 *
 *   1. The two packages describe the *same architecture* - identical layers, identical dimensions.
 *      This is what makes them a swap rather than two unrelated models.
 *   2. They differ only in their weights and the goldens derived from them.
 *   3. `Classifier1D::create()` succeeds against the new package with its new goldens, and
 *      `predict()` reproduces them.
 *   4. Pairing the **old** `weights.bin` with the **new** `package.json` fails closed on
 *      `digestMismatch`.
 *
 * Point 4 is the difference between an integrity check and a comment claiming there is one. Without
 * it, the digest field could be entirely decorative and every other assertion here would still
 * pass.
 *
 * The "no application source change" half is structural rather than asserted at runtime: this file
 * loads both packages through the identical code path, with the only difference being which
 * directory it reads. If a swap needed a source change, that could not be written.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.ml.schema;
import mdux.ml.runtime;
import mdux.tools.cli;
import mdux.tools.ml.packageload;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::tools::ml;
namespace ml = mdux::ml;
namespace evidence = mdux::evidence;

/// The committed artifacts, addressed from the repository root the build passes in.
[[nodiscard]] std::filesystem::path modelDirectory(std::string_view id) {
    return std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "model" / id;
}

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
    auto text = readText(path);
    if (!text.has_value()) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(text->size());
    for (std::size_t i = 0; i < text->size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>((*text)[i]));
    }
    return bytes;
}

/// Everything one committed model directory holds.
struct CommittedModel {
    std::unique_ptr<LoadedPackage> package;
    std::vector<std::byte> weights;
};

/// Loads a committed model, or returns nullopt with the reason recorded in `why`.
[[nodiscard]] std::optional<CommittedModel> loadCommitted(std::string_view id, std::string& why) {
    const std::filesystem::path directory = modelDirectory(id);

    auto packageText = readText(directory / "package.json");
    if (!packageText.has_value()) {
        why = std::format("cannot read {}/package.json", id);
        return std::nullopt;
    }
    auto weights = readBytes(directory / "weights.bin");
    if (!weights.has_value()) {
        why = std::format("cannot read {}/weights.bin", id);
        return std::nullopt;
    }

    auto loaded = loadPackage(*packageText, std::format("{}/package.json", id));
    if (!loaded.has_value()) {
        why = std::format("{}: {}", id, loaded.error().message);
        return std::nullopt;
    }

    // The weight blob has to be aligned for f32 - a std::vector<std::byte> is, because operator new
    // returns storage aligned for any fundamental type, but the runtime checks rather than assumes.
    return CommittedModel{.package = std::move(*loaded), .weights = std::move(*weights)};
}

/// Compares two layers for architectural identity, ignoring anything weight-derived.
[[nodiscard]] bool sameArchitecture(const ml::LayerDesc& a, const ml::LayerDesc& b) noexcept {
    return a.kind == b.kind && a.activation == b.activation && a.inLength == b.inLength &&
           a.inChannels == b.inChannels && a.outLength == b.outLength &&
           a.outChannels == b.outChannels && a.kernelSize == b.kernelSize && a.stride == b.stride &&
           a.weights.rank == b.weights.rank && a.weights.shape == b.weights.shape &&
           a.bias.rank == b.bias.rank && a.bias.shape == b.bias.shape;
}

constexpr std::string_view baseId = "ecg-demo";
constexpr std::string_view swappedId = "ecg-demo-alt";

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register bothPackagesShareOneArchitecture{
    "The two committed packages describe the same architecture", "evidence-unit", [] {
        return speclab::Test("ml-weight-swap-architecture")
            .Given("the demonstrator package and the second weight set", [] {})
            .When("their layer descriptions are compared", [] {})
            .Then("every layer matches except the weight bytes they address",
                  [] {
                      // Without this, the other scenarios would still pass for two entirely
                      // unrelated models, and "swap" would not be the word for what was tested.
                      mdux::spec::Checks checks;
                      std::string why;
                      auto base = loadCommitted(baseId, why);
                      checks.expect(base.has_value(), why);
                      auto swapped = loadCommitted(swappedId, why);
                      checks.expect(swapped.has_value(), why);
                      if (!base.has_value() || !swapped.has_value()) {
                          checks.raise();
                          return;
                      }

                      const ml::ModelPackage first = base->package->view();
                      const ml::ModelPackage second = swapped->package->view();

                      checks.expect(first.inputLength == second.inputLength, "same input length");
                      checks.expect(first.outputLength == second.outputLength, "same output length");
                      checks.expect(first.maxScratchFloats == second.maxScratchFloats,
                                    "same scratch requirement");
                      checks.expect(first.layers.size() == second.layers.size(),
                                    "same layer count");
                      checks.expect(first.weightsByteLength == second.weightsByteLength,
                                    "same weight blob length");

                      if (first.layers.size() == second.layers.size()) {
                          for (std::size_t i = 0; i < first.layers.size(); ++i) {
                              checks.expect(sameArchitecture(first.layers[i], second.layers[i]),
                                            std::format("layer {} is architecturally identical", i));
                          }
                      }

                      // And they really are different weights, or the swap proves nothing.
                      checks.expect(first.weightsDigest != second.weightsDigest,
                                    "the two packages carry different weights");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register swappedPackageWorks{
    "The swapped package loads, self-tests and predicts", "evidence-unit", [] {
        return speclab::Test("ml-weight-swap-loads")
            .Given("the second weight set and its own golden vectors", [] {})
            .When("a classifier is created from it through the identical code path", [] {})
            .Then("creation succeeds and predictions reproduce its goldens",
                  [] {
                      mdux::spec::Checks checks;
                      std::string why;
                      auto swapped = loadCommitted(swappedId, why);
                      checks.expect(swapped.has_value(), why);
                      if (!swapped.has_value()) {
                          checks.raise();
                          return;
                      }

                      const ml::ModelPackage package = swapped->package->view();
                      std::vector<float> scratch(package.maxScratchFloats, 0.0f);

                      auto classifier =
                          ml::Classifier1D::create(package, swapped->weights, scratch);
                      checks.expect(classifier.has_value(),
                                    classifier.has_value()
                                        ? "created"
                                        : std::string{ml::describe(classifier.error().code)});
                      if (!classifier.has_value()) {
                          checks.raise();
                          return;
                      }

                      // create() already re-ran every golden; predicting one again confirms the
                      // object the caller ends up holding behaves the same way.
                      checks.expect(!package.goldens.empty(), "the package carries goldens");
                      if (package.goldens.empty()) {
                          checks.raise();
                          return;
                      }

                      const ml::GoldenVector& golden = package.goldens.front();
                      std::vector<float> input(package.inputLength, 0.0f);
                      for (std::size_t i = 0; i < input.size(); ++i) {
                          input[i] = std::bit_cast<float>(golden.inputBits[i]);
                      }
                      std::vector<float> output(package.outputLength, 0.0f);
                      checks.expect(classifier->predict(input, output).has_value(),
                                    "predict succeeded");
                      for (std::size_t i = 0; i < output.size(); ++i) {
                          checks.expect(std::bit_cast<std::uint32_t>(output[i]) ==
                                            golden.expectedOutputBits[i],
                                        std::format("output[{}] reproduces its golden", i));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register mismatchedPairFailsClosed{
    "The old weights against the new package fail closed", "evidence-unit", [] {
        return speclab::Test("ml-weight-swap-digest-is-load-bearing")
            .Given("the new package.json and the old weights.bin", [] {})
            .When("a classifier is created from the mismatched pair", [] {})
            .Then("it fails closed on digestMismatch rather than classifying",
                  [] {
                      // The one worth writing carefully. Without it the digest field could be
                      // entirely decorative and every other assertion in this file would still
                      // pass - this is what makes the integrity check demonstrably load-bearing.
                      mdux::spec::Checks checks;
                      std::string why;
                      auto base = loadCommitted(baseId, why);
                      checks.expect(base.has_value(), why);
                      auto swapped = loadCommitted(swappedId, why);
                      checks.expect(swapped.has_value(), why);
                      if (!base.has_value() || !swapped.has_value()) {
                          checks.raise();
                          return;
                      }

                      const ml::ModelPackage newPackage = swapped->package->view();
                      std::vector<float> scratch(newPackage.maxScratchFloats, 0.0f);

                      // Same length, same architecture, wrong bytes - which is exactly the
                      // deployment mistake this check exists to catch: a partially-updated device.
                      auto mismatched =
                          ml::Classifier1D::create(newPackage, base->weights, scratch);

                      checks.expect(!mismatched.has_value(),
                                    "the mismatched pair must not produce a classifier");
                      if (mismatched.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(mismatched.error().code == ml::MlError::Code::DigestMismatch,
                                    std::format("expected digestMismatch, got {}",
                                                ml::describe(mismatched.error().code)));

                      // The reverse pairing fails too, so this is not an artefact of which blob
                      // happened to be first.
                      const ml::ModelPackage oldPackage = base->package->view();
                      std::vector<float> otherScratch(oldPackage.maxScratchFloats, 0.0f);
                      auto reversed =
                          ml::Classifier1D::create(oldPackage, swapped->weights, otherScratch);
                      checks.expect(!reversed.has_value() &&
                                        reversed.error().code == ml::MlError::Code::DigestMismatch,
                                    "the reverse pairing is refused too");

                      // And each package still accepts its own weights, so the check is
                      // discriminating rather than simply always failing.
                      auto correct = ml::Classifier1D::create(oldPackage, base->weights,
                                                              otherScratch);
                      checks.expect(correct.has_value(),
                                    "the correctly-paired package still creates");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
