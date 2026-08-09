/**
 * @file Truetype.cppm
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
 * ## v1 scope: read what an atlas needs, refuse only what cannot be baked
 *
 * v1 supports TrueType `glyf` outlines and Latin/Cyrillic/Greek LTR only. The parser emits
 * exactly two scope rejections, and both are cases where there is no outline for the baker to
 * turn into coverage:
 *
 * - CFF or CFF2 outlines (`CFF `/`CFF2` present, so the outlines are not in `glyf` at all):
 *   `CffOutlinesRejected`
 * - composite glyphs (`numberOfContours == -1`), whose outline is a transform over other
 *   glyphs rather than a contour list: `CompositeGlyphRejected`. ADR-010 decision 5 speaks of
 *   "composite glyph substitutions the baker did not pre-bake", which is the seam S4 (#160)
 *   fills - resolving a composite into a flat contour list is a baking step, and this code is
 *   the signal S4 acts on.
 *
 * Everything else ADR-010 names is *skipped rather than refused*. `GPOS`, `GSUB`, `GDEF`,
 * `morx`/`mort` and the per-glyph hinting `instructions[]` array are simply never read: the
 * parser walks `head`, `maxp`, `loca` and `glyf`, and a table it does not read cannot reach a
 * device. That is the shape ADR-010 actually asks for - decision 4 forbids *on-device* code
 * that walks a font table or advances a pen by a runtime-computed width, not the presence of
 * those bytes in the host's input file.
 *
 * The distinction matters because rejecting on presence would reject essentially every real
 * font. The following is a local measurement, not a validated or certified result: the eight
 * DejaVu 2.37 faces installed on one developer machine were fed to this parser, once with a
 * presence rule and once without.
 *
 * - With a presence rule: **0 of 8** faces reach `parse()` at all, because all eight carry both
 *   `GPOS` and `GSUB`. Feeding it a face with those tables stripped, so that per-glyph rules
 *   become observable, **41%** of DejaVuSans' 6253 glyphs produce an outline - the rest are
 *   refused as composites (42%), as carrying hinting bytecode (16%), or as zero-length records.
 * - Skipping instead: **8 of 8** faces parse as shipped, and **58%** of DejaVuSans' glyphs
 *   produce an outline.
 *
 * Skipping keeps the baker's input a font a designer actually shipped, while the runtime still
 * has no shaping code, because the atlas the baker commits carries coverage bitmaps and baked
 * advances and nothing else. Composites are then the one refusal that still costs coverage -
 * 42% of DejaVuSans' glyphs, which is where the accented Latin, Cyrillic and Greek forms live -
 * and S4 is where that is paid down by pre-baking them.
 *
 * RTL and complex scripts cannot be detected from the font binary alone - they are a property
 * of the string-to-glyph mapping, which is the `.medui` compiler's (#15) and the restricted
 * charset's (#161) job, not this parser's. The mechanical enforcement described in ADR-010
 * decision paragraph 5 is delivered by #161 (S5); this module is the structural half, refusing
 * the outline forms it cannot turn into a contour list.
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
    TruncatedHead,              ///< 'head' table is shorter than 54 bytes
    HeadBadMagicNumber,         ///< 'head' magic check (0x5F0F3CF5) failed
    UnsupportedUnitsPerEm,      ///< unitsPerEm is outside the spec's [16, 16384] range
    TruncatedMaxp,              ///< 'maxp' table is shorter than 6 bytes
    UnsupportedLocaFormat,      ///< head.indexToLocFormat is neither 0 (short) nor 1 (long)
    LocaSizeMismatch,           ///< 'loca' is too short to hold numGlyphs+1 entries
    LocaOutOfBounds,            ///< a 'loca' entry extends past the end of 'glyf'
    GlyphIndexOutOfRange,       ///< parseGlyph called with an index >= numGlyphs
    TruncatedGlyph,             ///< glyf record is non-empty but shorter than the 10-byte header
    CompositeGlyphRejected,     ///< numberOfContours == -1; composites are out of v1 scope
    TruncatedContourEndpoints,  ///< endPtsOfContours extends past the glyf record
    TruncatedGlyphInstructions, ///< the (skipped) hinting instruction array runs past the record
    TruncatedGlyphFlags,        ///< the flag array extends past the glyf record
    GlyphFlagRepeatOverrun,     ///< a REPEAT_FLAG count claims more points than the glyph has
    TruncatedGlyphCoords,       ///< x or y coordinates extend past the glyf record
    UnsupportedGlyphFlag,       ///< a flag value has reserved bit 7 set
    CoordinateOverflow,         ///< an accumulated coordinate left the int16 range the spec allows
    NonMonotonicContours,       ///< endPtsOfContours[i] <= endPtsOfContours[i-1]
    MissingCharacterMap,        ///< 'cmap' is absent, so no code point can be resolved to a glyph
    TruncatedCmap,              ///< a 'cmap' header, encoding record or subtable runs past the table
    UnsupportedCmapFormat,      ///< 'cmap' carries no subtable in a format this parser reads
    CharacterMapTooLarge,       ///< 'cmap' declares more mappings than `maxCmapEntries` allows
    MissingHorizontalMetrics,   ///< 'hhea' or 'hmtx' is absent, so no glyph has a known advance
    TruncatedHhea,              ///< 'hhea' is shorter than 36 bytes
    TruncatedHmtx,              ///< 'hmtx' is shorter than numberOfHMetrics requires
    UnsupportedMetricCount,     ///< hhea.numberOfHMetrics is zero, or exceeds maxp.numGlyphs
};

[[nodiscard]] std::string_view describe(ParseError error) noexcept;

/// One contour point, with absolute coordinates already resolved from the per-byte deltas the
/// `glyf` storage form encodes them as. Flags beyond on/off-curve (repeat, x-short, y-short,
/// x-same, y-same) are consumed inside `parseGlyph()` and never reach the caller; a baker that
/// wanted them would be re-walking the storage format this struct exists to hide.
struct GlyphPoint {
    std::int16_t x{0};
    std::int16_t y{0};
    bool         onCurve{true};
};

/// A simple, non-composite glyph outline. `CompositeGlyphRejected` is returned before this
/// struct is ever constructed, so the fields below describe a glyph with all-resolved
/// coordinates and no hinting instructions. Both vectors are empty for a glyph with no
/// outline - the blank the TrueType format spells either as a zero-length `glyf` record
/// (`loca[i] == loca[i+1]`, how every real font stores the space character) or as a header
/// declaring `numberOfContours == 0`.
struct SimpleGlyph {
    std::uint16_t              glyphIndex{0};
    std::int16_t               xMin{0};
    std::int16_t               yMin{0};
    std::int16_t               xMax{0};
    std::int16_t               yMax{0};
    std::vector<std::uint16_t> endPtsOfContours;
    std::vector<GlyphPoint>    points;
};

/// The largest number of `cmap` ranges `parse()` will build. A font mapping more than this is
/// either enormous or hostile; the cap turns the second case into `CharacterMapTooLarge` rather
/// than an allocation sized by a malformed table.
inline constexpr std::size_t maxCmapEntries = 1u << 16;

/**
 * @brief One run of consecutive code points mapping to consecutive glyph indices.
 *
 * `cmap` is decoded into a sorted list of these rather than kept in its on-disk shape, because
 * the two formats this parser reads store the same information very differently: format 4 uses
 * four parallel arrays plus an `idRangeOffset` indirection into a trailing glyph array, and
 * format 12 uses a flat group list. Normalising both at parse time means `glyphForCodePoint()`
 * is one binary search with no format switch, and the format-4 indirection - the fiddliest part
 * of the table - is walked exactly once, where it can be bounds-checked in one place.
 *
 * A segment whose glyphs are not consecutive (format 4's indirect case) decodes into one range
 * per code point, so `first == last` there. That costs entries but keeps the lookup uniform.
 */
