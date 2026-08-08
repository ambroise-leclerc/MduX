/**
 * @file Raster.cppm
 * @brief Governed-zone glyph rasteriser: outlines to an R8 coverage bitmap, integer arithmetic
 *        only, so the same glyph produces the same bytes on every supported toolchain.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine (the bytes this produces get committed)
 * @compliance ADR-008 Zero-SOUP ML inference (decision 1, mirrored to text by ADR-010: one
 *             module, not a host implementation and a device one that could drift)
 * @compliance ADR-010 No on-device text shaping
 *
 * Part of MduXCore. The font baker (#160, S4) imports this to fill an atlas; if a device path
 * ever needs to rasterise, it imports *this* module rather than growing a second implementation.
 * That is ADR-008 decision 1 applied to text, and it is why this file is governed rather than
 * sitting in `tools/` next to the parser that feeds it.
 *
 * ## Why it does not take a `truetype::SimpleGlyph`
 *
 * `mdux.tools.truetype` (#158) is host-tools-zone code. ADR-004 forbids a governed module from
 * importing one, and the rule is mechanically checked by `mdux_verify_trust_zones()` - so this
 * module cannot name that type even though it is the obvious input. It takes `Outline` instead:
 * the same TrueType shape (points, per-contour end indices, on/off-curve flags) expressed in
 * governed types, with the baker doing a flat conversion. That indirection is the trust-zone
 * boundary made visible, not an abstraction for its own sake.
 *
 * ## The determinism rule
 *
 * **There is no floating-point arithmetic in this module.** Not "float used carefully", not
 * "float with `-ffp-contract=off`" - none. Every coordinate is a fixed-point integer, every
 * division is integer division, and the flattening of a curve into line segments is a pure
 * function of integer inputs.
 *
 * That is a stronger position than `mdux.ml.kernels` takes, and deliberately so. The kernels have
 * to be float because the model's arithmetic is float, and they therefore depend on
 * `cmake/MduXDeterminism.cmake` continuing to keep contraction and fast-math away from them. A
 * rasteriser has no such obligation: coverage is a ratio of areas, areas are counts of subpixel
 * units, and counts are integers. Choosing integers means the cross-toolchain byte-identity
 * claim in issue #159 is a property of the source rather than of the flags it is compiled with,
 * and no future `-ffast-math` arriving from a preset can weaken it.
 *
 * Concretely, the arithmetic that would otherwise be float:
 *
 * - **Scaling** from font units to pixels is `(unit * pixelSize * kFixedOne) / unitsPerEm`,
 *   computed in `std::int64_t` and truncated once.
 * - **Curve flattening** picks a segment count from an integer curvature proxy and an integer
 *   square root, then evaluates the Bézier at `t = i/N` with the divisions done last, in
 *   `std::int64_t`.
 * - **Edge/scanline intersection** is `x0 + (x1 - x0) * (y - y0) / (y1 - y0)`, again in
 *   `std::int64_t`, with a floor division so that negative coordinates round the same way as
 *   positive ones instead of toward zero.
 * - **Coverage** is a sum of subpixel widths, and the final byte is `acc * 255 / kAccumulatorMax`.
 *
 * ## Why coverage cannot leave [0, 255]
 *
 * Issue #159 asks for "coverage values in [0,255] with no clamping surprises", and the way to
 * have no clamping surprises is to have no clamp. A pixel's accumulator gains at most
 * `kFixedOne` per sub-scanline - the span contributing to it is intersected with that pixel's
 * own column before being added - and there are exactly `kSubScanlines` of them, so the
 * accumulator is bounded above by `kAccumulatorMax` by construction. The final
 * `acc * 255 / kAccumulatorMax` therefore lands in [0, 255] with 0 and 255 both reachable and
 * neither produced by saturation. `rasterise()` contains no `std::clamp` on a coverage value,
 * and a future change that makes one necessary has broken this invariant rather than found a
 * corner case.
 *
 * ## The sampling model
 *
 * Analytic in x, sampled in y. Each pixel row is cut into `kSubScanlines` horizontal sample
 * lines; on each, the outline's crossings are computed exactly in fixed point, sorted, and
 * filled by the nonzero winding rule. A span contributes its exact overlap with each pixel
 * column, so horizontal edges are resolved to `1/kFixedOne` of a pixel while vertical detail is
 * quantised to `1/kSubScanlines`.
 *
 * The asymmetry is deliberate: exact area coverage in both axes needs a signed-area cell
 * accumulator whose correctness is much harder to see by reading it, and this is a *baker*.
 * It runs once per glyph at build time, never on a device, so the cost of 16 sample lines per
 * row buys legibility at a price nothing pays at runtime.
 */
module;

export module mdux.text.raster;

import std;
import mdux.core.result;

