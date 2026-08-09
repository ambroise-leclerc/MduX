/**
 * @file Draw.cppm
 * @brief Governed-zone coverage draw path: baked glyph runs into `CoverageR8` rectangles.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * Part of MduXCore. This is the last step of the text pipeline and the one that runs on a device:
 * it takes a run the baker positioned and turns it into rectangles the renderer draws.
 *
 * ## Why this is replay, not layout
 *
 * ADR-010 forbids on-device shaping, layout and font-table parsing. Nothing here does any of
 * those. A run record already carries the glyph and the position the baker computed; this module
 * looks each glyph up in the package's table and emits a rectangle at the coordinates it was
 * given. There is no pen advancing by a computed width, no code-point-to-glyph mapping, and no
 * decision about where anything goes - every such decision was made at build time and is in the
 * bytes.
 *
 * The distinction is worth stating because "draws text" sounds like the thing the ADR forbids.
 * The check is whether a different input could produce a different *layout*: it cannot, because
 * positions are inputs rather than outputs.
 *
 * ## The v1 run record
 *
 * Six bytes, little-endian, as `mdux.text.schema` documents and deliberately does not interpret:
 *
 * | offset | type            | meaning                                              |
 * |--------|-----------------|------------------------------------------------------|
 * | 0      | `std::uint16_t` | index into the font package's glyph table            |
 * | 2      | `std::int16_t`  | x, in pixels, in the run's coordinate frame          |
 * | 4      | `std::int16_t`  | y, in pixels, the glyph's baseline                   |
 *
 * The index is into the *package's* glyph table, not the font's `glyf` table. Those differ - the
 * package holds only the baked charset - and confusing them draws the wrong character, so the
 * record's field is validated against `FontPackage::glyphs.size()` rather than against anything
 * the font would recognise.
 *
 * Little-endian is stated rather than assumed: the sidecar is committed bytes compared across
 * toolchains, so the decode cannot inherit the host's byte order.
 *
 * ## Texture coordinates, and the reason this helper exists at all
 *
 * `DrawList::addRect()` takes its `uv` as a `core::Rect`, which is integer, and copies those
 * integers straight into the vertex's `float u`/`v`. The fragment shader samples with
 * `texture(uAtlas, fragUv)` - a `sampler2D`, so **normalised** coordinates. An integer rectangle
 * can therefore only express 0 and 1: a real slot such as (13, 46, 11x12) would arrive as
 * `u = 13.0` and sample far outside the sheet.
 *
 * So normalising a texel slot against the atlas extent is exactly what this module is for, and
 * `addGlyphRect()` is the only supported way to record a `CoverageR8` rectangle. Calling
 * `addRect()` with `DrawMode::CoverageR8` directly is not an error the type system can catch, but
 * it is one this module exists to make unnecessary.
 */
module;

export module mdux.text.draw;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;

export namespace mdux::text::draw {

/// Bytes per v1 run record. Mirrors `mdux.text.schema`'s `recordSize`, restated here because this
/// is the module that gives the number meaning - the schema only checks that the sidecar divides
/// by it.
inline constexpr std::size_t recordSize = 6;

enum class DrawTextError : std::uint8_t {
    PartialRecord,        ///< the run's byte length is not a multiple of `recordSize`
    RecordSizeWrong,      ///< a span handed to `decodeRecord()` is not exactly one record
    GlyphIndexOutOfRange, ///< a record names a glyph the package does not contain
    EmptyAtlas,           ///< the package's atlas has zero extent, so no uv can be normalised
    ListRejected,         ///< `DrawList` refused a rectangle - budget, clip or degenerate extent
};

[[nodiscard]] std::string_view describe(DrawTextError error) noexcept;

/// One decoded record: which glyph, and where the baker put it.
struct GlyphPlacement {
    /// Index into `FontPackage::glyphs`.
    ///
    /// Deliberately *not* called `glyphIndex`, because `font::GlyphRecord::glyphIndex` already
    /// means something else - the font's own glyph ID, from the `glyf` table. The two are
    /// different numbers for the same glyph, since a package holds only the baked charset, and
    /// code handling both types would otherwise have two fields with one name and no hint that
    /// swapping them draws the wrong character.
    std::uint16_t packageIndex{0};
    mdux::core::Px x{0};
    mdux::core::Px y{0};  ///< the baseline, not the bitmap's top edge
};

/**
 * @brief Decodes one v1 run record.
 *
 * Exposed separately from `recordRun()` so the byte-order contract can be tested directly rather
 * than only through a rendered frame - a decode that silently swapped its int16 fields would
 * otherwise show up as a glyph in the wrong place, which is a much harder failure to read.
 *
 * `record` must be exactly `recordSize` bytes. A longer span is refused rather than decoded and
 * truncated: handing this the whole run instead of one record is the obvious misuse, and it would
 * otherwise return the first glyph and look like it worked.
 */
[[nodiscard]] mdux::core::Result<GlyphPlacement, DrawTextError> decodeRecord(std::span<const std::byte> record) noexcept;

/**
 * @brief Records one baked glyph run into `list` as `CoverageR8` rectangles.
 *
 * @param list    the destination; rectangles are appended in record order
 * @param package the font package the run was baked against
 * @param records the run's bytes, a whole number of `recordSize` records
 * @param originX added to every record's x, so a run can be placed without re-baking it
 * @param originY added to every record's y, likewise
 * @param color   the text colour; coverage modulates its alpha, never its rgb
 *
 * Blank glyphs - the space, and anything else with no coverage - are skipped rather than recorded
 * as empty rectangles: `DrawList::addRect()` refuses a degenerate extent, and a space is not an
 * error. The advance it carries has already been applied by the baker, so skipping it changes
 * nothing about where the following glyphs land.
 *
 * All-or-nothing. On the first bad record the list is rolled back to where it was on entry, so a
 * run that fails halfway leaves no rectangles behind and a fragment of a word cannot reach a
 * frame. Anything the caller recorded *before* calling this is untouched.
 */
[[nodiscard]] mdux::core::ResultVoid<DrawTextError> recordRun(mdux::draw::DrawList& list,
                                                              const mdux::font::FontPackage& package,
                                                              std::span<const std::byte> records,
                                                              mdux::core::Px originX,
                                                              mdux::core::Px originY,
                                                              mdux::core::ColorRgba8 color) noexcept;

/**
 * @brief Records one glyph, normalising its atlas slot against the sheet extent.
 *
 * The rectangle is placed with the glyph's baked origin applied: `bitmapOriginX` from the pen and
 * `bitmapOriginY` *above* the baseline, which is why `y` in a record is the baseline rather than
 * the top edge. Getting that inversion wrong renders text that looks right in isolation and sits
 * at the wrong height next to anything else.
 */
[[nodiscard]] mdux::core::ResultVoid<DrawTextError> addGlyphRect(mdux::draw::DrawList& list,
                                                                 const mdux::font::FontPackage& package,
                                                                 const mdux::font::GlyphRecord& glyph,
                                                                 mdux::core::Px penX,
                                                                 mdux::core::Px baselineY,
                                                                 mdux::core::ColorRgba8 color) noexcept;

}  // namespace mdux::text::draw
