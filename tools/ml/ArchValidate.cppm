/**
 * @file ArchValidate.cppm
 * @brief Host-tools-zone architecture check: does the recipe describe the weights it imported?
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Checks a recipe's declared architecture against the tensors actually present in a safetensors
 * file, *before* anything is baked. Every rejection is an explicit error rather than a silent
 * coercion - a dense layer quietly reshaped to fit its weights is a model that classifies
 * something other than what the author reviewed.
 *
 * ## One set of shape rules, not two
 *
 * This module deliberately does not restate the shape rules. It resolves each layer's named
 * tensors, records their shapes **as the file declares them**, packs the weight blob, and then
 * hands the result to `mdux::ml::ModelPackage::validate()` - the same governed function the device
 * runtime calls. Every "weight tensor disagrees with the layer's dimensions" finding therefore
 * comes from the canonical rule rather than from a host-side copy of it that could drift.
 *
 * The reason that works is that the `TensorRef` shapes come from the *file* while the layer
 * dimensions come from the *recipe*. If the host filled in both sides from the recipe, the check
 * would be vacuous - which is the mistake this comment exists to prevent.
 *
 * What is left here is only what the schema cannot see: tensor names, their presence in the file,
 * and the packing of the blob.
 *
 * ## The blob layout is MduX's, not the file's
 *
 * Tensors are packed in layer order, weights then bias, contiguously. The safetensors file's own
 * ordering is irrelevant and is not preserved: the package addresses its blob by byte offset, and
 * a layout derived from the layer chain is reproducible from the recipe alone.
 */
module;

export module mdux.tools.ml.archvalidate;

import std;
import mdux.core.result;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.ml.safetensors;

export namespace mdux::tools::ml {

/// One layer as a recipe declares it: the dimensions the author asserts, plus the names of the
/// tensors in the weights file that should fill them.
struct LayerSpec {
    mdux::ml::LayerKind kind{mdux::ml::LayerKind::Dense};
    mdux::ml::Activation activation{mdux::ml::Activation::None};
    std::uint32_t inLength{0};
    std::uint32_t inChannels{0};
    std::uint32_t outLength{0};
    std::uint32_t outChannels{0};
    std::uint32_t kernelSize{0};
    std::uint32_t stride{0};
    std::string weightsTensor;  ///< empty for a layer that carries no weights
    std::string biasTensor;
};

/// A whole architecture as a recipe declares it.
struct ArchitectureSpec {
    std::string id;
    std::uint32_t inputLength{0};
    std::uint32_t outputLength{0};
    std::vector<LayerSpec> layers;
    /// 0 means "derive it", which is the normal case - the baker should not be asked to restate a
    /// number the layer chain already determines.
    std::uint32_t maxScratchFloats{0};
};

/// What a successful resolution produces: everything a ModelPackage needs except its goldens.
struct ResolvedArchitecture {
    std::vector<mdux::ml::LayerDesc> layers;
    std::vector<std::byte> weights;  ///< packed in layer order, f32 throughout
    std::uint32_t inputLength{0};
    std::uint32_t outputLength{0};
    std::uint32_t maxScratchFloats{0};
};

/**
 * @brief Resolves and validates, returning every problem rather than only the first.
 *
 * An author fixing a recipe wants the whole list; stopping at the first missing tensor turns one
 * edit-and-rerun cycle into five. (The safetensors parser stops at its first problem for the
 * opposite reason - findings past a malformed header are not trustworthy.)
 *
 * @param fileBytes the whole safetensors file, which the tensor byte ranges index into
 */
[[nodiscard]] mdux::core::Result<ResolvedArchitecture, std::vector<cli::Diagnostic>>
resolveArchitecture(const ArchitectureSpec& spec, const SafetensorsFile& file,
                    std::span<const std::byte> fileBytes, std::string_view recipeName);

}  // namespace mdux::tools::ml
