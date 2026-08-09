/**
 * @file Schema.cppm
 * @brief Governed-zone font package types: the canonical shape of every baked font `package.json`,
 *        and the restricted-charset table that makes "no shaping on device" checkable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference (decision 1, mirrored to text by ADR-010: one
 *             definition imported by both the baker and the runtime, not two that could drift)
 * @compliance ADR-010 No on-device text shaping
 *
 * Part of MduXCore. `mdux-textbake` (#160) writes packages through this module and the device
 * runtime reads them through it, which is the point: a baker that produced one shape and a
 * runtime that expected another is the Wave 2 lesson, and the reason `mdux.text.schema` exists in
 * the same form for text packages.
 *
 * S4 emitted this JSON by hand, because the baker landed before this module did. Adopting the
 * type here does not change what a package *is* - it removes the second, informal definition.
 *
 * A font package is one font, rasterised at one pixel size, for one set of approved locales:
 *
 * ```json
 * {
 *   "schemaVersion": 1,
 *   "id": "dejavu-ui",
 *   "kind": "font",
 *   "unitsPerEm": 2048,
 *   "pixelSize": 16,
 *   "locales": ["en-US"],
 *   "atlas":  { "path": "atlas.bin", "width": 128, "height": 128, "byteLength": 16384,
 *               "sha256": "…", "occupancyPercent": 54 },
 *   "glyphs": [ { "codePoint": 65, "glyphIndex": 36, "advanceWidth": 1401, … } ],
 *   "kerning": [ { "left": 65, "right": 86, "adjustment": -80 } ],
 *   "restrictedCharset": [ { "first": 32, "last": 126 } ]
 * }
 * ```
 *
 * ## What the restricted charset is for
 *
 * ADR-010's commitment is that a device performs no shaping: every glyph it draws was baked, at a
 * known slot, with a known advance. Static text is safe by construction - the baker positioned it.
 * *Dynamic* text is not: a screen that formats a number or interpolates a value at runtime can
 * reach a code point nobody baked, and the runtime has no fallback because having one would mean
 * mapping code points on device.
 *
 * `restrictedCharset` is the declared answer: the exact set of code points this package can draw.
 * It lets the `.medui` compiler (#15) reject a dynamic-text format whose output could escape the
 * set *at build time*, which is what turns the ADR's commitment from a slogan into a compile
 * error. This module carries the table and the membership test; the compiler is what enforces it,
 * and #161 is where the table starts existing to be enforced against.
 *
 * The table is stored as ranges rather than a code-point list because that is how a charset is
 * written and reviewed - "U+0020..U+007E" is checkable by eye in a way that ninety-five integers
 * is not - and because membership is then a binary search rather than a scan.
 *
 * ## Tabular figures
 *
 * `validate()` requires every decimal digit the package contains to share one advance width.
 *
 * This is a safety property, not a typographic preference. A numeric field on a medical device -
 * a heart rate, a dose, an elapsed time - is redrawn as its value changes. If '1' is narrower
 * than '8', the digits shift horizontally on every update and the field jitters. A clinician
 * reading a changing number at a glance is exactly the reader that motion hurts most, and the
 * defect is invisible in a static screenshot, so it has to be caught where the font is baked
 * rather than where it is looked at.
 *
 * A font whose digits are proportional is not broken - it is unsuitable for this use, and the
 * baker says so with a stable code rather than shipping a package that will jitter.
 */
module;

export module mdux.font.schema;

import std;
import mdux.core.result;
import mdux.evidence.json;

