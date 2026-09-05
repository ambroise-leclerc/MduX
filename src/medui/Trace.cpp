/**
 * @file Trace.cpp
 * @brief Implementation of the governed waveform expansion.
 */

module;

module mdux.medui.trace;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;

namespace mdux::medui {

namespace {

using mdux::core::Px;

/**
 * @brief One sample's position: the centre of the pixel it was quantised onto.
 *
 * Half a pixel, added once, and it is the difference between a stroke that lands on the pixels its
 * width says and one that straddles two rows. Surface coordinates address a pixel's top-left corner,
 * so pixel (px, py) covers the square from (px, py) to (px + 1, py + 1) and its centre is at
 * (px + 0.5, py + 0.5). A 1px stroke through that centre covers exactly that pixel; a 1px stroke
 * through the *corner* covers half of each of two.
 *
 * Both the cap and the segment are built from this one value, which is why the cap is a quad rather
 * than the `core::Rect` an axis-aligned square would naturally be: a rect's edges are integers, and
 * an even stroke width centred on a pixel centre has half-integer edges. Two primitives built from
 * one float centre cannot disagree about where the path is; a rect and a quad would disagree by half
 * a pixel at every even width.
 */
struct SamplePoint {
    float x{0.0F};
    float y{0.0F};
};

/// The square quad a joint cap occupies: the bounding square of the disc a round join would draw.
[[nodiscard]] std::array<mdux::draw::Point2F, 4> capQuad(SamplePoint at, float halfWidth) noexcept {
    return std::array<mdux::draw::Point2F, 4>{
        mdux::draw::Point2F{.x = at.x - halfWidth, .y = at.y - halfWidth},
        mdux::draw::Point2F{.x = at.x + halfWidth, .y = at.y - halfWidth},
        mdux::draw::Point2F{.x = at.x + halfWidth, .y = at.y + halfWidth},
        mdux::draw::Point2F{.x = at.x - halfWidth, .y = at.y + halfWidth}
    };
}

/**
 * @brief The row a sample lands on inside a band of `rows` rows, counting down from the top.
 *
 * The same shape as `medui::quantise()`, and for the same reason: the multiply and the add are
 * separate statements so neither the rounding nor the result depends on whether the compiler fuses
 * them. `mdux_enforce_fp_determinism(MduXCore)` already turns contraction off for this target; this
 * is the belt to that pair of braces, because a waveform compared across toolchains cannot afford a
 * last-bit difference in a vertex.
 *
 * Clamping rather than refusing is `TraceStyle`'s documented decision: an excursion past the display
 * range is a real reading a monitor pins to its rail.
 */
[[nodiscard]] Px rowFor(float sample, const TraceStyle& style, Px rows) noexcept {
    // Widened before subtracting, not afterwards. `create()` checks that the minimum, the maximum
    // and every sample are finite, which does not make their *differences* finite: a range of
    // [-FLT_MAX, FLT_MAX] passes every one of those checks and still overflows to an infinite span,
    // and at the top of that range `sample - minimum` overflows too. The float path then divided one
    // infinity by another, and the NaN fell into the `!(t > 0.0F)` branch below - so the largest
    // sample a screen can carry drew on the bottom rail, indistinguishable from the smallest. A
    // full-scale transition rendered as a flat line, which is the one failure a monitor must not
    // have.
    //
    // The difference of two floats is exact in double, so nothing is rounded here that was not
    // rounded before, and the division is exactly rounded once on the way back to float. Rail
    // clamping below is unchanged and still does the work for finite excursions.
    const double span       = static_cast<double>(style.maximum) - static_cast<double>(style.minimum);
    const double offset     = static_cast<double>(sample) - static_cast<double>(style.minimum);
    const auto   normalised = static_cast<float>(offset / span);

    float t = normalised;
    // `!(t > 0.0F)` rather than `t < 0.0F`, so a NaN lands on the bottom rail instead of falling
    // through both comparisons - the caller has already refused those, and being total is one test.
    if (!(t > 0.0F)) {
        t = 0.0F;
    } else if (t > 1.0F) {
        t = 1.0F;
    }

    const float scaled     = t * static_cast<float>(rows - 1);
    const float rounded    = scaled + 0.5F;
    const auto  fromBottom = static_cast<Px>(rounded);

    // Value up: the largest sample is at the smallest y. Clamped again on the integer, because the
    // cast above truncates a value that is at most `rows - 1 + 0.5` and could reach `rows - 1`
    // exactly - which is in range - but would not be if the arithmetic above ever changed.
    const Px clamped = fromBottom > rows - 1 ? rows - 1 : fromBottom;
    return (rows - 1) - clamped;
}

/// The x column sample `index` of `count` occupies inside a band `columns` wide.
///
/// Integer division in 64-bit, so the endpoints are exact - sample 0 is column 0 and sample
/// `count - 1` is column `columns - 1` - and no toolchain can round the interior differently.
[[nodiscard]] Px columnFor(std::size_t index, std::size_t count, Px columns) noexcept {
    if (count < 2) {
        return 0;
    }
    const auto scaled = static_cast<std::int64_t>(index) * static_cast<std::int64_t>(columns - 1);
    return static_cast<Px>(scaled / static_cast<std::int64_t>(count - 1));
}

/**
 * @brief The quad a segment from `from` to `to` occupies at `halfWidth` either side of it.
 *
 * Nothing but the offset normal, in ring order: both corners on one side, then both on the other,
 * so `addSolidQuad()`'s fixed 0-1-2 / 0-2-3 split covers the whole rectangle rather than folding it.
 *
 * `std::nullopt` for a zero-length segment, which is not an error: two consecutive samples land on
 * the same pixel whenever a ring holds more samples than the band has columns, and the caps drawn at
 * both endpoints already cover exactly the pixels the empty segment would have.
 */
[[nodiscard]] std::optional<std::array<mdux::draw::Point2F, 4>> segmentQuad(SamplePoint from, SamplePoint to, float halfWidth) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;

