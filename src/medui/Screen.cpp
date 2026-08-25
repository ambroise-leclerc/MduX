/**
 * @file Screen.cpp
 * @brief Implementation of the governed screen runtime.
 */

module;

module mdux.medui.screen;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.text.draw;
import mdux.text.schema;

namespace mdux::medui {

namespace {

/// A linear channel as the byte a `R8G8B8A8_UNORM` vertex colour carries.
///
/// Quantisation and nothing else: the governed table stores linear RGBA, the vertex colour is read
/// by the shader as a plain 0..1 UNORM value, so the byte is the linear value scaled. No transfer
/// function is applied here, because whether the *swapchain* is sRGB is the renderer's decision and
/// applying one in two places is how a colour ends up encoded twice.
///
/// The multiply and the add are separate statements so that neither the rounding nor the result
/// depends on whether the compiler fuses them. `mdux_enforce_fp_determinism(MduXCore)` already turns
/// contraction off for this target, and this is the belt to that pair of braces: a frame that is
/// byte-compared across toolchains cannot afford a last-bit difference in a colour.
[[nodiscard]] std::uint8_t quantise(float channel) noexcept {
    const float clamped = channel < 0.0F ? 0.0F : (channel > 1.0F ? 1.0F : channel);
    const float scaled  = clamped * 255.0F;
    const float rounded = scaled + 0.5F;
    return static_cast<std::uint8_t>(rounded);
}

[[nodiscard]] mdux::core::ColorRgba8 toColor(const std::array<float, 4>& linear) noexcept {
    return mdux::core::ColorRgba8{.r = quantise(linear[0]), .g = quantise(linear[1]), .b = quantise(linear[2]), .a = quantise(linear[3])};
}

[[nodiscard]] mdux::core::Rect toRect(const NodeRect& bounds) noexcept {
    return mdux::core::Rect{.x      = static_cast<mdux::core::Px>(bounds.x),
                            .y      = static_cast<mdux::core::Px>(bounds.y),
                            .width  = static_cast<mdux::core::Px>(bounds.width),
                            .height = static_cast<mdux::core::Px>(bounds.height)};
}

/// The box the ink of one run occupies, in the run's own coordinates.
///
/// `inked` false means the run paints nothing at all - a single space, say - which is a legitimate
/// run rather than an error, and one that produces no rectangles.
struct InkBox {
    bool           inked{false};
    mdux::core::Px left{0};
    mdux::core::Px top{0};
    mdux::core::Px right{0};   ///< exclusive
    mdux::core::Px bottom{0};  ///< exclusive

