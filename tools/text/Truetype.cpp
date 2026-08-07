/**
 * @brief Implementation of the host-only TrueType (glyf) parser.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * Two passes. `parse()` walks the offset table once and the table directory once, building the
 * bounds a `Font` keeps for every per-glyph lookup that follows. `parseGlyph()` walks one glyf
 * record against that `Font`, taking the bounds checks in the order they sit in the buffer so a
 * malformed record fails with the first problem it has rather than one several bytes further on.
 *
 * Every read goes through `beU16()`, `beI16()` or `beU32()`, which are bounds-checked against
 * the buffer they were given - TrueType is big-endian, hence the "be" prefix. A glyph whose
 * coordinate stream the buffer cannot reach fails with `TruncatedGlyphCoords` rather than running
 * off the end. That is the one invariant this file actually needs to enforce, for the same
 * reason `Spirv.cpp` does: this tool is also the first thing a malformed or truncated file
 * reaches, and it has to fail with a diagnostic rather than read past its buffer.
 */
module;

module mdux.tools.truetype;

import std;
import mdux.core.result;

namespace mdux::tools::truetype {

using mdux::core::err;
using mdux::core::Result;

namespace {

// Recognised sfnt version words. 0x00010000 is the canonical TrueType ID; 'true' and 'typ1' are
// the Apple and legacy alternative containers; 'OTTO' (0x4F54544F) is CFF-flavoured OpenType and
// rejected here only if it carries a CFF table, which the directory walk below spots.
constexpr std::uint32_t sfntVersionTrue    = 0x00010000u;
constexpr std::uint32_t sfntVersionTrueTag = 0x74727565u;  // 'true'
constexpr std::uint32_t sfntVersionTyp1    = 0x74797031u;  // 'typ1'

constexpr std::uint32_t headMagic = 0x5F0F3CF5u;

// head bytes we read.
constexpr std::size_t headMagicOffset            = 12;
constexpr std::size_t headUnitsPerEmOffset       = 18;
constexpr std::size_t headIndexToLocFormatOffset = 50;
constexpr std::size_t headMinSize                = 54;

// maxp bytes we read.
constexpr std::size_t maxpNumGlyphsOffset = 4;
constexpr std::size_t maxpMinSize         = 6;

constexpr std::size_t offsetTableSize = 12;
constexpr std::size_t tableRecordSize = 16;

// glyf flag bits per the TrueType specification.
constexpr std::uint8_t flagOnCurve         = 0x01u;
constexpr std::uint8_t flagXShort          = 0x02u;
constexpr std::uint8_t flagYShort          = 0x04u;
constexpr std::uint8_t flagRepeat          = 0x08u;
constexpr std::uint8_t flagXSameOrPositive = 0x10u;
constexpr std::uint8_t flagYSameOrPositive = 0x20u;
constexpr std::uint8_t flagReservedMask    = 0xC0u;

[[nodiscard]] std::optional<std::uint16_t> beU16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    if (offset + 2 > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 8)
                                      | static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])));
}

[[nodiscard]] std::optional<std::int16_t> beI16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    auto u = beU16(bytes, offset);
    if (!u) {
        return std::nullopt;
    }
    return std::bit_cast<std::int16_t>(*u);
}

[[nodiscard]] std::optional<std::uint32_t> beU32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    if (offset + 4 > bytes.size()) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24)
           | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16)
           | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8)
           | static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

/// A 32-bit table tag, rendered as the four ASCII characters the TrueType container spells it
/// with. Comparing as a string lets the directory walk below write `tagStr == "GSUB"` rather than
/// juggling the equivalent 0x47535542 literal.
[[nodiscard]] std::string tagToString(std::uint32_t tag) noexcept {
    char chars[5] = {0};
    chars[0]      = static_cast<char>((tag >> 24) & 0xffu);
    chars[1]      = static_cast<char>((tag >> 16) & 0xffu);
    chars[2]      = static_cast<char>((tag >> 8) & 0xffu);
    chars[3]      = static_cast<char>((tag >> 0) & 0xffu);
    return std::string{chars, 4};
}

}  // namespace