export namespace mdux::text::raster {

/// Every rejection code the rasteriser emits. Stable once published: S4 (#160) converts these to
/// `TXT`-prefixed diagnostics the same way the shader baker converts `Spirv.cpp`'s.
enum class RasterError : std::uint8_t {
    EmptyOutline,          ///< no contours, or every contour empty - nothing to rasterise
    MalformedContours,     ///< contour end indices are not strictly increasing, or exceed the points
    UnsupportedUnitsPerEm, ///< unitsPerEm is zero, so the font-unit-to-pixel scale is undefined
    UnsupportedPixelSize,  ///< pixelSize is zero, or larger than `kMaxPixelSize`
    BitmapTooLarge,        ///< the outline's scaled bounding box exceeds `kMaxBitmapPixels`
    CoordinateOverflow,    ///< a scaled coordinate left the range the fixed-point format holds
    OutlineTooComplex,     ///< the flattened outline exceeds `kMaxEdges` or `kMaxSweepWork`
    AllocationFailed,      ///< a buffer this call needed could not be allocated
};

[[nodiscard]] std::string_view describe(RasterError error) noexcept;

/// Subpixel resolution of the fixed-point coordinate format, as a shift. One pixel is
/// `kFixedOne` units, so an x coordinate is resolved to 1/256 of a pixel. Exposed because the
/// bitmap's `origin` fields are in whole pixels and a caller reconstructing subpixel positioning
/// needs to know the grid this module rounded coordinates against.
inline constexpr std::int32_t kFixedShift = 8;
inline constexpr std::int32_t kFixedOne   = 1 << kFixedShift;

/// Horizontal sample lines per pixel row. 16 gives 17 distinct vertical coverage levels before
/// the horizontal term refines them further, which is past the point where an 8-bit output can
/// show the difference. It is a power of two so the sub-scanline centres land on exact integers.
inline constexpr std::int32_t kSubScanlines = 16;

/// The largest value a pixel's coverage accumulator can hold: `kFixedOne` per sub-scanline.
inline constexpr std::int32_t kAccumulatorMax = kFixedOne * kSubScanlines;

/// Bounds that turn a malformed or hostile outline into a diagnostic rather than an allocation.
/// A baker feeding this a corrupt `loca` entry should get `BitmapTooLarge`, not a bad_alloc.
inline constexpr std::uint32_t kMaxPixelSize    = 4096;
inline constexpr std::uint64_t kMaxBitmapPixels = 64u * 1024u * 1024u;

/// Ceilings on the *work* a request can ask for, which the bitmap-area limit alone does not
/// bound. `contourEnds` holds `std::uint16_t` indices, so an outline may carry ~65k points, and
/// each off-curve point can flatten into `kMaxCurveSegments` edges; a tall narrow glyph can also
/// reach a large height while staying well under `kMaxBitmapPixels`. Without these two limits the
/// sweep below is quadratic in attacker-chosen quantities, which is a denial-of-service path
/// through a baker rather than a memory-safety one - it never allocates enough to be refused.
///
/// `kMaxSweepWork` counts edge-row visits: the sum, over every flattened edge, of the pixel rows
/// it spans. Real glyphs sit orders of magnitude below it - a 50-pixel-tall glyph with 200 edges
/// visits a few thousand - so the bound rejects only outlines that were never going to be text.
inline constexpr std::uint32_t kMaxEdges     = 1u << 16;
inline constexpr std::uint64_t kMaxSweepWork = 1u << 22;

/// One outline point in font units. Mirrors TrueType's `glyf` representation, which is the only
/// producer today - `onCurve == false` marks a quadratic Bézier control point.
struct OutlinePoint {
    std::int32_t x{0};
    std::int32_t y{0};
    bool         onCurve{true};
};

/**
 * @brief A glyph outline in font units: points, plus the index of each contour's last point.
 *
 * The shape `mdux.tools.truetype`'s `SimpleGlyph` carries, restated in governed types because a
 * governed module may not import a host-tools one (ADR-004). `contourEnds[i]` is the index of
 * the final point of contour `i`, so contour 0 is `points[0 .. contourEnds[0]]` and contour `i`
 * is `points[contourEnds[i-1] + 1 .. contourEnds[i]]` - TrueType's own convention, kept rather
 * than converted to offsets so the baker's translation stays a copy rather than a computation.
 *
 * Both spans are borrowed. `rasterise()` reads them and returns; it stores neither.
 */
struct Outline {
    std::span<const OutlinePoint>  points{};
    std::span<const std::uint16_t> contourEnds{};
};

/**
 * @brief An 8-bit coverage bitmap, and where it sits relative to the glyph origin.
 *
 * `coverage` is `width * height` bytes, row-major, **top row first** - the atlas orientation,
 * not the font's y-up one, so S4 can copy rows into an atlas without flipping them. `originX`
 * and `originY` are the pixel offsets from the glyph origin to the bitmap's top-left corner:
 * `originX` is typically the left side bearing and `originY` is positive for the usual case of
 * an outline that rises above the baseline.
 *
 * A glyph with no visible area - a space, or an outline that scales below one subpixel - yields
 * `width == height == 0` and an empty `coverage`, not a 1x1 bitmap of zeros.
 */
struct CoverageBitmap {
    std::uint32_t             width{0};
    std::uint32_t             height{0};
    std::int32_t              originX{0};
    std::int32_t              originY{0};
    std::vector<std::uint8_t> coverage{};
};

/// What to rasterise, and at what size. `pixelSize` is the em size in pixels: the scale applied
/// is `pixelSize / unitsPerEm`, so a 2048-upem font at `pixelSize == 32` maps 2048 font units to
/// 32 pixels.
struct RasterRequest {
    Outline       outline{};
    std::uint16_t unitsPerEm{0};
    std::uint32_t pixelSize{0};
};

/**
 * @brief Rasterises one outline to a coverage bitmap.
 *
 * Byte-identical across toolchains for identical input, which is the property issue #159 exists
 * to establish and `text-raster-determinism-crossToolchain` pins with a frozen digest.
 *
 * Reads only the spans in `request.outline`, and only for the duration of the call.
 */
[[nodiscard]] mdux::core::Result<CoverageBitmap, RasterError> rasterise(const RasterRequest& request) noexcept;

}  // namespace mdux::text::raster
