/**
 * @file DiffTests.cpp
 * @brief The failure diff image: what it draws, and that what it writes is a PNG.
 *
 * No GPU here. `composeDiff()` takes an array of pixels and `encodePng()` returns bytes, so both are
 * checkable from a framebuffer a test painted itself - the same property ADR-014 decision 1 gives
 * the governed checks, for the same reason.
 *
 * The encoder is checked by **decoding**, not by comparing against a stored byte string. A golden
 * file would pass for a stream that is self-consistently wrong: the point of writing a PNG rather
 * than a private format is that other software reads it, so the assertions here are the ones other
 * software makes - signature, per-chunk CRC, a zlib stream that inflates, and a raster whose filter
 * bytes and pixel values are the ones that went in.
 */
import std;
import speclab;
import mdux.core.units;
import mdux.medui.schema;
import mdux.tools.verify.diff;

#include "../framework/SpecLabBridge.hpp"

namespace {
namespace vu = mdux::tools::verify;

using mdux::core::ColorRgba8;

constexpr ColorRgba8 content{.r = 200, .g = 100, .b = 50, .a = 255};
constexpr ColorRgba8 ground{.r = 0, .g = 0, .b = 0, .a = 255};

[[nodiscard]] std::vector<ColorRgba8> flatFrame(std::uint32_t width, std::uint32_t height, ColorRgba8 colour) {
    return std::vector<ColorRgba8>(static_cast<std::size_t>(width) * height, colour);
}

[[nodiscard]] ColorRgba8 at(std::span<const ColorRgba8> pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    return pixels[static_cast<std::size_t>(y) * width + x];
}

/// A minimal inflater for stored deflate blocks - the only kind `encodePng()` emits.
///
/// Deliberately not a general one: it decodes exactly what this project writes, and a block of any
/// other type makes it return nothing rather than guess. That is what makes it an assertion about
/// the encoder rather than a second implementation of deflate.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> inflateStored(std::span<const std::uint8_t> zlib) {
    if (zlib.size() < 6 || (static_cast<unsigned>(zlib[0]) * 256U + zlib[1]) % 31U != 0U) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    std::size_t               index = 2;
    bool                      final = false;
    while (!final) {
        if (index + 5 > zlib.size() - 4) {
            return std::nullopt;
        }
        const std::uint8_t header = zlib[index];
        // Bit 0 is BFINAL; bits 1-2 are BTYPE, and 00 is a stored block.
        if ((header & 0x06U) != 0U) {
            return std::nullopt;
        }
        final                     = (header & 0x01U) != 0U;
        const std::uint16_t length = static_cast<std::uint16_t>(zlib[index + 1] | (zlib[index + 2] << 8U));
        const std::uint16_t nlength = static_cast<std::uint16_t>(zlib[index + 3] | (zlib[index + 4] << 8U));
        if (static_cast<std::uint16_t>(~static_cast<unsigned>(length)) != nlength) {
            return std::nullopt;
        }
        index += 5;
        if (index + length > zlib.size() - 4) {
            return std::nullopt;
        }
        out.insert(out.end(), zlib.begin() + static_cast<std::ptrdiff_t>(index), zlib.begin() + static_cast<std::ptrdiff_t>(index + length));
        index += length;
    }
    // The trailing four bytes are the Adler-32 of the inflated data, checked by the caller.
    return out;
}

struct Chunk {
    std::string               type;
    std::vector<std::uint8_t> body;
};

