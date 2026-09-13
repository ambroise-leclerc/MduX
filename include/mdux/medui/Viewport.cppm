/**
 * @file Viewport.cppm
 * @brief Governed-zone streaming-viewport data, composition and expansion: what a `VulkanViewport`'s
 *        live content is shaped like, and how it becomes `DrawList` geometry.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 * @compliance ADR-022 Streaming viewport data and composition contract
 *
 * #322's contract and #323's expansion, in one module - exactly as `mdux.medui.trace` carries both
 * `SampleRing`'s pure geometry and `recordTrace()`'s `DrawList` expansion for `SignalTrace`. Binding
 * this to a live `ScreenPackage` (`ViewportBinding`, and the `VulkanViewportSpec` case in
 * `render()`) is `mdux.medui.screen`'s, exactly as `SignalBinding` is - this file gains no knowledge
 * of any particular screen, and neither `Schema.cppm` nor the `.medui` compiler gains a line from
 * either issue.
 *
 * ## No schema extension, and why that is the whole answer to bullet 3
 *
 * `VulkanViewportSpec` already carries `streamSource` - the same field `SignalTraceSpec` uses to
 * let a screen name *where* a live stream goes without saying what it is. That is the only
 * compiled field this contract needs. Everything else - the numeric domain, the two-colour ramp,
 * the ring itself - arrives from the caller at run time, in `WaterfallStyle` and `WaterfallGrid`,
 * exactly as `TraceStyle` and `SampleRing` already do for `SignalTrace`.
 *
 * `VulkanViewportSpec` has never carried a colour token, and that is not an oversight this issue
 * corrects. `mdux::medui::fieldColorToken()` (`Screen.cppm`) has no case for it, `tools/medui/Goldens.cpp`
 * already lists it beside `Image` and `Clock` as a component that "declares none at all", and the
 * architecture's own inventory of components that "draw from live data" - `NumericDisplay`,
 * `SignalTrace`, `StatusIndicator`, `Clock`, `TextInput` - does not name it either. A `VulkanViewport`
 * has been the one component whose content was never meant to be `ColorHash`-checkable, for the
 * same reason a live camera feed is not: there is no single governed tint a continuous numeric
 * stream could be reduced to without the reduction itself becoming the thing under test. Adding a
 * `colorToken` now would be the schema extension the epic asks to avoid needing, and it would buy
 * nothing no existing exclusion does not already grant. So this contract adds none, and the
 * two-colour ramp `WaterfallStyle` carries is caller-supplied and unverified - the same status
 * `TraceStyle`'s numeric range already has.
 *
 * ## A ring of rows, not a ring of scalars
 *
 * `mdux.medui.trace`'s `SampleRing` wraps one scalar at a time, which is right for a producer that
 * emits one reading per tick. A waterfall's producer emits a whole row at a time - one spectrum, one
 * sweep - and a per-scalar ring gives a reader no place to learn where a row boundary falls. So
 * `WaterfallGrid` wraps at row granularity: `bins` is the fixed width of every row, declared once
 * when the caller sizes its storage, and `oldestRow`/`rowCount` are `SampleRing`'s `oldest`/`count`
 * moved up one level - the ring cursor is a row index, and `at(row, col)` reads within it exactly as
 * `SampleRing::at()` reads within a scalar ring.
 *
 * ## Composition: the live extent fills the band, exactly as a trace's does
 *
 * `mdux.medui.trace`'s x-axis does not reserve room for the full `maxSamplesPerTrace` and leave the
 * rest blank while a ring is warming up - `columnFor()` spans whichever `ring.count` is current
 * across the whole band, so a three-sample trace already reaches both edges. `waterfallCellRect()`
 * follows the same rule on both axes: a caller passes the *live* `rowCount` and the ring's fixed
 * `bins`, and the grid always tiles the whole band, growing new rows into it exactly as a trace
 * grows new segments into its width. Nothing here reserves a taller band for a ring that has not
 * filled yet, and nothing needs to: an empty grid (`rowCount == 0`) simply expands to nothing, and
 * `mdux.medui.screen`'s `ViewportBinding` decides what an *unbound* node draws - not this module's
 * business, exactly as `SignalBinding` decides what an unbound trace draws.
 *
 * Division is remainder-absorbing: a band whose width is not a multiple of `bins` gives every cell
 * but the last the same size and lets the last one keep whatever is left over, so cells tile the
 * band exactly - no gap, no overflow, and therefore no clip rectangle is needed the way a stroke
 * needs one. Row 0 is the oldest live row and sits at the band's top edge; row `rowCount - 1` is the
 * newest and sits at its bottom. Column 0 is the ring's first bin, at the band's left edge. Both are
 * the same "oldest first, ascending" convention `mdux.medui.trace`'s module comment states for its
 * own axis, generalised to two.
 *
 * ## Colour: a caller-supplied two-tint ramp, never a discrete palette
 *
 * A screen with a governed colour token can only ever show as many tints as it declares, and a
 * continuous numeric stream has no natural finite palette to declare. `waterfallCellColor()`
 * interpolates linearly, per channel, between `WaterfallStyle::lowColor` and `::highColor` - the
 * same "normalise against a caller-declared range, quantise like `medui::quantise()` does" technique
 * `mdux.medui.trace` already uses for its y-axis, applied to colour instead of position. A sample
 * outside `[minimum, maximum]` clamps to whichever rail it overshot, never extrapolating past the
 * ramp's own endpoints - `TraceStyle`'s "a real excursion is shown pinned to its rail, not refused"
 * rule, unchanged.
 *
 * ## Nothing here owns a GPU resource, and nothing needs an adapter lifecycle
 *
 * `recordWaterfall()` needs no Vulkan handle of its own. Every quantity it writes - the cell
 * rectangles, the cell colours - is ordinary `mdux::draw::DrawList` geometry: solid, untextured
 * rectangles the same `addSolidRect()` every other component already calls, through the one pipeline
 * `mdux::render::UiRenderer` already owns. There is no separate GPU-owned texture, no foreign handle,
 * and therefore nothing to synchronise between frames beyond what `DrawList` already guarantees, and
 * nothing to tear down beyond the caller's own ring storage - which outlives the frame exactly as a
 * `SampleRing`'s does. Resizing is `mdux::medui::SurfaceMapping`'s concern (ADR-019): the compiled
 * node's rectangle never changes shape, only the surface's presentation scale, and that is already
 * solved. `mdux-verify-scenario` (#321, ADR-021) fixed the verification story a live stream needs: a
 * static screen gate cannot golden-check a value it does not have, and a dynamic one asserts
 * presence (`mdux::verify::regionPainted()`) rather than an exact tint - the same disposition this
 * contract inherits rather than reargues (PAR-REQ-009).
 *
 * ## Bounds, and the endoscope-monitor arithmetic they were chosen against
 *
 * `maxWaterfallRows` (16) and `maxWaterfallBins` (32) bound the *worst* frame this module will ever
 * accept, exactly as `maxSamplesPerTrace` does for a trace - a constant a device can multiply by its
 * node count before it runs, not a number that moves with a producer's sampling rate. A full-cap
 * grid costs `rows * cols` solid quads, one per cell, with no cap or joint to add: 512 quads is 2048
 * vertices and 3072 indices - **exactly half** of the 4096/6144 the committed `endoscope-monitor`
 * screen declares for its *whole* frame, leaving the other half for every label, field and trace the
 * rest of the screen already carries. Chosen against real arithmetic rather than picked and hoped,
 * the way `mdux.medui.trace`'s own comment insists a budget number should be.
 */