std::string_view describe(ParseError error) noexcept {
    switch (error) {
        case ParseError::Empty:
            return "font is empty";
        case ParseError::NotATrueTypeOutlineFont:
            return "sfnt version is not a recognised TrueType outline container";
        case ParseError::TruncatedOffsetTable:
            return "offset table is shorter than the 12-byte header";
        case ParseError::TruncatedTableDirectory:
            return "a table directory record extends past the end of the file";
        case ParseError::DuplicateTable:
            return "a table tag appears more than once in the directory";
        case ParseError::MissingRequiredTable:
            return "a required table (head, maxp, glyf, loca) is absent";
        case ParseError::TableOutOfBounds:
            return "a table's offset + length exceeds the file size";
        case ParseError::CffOutlinesRejected:
            return "a 'CFF ' or 'CFF2' table is present - this is a CFF outline font, not glyf";
        case ParseError::GposRejected:
            return "a 'GPOS' table is present; glyph positioning is out of v1 scope";
        case ParseError::GsubRejected:
            return "a 'GSUB' table is present; ligatures and substitutions are out of v1 scope";
        case ParseError::AppleLayoutRejected:
            return "an 'morx' or 'mort' table is present; Apple AAT layout is out of v1 scope";
        case ParseError::TruncatedHead:
            return "'head' table is shorter than 54 bytes";
        case ParseError::HeadBadMagicNumber:
            return "'head' magic check failed (expected 0x5F0F3CF5)";
        case ParseError::UnsupportedUnitsPerEm:
            return "unitsPerEm is zero or outside the supported [16, 4096] range";
        case ParseError::TruncatedMaxp:
            return "'maxp' table is shorter than 6 bytes";
        case ParseError::UnsupportedLocaFormat:
            return "head.indexToLocFormat is neither 0 (short) nor 1 (long)";
        case ParseError::LocaSizeMismatch:
            return "'loca' byte length does not match numGlyphs+1 entries";
        case ParseError::LocaOutOfBounds:
            return "a 'loca' entry extends past the end of 'glyf'";
        case ParseError::GlyphIndexOutOfRange:
            return "parseGlyph called with an index >= numGlyphs";
        case ParseError::TruncatedGlyph:
            return "glyf record is shorter than the 10-byte header";
        case ParseError::CompositeGlyphRejected:
            return "numberOfContours == -1; composites are out of v1 scope";
        case ParseError::HintingRejected:
            return "instructionLength > 0; on-device hinting is out of v1 scope";
        case ParseError::TruncatedContourEndpoints:
            return "endPtsOfContours or the instruction length field extends past the glyf record";
        case ParseError::TruncatedGlyphFlags:
            return "the flag array extends past the glyf record";
        case ParseError::TruncatedGlyphCoords:
            return "x or y coordinates extend past the glyf record";
        case ParseError::UnsupportedGlyphFlag:
            return "a flag value has reserved bits 6 or 7 set";
        case ParseError::NonMonotonicContours:
            return "endPtsOfContours is not strictly increasing";
    }
    return "unknown TrueType parse error";
}

