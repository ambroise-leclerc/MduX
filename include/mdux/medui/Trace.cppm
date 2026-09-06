/**
 * @file Trace.cppm
 * @brief Governed-zone waveform expansion: a caller-owned ring of samples into stroke quads.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Part of MduXCore, which is what puts it under `mdux-governed-lint`, `governed.noThrow.symbolScan`
 * and `screen.noheap.symbolScan` without any of the three being told about it.
 *
 * This module is to `SignalTrace` what `mdux.text.draw` is to `Label`: the expansion step, split out
 * from the screen runtime that decides *whether* to call it. `mdux.medui.screen` owns the binding
 * and the composition; this owns the geometry, so the geometry can be tested against numbers rather
 * than only against a rendered frame.
 *
 * ## The samples are the caller's, and stay the caller's
 *
 * A waveform's producer is a device driver on a timer, not a UI. `SampleRing` is therefore a view of
 * storage the caller allocated once and writes into forever - this module never owns it, never
 * copies it into a scratch buffer, and never asks it to be contiguous or ordered. What it does is
 * read `count` samples starting at `oldest`, wrapping, which is the shape a ring already has.
 *
 * The consequence worth stating is that the ring is **mutable between frames and validated on every
 * one**. A `create()` that proved `oldest` and `count` consistent once, as `TextBinding` proves its
 * packages, would be proving it of a snapshot the producer has since moved. So the validation is
 * per-frame and bounded: three comparisons before the walk, plus one finiteness test per sample.
 *
 * ## Segments as quads, joints as caps
 *
 * Each pair of consecutive samples becomes one quad: the segment displaced by half the stroke width
 * along its normal. Each sample additionally becomes a small square quad centred on it.
 *
 * Both are built from one value - the centre of the pixel the sample quantised onto, which is the
 * pixel's corner plus half a pixel. That half is not a detail: a stroke through a pixel *corner*
 * covers half of each of two rows, so a 1px trace drawn that way is two rows of 50% coverage rather
 * than one row of the tint, and `ColorHash`'s "a fully covered pixel is exactly the tint" would find
 * no such pixel anywhere in the node. Building the cap as a quad rather than as the `core::Rect` an
 * axis-aligned square would naturally be is what lets both primitives share that one centre: a
 * rect's edges are integers, and an even stroke width centred on a pixel centre has half-integer
 * edges.
 *
 * That square is the "round-cap overdraw" the epic's design note asks for, and it is worth being
 * exact about what it is rather than letting the name carry a claim the code does not make. A true
 * round join is a disc; this is the disc's bounding square. At the stroke widths this module admits
 * - 1 to 3 pixels - a disc and its bounding square differ by at most the four corner pixels of a 3x3
 * block, and at 1px they are the same pixel. That is the whole reason the width is capped at 3
 * (`maxStrokeWidth`): past it the approximation starts to be visible as a squared-off join, and a
 * component that quietly stopped looking like what its documentation says is worse than one that
 * refuses a width it cannot draw honestly.
 *
 * The alternative - mitred joins - needs the intersection of two offset lines, which is unbounded as
 * the angle between segments closes: a near-reversal produces a spike whose length has no limit, and
 * a spike is exactly the artefact a waveform must not invent. Overdraw has no such failure mode, and
 * costs one extra quad per sample.
 *
 * ## Integer positions, and the one float this cannot avoid
 *
 * Which *pixel* a sample lands on is decided in integers. x is a whole column computed by integer
 * division, so two toolchains cannot disagree about which column a sample is in; y is a whole row,
 * the sample normalised against the caller's declared range and quantised exactly as
 * `medui::quantise()` turns a channel into a byte, with the multiply and the add kept as separate
 * statements so the rounding cannot depend on whether the compiler fuses them. The half-pixel that
 * turns that pixel into a centre is a literal, added last.
 *
 * What stays floating point is the **normal**: the perpendicular offset that gives a segment its
 * width is `half / length` along a direction, and `length` is a square root that is irrational for
 * all but a handful of slopes. Rounding it to whole pixels would make a 1px stroke alternate between
 * one and two pixels wide as the slope changed. `std::sqrt` is safe to depend on here in a way
 * `std::exp` was not for ADR-008: IEEE 754 requires it to be correctly rounded, so every conforming
 * implementation returns the same bits for the same input, which is the property a frame compared
 * across toolchains needs.
 *
 * ## Refused, never truncated
 *
 * A ring holding more than `maxSamplesPerTrace` samples is `TooManySamples`. It is not the last *n*
 * of them, and it is not the first: a waveform silently showing a window other than the one the
 * caller believes it is showing is the failure this refusal exists to prevent, and on a medical
 * display it is indistinguishable from a correct reading of different data.
 *
 * The second refusal is the budget's, and it belongs to `DrawList` rather than to this module: an
 * expansion that does not fit rolls back to where the trace began and reports `ListRejected`, so a
 * frame never carries half a waveform. Both bounds are knowable before a device runs - the cap is a
 * constant and the budget is baked into the compiled screen - which is what makes "the storage is
 * sized once and never grown" a property rather than an intention.
 */
