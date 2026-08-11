/**
 * @file Raster.cpp
 * @brief Implementation of the host-tools glyph rasteriser.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * Four passes, in order:
 *
 *   1. `buildEdges()` walks each contour, reconstructs TrueType's implied on-curve midpoints,
 *      scales every coordinate to fixed point, and flattens quadratic Béziers into line segments.
 *   2. The edge list's bounding box gives the bitmap's extent and origin.
 *   3. `rasterise()` sweeps sub-scanlines, intersecting edges and filling by nonzero winding.
 *   4. Each accumulator is normalised to a byte.
 *
 * Every arithmetic step is integer. See Raster.cppm's "The determinism rule" for why that is a
 * requirement rather than a preference; the short version is that this file's output gets
 * committed as evidence, and an integer pipeline makes the cross-toolchain claim a property of
 * the source instead of a property of the compile flags.
 *
 * `std::int64_t` appears wherever a product of two fixed-point coordinates can occur. Those
 * casts are load-bearing: a coordinate is up to 23 bits after scaling, so a product is up to 46,
 * which overflows `std::int32_t` silently and would do so only on large glyphs - the exact shape
 * of bug that survives a test suite built from small ones.
 */
module;

module mdux.text.raster;

import std;
import mdux.core.result;

namespace mdux::text::raster {

using mdux::core::err;
using mdux::core::Result;

namespace {

/// The widest coordinate the fixed-point format holds without a product overflowing the int64
/// intermediates below. 1 << 23 fixed-point units is 32768 pixels, far past `maxPixelSize`, so
/// hitting this means the scale computation itself produced something absurd.
constexpr std::int64_t maxFixedCoordinate = 1 << 23;

/// Largest number of line segments one quadratic Bézier may flatten into. A curve needing more
/// than this is either enormous or degenerate; capping keeps a hostile outline from producing an
/// unbounded edge list, and at 64 segments the residual error is far below one subpixel for any
/// glyph that fits `maxPixelSize`.
constexpr std::int32_t maxCurveSegments = 64;

/// Floor division for signed integers. `operator/` truncates toward zero, which rounds -3/2 to
/// -1 and 3/2 to 1 - an asymmetry that would place a scanline crossing differently on the left
/// and right halves of a glyph and put a visible bias into the coverage. Flooring is uniform.
[[nodiscard]] std::int64_t floorDiv(std::int64_t numerator, std::int64_t denominator) noexcept {
    const std::int64_t quotient  = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    if (remainder != 0 && ((remainder < 0) != (denominator < 0))) {
        return quotient - 1;
    }
    return quotient;
}

/// Integer square root by Newton's method. Used to pick a curve's segment count, so it has to be
/// exactly reproducible - `std::sqrt` on a double would be correctly rounded and therefore
/// portable in principle, but it would put a floating-point operation in the one file whose
/// contract is that it contains none.
[[nodiscard]] std::int64_t isqrt(std::int64_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    std::int64_t guess = value;
    std::int64_t next  = (guess + 1) / 2;
    while (next < guess) {
        guess = next;
        next  = (guess + value / guess) / 2;
    }
    return guess;
}

/// One flattened line segment in fixed-point pixel space. Curves are gone by this point: the
/// scanline sweep sees only straight edges, which is what keeps its intersection arithmetic a
/// single division rather than a root solve.
struct Edge {
    std::int32_t x0{0};
    std::int32_t y0{0};
    std::int32_t x1{0};
    std::int32_t y1{0};
};

/// A crossing of one sub-scanline by one edge: where, and which way the edge was going.
///
/// The direction is what makes the fill rule *nonzero* rather than even-odd. A counter wound
/// against its outer contour subtracts from the winding and leaves a hole - which is how an 'o'
/// works - while one wound the same way adds to it and stays filled. Even-odd cannot tell those
/// two apart and would punch a hole through both.
struct Crossing {
    std::int32_t x{0};
    std::int32_t direction{0};
};

/// Scales a font-unit coordinate into fixed-point pixel space.
///
/// `(unit * pixelSize * fixedOne) / unitsPerEm`, evaluated with the multiplications first so
/// the single truncation happens at the end. Doing the division first - scaling to pixels and
/// then to fixed point - would round twice and lose roughly half a subpixel per coordinate.
[[nodiscard]] std::optional<std::int32_t> scaleToFixed(std::int32_t unit, std::uint32_t pixelSize, std::uint16_t unitsPerEm) noexcept {
    const std::int64_t scaled = floorDiv(static_cast<std::int64_t>(unit) * static_cast<std::int64_t>(pixelSize) * fixedOne,
                                         static_cast<std::int64_t>(unitsPerEm));
    if (scaled > maxFixedCoordinate || scaled < -maxFixedCoordinate) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(scaled);
}

/// Chooses how many line segments a quadratic Bézier flattens into.
///
/// The metric is the control polygon's second difference, `|p0 - 2c + p1|` summed over both
/// axes, which is proportional to the curve's maximum deviation from the chord. Taking its
/// square root gives a segment count whose error falls roughly as 1/N², the standard result for
/// uniform subdivision of a quadratic. Both operations are integer, so two toolchains cannot
/// disagree about how many segments a given curve became - and since the segment count changes
/// the output bytes, that agreement is part of the determinism claim, not an implementation
/// detail underneath it.
[[nodiscard]] std::int32_t segmentsForCurve(const std::int32_t x0, const std::int32_t y0, const std::int32_t cx, const std::int32_t cy,
                                            const std::int32_t x1, const std::int32_t y1) noexcept {
    const std::int64_t dx     = std::abs(static_cast<std::int64_t>(x0) - 2 * static_cast<std::int64_t>(cx) + static_cast<std::int64_t>(x1));
    const std::int64_t dy     = std::abs(static_cast<std::int64_t>(y0) - 2 * static_cast<std::int64_t>(cy) + static_cast<std::int64_t>(y1));
    const std::int64_t metric = dx + dy;
    if (metric <= 0) {
        return 1;  // the control point is on the chord: the curve is a straight line
    }
    // Divide by fixedOne / 4 before the root so the count grows once the deviation passes a
    // quarter-pixel rather than a whole one; below that the flattening error is invisible at 8
    // bits of coverage.
    const std::int64_t segments = 1 + isqrt(floorDiv(metric * 4, fixedOne));
    return static_cast<std::int32_t>(std::min<std::int64_t>(segments, maxCurveSegments));
}

/// Evaluates a quadratic Bézier at `t = index / count`, entirely in integers.
///
/// `B(t) = (1-t)²p0 + 2t(1-t)c + t²p1`, multiplied through by `count²` so every term is an
/// integer product and the single division comes last. The int64 intermediates matter here:
/// `count²` is up to 4096 and a coordinate up to 2²³, so the numerator reaches 2³⁵.
[[nodiscard]] std::int32_t evaluateQuadratic(std::int32_t p0, std::int32_t control, std::int32_t p1, std::int32_t index,
                                             std::int32_t count) noexcept {
    const std::int64_t t  = index;
    const std::int64_t u  = static_cast<std::int64_t>(count) - t;
    const std::int64_t nn = static_cast<std::int64_t>(count) * count;
    const std::int64_t numerator =
        u * u * static_cast<std::int64_t>(p0) + 2 * t * u * static_cast<std::int64_t>(control) + t * t * static_cast<std::int64_t>(p1);
    return static_cast<std::int32_t>(floorDiv(numerator, nn));
}

/// Walks the contours, scales, reconstructs implied on-curve points, and flattens to edges.
///
/// TrueType stores a contour as a ring of points, each on- or off-curve, and allows two
/// consecutive off-curve points to imply an on-curve point at their midpoint - a shorthand that
/// real fonts use constantly. Reconstructing those midpoints is the reason this function walks a
/// resolved ring rather than consuming the raw points directly: getting it wrong produces an
/// outline that is subtly the wrong shape rather than one that fails.
[[nodiscard]] Result<std::vector<Edge>, RasterError> buildEdges(const Outline& outline, std::uint32_t pixelSize,
                                                                std::uint16_t unitsPerEm) noexcept {
    std::vector<Edge> edges;
    std::size_t       contourStart = 0;

    for (const std::uint16_t contourEnd : outline.contourEnds) {
        const std::size_t end = static_cast<std::size_t>(contourEnd);
        if (end >= outline.points.size() || end < contourStart) {
            return err(RasterError::MalformedContours);
        }
        const std::size_t count = end - contourStart + 1u;
        if (count < 2u) {
            // A contour of one point encloses no area. Skipping rather than rejecting: real
            // fonts do carry them, and they contribute nothing to coverage either way.
            contourStart = end + 1u;
            continue;
        }

        // Resolve the ring: scale every point, and insert the implied on-curve midpoint wherever
        // two off-curve points are adjacent.
        struct ResolvedPoint {
            std::int32_t x{0};
            std::int32_t y{0};
            bool         onCurve{true};
        };
        std::vector<ResolvedPoint> ring;
        ring.reserve(count * 2u);
        for (std::size_t i = 0; i < count; ++i) {
            const OutlinePoint& raw = outline.points[contourStart + i];
            const auto          sx  = scaleToFixed(raw.x, pixelSize, unitsPerEm);
            const auto          sy  = scaleToFixed(raw.y, pixelSize, unitsPerEm);
            if (!sx || !sy) {
                return err(RasterError::CoordinateOverflow);
            }
            const ResolvedPoint current{.x = *sx, .y = *sy, .onCurve = raw.onCurve};
            if (!ring.empty() && !ring.back().onCurve && !current.onCurve) {
                ring.push_back(ResolvedPoint{.x       = static_cast<std::int32_t>(floorDiv(static_cast<std::int64_t>(ring.back().x) + current.x, 2)),
                                             .y       = static_cast<std::int32_t>(floorDiv(static_cast<std::int64_t>(ring.back().y) + current.y, 2)),
                                             .onCurve = true});
            }
            ring.push_back(current);
        }
        if (ring.size() < 2u) {
            contourStart = end + 1u;
            continue;
        }
        // Close the ring, inserting a midpoint if both ends are off-curve for the same reason.
        if (!ring.back().onCurve && !ring.front().onCurve) {
            ring.push_back(ResolvedPoint{.x       = static_cast<std::int32_t>(floorDiv(static_cast<std::int64_t>(ring.back().x) + ring.front().x, 2)),
                                         .y       = static_cast<std::int32_t>(floorDiv(static_cast<std::int64_t>(ring.back().y) + ring.front().y, 2)),
                                         .onCurve = true});
        }

        // A contour may begin on an off-curve point. Rotating the ring so it starts on-curve
        // means the walk below always has a defined segment start; the alternative, synthesising
        // a start point, changes the outline.
        std::size_t startIndex = ring.size();
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i].onCurve) {
                startIndex = i;
                break;
            }
        }
        if (startIndex == ring.size()) {
            // Every point is off-curve. The implied-midpoint insertion above guarantees this
            // cannot happen for a contour of two or more points, so reaching here means the ring
            // is degenerate rather than merely unusual.
            contourStart = end + 1u;
            continue;
        }

