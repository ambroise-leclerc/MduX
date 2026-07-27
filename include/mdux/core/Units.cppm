/**
 * @file Units.cppm
 * @brief Governed-zone primitive types: pixels, rectangles, extents, color, locale.
 *
 * @compliance ADR-004 Trust zones in C++
 *
 * Part of MduXCore (see ADR-004). This module imports `std` only - no Vulkan, no
 * platform headers, no filesystem. Types here are the shared vocabulary later
 * governed modules (evidence, screen schema, ML) build on, so they stay simple
 * PODs/value types with no owning resources.
 */
module;

export module mdux.core.units;

import std;

export namespace mdux::core {

/// A pixel coordinate or dimension. Signed, since out-of-flow `position:` values
/// in a later .medui compiler may need to express negative offsets during layout
/// computation even though a final on-screen position never is.
using Px = std::int32_t;

struct Extent2D {
    Px width{};
    Px height{};

    friend constexpr bool operator==(const Extent2D&, const Extent2D&) = default;
};

struct Rect {
    Px x{};
    Px y{};
    Px width{};
    Px height{};

    [[nodiscard]] constexpr Px right() const noexcept { return x + width; }
    [[nodiscard]] constexpr Px bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr bool contains(Px px, Px py) const noexcept {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    [[nodiscard]] constexpr bool overlaps(const Rect& other) const noexcept {
        return x < other.right() && other.x < right() && y < other.bottom() && other.y < bottom();
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

/// Non-premultiplied 8-bit-per-channel RGBA. Deliberately not a Vulkan format enum
/// - that mapping belongs in the adapter zone, not here.
struct ColorRgba8 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    friend constexpr bool operator==(const ColorRgba8&, const ColorRgba8&) = default;
};

/// An index into a build-time-approved locale list (e.g. baked text/font packages'
/// per-locale glyph runs, issue #14/#15). Not a language tag itself - the mapping
/// from LocaleId to a BCP-47 tag lives in the baked package that defines the set.
using LocaleId = std::uint16_t;

inline constexpr LocaleId kDefaultLocale = 0;

}  // namespace mdux::core
