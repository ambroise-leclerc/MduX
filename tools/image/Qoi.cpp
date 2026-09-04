/**
 * @file Qoi.cpp
 * @brief Dependency-free QOI decoder implementation.
 *
 * Implements the public QOI specification directly. The format decoder remains in the host-tools
 * target and is never linked into MduXCore or MduX.
 */
module;

module mdux.tools.qoi;

import std;
import mdux.core.result;

namespace mdux::tools::qoi {

using mdux::core::err;

namespace {

constexpr std::size_t                 headerSize = 14;
constexpr std::array<std::uint8_t, 8> endMarker{0, 0, 0, 0, 0, 0, 0, 1};
constexpr std::uint8_t                opRgb  = 0xfe;
constexpr std::uint8_t                opRgba = 0xff;

struct Pixel {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{0};
};

[[nodiscard]] constexpr std::size_t hash(Pixel pixel) noexcept {
    return (static_cast<std::size_t>(pixel.r) * 3u + static_cast<std::size_t>(pixel.g) * 5u + static_cast<std::size_t>(pixel.b) * 7u
            + static_cast<std::size_t>(pixel.a) * 11u)
           % 64u;
}

[[nodiscard]] constexpr std::uint8_t addWrapped(std::uint8_t value, int difference) noexcept {
    return static_cast<std::uint8_t>(static_cast<unsigned>(value) + static_cast<unsigned>(difference));
}

[[nodiscard]] constexpr std::uint32_t readBe32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24u) | (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16u)
           | (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8u) | std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

void append(std::vector<std::byte>& rgba, Pixel pixel) {
    rgba.push_back(static_cast<std::byte>(pixel.r));
    rgba.push_back(static_cast<std::byte>(pixel.g));
    rgba.push_back(static_cast<std::byte>(pixel.b));
    rgba.push_back(static_cast<std::byte>(pixel.a));
}

}  // namespace

std::string_view describe(DecodeError error) noexcept {
    switch (error) {
        case DecodeError::Truncated:
            return "QOI stream is shorter than its header and end marker";
        case DecodeError::BadMagic:
            return "QOI magic is not 'qoif'";
        case DecodeError::ZeroExtent:
            return "QOI width and height must be non-zero";
        case DecodeError::UnsupportedChannels:
            return "QOI channels must be 3 or 4";
        case DecodeError::UnsupportedColorSpace:
            return "QOI colorspace must be 0 or 1";
        case DecodeError::SizeOverflow:
            return "QOI dimensions overflow the decoded RGBA size";
        case DecodeError::BadEndMarker:
            return "QOI end marker is missing or malformed";
        case DecodeError::TruncatedChunk:
            return "QOI pixel chunk is truncated";
        case DecodeError::RunExceedsImage:
            return "QOI run exceeds the declared pixel count";
        case DecodeError::TrailingChunkData:
            return "QOI has chunk bytes after the declared pixels";
    }
    return "unknown QOI decode error";
}