    [[nodiscard]] constexpr mdux::core::Px width() const noexcept { return right - left; }
    [[nodiscard]] constexpr mdux::core::Px height() const noexcept { return bottom - top; }
};

/**
 * @brief Measures where a run's ink begins, so it can be placed where the compiler measured it.
 *
 * The union of the glyph rectangles' left and top edges, blanks skipped - the same quantity #195's
 * `measureRun()` takes the full extent of, computed the same way and for the same reason: the box
 * the build-time budget proved fits is the box this places. See Screen.cppm, "Where a label's glyphs
 * go", for why that pairing is the decision rather than an arbitrary one.
 *
 * Placement arithmetic is `addGlyphRect()`'s and is not restated: `bitmapOriginX` from the pen, and
 * `bitmapOriginY` measured *up* from the baseline, hence subtracted on a downward y axis.
 */
[[nodiscard]] mdux::core::Result<InkBox, ScreenError> measureInk(const mdux::font::FontPackage& font,
                                                                 std::span<const std::byte>     records) noexcept {
    InkBox box;
    for (std::size_t offset = 0; offset < records.size(); offset += mdux::text::draw::recordSize) {
        const auto placement = mdux::text::draw::decodeRecord(records.subspan(offset, mdux::text::draw::recordSize));
        if (!placement.has_value()) {
            return mdux::core::err(ScreenError::MalformedTextRun);
        }
        if (placement->packageIndex >= font.glyphs.size()) {
            return mdux::core::err(ScreenError::MalformedTextRun);
        }
        const mdux::font::GlyphRecord& glyph = font.glyphs[placement->packageIndex];
        if (glyph.isBlank()) {
            continue;
        }
        const auto left   = static_cast<mdux::core::Px>(placement->x + glyph.bitmapOriginX);
        const auto top    = static_cast<mdux::core::Px>(placement->y - glyph.bitmapOriginY);
        const auto right  = static_cast<mdux::core::Px>(left + static_cast<mdux::core::Px>(glyph.width));
        const auto bottom = static_cast<mdux::core::Px>(top + static_cast<mdux::core::Px>(glyph.height));
        if (!box.inked) {
            box = InkBox{.inked = true, .left = left, .top = top, .right = right, .bottom = bottom};
            continue;
        }
        box.left   = left < box.left ? left : box.left;
        box.top    = top < box.top ? top : box.top;
        box.right  = right > box.right ? right : box.right;
        box.bottom = bottom > box.bottom ? bottom : box.bottom;
    }
    return box;
}

/// The run a node's `textKey` names, as a span of the bound sidecar.
///
/// A lookup and nothing else. Every structural property of the range - inside the sidecar, a whole
/// number of records, within the cap, hashing to what the package recorded - was established once by
/// `TextBinding::create()`, which is what a binding *is*. Repeating them here would put a hash in a
/// frame; not establishing them anywhere would put a `subspan()` precondition in the hands of a
/// package nobody checked.
///
/// The one failure left is the screen's rather than the binding's: a key this package does not
/// carry means the wrong locale, or the wrong package, was bound for this screen.
[[nodiscard]] mdux::core::Result<std::span<const std::byte>, ScreenError> runFor(const TextBinding& binding,
                                                                                  std::string_view   textKey) noexcept {
    const mdux::text::TextRun* run = binding.text()->find(textKey);
    if (run == nullptr) {
        return mdux::core::err(ScreenError::UnknownTextKey);
    }
    return binding.runs().subspan(static_cast<std::size_t>(run->byteOffset), static_cast<std::size_t>(run->byteLength));
}

}  // namespace

mdux::core::Result<TextBinding, ScreenError> TextBinding::create(const mdux::font::FontPackage& font,
                                                                 const mdux::text::TextPackage& text,
                                                                 std::span<const std::byte>     runs) noexcept {
    // The runs index `font.glyphs` by position, so a package baked against another font names the
    // same numbers for different shapes. Nothing downstream can detect that: every index would still
    // be in range, and the frame would draw a plausible sentence made of the wrong letters.
    if (text.atlasId != font.id) {
        return mdux::core::err(ScreenError::AtlasMismatch);
    }

    // The sidecar the package describes, exactly. Length first because it is free and rules out the
    // common mistake; the digest because a different sidecar of the same length passes every
    // structural check there is and renders different words.
    if (runs.size() != text.sidecarByteLength) {
        return mdux::core::err(ScreenError::SidecarMismatch);
    }
    if (mdux::evidence::sha256(runs) != text.sidecarSha256) {
        return mdux::core::err(ScreenError::SidecarMismatch);
    }

    for (const mdux::text::TextRun& run : text.runs) {
        // Subtraction rather than `byteOffset + byteLength`, which wraps: a hand-built package with
        // `byteOffset = UINT64_MAX - 5` and `byteLength = 6` sums to zero and would pass an addition
        // form, then violate `subspan()`'s precondition. This is the form `TextPackage::validate()`
        // already uses, for the same reason - a package that reached here without going through it
        // is exactly the case this function exists for.
        if (run.byteOffset > runs.size() || run.byteLength > runs.size() - run.byteOffset) {
            return mdux::core::err(ScreenError::MalformedTextRun);
        }
        if (run.byteLength % mdux::text::draw::recordSize != 0) {
            return mdux::core::err(ScreenError::MalformedTextRun);
        }
        if (run.byteLength / mdux::text::draw::recordSize > maxGlyphsPerRun) {
            return mdux::core::err(ScreenError::RunTooLong);
        }
        // Each run's own digest as well as the sidecar's. The sidecar hash already covers these
        // bytes, so this catches a package whose *ranges* were rewritten to point at each other's
        // runs - which the whole-file hash cannot see.
        const auto slice = runs.subspan(static_cast<std::size_t>(run.byteOffset), static_cast<std::size_t>(run.byteLength));
        if (mdux::evidence::sha256(slice) != run.sha256) {
            return mdux::core::err(ScreenError::SidecarMismatch);
        }
    }

    return TextBinding{&font, &text, runs};
}

std::string_view describe(ScreenError error) noexcept {
    switch (error) {
        case ScreenError::MalformedColorToken:
            return "a node's colour token is not of the form Theme.Colors.<Token>";
        case ScreenError::UnknownColorToken:
            return "a node names a colour token the governed table does not define";
        case ScreenError::BudgetExhausted:
            return "the frame would exceed a draw budget it is held to";
        case ScreenError::UnknownTextKey:
            return "the bound text package carries no run for a node's text key";
        case ScreenError::MalformedTextRun:
            return "a run's range leaves the bound sidecar, or its bytes are not whole records";
        case ScreenError::RunTooLong:
            return "a run holds more records than this runtime will draw in one node";
        case ScreenError::AtlasMismatch:
            return "the text package was baked against a different font package";
        case ScreenError::SidecarMismatch:
            return "the sidecar is not the one the text package describes";
        case ScreenError::TextOverflowsNode:
            return "a run's ink is larger than the node that names it";
    }
    // Unreachable for a value of the enumeration, and named rather than defaulted so that adding an
    // enumerator without a case here is a warning at this switch instead of a blank string later.
    return "unknown screen error";
}

mdux::core::Result<FrameStats, ScreenError> render(const ScreenPackage& screen, mdux::draw::DrawList& list,
                                                   const TextBinding& text) noexcept {
    // Taken before anything is recorded: every refusal below rolls back to here, so a frame is
    // whole or absent. A half-drawn frame on a medical display is the worst outcome available,
    // because it looks like a reading.
    const mdux::draw::DrawList::Marker start = list.mark();

    const auto refuse = [&list, &start](ScreenError error) {
        // The rollback cannot fail for a marker this function took from this list moments ago; the
        // result is discarded rather than checked because there is no second recovery to attempt,
        // and the error being returned is the one the caller needs.
        static_cast<void>(list.rollback(start));
        return mdux::core::err(error);
    };

    // Where the list stood before this frame. The screen's own budget bounds what *this screen*
    // draws, and `DrawList` can only enforce the budget it was created with - which may be larger,
    // because one list may carry several screens. Without this the declared ceiling was decorative:
    // a screen declaring room for one rectangle drew two whenever the caller passed a roomier list,
    // and a mistake in the baked budget was silently bypassed instead of being observable.
    const std::size_t vertexBase  = list.vertices().size();
    const std::size_t indexBase   = list.indices().size();
    const std::size_t commandBase = list.commands().size();

    const auto withinScreenBudget = [&]() noexcept {
        return list.vertices().size() - vertexBase <= screen.budget.maxVertices && list.indices().size() - indexBase <= screen.budget.maxIndices
               && list.commands().size() - commandBase <= screen.budget.maxCommands;
    };

    FrameStats stats;

    for (const CompiledNode& node : screen.nodes) {
        ++stats.nodes;
        ++stats.steps;

        if (const auto* label = std::get_if<LabelSpec>(&node.payload); label != nullptr) {
            if (!text.bound()) {
                // Nothing to join to. Deferred rather than refused: a caller that has not bound a
                // locale yet is in a normal state, not a broken one.
                ++stats.deferred;
                continue;
            }

            const auto labelColour = resolveColorToken(label->colorToken);
            if (!labelColour.has_value()) {
                return refuse(labelColour.error() == ThemeError::MalformedToken ? ScreenError::MalformedColorToken
                                                                                : ScreenError::UnknownColorToken);
            }

            const auto records = runFor(text, label->textKey);
            if (!records.has_value()) {
                return refuse(records.error());
            }

            const auto ink = measureInk(*text.font(), *records);
            if (!ink.has_value()) {
                return refuse(ink.error());
            }

            // Counted per record, not per node: this is the payload-proportional work that
            // `maxGlyphsPerRun` bounds, and `steps` has to say so or the bounded-work tests would be
            // reporting a constant that stopped being one.
            stats.steps += static_cast<std::uint32_t>(records->size() / mdux::text::draw::recordSize);

            if (!ink->inked) {
                // A run that paints nothing - a single space. Joined, measured, and found to have no
                // ink, which is a different outcome from having no package to join to, so it is not
                // counted as deferred.
                continue;
            }

            // The check that makes the placement rule safe against a package this module cannot
            // authenticate. #195 proved this box fits this rectangle for the package it measured;
            // this proves it for the package actually bound, at the cost of one comparison over a
            // walk the placement needs anyway. See Screen.cppm for why the two are not redundant.
            if (ink->width() > static_cast<mdux::core::Px>(node.bounds.width)
                || ink->height() > static_cast<mdux::core::Px>(node.bounds.height)) {
                return refuse(ScreenError::TextOverflowsNode);
            }

            // The ink box's corner goes to the node's corner. `originX`/`originY` are added to every
            // record, so subtracting where the ink starts puts that corner exactly on the origin.
            const auto originX = static_cast<mdux::core::Px>(node.bounds.x) - ink->left;
            const auto originY = static_cast<mdux::core::Px>(node.bounds.y) - ink->top;

            const std::size_t verticesBefore = list.vertices().size();
            if (const auto recorded =
                    mdux::text::draw::recordRun(list, *text.font(), *records, originX, originY, toColor(*labelColour));
                !recorded.has_value()) {
                // `recordRun()` rolls its own run back and this rolls the whole frame back. Its error
                // is not forwarded: every way it can fail here is either something `runFor()` and
                // `measureInk()` already refused, or the list declining a write - and the second is
                // the one a caller can do anything about.
                return refuse(ScreenError::BudgetExhausted);
            }
            if (!withinScreenBudget()) {
                return refuse(ScreenError::BudgetExhausted);
            }
            // Measured rather than predicted, for the reason the panel path gives below: a rectangle
            // costs four vertices, and reading the delta keeps this from carrying a second copy of
            // arithmetic `DrawList` owns. Blank glyphs record nothing, so this counts the inked ones.
            stats.rects += static_cast<std::uint32_t>((list.vertices().size() - verticesBefore) / 4);
            continue;
        }

        const auto* panel = std::get_if<PanelSpec>(&node.payload);
        if (panel == nullptr) {
            // Visited and left undrawn. The module comment says which components these are and why
            // each one's appearance is not decidable from a compiled screen alone.
            ++stats.deferred;
            continue;
        }

        const auto colour = resolveColorToken(panel->colorToken);
        if (!colour.has_value()) {
            return refuse(colour.error() == ThemeError::MalformedToken ? ScreenError::MalformedColorToken : ScreenError::UnknownColorToken);
        }

        if (const auto recorded = list.addSolidRect(toRect(node.bounds), toColor(*colour)); !recorded.has_value()) {
            return refuse(ScreenError::BudgetExhausted);
        }
        // Measured rather than predicted: a rectangle costs four vertices and six indices, and
        // extends the current command or starts a new one depending on the clip. Reading the deltas
        // keeps this from carrying a second copy of arithmetic `DrawList` already owns.
        if (!withinScreenBudget()) {
            return refuse(ScreenError::BudgetExhausted);
        }
        ++stats.rects;
        ++stats.steps;
    }

    return stats;
}

}  // namespace mdux::medui
