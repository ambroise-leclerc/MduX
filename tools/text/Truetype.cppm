/**
 * @brief Host-only TrueType parser, glyf only: walks the offset table, head/maxp/loca and one
 *        glyf record at a time, deliberately not linking freetype or any other font SOUP.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: never linked into MduXCore or MduX)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw; this returns)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference (decision 6, mirrored for text: the host-side
 *             parser is what refuses a font whose semantics depend on a library)
 * @compliance ADR-010 No on-device text shaping (the parser is one of the host-side enforcement
 *             points; everything it rejects never reaches the runtime)
 *
 * ## Why this is hand-written rather than linked against FreeType
 *
 * The same reason the SPIR-V reflector (`tools/shader/Spirv.cpp`) is hand-written rather than
 * linked against SPIRV-Reflect, and the safetensors reader (`tools/ml/Safetensors.cpp`) is
 * hand-written rather than linked against anything: a dependency here would be SOUP in the tool
 * that produces every committed font artifact (#160, S4), and its version would become part of
 * the byte-identity contract. TrueType's binary form is small enough to walk directly, and
 * walking it directly means the bytes this repository commits depend on nothing but this file.
 *
 * It reads only what an *atlas* needs: the offset table (to find the tables that follow), the
 * `head` table (unitsPerEm, indexToLocFormat, the magic number), `maxp` (numGlyphs), `loca`
 * (per-glyph byte offsets into `glyf`) and one `glyf` record per glyph. It is not a validator
 * and does not attempt to be one; what it guarantees is that it never reads outside the buffer
 * it was given, and that anything it does not understand is a refusal with a specific code
 * rather than a guess - the same contract `Spirv.cpp` makes.
 *
 * ## v1 scope, and what gets rejected
 *
 * v1 supports TrueType `glyf` outlines and Latin/Cyrillic/Greek LTR only. The rejections the
 * parser itself emits are the ones detectable from the binary form:
 *
 * - composite glyphs (numberOfContours == -1): `CompositeGlyphRejected`
 * - CFF or CFF2 outlines (`glyf` would be absent): `CffOutlinesRejected`
 * - hinting instruction bytecode per glyph: `HintingRejected`
 * - `GPOS` table present: `GposRejected`
 * - `GSUB` table present (carries ligatures and contextual substitutions): `GsubRejected`
 * - Apple AAT layout tables `morx` / `mort` present: `AppleLayoutRejected`
 *
 * RTL and complex scripts cannot be detected from the font binary alone - they are a property
 * of the string-to-glyph mapping, which is the `.medui` compiler's (#15) and the restricted
 * charset's (#161) job, not this parser's. The mechanical enforcement described in ADR-010
 * decision paragraph 5 is delivered by #161 (S5); this module is the structural half, refusing
 * the font features whose presence would require on-device shaping.
 *
 * ## The two-step shape
 *
 * `parse()` walks the directory once and returns a `Font` holding the bounds worth keeping
 * across per-glyph lookups. `parseGlyph()` walks one glyf record against that `Font`, without
 * re-reading the directory. The split mirrors `Spirv.cpp`'s separation of header validation
 * from instruction-stream interpretation, and for the same reason: a host that wants every
 * glyph pays the directory walk once, not once per glyph.
 *
 * ## The baker-facing contract
 *
 * This module returns `Result<…, ParseError>`; the higher-level baker (`tools/text/TextBake.cpp`)
 * converts each `ParseError` to a shared-envelope `cli::Diagnostic` carrying a stable `TXT`
 * code, the way `Spirv.cpp`'s `ParseError` is converted by the shader baker. The conversion is
 * deferred to S4 (#160), which is the wave that actually feeds a `.ttf` recipe through this
 * parser and commits a font artifact; S2 (#158, this issue) lands the parser and its tests alone.
 */
module;

export module mdux.tools.truetype;

import std;
import mdux.core.result;