/// Walks a PNG, verifying the signature and every chunk's CRC, and returns the chunks in order.
[[nodiscard]] std::optional<std::vector<Chunk>> readPng(std::span<const std::byte> bytes) {
    static constexpr std::array<std::uint8_t, 8> signature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < signature.size()) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[index]) != signature[index]) {
            return std::nullopt;
        }
    }

    const auto crc = [](std::span<const std::uint8_t> data) {
        std::uint32_t value = 0xFFFFFFFFU;
        for (const std::uint8_t byte : data) {
            value ^= byte;
            for (int bit = 0; bit < 8; ++bit) {
                value = ((value & 1U) != 0U) ? (0xEDB88320U ^ (value >> 1U)) : (value >> 1U);
            }
        }
        return value ^ 0xFFFFFFFFU;
    };
    const auto beAt = [&bytes](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
        }
        return value;
    };

    std::vector<Chunk> chunks;
    std::size_t        offset = signature.size();
    while (offset + 12 <= bytes.size()) {
        const std::uint32_t length = beAt(offset);
        if (offset + 12 + length > bytes.size()) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> typeAndBody;
        typeAndBody.reserve(4 + length);
        for (std::size_t index = 0; index < 4 + length; ++index) {
            typeAndBody.push_back(std::to_integer<std::uint8_t>(bytes[offset + 4 + index]));
        }
        if (crc(typeAndBody) != beAt(offset + 8 + length)) {
            return std::nullopt;
        }
        chunks.push_back(Chunk{.type = std::string{typeAndBody.begin(), typeAndBody.begin() + 4},
                               .body = std::vector<std::uint8_t>{typeAndBody.begin() + 4, typeAndBody.end()}});
        offset += 12 + length;
    }
    return offset == bytes.size() ? std::optional{chunks} : std::nullopt;
}

const mdux::spec::Register dimsAndOutlines{"A diff image dims the frame and outlines expected and found", "evidence-unit", [] {
    return speclab::Test("verify-ui-diff-outlines")
        .Given("a flat frame and one failed obligation whose found box is not its expected box", [] {})
        .When("the diff is composed", [] {})
        .Then("the frame is dim, the expected box is magenta and the found box is cyan",
              [] {
                  constexpr std::uint32_t width  = 32;
                  constexpr std::uint32_t height = 32;
                  const std::array        marks{vu::DiffMark{.nodeId     = "readout",
                                                             .check      = "Bounds",
                                                             .expected   = mdux::medui::NodeRect{.x = 4, .y = 4, .width = 12, .height = 12},
                                                             .found      = mdux::medui::NodeRect{.x = 20, .y = 20, .width = 8, .height = 8},
                                                             .foundValid = true}};
                  const auto composed = vu::composeDiff(flatFrame(width, height, content), width, height, marks);

                  mdux::spec::Checks assertions;
                  assertions.expect(composed.size() == static_cast<std::size_t>(width) * height, "one pixel in, one pixel out");
                  // 200 * 96 / 256 == 75. Asserted as the arithmetic rather than as "darker", so a
                  // change to `dimNumerator` is a change somebody has to make here too.
                  assertions.expect(at(composed, width, 0, 0) == ColorRgba8{.r = 75, .g = 37, .b = 18, .a = 255},
                                    "an untouched pixel is the frame's colour scaled by the dim numerator");
                  assertions.expect(at(composed, width, 4, 4) == vu::expectedOutline, "the expected rectangle's corner is outlined");
                  assertions.expect(at(composed, width, 15, 15) == vu::expectedOutline, "and so is its far corner, inclusive of the last pixel");
                  assertions.expect(at(composed, width, 20, 20) == vu::foundOutline, "the found rectangle is outlined in the other colour");
                  // Two pixels in from the edge on both axes, which is `outlineThickness` - so this
                  // is the first interior pixel, and it must still show the frame.
                  assertions.expect(at(composed, width, 6, 6) == ColorRgba8{.r = 75, .g = 37, .b = 18, .a = 255},
                                    "the outline is hollow, so the content inside stays visible");
                  assertions.raise();
              })
        .Execute();
}};

const mdux::spec::Register noFoundNoCyan{"An outcome with no found rectangle draws only the expected one", "evidence-unit", [] {
    return speclab::Test("verify-ui-diff-found-optional")
        .Given("a failure that reports nothing painted, so there is no found box", [] {})
        .When("the diff is composed", [] {})
        .Then("no cyan is drawn anywhere",
              [] {
                  constexpr std::uint32_t width  = 16;
                  constexpr std::uint32_t height = 16;
                  const std::array        marks{vu::DiffMark{.nodeId     = "readout",
                                                             .check      = "Bounds",
                                                             .expected   = mdux::medui::NodeRect{.x = 0, .y = 0, .width = 8, .height = 8},
                                                             .found      = mdux::medui::NodeRect{},
                                                             .foundValid = false}};
                  const auto composed = vu::composeDiff(flatFrame(width, height, ground), width, height, marks);

                  mdux::spec::Checks assertions;
                  assertions.expect(std::ranges::none_of(composed,
                                                         [](ColorRgba8 pixel) {
                                                             return pixel == vu::foundOutline;
                                                         }),
                                    "a found box that was never measured is not invented as a rectangle at the origin");
                  assertions.expect(at(composed, width, 0, 0) == vu::expectedOutline, "the expected box is still drawn");
                  assertions.raise();
              })
        .Execute();
}};