module;

export module mdux.medui.viewport;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;

export namespace mdux::medui {

/**
 * @brief The most history rows this module will ever accept live, at once.
 *
 * Bounds `WaterfallGrid::rowCount`, not `WaterfallGrid::bins` - the ring can be *sized* larger than
 * this (a caller who wants headroom may allocate more storage), but a frame carrying more than this
 * many *live* rows is refused. See the module comment for the arithmetic this number was chosen
 * against.
 */
inline constexpr std::size_t maxWaterfallRows = 16;

/// The most bins one row of a waterfall may declare. Fixed for the ring's whole life - unlike
/// `rowCount`, a ring does not "warm up" into a wider row.
inline constexpr std::size_t maxWaterfallBins = 32;

/**
 * @brief A caller-owned ring of rows, as this module reads it.
 *
 * An aggregate, like `SampleRing`, and for the same reason: what this proves - that the shape
 * describes a real position in `storage` - is cheap and has to be reproved every frame, because the
 * producer moves `oldestRow`/`rowCount` between them.
 *
 * `storage` holds `bins` scalars per row, row-major, for as many rows as the caller allocated -
 * `storage.size() / bins` is the ring's total capacity, which need not equal `rowCount`: a ring
 * fills from zero over a device's first frames exactly as a `SampleRing` does. `oldestRow` is the
 * index, in row units, of the oldest live row; `rowCount` is how many rows are live, at most the
 * ring's capacity.
 */
struct WaterfallGrid {
    std::span<const float> storage{};     ///< `bins` scalars per row, row-major, owned by the caller
    std::size_t            bins{0};       ///< scalars per row - fixed for the ring's life
    std::size_t            oldestRow{0};  ///< index, in rows, of the oldest live row
    std::size_t            rowCount{0};   ///< live rows, at most the ring's capacity

