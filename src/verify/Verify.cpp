/**
 * @file Verify.cpp
 * @brief Implementation of the four rendered-truth checks and the two expectation views.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * See Verify.cppm for what each check claims, why an expectation carries two resolved colours, and
 * which failures each check is structurally unable to see. This file adds only the arithmetic.
 */
module;

module mdux.verify;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.draw;
import mdux.text.schema;

namespace mdux::verify {

using mdux::core::ColorRgba8;
using mdux::core::err;
using mdux::core::Px;
using mdux::core::Result;
using mdux::medui::NodeRect;

namespace {

/// A measured rectangle, with exclusive right and bottom edges.
///
/// The same shape `mdux.medui.screen`'s `InkBox` and `ScreenPixelTests`' both use, and for the same
/// reason: a union of rectangles is written far more simply against edges than against an origin and
/// an extent, and converting once at the end keeps the conversion in one place.
struct Box {
    bool inked{false};
    Px   left{0};
    Px   top{0};
    Px   right{0};
    Px   bottom{0};

    void add(Px x, Px y, Px width, Px height) noexcept {
        const Px boxRight  = x + width;
        const Px boxBottom = y + height;
        if (!inked) {
            inked  = true;
            left   = x;
            top    = y;
            right  = boxRight;
            bottom = boxBottom;
            return;
        }
        left   = x < left ? x : left;
        top    = y < top ? y : top;
        right  = boxRight > right ? boxRight : right;
        bottom = boxBottom > bottom ? boxBottom : bottom;
    }
};

[[nodiscard]] NodeRect asRect(const Box& box) noexcept {
    return NodeRect{.x = box.left, .y = box.top, .width = box.right - box.left, .height = box.bottom - box.top};
}

/// A one-pixel rectangle, so a finding about a single pixel can still name a place.
[[nodiscard]] NodeRect atPixel(Px x, Px y) noexcept {
    return NodeRect{.x = x, .y = y, .width = 1, .height = 1};
}

/// Whether `inner` lies wholly inside `outer`. 64-bit, for `containedBy()`'s reason: two `int32_t`
/// at their extremes overflow on addition, and an overflowed comparison admits what it should refuse.
[[nodiscard]] bool inside(NodeRect inner, NodeRect outer) noexcept {
    const auto innerRight  = static_cast<std::int64_t>(inner.x) + inner.width;
    const auto innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    const auto outerRight  = static_cast<std::int64_t>(outer.x) + outer.width;
    const auto outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    return inner.x >= outer.x && inner.y >= outer.y && innerRight <= outerRight && innerBottom <= outerBottom;
}

[[nodiscard]] bool holds(NodeRect rect, Px x, Px y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/**
 * @brief Whether `pixel` could be the tint composited over the ground at *one* coverage.
 *
 * Channel-wise membership of the interval between ground and tint is necessary and not sufficient,
 * which is the whole reason this is a function rather than four comparisons. Alpha blending applies
 * one coverage to every channel, so each channel constrains that coverage to an interval, and the
 * pixel is possible exactly when those intervals intersect. With a black ground and
 * `Theme.Colors.ScoreDigits` at `(33, 184, 107)`, the pixel `(33, 0, 107)` lies inside every
 * channel's range while demanding full coverage of red and blue and none of green - a per-channel
 * test accepts it and no blend can produce it.
 *
 * Integer cross-products, no division and no float: the coverage each channel implies is the
 * rational `(pixel - ground) / (tint - ground)`, so the intervals are compared by multiplying out.
 * `allowance` is one UNORM step, because the device blends in floating point and quantises back to
 * eight bits; a channel whose tint and ground are equal admits any coverage and only requires the
 * pixel to match within the same step.
 */
[[nodiscard]] bool couldBeBlend(ColorRgba8 pixel, ColorRgba8 ground, ColorRgba8 tint) noexcept {
    constexpr std::int64_t allowance = 1;

    // The feasible coverage, as a closed interval of rationals, narrowed channel by channel from
    // the whole of [0, 1].
    std::int64_t lowNum  = 0;
    std::int64_t lowDen  = 1;
    std::int64_t highNum = 1;
    std::int64_t highDen = 1;

    const std::array<std::int64_t, 4> pixels{pixel.r, pixel.g, pixel.b, pixel.a};
    const std::array<std::int64_t, 4> grounds{ground.r, ground.g, ground.b, ground.a};
    const std::array<std::int64_t, 4> tints{tint.r, tint.g, tint.b, tint.a};

    for (std::size_t channel = 0; channel < pixels.size(); ++channel) {
        const std::int64_t span     = tints[channel] - grounds[channel];
        const std::int64_t distance = pixels[channel] - grounds[channel];
        if (span == 0) {
            // Every coverage produces the ground on this channel, so the channel says nothing about
            // the coverage - and everything about the pixel.
            if (distance > allowance || distance < -allowance) {
                return false;
            }
            continue;
        }

        // coverage * span is distance, to within one step: coverage lies between the two bounds
        // below, in whichever order the sign of `span` puts them.
        std::int64_t candidateLowNum  = distance - allowance;
        std::int64_t candidateHighNum = distance + allowance;
        std::int64_t denominator      = span;
        if (denominator < 0) {
            denominator                = -denominator;
            const std::int64_t swapped = -candidateLowNum;
            candidateLowNum            = -candidateHighNum;
            candidateHighNum           = swapped;
        }
        if (candidateLowNum * lowDen > lowNum * denominator) {
            lowNum = candidateLowNum;
            lowDen = denominator;
        }
        if (candidateHighNum * highDen < highNum * denominator) {
            highNum = candidateHighNum;
            highDen = denominator;
        }
        if (lowNum * highDen > highNum * lowDen) {
            return false;
        }
    }
    return true;
}

/// Whether every channel of `pixel` is within `steps` UNORM steps of `expected`.
///
/// One step per quantisation the device performed. A single glyph is one blend and therefore one
/// step; a pixel two overlapping quads deep was quantised to eight bits twice, so it may differ in
/// the last bit twice. Bounded by the number of glyphs covering the pixel, never by a fixed slack.
[[nodiscard]] bool withinSteps(ColorRgba8 pixel, ColorRgba8 expected, std::size_t steps) noexcept {
    const auto close = [steps](std::uint8_t left, std::uint8_t right) noexcept {
        const auto difference = static_cast<std::int64_t>(left) - static_cast<std::int64_t>(right);
        const auto allowed    = static_cast<std::int64_t>(steps == 0 ? 1 : steps);
        return difference <= allowed && difference >= -allowed;
    };
    return close(pixel.r, expected.r) && close(pixel.g, expected.g) && close(pixel.b, expected.b) && close(pixel.a, expected.a);
}

/**
 * @brief Two coverages of one tint over one ground, as the draw path composites them.
 *
 * `1 - (1 - a1)(1 - a2)`, in integers. The draw path records one quad per glyph and blends them in
 * turn, so expanding `t*a2 + (t*a1 + g*(1-a1))*(1-a2)` leaves the tint weighted by exactly this
 * expression and the ground by its complement. Commutative, so the order glyphs are visited in does
 * not change the answer.
 */
[[nodiscard]] std::uint8_t composeCoverage(std::uint8_t first, std::uint8_t second) noexcept {
    const auto remaining = (255 - static_cast<int>(first)) * (255 - static_cast<int>(second));
    return static_cast<std::uint8_t>(255 - (remaining + 127) / 255);
}

/// The bounding box of every pixel of `region` that is not `ground`.
///
/// "Painted" is defined against the ground rather than against the tint deliberately: an
/// anti-aliased edge is a blend, so it is neither the ground nor the tint, and a scan that looked
/// only for the tint would measure a box one pixel small on every side.
[[nodiscard]] Box paintedBox(const FramebufferView& frame, NodeRect region, ColorRgba8 ground) noexcept {
    Box box;
    for (Px y = region.y; y < region.y + region.height; ++y) {
        for (Px x = region.x; x < region.x + region.width; ++x) {
            const std::optional<ColorRgba8> pixel = frame.pixelAt(x, y);
            if (!pixel.has_value() || *pixel == ground) {
                continue;
            }
            box.add(x, y, 1, 1);
        }
    }
    return box;
}

/// The outcome every check starts from, so the reporting fields are filled in one place.
[[nodiscard]] CheckOutcome opened(std::string_view nodeId, RenderScope scope, std::string_view check, NodeRect expected) noexcept {
    return CheckOutcome{.finding = Finding::Held, .nodeId = nodeId, .scope = scope.name(), .check = check, .expected = expected};
}

[[nodiscard]] CheckOutcome failed(CheckOutcome outcome, Finding finding) noexcept {
    outcome.finding = finding;
    return outcome;
}

}  // namespace

std::string_view describe(VerifyError error) noexcept {
    switch (error) {
        case VerifyError::EmptyFramebuffer:
            return "the framebuffer has zero width or height";
        case VerifyError::UnsupportedFormat:
            return "the framebuffer's pixel format is not one this module decodes";
        case VerifyError::RowStrideTooSmall:
            return "the row stride cannot hold one row of pixels of the declared format";
        case VerifyError::FramebufferTooSmall:
            return "the byte span is shorter than the declared extent requires";
        case VerifyError::DanglingGoldenId:
            return "the golden names a node the compiled screen does not contain";
        case VerifyError::GoldenBoundsDisagree:
            return "the golden's rectangle is not the rectangle the named node occupies";
        case VerifyError::GoldenTextKeyDisagrees:
            return "the golden's text key is not the one the named node carries";
        case VerifyError::GoldenColorTokenDisagrees:
            return "the golden's colour token is not the one the named node carries";
        case VerifyError::NoChecksDeclared:
            return "the golden entry declares no cvChecks, so it can discharge nothing";
        case VerifyError::ChecksNotCanonical:
            return "the golden entry's cvChecks are not sorted and deduplicated";
        case VerifyError::ColorHashWithoutTint:
            return "the golden entry asks for ColorHash and names no colour token";
        case VerifyError::UnresolvedColorToken:
            return "the governed colour table does not define this token";
        case VerifyError::NodeCarriesNoTextKey:
            return "a text obligation was raised over a node whose spec carries no textKey";
        case VerifyError::ScopeIsLocaleFree:
            return "a text obligation must name the approved locale it is discharged in";
        case VerifyError::MalformedRun:
            return "the run's byte length is not a whole number of records";
        case VerifyError::EmptyRun:
            return "the approved locale's run paints no ink for this node";
        case VerifyError::RunTooLong:
            return "the run holds more records than the runtime's per-node cap admits";
        case VerifyError::GlyphIndexOutOfRange:
            return "a record names a glyph index the font package does not contain";
        case VerifyError::TextBindingUnbound:
            return "the text binding carries no packages, so it authorises nothing";
        case VerifyError::BindingNotApproved:
            return "this screen's manifest does not approve the bound text package";
        case VerifyError::NodeNotInScreen:
            return "the node is not one this screen contains";
        case VerifyError::ScopeIsNotTheBoundLocale:
            return "the render scope names a locale other than the bound package's";
        case VerifyError::UnknownTextKey:
            return "the bound text package carries no run for this node's text key";
        case VerifyError::AtlasSizeMismatch:
            return "the coverage sheet is not the size the font package declares";
        case VerifyError::GlyphOutsideAtlas:
            return "a glyph's slot leaves the coverage sheet";
    }
    return "unknown verification error";
}

std::string_view describe(Finding finding) noexcept {
    switch (finding) {
        case Finding::Held:
            return "the check held";
        case Finding::RegionOutsideFrame:
            return "the rectangle to inspect is not inside the rendered frame";
        case Finding::NoTintToCompare:
            return "the expectation resolved no tint, so no colour claim can be made";
        case Finding::NothingPainted:
            return "the rectangle shows its ground everywhere: nothing was drawn there";
        case Finding::BoundsDiffer:
            return "the content in the rectangle does not occupy the rectangle it was pinned to";
        case Finding::TintAbsent:
            return "content is present and no pixel of it carries the resolved tint";
        case Finding::ForeignColour:
            return "a painted pixel is not the resolved tint blended over the ground";
        case Finding::InkLeftItsNode:
            return "the run's ink, placed as the runtime places it, is not inside its node";
        case Finding::InkExtentDiffers:
            return "the ink in the frame is not where the committed run says it would be";
        case Finding::GlyphMissing:
            return "a non-blank record of the approved run painted nothing";
        case Finding::CoverageDiffers:
            return "a glyph's pixels are not what its baked coverage would paint in this tint";
        case Finding::InkOutsideTheRun:
            return "the node shows ink where this locale's run paints none";
    }
    return "unknown verification finding";
}

ColorRgba8 blend(ColorRgba8 ground, ColorRgba8 tint, std::uint8_t coverage) noexcept {
    // `a = (tint.a / 255) * (coverage / 255)`, kept as the numerator over 255*255 so the whole
    // computation stays in integers. Rounding to nearest, with the sign of the span carried through
    // because a tint darker than its ground moves the channel down.
    const auto alpha = static_cast<std::int64_t>(tint.a) * static_cast<std::int64_t>(coverage);
    const auto mix   = [alpha](std::uint8_t from, std::uint8_t to) noexcept {
        constexpr std::int64_t full   = 255 * 255;
        const std::int64_t     span   = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
        const std::int64_t     scaled = span * alpha;
        const std::int64_t     step   = scaled >= 0 ? (scaled + full / 2) / full : -((-scaled + full / 2) / full);
        return static_cast<std::uint8_t>(static_cast<std::int64_t>(from) + step);
    };
    // The alpha channel is the destination's: the target is opaque and the blend writes coverage
    // into the source's alpha, not into the frame's.
    return ColorRgba8{.r = mix(ground.r, tint.r), .g = mix(ground.g, tint.g), .b = mix(ground.b, tint.b), .a = ground.a};
}


Result<FramebufferView, VerifyError>
FramebufferView::create(std::span<const std::byte> bytes, Px width, Px height, std::size_t rowStride, PixelFormat format) noexcept {
    if (width <= 0 || height <= 0) {
        return err(VerifyError::EmptyFramebuffer);
    }
    const std::size_t stride = bytesPerPixel(format);
    if (stride == 0) {
        // Only reachable from a cast outside the enumeration. Refused rather than divided by, for
        // the reason the enumeration exists at all: a misread buffer is a plausible wrong answer.
        return err(VerifyError::UnsupportedFormat);
    }

    // `width` is an `int32_t` and `stride` is four, so this product cannot overflow 64 bits.
    const auto rowBytes = static_cast<std::uint64_t>(width) * stride;
    if (static_cast<std::uint64_t>(rowStride) < rowBytes) {
        return err(VerifyError::RowStrideTooSmall);
    }

    // The last row need not be padded, so the requirement is every full row but the last, plus one
    // row of pixels. The multiplication is *checked* rather than merely widened: `rowStride` is a
    // `size_t` a caller chooses, and unsigned 64-bit arithmetic wraps as happily as any other. With
    // `height = 2` and `rowStride = SIZE_MAX` the sum wraps to three, which would admit a four-byte
    // span and then read at offset `SIZE_MAX` - so the bound is established by division and
    // subtraction, which cannot wrap, before anything is multiplied.
    const auto rows = static_cast<std::uint64_t>(height) - 1;
    const auto room = static_cast<std::uint64_t>(bytes.size());
    if (room < rowBytes) {
        return err(VerifyError::FramebufferTooSmall);
    }
    if (rows != 0 && static_cast<std::uint64_t>(rowStride) > (room - rowBytes) / rows) {
        return err(VerifyError::FramebufferTooSmall);
    }
    return FramebufferView{bytes, width, height, rowStride, format};
}

Result<FramebufferView, VerifyError> FramebufferView::createPacked(std::span<const ColorRgba8> pixels, Px width, Px height) noexcept {
    if (width <= 0 || height <= 0) {
        return err(VerifyError::EmptyFramebuffer);
    }
    const std::size_t rowStride = static_cast<std::size_t>(width) * bytesPerPixel(PixelFormat::Rgba8Unorm);
    return create(std::as_bytes(pixels), width, height, rowStride, PixelFormat::Rgba8Unorm);
}

std::optional<ColorRgba8> FramebufferView::pixelAt(Px x, Px y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return std::nullopt;
    }
    const std::size_t pixelBytes = bytesPerPixel(format_);
    if (bytes_.size() < pixelBytes) {
        return std::nullopt;
    }
    // Subtraction rather than `offset + pixelBytes > size`, which wraps: the addition form's own
    // guard can overflow past the end of the caller's span, which is exactly how a `SIZE_MAX` stride
    // used to get through. `create()` has already bounded the product below, and this is the second
    // gate rather than the only one.
    const std::size_t offset = static_cast<std::size_t>(y) * rowStride_ + static_cast<std::size_t>(x) * pixelBytes;
    if (offset > bytes_.size() - pixelBytes) {
        return std::nullopt;
    }
    const auto channel = [this, offset](std::size_t index) noexcept {
        return std::to_integer<std::uint8_t>(bytes_[offset + index]);
    };
    return ColorRgba8{.r = channel(0), .g = channel(1), .b = channel(2), .a = channel(3)};
}

Result<GoldenExpectation, VerifyError>
GoldenExpectation::create(const GoldenEntry& entry, const mdux::medui::ScreenPackage& screen, RenderScope scope, ColorRgba8 ground) noexcept {
    const mdux::medui::CompiledNode* node = screen.find(entry.nodeId);
    if (node == nullptr) {
        // The verifier's first lookup, and the failure ADR-014's ownership table assigns to it. A
        // golden addressing a node that does not exist is a sidecar that drifted from its package.
        return err(VerifyError::DanglingGoldenId);
    }
    if (entry.bounds != node->bounds) {
        return err(VerifyError::GoldenBoundsDisagree);
    }
    if (entry.textKey != textKeyOf(*node)) {
        return err(VerifyError::GoldenTextKeyDisagrees);
    }
    if (entry.colorToken != colorTokenOf(*node)) {
        return err(VerifyError::GoldenColorTokenDisagrees);
    }
    if (entry.cvChecks.empty()) {
        return err(VerifyError::NoChecksDeclared);
    }
    for (std::size_t index = 1; index < entry.cvChecks.size(); ++index) {
        // Strictly ascending, which is sorted and deduplicated in one comparison. The sidecar is
        // byte-compared across four toolchains, so a set whose order depended on which rule selected
        // the node first could not survive that - an entry that is not in canonical form did not
        // come from the compiler that writes them.
        if (static_cast<std::uint8_t>(entry.cvChecks[index - 1]) >= static_cast<std::uint8_t>(entry.cvChecks[index])) {
            return err(VerifyError::ChecksNotCanonical);
        }
    }

    const bool wantsTint = std::ranges::find(entry.cvChecks, CvCheck::ColorHash) != entry.cvChecks.end();
    if (wantsTint && entry.colorToken.empty()) {
        // The two halves of one claim, as `collectGoldens()` treats them: a verifier asked to
        // compare a tint has to be told which tint, and an entry that asked without saying is one
        // this module could only skip.
        return err(VerifyError::ColorHashWithoutTint);
    }

    bool       hasTint = false;
    ColorRgba8 tint{};
    if (!entry.colorToken.empty()) {
        const auto resolved = mdux::medui::resolveColorToken(entry.colorToken);
        if (!resolved.has_value()) {
            return err(VerifyError::UnresolvedColorToken);
        }
        hasTint = true;
        tint    = mdux::medui::quantise(*resolved);
    }
    return GoldenExpectation{node, scope, entry.cvChecks, ground, hasTint, tint};
}

Result<TextExpectation, VerifyError> TextExpectation::create(const mdux::medui::ScreenPackage& screen,
                                                             const mdux::medui::CompiledNode&  node,
                                                             const mdux::medui::TextBinding&   binding,
                                                             std::span<const std::byte>        atlas,
                                                             RenderScope                       scope,
                                                             ColorRgba8                        ground) noexcept {
    // Everything below is provenance, and it is all this function adds over `createSynthetic()`.
    // `TextBinding::create()` has already proved that the font, the text package, its canonical
    // bytes and its sidecar describe each other; what it cannot know is which screen and which node
    // the caller means, so those are checked here.
    if (!binding.bound()) {
        return err(VerifyError::TextBindingUnbound);
    }
    if (!binding.approvedBy(screen)) {
        // A binding approved by screen A being used to verify screen B. The binding retains the
        // locale, package id and canonical digest it was proved against, so this is a comparison
        // against the manifest rather than a second hash.
        return err(VerifyError::BindingNotApproved);
    }
    if (screen.find(node.id) != &node) {
        // Not "a node with this id exists" but "this is that node". A caller holding a node from a
        // different screen would otherwise have its bounds verified against a manifest that never
        // mentioned it.
        return err(VerifyError::NodeNotInScreen);
    }
    if (scope.tag() != binding.text()->locale) {
        // The scope names the locale an outcome will be reported under. A scope saying `de-DE` over
        // a binding carrying `en-US` would produce a passing German outcome from an English frame,
        // which is precisely the confusion ADR-014 decision 3 exists to prevent.
        return err(VerifyError::ScopeIsNotTheBoundLocale);
    }

    const std::string_view key = textKeyOf(node);
    if (key.empty()) {
        return err(VerifyError::NodeCarriesNoTextKey);
    }
    const mdux::text::TextRun* run = binding.text()->find(key);
    if (run == nullptr) {
        return err(VerifyError::UnknownTextKey);
    }
    // Bounds before the span exists. The binding proved every range lies inside the sidecar, so this
    // is defence in depth rather than the first check - and subtraction rather than an addition that
    // could wrap, which is the form `TextBinding::create()` uses for the same reason.
    const std::span<const std::byte> sidecar = binding.runs();
    if (run->byteOffset > sidecar.size() || run->byteLength > sidecar.size() - run->byteOffset) {
        return err(VerifyError::MalformedRun);
    }
    const std::span<const std::byte> records = sidecar.subspan(static_cast<std::size_t>(run->byteOffset), static_cast<std::size_t>(run->byteLength));

    return build(node, scope, records, *binding.font(), atlas, ground);
}

Result<TextExpectation, VerifyError> TextExpectation::createSynthetic(const mdux::medui::CompiledNode& node,
                                                                      RenderScope                      scope,
                                                                      std::span<const std::byte>       records,
                                                                      const mdux::font::FontPackage&   font,
                                                                      std::span<const std::byte>       atlas,
                                                                      ColorRgba8                       ground) noexcept {
    if (textKeyOf(node).empty()) {
        return err(VerifyError::NodeCarriesNoTextKey);
    }
    return build(node, scope, records, font, atlas, ground);
}

Result<TextExpectation, VerifyError> TextExpectation::build(const mdux::medui::CompiledNode& node,
                                                            RenderScope                      scope,
                                                            std::span<const std::byte>       records,
                                                            const mdux::font::FontPackage&   font,
                                                            std::span<const std::byte>       atlas,
                                                            ColorRgba8                       ground) noexcept {
    if (scope.isLocaleFree()) {
        // The locale-free scope exists so a textless screen keeps its geometric obligations, not so
        // a text obligation can lose the only thing that distinguishes one of its two from another.
        return err(VerifyError::ScopeIsLocaleFree);
    }
    if (records.size() % mdux::text::draw::recordSize != 0) {
        return err(VerifyError::MalformedRun);
    }
    const std::size_t count = records.size() / mdux::text::draw::recordSize;
    if (count == 0) {
        return err(VerifyError::EmptyRun);
    }
    if (count > mdux::medui::maxGlyphsPerRun) {
        // The runtime's cap rather than a second one. A run the device would refuse to draw is not a
        // run a verifier should describe as checkable.
        return err(VerifyError::RunTooLong);
    }

    // The sheet, before any glyph is read out of it. `byteLength` is `width * height` for a
    // validated font package, and both are restated here because this module is handed the bytes
    // rather than the file: a caller that passed the wrong sidecar would otherwise sample coverage
    // from another font's shapes and find the run present in the wrong letters.
    const auto sheetWidth  = static_cast<std::uint64_t>(font.atlas.width);
    const auto sheetHeight = static_cast<std::uint64_t>(font.atlas.height);
    if (sheetWidth == 0 || sheetHeight == 0 || font.atlas.byteLength != sheetWidth * sheetHeight) {
        return err(VerifyError::AtlasSizeMismatch);
    }
    if (static_cast<std::uint64_t>(atlas.size()) != font.atlas.byteLength) {
        return err(VerifyError::AtlasSizeMismatch);
    }

    const auto resolved = mdux::medui::resolveColorToken(colorTokenOf(node));
    if (!resolved.has_value()) {
        return err(VerifyError::UnresolvedColorToken);
    }

    // The ink box, measured exactly as `mdux.medui.screen` measures it before placing a run: the
    // union of the non-blank glyph rectangles, in the run's own coordinate frame. Computed here so
    // that a check compares the frame against where the runtime would have drawn, rather than
    // against a second opinion about where text belongs.
    Box ink;
    for (std::size_t index = 0; index < count; ++index) {
        const auto placement = mdux::text::draw::decodeRecord(records.subspan(index * mdux::text::draw::recordSize, mdux::text::draw::recordSize));
        if (!placement.has_value()) {
            return err(VerifyError::MalformedRun);
        }
        if (placement->packageIndex >= font.glyphs.size()) {
            return err(VerifyError::GlyphIndexOutOfRange);
        }
        const mdux::font::GlyphRecord& glyph = font.glyphs[placement->packageIndex];
        if (glyph.isBlank()) {
            continue;
        }
        if (static_cast<std::uint64_t>(glyph.x) + glyph.width > sheetWidth || static_cast<std::uint64_t>(glyph.y) + glyph.height > sheetHeight) {
            return err(VerifyError::GlyphOutsideAtlas);
        }
        ink.add(placement->x + glyph.bitmapOriginX, placement->y - glyph.bitmapOriginY, static_cast<Px>(glyph.width), static_cast<Px>(glyph.height));
    }
    if (!ink.inked) {
        // Every glyph blank. Legitimate for the runtime, which draws nothing; not something a
        // rendered-truth obligation can discharge, because a pass would be indistinguishable from a
        // screen that lost its text. See the interface comment for why this refuses rather than
        // passing vacuously.
        return err(VerifyError::EmptyRun);
    }

    // The placement rule, restated as the arithmetic it is: the ink box's corner goes on the node's
    // corner, so the origin added to every record is the node's corner less where the ink starts.
    const Px       originX = node.bounds.x - ink.left;
    const Px       originY = node.bounds.y - ink.top;
    const NodeRect placed{.x = node.bounds.x, .y = node.bounds.y, .width = ink.right - ink.left, .height = ink.bottom - ink.top};

    return TextExpectation{&node, scope, records, &font, atlas, mdux::medui::quantise(*resolved), ground, placed, originX, originY};
}

std::optional<PlacedGlyph> TextExpectation::glyph(std::size_t index) const noexcept {
    if (index >= glyphCount()) {
        return std::nullopt;
    }
    const auto placement = mdux::text::draw::decodeRecord(records_.subspan(index * mdux::text::draw::recordSize, mdux::text::draw::recordSize));
    if (!placement.has_value() || placement->packageIndex >= font_->glyphs.size()) {
        // Both refused when the expectation was built, so neither is reachable through a live one.
        // Answered as "no glyph" rather than assumed away, because the alternative is indexing a
        // vector with a number this function did not check.
        return std::nullopt;
    }
    const mdux::font::GlyphRecord& glyph = font_->glyphs[placement->packageIndex];
    if (glyph.isBlank()) {
        return std::nullopt;
    }
    return PlacedGlyph{
        .rect   = NodeRect{.x      = originX_ + placement->x + glyph.bitmapOriginX,
                           .y      = originY_ + placement->y - glyph.bitmapOriginY,
                           .width  = static_cast<Px>(glyph.width),
                           .height = static_cast<Px>(glyph.height)},
        .atlasX = glyph.x,
        .atlasY = glyph.y
    };
}

std::uint8_t TextExpectation::coverage(const PlacedGlyph& placed, Px dx, Px dy) const noexcept {
    if (dx < 0 || dy < 0 || dx >= placed.rect.width || dy >= placed.rect.height) {
        return 0;
    }
    const auto texelX = static_cast<std::uint64_t>(placed.atlasX) + static_cast<std::uint64_t>(dx);
    const auto texelY = static_cast<std::uint64_t>(placed.atlasY) + static_cast<std::uint64_t>(dy);
    const auto offset = texelY * static_cast<std::uint64_t>(font_->atlas.width) + texelX;
    if (offset >= static_cast<std::uint64_t>(atlas_.size())) {
        return 0;
    }
    return std::to_integer<std::uint8_t>(atlas_[static_cast<std::size_t>(offset)]);
}

CheckOutcome goldenBounds(const FramebufferView& frame, const GoldenExpectation& expectation) noexcept {
    CheckOutcome outcome = opened(expectation.nodeId(), expectation.scope(), spell(CvCheck::Bounds), expectation.bounds());
    if (!frame.contains(expectation.bounds())) {
        return failed(outcome, Finding::RegionOutsideFrame);
    }

    const Box painted = paintedBox(frame, expectation.bounds(), expectation.ground());
    if (!painted.inked) {
        // What a node that vanished, or was never drawn, looks like from the frame. It is also
        // exactly the state `tests/render/ScreenPixelTests.cpp` asserts as a tripwire today, since
        // both of the committed screen's golden nodes are still deferred.
        return failed(outcome, Finding::NothingPainted);
    }
    outcome.found      = asRect(painted);
    outcome.foundValid = true;
    if (outcome.found != expectation.bounds()) {
        // An equality rather than a containment, which is ADR-014's own worked example: a rectangle
        // moved by one pixel still overlaps its declared box, so only "the content *is* the box"
        // fails on it. See Verify.cppm for the partial fill this reading deliberately excludes.
        return failed(outcome, Finding::BoundsDiffer);
    }
    return outcome;
}

CheckOutcome colorHash(const FramebufferView& frame, const GoldenExpectation& expectation) noexcept {
    CheckOutcome outcome  = opened(expectation.nodeId(), expectation.scope(), spell(CvCheck::ColorHash), expectation.bounds());
    outcome.expectedColor = expectation.tint();
    if (!expectation.hasTint()) {
        return failed(outcome, Finding::NoTintToCompare);
    }
    if (!frame.contains(expectation.bounds())) {
        return failed(outcome, Finding::RegionOutsideFrame);
    }

    const NodeRect   region = expectation.bounds();
    const ColorRgba8 ground = expectation.ground();
    const ColorRgba8 tint   = expectation.tint();

    Box  painted;
    bool carriesTint = false;
    for (Px y = region.y; y < region.y + region.height; ++y) {
        for (Px x = region.x; x < region.x + region.width; ++x) {
            const std::optional<ColorRgba8> pixel = frame.pixelAt(x, y);
            if (!pixel.has_value() || *pixel == ground) {
                continue;
            }
            if (!couldBeBlend(*pixel, ground, tint)) {
                outcome.found           = atPixel(x, y);
                outcome.foundValid      = true;
                outcome.foundColor      = *pixel;
                outcome.foundColorValid = true;
                return failed(outcome, Finding::ForeignColour);
            }
            painted.add(x, y, 1, 1);
            carriesTint = carriesTint || *pixel == tint;
        }
    }

    if (!painted.inked) {
        return failed(outcome, Finding::NothingPainted);
    }
    outcome.found      = asRect(painted);
    outcome.foundValid = true;
    if (!carriesTint) {
        // Content that never reaches full coverage cannot be said to carry its tint. Reported rather
        // than rounded away: see Verify.cppm for why this direction is the fail-closed one.
        return failed(outcome, Finding::TintAbsent);
    }
    return outcome;
}

CheckOutcome inkContainment(const FramebufferView& frame, const TextExpectation& expectation) noexcept {
    CheckOutcome outcome = opened(expectation.nodeId(), expectation.scope(), spell(TextCheck::InkContainment), expectation.bounds());
    if (!inside(expectation.ink(), expectation.bounds())) {
        // The compile-time claim, failing before a pixel is read. #195 proved this box fits this
        // rectangle for the package it measured; a run that overflows here was bound from a package
        // nobody measured against this screen.
        outcome.found      = expectation.ink();
        outcome.foundValid = true;
        return failed(outcome, Finding::InkLeftItsNode);
    }
    if (!frame.contains(expectation.bounds())) {
        return failed(outcome, Finding::RegionOutsideFrame);
    }

    const Box painted = paintedBox(frame, expectation.bounds(), expectation.ground());
    outcome.expected  = expectation.ink();
    if (!painted.inked) {
        return failed(outcome, Finding::NothingPainted);
    }
    outcome.found      = asRect(painted);
    outcome.foundValid = true;
    if (outcome.found != expectation.ink()) {
        // The rendered half. A run that was clipped at the node's edge, displaced, or drawn from a
        // different package leaves ink whose extent is not the one the committed records predict.
        return failed(outcome, Finding::InkExtentDiffers);
    }
    return outcome;
}

CheckOutcome localizedTextPresence(const FramebufferView& frame, const TextExpectation& expectation) noexcept {
    CheckOutcome outcome  = opened(expectation.nodeId(), expectation.scope(), spell(TextCheck::LocalizedTextPresence), expectation.bounds());
    outcome.expectedColor = expectation.tint();
    if (!frame.contains(expectation.bounds())) {
        return failed(outcome, Finding::RegionOutsideFrame);
    }

    const NodeRect   region = expectation.bounds();
    const ColorRgba8 ground = expectation.ground();
    const ColorRgba8 tint   = expectation.tint();

    // Every placed glyph has to be somewhere this function can look, before any of them is read.
    for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
        const std::optional<PlacedGlyph> placed = expectation.glyph(index);
        if (placed.has_value() && !frame.contains(placed->rect)) {
            outcome.glyphIndex = index;
            outcome.expected   = placed->rect;
            outcome.found      = placed->rect;
            outcome.foundValid = true;
            return failed(outcome, Finding::RegionOutsideFrame);
        }
    }