        const auto at = [&ring, startIndex](std::size_t i) noexcept -> const ResolvedPoint& {
            return ring[(startIndex + i) % ring.size()];
        };

        std::int32_t penX = at(0).x;
        std::int32_t penY = at(0).y;
        const auto   emit = [&edges](std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
            if (ay != by) {  // horizontal edges cross no sub-scanline and only add work
                edges.push_back(Edge{.x0 = ax, .y0 = ay, .x1 = bx, .y1 = by});
            }
        };

        for (std::size_t i = 1; i <= ring.size(); ++i) {
            const ResolvedPoint& point = at(i % ring.size());
            if (point.onCurve) {
                emit(penX, penY, point.x, point.y);
                penX = point.x;
                penY = point.y;
                continue;
            }
            // Off-curve: the segment is a quadratic ending at the next on-curve point, which is
            // either the next ring entry or the implied midpoint the resolution pass inserted.
            const ResolvedPoint& next  = at((i + 1u) % ring.size());
            const std::int32_t   endX  = next.x;
            const std::int32_t   endY  = next.y;
            // The curve's start is captured before the loop and never moves. `penX`/`penY` walk
            // forward to carry each emitted segment's previous endpoint, so evaluating against
            // them instead would feed the previous *sample* to the evaluator as p0 from step 2
            // onward - a different curve, and one whose endpoints still land correctly because
            // the final sample's `u` term is zero. That makes the error invisible to any test
            // that only checks where a curve starts and ends; measured on a 5-segment curve it
            // reaches 266 fixed units, just over one pixel.
            const std::int32_t   startX  = penX;
            const std::int32_t   startY  = penY;
            const std::int32_t   samples = segmentsForCurve(startX, startY, point.x, point.y, endX, endY);
            for (std::int32_t step = 1; step <= samples; ++step) {
                const std::int32_t nx = evaluateQuadratic(startX, point.x, endX, step, samples);
                const std::int32_t ny = evaluateQuadratic(startY, point.y, endY, step, samples);
                emit(penX, penY, nx, ny);
                penX = nx;
                penY = ny;
            }
            ++i;  // the on-curve endpoint was consumed as this curve's terminus
        }

