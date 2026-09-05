/**
 * @file Reading.cpp
 * @brief Implementation of the governed reading expansion.
 */

module;

module mdux.medui.reading;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.text.draw;

namespace mdux::medui {

namespace {

/// The code points one concrete reading draws, one per pattern position.
///
/// Fixed capacity, so the whole expansion is a walk over caller-free storage. `maxPatternLength`
/// is the bound ADR-010 decision 4's amendment names, and this array is where it is spent.
struct RenderedPattern {
    std::array<char32_t, maxPatternLength> points{};
    std::size_t                            count{0};
};

/// The digits of `value`, most significant first, zero-padded to `slots`.
///
/// `ValueTooLarge` rather than a truncation, for the reason Reading.cppm gives at length: a reading
/// that dropped its most significant digit would be a smaller, entirely plausible number in a box
/// whose unit says what it means.
[[nodiscard]] mdux::core::Result<std::array<std::uint8_t, maxDigitsPerField>, ReadingError> digitsOf(std::int64_t value, std::size_t slots) noexcept {
    std::array<std::uint8_t, maxDigitsPerField> digits{};
    if (value < 0) {
        return mdux::core::err(ReadingError::ValueNegative);
    }
    if (slots == 0) {
        return mdux::core::err(ReadingError::NoDigitSlots);
    }
    if (slots > digits.size()) {
        return mdux::core::err(ReadingError::ValueTooLarge);
    }

    std::int64_t remaining = value;
    for (std::size_t position = slots; position > 0; --position) {
        digits[position - 1] = static_cast<std::uint8_t>(remaining % 10);
        remaining           /= 10;
    }
    if (remaining != 0) {
        // What is left after filling every slot is the part that would have been dropped.
        return mdux::core::err(ReadingError::ValueTooLarge);
    }
    return digits;
}

/// A pattern's own structural limits, checked once before either caller walks it.
[[nodiscard]] mdux::core::ResultVoid<ReadingError> checkPattern(std::string_view pattern) noexcept {
    if (pattern.empty()) {
        return mdux::core::err(ReadingError::PatternEmpty);
    }
    if (pattern.size() > maxPatternLength) {
        return mdux::core::err(ReadingError::PatternTooLong);
    }
    return {};
}

/**
 * @brief The concrete code points a pattern draws, with `digits` filling its slots in order.
 *
 * Literals pass through as themselves. The caller has already proved `digits` holds one entry per
 * slot, so this cannot run off the end of it.
 */
[[nodiscard]] RenderedPattern render(std::string_view pattern, PatternKind kind, std::span<const std::uint8_t> digits) noexcept {
    RenderedPattern rendered;
    std::size_t     nextDigit = 0;
    for (const char character : pattern) {
        if (isDigitSlot(character, kind) && nextDigit < digits.size()) {
            rendered.points[rendered.count] = U'0' + static_cast<char32_t>(digits[nextDigit]);
            ++nextDigit;
        } else {
            rendered.points[rendered.count] = static_cast<char32_t>(static_cast<unsigned char>(character));
        }
        ++rendered.count;
    }
    return rendered;
}

/// The ink box one rendered pattern occupies, in the reading's own coordinates.
///
/// The same quantity `measureInk()` takes for a baked run in `Screen.cpp`, computed for glyphs this
/// module places rather than for records a baker positioned - so the placement rule and the
/// measurement that justifies it stay one walk apart rather than one module apart.
struct InkBox {
    bool         inked{false};
    std::int64_t left{0};
    std::int64_t top{0};
    std::int64_t right{0};
    std::int64_t bottom{0};
};

/**
 * @brief Walks a rendered pattern, calling `visit` with each glyph and its pen position in pixels.
 *
 * The one pen in this module. `measurePattern()` does not use it - an envelope needs two pens at
 * once - but both concrete walks below do, so a placement and the overflow re-check that follows it
 * cannot disagree about where a glyph went.
 */
template <typename Visit>
[[nodiscard]] mdux::core::ResultVoid<ReadingError> walk(const mdux::font::FontPackage& font, const RenderedPattern& rendered, Visit&& visit) noexcept {
    std::int64_t pen = 0;
    for (std::size_t index = 0; index < rendered.count; ++index) {
        const mdux::font::GlyphRecord* glyph = font.find(rendered.points[index]);
        if (glyph == nullptr) {
            // No fallback: ADR-010 leaves the runtime none, and a substitute character in a reading
            // is a reading nobody wrote.
            return mdux::core::err(ReadingError::GlyphNotInPackage);
        }

        visit(*glyph, toPixels(pen, font));

        if (index + 1 < rendered.count) {
            pen += static_cast<std::int64_t>(glyph->advanceWidth) + font.kerningFor(rendered.points[index], rendered.points[index + 1]);
            if (pen < 0) {
                // Kerning is signed, so a package could in principle walk the pen behind the
                // reading's origin. Refused rather than clamped: a glyph left of where the node
                // begins is a glyph in the neighbour's box.
                return mdux::core::err(ReadingError::PenMovedBackwards);
            }
        }
    }
    return {};
}

/// The ink box of a rendered pattern, without recording anything.
[[nodiscard]] mdux::core::Result<InkBox, ReadingError> inkOf(const mdux::font::FontPackage& font, const RenderedPattern& rendered) noexcept {
    InkBox box;
    if (const auto walked = walk(font,
                                 rendered,
                                 [&box](const mdux::font::GlyphRecord& glyph, std::int64_t penX) {
                                     if (glyph.isBlank()) {
                                         return;
                                     }
                                     const std::int64_t left   = penX + glyph.bitmapOriginX;
                                     const std::int64_t right  = left + glyph.width;
                                     const std::int64_t top    = -static_cast<std::int64_t>(glyph.bitmapOriginY);
                                     const std::int64_t bottom = top + glyph.height;
                                     if (!box.inked) {
                                         box = InkBox{.inked = true, .left = left, .top = top, .right = right, .bottom = bottom};
                                         return;
                                     }
                                     box.left   = std::min(box.left, left);
                                     box.top    = std::min(box.top, top);
                                     box.right  = std::max(box.right, right);
                                     box.bottom = std::max(box.bottom, bottom);
                                 });
        !walked.has_value()) {
        return mdux::core::err(walked.error());
    }
    return box;
}

/**
 * @brief Records one rendered pattern into `list`, with its ink box at `node`'s top-left corner.
 *
 * Two walks rather than one, and the cost is deliberate. The first measures where the ink starts so
 * the placement can put that corner on the node's corner - the rule `mdux.medui.screen` fixes for a
 * `Label`, applied here for the same reason: it is the box the build-time measurement proved fits.
 * The second records. Both are bounded by `maxPatternLength`, so the doubling is a constant factor
 * on a constant.
 */
[[nodiscard]] mdux::core::ResultVoid<ReadingError> recordRendered(mdux::draw::DrawList&          list,
                                                                  const mdux::font::FontPackage& font,
                                                                  const mdux::core::Rect&        node,
                                                                  const RenderedPattern&         rendered,
                                                                  mdux::core::ColorRgba8         color) noexcept {
    const auto ink = inkOf(font, rendered);
    if (!ink.has_value()) {
        return mdux::core::err(ink.error());
    }
    if (!ink->inked) {
        // A pattern that paints nothing - all spaces. Measured, found to have no ink, and recorded
        // as nothing, which is a different outcome from a refusal and is not one.
        return {};
    }

    const mdux::draw::DrawList::Marker start  = list.mark();
    const auto                         refuse = [&list, &start](ReadingError error) {
        // Cannot fail for a marker taken from this list moments ago; discarded rather than checked
        // because there is no second recovery to attempt.
        static_cast<void>(list.rollback(start));
        return mdux::core::err(error);
    };

    const auto originX   = static_cast<mdux::core::Px>(node.x - ink->left);
    const auto baselineY = static_cast<mdux::core::Px>(node.y - ink->top);

    bool       listRefused = false;
    const auto walked      = walk(font, rendered, [&](const mdux::font::GlyphRecord& glyph, std::int64_t penX) {
        if (listRefused) {
            return;
        }
        if (const auto added = mdux::text::draw::addGlyphRect(list, font, glyph, originX + static_cast<mdux::core::Px>(penX), baselineY, color);
            !added.has_value()) {
            listRefused = true;
        }
    });
    if (!walked.has_value()) {
        return refuse(walked.error());
    }
    if (listRefused) {
        return refuse(ReadingError::ListRejected);
    }
    return {};
}

}  // namespace

std::string_view describe(ReadingError error) noexcept {
    switch (error) {
        case ReadingError::PatternEmpty:
            return "the reading's pattern has no characters";
        case ReadingError::PatternTooLong:
            return "the reading's pattern is longer than this runtime will draw";
        case ReadingError::GlyphNotInPackage:
            return "the pattern needs a character the font package cannot draw";
        case ReadingError::PenMovedBackwards:
            return "kerning would move the pen left of the reading's origin";
        case ReadingError::ValueTooLarge:
            return "the value has more digits than the pattern's slots can hold";
        case ReadingError::ValueNegative:
            return "the value is negative, and no pattern here renders a sign";
        case ReadingError::NoDigitSlots:
            return "a value was offered to a pattern with no digit slot to put it in";
        case ReadingError::ListRejected:
            return "the draw list refused a rectangle - budget, or a degenerate extent";
    }
    // Named rather than defaulted so that adding an enumerator without a case here is a warning at
    // this switch instead of a blank string later.
    return "unknown reading error";
}

std::size_t countSlots(std::string_view pattern, PatternKind kind) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(pattern, [kind](char character) {
        return isDigitSlot(character, kind);
    }));
}