const mdux::spec::Register mismatchedExtentRefused{"Composing and encoding refuse an extent the pixels cannot fill", "evidence-unit", [] {
    return speclab::Test("verify-ui-diff-extent")
        .Given("a pixel count that is not width times height", [] {})
        .When("a diff is composed or encoded", [] {})
        .Then("both return nothing rather than reading past the frame",
              [] {
                  const std::vector<ColorRgba8> tooFew = flatFrame(4, 4, content);
                  mdux::spec::Checks            assertions;
                  assertions.expect(vu::composeDiff(tooFew, 8, 8, {}).empty(), "compose refuses a short frame");
                  assertions.expect(vu::encodePng(tooFew, 8, 8).empty(), "encode refuses a short frame");
                  assertions.expect(vu::encodePng(tooFew, 0, 0).empty(), "and refuses an empty extent, which no PNG can express");
                  assertions.raise();
              })
        .Execute();
}};

const mdux::spec::Register scopeNamesDoNotCollide{"Two scopes of one screen cannot claim the same diff image", "evidence-unit", [] {
    return speclab::Test("verify-ui-diff-name")
        .Given("scopes that a filtering encoder would map onto one name", [] {})
        .When("each is turned into a filename", [] {})
        .Then("they stay distinct, and an ordinary locale tag is unchanged",
              [] {
                  mdux::spec::Checks assertions;
                  // The common case first: nothing to escape, so nothing changes. An encoding that
                  // made every filename unreadable would be collision-free and useless.
                  assertions.expect(vu::diffImageName("endoscope-monitor", "en-US") == "endoscope-monitor.en-US.png",
                                    "an approved locale tag comes out as itself");

                  // The two collisions a filter produces. `ScreenPackage::validate()` refuses an
                  // empty locale and a duplicated one and imposes no grammar beyond that, so both
                  // pairs are reachable from an artifact this pipeline accepts.
                  assertions.expect(vu::diffImageName("s", "en/US") != vu::diffImageName("s", "enUS"),
                                    "a separator is escaped rather than dropped");
                  assertions.expect(vu::diffImageName("s", "(locale-free)") != vu::diffImageName("s", "locale-free"),
                                    "the locale-free scope cannot be confused with a locale of that name");

                  // No unescaped path separator survives, on either platform's spelling: a scope
                  // that produced one would not overwrite a sibling image, it would try to write
                  // into a directory that does not exist.
                  const std::string awkward = vu::diffImageName("s", "a/b\\c");
                  assertions.expect(awkward.find('/') == std::string::npos && awkward.find('\\') == std::string::npos,
                                    std::format("no path separator reaches the filename, got '{}'", awkward));
                  assertions.raise();
              })
        .Execute();
}};

