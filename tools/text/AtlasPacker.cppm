/**
 * @file AtlasPacker.cppm
 * @brief Host-only atlas packer: coverage bitmaps into one power-of-two R8 sheet, by deterministic
 *        shelf placement.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: never linked into a device target)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning)
 * @compliance ADR-007 Evidence pipeline doctrine (the sheet it produces is committed)
 * @compliance ADR-010 No on-device text shaping
 *
 * ## Why this is host-tools rather than governed
 *
 * An atlas is chosen once, at build time, and a device consumes the slot rectangles the baker
 * recorded. Nothing on a device ever packs, so a governed packer would be a promise with no
 * caller - and every module in the governed zone is a module a reviewer has to reason about for
 * device behaviour.
 *
 * `mdux.text.raster` (#159) sits in this zone for the same reason as of #116. It was governed
 * on the argument that a device path might one day rasterise, which ADR-008 decision 1 would
 * then require to be *that* module rather than a second one - but it allocates, so it could not
 * stay in a target compiled with `-fno-exceptions`. See `Raster.cppm`.
 *
 * ## Shelf placement, and why not something better
 *
 * Glyphs are sorted by descending height, then laid left to right into rows ("shelves") whose
 * height is set by the first glyph placed on them. When a glyph does not fit the current shelf's
 * remaining width, a new shelf opens above.
 *
 * Skyline packing would waste less. It is not used because shelf placement's output is trivially
 * predictable from the input order, and that is worth more here than density: this sheet is
 * committed and byte-compared across toolchains on every CI run (ADR-007). A packer whose result
 * depends on a heuristic's tie-breaking is a packer whose diff a reviewer cannot predict from the
 * recipe, and the atlas is not the size constraint in any realistic font package.
 *
 * The sort is by (height, width, id) rather than height alone, so two glyphs of equal height
 * cannot swap depending on the sort's stability. That triple is a total order, which is what makes
 * the placement a pure function of the glyph set rather than of the standard library.
 *
 * ## Power-of-two, and how the size is chosen
 *
 * Sheet dimensions are powers of two because a texture upload path should not have to care about
 * row padding. The packer starts at `minimumAtlasEdge` and doubles - width first, then height up
 * to the current width - retrying the whole placement each time. It refuses with
 * `AtlasBudgetExceeded` once both edges would pass `maximumAtlasEdge`, which is the "over-budget
 * glyph set" rejection issue #160 asks for.
 *
 * **This is a first fit in width-major order, not a minimum.** Candidates are enumerated by
 * increasing width and, within each width, by increasing height up to it - so area is not
 * monotonic along the search and the first sheet that fits is not necessarily the smallest one
 * that would have. 256x256 (65536 px) is reached before 512x64 (32768 px), and a glyph set
 * fitting both takes the larger.
 *
 * That is kept deliberately. "Squarish" is the property worth having in a texture atlas and
 * "minimal area" is not: a 512x64 strip wastes no pixels but is a worse shape to sample from and
 * to fit under a device's texture limits. What ADR-007 needs is that the choice be a pure
 * function of the glyph set, which a first fit in a fixed order is.
 *
 * Retrying from scratch on each growth step, rather than incrementally fitting, keeps the result a
 * function of the final size alone: a sheet packed at 512 wide is identical whether or not the
 * packer tried 256 first.
 */
module;

export module mdux.tools.atlaspacker;

import std;
import mdux.core.result;

export namespace mdux::tools::atlas {

/// Every rejection code the packer emits. Converted to `TXT`-prefixed diagnostics by the baker,
/// the same way `truetype::ParseError` and `raster::RasterError` are.
enum class PackError : std::uint8_t {
    NoGlyphs,             ///< nothing to pack
    GlyphTooLarge,        ///< one glyph alone exceeds `maximumAtlasEdge`, so no sheet can hold it
    AtlasBudgetExceeded,  ///< the set does not fit within `maximumAtlasEdge` in both axes
    DuplicateGlyphId,     ///< two entries carry the same id, so a slot lookup would be ambiguous
};

[[nodiscard]] std::string_view describe(PackError error) noexcept;

/// Smallest and largest sheet edge the packer will produce. The floor keeps a two-glyph font from
/// producing an 8x8 sheet whose slot coordinates are then awkward to reason about; the ceiling is
/// the budget an over-large glyph set is refused against.
inline constexpr std::uint32_t minimumAtlasEdge = 64;
inline constexpr std::uint32_t maximumAtlasEdge = 8192;

/// Transparent margin left between packed glyphs, in pixels.
///
/// One pixel, not zero: a sampler filtering at a slot's edge reads the neighbouring texel, and
/// without a gap that texel belongs to a different glyph. The runtime samples `CoverageR8` with
/// linear filtering (#162, S6), so this is the difference between a clean edge and a faint ghost
/// of whatever was packed next door.
inline constexpr std::uint32_t glyphPadding = 1;

/// One glyph to place: its identity, and the size of the bitmap that has to fit.
struct GlyphExtent {
    std::uint32_t id{0};      ///< the caller's handle, echoed back on the slot; usually a glyph index
    std::uint32_t width{0};   ///< bitmap width in pixels; zero for a glyph with no coverage
    std::uint32_t height{0};  ///< bitmap height in pixels
};

/// Where one glyph landed. A blank glyph keeps `width == height == 0` and occupies no area, so a
/// space costs the atlas nothing while still having a slot the package can record.
struct GlyphSlot {
    std::uint32_t id{0};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

/// The chosen sheet, and where everything went. `slots` is sorted by `id`, not by placement order,
/// so a caller can binary-search it and a diff of the committed package stays readable when one
/// glyph changes size.
struct AtlasLayout {
    std::uint32_t          width{0};
    std::uint32_t          height{0};
    std::vector<GlyphSlot> slots{};

    /// Occupied area over sheet area, in percent, for the bake report. Recorded because it is the
    /// number that tells an author whether the next glyph will force a doubling.
    [[nodiscard]] std::uint32_t occupancyPercent() const noexcept;
};

/**
 * @brief Places every extent on a power-of-two sheet.
 *
 * The sheet is the first that holds them in a width-major enumeration, which is not the same as
 * the smallest - see "Power-of-two" above for the order, the example, and why it is kept.
 *
 * Deterministic: identical input yields an identical layout on every toolchain, which is what
 * makes the committed `atlas.bin` byte-comparable across CI legs.
 */
[[nodiscard]] mdux::core::Result<AtlasLayout, PackError> pack(std::span<const GlyphExtent> extents) noexcept;

}  // namespace mdux::tools::atlas