    /// The ring's total capacity in rows, or 0 when `bins` is 0 (a degenerate ring with no shape).
    [[nodiscard]] constexpr std::size_t totalRows() const noexcept {
        return bins == 0 ? 0 : storage.size() / bins;
    }

    /// The sample at row `row` (0 is oldest), bin `col`, or nothing when either is out of range.
    ///
    /// Bounds-checked for the reason `SampleRing::at()` is: a caller reading its own ring back
    /// deserves the same refusal the validation walk gets, rather than a subscript past the end.
    [[nodiscard]] constexpr std::optional<float> at(std::size_t row, std::size_t col) const noexcept {
        const std::size_t total = totalRows();
        if (bins == 0 || col >= bins || row >= rowCount || total == 0) {
            return std::nullopt;
        }
        const std::size_t physicalRow = (oldestRow + row) % total;
        return storage[(physicalRow * bins) + col];
    }
};

/**
 * @brief The numeric domain a waterfall's samples are read against, and the ramp they paint with.
 *
 * `minimum` and `maximum` play `TraceStyle`'s role exactly: the device's numbers, not the screen's,
 * because only the producer of the live stream knows what a sample of it means. `lowColor` and
 * `highColor` are the ramp's two ends - see the module comment for why a ramp rather than a governed
 * token is the only shape this component's content can honestly take.
 */
struct WaterfallStyle {
    float                  minimum{0.0F};  ///< the sample value the ramp's low end represents
    float                  maximum{1.0F};  ///< the sample value the ramp's high end represents
    mdux::core::ColorRgba8 lowColor{};     ///< the colour a sample at or below `minimum` paints
    mdux::core::ColorRgba8 highColor{};    ///< the colour a sample at or above `maximum` paints