mdux::core::Result<Image, DecodeError> decode(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < headerSize + endMarker.size())
        return err(DecodeError::Truncated);
    if (std::to_integer<char>(bytes[0]) != 'q' || std::to_integer<char>(bytes[1]) != 'o' || std::to_integer<char>(bytes[2]) != 'i'
        || std::to_integer<char>(bytes[3]) != 'f') {
        return err(DecodeError::BadMagic);
    }
    const std::uint32_t width      = readBe32(bytes, 4);
    const std::uint32_t height     = readBe32(bytes, 8);
    const std::uint8_t  channels   = std::to_integer<std::uint8_t>(bytes[12]);
    const std::uint8_t  colorSpace = std::to_integer<std::uint8_t>(bytes[13]);
    if (width == 0 || height == 0)
        return err(DecodeError::ZeroExtent);
    if (channels != 3 && channels != 4)
        return err(DecodeError::UnsupportedChannels);
    if (colorSpace > 1)
        return err(DecodeError::UnsupportedColorSpace);

    const std::uint64_t pixelCount64 = static_cast<std::uint64_t>(width) * height;
    if (pixelCount64 > maxDecodedPixels || pixelCount64 > std::numeric_limits<std::size_t>::max() / 4u)
        return err(DecodeError::SizeOverflow);
    const std::size_t pixelCount = static_cast<std::size_t>(pixelCount64);
    const std::size_t chunksEnd  = bytes.size() - endMarker.size();
    for (std::size_t index = 0; index < endMarker.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[chunksEnd + index]) != endMarker[index]) {
            return err(DecodeError::BadEndMarker);
        }
    }

    Image image{.width = width, .height = height, .sourceChannels = channels, .colorSpace = colorSpace, .rgba = {}};
    try {
        image.rgba.reserve(pixelCount * 4u);
    } catch (...) {
        return err(DecodeError::SizeOverflow);
    }
    std::array<Pixel, 64> seen{};
    Pixel                 previous{.r = 0, .g = 0, .b = 0, .a = 255};
    std::size_t           cursor   = headerSize;
    std::size_t           produced = 0;

    const auto need = [&](std::size_t count) noexcept {
        return count <= chunksEnd - std::min(cursor, chunksEnd);
    };
    try {
        while (produced < pixelCount) {
            if (!need(1))
                return err(DecodeError::TruncatedChunk);
            const std::uint8_t first   = std::to_integer<std::uint8_t>(bytes[cursor++]);
            Pixel              current = previous;
            std::size_t        run     = 1;

            if (first == opRgb) {
                if (!need(3))
                    return err(DecodeError::TruncatedChunk);
                current.r = std::to_integer<std::uint8_t>(bytes[cursor++]);
                current.g = std::to_integer<std::uint8_t>(bytes[cursor++]);
                current.b = std::to_integer<std::uint8_t>(bytes[cursor++]);
            } else if (first == opRgba) {
                if (!need(4))
                    return err(DecodeError::TruncatedChunk);
                current.r = std::to_integer<std::uint8_t>(bytes[cursor++]);
                current.g = std::to_integer<std::uint8_t>(bytes[cursor++]);
                current.b = std::to_integer<std::uint8_t>(bytes[cursor++]);
                current.a = std::to_integer<std::uint8_t>(bytes[cursor++]);
            } else {
                switch (first & 0xc0u) {
                    case 0x00u:
                        current = seen[first & 0x3fu];
                        break;
                    case 0x40u:
                        current.r = addWrapped(current.r, static_cast<int>((first >> 4u) & 0x03u) - 2);
                        current.g = addWrapped(current.g, static_cast<int>((first >> 2u) & 0x03u) - 2);
                        current.b = addWrapped(current.b, static_cast<int>(first & 0x03u) - 2);
                        break;
                    case 0x80u: {
                        if (!need(1))
                            return err(DecodeError::TruncatedChunk);
                        const std::uint8_t second = std::to_integer<std::uint8_t>(bytes[cursor++]);
                        const int          dg     = static_cast<int>(first & 0x3fu) - 32;
                        const int          dr     = dg + static_cast<int>((second >> 4u) & 0x0fu) - 8;
                        const int          db     = dg + static_cast<int>(second & 0x0fu) - 8;
                        current.r                 = addWrapped(current.r, dr);
                        current.g                 = addWrapped(current.g, dg);
                        current.b                 = addWrapped(current.b, db);
                        break;
                    }
                    case 0xc0u:
                        run = static_cast<std::size_t>(first & 0x3fu) + 1u;
                        break;
                }
            }

            if (run > pixelCount - produced)
                return err(DecodeError::RunExceedsImage);
            for (std::size_t index = 0; index < run; ++index) {
                append(image.rgba, current);
                seen[hash(current)] = current;
                ++produced;
            }
            previous = current;
        }
    } catch (...) {
        return err(DecodeError::SizeOverflow);
    }
    if (cursor != chunksEnd)
        return err(DecodeError::TrailingChunkData);
    return image;
}

}  // namespace mdux::tools::qoi
