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

/// Whether `value` is within the closed interval the two bounds span, in either order.
[[nodiscard]] bool between(std::uint8_t value, std::uint8_t first, std::uint8_t second) noexcept {
    const std::uint8_t low  = first < second ? first : second;
    const std::uint8_t high = first < second ? second : first;
    return value >= low && value <= high;
}

/**
 * @brief Whether `pixel` could be the tint blended over the ground at some coverage.
 *
 * Channel-wise containment, and it needs no tolerance. Alpha blending produces a convex combination
 * of two integer channel values, and rounding a value that lies between two integers cannot leave
 * the closed interval they span - so a correct blend is always inside, and a pixel outside was
 * painted by something the expectation does not describe.
 */
[[nodiscard]] bool blendOf(ColorRgba8 pixel, ColorRgba8 ground, ColorRgba8 tint) noexcept {
    return between(pixel.r, ground.r, tint.r) && between(pixel.g, ground.g, tint.g) && between(pixel.b, ground.b, tint.b) && between(pixel.a, ground.a, tint.a);
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
        case Finding::InkOutsideTheRun:
            return "the node shows ink where this locale's run paints none";
    }
    return "unknown verification finding";
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

    const auto rowBytes = static_cast<std::uint64_t>(width) * stride;
    if (static_cast<std::uint64_t>(rowStride) < rowBytes) {
        return err(VerifyError::RowStrideTooSmall);
    }
    // The last row need not be padded, so the requirement is every full row but the last, plus one
    // row of pixels. 64-bit throughout: a large extent multiplied in `size_t` on a 32-bit host would
    // wrap into admitting exactly the buffer this refuses.
    const auto required = (static_cast<std::uint64_t>(height) - 1) * static_cast<std::uint64_t>(rowStride) + rowBytes;
    if (static_cast<std::uint64_t>(bytes.size()) < required) {
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
    const std::size_t offset = static_cast<std::size_t>(y) * rowStride_ + static_cast<std::size_t>(x) * bytesPerPixel(format_);
    if (offset + bytesPerPixel(format_) > bytes_.size()) {
        // Unreachable for a view `create()` admitted, and kept because "unreachable" is a property
        // of an invariant rather than of the compiler: a future format with a different stride would
        // find this here instead of past the end of the caller's span.
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

Result<TextExpectation, VerifyError> TextExpectation::create(const mdux::medui::CompiledNode& node,
                                                             RenderScope                      scope,
                                                             std::span<const std::byte>       records,
                                                             const mdux::font::FontPackage&   font,
                                                             ColorRgba8                       ground) noexcept {
    if (textKeyOf(node).empty()) {
        return err(VerifyError::NodeCarriesNoTextKey);
    }
    if (scope.isLocaleFree()) {
        // A text obligation is one node, one check and one *approved locale*: the locale-free scope
        // exists so a textless screen keeps its geometric obligations, not so a text node can lose
        // the only thing that distinguishes one of its two obligations from another.
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

    return TextExpectation{&node, scope, records, &font, mdux::medui::quantise(*resolved), ground, placed, originX, originY};
}

std::optional<NodeRect> TextExpectation::glyphRect(std::size_t index) const noexcept {
    if (index >= glyphCount()) {
        return std::nullopt;
    }
    const auto placement = mdux::text::draw::decodeRecord(records_.subspan(index * mdux::text::draw::recordSize, mdux::text::draw::recordSize));
    if (!placement.has_value() || placement->packageIndex >= font_->glyphs.size()) {
        // Both refused by `create()`, so neither is reachable through a live expectation. Answered
        // as "no rectangle" rather than assumed away, because the alternative is indexing a vector
        // with a number this function did not check.
        return std::nullopt;
    }
    const mdux::font::GlyphRecord& glyph = font_->glyphs[placement->packageIndex];
    if (glyph.isBlank()) {
        return std::nullopt;
    }
    return NodeRect{.x      = originX_ + placement->x + glyph.bitmapOriginX,
                    .y      = originY_ + placement->y - glyph.bitmapOriginY,
                    .width  = static_cast<Px>(glyph.width),
                    .height = static_cast<Px>(glyph.height)};
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
            if (!blendOf(*pixel, ground, tint)) {
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

    // Half one: every glyph this locale's run paints has painted something of its own.
    for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
        const std::optional<NodeRect> glyph = expectation.glyphRect(index);
        if (!glyph.has_value()) {
            // A blank, most often the space. It advances the pen and paints nothing, so there is
            // nothing to find and nothing missing.
            continue;
        }
        outcome.glyphIndex = index;
        if (!frame.contains(*glyph)) {
            outcome.found      = *glyph;
            outcome.foundValid = true;
            return failed(outcome, Finding::RegionOutsideFrame);
        }
        if (!paintedBox(frame, *glyph, ground).inked) {
            outcome.expected   = *glyph;
            outcome.found      = *glyph;
            outcome.foundValid = true;
            return failed(outcome, Finding::GlyphMissing);
        }
    }
    outcome.glyphIndex = 0;
    outcome.expected   = region;

    // Half two: nothing painted anywhere else in the node. The atlas slot is exactly the glyph's
    // bitmap, so a correct frame paints outside no rectangle - which is what stops a different
    // translation, whose glyphs land elsewhere, from satisfying half one and being called present.
    for (Px y = region.y; y < region.y + region.height; ++y) {
        for (Px x = region.x; x < region.x + region.width; ++x) {
            const std::optional<ColorRgba8> pixel = frame.pixelAt(x, y);
            if (!pixel.has_value() || *pixel == ground) {
                continue;
            }
            bool attributed = false;
            for (std::size_t index = 0; index < expectation.glyphCount() && !attributed; ++index) {
                const std::optional<NodeRect> glyph = expectation.glyphRect(index);
                attributed                          = glyph.has_value() && holds(*glyph, x, y);
            }
            outcome.found           = atPixel(x, y);
            outcome.foundValid      = true;
            outcome.foundColor      = *pixel;
            outcome.foundColorValid = true;
            if (!attributed) {
                return failed(outcome, Finding::InkOutsideTheRun);
            }
            if (!blendOf(*pixel, ground, tint)) {
                return failed(outcome, Finding::ForeignColour);
            }
        }
    }

    outcome.found           = expectation.ink();
    outcome.foundValid      = true;
    outcome.foundColor      = ColorRgba8{};
    outcome.foundColorValid = false;
    return outcome;
}

}  // namespace mdux::verify
