/**
 * @file Safetensors.cpp
 * @brief Implementation of the hand-written safetensors reader.
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Every bound is checked before it is used. The arithmetic on offsets is done in `std::uint64_t`
 * with explicit overflow guards rather than by adding and hoping - a header can declare any
 * numbers at all, and `begin + length` wrapping is the classic way a range check passes and the
 * read still runs off the end.
 */
module;

module mdux.tools.ml.safetensors;

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.tools.cli;

namespace mdux::tools::ml {

using mdux::core::err;
namespace json = mdux::evidence::json;

namespace {

/// v1 caps rank at 3, matching mdux.ml.schema's maxTensorRank.
constexpr std::size_t maxSupportedRank = 3;

[[nodiscard]] cli::Diagnostic problem(std::string_view fileName, std::string code,
                                      std::string message, std::string fixHint = {}) {
    return cli::Diagnostic{.file = std::string{fileName},
                           .line = 0,
                           .column = 0,
                           .code = std::move(code),
                           .severity = cli::Severity::Error,
                           .message = std::move(message),
                           .fixHint = std::move(fixHint)};
}

}  // namespace

std::uint64_t dtypeByteWidth(Dtype dtype) noexcept {
    switch (dtype) {
        case Dtype::Bool:
        case Dtype::U8:
        case Dtype::I8:   return 1;
        case Dtype::U16:
        case Dtype::I16:
        case Dtype::F16:
        case Dtype::BF16: return 2;
        case Dtype::U32:
        case Dtype::I32:
        case Dtype::F32:  return 4;
        case Dtype::U64:
        case Dtype::I64:
        case Dtype::F64:  return 8;
    }
    return 0;
}

std::optional<Dtype> dtypeFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < dtypeWireValues.size(); ++i) {
        if (dtypeWireValues[i] == wire) {
            return static_cast<Dtype>(i);
        }
    }
    return std::nullopt;
}

std::uint64_t TensorEntry::elementCount() const noexcept {
    std::uint64_t count = 1;
    for (std::uint64_t dimension : shape) {
        count *= dimension;
    }
    return count;
}

const TensorEntry* SafetensorsFile::find(std::string_view name) const noexcept {
    for (const TensorEntry& tensor : tensors) {
        if (tensor.name == name) {
            return &tensor;
        }
    }
    return nullptr;
}