module;

export module mdux.medui.trace;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;

export namespace mdux::medui {

/**
 * @brief The largest trace this module will expand, in samples.
 *
 * The type-level cap `maxGlyphsPerRun` is for a `Label`, and it exists for the same reason: per-node
 * work has to be a constant a device can multiply by its node count before it runs, rather than a
 * number that moves when a driver changes its sampling rate.
 *
 * 256, the same number, and chosen the same way - large enough for the window a demonstrator
 * classifies (180 samples at 180 Hz is one second of ECG) and small enough that a reviewer can
 * multiply it against a `DrawBudget` in their head. A trace costs `2n - 1` quads, so 256 samples is
 * 511 quads, 2044 vertices and 3066 indices: comfortably inside the 4096/6144 the committed
 * endoscope screen declares, and visibly so rather than by a margin nobody checked.
 */
inline constexpr std::size_t maxSamplesPerTrace = 256;

/**
 * @brief The widest stroke the cap approximation stays honest at, in pixels.
 *
 * See the module comment: the joint cap is a square standing in for a disc, and the two are
 * indistinguishable only while the disc is small. Three pixels is where that stops being true.
 */
inline constexpr mdux::core::Px maxStrokeWidth = 3;

/**
 * @brief A caller-owned ring of samples, as this module reads it.
 *
 * An aggregate rather than a `create()`-guarded class, and deliberately so. `TextBinding` is guarded
 * because what it proves - four artifacts agreeing - is expensive and decided once. This proves
 * three comparisons about numbers a producer changes between every frame, so a guard would be
 * proving them of a state that has already moved. See the module comment.
 *
 * `storage` is the ring's fixed allocation, made once. `oldest` is the index in it of the oldest
 * live sample and `count` how many there are; a producer that has not yet filled the ring has
 * `oldest == 0` and a growing `count`, and one that has wrapped has a moving `oldest` and
 * `count == storage.size()`.
 */
struct SampleRing {
    std::span<const float> storage{};  ///< the ring's storage, owned by the caller
    std::size_t            oldest{0};  ///< index of the oldest live sample
    std::size_t            count{0};   ///< live samples, at most `storage.size()`

