/**
 * @file Draw.cpp
 * @brief Implementation of the governed-zone coverage draw path.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * See Draw.cppm for why replaying a baked run is not the layout ADR-010 forbids, and for the
 * record layout this file decodes.
 */
module;

module mdux.text.draw;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;

namespace mdux::text::draw {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;

std::string_view describe(DrawTextError error) noexcept {
    switch (error) {
        case DrawTextError::PartialRecord:
            return "the run's byte length is not a whole number of records";
        case DrawTextError::RecordSizeWrong:
            return "a single-record span is not exactly recordSize bytes";
        case DrawTextError::GlyphIndexOutOfRange:
            return "a record names a glyph index the font package does not contain";
        case DrawTextError::EmptyAtlas:
            return "the font package's atlas has zero extent, so no texture coordinate exists";
        case DrawTextError::ListRejected:
            return "the draw list refused a rectangle (budget, clip, or degenerate extent)";
    }
    return "unknown text draw error";
}

Result<GlyphPlacement, DrawTextError> decodeRecord(std::span<const std::byte> record) noexcept {
    if (record.size() != recordSize) {
        // Exactly one record, not "at least one". Accepting a longer span would silently decode
        // the first record of a whole run and discard the rest, which is a plausible-looking
        // wrong answer rather than a failure - and passing the run instead of a record is the
        // most likely way to misuse this.
        return err(DrawTextError::RecordSizeWrong);
    }
    // Little-endian, spelled out rather than memcpy'd over the struct: the sidecar is committed
    // bytes compared across toolchains, so the decode must not inherit the host's byte order or
    // its struct padding. Two toolchains disagreeing here would move glyphs, not fail.
    const auto byteAt = [record](std::size_t index) noexcept {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(record[index]));
    };
    const auto glyphIndex = static_cast<std::uint16_t>(byteAt(0) | (byteAt(1) << 8));
    const auto rawX       = static_cast<std::uint16_t>(byteAt(2) | (byteAt(3) << 8));
    const auto rawY       = static_cast<std::uint16_t>(byteAt(4) | (byteAt(5) << 8));
    return GlyphPlacement{.packageIndex = glyphIndex,
                          // bit_cast rather than a cast chain: x and y are signed, and a glyph
                          // left of the run's origin is ordinary rather than exceptional.
                          .x = static_cast<mdux::core::Px>(std::bit_cast<std::int16_t>(rawX)),
                          .y = static_cast<mdux::core::Px>(std::bit_cast<std::int16_t>(rawY))};
}

ResultVoid<DrawTextError> addGlyphRect(mdux::draw::DrawList& list, const mdux::font::FontPackage& package,
                                       const mdux::font::GlyphRecord& glyph, mdux::core::Px penX,
                                       mdux::core::Px baselineY, mdux::core::ColorRgba8 color) noexcept {
    if (package.atlas.width == 0 || package.atlas.height == 0) {
        return err(DrawTextError::EmptyAtlas);
    }
    if (glyph.isBlank()) {
        // A space. Not an error, and not a rectangle: addRect() refuses a degenerate extent, and
        // the advance the baker already applied means skipping it moves nothing.
        return {};
    }

    // Place the bitmap against the pen. `bitmapOriginY` is measured *up* from the baseline, and
    // the draw path's y axis points down - so it is subtracted. Getting that inversion wrong
    // renders text that looks correct in isolation and sits at the wrong height beside anything
    // else, which is why it is one line with a comment rather than folded into the expression.
    const mdux::core::Rect rect{.x      = penX + glyph.bitmapOriginX,
                                .y      = baselineY - glyph.bitmapOriginY,
                                .width  = static_cast<mdux::core::Px>(glyph.width),
                                .height = static_cast<mdux::core::Px>(glyph.height)};

    // Texel slot to normalised coordinates. The division is what the integer `addRect` overload
    // cannot express; see Draw.cppm.
    const auto sheetWidth  = static_cast<float>(package.atlas.width);
    const auto sheetHeight = static_cast<float>(package.atlas.height);
    const mdux::draw::UvRect uv{.u0 = static_cast<float>(glyph.x) / sheetWidth,
                                .v0 = static_cast<float>(glyph.y) / sheetHeight,
                                .u1 = static_cast<float>(glyph.x + glyph.width) / sheetWidth,
                                .v1 = static_cast<float>(glyph.y + glyph.height) / sheetHeight};

    if (auto added = list.addRect(rect, color, mdux::draw::DrawMode::CoverageR8, uv); !added.has_value()) {
        // The list's own error is not forwarded: a budget overflow and a degenerate extent are
        // both "this rectangle was not recorded" to a caller drawing text, and the list's code is
        // already the actionable one for whoever sized the budget.
        return err(DrawTextError::ListRejected);
    }
    return {};
}

ResultVoid<DrawTextError> recordRun(mdux::draw::DrawList& list, const mdux::font::FontPackage& package,
                                    std::span<const std::byte> records, mdux::core::Px originX,
                                    mdux::core::Px originY, mdux::core::ColorRgba8 color) noexcept {
    if (records.size() % recordSize != 0) {
        // Checked before anything is recorded. A run that is one byte short would otherwise draw
        // every glyph but the last and look merely truncated, which is the failure mode
        // `mdux.text.schema` refuses to let reach a device in the first place.
        return err(DrawTextError::PartialRecord);
    }

    // The run is all-or-nothing. The length check above catches a truncated sidecar before any
    // rectangle exists, but `GlyphIndexOutOfRange`, `EmptyAtlas` and `ListRejected` can all fire
    // on the fourth record of four - and a run that drew three glyphs and then failed would put a
    // fragment of a word on screen if the caller kept the list. Marking here and rolling back
    // makes "does not reach a frame" true of the list itself rather than a caller obligation.
    const auto start = list.mark();
    const auto abort = [&list, start](DrawTextError error) {
        // The marker came from this list one statement ago, so the only way this refuses is a bug
        // in `rollback()` itself. Discarded rather than reported, because the caller is already
        // being told the run failed and `DrawTextError` has no truthful code for "and the undo
        // also failed" that would not read as a second, unrelated fault.
        static_cast<void>(list.rollback(start));
        return err(error);
    };

    for (std::size_t offset = 0; offset < records.size(); offset += recordSize) {
        auto placement = decodeRecord(records.subspan(offset, recordSize));
        if (!placement.has_value()) {
            return abort(placement.error());
        }
        if (placement->packageIndex >= package.glyphs.size()) {
            // An index into the *package's* table, not the font's. The two differ - the package
            // holds only the baked charset - and a record built against a different package would
            // otherwise draw a plausible wrong character rather than failing.
            return abort(DrawTextError::GlyphIndexOutOfRange);
        }
        const mdux::font::GlyphRecord& glyph = package.glyphs[placement->packageIndex];
        if (auto added = addGlyphRect(list, package, glyph, originX + placement->x, originY + placement->y, color);
            !added.has_value()) {
            return abort(added.error());
        }
    }
    return {};
}

}  // namespace mdux::text::draw