    [[nodiscard]] constexpr bool operator==(const WaterfallStyle&) const noexcept = default;
};

/// Why a waterfall was refused. Every one but `ListRejected` leaves the caller's own state exactly
/// as it found it, because everything up to that point is read-only; `recordWaterfall()` rolls its
/// own writes back on `ListRejected` so the same is true of it.
enum class WaterfallError : std::uint8_t {
    MalformedGrid,    ///< `bins` is 0, does not divide `storage`, or `oldestRow`/`rowCount` is out of range
    TooManyRows,      ///< `rowCount` exceeds `maxWaterfallRows`
    TooManyBins,      ///< `bins` exceeds `maxWaterfallBins`
    NonFiniteSample,  ///< a live sample is a NaN or an infinity
    MalformedStyle,   ///< the range is empty, inverted or not finite
    BandTooSmall,     ///< the node's rectangle cannot hold one pixel per row, or per bin
    ListRejected,     ///< `DrawList` refused a cell - budget, or a degenerate quad
};

[[nodiscard]] constexpr std::string_view describe(WaterfallError error) noexcept {
    switch (error) {
        case WaterfallError::MalformedGrid:
            return "the grid's shape does not describe a position in its own storage";
        case WaterfallError::TooManyRows:
            return "the grid holds more live rows than maxWaterfallRows admits";
        case WaterfallError::TooManyBins:
            return "the grid declares more bins per row than maxWaterfallBins admits";
        case WaterfallError::NonFiniteSample:
            return "a live sample is a NaN or an infinity";
        case WaterfallError::MalformedStyle:
            return "the numeric range is empty, inverted, or not finite";
        case WaterfallError::BandTooSmall:
            return "the node's rectangle has fewer pixels than the grid has rows or bins";
        case WaterfallError::ListRejected:
            return "the draw list refused a cell - budget, or a degenerate quad";
    }
    // Named rather than defaulted so that a new enumerator is a warning at this switch instead of a
    // silent empty description later.
    return {};
}

/// The quads one full expansion of a `rows`-by-`cols` grid costs: exactly one solid rectangle per
/// cell, with nothing shared between neighbours the way a trace's segments share their caps.
///
/// Exported for the same reason `quadsForSamples()` is - a reviewer sizing a screen's `DrawBudget`
/// multiplies this rather than reconstructing the cost model from the module comment.
[[nodiscard]] constexpr std::size_t quadsForWaterfall(std::size_t rows, std::size_t cols) noexcept {
    return rows * cols;
}

/**
 * @brief The pixel rectangle cell `(row, col)` of a `rows`-by-`cols` grid occupies inside `nodeBand`.
 *
 * Remainder-absorbing division: every cell but the last in each axis gets `nodeBand.width / cols`
 * (or `nodeBand.height / rows`) pixels, and the last one in each axis keeps whatever integer
 * division left over. That is what makes the cells tile `nodeBand` exactly - the union of every
 * `waterfallCellRect()` call over a fixed `(rows, cols)` is `nodeBand` itself, with no gap and no
 * cell spilling past its far edge, so no clip rectangle is needed to promise that a device holding
 * this contract does not already give for free.
 *
 * Total rather than `Result`-returning: a rectangle is a value type with no failure mode of its own
 * (`core::Rect` itself has none), and the only ways to call this outside its contract - `rows` or
 * `cols` zero, or `row`/`col` past them - are caller bugs a validated `WaterfallGrid` already
 * excludes by construction. Both degenerate to a zero-area rectangle at `nodeBand`'s origin rather
 * than an out-of-range computation, so a caller that does call it out of contract gets a value that
 * reads as "nothing" rather than undefined behaviour.
 *
 * The parameter is `nodeBand`, not `band`: a `constexpr` function defined in a module interface has
 * its body re-checked against each importing translation unit on MSVC, and a parameter named `band`
 * triggered `C4459` (a parameter hiding a global) in `ViewportContractTests.cpp`, which declares its
 * own file-scope `band` fixture. The rename avoids the collision rather than suppressing the warning.
 */
[[nodiscard]] constexpr mdux::core::Rect
waterfallCellRect(const mdux::core::Rect& nodeBand, std::size_t rows, std::size_t cols, std::size_t row, std::size_t col) noexcept {
    if (rows == 0 || cols == 0 || row >= rows || col >= cols) {
        return mdux::core::Rect{.x = nodeBand.x, .y = nodeBand.y, .width = 0, .height = 0};
    }

    const auto cellWidth  = static_cast<mdux::core::Px>(static_cast<std::int64_t>(nodeBand.width) / static_cast<std::int64_t>(cols));
    const auto cellHeight = static_cast<mdux::core::Px>(static_cast<std::int64_t>(nodeBand.height) / static_cast<std::int64_t>(rows));

    const auto originX = static_cast<mdux::core::Px>(static_cast<std::int64_t>(col) * static_cast<std::int64_t>(cellWidth));
    const auto originY = static_cast<mdux::core::Px>(static_cast<std::int64_t>(row) * static_cast<std::int64_t>(cellHeight));

    const bool lastCol = col + 1 == cols;
    const bool lastRow = row + 1 == rows;

    return mdux::core::Rect{
        .x      = nodeBand.x + originX,
        .y      = nodeBand.y + originY,
        .width  = lastCol ? nodeBand.width - originX : cellWidth,
        .height = lastRow ? nodeBand.height - originY : cellHeight,
    };
}

/// Linear interpolation between two byte channels, rounded the way `medui::quantise()` rounds a
/// linear channel - the multiply, the add and the rounding offset kept as separate statements so
/// the result cannot depend on whether a compiler fuses them, which matters here for the same
/// cross-toolchain reason it matters there.
[[nodiscard]] constexpr std::uint8_t lerpByte(std::uint8_t low, std::uint8_t high, float t) noexcept {
    const float span       = static_cast<float>(high) - static_cast<float>(low);
    const float scaled     = t * span;
    const float positioned = static_cast<float>(low) + scaled;
    const float rounded    = positioned + 0.5F;
    return static_cast<std::uint8_t>(rounded);
}

/**
 * @brief The colour one sample of a waterfall paints, under `style`'s ramp.
 *
 * `value` is clamped to `[minimum, maximum]` before it is normalised - an excursion past the ramp's
 * declared domain paints the rail colour it overshot, exactly as `mdux.medui.trace` pins an
 * excursion to its rail rather than refusing the frame, and a NaN is treated as "at or below
 * `minimum`" rather than propagated, matching `medui::quantise()`'s "NaN maps to the low end of the
 * scale it cannot interpret" doctrine.
 *
 * The span and the offset are widened to `double` before they are subtracted, not after - exactly
 * `mdux::medui::rowFor()`'s reasoning in `Trace.cpp`. `validate()` (and the ordering check just
 * below) only requires `minimum`/`maximum` finite and `maximum > minimum`, which a range of
 * `[-FLT_MAX, FLT_MAX]` satisfies while its *difference* overflows to infinity in `float` - and at
 * the top of that range the offset overflows too, so the float path divides one infinity by another
 * and sends a NaN `t` into `lerpByte()`'s float-to-byte cast, which is undefined behaviour. The
 * difference of two finite floats is exact in `double` and cannot overflow there, so nothing is
 * rounded here that was not rounded before, and `t` is exactly rounded once on the way back to
 * `float`.
 *
 * Total, like `waterfallCellRect()`: a degenerate `style` (an empty or non-finite range) is a defect
 * `validate()` already refuses before a frame is recorded, and this function's fallback for one -
 * `style.lowColor`, unchanged - exists so a caller who calls it anyway gets a defined colour rather
 * than a divide against zero.
 */
[[nodiscard]] constexpr mdux::core::ColorRgba8 waterfallCellColor(float value, const WaterfallStyle& style) noexcept {
    if (!std::isfinite(style.minimum) || !std::isfinite(style.maximum) || !(style.maximum > style.minimum)) {
        return style.lowColor;
    }

    const double span   = static_cast<double>(style.maximum) - static_cast<double>(style.minimum);
    const double offset = static_cast<double>(value) - static_cast<double>(style.minimum);
    float        t      = static_cast<float>(offset / span);

    // `!(t > 0.0F)` rather than `t < 0.0F`, so a NaN - from a NaN `value` - lands on the low rail
    // instead of falling through both comparisons, matching `rowFor()`'s reasoning in `Trace.cpp`.
    if (!(t > 0.0F)) {
        t = 0.0F;
    } else if (t > 1.0F) {
        t = 1.0F;
    }

    return mdux::core::ColorRgba8{
        .r = lerpByte(style.lowColor.r, style.highColor.r, t),
        .g = lerpByte(style.lowColor.g, style.highColor.g, t),
        .b = lerpByte(style.lowColor.b, style.highColor.b, t),
        .a = lerpByte(style.lowColor.a, style.highColor.a, t),
    };
}

/**
 * @brief Whether `grid` and `style` describe a waterfall `nodeBand` could show, without recording
 *        anything.
 *
 * Checks every refusal this contract admits, in the order `mdux.medui.trace`'s own validation
 * checks its analogues: the style first (a malformed ramp domain makes every sample's colour
 * undefined), the grid's shape, the two type-level caps, whether `nodeBand` has room for one pixel
 * per row and per bin at the grid's *live* extent, and finally every live sample's finiteness.
 *
 * This proves a frame *could* be recorded, without recording anything - `recordWaterfall()` below
 * calls this first and turns a pass into `DrawList` primitives only once it holds, exactly as
 * `recordTrace()` validates before it expands a `SampleRing` into stroke quads. Exported on its own
 * regardless, because a caller that only wants to know whether a grid/style/band triple is drawable
 * - a scenario runner checking an `Expect` step, a test - should not need a `DrawList` to ask.
 *
 * The parameter is `nodeBand` rather than `band` for the same MSVC-module reason
 * `waterfallCellRect()`'s is - see its doc comment.
 */
[[nodiscard]] constexpr mdux::core::ResultVoid<WaterfallError>
validate(const mdux::core::Rect& nodeBand, const WaterfallGrid& grid, const WaterfallStyle& style) noexcept {
    if (!std::isfinite(style.minimum) || !std::isfinite(style.maximum) || !(style.maximum > style.minimum)) {
        return mdux::core::err(WaterfallError::MalformedStyle);
    }

    if (grid.bins == 0 || grid.storage.size() % grid.bins != 0) {
        return mdux::core::err(WaterfallError::MalformedGrid);
    }
    const std::size_t total = grid.totalRows();
    if (grid.rowCount > total || (total > 0 && grid.oldestRow >= total) || (total == 0 && grid.oldestRow != 0)) {
        return mdux::core::err(WaterfallError::MalformedGrid);
    }

    if (grid.bins > maxWaterfallBins) {
        return mdux::core::err(WaterfallError::TooManyBins);
    }
    if (grid.rowCount > maxWaterfallRows) {
        return mdux::core::err(WaterfallError::TooManyRows);
    }

    if (grid.rowCount > 0 && (nodeBand.width < static_cast<mdux::core::Px>(grid.bins) || nodeBand.height < static_cast<mdux::core::Px>(grid.rowCount))) {
        return mdux::core::err(WaterfallError::BandTooSmall);
    }

    for (std::size_t row = 0; row < grid.rowCount; ++row) {
        for (std::size_t col = 0; col < grid.bins; ++col) {
            const std::optional<float> sample = grid.at(row, col);
            if (!sample.has_value() || !std::isfinite(*sample)) {
                return mdux::core::err(WaterfallError::NonFiniteSample);
            }
        }
    }

    return {};
}

/**
 * @brief Records one waterfall of `grid` into `list`, inside `nodeBand`, under `style`'s ramp.
 *
 * @param list     the destination; each live cell is appended as one `Solid` quad
 * @param nodeBand the node's resolved rectangle, in surface pixels
 * @param grid     the caller's rows, read oldest-first, top to bottom
 * @param style    the numeric domain the samples are read against, and the colour ramp
 *
 * Calls `validate()` first, so every refusal it can make - a malformed grid, an oversized one, a
 * non-finite sample, a degenerate style, a band with no room - is made here too, unchanged, before a
 * single cell is written. Past that point the only way left to fail is `list` itself declining a
 * write, which is `ListRejected`.
 *
 * Recorded row-major, oldest row first and left bin first within each row - the ascending order the
 * module comment fixes for both axes - so a `DrawList` that merges consecutive same-clip primitives
 * into one command (see `DrawList::addRect()`) does so over the whole grid rather than restarting a
 * command at every row.
 *
 * All-or-nothing, as `recordTrace()` and `mdux::text::draw::recordRun()` are: on any refusal the
 * list is rolled back to where it stood on entry, so a frame never carries part of a waterfall. A
 * partial grid on a medical display reads as a smaller, complete one rather than as the failure it
 * is - exactly the failure mode `mdux.medui.trace`'s own "refused, never truncated" rule exists to
 * prevent, generalised from a cap on sample count to a cap on draw-list room.
 */
[[nodiscard]] mdux::core::ResultVoid<WaterfallError>
recordWaterfall(mdux::draw::DrawList& list, const mdux::core::Rect& nodeBand, const WaterfallGrid& grid, const WaterfallStyle& style) noexcept;

}  // namespace mdux::medui