mdux::core::Result<PatternExtent, ReadingError> measurePattern(const mdux::font::FontPackage& font, std::string_view pattern, PatternKind kind) noexcept {
    if (const auto checked = checkPattern(pattern); !checked.has_value()) {
        return mdux::core::err(checked.error());
    }

    // Two pens, not one. The envelope has to hold every reading the pattern can produce, and digit
    // advances need not be equal - so the leftmost a glyph can start and the rightmost it can end
    // are reached by different readings, and a single pen would describe neither.
    std::int64_t  penMin{0};
    std::int64_t  penMax{0};
    PatternExtent extent;
    std::int64_t  left{0};
    std::int64_t  top{0};
    std::int64_t  right{0};
    std::int64_t  bottom{0};

    for (std::size_t index = 0; index < pattern.size(); ++index) {
        const PatternSlot slot = slotAt(pattern[index], kind);
        std::int64_t      minAdvance{std::numeric_limits<std::int64_t>::max()};
        std::int64_t      maxAdvance{0};

        for (std::size_t candidate = 0; candidate < slot.count; ++candidate) {
            const mdux::font::GlyphRecord* glyph = font.find(slot.points[candidate]);
            if (glyph == nullptr) {
                return mdux::core::err(ReadingError::GlyphNotInPackage);
            }
            minAdvance = std::min(minAdvance, static_cast<std::int64_t>(glyph->advanceWidth));
            maxAdvance = std::max(maxAdvance, static_cast<std::int64_t>(glyph->advanceWidth));
            if (glyph->isBlank()) {
                continue;
            }

            const std::int64_t glyphLeft   = toPixels(penMin, font) + glyph->bitmapOriginX;
            const std::int64_t glyphRight  = toPixels(penMax, font) + glyph->bitmapOriginX + glyph->width;
            const std::int64_t glyphTop    = -static_cast<std::int64_t>(glyph->bitmapOriginY);
            const std::int64_t glyphBottom = glyphTop + glyph->height;
            if (!extent.inked) {
                extent.inked = true;
                left         = glyphLeft;
                right        = glyphRight;
                top          = glyphTop;
                bottom       = glyphBottom;
                continue;
            }
            left   = std::min(left, glyphLeft);
            right  = std::max(right, glyphRight);
            top    = std::min(top, glyphTop);
            bottom = std::max(bottom, glyphBottom);
        }

        if (index + 1 < pattern.size()) {
            const PatternSlot next = slotAt(pattern[index + 1], kind);
            std::int64_t      minKerning{std::numeric_limits<std::int64_t>::max()};
            std::int64_t      maxKerning{std::numeric_limits<std::int64_t>::min()};
            for (std::size_t current = 0; current < slot.count; ++current) {
                for (std::size_t following = 0; following < next.count; ++following) {
                    const std::int64_t adjustment = font.kerningFor(slot.points[current], next.points[following]);
                    minKerning                    = std::min(minKerning, adjustment);
                    maxKerning                    = std::max(maxKerning, adjustment);
                }
            }
            penMin += minAdvance + minKerning;
            penMax += maxAdvance + maxKerning;
            if (penMin < 0) {
                return mdux::core::err(ReadingError::PenMovedBackwards);
            }
        }
    }

    if (extent.inked) {
        extent.width  = right - left;
        extent.height = bottom - top;
    }
    return extent;
}

