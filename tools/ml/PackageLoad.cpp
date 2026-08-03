/**
 * @file PackageLoad.cpp
 * @brief Implementation of the host-side package reader.
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 */
module;

module mdux.tools.ml.packageload;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.ml.schema;
import mdux.tools.cli;

namespace mdux::tools::ml {

using mdux::core::err;
namespace evidence = mdux::evidence;
namespace json = mdux::evidence::json;
namespace ml = mdux::ml;

namespace {

constexpr std::string_view malformed = "mdux.ml.package.malformed";
constexpr std::string_view invalid = "mdux.ml.package.invalid";

[[nodiscard]] cli::Diagnostic problem(std::string_view fileName, std::string_view code,
                                      std::string message) {
    return cli::Diagnostic{.file = std::string{fileName},
                           .line = 0,
                           .column = 0,
                           .code = std::string{code},
                           .severity = cli::Severity::Error,
                           .message = std::move(message),
                           .fixHint = {}};
}

[[nodiscard]] std::optional<ml::LayerKind> layerKindFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < ml::layerKindWireValues.size(); ++i) {
        if (ml::layerKindWireValues[i] == wire) {
            return static_cast<ml::LayerKind>(i);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ml::Activation> activationFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < ml::activationWireValues.size(); ++i) {
        if (ml::activationWireValues[i] == wire) {
            return static_cast<ml::Activation>(i);
        }
    }
    return std::nullopt;
}

/// Reads an unsigned member, or nullopt when it is absent or the wrong kind.
[[nodiscard]] std::optional<std::uint64_t> readUInt(const json::Value& object,
                                                    std::string_view key) {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return std::nullopt;
    }
    auto value = member->asUInt();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return *value;
}

/// Reads an unsigned member that must fit in `uint32`, or nullopt.
///
/// Separate from readUInt() because a straight cast is the wrong thing for the fields that feed
/// buffer sizes: a JSON value above 2^32 would wrap, and validate() would then be checking a
/// different number than the file actually contained - passing on a package that describes
/// something else entirely.
[[nodiscard]] std::optional<std::uint32_t> readUInt32(const json::Value& object,
                                                      std::string_view key) {
    const std::optional<std::uint64_t> wide = readUInt(object, key);
    if (!wide.has_value() || *wide > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*wide);
}

/// Reads a TensorRef. An absent member is an absent tensor, which is well-formed.
[[nodiscard]] std::optional<ml::TensorRef> readTensor(const json::Value& layer,
                                                      std::string_view key) {
    const json::Value* member = layer.find(key);
    if (member == nullptr) {
        return ml::TensorRef{};
    }
    if (member->kind() != json::Value::Kind::Object) {
        return std::nullopt;
    }
    auto offset = readUInt(*member, "byteOffset");
    const json::Value* shape = member->find("shape");
    if (!offset.has_value() || shape == nullptr ||
        shape->kind() != json::Value::Kind::Array) {
        return std::nullopt;
    }
    const std::span<const json::Value> dimensions = shape->elements();
    if (dimensions.size() > ml::maxTensorRank) {
        return std::nullopt;
    }

    ml::TensorRef tensor;
    tensor.byteOffset = *offset;
    tensor.rank = static_cast<std::uint8_t>(dimensions.size());
    for (std::size_t i = 0; i < dimensions.size(); ++i) {
        auto extent = dimensions[i].asUInt();
        if (!extent.has_value() || *extent > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        tensor.shape[i] = static_cast<std::uint32_t>(*extent);
    }
    return tensor;
}

/// Reads an array of u32 bit patterns. Bit patterns, never decimal - see ADR-008 decision 4.
[[nodiscard]] std::optional<std::vector<std::uint32_t>> readBits(const json::Value& object,
                                                                 std::string_view key) {
    const json::Value* member = object.find(key);
    if (member == nullptr || member->kind() != json::Value::Kind::Array) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> bits;
    bits.reserve(member->elements().size());
    for (const json::Value& element : member->elements()) {
        auto value = element.asUInt();
        if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        bits.push_back(static_cast<std::uint32_t>(*value));
    }
    return bits;
}

}  // namespace

ml::ModelPackage LoadedPackage::view() const noexcept {
    return ml::ModelPackage{.id = id_,
                            .schemaVersion = schemaVersion_,
                            .weightsDigest = weightsDigest_,
                            .weightsByteLength = weightsByteLength_,
                            .layers = layers_,
                            .goldens = goldenViews_,
                            .inputLength = inputLength_,
                            .outputLength = outputLength_,
                            .maxScratchFloats = maxScratchFloats_};
}

