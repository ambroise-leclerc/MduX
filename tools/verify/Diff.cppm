/**
 * @file Diff.cppm
 * @brief The failure diff image: a rendered frame with every failed obligation drawn on it.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * ## Why this is outside the artifact, and outside the source tree
 *
 * ADR-014 decision 4 keeps measured pixels out of `verification.json`, because a byte-compared file
 * that carried them would become a property of the driver tuple that produced the frame rather than
 * of its declared inputs. The diff image is exactly such a measurement - it *is* the frame - so it
 * is a CI attachment for a human and never a fifth file in the screen bundle. #255 states that split
 * and this module is the half that lives outside.
 *
 * Two consequences follow and both are deliberate. Nothing here is byte-compared, so lavapipe and
 * MoltenVK may legitimately produce different images for one failure. And nothing here is read back
 * by any check: an image this module wrote is never an input to a verdict, so a bug in it can make a
 * failure harder to read and can never make one pass.
 *
 * ## Why the encoder is written here rather than linked
 *
 * A PNG a reviewer can open in the browser tab GitHub already gives them is worth more than a format
 * they have to convert, and the alternative to ~90 lines here is a compression library in the
 * dependency graph of a build tool - which ADR-007's zero-SOUP direction exists to avoid, and which
 * would need qualifying for the sake of an image nobody's verdict depends on.
 *
 * The deflate stream is **stored blocks only**: `BTYPE=00`, a length and its complement, then the
 * bytes. That is a legal deflate stream by RFC 1951 §3.2.4, every decoder accepts it, and it costs
 * no Huffman coder. The price is size - a 1280x720 frame lands at about 3.7 MB - and it is paid only
 * on a failing run, once per failing render scope.
 */
module;

export module mdux.tools.verify.diff;

import std;
import mdux.core.units;
import mdux.medui.schema;

export namespace mdux::tools::verify {

/**
 * @brief One failed obligation, reduced to what can be drawn on a frame.
 *
 * Owned and flat rather than a reference to a driver `Outcome`, so composing an image needs no part
 * of the driver's type graph and can be unit-tested from four numbers and a colour.
 */
struct DiffMark {
    std::string           nodeId;
    std::string           check;
    mdux::medui::NodeRect expected{};
    mdux::medui::NodeRect found{};
    bool                  foundValid{false};
};

/// The colours the overlay uses, exported so a test names them rather than repeating literals.
///
/// Magenta and cyan because neither appears in the governed theme table, so an outline can never be
/// mistaken for content the screen itself drew.
inline constexpr mdux::core::ColorRgba8 expectedOutline{.r = 255, .g = 0, .b = 255, .a = 255};
inline constexpr mdux::core::ColorRgba8 foundOutline{.r = 0, .g = 255, .b = 255, .a = 255};

/// How far the frame is dimmed before the outlines go on, as a numerator over 256.
///
/// The frame has to stay legible - a reader needs to see *what* was drawn, not only where the boxes
/// are - while an outline in full-intensity magenta has to be unmistakably not part of it.
inline constexpr std::uint32_t dimNumerator = 96;

/// Thickness of an outline, in pixels. Two rather than one: a single-pixel box on a 1280x720 frame
/// disappears at the zoom level a reviewer first opens the image at.
inline constexpr std::int64_t outlineThickness = 2;

/**
 * @brief Dims `frame` and draws every mark's expected and found rectangle on top of it.
 *
 * @param frame  the rendered readback, row-major, `width * height` pixels
 * @param width  the frame's width in pixels
 * @param height the frame's height in pixels
 * @param marks  the failed obligations of one render scope
 *
 * Returns an empty vector when `frame` is not exactly `width * height` pixels, which is the only way
 * this can be asked for something it cannot produce.
 */
[[nodiscard]] std::vector<mdux::core::ColorRgba8>
composeDiff(std::span<const mdux::core::ColorRgba8> frame, std::uint32_t width, std::uint32_t height, std::span<const DiffMark> marks);

/**
 * @brief Encodes RGBA pixels as a PNG.
 *
 * Returns an empty vector when `pixels` is not exactly `width * height`, or when either dimension is
 * zero - a PNG has no legal representation of an empty image.
 */
[[nodiscard]] std::vector<std::byte> encodePng(std::span<const mdux::core::ColorRgba8> pixels, std::uint32_t width, std::uint32_t height);

}  // namespace mdux::tools::verify