namespace {

/// What the directory walk found, kept in a struct so the function does not return seven
/// optionals on a single path. Each `found` field is the tag's offset+length pair after the
/// bounds check the walk performs inline, so a caller can subspan directly without re-checking.
struct Directory {
    std::optional<std::pair<std::uint32_t, std::uint32_t>> head;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> maxp;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> loca;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> glyf;
};

[[nodiscard]] Result<Directory, ParseError> walkDirectory(std::span<const std::byte> bytes, std::uint16_t numTables) noexcept {
    Directory             out;
    std::set<std::string> seen;
    for (std::uint16_t i = 0; i < numTables; ++i) {
        const std::size_t recordOffset = offsetTableSize + static_cast<std::size_t>(i) * tableRecordSize;
        const auto        tag          = beU32(bytes, recordOffset);
        const auto        tableOffset  = beU32(bytes, recordOffset + 8);
        const auto        tableLength  = beU32(bytes, recordOffset + 12);
        if (!tag || !tableOffset || !tableLength) {
            return err(ParseError::TruncatedTableDirectory);
        }
        const std::string tagStr = tagToString(*tag);

        if (auto [iter, inserted] = seen.insert(tagStr); !inserted) {
            return err(ParseError::DuplicateTable);
        }

        // Reject the unsupported layout/outline tables up front: the structure check at the top
        // is not enough to refuse a font whose presence would force the runtime to do shaping,
        // and the catalog of structural categories is the parser's first enforcement point.
        if (tagStr == "CFF " || tagStr == "CFF2")
            return err(ParseError::CffOutlinesRejected);
        if (tagStr == "GPOS")
            return err(ParseError::GposRejected);
        if (tagStr == "GSUB")
            return err(ParseError::GsubRejected);
        if (tagStr == "morx" || tagStr == "mort")
            return err(ParseError::AppleLayoutRejected);

        // Bounds-check the record's slice before storing it; a later subspan() takes the validated
        // length without re-checking, which is the whole point of doing the walk in pass one.
        const auto end = static_cast<std::uint64_t>(*tableOffset) + static_cast<std::uint64_t>(*tableLength);
        if (end > bytes.size()) {
            return err(ParseError::TableOutOfBounds);
        }

        const auto record = std::make_pair(*tableOffset, *tableLength);
        if (tagStr == "head")
            out.head = record;
        else if (tagStr == "maxp")
            out.maxp = record;
        else if (tagStr == "loca")
            out.loca = record;
        else if (tagStr == "glyf")
            out.glyf = record;
    }
    return out;
}

[[nodiscard]] Result<std::vector<std::uint32_t>, ParseError>
readLoca(std::span<const std::byte> loca, std::uint16_t numGlyphs, std::uint16_t indexToLocFormat, std::uint32_t glyfLength) noexcept {
    // numGlyphs + 1 entries; short form is uint16 × 2 (the storage value is half of the byte
    // offset), long form is uint32 straight. The byte length is the size check, so a font that
    // author declares one glyph but ships a two-entry short loca (4 bytes total) is well-formed.
    const std::size_t entryCount  = static_cast<std::size_t>(numGlyphs) + 1u;
    const std::size_t elementSize = (indexToLocFormat == 0) ? 2u : 4u;
    if (loca.size() < entryCount * elementSize) {
        return err(ParseError::LocaSizeMismatch);
    }

    std::vector<std::uint32_t> offsets;
    offsets.reserve(entryCount);
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (elementSize == 2) {
            // Short form: the uint16 is half of the byte offset; spec defines it as
            // `actualOffset / 2`, so a stored 0x0008 means offset 16 into glyf.
            auto v = beU16(loca, i * 2);
            if (!v) {
                return err(ParseError::LocaSizeMismatch);  // size guard has already passed; defensive
            }
            offsets.push_back(static_cast<std::uint32_t>(*v) * 2u);
        } else {
            auto v = beU32(loca, i * 4);
            if (!v) {
                return err(ParseError::LocaSizeMismatch);
            }
            offsets.push_back(*v);
        }
        if (offsets.back() > glyfLength) {
            return err(ParseError::LocaOutOfBounds);
        }
    }
    return offsets;
}

}  // namespace

Result<Font, ParseError> parse(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return err(ParseError::Empty);
    }
    if (bytes.size() < offsetTableSize) {
        return err(ParseError::TruncatedOffsetTable);
    }

    const auto sfnt = beU32(bytes, 0);
    if (!sfnt) {
        return err(ParseError::TruncatedOffsetTable);
    }
    if (*sfnt != sfntVersionTrue && *sfnt != sfntVersionTrueTag && *sfnt != sfntVersionTyp1) {
        return err(ParseError::NotATrueTypeOutlineFont);
    }

    const auto numTables = beU16(bytes, 4);
    if (!numTables) {
        return err(ParseError::TruncatedOffsetTable);
    }
    // A whole-table directory of numTables records sits right after the 12-byte offset table.
    // An `operator>` here that does not multiply into 64-bit could itself overflow on a malformed
    // numTables; cast through the 64-bit to take the safe path.
    const auto directoryEnd = static_cast<std::uint64_t>(offsetTableSize) + static_cast<std::uint64_t>(*numTables) * tableRecordSize;
    if (directoryEnd > bytes.size()) {
        return err(ParseError::TruncatedTableDirectory);
    }

    auto directory = walkDirectory(bytes, *numTables);
    if (!directory.has_value()) {
        return err(directory.error());
    }
    if (!directory->head.has_value() || !directory->maxp.has_value() || !directory->loca.has_value() || !directory->glyf.has_value()) {
        return err(ParseError::MissingRequiredTable);
    }

    const auto [headOffset, headLength] = *directory->head;
    const auto [maxpOffset, maxpLength] = *directory->maxp;
    const auto [locaOffset, locaLength] = *directory->loca;
    const auto [glyfOffset, glyfLength] = *directory->glyf;

    auto head = bytes.subspan(headOffset, headLength);
    auto maxp = bytes.subspan(maxpOffset, maxpLength);
    auto glyf = bytes.subspan(glyfOffset, glyfLength);
    auto loca = bytes.subspan(locaOffset, locaLength);

    if (head.size() < headMinSize) {
        return err(ParseError::TruncatedHead);
    }
    const auto magic = beU32(head, headMagicOffset);
    if (!magic || *magic != headMagic) {
        return err(ParseError::HeadBadMagicNumber);
    }
    const auto unitsPerEm = beU16(head, headUnitsPerEmOffset);
    if (!unitsPerEm) {
        return err(ParseError::TruncatedHead);
    }
    if (*unitsPerEm < 16 || *unitsPerEm > 4096) {
        return err(ParseError::UnsupportedUnitsPerEm);
    }
    const auto indexToLocFormat = beI16(head, headIndexToLocFormatOffset);
    if (!indexToLocFormat) {
        return err(ParseError::TruncatedHead);
    }
    if (*indexToLocFormat != 0 && *indexToLocFormat != 1) {
        return err(ParseError::UnsupportedLocaFormat);
    }

    if (maxp.size() < maxpMinSize) {
        return err(ParseError::TruncatedMaxp);
    }
    const auto numGlyphs = beU16(maxp, maxpNumGlyphsOffset);
    if (!numGlyphs) {
        return err(ParseError::TruncatedMaxp);
    }

    auto locaOffsets = readLoca(loca, *numGlyphs, static_cast<std::uint16_t>(*indexToLocFormat), static_cast<std::uint32_t>(glyfLength));
    if (!locaOffsets.has_value()) {
        return err(locaOffsets.error());
    }

    Font font;
    font.sfntVersion      = *sfnt;
    font.unitsPerEm       = *unitsPerEm;
    font.numGlyphs        = *numGlyphs;
    font.indexToLocFormat = static_cast<std::uint16_t>(*indexToLocFormat);
    font.head             = head;
    font.glyf             = glyf;
    font.loca             = std::move(*locaOffsets);
    return font;
}