        contourStart = end + 1u;
    }

    return edges;
}

}  // namespace

std::string_view describe(RasterError error) noexcept {
    switch (error) {
        case RasterError::EmptyOutline:
            return "the outline has no contours, or none that enclose area";
        case RasterError::MalformedContours:
            return "contour end indices are not increasing, or point past the end of the point list";
        case RasterError::UnsupportedUnitsPerEm:
            return "unitsPerEm is zero; the font-unit-to-pixel scale would be undefined";
        case RasterError::UnsupportedPixelSize:
            return "pixelSize is zero or exceeds the supported maximum";
        case RasterError::BitmapTooLarge:
            return "the scaled outline's bounding box exceeds the supported bitmap area";
        case RasterError::CoordinateOverflow:
            return "a scaled coordinate left the range the fixed-point format represents";
        case RasterError::OutlineTooComplex:
            return "the flattened outline exceeds the supported edge count or sweep-work budget";
        case RasterError::AllocationFailed:
            return "a buffer this rasterisation needed could not be allocated";
    }
    return "unknown rasteriser error";
}

namespace {

/// The body of `rasterise()`. Allowed to throw, because it allocates: the public entry point
/// below is the `noexcept` boundary that turns an allocation failure into a diagnostic.
[[nodiscard]] Result<CoverageBitmap, RasterError> rasteriseImpl(const RasterRequest& request) {
    if (request.unitsPerEm == 0) {
        return err(RasterError::UnsupportedUnitsPerEm);
    }
    if (request.pixelSize == 0 || request.pixelSize > maxPixelSize) {
        return err(RasterError::UnsupportedPixelSize);
    }
    if (request.outline.contourEnds.empty() || request.outline.points.empty()) {
        return err(RasterError::EmptyOutline);
    }
    // Strictly increasing end indices, checked before any scaling so a malformed outline costs
    // nothing. `buildEdges()` re-checks the bound against the point count; this is the ordering
    // half, which it cannot see one contour at a time.
    std::optional<std::uint16_t> previousEnd;
    for (const std::uint16_t contourEnd : request.outline.contourEnds) {
        if (previousEnd.has_value() && contourEnd <= *previousEnd) {
            return err(RasterError::MalformedContours);
        }
        previousEnd = contourEnd;
    }

    auto edges = buildEdges(request.outline, request.pixelSize, request.unitsPerEm);
    if (!edges.has_value()) {
        return err(edges.error());
    }
    if (edges->size() > maxEdges) {
        // The second half of the work bound. An outline can reach this without any single
        // contour looking unusual, because every off-curve point may flatten into
        // maxCurveSegments edges - so the check belongs on the flattened count, not the input's.
        return err(RasterError::OutlineTooComplex);
    }
    if (edges->empty()) {
        // Every contour was degenerate or horizontal: a real outline with no enclosed area, such
        // as a space. An empty bitmap, not a 1x1 of zeros - see CoverageBitmap's contract.
        return CoverageBitmap{};
    }

    // Bounding box in fixed point, then in whole pixels. The box is expanded outward to pixel
    // boundaries, so a partially covered edge pixel is inside the bitmap rather than clipped.
    std::int32_t minX = std::numeric_limits<std::int32_t>::max();
    std::int32_t minY = std::numeric_limits<std::int32_t>::max();
    std::int32_t maxX = std::numeric_limits<std::int32_t>::min();
    std::int32_t maxY = std::numeric_limits<std::int32_t>::min();
    for (const Edge& edge : *edges) {
        minX = std::min({minX, edge.x0, edge.x1});
        minY = std::min({minY, edge.y0, edge.y1});
        maxX = std::max({maxX, edge.x0, edge.x1});
        maxY = std::max({maxY, edge.y0, edge.y1});
    }

    const std::int64_t pixelMinX = floorDiv(minX, fixedOne);
    const std::int64_t pixelMinY = floorDiv(minY, fixedOne);
    const std::int64_t pixelMaxX = floorDiv(maxX + fixedOne - 1, fixedOne);
    const std::int64_t pixelMaxY = floorDiv(maxY + fixedOne - 1, fixedOne);

    const std::int64_t width  = pixelMaxX - pixelMinX;
    const std::int64_t height = pixelMaxY - pixelMinY;
    if (width <= 0 || height <= 0) {
        return CoverageBitmap{};
    }
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > maxBitmapPixels) {
        return err(RasterError::BitmapTooLarge);
    }