mdux::core::ResultVoid<ReadingError> recordNumeric(mdux::draw::DrawList&          list,
                                                   const mdux::font::FontPackage& font,
                                                   const mdux::core::Rect&        node,
                                                   std::string_view               pattern,
                                                   std::int64_t                   value,
                                                   mdux::core::ColorRgba8         color) noexcept {
    if (const auto checked = checkPattern(pattern); !checked.has_value()) {
        return mdux::core::err(checked.error());
    }

    const std::size_t slots  = countSlots(pattern, PatternKind::Numeric);
    const auto        digits = digitsOf(value, slots);
    if (!digits.has_value()) {
        return mdux::core::err(digits.error());
    }

    const RenderedPattern rendered = render(pattern, PatternKind::Numeric, std::span{*digits}.first(slots));
    return recordRendered(list, font, node, rendered, color);
}

mdux::core::ResultVoid<ReadingError> recordClock(mdux::draw::DrawList&          list,
                                                 const mdux::font::FontPackage& font,
                                                 const mdux::core::Rect&        node,
                                                 ClockFormat                    format,
                                                 const CivilTime&               now,
                                                 mdux::core::ColorRgba8         color) noexcept {
    const std::string_view pattern = rendering(format);
    if (const auto checked = checkPattern(pattern); !checked.has_value()) {
        return mdux::core::err(checked.error());
    }

    // Written out per format rather than derived from the slot letters. `YYYY-MM-DD HH:MM:SS` uses
    // `M` for both the month and the minute, so a letter-driven rule would have to break that tie
    // by position - a fragile inference from a string, when the set of strings is closed and has
    // two members and the contract fixes both. See Reading.cppm.
    std::array<std::uint8_t, maxPatternLength> digits{};
    std::size_t                                count = 0;

    // Whether `value` is drawable in exactly `width` slots. Checked before anything is pushed,
    // because `push()` below takes the low `width` digits and cannot tell a value that fits from one
    // that was truncated to fit. The field types do not do this for us: month, day, hour, minute and
    // second are `std::uint8_t`, which holds 0-255, so an hour of 123 has always been representable
    // and used to draw as `23` - a plausible different time, which is the one output this module
    // exists to refuse.
    const auto fits = [](std::int64_t value, std::size_t width) noexcept {
        if (value < 0) {
            return false;
        }
        std::int64_t limit = 1;
        for (std::size_t decade = 0; decade < width; ++decade) {
            limit *= 10;
        }
        return value < limit;
    };

    // Only the fields this format actually renders. A `TimeSeconds` clock draws no year, so a host
    // that leaves `year` at some out-of-range value must still get its clock - which is why this is
    // per-format rather than one check over the whole struct.
    const bool drawable = format == ClockFormat::TimeSeconds
                              ? fits(now.hour, 2) && fits(now.minute, 2) && fits(now.second, 2)
                              : fits(now.year, 4) && fits(now.month, 2) && fits(now.day, 2) && fits(now.hour, 2) && fits(now.minute, 2) && fits(now.second, 2);
    if (format != ClockFormat::Unspecified && !drawable) {
        return mdux::core::err(ReadingError::ValueTooLarge);
    }

    const auto push = [&digits, &count](std::int64_t value, std::size_t width) noexcept {
        for (std::size_t position = width; position > 0; --position) {
            std::int64_t scaled = value;
            for (std::size_t drop = 1; drop < position; ++drop) {
                scaled /= 10;
            }
            digits[count] = static_cast<std::uint8_t>(scaled % 10);
            ++count;
        }
    };

    switch (format) {
        case ClockFormat::TimeSeconds:
            push(now.hour, 2);
            push(now.minute, 2);
            push(now.second, 2);
            break;
        case ClockFormat::DateTimeSeconds:
            push(now.year, 4);
            push(now.month, 2);
            push(now.day, 2);
            push(now.hour, 2);
            push(now.minute, 2);
            push(now.second, 2);
            break;
        case ClockFormat::Unspecified:
            // `validatePayload()` refuses this at compile time and a screen built by hand at run
            // time never met that `static_assert`. `rendering()` returns nothing for it, so the
            // pattern check above has already refused; this case exists so that adding an
            // enumerator is a warning here rather than a clock that draws a previous format's shape.
            return mdux::core::err(ReadingError::PatternEmpty);
    }

    const RenderedPattern rendered = render(pattern, PatternKind::Clock, std::span{digits}.first(count));
    return recordRendered(list, font, node, rendered, color);
}

}  // namespace mdux::medui
