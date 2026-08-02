/**
 * @brief Compares a rendered frame against an expectation, and says exactly where it differs.
 *
 * ## The expectation is code, not a committed image
 *
 * There is no golden PNG in this repository and there is not meant to be. An expected *image* is a
 * binary blob a reviewer cannot check, so the only thing a reviewer could do with a golden-image
 * diff is accept it - which makes "update the golden" the path of least resistance and the
 * comparison decorative. An expected *rectangle at (10, 8), 20x16, opaque red on black* is a
 * sentence a reviewer can disagree with.
 *
 * It follows that nothing here can silently rewrite a golden, because there is no golden to
 * rewrite. Changing what a test expects means editing the expectation in the test, in the diff,
 * under review. That is issue #126's "no golden is silently rewritten" requirement satisfied by
 * construction rather than by discipline.
 *
 * The same reasoning the shader emitter uses for not committing generated C++: review the thing a
 * human can actually check, and derive the rest.
 *
 * ## Include order
 *
 * A header rather than a module because it is test scaffolding, in the same spirit as
 * MduXTest.hpp and SpirvFixtures.hpp: **include it after `import std;` and after
 * `import mdux.core.units;`**. It names `std::vector`, `std::span` and `std::format`, and
 * `mdux::core::ColorRgba8`, `Rect` and `Extent2D`, and declares none of them itself - so included
 * before those imports it fails with a list of undeclared identifiers that does not suggest the
 * cause.
 *
 * ## Actionable output
 *
 * A failure reports the first differing pixels by coordinate with expected and actual values in
 * hex, plus how many differ in total. "The image does not match" sends a reader to a diff tool
 * they do not have; "(12, 8): expected #ff0000ff, actual #000000ff - 320 of 3072 pixels differ"
 * usually identifies the fault on its own.
 */
#pragma once

namespace mdux::test {

/// One rectangle of flat colour, painted over whatever is beneath it.
struct ExpectedRect {
    mdux::core::Rect bounds{};
    mdux::core::ColorRgba8 color{};
};

/// An expected frame: a background, then rectangles painted in order.
///
/// Deliberately the same painter's model the renderer uses, so an expectation is written the way
/// the frame was built rather than as a transformation of it.
class ExpectedImage {
public:
    ExpectedImage(mdux::core::Extent2D extent, mdux::core::ColorRgba8 background) noexcept
        : extent_{extent}, background_{background} {}

    ExpectedImage& paint(mdux::core::Rect bounds, mdux::core::ColorRgba8 color) {
        rects_.push_back(ExpectedRect{.bounds = bounds, .color = color});
        return *this;
    }

    [[nodiscard]] mdux::core::ColorRgba8 at(mdux::core::Px x, mdux::core::Px y) const noexcept {
        mdux::core::ColorRgba8 result = background_;
        for (const ExpectedRect& rect : rects_) {
            if (rect.bounds.contains(x, y)) {
                result = rect.color;
            }
        }
        return result;
    }

    [[nodiscard]] mdux::core::Extent2D extent() const noexcept { return extent_; }

private:
    mdux::core::Extent2D extent_{};
    mdux::core::ColorRgba8 background_{};
    std::vector<ExpectedRect> rects_;
};

/// What a comparison found. Empty `message` means the frame matched.
struct PixelDiff {
    std::size_t differing{0};
    std::size_t total{0};
    std::string message;

    [[nodiscard]] bool matched() const noexcept { return differing == 0; }
};

[[nodiscard]] inline std::string toHex(mdux::core::ColorRgba8 color) {
    return std::format("#{:02x}{:02x}{:02x}{:02x}", color.r, color.g, color.b, color.a);
}

/**
 * @brief Compares every pixel of `actual` against `expected`.
 *
 * Every pixel, not a sample: stray geometry in a corner nobody sampled is exactly the defect a
 * spot check steps over, and it is cheap to be thorough at these sizes.
 *
 * At most `reportLimit` differences are listed. A frame that is wholly wrong would otherwise
 * produce thousands of identical lines and bury the count, which is the useful part.
 */
[[nodiscard]] inline PixelDiff compare(const ExpectedImage& expected,
                                       std::span<const mdux::core::ColorRgba8> actual,
                                       std::size_t reportLimit = 8) {
    PixelDiff diff;
    const mdux::core::Extent2D extent = expected.extent();
    diff.total = static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height);

    if (actual.size() != diff.total) {
        diff.differing = diff.total;
        diff.message = std::format("size mismatch: expected {} pixels ({}x{}), got {}", diff.total,
                                   extent.width, extent.height, actual.size());
        return diff;
    }

    std::string listed;
    for (mdux::core::Px y = 0; y < extent.height; ++y) {
        for (mdux::core::Px x = 0; x < extent.width; ++x) {
            const auto index = static_cast<std::size_t>(y) *
                                   static_cast<std::size_t>(extent.width) +
                               static_cast<std::size_t>(x);
            const mdux::core::ColorRgba8 want = expected.at(x, y);
            const mdux::core::ColorRgba8 got = actual[index];
            if (want == got) {
                continue;
            }
            ++diff.differing;
            if (diff.differing <= reportLimit) {
                listed += std::format("\n  ({}, {}): expected {}, actual {}", x, y, toHex(want),
                                      toHex(got));
            }
        }
    }

    if (diff.differing != 0) {
        diff.message = std::format("{} of {} pixels differ:{}", diff.differing, diff.total, listed);
        if (diff.differing > reportLimit) {
            diff.message += std::format("\n  ... and {} more", diff.differing - reportLimit);
        }
    }
    return diff;
}

}  // namespace mdux::test
