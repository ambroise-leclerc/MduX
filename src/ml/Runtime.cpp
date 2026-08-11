/**
 * @file Runtime.cpp
 * @brief Implementation of the fail-closed device-side classifier.
 *
 * @compliance ADR-005 Error handling and exceptions policy (noexcept throughout, no throwing)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Read Runtime.cppm's module comment first; in particular, the ordering of the checks in create()
 * is part of the design rather than an accident of how it was written.
 */
module;

module mdux.ml.runtime;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.ml.schema;
import mdux.ml.kernels;

namespace mdux::ml {

using mdux::core::err;

std::string_view describe(MlError::Code code) noexcept {
    switch (code) {
        case MlError::Code::SchemaInvalid:    return "model package failed schema validation";
        case MlError::Code::DigestMismatch:   return "weight blob digest does not match the package";
        case MlError::Code::WeightsWrongSize: return "weight blob length does not match the package";
        case MlError::Code::WeightsUnaligned: return "weight blob is not aligned for f32 access";
        case MlError::Code::SchemaVersion:    return "unsupported package schema version";
        case MlError::Code::UnsupportedLayer: return "package uses more layers than the runtime supports";
        case MlError::Code::ShapeMismatch:    return "a kernel rejected the shapes the package implied";
        case MlError::Code::ScratchTooSmall:  return "scratch buffer is smaller than maxScratchFloats";
        case MlError::Code::NoGoldens:        return "package carries no golden vectors to self-test against";
        case MlError::Code::GoldenMismatch:   return "a golden vector did not reproduce bit for bit";
        case MlError::Code::InputLength:      return "input length does not match the package";
        case MlError::Code::OutputLength:     return "output length does not match the package";
    }
    return "unknown ML error";
}

namespace {

/// Half the scratch: the largest activation the chain holds. predict() ping-pongs between the two
/// halves, so this is the width of each buffer.
[[nodiscard]] std::size_t bufferWidth(std::span<const LayerDesc> layers,
                                      std::uint32_t inputLength) noexcept {
    return static_cast<std::size_t>(requiredScratchFloats(layers, inputLength) / 2);
}

}  // namespace

mdux::core::Result<Classifier1D, MlError> Classifier1D::create(const ModelPackage& package,
                                                               std::span<const std::byte> weights,
                                                               std::span<float> scratch) noexcept {
    // 1. The package itself. Everything below assumes a validated descriptor - the kernels are
    //    written without defensive checks in their inner loops precisely because of this call.
    if (auto valid = package.validate(); !valid.has_value()) {
        // A wrong schema version is reported as itself rather than folded into the generic
        // "package is invalid": it is the one rejection a caller can act on without reading the
        // schema diagnostic, because it means the artifact and the binary are different vintages.
        const MlError::Code code = valid.error() == SchemaError::UnsupportedSchemaVersion
                                       ? MlError::Code::SchemaVersion
                                       : MlError::Code::SchemaInvalid;
        return err(MlError{.code = code, .schemaError = valid.error()});
    }
    if (package.layers.size() > maxSupportedLayers) {
        // The first layer that does not fit, so the field is an index as its name says. The
        // count is recoverable from the package; the index is the actionable half.
        return err(MlError{.code = MlError::Code::UnsupportedLayer,
                           .layerIndex = static_cast<std::uint32_t>(maxSupportedLayers)});
    }

    // 2. The weights are exactly the bytes the package was baked against. Without this check,
    //    "the caller supplies the weights" would mean "anything at all can be loaded", and the
    //    golden vectors would be self-consistent against the wrong model.
    if (weights.size() != package.weightsByteLength) {
        return err(MlError{.code = MlError::Code::WeightsWrongSize});
    }
    if (package.weightsDigest != evidence::sha256(weights)) {
        return err(MlError{.code = MlError::Code::DigestMismatch});
    }

    // The format stores f32 object representations, so the blob has to be readable as float. This
    // is the one place the runtime depends on the caller's allocation being suitably aligned -
    // mmap, a linked array and a ROM section all are, but a byte span carved out of a larger
    // buffer at an odd offset would not be. Checked rather than assumed, and fails closed.
    //
    // Note the golden self-test below independently covers a *wrong* interpretation of these
    // bytes: if the weights were being read incorrectly, no golden vector would reproduce.
    const auto blobAddress = reinterpret_cast<std::uintptr_t>(weights.data());  // mdux-governed-lint:allow
    if (!weights.empty() && blobAddress % alignof(float) != 0) {
        return err(MlError{.code = MlError::Code::WeightsUnaligned});
    }

    // 3. Scratch.
    if (scratch.size() < package.maxScratchFloats) {
        return err(MlError{.code = MlError::Code::ScratchTooSmall,
                           .elementIndex = static_cast<std::uint32_t>(scratch.size())});
    }

    Classifier1D classifier;
    classifier.layers_ = package.layers;
    classifier.scratch_ = scratch;
    classifier.inputLength_ = package.inputLength;
    classifier.outputLength_ = package.outputLength;

    // Resolve every tensor to a float span once, here, rather than per predict() call.
    const float* base = weights.empty() ? nullptr : reinterpret_cast<const float*>(weights.data());  // mdux-governed-lint:allow
    for (std::size_t i = 0; i < package.layers.size(); ++i) {
        const LayerDesc& layer = package.layers[i];
        LayerTensors tensors;
        if (layer.weights.present()) {
            tensors.weights = std::span<const float>{
                base + layer.weights.byteOffset / sizeof(float),
                static_cast<std::size_t>(layer.weights.elementCount())};
        }
        if (layer.bias.present()) {
            tensors.bias =
                std::span<const float>{base + layer.bias.byteOffset / sizeof(float),
                                       static_cast<std::size_t>(layer.bias.elementCount())};
        }
        classifier.tensors_[i] = tensors;
    }

    // 4. There has to be something to self-test against. Step 5's loop over an empty golden set
    //    succeeds trivially, which would make the strongest control in this subsystem report
    //    success without checking anything - worse than not having it, because the failure is
    //    silent. mdux.ml.schema accepts an empty set on purpose (the baker validates an
    //    architecture before generating goldens); the requirement lives here, at the boundary
    //    where an unverified model would actually run.
    if (package.goldens.empty()) {
        return err(MlError{.code = MlError::Code::NoGoldens});
    }

    // 5. The self-test. A genuine safety control, not a late unit test - see Runtime.cppm.
    const std::size_t width = bufferWidth(package.layers, package.inputLength);
    for (std::size_t g = 0; g < package.goldens.size(); ++g) {
        const GoldenVector& golden = package.goldens[g];

        // Materialise the golden input into the first scratch buffer. Bit patterns, so this is a
        // reinterpretation of the stored bits, never a decimal parse.
        std::span<float> input = scratch.first(width);
        for (std::size_t i = 0; i < golden.inputBits.size(); ++i) {
            input[i] = std::bit_cast<float>(golden.inputBits[i]);
        }

        auto produced = classifier.runFromScratch();
        if (!produced.has_value()) {
            MlError error = produced.error();
            error.goldenIndex = static_cast<std::uint32_t>(g);
            return err(error);
        }

        const std::span<const float> actual = *produced;
        for (std::size_t i = 0; i < golden.expectedOutputBits.size(); ++i) {
            const std::uint32_t actualBits = std::bit_cast<std::uint32_t>(actual[i]);
            if (actualBits != golden.expectedOutputBits[i]) {
                // The whole point of MlError carrying evidence: this record is what a field
                // incident report needs, and the divergence is by definition not reproducible on
                // the bench - if it were, CI would have caught it.
                // schemaError and layerIndex are left at their defaults: the header says the
                // index fields are only meaningful for the codes that set them, and spelling out
                // placeholders here would imply they carry information.
                return err(MlError{.code = MlError::Code::GoldenMismatch,
                                   .goldenIndex = static_cast<std::uint32_t>(g),
                                   .elementIndex = static_cast<std::uint32_t>(i),
                                   .expectedBits = golden.expectedOutputBits[i],
                                   .actualBits = actualBits});
            }
        }
    }

    return classifier;
}

mdux::core::Result<std::span<const float>, MlError> Classifier1D::runFromScratch() const noexcept {
    const std::size_t width = bufferWidth(layers_, inputLength_);
    std::span<float> bufferA = scratch_.first(width);
    std::span<float> bufferB = scratch_.subspan(width, width);

    // The input is already in bufferA. Each layer reads one buffer and writes the other, so a
    // kernel never has its input and output aliased.
    std::span<const float> current = bufferA.first(static_cast<std::size_t>(layers_[0].inputFloats()));

    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const LayerDesc& layer = layers_[i];
        std::span<float> destination = ((i % 2) == 0 ? bufferB : bufferA)
                                           .first(static_cast<std::size_t>(layer.outputFloats()));

        if (!applyLayer(layer, current, tensors_[i].weights, tensors_[i].bias, destination)) {
            return err(MlError{.code = MlError::Code::ShapeMismatch,
                               .layerIndex = static_cast<std::uint32_t>(i)});
        }
        current = destination;
    }
    return current;
}

mdux::core::ResultVoid<MlError> Classifier1D::predict(std::span<const float> input,
                                                      std::span<float> output) const noexcept {
    if (layers_.empty()) {
        return err(MlError{.code = MlError::Code::SchemaInvalid});
    }
    if (input.size() != inputLength_) {
        return err(MlError{.code = MlError::Code::InputLength,
                           .elementIndex = static_cast<std::uint32_t>(input.size())});
    }
    if (output.size() != outputLength_) {
        return err(MlError{.code = MlError::Code::OutputLength,
                           .elementIndex = static_cast<std::uint32_t>(output.size())});
    }

    const std::size_t width = bufferWidth(layers_, inputLength_);
    std::span<float> staging = scratch_.first(width);
    for (std::size_t i = 0; i < input.size(); ++i) {
        staging[i] = input[i];
    }

    auto produced = runFromScratch();
    if (!produced.has_value()) {
        return err(produced.error());
    }

    // Written only on success, so a failed prediction cannot leave a caller holding half an
    // answer it might mistake for a classification.
    const std::span<const float> result = *produced;
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = result[i];
    }
    return {};
}

}  // namespace mdux::ml