mdux::core::Result<std::unique_ptr<LoadedPackage>, cli::Diagnostic> loadPackage(
    std::string_view text, std::string_view fileName) {
    auto parsed = json::parse(text);
    if (!parsed.has_value()) {
        return err(problem(fileName, malformed,
                           std::format("package.json is not valid JSON: {}",
                                       json::describe(parsed.error().code))));
    }
    const json::Value& root = *parsed;
    if (root.kind() != json::Value::Kind::Object) {
        return err(problem(fileName, malformed, "package.json is not an object"));
    }

    auto header = evidence::PackageHeader::readFrom(root);
    if (!header.has_value()) {
        return err(problem(fileName, malformed,
                           std::format("package header is malformed: {}",
                                       evidence::describe(header.error()))));
    }
    if (header->kind != ml::packageKind) {
        return err(problem(fileName, malformed,
                           std::format("package kind is '{}', expected '{}'", header->kind,
                                       ml::packageKind)));
    }

    auto loaded = std::make_unique<LoadedPackage>();
    loaded->id_ = header->id;
    loaded->schemaVersion_ = header->schemaVersion;

    const auto inputLength = readUInt32(root, "inputLength");
    const auto outputLength = readUInt32(root, "outputLength");
    const auto scratch = readUInt32(root, "maxScratchFloats");
    if (!inputLength.has_value() || !outputLength.has_value() || !scratch.has_value()) {
        return err(problem(
            fileName, malformed,
            "package is missing inputLength, outputLength or maxScratchFloats, or one of them "
            "does not fit in 32 bits"));
    }
    loaded->inputLength_ = *inputLength;
    loaded->outputLength_ = *outputLength;
    loaded->maxScratchFloats_ = *scratch;

    const json::Value* weights = root.find("weights");
    if (weights == nullptr || weights->kind() != json::Value::Kind::Object) {
        return err(problem(fileName, malformed, "package has no weights record"));
    }
    const auto weightsLength = readUInt(*weights, "byteLength");
    const json::Value* digestText = weights->find("sha256");
    if (!weightsLength.has_value() || digestText == nullptr) {
        return err(problem(fileName, malformed, "weights record is incomplete"));
    }
    auto digestString = digestText->asString();
    if (!digestString.has_value()) {
        return err(problem(fileName, malformed, "weights sha256 is not a string"));
    }
    auto digest = evidence::digestFromHex(*digestString);
    if (!digest.has_value()) {
        return err(problem(fileName, malformed, "weights sha256 is not 64 hex digits"));
    }
    loaded->weightsByteLength_ = *weightsLength;
    loaded->weightsDigest_ = *digest;

    const json::Value* layers = root.find("layers");
    if (layers == nullptr || layers->kind() != json::Value::Kind::Array) {
        return err(problem(fileName, malformed, "package has no layers array"));
    }
    for (const json::Value& entry : layers->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(problem(fileName, malformed, "a layer is not an object"));
        }
        const json::Value* kindText = entry.find("kind");
        const json::Value* activationText = entry.find("activation");
        if (kindText == nullptr || activationText == nullptr) {
            return err(problem(fileName, malformed, "a layer is missing kind or activation"));
        }
        auto kindString = kindText->asString();
        auto activationString = activationText->asString();
        if (!kindString.has_value() || !activationString.has_value()) {
            return err(problem(fileName, malformed, "a layer's kind or activation is not a string"));
        }
        const auto kind = layerKindFromWire(*kindString);
        const auto activation = activationFromWire(*activationString);
        if (!kind.has_value() || !activation.has_value()) {
            return err(problem(fileName, malformed,
                               std::format("unknown layer kind '{}' or activation '{}'",
                                           *kindString, *activationString)));
        }

        const auto inLength = readUInt32(entry, "inLength");
        const auto inChannels = readUInt32(entry, "inChannels");
        const auto outLen = readUInt32(entry, "outLength");
        const auto outChannels = readUInt32(entry, "outChannels");
        if (!inLength.has_value() || !inChannels.has_value() || !outLen.has_value() ||
            !outChannels.has_value()) {
            return err(problem(fileName, malformed,
                               "a layer is missing a dimension, or one does not fit in 32 bits"));
        }

        // kernelSize and stride are written only for windowed layers, so their absence is
        // well-formed and means zero - which is what the schema requires elsewhere.
        const auto kernelSize = readUInt32(entry, "kernelSize").value_or(0u);
        const auto stride = readUInt32(entry, "stride").value_or(0u);

        const auto weightsRef = readTensor(entry, "weights");
        const auto biasRef = readTensor(entry, "bias");
        if (!weightsRef.has_value() || !biasRef.has_value()) {
            return err(problem(fileName, malformed, "a layer's tensor record is malformed"));
        }

        loaded->layers_.push_back(
            ml::LayerDesc{.kind = *kind,
                          .activation = *activation,
                          .inLength = *inLength,
                          .inChannels = *inChannels,
                          .outLength = *outLen,
                          .outChannels = *outChannels,
                          .kernelSize = kernelSize,
                          .stride = stride,
                          .weights = *weightsRef,
                          .bias = *biasRef});
    }

    const json::Value* goldens = root.find("goldens");
    if (goldens == nullptr || goldens->kind() != json::Value::Kind::Array) {
        return err(problem(fileName, malformed, "package has no goldens array"));
    }
    for (const json::Value& entry : goldens->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(problem(fileName, malformed, "a golden is not an object"));
        }
        auto inputBits = readBits(entry, "inputBits");
        auto outputBits = readBits(entry, "expectedOutputBits");
        if (!inputBits.has_value() || !outputBits.has_value()) {
            return err(problem(fileName, malformed, "a golden's bit arrays are malformed"));
        }
        loaded->goldenInputs_.push_back(std::move(*inputBits));
        loaded->goldenOutputs_.push_back(std::move(*outputBits));
    }

    // Built only once the vectors are final, so no span points into storage that later grows.
    loaded->goldenViews_.reserve(loaded->goldenInputs_.size());
    for (std::size_t i = 0; i < loaded->goldenInputs_.size(); ++i) {
        loaded->goldenViews_.push_back(
            ml::GoldenVector{.inputBits = loaded->goldenInputs_[i],
                             .expectedOutputBits = loaded->goldenOutputs_[i]});
    }

    if (auto valid = loaded->view().validate(); !valid.has_value()) {
        return err(problem(fileName, invalid,
                           std::format("package does not validate: {}",
                                       ml::describe(valid.error()))));
    }

    return loaded;
}

}  // namespace mdux::tools::ml