    // What the run paints at one pixel: the coverage of every glyph that covers it, composed, and
    // how many did.
    //
    // Composed rather than taken one glyph at a time, because the draw path draws one quad per
    // glyph and blends them in turn - so where two placed rectangles overlap, the frame holds
    // `1 - (1 - a1)(1 - a2)` of the tint and not either coverage alone. Nothing in
    // `FontPackage::validate()` requires an advance to clear its own bitmap, so a font with tight
    // advances or negative kerning can produce that overlap, and comparing against a single glyph's
    // coverage there would fail a correct frame. The composition is exact because every glyph of one
    // run carries one tint over one ground: expanding the two blends leaves the tint weighted by
    // exactly that expression.
    struct Painted {
        std::uint8_t coverage{0};
        std::size_t  layers{0};
        std::size_t  first{0};
    };
    const auto paintedAt = [&expectation](Px x, Px y) noexcept {
        Painted painted;
        for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
            const std::optional<PlacedGlyph> placed = expectation.glyph(index);
            if (!placed.has_value() || !holds(placed->rect, x, y)) {
                continue;
            }
            if (painted.layers == 0) {
                painted.first = index;
            }
            ++painted.layers;
            painted.coverage = composeCoverage(painted.coverage, expectation.coverage(*placed, x - placed->rect.x, y - placed->rect.y));
        }
        return painted;
    };

    // Whether a glyph's rectangle shows anything at all. It is what separates "this glyph is not on
    // screen" from "something is, and it is not this glyph as the baker covered it" - two sentences
    // a reader acts on differently, and a distinction the first disagreeing pixel cannot make: a run
    // in the wrong tint disagrees at its very first pixel having painted plenty.
    const auto glyphShowsInk = [&frame, ground](const PlacedGlyph& placed) noexcept {
        for (Px dy = 0; dy < placed.rect.height; ++dy) {
            for (Px dx = 0; dx < placed.rect.width; ++dx) {
                const std::optional<ColorRgba8> pixel = frame.pixelAt(placed.rect.x + dx, placed.rect.y + dy);
                if (pixel.has_value() && *pixel != ground) {
                    return true;
                }
            }
        }
        return false;
    };

    const NodeRect ink = expectation.ink();
    for (Px y = region.y; y < region.y + region.height; ++y) {
        for (Px x = region.x; x < region.x + region.width; ++x) {
            const std::optional<ColorRgba8> pixel = frame.pixelAt(x, y);
            if (!pixel.has_value()) {
                outcome.found      = atPixel(x, y);
                outcome.foundValid = true;
                return failed(outcome, Finding::RegionOutsideFrame);
            }

            // Outside the run's ink box no glyph can cover this pixel, so the answer is the ground
            // and the glyph walk is skipped. That keeps the per-pixel cost proportional to the run
            // only where the run is, rather than over the whole node.
            const bool    reachable = holds(ink, x, y);
            const Painted painted   = reachable ? paintedAt(x, y) : Painted{};
            if (painted.layers == 0) {
                if (*pixel != ground) {
                    // The atlas slot is exactly the glyph's bitmap, so a correct frame paints
                    // nothing here.
                    outcome.found           = atPixel(x, y);
                    outcome.foundValid      = true;
                    outcome.foundColor      = *pixel;
                    outcome.foundColorValid = true;
                    return failed(outcome, Finding::InkOutsideTheRun);
                }
                continue;
            }

            // One UNORM step per composite the device performed: it blends in floating point and
            // quantises back to eight bits at every step, so a pixel two quads deep can differ in
            // the last bit twice. Not a similarity threshold - a wrong shape misses by far more.
            const ColorRgba8 wanted = blend(ground, tint, painted.coverage);
            if (!withinSteps(*pixel, wanted, painted.layers)) {
                const std::optional<PlacedGlyph> placed = expectation.glyph(painted.first);
                outcome.glyphIndex                      = painted.first;
                outcome.expected                        = placed.has_value() ? placed->rect : region;
                outcome.found                           = atPixel(x, y);
                outcome.foundValid                      = true;
                outcome.foundColor                      = *pixel;
                outcome.foundColorValid                 = true;
                outcome.expectedColor                   = wanted;
                return failed(outcome, placed.has_value() && glyphShowsInk(*placed) ? Finding::CoverageDiffers : Finding::GlyphMissing);
            }
        }
    }

    // A non-blank glyph whose sheet slot is empty paints nothing, agrees with the ground everywhere,
    // and would otherwise pass having shown nothing at all - a package whose metrics and coverage
    // disagree, which is worth its own refusal rather than a silent hold.
    for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
        const std::optional<PlacedGlyph> placed = expectation.glyph(index);
        if (!placed.has_value()) {
            continue;
        }
        bool covered = false;
        for (Px dy = 0; dy < placed->rect.height && !covered; ++dy) {
            for (Px dx = 0; dx < placed->rect.width && !covered; ++dx) {
                covered = expectation.coverage(*placed, dx, dy) != 0;
            }
        }
        if (!covered) {
            outcome.glyphIndex = index;
            outcome.expected   = placed->rect;
            outcome.found      = placed->rect;
            outcome.foundValid = true;
            return failed(outcome, Finding::GlyphMissing);
        }
    }

    outcome.glyphIndex    = 0;
    outcome.expected      = region;
    outcome.expectedColor = tint;
    outcome.found         = ink;
    outcome.foundValid    = true;
    return outcome;
}

}  // namespace mdux::verify
