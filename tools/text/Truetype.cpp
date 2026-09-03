/**
 * @file Truetype.cpp
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
// the Apple and legacy alternative containers. 'OTTO' is CFF-flavoured OpenType: it is admitted
// at this gate and rejected by the directory walk below on its 'CFF '/'CFF2' tag, so an author
// who hands the baker an OpenType/CFF font gets `CffOutlinesRejected`, which names the actual
// problem, rather than `NotATrueTypeOutlineFont`, which would only say the container word was
// unfamiliar. An 'OTTO' font carrying neither a CFF table nor glyf falls out as
// MissingRequiredTable.
constexpr std::uint32_t sfntVersionTrue    = 0x00010000u;
constexpr std::uint32_t sfntVersionTrueTag = 0x74727565u;  // 'true'
constexpr std::uint32_t sfntVersionTyp1    = 0x74797031u;  // 'typ1'
constexpr std::uint32_t sfntVersionOtto    = 0x4F54544Fu;  // 'OTTO'

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
// Bit 6 is OVERLAP_SIMPLE in the current OpenType spec - a rasteriser hint with no shaping
// semantics, which modern tooling does set - so only bit 7 is genuinely reserved. Masking 0xC0
// here would reject fonts whose only sin is having been built by a recent fontmake.
constexpr std::uint8_t flagReservedMask    = 0x80u;

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

/// Decodes a format 4 subtable - the BMP workhorse every font carries - into ranges.
///
/// Four parallel arrays of `segCount` entries, then a trailing glyph array. For a segment with
/// `idRangeOffset == 0` the mapping is affine: `glyph = code + idDelta`, so the whole segment is
/// one range. Otherwise `idRangeOffset` is a *byte* offset measured from its own slot in the
/// array - a self-relative pointer into the trailing glyph array - and each code point resolves
/// individually, so those segments expand to one range per mapped code point.
///
/// The self-relative offset is the fiddliest arithmetic in the whole format, and it is walked
/// exactly once here rather than at lookup time, so the bounds check lives in one place.
[[nodiscard]] Result<std::vector<CmapRange>, ParseError> decodeCmapFormat4(std::span<const std::byte> table) noexcept {
    const auto segCountX2 = beU16(table, 6);
    if (!segCountX2) {
        return err(ParseError::TruncatedCmap);
    }
    // segCountX2 is, as the name says, twice the segment count. An odd value is not a slightly
    // wrong count - it means the four parallel arrays are not where the layout arithmetic below
    // computes them to be, so every offset after this point would address the wrong field and the
    // table would be *misread* rather than rejected. Checking parity is what turns that into a
    // diagnostic. A legal table always has at least the mandatory 0xFFFF terminator segment.
    if (*segCountX2 == 0 || (*segCountX2 % 2u) != 0) {
        return err(ParseError::TruncatedCmap);
    }
    const std::size_t segCount = static_cast<std::size_t>(*segCountX2) / 2u;
    // endCode[segCount], reservedPad, startCode[segCount], idDelta[segCount], idRangeOffset[segCount]
    const std::size_t endCodeAt       = 14;
    const std::size_t startCodeAt     = endCodeAt + segCount * 2u + 2u;
    const std::size_t idDeltaAt       = startCodeAt + segCount * 2u;
    const std::size_t idRangeOffsetAt = idDeltaAt + segCount * 2u;
    if (idRangeOffsetAt + segCount * 2u > table.size()) {
        return err(ParseError::TruncatedCmap);
    }

    std::vector<CmapRange> ranges;
    for (std::size_t i = 0; i < segCount; ++i) {
        const auto endCode       = beU16(table, endCodeAt + i * 2u);
        const auto startCode     = beU16(table, startCodeAt + i * 2u);
        const auto idDelta       = beU16(table, idDeltaAt + i * 2u);
        const auto idRangeOffset = beU16(table, idRangeOffsetAt + i * 2u);
        if (!endCode || !startCode || !idDelta || !idRangeOffset) {
            return err(ParseError::TruncatedCmap);
        }
        if (*startCode > *endCode) {
            return err(ParseError::TruncatedCmap);
        }
        // The final segment is the mandatory 0xFFFF terminator and maps nothing useful.
        if (*startCode == 0xFFFFu) {
            continue;
        }
        if (ranges.size() > maxCmapEntries) {
            return err(ParseError::CharacterMapTooLarge);
        }

        if (*idRangeOffset == 0) {
            // Affine: one range. Glyph 0 means unmapped, so a segment that maps its start to
            // .notdef is skipped rather than recorded.
            const std::uint16_t firstGlyph = static_cast<std::uint16_t>((*startCode + *idDelta) & 0xFFFFu);
            if (firstGlyph == 0) {
                continue;
            }
            ranges.push_back(CmapRange{.first      = static_cast<char32_t>(*startCode),
                                       .last       = static_cast<char32_t>(*endCode),
                                       .firstGlyph = firstGlyph});
            continue;
        }

        for (std::uint32_t code = *startCode; code <= *endCode; ++code) {
            // The spec's own expression, kept in its original shape so it can be checked against
            // the specification rather than against a simplification of it:
            //   glyphIndexAddress = idRangeOffset[i] + 2 * (code - startCode[i]) + &idRangeOffset[i]
            const std::size_t slot    = idRangeOffsetAt + i * 2u;
            const std::size_t address = slot + *idRangeOffset + 2u * (code - *startCode);
            const auto        raw     = beU16(table, address);
            if (!raw) {
                return err(ParseError::TruncatedCmap);
            }
            if (*raw == 0) {
                continue;  // explicitly unmapped
            }
            const std::uint16_t glyph = static_cast<std::uint16_t>((*raw + *idDelta) & 0xFFFFu);
            if (glyph == 0) {
                continue;
            }
            if (ranges.size() >= maxCmapEntries) {
                return err(ParseError::CharacterMapTooLarge);
            }
            // Extend the previous range when this code point continues it, so an indirect
            // segment whose glyphs happen to be consecutive still collapses.
            if (!ranges.empty() && ranges.back().last + 1u == code
                && ranges.back().firstGlyph + (ranges.back().last - ranges.back().first) + 1u == glyph) {
                ranges.back().last = static_cast<char32_t>(code);
                continue;
            }
            ranges.push_back(CmapRange{.first = static_cast<char32_t>(code), .last = static_cast<char32_t>(code), .firstGlyph = glyph});
        }
    }
    return ranges;
}

/// Decodes a format 12 subtable: a flat list of (startCharCode, endCharCode, startGlyphID)
/// groups, already the shape `CmapRange` wants. Preferred over format 4 when present, because it
/// is the only one of the two that reaches beyond the BMP.
[[nodiscard]] Result<std::vector<CmapRange>, ParseError> decodeCmapFormat12(std::span<const std::byte> table) noexcept {
    const auto groupCount = beU32(table, 12);
    if (!groupCount) {
        return err(ParseError::TruncatedCmap);
    }
    if (*groupCount > maxCmapEntries) {
        return err(ParseError::CharacterMapTooLarge);
    }
    const std::uint64_t end = 16u + static_cast<std::uint64_t>(*groupCount) * 12u;
    if (end > table.size()) {
        return err(ParseError::TruncatedCmap);
    }
    std::vector<CmapRange> ranges;
    ranges.reserve(*groupCount);
    for (std::uint32_t g = 0; g < *groupCount; ++g) {
        const std::size_t at         = 16u + static_cast<std::size_t>(g) * 12u;
        const auto        startCode  = beU32(table, at);
        const auto        endCode    = beU32(table, at + 4);
        const auto        startGlyph = beU32(table, at + 8);
        if (!startCode || !endCode || !startGlyph) {
            return err(ParseError::TruncatedCmap);
        }
        if (*startCode > *endCode || *startGlyph > 0xFFFFu) {
            return err(ParseError::TruncatedCmap);
        }
        if (*startGlyph == 0) {
            continue;
        }
        ranges.push_back(CmapRange{.first      = static_cast<char32_t>(*startCode),
                                   .last       = static_cast<char32_t>(*endCode),
                                   .firstGlyph = static_cast<std::uint16_t>(*startGlyph)});
    }
    return ranges;
}

/// Picks a subtable out of `cmap` and decodes it.
///
/// Preference order is (3,10) format 12, then (3,1) format 4, then (0,*) - Windows full-Unicode,
/// Windows BMP, then Unicode-platform as a fallback. A font carrying only a Macintosh (1,0)
/// byte-encoding table is refused with `UnsupportedCmapFormat` rather than decoded, because that
/// table maps bytes through a legacy codepage rather than code points, and guessing the codepage
/// is exactly the kind of inference this parser refuses to make.
[[nodiscard]] Result<std::vector<CmapRange>, ParseError> decodeCmap(std::span<const std::byte> cmap) noexcept {
    const auto numTables = beU16(cmap, 2);
    if (!numTables) {
        return err(ParseError::TruncatedCmap);
    }
    if (4u + static_cast<std::size_t>(*numTables) * 8u > cmap.size()) {
        return err(ParseError::TruncatedCmap);
    }

    std::optional<std::size_t> best;
    int                        bestRank = -1;
    for (std::uint16_t i = 0; i < *numTables; ++i) {
        const std::size_t at         = 4u + static_cast<std::size_t>(i) * 8u;
        const auto        platformId = beU16(cmap, at);
        const auto        encodingId = beU16(cmap, at + 2);
        const auto        offset     = beU32(cmap, at + 4);
        if (!platformId || !encodingId || !offset) {
            return err(ParseError::TruncatedCmap);
        }
        // Widened deliberately: `*offset` is a uint32, so `*offset + 2u` is evaluated in 32-bit
        // and wraps for an offset near 0xFFFFFFFF, letting the wrapped value pass this guard. The
        // beU16() below would still refuse to read out of bounds, so nothing was reachable, but a
        // bounds check that does not hold on its own terms is one nobody can rely on later.
        if (static_cast<std::uint64_t>(*offset) + 2u > cmap.size()) {
            return err(ParseError::TruncatedCmap);
        }
        const auto format = beU16(cmap, *offset);
        if (!format) {
            return err(ParseError::TruncatedCmap);
        }
        int rank = -1;
        if (*platformId == 3 && *encodingId == 10 && *format == 12)
            rank = 3;
        else if (*platformId == 3 && *encodingId == 1 && *format == 4)
            rank = 2;
        else if (*platformId == 0 && (*format == 4 || *format == 12))
            rank = 1;
        if (rank > bestRank) {
            bestRank = rank;
            best     = *offset;
        }
    }
    if (!best.has_value() || bestRank < 0) {
        return err(ParseError::UnsupportedCmapFormat);
    }

    const auto whatFollows = cmap.subspan(*best);
    const auto format      = beU16(whatFollows, 0);
    if (!format) {
        return err(ParseError::TruncatedCmap);
    }

    // Clip the subtable to its own declared length rather than letting it run to the end of the
    // `cmap` table.
    //
    // This is not tidiness. Format 4's idRangeOffset is a self-relative pointer into a glyph array
    // that the subtable's length is what terminates - so a subtable sliced too generously lets
    // that indirection read into whatever subtable happens to follow it. A font with several cmap
    // subtables, which is the normal case, would then produce *wrong mappings* rather than a
    // diagnostic: the reads stay inside `cmap`, so no bounds check fires and the glyphs are simply
    // someone else's. Silently drawing the wrong character is the worst failure this parser has.
    //
    // The two formats declare their length in different places and widths: format 4 as a uint16
    // at offset 2, format 12 as a uint32 at offset 4.
    std::uint64_t declaredLength = 0;
    if (*format == 12) {
        const auto length = beU32(whatFollows, 4);
        if (!length) {
            return err(ParseError::TruncatedCmap);
        }
        declaredLength = *length;
    } else {
        const auto length = beU16(whatFollows, 2);
        if (!length) {
            return err(ParseError::TruncatedCmap);
        }
        declaredLength = *length;
    }
    if (declaredLength == 0 || declaredLength > whatFollows.size()) {
        return err(ParseError::TruncatedCmap);
    }
    const auto subtable = whatFollows.first(static_cast<std::size_t>(declaredLength));

    auto ranges = (*format == 12) ? decodeCmapFormat12(subtable) : decodeCmapFormat4(subtable);
    if (!ranges.has_value()) {
        return err(ranges.error());
    }
    // Sorted so glyphForCodePoint() can binary-search. Format 12 groups and format 4 segments are
    // both specified as ascending, but sorting rather than trusting that is one line and removes
    // a silent-wrong-answer path for a font that violates it.
    std::sort(ranges->begin(), ranges->end(), [](const CmapRange& a, const CmapRange& b) noexcept { return a.first < b.first; });
    return ranges;
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
        case ParseError::TruncatedHead:
            return "'head' table is shorter than 54 bytes";
        case ParseError::HeadBadMagicNumber:
            return "'head' magic check failed (expected 0x5F0F3CF5)";
        case ParseError::UnsupportedUnitsPerEm:
            return "unitsPerEm is outside the specification's [16, 16384] range";
        case ParseError::TruncatedMaxp:
            return "'maxp' table is shorter than 6 bytes";
        case ParseError::UnsupportedLocaFormat:
            return "head.indexToLocFormat is neither 0 (short) nor 1 (long)";
        case ParseError::LocaSizeMismatch:
            return "'loca' is too short to hold numGlyphs+1 entries";
        case ParseError::LocaOutOfBounds:
            return "a 'loca' entry extends past the end of 'glyf'";
        case ParseError::GlyphIndexOutOfRange:
            return "parseGlyph called with an index >= numGlyphs";
        case ParseError::TruncatedGlyph:
            return "glyf record is non-empty but shorter than the 10-byte header";
        case ParseError::CompositeGlyphRejected:
            return "numberOfContours == -1; composites are out of v1 scope";
        case ParseError::TruncatedContourEndpoints:
            return "endPtsOfContours or the instruction length field extends past the glyf record";
        case ParseError::TruncatedGlyphInstructions:
            return "the hinting instruction array extends past the glyf record";
        case ParseError::TruncatedGlyphFlags:
            return "the flag array extends past the glyf record";
        case ParseError::GlyphFlagRepeatOverrun:
            return "a REPEAT_FLAG count claims more points than endPtsOfContours declares";
        case ParseError::TruncatedGlyphCoords:
            return "x or y coordinates extend past the glyf record";
        case ParseError::UnsupportedGlyphFlag:
            return "a flag value has reserved bit 7 set";
        case ParseError::CoordinateOverflow:
            return "an accumulated coordinate left the int16 range the specification allows";
        case ParseError::NonMonotonicContours:
            return "endPtsOfContours is not strictly increasing";
        case ParseError::MissingCharacterMap:
            return "'cmap' is absent; no code point can be resolved to a glyph";
        case ParseError::TruncatedCmap:
            return "a 'cmap' header, encoding record or subtable extends past the table";
        case ParseError::UnsupportedCmapFormat:
            return "'cmap' carries no subtable in a supported format (4 or 12, Unicode-encoded)";
        case ParseError::CharacterMapTooLarge:
            return "'cmap' declares more mappings than the supported maximum";
        case ParseError::MissingHorizontalMetrics:
            return "'hhea' or 'hmtx' is absent; no glyph has a known advance width";
        case ParseError::TruncatedHhea:
            return "'hhea' table is shorter than 36 bytes";
        case ParseError::TruncatedHmtx:
            return "'hmtx' is shorter than hhea.numberOfHMetrics requires";
        case ParseError::UnsupportedMetricCount:
            return "hhea.numberOfHMetrics is zero or exceeds maxp.numGlyphs";
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
    std::optional<std::pair<std::uint32_t, std::uint32_t>> cmap;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> hhea;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> hmtx;
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

        // A CFF/CFF2 font is refused here rather than falling out as MissingRequiredTable: its
        // outlines live in a table this parser does not read at all, so "no glyf" is a symptom
        // and "these are CFF outlines" is the diagnosis the author needs.
        //
        // GPOS, GSUB, GDEF and the Apple AAT tables morx/mort are deliberately *not* refused.
        // ADR-010 forbids on-device shaping, and this walk is what makes that true: those tags
        // fall through the tag ladder below, so their bytes are never sliced, never read and
        // never reach a `Font`. A baker that cannot see a table cannot bake it into an artifact,
        // and a runtime handed that artifact has nothing to apply. Refusing on presence instead
        // would refuse every font a designer has ever shipped (8 of 8 stock DejaVu faces carry
        // both GPOS and GSUB) while adding no safety this omission does not already provide.
        if (tagStr == "CFF " || tagStr == "CFF2")
            return err(ParseError::CffOutlinesRejected);

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
        else if (tagStr == "cmap")
            out.cmap = record;
        else if (tagStr == "hhea")
            out.hhea = record;
        else if (tagStr == "hmtx")
            out.hmtx = record;
    }
    return out;
}

[[nodiscard]] Result<std::vector<std::uint32_t>, ParseError>
readLoca(std::span<const std::byte> loca, std::uint16_t numGlyphs, std::uint16_t indexToLocFormat, std::uint32_t glyfLength) noexcept {
    // numGlyphs + 1 entries; short form is uint16 × 2 (the storage value is half of the byte
    // offset), long form is uint32 straight. The check below is `<`, not `!=`, deliberately: a
    // table longer than numGlyphs+1 entries is well-formed, because sfnt pads every table to a
    // 4-byte boundary and a short-format loca with an even entry count picks up two trailing
    // bytes. Only a table too short to hold the entries the directory promises is a defect, so
    // `LocaSizeMismatch` reads "too short for numGlyphs+1 entries", not "not exactly".
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
    if (*sfnt != sfntVersionTrue && *sfnt != sfntVersionTrueTag && *sfnt != sfntVersionTyp1 && *sfnt != sfntVersionOtto) {
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
    // `cmap` and `hhea`/`hmtx` are required rather than optional, and the codes are distinct from
    // MissingRequiredTable so an author learns which capability is missing. Without `cmap` a
    // recipe cannot name a character; without `hmtx` a baked glyph has no advance, and ADR-010
    // defines the baked unit as a glyph at a known slot *with a known advance*. A font missing
    // either is not one this pipeline can bake, whatever its outlines look like.
    if (!directory->cmap.has_value()) {
        return err(ParseError::MissingCharacterMap);
    }
    if (!directory->hhea.has_value() || !directory->hmtx.has_value()) {
        return err(ParseError::MissingHorizontalMetrics);
    }

    const auto [headOffset, headLength] = *directory->head;
    const auto [maxpOffset, maxpLength] = *directory->maxp;
    const auto [locaOffset, locaLength] = *directory->loca;
    const auto [glyfOffset, glyfLength] = *directory->glyf;
    const auto [cmapOffset, cmapLength] = *directory->cmap;
    const auto [hheaOffset, hheaLength] = *directory->hhea;
    const auto [hmtxOffset, hmtxLength] = *directory->hmtx;

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
    // The OpenType specification's own range for head.unitsPerEm. Narrowing it further would
    // reject spec-valid fonts (an 8192-upem face is legal and not unusual for display faces)
    // with no ADR behind the narrowing - the baker scales to the atlas' pixel grid anyway, so
    // a larger em square costs nothing downstream.
    if (*unitsPerEm < 16 || *unitsPerEm > 16384) {
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

    // hhea's numberOfHMetrics sits at offset 34, the last field of a 36-byte table.
    auto hhea = bytes.subspan(hheaOffset, hheaLength);
    if (hhea.size() < 36u) {
        return err(ParseError::TruncatedHhea);
    }
    const auto numberOfHMetrics = beU16(hhea, 34);
    if (!numberOfHMetrics) {
        return err(ParseError::TruncatedHhea);
    }
    if (*numberOfHMetrics == 0 || *numberOfHMetrics > *numGlyphs) {
        return err(ParseError::UnsupportedMetricCount);
    }
    auto hmtx = bytes.subspan(hmtxOffset, hmtxLength);
    // numberOfHMetrics full pairs, then one int16 bearing for each remaining glyph.
    const std::uint64_t hmtxNeeded = static_cast<std::uint64_t>(*numberOfHMetrics) * 4u
                                     + (static_cast<std::uint64_t>(*numGlyphs) - *numberOfHMetrics) * 2u;
    if (hmtx.size() < hmtxNeeded) {
        return err(ParseError::TruncatedHmtx);
    }
    font.numberOfHMetrics = *numberOfHMetrics;
    font.hmtx             = hmtx;

    auto ranges = decodeCmap(bytes.subspan(cmapOffset, cmapLength));
    if (!ranges.has_value()) {
        return err(ranges.error());
    }
    font.characterMap = std::move(*ranges);

    return font;
}

namespace {

/// Checked add of two int16s, used to accumulate the TrueType coordinate deltas that are
/// themselves int16. The spec stores coordinates as absolute int16 values bounded by the `head`
/// bounding box, so an accumulation that leaves int16 can only come from malformed input - and
/// this file's contract is that anything it does not understand is a refusal with a specific
/// code rather than a guess. Returning nullopt (which the caller turns into
/// `CoordinateOverflow`) is the version of that contract for coordinates: a clamped value would
/// be a plausible-looking but wrong outline, which S4 would then bake and byte-commit as
/// evidence.
[[nodiscard]] std::optional<std::int16_t> checkedAdd(std::int16_t a, std::int16_t b) noexcept {
    const auto sum = static_cast<std::int32_t>(a) + static_cast<std::int32_t>(b);
    if (sum > static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())
        || sum < static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min())) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(sum);
}

}  // namespace

Result<SimpleGlyph, ParseError> parseGlyph(const Font& font, std::uint16_t glyphIndex) noexcept {
    if (glyphIndex >= font.numGlyphs) {
        return err(ParseError::GlyphIndexOutOfRange);
    }
    // `Font` is an exported aggregate with public fields, so this can be one a caller built or
    // mutated rather than one `parse()` returned. `parse()` guarantees numGlyphs+1 loca entries;
    // re-checking costs one comparison and is what keeps the two indexes below from being
    // std::vector::operator[] out of range, which is UB rather than a diagnostic.
    if (font.loca.size() < static_cast<std::size_t>(font.numGlyphs) + 1u) {
        return err(ParseError::LocaSizeMismatch);
    }

    const std::uint32_t start = font.loca[glyphIndex];
    const std::uint32_t end   = font.loca[glyphIndex + 1];
    if (start > font.glyf.size() || end > font.glyf.size() || start > end) {
        return err(ParseError::LocaOutOfBounds);
    }
    const auto span = font.glyf.subspan(start, end - start);

    // A zero-length record - loca[i] == loca[i+1] - is how TrueType spells "this glyph has no
    // outline". It is the mandated encoding for the space character and for every other blank,
    // not a truncation: DejaVuSans stores 63 of its 6253 glyphs this way, gid 3 (space) among
    // them. It has to be checked before the 10-byte header bound, because there is no header.
    if (span.empty()) {
        SimpleGlyph blank;
        blank.glyphIndex = glyphIndex;
        return blank;
    }
    if (span.size() < 10) {
        return err(ParseError::TruncatedGlyph);
    }

    SimpleGlyph glyph;
    glyph.glyphIndex = glyphIndex;
    // The five int16 header fields are all inside the 10 bytes the guard above secured, so these
    // five reads cannot fail; dereferencing without a second check is safe here and nowhere else.
    const auto numberOfContours = *beI16(span, 0);
    glyph.xMin                  = *beI16(span, 2);
    glyph.yMin                  = *beI16(span, 4);
    glyph.xMax                  = *beI16(span, 6);
    glyph.yMax                  = *beI16(span, 8);

    if (numberOfContours < 0) {
        return err(ParseError::CompositeGlyphRejected);
    }
    const auto contours = static_cast<std::size_t>(numberOfContours);

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

    // instructionLength and the hinting bytecode that follows it. The bytecode is stepped over,
    // not read: the runtime has no hinting engine (ADR-010 decision 4), so the bytes have no
    // consumer downstream, and a font whose only unsupported feature is that its designer ran
    // ttfautohint is a font this parser can still produce an outline from. Only the step itself
    // is checked, so a declared length that runs past the record is still a diagnostic.
    std::size_t cursor            = endPtsEnd;
    const auto  instructionLength = *beU16(span, cursor);  // the endPtsEnd + 2 guard secured this
    cursor += 2u;
    if (instructionLength > span.size() - cursor) {
        return err(ParseError::TruncatedGlyphInstructions);
    }
    cursor += instructionLength;

    // An empty glyph (numberOfContours == 0) declares no end points, so it has no points either
    // and the flag and coordinate loops below run zero times, leaving both vectors empty.
    const std::size_t numPoints = (contours == 0) ? 0u : static_cast<std::size_t>(previousEndPoint) + 1u;

    // Flags: one byte per point, with the REPEAT_FLAG encoding (the next byte is the count of
    // additional repetitions). The loop fills exactly numPoints flags; an abbreviated repeat or
    // an absent byte fails with TruncatedGlyphFlags, and a repeat count claiming more points
    // than endPtsOfContours declared fails with GlyphFlagRepeatOverrun rather than being
    // silently truncated - an over-long repeat means the flag stream and the contour list
    // disagree about how many points the glyph has, and the parser cannot know which is right.
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
            if (static_cast<std::size_t>(repeatCount) > numPoints - flags.size()) {
                return err(ParseError::GlyphFlagRepeatOverrun);
            }
            flags.insert(flags.end(), repeatCount, flag);
        }
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
            const std::int32_t signedDelta = sameOrPositive ? static_cast<std::int32_t>(raw)
                                                            : -static_cast<std::int32_t>(raw);
            const std::int16_t delta = static_cast<std::int16_t>(signedDelta);
            const auto         next  = checkedAdd(x, delta);
            if (!next) {
                return err(ParseError::CoordinateOverflow);
            }
            x = *next;
        } else if (!sameOrPositive) {
            const auto v = beI16(span, cursor);
            if (!v) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            cursor          += 2u;
            const auto next  = checkedAdd(x, *v);
            if (!next) {
                return err(ParseError::CoordinateOverflow);
            }
            x = *next;
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
            const std::int32_t signedDelta = sameOrPositive ? static_cast<std::int32_t>(raw)
                                                            : -static_cast<std::int32_t>(raw);
            const std::int16_t delta = static_cast<std::int16_t>(signedDelta);
            const auto         next  = checkedAdd(y, delta);
            if (!next) {
                return err(ParseError::CoordinateOverflow);
            }
            y = *next;
        } else if (!sameOrPositive) {
            const auto v = beI16(span, cursor);
            if (!v) {
                return err(ParseError::TruncatedGlyphCoords);
            }
            cursor          += 2u;
            const auto next  = checkedAdd(y, *v);
            if (!next) {
                return err(ParseError::CoordinateOverflow);
            }
            y = *next;
        }
        yCoords.push_back(y);
    }

    for (std::size_t i = 0; i < numPoints; ++i) {
        glyph.points.push_back(GlyphPoint{.x = xCoords[i], .y = yCoords[i], .onCurve = (flags[i] & flagOnCurve) != 0u});
    }
    return glyph;
}

std::optional<std::uint16_t> glyphForCodePoint(const Font& font, char32_t codePoint) noexcept {
    // Ranges are sorted and non-overlapping, so the last one starting at or below the code point
    // is the only candidate.
    const auto it = std::upper_bound(font.characterMap.begin(), font.characterMap.end(), codePoint,
                                     [](char32_t value, const CmapRange& range) noexcept { return value < range.first; });
    if (it == font.characterMap.begin()) {
        return std::nullopt;
    }
    const CmapRange& range = *std::prev(it);
    if (codePoint > range.last) {
        return std::nullopt;
    }
    const std::uint32_t glyph = static_cast<std::uint32_t>(range.firstGlyph) + (codePoint - range.first);
    if (glyph >= font.numGlyphs) {
        // A cmap that names a glyph the font does not have. Refusing beats returning an index
        // parseGlyph() would then reject with a less specific code.
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(glyph);
}

Result<GlyphMetrics, ParseError> metricsFor(const Font& font, std::uint16_t glyphIndex) noexcept {
    if (glyphIndex >= font.numGlyphs) {
        return err(ParseError::GlyphIndexOutOfRange);
    }
    if (font.numberOfHMetrics == 0) {
        return err(ParseError::UnsupportedMetricCount);
    }
    // Past the last full pair, the advance is the last one stored and only the bearing is
    // per-glyph - the format's compression for a trailing run of equal-width glyphs.
    const bool          hasOwnAdvance = glyphIndex < font.numberOfHMetrics;
    const std::uint16_t advanceSlot   = hasOwnAdvance ? glyphIndex : static_cast<std::uint16_t>(font.numberOfHMetrics - 1u);
    const auto          advance       = beU16(font.hmtx, static_cast<std::size_t>(advanceSlot) * 4u);
    if (!advance) {
        return err(ParseError::TruncatedHmtx);
    }
    const std::size_t bearingAt = hasOwnAdvance
                                      ? static_cast<std::size_t>(glyphIndex) * 4u + 2u
                                      : static_cast<std::size_t>(font.numberOfHMetrics) * 4u
                                            + (static_cast<std::size_t>(glyphIndex) - font.numberOfHMetrics) * 2u;
    const auto bearing = beI16(font.hmtx, bearingAt);
    if (!bearing) {
        return err(ParseError::TruncatedHmtx);
    }
    return GlyphMetrics{.advanceWidth = *advance, .leftSideBearing = *bearing};
}

}  // namespace mdux::tools::truetype
