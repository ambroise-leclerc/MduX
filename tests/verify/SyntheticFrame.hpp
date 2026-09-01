/**
 * @file SyntheticFrame.hpp
 * @brief A framebuffer a scenario paints by hand, so a check can be exercised with no GPU.
 *
 * @compliance ADR-004 Trust zones in C++ (test-only; the library allocates, this may)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * ADR-014 decision 1 says a framebuffer is an array, so a unit test paints a rectangle into one and
 * asserts that `GoldenBounds` passes, then moves it by a pixel and asserts that it fails. This is
 * that array. It is deliberately the only fixture machinery in this suite: everything else a
 * scenario needs is a compiled screen, a golden entry or a run of records, all of which are plain
 * data a scenario writes out where a reader can see it.
 *
 * A `std::vector` rather than the caller-owned storage the governed zone insists on. The rule
 * ADR-004 states is about the library, and inverting it here would only mean sizing an array from
 * numbers a scenario would then have to repeat.
 */
#ifndef MDUX_TESTS_VERIFY_SYNTHETICFRAME_HPP
#define MDUX_TESTS_VERIFY_SYNTHETICFRAME_HPP

namespace mdux::test::verify {

/// A tightly packed RGBA8 image, painted rectangle by rectangle.
class Canvas {
public:
    Canvas(mdux::core::Px width, mdux::core::Px height, mdux::core::ColorRgba8 ground)
        : width_{width}, height_{height}, pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), ground) {}

    /// Paints `rect` in `colour`, clipped to the image so a scenario can deliberately overflow.
    void fill(mdux::medui::NodeRect rect, mdux::core::ColorRgba8 colour) {
        for (mdux::core::Px y = rect.y; y < rect.y + rect.height; ++y) {
            for (mdux::core::Px x = rect.x; x < rect.x + rect.width; ++x) {
                set(x, y, colour);
            }
        }
    }

    void set(mdux::core::Px x, mdux::core::Px y, mdux::core::ColorRgba8 colour) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = colour;
    }

    [[nodiscard]] std::span<const mdux::core::ColorRgba8> pixels() const noexcept {
        return pixels_;
    }

    /// The view the checks take. Throws rather than returning a `Result`: a fixture that cannot
    /// describe its own image is a broken scenario, not a verification outcome.
    [[nodiscard]] mdux::verify::FramebufferView view() const {
        auto made = mdux::verify::FramebufferView::createPacked(pixels_, width_, height_);
        if (!made.has_value()) {
            throw speclab::core::AssertionFailure(std::string{"the synthetic frame was refused: "} + std::string{mdux::verify::describe(made.error())},
                                                  std::source_location::current());
        }
        return *made;
    }

private:
    mdux::core::Px                      width_{0};
    mdux::core::Px                      height_{0};
    std::vector<mdux::core::ColorRgba8> pixels_;
};

/// The quantised colour a token resolves to, which is what a correct frame actually carries.
///
/// Through the governed table and the governed quantisation rather than four numbers written into a
/// scenario: an expectation carrying its own copy of a colour would keep passing while the table
/// moved, which is one of the two regressions this suite exists to catch.
[[nodiscard]] inline mdux::core::ColorRgba8 tintOf(std::string_view token) {
    const auto resolved = mdux::medui::resolveColorToken(token);
    if (!resolved.has_value()) {
        throw speclab::core::AssertionFailure(std::string{"the governed table does not define "} + std::string{token}, std::source_location::current());
    }
    return mdux::medui::quantise(*resolved);
}

/// A v1 run record: glyph index, then x, then y, little-endian, six bytes.
///
/// Written out here rather than memcpy'd over a struct for `decodeRecord()`'s reason: the sidecar's
/// byte order is a contract, and a fixture that inherited the host's would make a byte-order defect
/// invisible to every scenario built on it.
[[nodiscard]] inline std::array<std::byte, 6> record(std::uint16_t glyph, std::int16_t x, std::int16_t y) {
    const auto ux = static_cast<std::uint16_t>(x);
    const auto uy = static_cast<std::uint16_t>(y);
    return std::array<std::byte, 6>{static_cast<std::byte>(glyph & 0xFFU),
                                    static_cast<std::byte>((glyph >> 8) & 0xFFU),
                                    static_cast<std::byte>(ux & 0xFFU),
                                    static_cast<std::byte>((ux >> 8) & 0xFFU),
                                    static_cast<std::byte>(uy & 0xFFU),
                                    static_cast<std::byte>((uy >> 8) & 0xFFU)};
}

/// Concatenates records into the sidecar bytes a run addresses.
[[nodiscard]] inline std::vector<std::byte> runOf(std::span<const std::array<std::byte, 6>> records) {
    std::vector<std::byte> bytes;
    for (const std::array<std::byte, 6>& one : records) {
        bytes.insert(bytes.end(), one.begin(), one.end());
    }
    return bytes;
}

}  // namespace mdux::test::verify

#endif  // MDUX_TESTS_VERIFY_SYNTHETICFRAME_HPP