const mdux::spec::Register encodesRealPng{"The encoder writes a PNG a decoder reads back pixel for pixel", "evidence-unit", [] {
    return speclab::Test("verify-ui-diff-png")
        .Given("a frame whose every pixel differs from its neighbours", [] {})
        .When("it is encoded and then decoded by chunk, CRC and inflate", [] {})
        .Then("the raster that comes back is the one that went in",
              [] {
                  // Wide enough that the raw raster exceeds one 65535-byte stored block, which is
                  // the case a single-block encoder would pass and a real frame would not: at
                  // 1280x720 the stream needs 57 of them.
                  constexpr std::uint32_t width  = 200;
                  constexpr std::uint32_t height = 100;
                  std::vector<ColorRgba8> frame;
                  frame.reserve(static_cast<std::size_t>(width) * height);
                  for (std::uint32_t y = 0; y < height; ++y) {
                      for (std::uint32_t x = 0; x < width; ++x) {
                          frame.push_back(ColorRgba8{.r = static_cast<std::uint8_t>(x),
                                                     .g = static_cast<std::uint8_t>(y),
                                                     .b = static_cast<std::uint8_t>(x ^ y),
                                                     .a = 255});
                      }
                  }

                  const std::vector<std::byte> encoded = vu::encodePng(frame, width, height);
                  mdux::spec::Checks           assertions;
                  const auto                   chunks = readPng(encoded);
                  assertions.expect(chunks.has_value(), "the signature and every chunk CRC are correct");
                  if (!chunks.has_value()) {
                      assertions.raise();
                      return;
                  }
                  assertions.expect(chunks->size() == 3, "IHDR, IDAT and IEND");
                  assertions.expect((*chunks)[0].type == "IHDR" && (*chunks)[1].type == "IDAT" && (*chunks)[2].type == "IEND",
                                    "in the order the format fixes");

                  const std::vector<std::uint8_t>& header = (*chunks)[0].body;
                  assertions.expect(header.size() == 13, "IHDR is thirteen bytes");
                  const auto beAt = [&header](std::size_t offset) {
                      return (static_cast<std::uint32_t>(header[offset]) << 24U) | (static_cast<std::uint32_t>(header[offset + 1]) << 16U)
                             | (static_cast<std::uint32_t>(header[offset + 2]) << 8U) | header[offset + 3];
                  };
                  assertions.expect(beAt(0) == width && beAt(4) == height, "and names the extent that was encoded");
                  assertions.expect(header[8] == 8 && header[9] == 6, "8 bits per channel, truecolour with alpha");
                  assertions.expect(header[10] == 0 && header[11] == 0 && header[12] == 0, "deflate, adaptive filtering, no interlace");

                  const auto raster = inflateStored((*chunks)[1].body);
                  assertions.expect(raster.has_value(), "the IDAT is a zlib stream of stored deflate blocks");
                  if (!raster.has_value()) {
                      assertions.raise();
                      return;
                  }
                  assertions.expect(raster->size() == static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4),
                                    "one filter byte plus four bytes per pixel, per row");

                  std::uint32_t low  = 1;
                  std::uint32_t high = 0;
                  for (const std::uint8_t byte : *raster) {
                      low  = (low + byte) % 65521U;
                      high = (high + low) % 65521U;
                  }
                  const std::span<const std::uint8_t> trailer{(*chunks)[1].body.end() - 4, (*chunks)[1].body.end()};
                  const std::uint32_t                 stated = (static_cast<std::uint32_t>(trailer[0]) << 24U)
                                                             | (static_cast<std::uint32_t>(trailer[1]) << 16U)
                                                             | (static_cast<std::uint32_t>(trailer[2]) << 8U) | trailer[3];
                  assertions.expect(stated == ((high << 16U) | low), "whose Adler-32 covers the inflated bytes");

                  std::size_t wrongFilter = 0;
                  std::size_t wrongPixel  = 0;
                  for (std::uint32_t y = 0; y < height; ++y) {
                      const std::size_t row = static_cast<std::size_t>(y) * (1 + static_cast<std::size_t>(width) * 4);
                      wrongFilter += static_cast<std::size_t>((*raster)[row] != 0);
                      for (std::uint32_t x = 0; x < width; ++x) {
                          const ColorRgba8  expected = frame[static_cast<std::size_t>(y) * width + x];
                          const std::size_t at4      = row + 1 + static_cast<std::size_t>(x) * 4;
                          wrongPixel += static_cast<std::size_t>((*raster)[at4] != expected.r || (*raster)[at4 + 1] != expected.g
                                                                 || (*raster)[at4 + 2] != expected.b || (*raster)[at4 + 3] != expected.a);
                      }
                  }
                  assertions.expect(wrongFilter == 0, "every row declares filter type None");
                  assertions.expect(wrongPixel == 0, "and every pixel survives the round trip");
                  assertions.raise();
              })
        .Execute();
}};

}  // namespace