struct CmapRange {
    char32_t      first{0};       ///< first code point in the run
    char32_t      last{0};        ///< last code point, inclusive
    std::uint16_t firstGlyph{0};  ///< glyph index for `first`; the run is consecutive from there
};

/**
 * @brief One glyph's horizontal metrics, in font units.
 *
 * `advanceWidth` is the quantity ADR-010 requires a baked glyph to carry: the runtime advances a
 * pen by a value the baker computed, never by one it derives at runtime. `leftSideBearing` is the
 * gap from the pen position to the glyph's left edge, which the atlas packer needs to place a
 * coverage bitmap against the origin the advance is measured from.
 */
struct GlyphMetrics {
    std::uint16_t advanceWidth{0};
    std::int16_t  leftSideBearing{0};
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

    /// How many leading glyphs `hmtx` gives a full (advance, bearing) pair for. Glyphs past this
    /// carry only a bearing and reuse the last advance - the format's compression for the run of
    /// equal-width glyphs a monospace or CJK font ends with.
    std::uint16_t numberOfHMetrics{0};

    /// The `hmtx` table, sliced but not decoded. `metricsFor()` reads one glyph's pair from it,
    /// the same way `parseGlyph()` reads one record from `glyf` - a font with 6000 glyphs should
    /// not pay for 6000 metric structs when a baker asks for ninety-five of them.
    std::span<const std::byte> hmtx{};

