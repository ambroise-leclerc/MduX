/**
 * @file ArchValidate.cpp
 * @brief Implementation of the recipe-versus-weights architecture check.
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 */
module;

module mdux.tools.ml.archvalidate;

import std;
import mdux.core.result;
import mdux.evidence.report;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.ml.safetensors;

namespace mdux::tools::ml {

using mdux::core::err;
namespace ml = mdux::ml;

namespace {

[[nodiscard]] cli::Diagnostic problem(std::string_view recipeName, std::string code,
                                      std::string message, std::string fixHint = {}) {
    return cli::Diagnostic{.file = std::string{recipeName},
                           .line = 0,
                           .column = 0,
                           .code = std::move(code),
                           .severity = cli::Severity::Error,
                           .message = std::move(message),
                           .fixHint = std::move(fixHint)};
}

/// Maps a schema rejection to a diagnostic code. The most actionable causes get their own code so
/// a recipe author (or an agent reading --format=json) can key off it; the rest share one.
[[nodiscard]] std::string codeFor(ml::SchemaError error) noexcept {
    switch (error) {
        case ml::SchemaError::WeightShapeMismatch:
        case ml::SchemaError::BiasShapeMismatch:
            return "mdux.ml.arch.shapeMismatch";
        case ml::SchemaError::MissingBias:
        case ml::SchemaError::MissingWeights:
            return "mdux.ml.arch.missingTensor";
        case ml::SchemaError::UnexpectedWeights:
            return "mdux.ml.arch.unexpectedTensor";
        case ml::SchemaError::ScratchTooSmall:
            return "mdux.ml.arch.scratchTooSmall";
        case ml::SchemaError::LayerChainMismatch:
        case ml::SchemaError::InputLengthMismatch:
        case ml::SchemaError::OutputLengthMismatch:
            return "mdux.ml.arch.chainMismatch";
        case ml::SchemaError::OutputLengthNotDerivable:
        case ml::SchemaError::KernelLargerThanInput:
            return "mdux.ml.arch.windowMismatch";
        default:
            return "mdux.ml.arch.invalid";
    }
}

/// Copies a tensor's f32 bytes out of the file and appends them to the packed blob, returning the
/// TensorRef that addresses them. The shape recorded is the *file's*, which is what makes the
/// schema's shape check meaningful - see ArchValidate.cppm.
[[nodiscard]] std::optional<ml::TensorRef> appendTensor(const TensorEntry& tensor,
                                                        std::span<const std::byte> fileBytes,
                                                        std::vector<std::byte>& blob) {
    ml::TensorRef reference;
    reference.byteOffset = blob.size();
    reference.rank = static_cast<std::uint8_t>(tensor.shape.size());
    for (std::size_t i = 0; i < tensor.shape.size() && i < ml::maxTensorRank; ++i) {
        // TensorRef stores extents as uint32 while safetensors declares them as uint64. A silent
        // narrowing here would be the worst kind: the appended bytes stay the full length, but the
        // TensorRef describing them wraps to something small, so the package validates against a
        // blob it mis-describes. Reported rather than truncated.
        if (tensor.shape[i] > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        reference.shape[i] = static_cast<std::uint32_t>(tensor.shape[i]);
    }

    const std::span<const std::byte> source =
        fileBytes.subspan(static_cast<std::size_t>(tensor.byteOffset),
                          static_cast<std::size_t>(tensor.byteLength));
    blob.insert(blob.end(), source.begin(), source.end());
    return reference;
}

/// Appends `tensor` into `blob`, reporting a diagnostic instead of narrowing an extent silently.
[[nodiscard]] bool appendInto(const TensorEntry& tensor, std::span<const std::byte> fileBytes,
                              std::vector<std::byte>& blob, ml::TensorRef& out,
                              std::string_view recipeName, std::size_t layerIndex,
                              std::string_view role, std::vector<cli::Diagnostic>& diagnostics) {
    auto appended = appendTensor(tensor, fileBytes, blob);
    if (!appended.has_value()) {
        diagnostics.push_back(problem(
            recipeName, "mdux.ml.arch.shapeMismatch",
            std::format("layer {}'s {} tensor '{}' has an extent too large to describe", layerIndex,
                        role, tensor.name)));
        return false;
    }
    out = *appended;
    return true;
}

}  // namespace

mdux::core::Result<ResolvedArchitecture, std::vector<cli::Diagnostic>> resolveArchitecture(
    const ArchitectureSpec& spec, const SafetensorsFile& file, std::span<const std::byte> fileBytes,
    std::string_view recipeName) {
    std::vector<cli::Diagnostic> diagnostics;

    if (spec.layers.empty()) {
        diagnostics.push_back(problem(recipeName, "mdux.ml.arch.noLayers",
                                      "the architecture declares no layers"));
        return err(std::move(diagnostics));
    }

    ResolvedArchitecture resolved;
    resolved.inputLength = spec.inputLength;
    resolved.outputLength = spec.outputLength;
    resolved.layers.reserve(spec.layers.size());

    for (std::size_t i = 0; i < spec.layers.size(); ++i) {
        const LayerSpec& layerSpec = spec.layers[i];
        ml::LayerDesc layer{.kind = layerSpec.kind,
                            .activation = layerSpec.activation,
                            .inLength = layerSpec.inLength,
                            .inChannels = layerSpec.inChannels,
                            .outLength = layerSpec.outLength,
                            .outChannels = layerSpec.outChannels,
                            .kernelSize = layerSpec.kernelSize,
                            .stride = layerSpec.stride,
                            .weights = ml::TensorRef{},
                            .bias = ml::TensorRef{}};

        // A layer kind that carries no weights must not name any: silently ignoring the name would
        // hide a recipe that thinks its pooling layer is learned.
        const bool wantsWeights = ml::carriesWeights(layerSpec.kind);
        if (!wantsWeights && (!layerSpec.weightsTensor.empty() || !layerSpec.biasTensor.empty())) {
            diagnostics.push_back(problem(
                recipeName, "mdux.ml.arch.unexpectedTensor",
                std::format("layer {} is {} and carries no weights, but names a tensor", i,
                            ml::layerKindWireValues[static_cast<std::size_t>(layerSpec.kind)])));
        }

        if (wantsWeights) {
            if (layerSpec.weightsTensor.empty()) {
                diagnostics.push_back(problem(
                    recipeName, "mdux.ml.arch.missingTensor",
                    std::format("layer {} names no weight tensor", i)));
            } else if (const TensorEntry* tensor = file.find(layerSpec.weightsTensor)) {
                (void)appendInto(*tensor, fileBytes, resolved.weights, layer.weights, recipeName, i,
                                 "weight", diagnostics);
            } else {
                diagnostics.push_back(problem(
                    recipeName, "mdux.ml.arch.missingTensor",
                    std::format("layer {} names weight tensor '{}', which the weights file does "
                                "not contain",
                                i, layerSpec.weightsTensor),
                    "check the tensor name against the exporter's naming"));
            }

            if (layerSpec.biasTensor.empty()) {
                diagnostics.push_back(problem(
                    recipeName, "mdux.ml.arch.missingTensor",
                    std::format("layer {} names no bias tensor; every v1 dense and conv1d layer "
                                "carries one",
                                i)));
            } else if (const TensorEntry* tensor = file.find(layerSpec.biasTensor)) {
                (void)appendInto(*tensor, fileBytes, resolved.weights, layer.bias, recipeName, i,
                                 "bias", diagnostics);
            } else {
                diagnostics.push_back(problem(
                    recipeName, "mdux.ml.arch.missingTensor",
                    std::format("layer {} names bias tensor '{}', which the weights file does not "
                                "contain",
                                i, layerSpec.biasTensor)));
            }
        }

        resolved.layers.push_back(layer);
    }

    // A missing tensor makes every downstream shape finding noise, so stop before delegating.
    if (!diagnostics.empty()) {
        return err(std::move(diagnostics));
    }

    if (spec.maxScratchFloats != 0) {
        resolved.maxScratchFloats = spec.maxScratchFloats;
    } else {
        // requiredScratchFloats() is uint64 and maxScratchFloats is uint32. A bare cast wraps for
        // a large enough activation, and the wrapped value is *smaller* - so validate() then
        // rejects the package as ScratchTooSmall, pointing at a budget the baker itself derived.
        // That diagnostic sends the author looking in exactly the wrong place, so the real cause
        // is reported here instead.
        const std::uint64_t required = ml::requiredScratchFloats(resolved.layers, spec.inputLength);
        if (required > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.push_back(problem(
                recipeName, "mdux.ml.arch.scratchTooSmall",
                std::format("the layer chain needs {} scratch floats, which does not fit in the "
                            "package's 32-bit budget",
                            required),
                "the architecture is too large for the v1 package format"));
            return err(std::move(diagnostics));
        }
        resolved.maxScratchFloats = static_cast<std::uint32_t>(required);
    }

    // The canonical rules, run once, from the governed module. See ArchValidate.cppm.
    const ml::ModelPackage package{.id = spec.id,
                                   .schemaVersion = mdux::evidence::kSchemaVersion,
                                   .weightsDigest = {},
                                   .weightsByteLength = resolved.weights.size(),
                                   .layers = resolved.layers,
                                   .goldens = {},
                                   .inputLength = spec.inputLength,
                                   .outputLength = spec.outputLength,
                                   .maxScratchFloats = resolved.maxScratchFloats};

    if (auto valid = package.validate(); !valid.has_value()) {
        const ml::SchemaError error = valid.error();
        diagnostics.push_back(problem(
            recipeName, codeFor(error),
            std::format("the declared architecture does not match the imported weights: {}",
                        ml::describe(error)),
            "compare the recipe's layer dimensions against the tensor shapes in the weights file"));
        return err(std::move(diagnostics));
    }

    return resolved;
}

}  // namespace mdux::tools::ml