export namespace mdux::tools::truetype {

/// Every rejection code the parser emits. Stable once published - the baker will switch on it
/// (S4 / #160) and an agent keying off the resulting `TXT` code should not be broken by a
/// reword of `describe()`. Mirrors the convention `Spirv.cpp`'s `ParseError` established.
enum class ParseError : std::uint8_t {
    Empty,                      ///< the buffer is empty
    NotATrueTypeOutlineFont,    ///< sfnt version is not one of the recognised TrueType words
    TruncatedOffsetTable,       ///< fewer than 12 bytes of offset table
    TruncatedTableDirectory,    ///< a table record extends past the end of the file
    DuplicateTable,             ///< a table tag appears twice in the directory
    MissingRequiredTable,       ///< head, maxp, glyf or loca is absent
    TableOutOfBounds,           ///< a table's offset+length exceeds the file
    CffOutlinesRejected,        ///< 'CFF ' or 'CFF2' table present - this is a CFF outline font
    GposRejected,               ///< 'GPOS' table present
    GsubRejected,               ///< 'GSUB' table present
    AppleLayoutRejected,        ///< 'morx' or 'mort' table present
    TruncatedHead,              ///< 'head' table is shorter than 54 bytes
    HeadBadMagicNumber,         ///< 'head' magic check (0x5F0F3CF5) failed
    UnsupportedUnitsPerEm,      ///< unitsPerEm is zero or outside the supported [16, 4096] range
    TruncatedMaxp,              ///< 'maxp' table is shorter than 6 bytes
    UnsupportedLocaFormat,      ///< head.indexToLocFormat is neither 0 (short) nor 1 (long)
    LocaSizeMismatch,           ///< 'loca' byte length does not match numGlyphs+1 entries
    LocaOutOfBounds,            ///< a 'loca' entry extends past the end of 'glyf'
    GlyphIndexOutOfRange,       ///< parseGlyph called with an index >= numGlyphs
    TruncatedGlyph,             ///< glyf record is shorter than the 10-byte header
    CompositeGlyphRejected,     ///< numberOfContours == -1; composites are out of v1 scope
    HintingRejected,            ///< instructionLength > 0; on-device hinting is out of scope
    TruncatedContourEndpoints,  ///< endPtsOfContours extends past the glyf record
    TruncatedGlyphFlags,        ///< the flag array extends past the glyf record
    TruncatedGlyphCoords,       ///< x or y coordinates extend past the glyf record
    UnsupportedGlyphFlag,       ///< a flag value bits 6 or 7 (reserved) are set
    NonMonotonicContours,       ///< endPtsOfContours[i] <= endPtsOfContours[i-1]
};

[[nodiscard]] std::string_view describe(ParseError error) noexcept;

/// One contour point, with absolute coordinates already resolved from the per-byte deltas the
/// filesystem form stores. Flags beyond on/off-curve (repeat, x-short, y-short, x-same, y-same)
/// are consumed inside `parseGlyph()` and never reach the caller; a baker that wanted them
/// would be re-walking the storage format this struct exists to hide.
struct GlyphPoint {
    std::int16_t x{0};
    std::int16_t y{0};
    bool         onCurve{true};
};

/// A simple, non-composite glyph outline. `CompositeGlyphRejected` is returned before this
/// struct is ever constructed, so the fields below describe a glyphs that has at least one
/// contour with all-resolved coordinates and no hinting instructions.
struct SimpleGlyph {
    std::uint16_t              glyphIndex{0};
    std::int16_t               xMin{0};
    std::int16_t               yMin{0};
    std::int16_t               xMax{0};
    std::int16_t               yMax{0};
    std::vector<std::uint16_t> endPtsOfContours;
    std::vector<GlyphPoint>    points;
};

/**
 * @brief Everything `parseGlyph()` needs that `parse()` already walked.
 *
 * `parse()` slices subspans out of the bytes it was given; the caller must keep that storage
 * alive for at least as long as the `Font` it returned. The spans are read-only and never
 * copied - a 10 MB font directory stays a 10 MB font directory, not 10 MB plus an in-memory
 * mirror.
 *
 * `loca` carries `numGlyphs + 1` entries because the TrueType format encodes the last glyph's
 * length by giving a trailing entry past the glyph's start - "the next glyph's start minus this
 * glyph's start" is this glyph's length, and the trailing entry is the last start.
 */
struct Font {
    std::uint32_t sfntVersion{0};
    std::uint16_t unitsPerEm{0};
    std::uint16_t numGlyphs{0};
    std::uint16_t indexToLocFormat{0};  ///< 0 = short (uint16/2) offsets, 1 = long (uint32) offsets

    std::span<const std::byte> head{};
    std::span<const std::byte> glyf{};

    /// Byte offsets into `glyf`. `numGlyphs + 1` entries; the last is the past-the-end offset
    /// of the last glyph, i.e. the byte length of `glyf` the directory asserts.
    std::vector<std::uint32_t> loca{};
};

/// Walks the offset table, head, maxp, loca and the implicit "is this a TrueType outline font"
/// check. Rejects CFF/CFF2/GPOS/GSUB/morx/mort presence up front. Does not read any glyf record;
/// `parseGlyph()` does that, one at a time, against the `Font` this returns.
[[nodiscard]] mdux::core::Result<Font, ParseError> parse(std::span<const std::byte> bytes) noexcept;

/// Walks one glyf record against `font`, identified by `glyphIndex`. Rejects a composite glyph
/// before constructing any `SimpleGlyph` state, and a glyph with hinting instructions before
/// touching its flag or coordinate arrays - so neither of those paths can accidentally read past
/// a record that was malformed in a way the bound check has not yet run for.
///
/// Reads nothing outside the `Font`'s own `glyf` span (and the trailing `loca` entry needed to
/// compute the record length). The outer buffer has to stay alive for the lifetime of the
/// `Font`; this function does not store it.
[[nodiscard]] mdux::core::Result<SimpleGlyph, ParseError> parseGlyph(const Font& font, std::uint16_t glyphIndex) noexcept;

}  // namespace mdux::tools::truetype