namespace {

/// Saturating add of two int16s into int16, used for accumulating TrueType coordinate deltas that
/// are themselves int16. The spec stores coordinates as absolute int16 values, so an accumulation
/// that overflows int16 would already be malformed - clamp to int16_max/min rather than wrap, so a
/// malformed accumulator stays deterministic rather than producing "anything is allowed".
[[nodiscard]] std::int16_t satAdd(std::int16_t a, std::int16_t b) noexcept {
    const auto sum = static_cast<std::int32_t>(a) + static_cast<std::int32_t>(b);
    if (sum > static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())) {
        return std::numeric_limits<std::int16_t>::max();
    }
    if (sum < static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min())) {
        return std::numeric_limits<std::int16_t>::min();
    }
    return static_cast<std::int16_t>(sum);
}

}  // namespace

Result<SimpleGlyph, ParseError> parseGlyph(const Font& font, std::uint16_t glyphIndex) noexcept {
    if (glyphIndex >= font.numGlyphs) {
        return err(ParseError::GlyphIndexOutOfRange);
    }

    const std::uint32_t start = font.loca[glyphIndex];
    const std::uint32_t end   = font.loca[glyphIndex + 1];
    if (start > font.glyf.size() || end > font.glyf.size() || start > end) {
        return err(ParseError::LocaOutOfBounds);
    }
    const auto span = font.glyf.subspan(start, end - start);
    if (span.size() < 10) {
        return err(ParseError::TruncatedGlyph);
    }

    SimpleGlyph glyph;
    glyph.glyphIndex            = glyphIndex;
    const auto numberOfContours = beI16(span, 0);
    glyph.xMin                  = *beI16(span, 2);
    glyph.yMin                  = *beI16(span, 4);
    glyph.xMax                  = *beI16(span, 6);
    glyph.yMax                  = *beI16(span, 8);

    if (!numberOfContours) {
        return err(ParseError::TruncatedGlyph);
    }
    if (*numberOfContours < 0) {
        return err(ParseError::CompositeGlyphRejected);
    }
    const auto contours = static_cast<std::size_t>(*numberOfContours);

    // endPtsOfContours: contours × 2 bytes, starting at offset 10. The instruction-length field
    // follows immediately, so the bounds check below also reserves the 2 bytes for it; an attempt
    // to read the structural header past the glyf record fails with one code rather than two.
    const std::size_t endPtsEnd = 10u + contours * 2u;
    if (span.size() < endPtsEnd + 2u) {
        return err(ParseError::TruncatedContourEndpoints);
    }
    std::uint16_t previousEndPoint = 0;
    for (std::size_t c = 0; c < contours; ++c) {
        const auto ep = beU16(span, 10u + c * 2u);
        if (!ep) {
            return err(ParseError::TruncatedContourEndpoints);
        }
        if (c > 0 && *ep <= previousEndPoint) {
            return err(ParseError::NonMonotonicContours);
        }
        glyph.endPtsOfContours.push_back(*ep);
        previousEndPoint = *ep;
    }

    std::size_t cursor            = endPtsEnd;
    const auto  instructionLength = beU16(span, cursor);
    if (!instructionLength) {
        return err(ParseError::TruncatedContourEndpoints);
    }
    cursor += 2u;
    if (*instructionLength > 0) {
        return err(ParseError::HintingRejected);
    }

    // An empty glyph (numberOfContours == 0): no end points, no instructions, no flags and
    // no coordinates. Returns with empty point lists and a single zero-point contour state.
    const std::size_t numPoints = (contours == 0) ? 0u : static_cast<std::size_t>(previousEndPoint) + 1u;

    // Flags: one byte per point, with the REPEAT_FLAG encoding (the next byte is the count of
    // additional repetitions). The loop fills exactly numPoints flags; an abbreviated repeat or
    // an absent byte fails with TruncatedGlyphFlags.
    std::vector<std::uint8_t> flags;
    flags.reserve(numPoints);
    while (flags.size() < numPoints) {
        if (cursor >= span.size()) {
            return err(ParseError::TruncatedGlyphFlags);
        }
        const std::uint8_t flag = std::to_integer<std::uint8_t>(span[cursor]);
        ++cursor;
        if ((flag & flagReservedMask) != 0u) {
            return err(ParseError::UnsupportedGlyphFlag);
        }
        flags.push_back(flag);
        if ((flag & flagRepeat) != 0u) {
            if (cursor >= span.size()) {
                return err(ParseError::TruncatedGlyphFlags);
            }
            const std::uint8_t repeatCount = std::to_integer<std::uint8_t>(span[cursor]);
            ++cursor;
            for (std::uint8_t r = 0; r < repeatCount && flags.size() < numPoints; ++r) {
                flags.push_back(flag);
            }
        }
    }
    if (flags.size() != numPoints) {
        return err(ParseError::TruncatedGlyphFlags);
    }

    // X coordinates. The two flag bits are X_SHORT_VECTOR and X_IS_SAME_OR_POSITIVE; the four
    // interpretations are exactly the table in the TrueType spec, written out so the path a
    // malformed file takes through here is inspectable rather than packed into a single expression.
    std::vector<std::int16_t> xCoords;
    xCoords.reserve(numPoints);
    std::int16_t x = 0;
    for (std::size_t i = 0; i < numPoints; ++i) {
        const std::uint8_t flag           = flags[i];
        const bool         isShort        = (flag & flagXShort) != 0u;
        const bool         sameOrPositive = (flag & flagXSameOrPositive) != 0u;
        if (isShort) {
            if (cursor >= span.size()) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            const std::uint8_t raw = std::to_integer<std::uint8_t>(span[cursor]);
            ++cursor;
            const std::int16_t delta = sameOrPositive ? static_cast<std::int16_t>(static_cast<std::uint16_t>(raw))
                                                      : -static_cast<std::int16_t>(static_cast<std::uint16_t>(raw));
            x                        = satAdd(x, delta);
        } else if (!sameOrPositive) {
            const auto v = beI16(span, cursor);
            if (!v) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            cursor += 2u;
            x       = satAdd(x, *v);
        }
        // else (!short && sameOrPositive): delta is zero, no bytes consumed.
        xCoords.push_back(x);
    }

    // Y coordinates. Symmetric with X; the flag bits are Y_SHORT_VECTOR and Y_IS_SAME_OR_POSITIVE.
    std::vector<std::int16_t> yCoords;
    yCoords.reserve(numPoints);
    std::int16_t y = 0;
    for (std::size_t i = 0; i < numPoints; ++i) {
        const std::uint8_t flag           = flags[i];
        const bool         isShort        = (flag & flagYShort) != 0u;
        const bool         sameOrPositive = (flag & flagYSameOrPositive) != 0u;
        if (isShort) {
            if (cursor >= span.size()) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            const std::uint8_t raw = std::to_integer<std::uint8_t>(span[cursor]);
            ++cursor;
            const std::int16_t delta = sameOrPositive ? static_cast<std::int16_t>(static_cast<std::uint16_t>(raw))
                                                      : -static_cast<std::int16_t>(static_cast<std::uint16_t>(raw));
            y                        = satAdd(y, delta);
        } else if (!sameOrPositive) {
            const auto v = beI16(span, cursor);
            if (!v) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            cursor += 2u;
            y       = satAdd(y, *v);
        }
        yCoords.push_back(y);
    }

    for (std::size_t i = 0; i < numPoints; ++i) {
        glyph.points.push_back(GlyphPoint{.x = xCoords[i], .y = yCoords[i], .onCurve = (flags[i] & flagOnCurve) != 0u});
    }
    return glyph;
}

}  // namespace mdux::tools::truetype