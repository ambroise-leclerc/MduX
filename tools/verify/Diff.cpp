/**
 * @file Diff.cpp
 * @brief Implementation of the failure diff image and its PNG encoder.
 */
module;

module mdux.tools.verify.diff;

import std;
import mdux.core.units;
import mdux.medui.schema;

namespace mdux::tools::verify {

namespace {

using mdux::core::ColorRgba8;

[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    // Built on first use rather than written out: 256 constants in a source file are 256 chances to
    // transcribe one wrongly, and nothing downstream would report it as anything but a corrupt file.
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> built{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = ((value & 1U) != 0U) ? (0xEDB88320U ^ (value >> 1U)) : (value >> 1U);
            }
            built[index] = value;
        }
        return built;
    }();

    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc = table[(crc ^ static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(byte))) & 0xFFU] ^ (crc >> 8U);
    }
    return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] std::uint32_t adler32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t low  = 1;
    std::uint32_t high = 0;
    for (const std::byte byte : bytes) {
        low  = (low + static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(byte))) % 65521U;
        high = (high + low) % 65521U;
    }
    return (high << 16U) | low;
}

void appendBigEndian(std::vector<std::byte>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::byte>((value >> static_cast<std::uint32_t>(shift)) & 0xFFU));
    }
}

/// One PNG chunk: length, type, data, and the CRC over type and data (never over the length).
void appendChunk(std::vector<std::byte>& out, std::string_view type, std::span<const std::byte> data) {
    appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcFrom = out.size();
    for (const char character : type) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    out.insert(out.end(), data.begin(), data.end());
    appendBigEndian(out, crc32(std::span{out}.subspan(crcFrom)));
}

/// Wraps `raw` in a zlib stream whose deflate blocks are all stored. See the module comment.
[[nodiscard]] std::vector<std::byte> storedZlib(std::span<const std::byte> raw) {
    std::vector<std::byte> out;
    // 0x78 0x01: deflate, 32K window, no preset dictionary, "fastest" compression level. The pair is
    // a multiple of 31, which is the check zlib readers apply to the two header bytes.
    out.push_back(std::byte{0x78});
    out.push_back(std::byte{0x01});

    constexpr std::size_t blockLimit = 65535;
    std::size_t           offset     = 0;
    do {
        const std::size_t take  = std::min(blockLimit, raw.size() - offset);
        const bool        final = offset + take >= raw.size();
        out.push_back(static_cast<std::byte>(final ? 0x01 : 0x00));
        const auto length = static_cast<std::uint16_t>(take);
        // LEN then its one's complement, both little-endian, which is how a decoder proves it read
        // the length rather than a byte of the previous block.
        out.push_back(static_cast<std::byte>(length & 0xFFU));
        out.push_back(static_cast<std::byte>((length >> 8U) & 0xFFU));
        const auto complement = static_cast<std::uint16_t>(~static_cast<unsigned>(length));
        out.push_back(static_cast<std::byte>(complement & 0xFFU));
        out.push_back(static_cast<std::byte>((complement >> 8U) & 0xFFU));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + take));
        offset += take;
        // `do`/`while` rather than `while`: zero bytes still needs one final empty stored block, or
        // the stream ends without one and a decoder reports truncation.
    } while (offset < raw.size());

    appendBigEndian(out, adler32(raw));
    return out;
}

/// Paints one pixel, ignoring coordinates outside the frame so a mark may hang off an edge.
void plot(std::vector<ColorRgba8>& pixels, std::uint32_t width, std::uint32_t height, std::int64_t x, std::int64_t y, ColorRgba8 colour) {
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width) || y >= static_cast<std::int64_t>(height)) {
        return;
    }
    pixels[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = colour;
}