    // Bucket the edges by the pixel rows they span, so the sweep visits an edge only on rows it
    // can actually cross.
    //
    // Without this the sweep is `height * subScanlines * edgeCount`, and none of those three is
    // bounded by the bitmap-area check: `contourEnds` holds uint16 indices, so an outline can
    // carry ~65k points, each off-curve one flattening into up to maxCurveSegments edges, while
    // a tall narrow glyph reaches a large height well under maxBitmapPixels. The product is
    // quadratic in attacker-chosen quantities - a denial-of-service path through a baker that
    // never allocates enough to be refused. Bucketing makes the cost proportional to the geometry
    // instead: the total is the sum, over edges, of the rows each one spans.
    //
    // Stored as CSR (offsets plus a flat index array) rather than a vector-of-vectors, because
    // `height` allocations of a few elements each is the shape that turns a tall glyph slow for
    // reasons unrelated to its outline.
    // The row span each edge occupies, computed once and reused by both passes below. The
    // half-open convention matches the sweep's: an edge covers [top, bottom), so its last row is
    // the one containing `bottom - 1`.
    const auto rowSpanOf = [pixelMinY, height](const Edge& edge) noexcept -> std::pair<std::int64_t, std::int64_t> {
        const std::int64_t topY    = std::min(edge.y0, edge.y1);
        const std::int64_t bottomY = std::max(edge.y0, edge.y1);
        return {std::max<std::int64_t>(floorDiv(topY, fixedOne) - pixelMinY, 0),
                std::min<std::int64_t>(floorDiv(bottomY - 1, fixedOne) - pixelMinY, height - 1)};
    };