    /// The `index`-th sample counting from the oldest, or nothing when `index` is past `count`.
    ///
    /// Bounds-checked rather than a raw subscript: the walk below could be written to be provably in
    /// range, but this type is exported and a caller reading its own ring back deserves the same
    /// refusal the expansion gets.
    [[nodiscard]] constexpr std::optional<float> at(std::size_t index) const noexcept {
        if (index >= count || storage.empty()) {
            return std::nullopt;
        }
        return storage[(oldest + index) % storage.size()];
    }
};

/**
 * @brief How a trace is drawn, and against what range its samples are read.
 *
 * These are the device's numbers, not the screen's, and that split is the point. A compiled screen
 * says *where* a waveform goes and *in what tint* - both validated names, both in an artifact four
 * CI legs byte-compare. It cannot say what a sample of `ECG_LEAD_II` means in millivolts, because
 * that is a property of the amplifier the host owns and not of the layout. So the range arrives with
 * the samples, from the caller that produces them.
 *
 * `minimum` maps to the bottom edge of the node's band and `maximum` to the top - value up, the
 * convention every physiological trace is read with. A sample outside the range is **clamped to the
 * band, not refused**: an excursion past the display range is a real reading that a monitor shows
 * pinned to its rail, and refusing the frame would blank the screen at the moment the waveform
 * became most interesting. A range that is empty or not finite *is* refused, because it makes every
 * sample's position undefined rather than extreme.
 */
struct TraceStyle {
    float          minimum{0.0F};   ///< the sample value the band's bottom edge represents
    float          maximum{1.0F};   ///< the sample value the band's top edge represents
    mdux::core::Px strokeWidth{1};  ///< 1 to `maxStrokeWidth` pixels

    [[nodiscard]] constexpr bool operator==(const TraceStyle&) const noexcept = default;
};

/// Why a trace was refused. Every one leaves the draw list exactly as it was found.
enum class TraceError : std::uint8_t {
    MalformedRing,    ///< `oldest` or `count` does not describe a position in `storage`
    TooManySamples,   ///< the ring holds more than `maxSamplesPerTrace`
    NonFiniteSample,  ///< a live sample is a NaN or an infinity
    MalformedStyle,   ///< the range is empty or not finite, or the stroke width is out of range
    BandTooSmall,     ///< the node's rectangle cannot hold a band of this stroke width
    ListRejected,     ///< `DrawList` refused a primitive - budget, or a degenerate quad
};

[[nodiscard]] std::string_view describe(TraceError error) noexcept;

/// The quads one trace of `samples` costs at worst: one cap per sample, one quad per segment.
///
/// Exposed so a caller sizing storage, or a reviewer checking a budget, reads the cost model rather
/// than reconstructing it. An upper bound rather than an exact count: a segment between two samples
/// that land on the same pixel is skipped, because the caps at its endpoints already cover every
/// pixel it would have. A budget is a ceiling, so the bound is the number that belongs in one.
///
/// Zero for a ring too short to have a segment, which is not an error - a trace with one sample or
/// none draws nothing and says so by recording nothing.
[[nodiscard]] constexpr std::size_t quadsForSamples(std::size_t samples) noexcept {
    return samples < 2 ? 0 : (2 * samples) - 1;
}

/**
 * @brief Records one trace of `ring` into `list`, inside `band`.
 *
 * @param list  the destination; the trace is appended as `Solid` quads
 * @param band  the node's resolved rectangle, in surface pixels
 * @param ring  the caller's samples, read oldest-first, left to right
 * @param style the range the samples are normalised against, and the stroke width
 * @param color the tint the stroke is drawn in
 *
 * Allocation-free and `noexcept`. The path is inset so that the stroke - half its width either side
 * of a pixel centre - stays inside `band`: every vertex this records lies within the rectangle, so a
 * trace cannot spill onto its neighbour and the runtime needs no clip rectangle to promise it. The
 * cost is that the band a trace can reach is `strokeWidth / 2` pixels narrower and shorter than the
 * node on each side, which is why a node too small to hold its own stroke is `BandTooSmall` rather
 * than a trace drawn slightly outside it.
 *
 * All-or-nothing, as `mdux::text::draw::recordRun()` is: on any refusal the list is rolled back to
 * where it stood on entry, so a frame never carries a partial waveform. A fragment of a trace reads
 * as a flat line, which on a medical display is a reading rather than an absence.
 */
[[nodiscard]] mdux::core::ResultVoid<TraceError>
recordTrace(mdux::draw::DrawList& list, const mdux::core::Rect& band, const SampleRing& ring, const TraceStyle& style, mdux::core::ColorRgba8 color) noexcept;

}  // namespace mdux::medui
