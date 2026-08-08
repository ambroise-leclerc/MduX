/**
 * @file AtlasPacker.cpp
 * @brief Implementation of the host-only shelf atlas packer.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * See AtlasPacker.cppm for why shelf placement rather than skyline, and why the sheet is retried
 * from scratch at each candidate size rather than grown incrementally.
 */
module;

module mdux.tools.atlaspacker;

import std;
import mdux.core.result;

namespace mdux::tools::atlas {

using mdux::core::err;
using mdux::core::Result;

namespace {

/// Attempts a full placement at one sheet size. Returns nullopt when the set does not fit, which
/// the caller reads as "try the next size up" rather than as an error.
[[nodiscard]] std::optional<std::vector<GlyphSlot>> placeAt(std::span<const GlyphExtent> sorted, std::uint32_t sheetWidth,
                                                            std::uint32_t sheetHeight) {
    std::vector<GlyphSlot> slots;
    slots.reserve(sorted.size());

    std::uint32_t shelfY      = 0;  // bottom of the current shelf
    std::uint32_t shelfHeight = 0;  // set by the first glyph placed on it
    std::uint32_t penX        = 0;

    for (const GlyphExtent& extent : sorted) {
        if (extent.width == 0 || extent.height == 0) {
            // A blank glyph - a space, or an outline that scaled below one subpixel. It occupies
            // no area, so it takes a zero-size slot at the origin rather than advancing the pen.
            // Skipping it entirely would leave the package without a slot to record.
            slots.push_back(GlyphSlot{.id = extent.id, .x = 0, .y = 0, .width = 0, .height = 0});
            continue;
        }

        const std::uint32_t needWidth  = extent.width + glyphPadding;
        const std::uint32_t needHeight = extent.height + glyphPadding;

        if (penX + needWidth > sheetWidth) {
            // Close this shelf and open the next one above it.
            shelfY += shelfHeight;
            shelfHeight = 0;
            penX        = 0;
        }
        if (shelfHeight == 0) {
            shelfHeight = needHeight;
        }
        if (shelfY + shelfHeight > sheetHeight || penX + needWidth > sheetWidth) {
            return std::nullopt;  // does not fit at this size
        }

        slots.push_back(GlyphSlot{.id = extent.id, .x = penX, .y = shelfY, .width = extent.width, .height = extent.height});
        penX += needWidth;
    }
    return slots;
}

}  // namespace

std::string_view describe(PackError error) noexcept {
    switch (error) {
        case PackError::NoGlyphs:
            return "there are no glyphs to pack";
        case PackError::GlyphTooLarge:
            return "one glyph alone exceeds the maximum atlas edge, so no sheet can hold it";
        case PackError::AtlasBudgetExceeded:
            return "the glyph set does not fit within the maximum atlas edge in both axes";
        case PackError::DuplicateGlyphId:
            return "two glyphs carry the same id, so a slot lookup would be ambiguous";
    }
    return "unknown atlas packing error";
}

std::uint32_t AtlasLayout::occupancyPercent() const noexcept {
    const std::uint64_t sheet = static_cast<std::uint64_t>(width) * height;
    if (sheet == 0) {
        return 0;
    }
    std::uint64_t used = 0;
    for (const GlyphSlot& slot : slots) {
        used += static_cast<std::uint64_t>(slot.width) * slot.height;
    }
    return static_cast<std::uint32_t>(used * 100u / sheet);
}

Result<AtlasLayout, PackError> pack(std::span<const GlyphExtent> extents) noexcept {
    if (extents.empty()) {
        return err(PackError::NoGlyphs);
    }

    try {
        // Ids must be unique: the package records one slot per id, and a duplicate would make the
        // lookup ambiguous in a way that only shows up as the wrong glyph being drawn.
        std::vector<std::uint32_t> ids;
        ids.reserve(extents.size());
        for (const GlyphExtent& extent : extents) {
            ids.push_back(extent.id);
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
            return err(PackError::DuplicateGlyphId);
        }

        for (const GlyphExtent& extent : extents) {
            // Subtraction, not addition. `extent.width + glyphPadding` is uint32 arithmetic and
            // wraps to a small number for a width near 0xFFFFFFFF, so the addition form lets the
            // very largest inputs slip past the guard that exists to catch them. Comparing
            // against `maximumAtlasEdge - glyphPadding` cannot wrap, because the constant is
            // larger than the padding.
            static_assert(maximumAtlasEdge > glyphPadding, "the subtraction below would wrap");
            if (extent.width > maximumAtlasEdge - glyphPadding || extent.height > maximumAtlasEdge - glyphPadding) {
                // Distinct from AtlasBudgetExceeded: no sheet size can ever help, so an author
                // should be told the glyph is the problem rather than the budget.
                return err(PackError::GlyphTooLarge);
            }
        }

        // Tallest first, so shelves are filled by glyphs of similar height and the wasted band
        // above each row stays small. Width and id break ties, making the order total - a pair of
        // equal-height glyphs must not be able to swap on a different standard library.
        std::vector<GlyphExtent> sorted(extents.begin(), extents.end());
        std::sort(sorted.begin(), sorted.end(), [](const GlyphExtent& a, const GlyphExtent& b) noexcept {
            if (a.height != b.height)
                return a.height > b.height;
            if (a.width != b.width)
                return a.width > b.width;
            return a.id < b.id;
        });

        for (std::uint32_t width = minimumAtlasEdge; width <= maximumAtlasEdge; width *= 2u) {
            for (std::uint32_t height = minimumAtlasEdge; height <= width; height *= 2u) {
                // First fit in width-major order. Not the smallest sheet by area, and not the
                // smallest among these candidates either - area is not monotonic in this
                // enumeration, so 256x256 (65536 px) is reached before 512x64 (32768 px) and a
                // set fitting both takes the larger one.
                //
                // Kept anyway, because "squarish" is the property worth having in a texture
                // atlas and "minimal area" is not: a 512x64 strip wastes no pixels but is a worse
                // shape to sample from and to fit under a device's texture limits. What matters
                // for ADR-007 is that the choice is a pure function of the glyph set, which
                // first-fit in a fixed order is.
                //
                // Stated this way after two rounds of getting it wrong: the earlier comments
                // claimed a minimum, then a qualified minimum, and neither was true.
                auto slots = placeAt(sorted, width, height);
                if (!slots.has_value()) {
                    continue;
                }
                AtlasLayout layout;
                layout.width  = width;
                layout.height = height;
                layout.slots  = std::move(*slots);
                // Sorted by id, not placement order: a caller binary-searches this, and a
                // committed package whose slot list reorders when one glyph changes size would
                // produce a diff nobody can read.
                std::sort(layout.slots.begin(), layout.slots.end(),
                          [](const GlyphSlot& a, const GlyphSlot& b) noexcept { return a.id < b.id; });
                return layout;
            }
        }
        return err(PackError::AtlasBudgetExceeded);
    } catch (...) {
        // `pack()` *is* noexcept, which is exactly why this catch exists: the vectors above can
        // throw `std::bad_alloc`, and an exception escaping a noexcept function calls
        // `std::terminate`. Turning it into the budget diagnostic keeps a host tool that ran out
        // of memory on one font from taking down the whole bake, the same reasoning
        // `raster::rasterise()` applies at its own noexcept boundary.
        return err(PackError::AtlasBudgetExceeded);
    }
}

}  // namespace mdux::tools::atlas