    // Pass one: total the work and count each row's edges. Both come from the same spans, so the
    // budget is checked before anything proportional to it is allocated.
    std::vector<std::uint32_t> rowOffset(static_cast<std::size_t>(height) + 1u, 0u);
    std::uint64_t              sweepWork = 0;
    for (const Edge& edge : *edges) {
        const auto [firstRow, lastRow] = rowSpanOf(edge);
        if (lastRow < firstRow) {
            continue;
        }
        sweepWork += static_cast<std::uint64_t>(lastRow - firstRow + 1);
        if (sweepWork > maxSweepWork) {
            return err(RasterError::OutlineTooComplex);
        }
        for (std::int64_t r = firstRow; r <= lastRow; ++r) {
            rowOffset[static_cast<std::size_t>(r) + 1u] += 1u;
        }
    }
    for (std::size_t r = 1; r < rowOffset.size(); ++r) {
        rowOffset[r] += rowOffset[r - 1u];
    }

    // Pass two: fill the flat index array, walking a cursor per row.
    std::vector<std::uint32_t> rowEdges(rowOffset.back(), 0u);
    {
        std::vector<std::uint32_t> cursor(rowOffset.begin(), rowOffset.end() - 1);
        for (std::uint32_t index = 0; index < edges->size(); ++index) {
            const auto [firstRow, lastRow] = rowSpanOf((*edges)[index]);
            for (std::int64_t r = firstRow; r <= lastRow; ++r) {
                rowEdges[cursor[static_cast<std::size_t>(r)]++] = index;
            }
        }
    }