    const float lengthSquared = (dx * dx) + (dy * dy);
    if (!(lengthSquared > 0.0F)) {
        return std::nullopt;
    }
    // IEEE 754 requires sqrt to be correctly rounded, so this is the one transcendental-looking
    // call a byte-compared frame may depend on. See Trace.cppm for why `std::exp` was not.
    const float length  = std::sqrt(lengthSquared);
    const float scale   = halfWidth / length;
    const float normalX = -dy * scale;
    const float normalY = dx * scale;

    return std::array<mdux::draw::Point2F, 4>{
        mdux::draw::Point2F{.x = from.x + normalX, .y = from.y + normalY},
        mdux::draw::Point2F{  .x = to.x + normalX,   .y = to.y + normalY},
        mdux::draw::Point2F{  .x = to.x - normalX,   .y = to.y - normalY},
        mdux::draw::Point2F{.x = from.x - normalX, .y = from.y - normalY}
    };
}

}  // namespace

std::string_view describe(TraceError error) noexcept {
    switch (error) {
        case TraceError::MalformedRing:
            return "a ring's oldest index or live count does not describe a position in its storage";
        case TraceError::TooManySamples:
            return "a ring holds more samples than this runtime will expand in one trace";
        case TraceError::NonFiniteSample:
            return "a live sample is not a finite number";
        case TraceError::MalformedStyle:
            return "the sample range is empty or not finite, or the stroke width is out of range";
        case TraceError::BandTooSmall:
            return "the node's rectangle cannot hold a band of this stroke width";
        case TraceError::ListRejected:
            return "the draw list refused a primitive - budget, or a degenerate quad";
    }
    // Named rather than defaulted so that adding an enumerator without a case here is a warning at
    // this switch instead of a blank string later.
    return "unknown trace error";
}