export namespace mdux::font {

/// The `kind` every font package declares, so a reader that is handed the wrong package type
/// finds out from the package rather than from a missing field further in.
inline constexpr std::string_view packageKind = "font";

/// The schema version this module reads and writes. A package declaring anything else is refused
/// rather than interpreted, because a reader guessing at an unknown version is how a format ends
/// up with two incompatible meanings for the same bytes.
inline constexpr std::int64_t schemaVersion = 1;

enum class SchemaError : std::uint8_t {
    NotAnObject,             ///< the document is not a JSON object
    MissingMember,           ///< a required member is absent
    WrongType,               ///< a member has the wrong JSON type
    UnsupportedVersion,      ///< schemaVersion is not `schemaVersion`
    WrongKind,               ///< kind is not "font"
    EmptyId,                 ///< id is empty
    EmptyLocales,            ///< no approved locale
    EmptyLocaleTag,          ///< a locale entry is empty
    DuplicateLocale,         ///< a locale is listed twice
    UnsupportedUnitsPerEm,   ///< unitsPerEm is zero
    UnsupportedPixelSize,    ///< pixelSize is zero
    AtlasNotPowerOfTwo,      ///< atlas width or height is not a power of two
    AtlasSizeMismatch,       ///< byteLength is not width * height
    EmptyAtlasPath,          ///< the atlas sidecar has no filename
    AtlasPathHasSeparator,   ///< the atlas path is not a bare filename
    NoGlyphs,                ///< the package draws nothing
    DuplicateCodePoint,      ///< two glyph records claim the same code point
    GlyphsNotSorted,         ///< glyph records are not in ascending code-point order
    SurrogateCodePoint,      ///< a glyph or charset range names a UTF-16 surrogate
    GlyphOutsideAtlas,       ///< a glyph's slot leaves the sheet
    EmptyCharset,            ///< the restricted charset declares no ranges
    CharsetRangeDescending,  ///< a charset range's last precedes its first
    CharsetRangesOverlap,    ///< two charset ranges cover the same code point
    CharsetGlyphMissing,     ///< the charset names a code point the package has no glyph for
    TabularFigureMismatch,   ///< the decimal digits do not share one advance width
    KerningGlyphMissing,     ///< a kerning pair names a code point the package has no glyph for
    DuplicateKerningPair,    ///< the same ordered pair appears twice
    IntegerOutOfRange,       ///< a JSON integer does not fit the field it was read into
    CodePointOutOfRange,     ///< a code point exceeds U+10FFFF, so it is not a Unicode scalar value
    InvalidAtlasDigest,      ///< the atlas sha256 is not 64 lowercase hexadecimal characters
};

[[nodiscard]] std::string_view describe(SchemaError error) noexcept;

/// Where the coverage sheet lives and what shape it is. The digest is carried here rather than
/// only in `report.json` so a runtime that loads a package can check the sidecar it was given
/// belongs to it, without needing the bake report.
struct AtlasMetrics {
    std::string   path{};              ///< bare filename, resolved beside `package.json`
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint64_t byteLength{0};       ///< exactly `width * height`; the sheet is R8
    std::string   sha256{};            ///< 64 lowercase hex characters
    std::uint32_t occupancyPercent{0}; ///< recorded for the report, not load-bearing
};

/// One baked glyph: where it sits in the sheet, and what it does to the pen.
///
/// `advanceWidth` and `leftSideBearing` are in font units, not pixels, so a consumer can scale
/// them to any size the same way the rasteriser did. The slot and origin are in pixels, because
/// they describe the sheet that was actually baked.
struct GlyphRecord {
    char32_t      codePoint{0};
    std::uint16_t glyphIndex{0};
    std::uint16_t advanceWidth{0};
    std::int16_t  leftSideBearing{0};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};   ///< zero for a blank such as the space, which still has an advance
    std::uint32_t height{0};
    std::int32_t  bitmapOriginX{0};
    std::int32_t  bitmapOriginY{0};

    [[nodiscard]] bool isBlank() const noexcept { return width == 0 || height == 0; }
};

/// One kerning adjustment the baker chose to bake, in font units.
///
/// "Chose to bake" is the operative phrase: ADR-010 forbids the runtime from consulting a kerning
/// table, so a pair is either here - resolved at build time - or it does not apply. There is no
/// on-device lookup that could find one this list omits.
struct KerningPair {
    char32_t     left{0};
    char32_t     right{0};
    std::int16_t adjustment{0};
};

/// One run of code points the package is allowed to draw.
struct CharsetRange {
    char32_t first{0};
    char32_t last{0};

    [[nodiscard]] bool contains(char32_t point) const noexcept { return point >= first && point <= last; }
};

/**
 * @brief A baked font package: one font, one pixel size, one approved locale set.
 *
 * `validate()` is the whole contract. It is called by the baker before writing and by `parse()`
 * after reading, so a package that exists is a package that passed - there is no state in which a
 * caller holds a `FontPackage` that has not been checked.
 */
struct FontPackage {
    std::string              id{};
    std::uint16_t            unitsPerEm{0};
    std::uint32_t            pixelSize{0};
    std::vector<std::string> locales{};
    AtlasMetrics             atlas{};
    std::vector<GlyphRecord> glyphs{};       ///< sorted by code point
    std::vector<KerningPair> kerning{};
    std::vector<CharsetRange> restrictedCharset{};  ///< sorted, non-overlapping

    /// Every structural rule this module enforces, including the tabular-figure requirement and
    /// that the restricted charset names nothing the package cannot draw.
    [[nodiscard]] mdux::core::ResultVoid<SchemaError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<evidence::json::Value, SchemaError> toJson() const noexcept;

    /// Canonical JSON text, ready to commit. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, SchemaError> write() const noexcept;

    /// Reads and validates. A package that parses is one that passed `validate()`.
    [[nodiscard]] static mdux::core::Result<FontPackage, SchemaError> parse(std::string_view json) noexcept;

    /// The glyph for `point`, or nullptr. Binary search; `glyphs` is sorted by code point.
    [[nodiscard]] const GlyphRecord* find(char32_t point) const noexcept;

    /**
     * @brief Whether `point` is inside the restricted charset.
     *
     * The test the `.medui` compiler (#15) applies to every code point a dynamic-text format
     * could produce. Membership here is a promise that the glyph was baked - `validate()` refuses
     * a package whose charset names a code point it has no glyph for, so the two cannot disagree.
     */
    [[nodiscard]] bool permits(char32_t point) const noexcept;

    /// The kerning adjustment for an ordered pair, or zero when none was baked. Zero rather than
    /// an optional: "no adjustment" and "an adjustment of zero" mean the same thing to a pen.
    [[nodiscard]] std::int16_t kerningFor(char32_t left, char32_t right) const noexcept;
};

}  // namespace mdux::font