    // Accumulators, one per pixel, in subpixel-width units. `accumulatorMax` is the ceiling by
    // construction: each sub-scanline contributes at most fixedOne to any one pixel, because
    // the span added is intersected with that pixel's own column first.
    std::vector<std::int32_t> accumulator(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

    std::vector<Crossing> crossings;

    // Coverage is accumulated as *differences* along the row, then prefix-summed once at the end
    // of it, rather than written pixel by pixel as each span is filled.
    //
    // Writing per pixel makes the fill cost `height * subScanlines * spanWidth`, which nothing
    // above bounds: a solid 65536 x 1024 rectangle satisfies both the bitmap-area and sweep-work
    // limits while asking for about 1.07e9 accumulator writes. Encoding a span as two range-adds
    // makes each one O(1) regardless of width, and the single prefix pass costs O(width) per row.
    // Total fill work becomes O(bitmap area), which `maxBitmapPixels` already bounds - so this
    // removes the unbounded quantity instead of adding another ceiling to check against it.
    //
    // One entry longer than the row: a span ending on the last pixel writes its closing
    // difference at index `width`.
    std::vector<std::int32_t> rowDelta(static_cast<std::size_t>(width) + 1u, 0);

    for (std::int64_t row = 0; row < height; ++row) {
        const std::uint32_t rowBegin = rowOffset[static_cast<std::size_t>(row)];
        const std::uint32_t rowEnd   = rowOffset[static_cast<std::size_t>(row) + 1u];
        if (rowBegin == rowEnd) {
            continue;  // no edge reaches this row
        }
        std::fill(rowDelta.begin(), rowDelta.end(), 0);
        for (std::int32_t sub = 0; sub < subScanlines; ++sub) {
            // The sub-scanline's y, sampled at the centre of its band. subScanlines is a power
            // of two and divides 2 * fixedOne exactly, so this is an integer with no rounding.
            const std::int64_t sampleY =
                (pixelMinY + row) * fixedOne + (2 * static_cast<std::int64_t>(sub) + 1) * fixedOne / (2 * subScanlines);

            crossings.clear();
            for (std::uint32_t slot = rowBegin; slot < rowEnd; ++slot) {
                const Edge&        edge    = (*edges)[rowEdges[slot]];
                const std::int32_t topY    = std::min(edge.y0, edge.y1);
                const std::int32_t bottomY = std::max(edge.y0, edge.y1);
                // Half-open in y: an edge covers [top, bottom). Without this, a vertex shared by
                // two edges would be counted twice on the scanline that passes exactly through
                // it, flipping the winding and punching a hole in the fill.
                if (sampleY < topY || sampleY >= bottomY) {
                    continue;
                }
                const std::int64_t dy = static_cast<std::int64_t>(edge.y1) - edge.y0;
                const std::int64_t dx = static_cast<std::int64_t>(edge.x1) - edge.x0;
                const std::int64_t x  = static_cast<std::int64_t>(edge.x0) + floorDiv(dx * (sampleY - edge.y0), dy);
                crossings.push_back(Crossing{.x = static_cast<std::int32_t>(x), .direction = dy > 0 ? 1 : -1});
            }
            if (crossings.size() < 2u) {
                continue;
            }

            // Sorting on the pair, not just x, so the order is a total one: two crossings at the
            // same x with different directions must land in a fixed sequence or the sweep below
            // could differ between standard-library implementations, and this file's whole claim
            // is that it cannot.
            std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) noexcept {
                return a.x != b.x ? a.x < b.x : a.direction < b.direction;
            });

            std::int32_t winding  = 0;
            std::int32_t spanFrom = 0;
            for (const Crossing& crossing : crossings) {
                const std::int32_t previous = winding;
                winding += crossing.direction;
                if (previous == 0 && winding != 0) {
                    spanFrom = crossing.x;
                    continue;
                }
                if (previous != 0 && winding == 0) {
                    // Clip to the bitmap, then record the span as range-adds on rowDelta. The
                    // three ranges are the partially covered first pixel, the fully covered
                    // interior, and the partially covered last pixel - each an O(1) pair of
                    // differences rather than a write per pixel.
                    const std::int64_t left  = std::max<std::int64_t>(spanFrom, pixelMinX * fixedOne);
                    const std::int64_t right = std::min<std::int64_t>(crossing.x, pixelMaxX * fixedOne);
                    if (right <= left) {
                        continue;
                    }
                    const std::int64_t firstPixel = floorDiv(left, fixedOne) - pixelMinX;
                    const std::int64_t lastPixel  = floorDiv(right - 1, fixedOne) - pixelMinX;
                    const auto         addRange   = [&rowDelta](std::int64_t from, std::int64_t to, std::int32_t amount) noexcept {
                        rowDelta[static_cast<std::size_t>(from)] += amount;
                        rowDelta[static_cast<std::size_t>(to)] -= amount;
                    };
                    if (firstPixel == lastPixel) {
                        addRange(firstPixel, firstPixel + 1, static_cast<std::int32_t>(right - left));
                        continue;
                    }
                    const std::int64_t firstPixelRight = (pixelMinX + firstPixel + 1) * fixedOne;
                    const std::int64_t lastPixelLeft   = (pixelMinX + lastPixel) * fixedOne;
                    addRange(firstPixel, firstPixel + 1, static_cast<std::int32_t>(firstPixelRight - left));
                    addRange(lastPixel, lastPixel + 1, static_cast<std::int32_t>(right - lastPixelLeft));
                    if (lastPixel > firstPixel + 1) {
                        addRange(firstPixel + 1, lastPixel, fixedOne);
                    }
                }
            }
        }