    /// Code-point ranges resolved out of `cmap`, sorted by `first` and non-overlapping.
    /// `glyphForCodePoint()` binary-searches this; see `CmapRange` for why the decoded form is a
    /// range list rather than the format-4 segment arrays it usually comes from.
    std::vector<CmapRange> characterMap{};
};

/// Walks the offset table, head, maxp, loca and the implicit "is this a TrueType outline font"
/// check. Rejects a CFF/CFF2 outline font up front, since it has no `glyf` to read; a `GPOS`,
/// `GSUB`, `GDEF`, `morx` or `mort` table is left unread rather than refused (see "v1 scope"
/// above). Does not read any glyf record; `parseGlyph()` does that, one at a time, against the
/// `Font` this returns.
[[nodiscard]] mdux::core::Result<Font, ParseError> parse(std::span<const std::byte> bytes) noexcept;

/// Walks one glyf record against `font`, identified by `glyphIndex`. Rejects a composite glyph
/// before constructing any `SimpleGlyph` state, so that path cannot accidentally read past a
/// record that was malformed in a way the bound check has not yet run for. A glyph's hinting
/// `instructions[]` array is stepped over, not interpreted and not returned - the runtime has no
/// hinting engine, so the bytes have no consumer. A zero-length record
/// (`loca[glyphIndex] == loca[glyphIndex + 1]`) is the spec's encoding of a glyph with no
/// outline, not a truncation, and yields an empty `SimpleGlyph`.
///
/// Reads nothing outside the `Font`'s own `glyf` span (and the trailing `loca` entry needed to
/// compute the record length). The outer buffer has to stay alive for the lifetime of the
/// `Font`; this function does not store it.
///
/// `Font` is an aggregate with public fields, so a caller can hand this function one that
/// `parse()` did not build. The `numGlyphs`/`loca` consistency `parse()` guarantees is
/// therefore re-checked here rather than assumed, and a `Font` that fails it is refused with
/// `LocaSizeMismatch` instead of indexing a `std::vector` out of range.
[[nodiscard]] mdux::core::Result<SimpleGlyph, ParseError> parseGlyph(const Font& font, std::uint16_t glyphIndex) noexcept;

/**
 * @brief Resolves a code point to a glyph index, or nullopt when the font does not map it.
 *
 * Binary search over `font.characterMap`. Returns nullopt rather than glyph 0 for an unmapped
 * code point, deliberately: glyph 0 is `.notdef`, a real glyph with a real outline, and a baker
 * that silently baked it for every missing character would produce an atlas full of tofu boxes
 * that looked like a successful bake. The caller decides whether a miss is fatal - for the font
 * baker (#160) it is, because the recipe named a character the font cannot draw.
 */
[[nodiscard]] std::optional<std::uint16_t> glyphForCodePoint(const Font& font, char32_t codePoint) noexcept;

/**
 * @brief Reads one glyph's horizontal metrics out of `hmtx`.
 *
 * Glyphs at or past `numberOfHMetrics` have no advance of their own and inherit the last one the
 * table stores, which is how the format compresses the trailing run of equal-width glyphs. That
 * is not an error and this function resolves it silently; `TruncatedHmtx` means the table is
 * genuinely too short for the metrics it promised.
 */
[[nodiscard]] mdux::core::Result<GlyphMetrics, ParseError> metricsFor(const Font& font, std::uint16_t glyphIndex) noexcept;

}  // namespace mdux::tools::truetype