mdux::core::Result<SafetensorsFile, cli::Diagnostic> parseSafetensors(
    std::span<const std::byte> bytes, std::string_view fileName) {
    // 1. The 8-byte little-endian header length.
    constexpr std::size_t headerLengthSize = 8;
    if (bytes.size() < headerLengthSize) {
        return err(problem(fileName, "mdux.ml.safetensors.malformedTruncatedHeaderLength",
                           std::format("file is {} bytes; a safetensors file starts with an "
                                       "8-byte header length",
                                       bytes.size()),
                           "check the file downloaded completely"));
    }

    std::uint64_t headerLength = 0;
    for (std::size_t i = 0; i < headerLengthSize; ++i) {
        headerLength |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i]))
                        << (8 * i);
    }

    // Guarded against overflow rather than written as `8 + headerLength > size`, which wraps for a
    // header length near 2^64 and would let a hostile file past this check.
    if (headerLength > bytes.size() - headerLengthSize) {
        return err(problem(fileName, "mdux.ml.safetensors.malformedHeaderOutOfBounds",
                           std::format("header claims {} bytes but only {} remain after the "
                                       "length prefix",
                                       headerLength, bytes.size() - headerLengthSize),
                           "the file is truncated or is not safetensors"));
    }

    const std::size_t dataStart = headerLengthSize + static_cast<std::size_t>(headerLength);
    std::string_view headerText{reinterpret_cast<const char*>(bytes.data() + headerLengthSize),
                                static_cast<std::size_t>(headerLength)};

    // Real exporters pad the header with spaces (and occasionally NULs) so the data section starts
    // on an alignment boundary. The padding is part of the declared header length, and the
    // canonical JSON reader is strict about trailing bytes, so it is trimmed here rather than
    // being tolerated further in. Only trailing whitespace is removed; anything else stays and is
    // reported as malformed JSON, which is what it is.
    while (!headerText.empty() && (headerText.back() == ' ' || headerText.back() == '\0' ||
                                   headerText.back() == '\n' || headerText.back() == '\r' ||
                                   headerText.back() == '\t')) {
        headerText.remove_suffix(1);
    }

    auto parsed = json::parse(headerText);
    if (!parsed.has_value()) {
        return err(problem(fileName, "mdux.ml.safetensors.malformedHeaderJson",
                           std::format("header is not valid JSON: {}",
                                       json::describe(parsed.error().code)),
                           "re-export the weights"));
    }
    const json::Value& header = *parsed;
    if (header.kind() != json::Value::Kind::Object) {
        return err(problem(fileName, "mdux.ml.safetensors.malformedHeaderNotObject",
                           "header JSON is not an object"));
    }

    SafetensorsFile file;
    const std::uint64_t dataBytes = bytes.size() - dataStart;

    for (const json::Member& member : header.members()) {
        // __metadata__ is the one reserved key, and it is a flat string map rather than a tensor.
        if (member.key == "__metadata__") {
            if (member.value.kind() == json::Value::Kind::Object) {
                for (const json::Member& entry : member.value.members()) {
                    auto text = entry.value.asString();
                    if (text.has_value()) {
                        file.metadata.emplace_back(entry.key, std::string{*text});
                    }
                }
            }
            continue;
        }

        const json::Value& descriptor = member.value;
        if (descriptor.kind() != json::Value::Kind::Object) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedTensorNotObject",
                               std::format("tensor '{}' is not described by an object",
                                           member.key)));
        }

        const json::Value* dtypeValue = descriptor.find("dtype");
        const json::Value* shapeValue = descriptor.find("shape");
        const json::Value* offsetsValue = descriptor.find("data_offsets");
        if (dtypeValue == nullptr || shapeValue == nullptr || offsetsValue == nullptr) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedTensorFields",
                               std::format("tensor '{}' is missing dtype, shape or data_offsets",
                                           member.key)));
        }

        auto dtypeText = dtypeValue->asString();
        if (!dtypeText.has_value()) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedTensorFields",
                               std::format("tensor '{}' has a non-string dtype", member.key)));
        }
        const std::optional<Dtype> dtype = dtypeFromWire(*dtypeText);
        if (!dtype.has_value()) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedUnknownDtype",
                               std::format("tensor '{}' declares unknown dtype '{}'", member.key,
                                           *dtypeText)));
        }
        // v1 scope, not a format problem - see the module comment on code prefixes.
        if (*dtype != Dtype::F32) {
            return err(problem(
                fileName, "mdux.ml.safetensors.unsupportedDtype",
                std::format("tensor '{}' is {}; MduX v1 inference is f32 only", member.key,
                            *dtypeText),
                "convert the checkpoint to f32, or raise an ADR extending ADR-008's v1 scope"));
        }

        if (shapeValue->kind() != json::Value::Kind::Array) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedTensorFields",
                               std::format("tensor '{}' has a non-array shape", member.key)));
        }
        const std::span<const json::Value> dimensions = shapeValue->elements();
        if (dimensions.size() > maxSupportedRank) {
            return err(problem(
                fileName, "mdux.ml.safetensors.unsupportedRank",
                std::format("tensor '{}' has rank {}; MduX v1 supports up to {}", member.key,
                            dimensions.size(), maxSupportedRank),
                "the v1 kernel set is 1-D; a higher-rank model needs a follow-up ADR"));
        }

        TensorEntry entry;
        entry.name = member.key;
        entry.dtype = *dtype;
        for (const json::Value& dimension : dimensions) {
            auto extent = dimension.asUInt();
            if (!extent.has_value()) {
                return err(problem(fileName, "mdux.ml.safetensors.malformedShape",
                                   std::format("tensor '{}' has a non-integer dimension",
                                               member.key)));
            }
            entry.shape.push_back(*extent);
        }

        if (offsetsValue->kind() != json::Value::Kind::Array ||
            offsetsValue->elements().size() != 2) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedOffsets",
                               std::format("tensor '{}' data_offsets is not a pair", member.key)));
        }
        auto begin = offsetsValue->elements()[0].asUInt();
        auto end = offsetsValue->elements()[1].asUInt();
        if (!begin.has_value() || !end.has_value()) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedOffsets",
                               std::format("tensor '{}' data_offsets are not integers",
                                           member.key)));
        }
        if (*end < *begin) {
            return err(problem(fileName, "mdux.ml.safetensors.malformedOffsets",
                               std::format("tensor '{}' ends ({}) before it begins ({})",
                                           member.key, *end, *begin)));
        }
        // Both compared against the data-section length rather than summed, so nothing here can
        // wrap.
        if (*begin > dataBytes || *end > dataBytes) {
            return err(problem(
                fileName, "mdux.ml.safetensors.malformedRangeOutOfBounds",
                std::format("tensor '{}' occupies [{}, {}) but the data section is {} bytes",
                            member.key, *begin, *end, dataBytes),
                "the file is truncated"));
        }

        entry.byteLength = *end - *begin;
        entry.byteOffset = static_cast<std::uint64_t>(dataStart) + *begin;

        // Overflow-checked, for the same reason the offset arithmetic above is: a header can
        // declare any extents it likes, and three of them multiply well past 2^64. A wrapped
        // product can land exactly on the declared byte length, which would let a tensor whose
        // shape describes gigabytes validate against a range of a few bytes.
        constexpr std::uint64_t saturated = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t elements = 1;
        bool overflowed = false;
        for (std::uint64_t extent : entry.shape) {
            if (extent != 0 && elements > saturated / extent) {
                overflowed = true;
                break;
            }
            elements *= extent;
        }
        const std::uint64_t width = dtypeByteWidth(entry.dtype);
        if (!overflowed && width != 0 && elements > saturated / width) {
            overflowed = true;
        }
        if (overflowed) {
            return err(problem(
                fileName, "mdux.ml.safetensors.malformedShapeByteLength",
                std::format("tensor '{}' declares a shape whose size overflows", member.key),
                "the shape cannot describe a real tensor; re-export the weights"));
        }

        const std::uint64_t expected = elements * width;
        if (expected != entry.byteLength) {
            return err(problem(
                fileName, "mdux.ml.safetensors.malformedShapeByteLength",
                std::format("tensor '{}' declares a shape needing {} bytes but reserves {}",
                            member.key, expected, entry.byteLength),
                "the shape and the byte range disagree; re-export the weights"));
        }

        file.tensors.push_back(std::move(entry));
    }

    // Fixed order, so the baker's output does not depend on the exporter's key ordering. See the
    // SafetensorsFile comment.
    std::ranges::sort(file.tensors,
                      [](const TensorEntry& a, const TensorEntry& b) { return a.name < b.name; });
    std::ranges::sort(file.metadata, [](const auto& a, const auto& b) { return a.first < b.first; });

    // Overlap is checked after sorting by name, so it walks a copy ordered by offset instead.
    // Two tensors sharing bytes means the file is malformed - unlike weight tying in a *package*,
    // which is legitimate and is why mdux.ml.schema deliberately does not check this.
    std::vector<const TensorEntry*> byOffset;
    byOffset.reserve(file.tensors.size());
    for (const TensorEntry& tensor : file.tensors) {
        byOffset.push_back(&tensor);
    }
    std::ranges::sort(byOffset, [](const TensorEntry* a, const TensorEntry* b) {
        return a->byteOffset < b->byteOffset;
    });
    for (std::size_t i = 1; i < byOffset.size(); ++i) {
        const TensorEntry& previous = *byOffset[i - 1];
        const TensorEntry& current = *byOffset[i];
        if (previous.byteOffset + previous.byteLength > current.byteOffset) {
            return err(problem(
                fileName, "mdux.ml.safetensors.malformedOverlappingTensors",
                std::format("tensors '{}' and '{}' share bytes", previous.name, current.name),
                "each tensor must occupy its own range"));
        }
    }

    return file;
}

mdux::core::Result<SafetensorsFile, cli::Diagnostic> readSafetensors(
    const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{std::format("cannot open '{}'", path.string())};
    }
    std::vector<char> contents{std::istreambuf_iterator<char>{stream},
                               std::istreambuf_iterator<char>{}};
    return parseSafetensors(std::as_bytes(std::span{contents}), path.filename().string());
}

}  // namespace mdux::tools::ml