mdux::core::ResultVoid<TraceError>
recordTrace(mdux::draw::DrawList& list, const mdux::core::Rect& band, const SampleRing& ring, const TraceStyle& style, mdux::core::ColorRgba8 color) noexcept {
    if (style.strokeWidth < 1 || style.strokeWidth > maxStrokeWidth) {
        return mdux::core::err(TraceError::MalformedStyle);
    }
    if (!std::isfinite(style.minimum) || !std::isfinite(style.maximum) || !(style.maximum > style.minimum)) {
        return mdux::core::err(TraceError::MalformedStyle);
    }

    if (ring.count > ring.storage.size() || (ring.count > 0 && ring.oldest >= ring.storage.size())) {
        return mdux::core::err(TraceError::MalformedRing);
    }
    if (ring.count > maxSamplesPerTrace) {
        // Refused, never truncated. Showing the newest 256 of 300 samples would be a waveform the
        // caller did not ask for, drawn in the tint that says it is the one it did.
        return mdux::core::err(TraceError::TooManySamples);
    }

    // The path is inset so that the stroke - half its width either side of a pixel *centre* - stays
    // inside the band. The condition is `inset + 0.5 >= strokeWidth / 2`, and integer division
    // satisfies it exactly at each admitted width: 0 for a 1px stroke, 1 for 2px and 1 for 3px. The
    // half-pixel that makes those numbers work is `SamplePoint`'s, and is why this is not simply
    // `strokeWidth / 2` rounded up.
    const Px inset   = style.strokeWidth / 2;
    const Px columns = band.width - (2 * inset);
    const Px rows    = band.height - (2 * inset);
    if (columns < 1 || rows < 1) {
        return mdux::core::err(TraceError::BandTooSmall);
    }

    if (ring.count < 2) {
        // One sample is a point, not a trace, and no sample is an empty band. Neither is an error -
        // a ring fills up over the first frames of a device's life - and neither records anything,
        // so there is nothing to roll back either.
        return {};
    }

    const mdux::draw::DrawList::Marker start  = list.mark();
    const auto                         refuse = [&list, &start](TraceError error) {
        // Cannot fail for a marker taken from this list moments ago; discarded rather than checked
        // because there is no second recovery to attempt.
        static_cast<void>(list.rollback(start));
        return mdux::core::err(error);
    };

    const float halfWidth = static_cast<float>(style.strokeWidth) / 2.0F;
    const Px    originX   = band.x + inset;
    const Px    originY   = band.y + inset;

    // One cap per sample and one segment behind it, in sample order. Fixed rather than incidental:
    // two frames built from the same samples must produce byte-identical buffers.
    SamplePoint previous{};
    for (std::size_t index = 0; index < ring.count; ++index) {
        const std::optional<float> sample = ring.at(index);
        if (!sample.has_value()) {
            // Unreachable given the bounds checked above, and checked anyway: `at()` is the only
            // reader, and a bound it enforces that this ignored would be a silent zero sample.
            return refuse(TraceError::MalformedRing);
        }
        if (!std::isfinite(*sample)) {
            // Before any arithmetic touches it. A NaN sample normalises to a NaN position and
            // reaches the rasteriser as undefined behaviour rather than as an invisible primitive.
            return refuse(TraceError::NonFiniteSample);
        }

        const SamplePoint point{.x = static_cast<float>(originX + columnFor(index, ring.count, columns)) + 0.5F,
                                .y = static_cast<float>(originY + rowFor(*sample, style, rows)) + 0.5F};

        // The cap: the bounding square of the disc a round join would draw. See Trace.cppm for why
        // that substitution is honest at these widths and refused past them.
        if (const auto recorded = list.addSolidQuad(capQuad(point, halfWidth), color); !recorded.has_value()) {
            return refuse(TraceError::ListRejected);
        }

        if (index > 0) {
            if (const std::optional quad = segmentQuad(previous, point, halfWidth); quad.has_value()) {
                if (const auto recorded = list.addSolidQuad(*quad, color); !recorded.has_value()) {
                    return refuse(TraceError::ListRejected);
                }
            }
        }
        previous = point;
    }

    return {};
}

}  // namespace mdux::medui
