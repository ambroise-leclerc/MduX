/**
 * @file Viewport.cpp
 * @brief Implementation of `recordWaterfall()`, the one part of `mdux.medui.viewport` that touches a
 *        `DrawList`.
 */

module;

module mdux.medui.viewport;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;

namespace mdux::medui {

mdux::core::ResultVoid<WaterfallError>
recordWaterfall(mdux::draw::DrawList& list, const mdux::core::Rect& nodeBand, const WaterfallGrid& grid, const WaterfallStyle& style) noexcept {
    if (const auto valid = validate(nodeBand, grid, style); !valid.has_value()) {
        return valid;
    }

    const mdux::draw::DrawList::Marker start = list.mark();

    for (std::size_t row = 0; row < grid.rowCount; ++row) {
        for (std::size_t col = 0; col < grid.bins; ++col) {
            // `validate()` has just proven every (row, col) in this range names a live, finite
            // sample - but it read `grid` a moment ago, and a caller-owned ring is revalidated on
            // every call for exactly the reason `mdux.medui.trace`'s module comment gives: nothing
            // stops the producer from moving `oldestRow`/`rowCount` between two calls made by two
            // different callers. Within this one call nothing yields between the two reads, so this
            // can only fail if that invariant was somehow wrong - `MalformedGrid` rather than a
            // subscript past the end either way.
            const std::optional<float> sample = grid.at(row, col);
            if (!sample.has_value()) {
                static_cast<void>(list.rollback(start));
                return mdux::core::err(WaterfallError::MalformedGrid);
            }

            const mdux::core::Rect       cell   = waterfallCellRect(nodeBand, grid.rowCount, grid.bins, row, col);
            const mdux::core::ColorRgba8 colour = waterfallCellColor(*sample, style);
            if (const auto recorded = list.addSolidRect(cell, colour); !recorded.has_value()) {
                static_cast<void>(list.rollback(start));
                return mdux::core::err(WaterfallError::ListRejected);
            }
        }
    }

    return {};
}

}  // namespace mdux::medui