/// A hollow rectangle `outlineThickness` pixels thick, drawn inward from `rect`'s edges.
void outline(std::vector<ColorRgba8>& pixels, std::uint32_t width, std::uint32_t height, const mdux::medui::NodeRect& rect, ColorRgba8 colour) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const auto left   = static_cast<std::int64_t>(rect.x);
    const auto top    = static_cast<std::int64_t>(rect.y);
    const auto right  = left + rect.width - 1;
    const auto bottom = top + rect.height - 1;
    // Clamped so a rectangle thinner than the stroke is drawn solid rather than twice, which would
    // be invisible either way but would make the loops depend on the rectangle's size.
    const std::int64_t stroke = std::min<std::int64_t>(outlineThickness, std::min(rect.width, rect.height));
    for (std::int64_t y = top; y <= bottom; ++y) {
        const bool horizontalBand = y < top + stroke || y > bottom - stroke;
        for (std::int64_t x = left; x <= right; ++x) {
            if (horizontalBand || x < left + stroke || x > right - stroke) {
                plot(pixels, width, height, x, y, colour);
            }
        }
    }
}

}  // namespace

std::vector<ColorRgba8> composeDiff(std::span<const ColorRgba8> frame, std::uint32_t width, std::uint32_t height, std::span<const DiffMark> marks) {
    if (width == 0 || height == 0 || frame.size() != static_cast<std::size_t>(width) * height) {
        return {};
    }

    std::vector<ColorRgba8> pixels;
    pixels.reserve(frame.size());
    for (const ColorRgba8 pixel : frame) {
        // Integer scaling, so the dimmed frame is a function of the readback alone and carries no
        // rounding a floating-point path would make platform-dependent. Alpha is left as it was:
        // dimming it would make the image itself translucent rather than the content dark.
        pixels.push_back(ColorRgba8{.r = static_cast<std::uint8_t>(pixel.r * dimNumerator / 256U),
                                    .g = static_cast<std::uint8_t>(pixel.g * dimNumerator / 256U),
                                    .b = static_cast<std::uint8_t>(pixel.b * dimNumerator / 256U),
                                    .a = pixel.a});
    }

    // Found after expected, so where the two coincide the reader sees the found box - which is the
    // one that says what the frame actually holds.
    for (const DiffMark& mark : marks) {
        outline(pixels, width, height, mark.expected, expectedOutline);
    }
    for (const DiffMark& mark : marks) {
        if (mark.foundValid) {
            outline(pixels, width, height, mark.found, foundOutline);
        }
    }
    return pixels;
}

std::vector<std::byte> encodePng(std::span<const ColorRgba8> pixels, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || pixels.size() != static_cast<std::size_t>(width) * height) {
        return {};
    }

    std::vector<std::byte> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4));
    for (std::uint32_t y = 0; y < height; ++y) {
        // Filter type 0 (None) on every row. A predictor would shrink a compressed stream and does
        // nothing for a stored one, so it would be cost with no benefit.
        raw.push_back(std::byte{0});
        for (std::uint32_t x = 0; x < width; ++x) {
            const ColorRgba8 pixel = pixels[static_cast<std::size_t>(y) * width + x];
            raw.push_back(static_cast<std::byte>(pixel.r));
            raw.push_back(static_cast<std::byte>(pixel.g));
            raw.push_back(static_cast<std::byte>(pixel.b));
            raw.push_back(static_cast<std::byte>(pixel.a));
        }
    }

    std::vector<std::byte> out{std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
                               std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};

    std::vector<std::byte> header;
    appendBigEndian(header, width);
    appendBigEndian(header, height);
    header.push_back(std::byte{8});  // bit depth
    header.push_back(std::byte{6});  // colour type: truecolour with alpha
    header.push_back(std::byte{0});  // compression method: deflate, the only one PNG defines
    header.push_back(std::byte{0});  // filter method
    header.push_back(std::byte{0});  // interlace: none
    appendChunk(out, "IHDR", header);
    appendChunk(out, "IDAT", storedZlib(raw));
    appendChunk(out, "IEND", {});
    return out;
}

}  // namespace mdux::tools::verify
