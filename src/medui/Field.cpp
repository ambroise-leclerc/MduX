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
        case FieldError::ListRejected:
            return "the draw list refused a rectangle - budget, or a degenerate extent";
    }
    // Named rather than defaulted so that adding an enumerator without a case here is a warning at
    // this switch instead of a blank string later.
    return "unknown field error";
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
    const std::int64_t pitch = toPixels(cell.widestAdvance, font);

    if (!cell.inked) {
        // Every permitted glyph is blank. A field of them draws no glyph, but the caret is still
        // drawn and still has to fit, so the extent is the caret's own column rather than nothing.
        return FieldExtent{.inked = false, .width = (static_cast<std::int64_t>(cells) * pitch) + caretWidth, .height = 0};
    }

    // The leftmost ink any value can put in cell 0, and the rightmost in the last cell. `left` may
    // be negative for a glyph whose bitmap starts behind its pen, which is why the width is a
    // difference rather than the right edge alone.
    const std::int64_t lastCellPen = static_cast<std::int64_t>(cells - 1) * pitch;
    const std::int64_t inkLeft     = std::min<std::int64_t>(cell.left, 0);
    const std::int64_t inkRight    = std::max(lastCellPen + cell.right, static_cast<std::int64_t>(cells) * pitch + caretWidth);

    return FieldExtent{.inked = true, .width = inkRight - inkLeft, .height = cell.bottom - cell.top};
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
    const auto originX   = static_cast<mdux::core::Px>(node.x);
    const auto baselineY = static_cast<mdux::core::Px>(node.y - cell.top);

    for (std::size_t index = 0; index < text.size(); ++index) {
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
        const auto height = static_cast<mdux::core::Px>(cell.inked ? cell.bottom - cell.top : 0);
        if (height > 0) {
            const mdux::core::Rect bar{.x      = caretX,
                                       .y      = static_cast<mdux::core::Px>(node.y),
                                       .width  = static_cast<mdux::core::Px>(caretWidth),
                                       .height = height};
            if (const auto added = list.addSolidRect(bar, color); !added.has_value()) {
                return refuse(FieldError::ListRejected);
            }
        }
    }

    return {};
}

}  // namespace mdux::medui