        // Resolve the row's differences into per-pixel coverage. The running total is the sum of
        // every span that covered this pixel across all subScanlines, so it is bounded by
        // accumulatorMax exactly as the per-pixel version was.
        std::int32_t running = 0;
        for (std::int64_t px = 0; px < width; ++px) {
            running += rowDelta[static_cast<std::size_t>(px)];
            accumulator[static_cast<std::size_t>(row * width + px)] = running;
        }
    }

    CoverageBitmap bitmap;
    bitmap.width   = static_cast<std::uint32_t>(width);
    bitmap.height  = static_cast<std::uint32_t>(height);
    bitmap.originX = static_cast<std::int32_t>(pixelMinX);
    // Font space is y-up and the bitmap is top-row-first, so the top edge of the box is the one
    // furthest above the baseline. `originY` is that distance, positive for a glyph that rises.
    bitmap.originY = static_cast<std::int32_t>(pixelMaxY);
    bitmap.coverage.resize(accumulator.size());
    for (std::size_t i = 0; i < accumulator.size(); ++i) {
        // Row flip: accumulator row 0 is the bottom of the box in font space, bitmap row 0 is
        // the top.
        const std::size_t row    = i / static_cast<std::size_t>(width);
        const std::size_t column = i % static_cast<std::size_t>(width);
        const std::size_t target = (static_cast<std::size_t>(height) - 1u - row) * static_cast<std::size_t>(width) + column;
        // No clamp, deliberately: `accumulator[i]` is bounded by accumulatorMax by construction,
        // so this is in [0, 255] already. See Raster.cppm on why a clamp here would be a bug
        // rather than a safety net.
        bitmap.coverage[target] = static_cast<std::uint8_t>(accumulator[i] * 255 / accumulatorMax);
    }
    return bitmap;
}

}  // namespace

Result<CoverageBitmap, RasterError> rasterise(const RasterRequest& request) noexcept {
    // The `noexcept` boundary, and the reason `rasteriseImpl()` is allowed to throw.
    //
    // The header promises a diagnostic rather than a `bad_alloc` for a hostile outline, and the
    // size limits exist to make that promise keepable - but the accumulator alone can ask for
    // 256 MiB at maxBitmapPixels, and `std::vector` reports failure by throwing. Declaring the
    // entry point `noexcept` without catching that would turn an ordinary out-of-memory condition
    // into `std::terminate`, taking down the baker instead of failing one glyph.
    //
    // `std::length_error` is caught alongside `bad_alloc` because `resize()` raises it when a
    // request exceeds `max_size()`, which is the same condition arriving by a different name.
    // The final `catch (...)` is defensive: nothing else on this path throws anything else, and
    // if that ever stops being true this still returns rather than terminating.
    //
    // This block is what put the module in the host-tools zone (#116): governed code does not
    // throw, and ADR-005 makes that a rule rather than a preference. See Raster.cppm on why the
    // zone moved rather than the code.
    try {
        return rasteriseImpl(request);
    } catch (const std::bad_alloc&) {
        return err(RasterError::AllocationFailed);
    } catch (const std::length_error&) {
        return err(RasterError::AllocationFailed);
    } catch (...) {
        return err(RasterError::AllocationFailed);
    }
}

}  // namespace mdux::text::raster
