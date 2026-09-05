/**
 * @file Field.cpp
 * @brief Implementation of the fixed-pitch text field.
 */

module;

module mdux.medui.field;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.reading;
import mdux.text.draw;

namespace mdux::medui {

namespace {

/// The union of every glyph the package's declared charset can put in one cell.
///
/// Walked over `restrictedCharset` rather than over the glyph table because the charset is what the
/// package *promises*, and a promise is the right basis for a bound a compiler signs. The ranges are
/// sorted and non-overlapping (`FontPackage::validate()`), so this visits each admitted code point
/// once.
struct CellInk {
    bool         inked{false};
    std::int64_t left{0};
    std::int64_t top{0};
    std::int64_t right{0};
    std::int64_t bottom{0};
    std::int64_t widestAdvance{0};
    bool         anyPermitted{false};
};

[[nodiscard]] CellInk cellInkOf(const mdux::font::FontPackage& font) noexcept {
    CellInk cell;
    for (const mdux::font::CharsetRange& range : font.restrictedCharset) {
        for (char32_t point = range.first; point <= range.last; ++point) {
            const mdux::font::GlyphRecord* glyph = font.find(point);
            if (glyph == nullptr) {
                // A charset that names a code point the package cannot draw. `validate()` refuses
                // such a package; skipping it here keeps a package that reached this function
                // anyway from widening a cell with a glyph it has no way to put in one.
                continue;
            }
            cell.anyPermitted  = true;
            cell.widestAdvance = std::max(cell.widestAdvance, static_cast<std::int64_t>(glyph->advanceWidth));
            if (glyph->isBlank()) {
                continue;
            }
            const std::int64_t left   = glyph->bitmapOriginX;
            const std::int64_t right  = left + glyph->width;
            const std::int64_t top    = -static_cast<std::int64_t>(glyph->bitmapOriginY);
            const std::int64_t bottom = top + glyph->height;
            if (!cell.inked) {
                cell = CellInk{.inked         = true,
                               .left          = left,
                               .top           = top,
                               .right         = right,
                               .bottom        = bottom,
                               .widestAdvance = cell.widestAdvance,
                               .anyPermitted  = true};
                continue;
            }
            cell.left   = std::min(cell.left, left);
            cell.top    = std::min(cell.top, top);
            cell.right  = std::max(cell.right, right);
            cell.bottom = std::max(cell.bottom, bottom);

            // `point` is a `char32_t` and the last range may end at the maximum value, so the loop
            // condition alone would wrap and never terminate. Checked here rather than in the
            // condition because the body must still run for that final code point.
            if (point == std::numeric_limits<char32_t>::max()) {
                break;
            }
        }
        if (range.last == std::numeric_limits<char32_t>::max()) {
            break;
        }
    }
    return cell;
}

/// How far the widest-reaching glyph's bitmap starts behind its pen, in pixels, or zero.
///
/// The one number the measurement and the placement must agree on, so it is computed once and used
/// by both. A positive left bearing is *not* pulled the other way: shifting the grid left to close a
/// gap would put ink outside the node on the right, and the node's edge is the thing being protected.
[[nodiscard]] std::int64_t overhangOf(const CellInk& cell) noexcept {
    return cell.inked && cell.left < 0 ? -cell.left : 0;
}

}  // namespace

mdux::core::ResultVoid<FieldError> fieldAccepts(std::size_t cells, std::size_t textLength, std::optional<std::size_t> caret) noexcept {
    if (cells == 0) {
        return mdux::core::err(FieldError::NoCells);
    }
    if (cells > maxFieldCells) {
        return mdux::core::err(FieldError::TooManyCells);
    }
    if (textLength > cells) {
        // Refused, never truncated. See Field.cppm: a shortened identifier is a different
        // identifier, and nothing on screen would say it had been cut.
        return mdux::core::err(FieldError::TextTooLong);
    }
    if (caret.has_value() && *caret > cells) {
        // `cells` itself is a position: the caret sits *before* cell `n`, so a caret at `cells` is
        // the one just past the last character, which is where it stands on a full field.
        return mdux::core::err(FieldError::CaretOutOfRange);
    }
    return {};
}

std::string_view describe(FieldError error) noexcept {
    switch (error) {
        case FieldError::NoCells:
            return "the field has no cells, so it has nowhere to display anything";
        case FieldError::TooManyCells:
            return "the field has more cells than this runtime will draw";
        case FieldError::TextTooLong:
            return "the value has more characters than the field has cells";
        case FieldError::CaretOutOfRange:
            return "the caret is not a position in the field";
        case FieldError::GlyphNotInPackage:
            return "the value needs a character the font package cannot draw";
        case FieldError::EmptyCharset:
            return "the font package admits no code point, so no cell width can be derived";
        case FieldError::CharsetHasNoInk:
            return "every code point the font package admits is blank, so no field could show anything";
        case FieldError::ListRejected:
            return "the draw list refused a rectangle - budget, or a degenerate extent";
    }
    // Named rather than defaulted so that adding an enumerator without a case here is a warning at
    // this switch instead of a blank string later.
    return "unknown field error";
}

mdux::core::Result<std::int64_t, FieldError> fieldOriginX(const mdux::font::FontPackage& font) noexcept {
    const CellInk cell = cellInkOf(font);
    if (!cell.anyPermitted) {
        return mdux::core::err(FieldError::EmptyCharset);
    }
    return overhangOf(cell);
}

mdux::core::Result<std::int64_t, FieldError> cellWidth(const mdux::font::FontPackage& font) noexcept {
    const CellInk cell = cellInkOf(font);
    if (!cell.anyPermitted) {
        return mdux::core::err(FieldError::EmptyCharset);
    }
    // Converted once, here, rather than per cell: a grid whose pitch was rounded per position would
    // accumulate the rounding across the field and put the last cell somewhere the measurement did
    // not. `toPixels()` is `mdux.medui.reading`'s, so a field and a reading round the same way.
    return toPixels(cell.widestAdvance, font);
}

mdux::core::Result<FieldExtent, FieldError> measureField(const mdux::font::FontPackage& font, std::size_t cells) noexcept {
    // No value and no caret to check here - a measurement is about what the field could *ever* hold.
    if (const auto counted = fieldAccepts(cells, 0, std::nullopt); !counted.has_value()) {
        return mdux::core::err(counted.error());
    }
    const CellInk cell = cellInkOf(font);
    if (!cell.anyPermitted) {
        return mdux::core::err(FieldError::EmptyCharset);
    }
    if (!cell.inked) {
        // Every permitted glyph is blank. This used to be reported as a legitimate measurement of
        // height zero, and `recordField()` then drew no caret for it and returned success - a caller
        // asking for a caret got neither a caret nor a refusal, which is the silent-absence failure
        // this module refuses everywhere else. A package that can put no ink in a cell is one no
        // field can display anything with, so it is refused in both places instead.
        return mdux::core::err(FieldError::CharsetHasNoInk);
    }
    const std::int64_t pitch = toPixels(cell.widestAdvance, font);

    // The leftmost ink any value can put in cell 0, and the rightmost in the last cell. `left` may
    // be negative for a glyph whose bitmap starts behind its pen - `J`, `T`, `Y`, `_` and `j` all do
    // in the committed package - which is why the width is a difference rather than the right edge
    // alone, and why `fieldOriginX()` shifts the grid by exactly the same amount.
    const std::int64_t lastCellPen = static_cast<std::int64_t>(cells - 1) * pitch;
    const std::int64_t inkRight    = std::max(lastCellPen + cell.right, static_cast<std::int64_t>(cells) * pitch + caretWidth);

    return FieldExtent{.inked = true, .width = inkRight + overhangOf(cell), .height = cell.bottom - cell.top};
}

mdux::core::ResultVoid<FieldError> recordField(mdux::draw::DrawList&          list,
                                               const mdux::font::FontPackage& font,
                                               const mdux::core::Rect&        node,
                                               std::size_t                    cells,
                                               std::span<const char32_t>      text,
                                               std::optional<std::size_t>     caret,
                                               mdux::core::ColorRgba8         color) noexcept {
    if (const auto accepted = fieldAccepts(cells, text.size(), caret); !accepted.has_value()) {
        return mdux::core::err(accepted.error());
    }

    const CellInk cell = cellInkOf(font);
    if (!cell.anyPermitted) {
        return mdux::core::err(FieldError::EmptyCharset);
    }
    if (!cell.inked) {
        // The same refusal `measureField()` makes, in the same words: a package that can put no ink
        // in a cell can draw neither a character nor a caret, and returning success for it would
        // report a field nobody can see as a field that was drawn.
        return mdux::core::err(FieldError::CharsetHasNoInk);
    }
    const std::int64_t pitch = toPixels(cell.widestAdvance, font);

    const mdux::draw::DrawList::Marker start  = list.mark();
    const auto                         refuse = [&list, &start](FieldError error) {
        // Cannot fail for a marker taken from this list moments ago; discarded rather than checked
        // because there is no second recovery to attempt.
        static_cast<void>(list.rollback(start));
        return mdux::core::err(error);
    };

    // The grid's own origin, from the envelope rather than from this value's ink - see Field.cppm
    // for why a field that re-derived its baseline per value would jump as an operator typed.
    //
    // The x shift is what keeps the placement and the measurement talking about the same rectangle.
    // A glyph whose bitmap starts behind its pen would otherwise put cell 0's ink left of the node -
    // one pixel, for five of the committed package's glyphs - and `mdux.medui.screen`'s own overflow
    // check refuses the *whole frame* for it, so a patient identifier beginning with `J` blanked the
    // display. `measureField()` had always reserved the room; only the placement had not used it.
    const auto originX   = static_cast<mdux::core::Px>(node.x + overhangOf(cell));
    const auto baselineY = static_cast<mdux::core::Px>(node.y - cell.top);

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (!font.permits(text[index])) {
            // The charset is the bound, and the glyph table is not the same set: `validate()`
            // requires every permitted code point to have a glyph but admits glyphs beyond the
            // charset. Drawing one of those would put a character the cell was never sized for into
            // a cell - `cellWidth()` is the widest advance over the *charset* - so what a field can
            // display has to be asked of the charset rather than of whatever the package happens to
            // carry. Same refusal as an absent glyph, because from a caller's side it is the same
            // fact: this package will not display that character.
            return refuse(FieldError::GlyphNotInPackage);
        }
        const mdux::font::GlyphRecord* glyph = font.find(text[index]);
        if (glyph == nullptr) {
            // No fallback: ADR-010 leaves the runtime none, and a substitute character in an
            // identifier is a different identifier.
            return refuse(FieldError::GlyphNotInPackage);
        }
        if (glyph->isBlank()) {
            // A space occupies its cell and paints nothing, exactly as `recordRun()` treats one.
            continue;
        }
        const auto penX = static_cast<mdux::core::Px>(originX + static_cast<mdux::core::Px>(static_cast<std::int64_t>(index) * pitch));
        if (const auto added = mdux::text::draw::addGlyphRect(list, font, *glyph, penX, baselineY, color); !added.has_value()) {
            return refuse(FieldError::ListRejected);
        }
    }

    if (caret.has_value()) {
        // A solid column at the cell boundary the caret sits on, as tall as the envelope the
        // compiler measured - so a caret in an empty field is as visible as one after a character,
        // and neither depends on what has been typed.
        const auto caretX = static_cast<mdux::core::Px>(originX + static_cast<mdux::core::Px>(static_cast<std::int64_t>(*caret) * pitch));
        // Always drawn, and always the envelope's full height: `cell.inked` is guaranteed above, so
        // there is no branch here in which a caret was asked for and silently not recorded.
        const mdux::core::Rect bar{.x      = caretX,
                                   .y      = static_cast<mdux::core::Px>(node.y),
                                   .width  = static_cast<mdux::core::Px>(caretWidth),
                                   .height = static_cast<mdux::core::Px>(cell.bottom - cell.top)};
        if (const auto added = list.addSolidRect(bar, color); !added.has_value()) {
            return refuse(FieldError::ListRejected);
        }
    }

    return {};
}

}  // namespace mdux::medui
