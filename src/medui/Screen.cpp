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
import mdux.image.schema;
import mdux.medui.schema;
import mdux.medui.trace;
import mdux.text.draw;
import mdux.text.schema;

namespace mdux::medui {

namespace {

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

    [[nodiscard]] constexpr mdux::core::Px width() const noexcept {
        return right - left;
    }
    [[nodiscard]] constexpr mdux::core::Px height() const noexcept {
        return bottom - top;
    }
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
[[nodiscard]] mdux::core::Result<InkBox, ScreenError> measureInk(const mdux::font::FontPackage& font, std::span<const std::byte> records) noexcept {
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
[[nodiscard]] mdux::core::Result<std::span<const std::byte>, ScreenError> runFor(const TextBinding& binding, std::string_view textKey) noexcept {
    const mdux::text::TextRun* run = binding.text()->find(textKey);
    if (run == nullptr) {
        return mdux::core::err(ScreenError::UnknownTextKey);
    }
    return binding.runs().subspan(static_cast<std::size_t>(run->byteOffset), static_cast<std::size_t>(run->byteLength));
}

}  // namespace

mdux::core::Result<TextBinding, ScreenError> TextBinding::create(const ScreenPackage&           screen,
                                                                 const mdux::font::FontPackage& font,
                                                                 const mdux::text::TextPackage& text,
                                                                 std::span<const std::byte>     packageJson,
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

    // The compiler admits only canonical package bytes, so hashing the caller's already-loaded
    // artifact is the exact identity it recorded. The allocation-free canonical hash additionally
    // proves those bytes describe the `text` object that will drive rendering; otherwise approved
    // bytes for package A could be paired with parsed content from package B.
    const mdux::evidence::Digest packageSha256 = mdux::evidence::sha256(packageJson);
    const auto                   textSha256    = text.canonicalSha256();
    if (!textSha256.has_value() || *textSha256 != packageSha256) {
        return mdux::core::err(ScreenError::PackageNotApproved);
    }

    const auto approved = std::ranges::find_if(screen.approvedTextPackages, [&](const TextPackageApproval& candidate) {
        return candidate.locale == text.locale && candidate.packageId == text.header.id && candidate.packageSha256 == packageSha256;
    });
    if (approved == screen.approvedTextPackages.end()) {
        return mdux::core::err(ScreenError::PackageNotApproved);
    }

    return TextBinding{&font, &text, runs, packageSha256};
}

mdux::core::Result<ImageBinding, ScreenError> ImageBinding::create(const ScreenPackage&             screen,
                                                                   const mdux::image::ImagePackage& image,
                                                                   std::span<const std::byte>       packageJson,
                                                                   std::span<const std::byte>       pixels) noexcept {
    if (pixels.size() != image.sidecarByteLength || mdux::evidence::sha256(pixels) != image.sidecarSha256) {
        return mdux::core::err(ScreenError::ImageSidecarMismatch);
    }
    const mdux::evidence::Digest packageSha256   = mdux::evidence::sha256(packageJson);
    const auto                   canonicalSha256 = image.canonicalSha256();
    if (!canonicalSha256.has_value() || *canonicalSha256 != packageSha256) {
        return mdux::core::err(ScreenError::ImageNotApproved);
    }
    const auto approved = std::ranges::find_if(screen.approvedImagePackages, [&](const ImagePackageApproval& candidate) {
        return candidate.packageId == image.header.id && candidate.packageSha256 == packageSha256 && candidate.width == image.width
               && candidate.height == image.height;
    });
    if (approved == screen.approvedImagePackages.end()) {
        return mdux::core::err(ScreenError::ImageNotApproved);
    }
    return ImageBinding{packageSha256, image.width, image.height};
}

mdux::core::Result<SignalBinding, ScreenError> SignalBinding::create(const ScreenPackage& screen, std::span<const SignalSlot> slots) noexcept {
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const SignalSlot& slot = slots[index];

        if (slot.ring == nullptr) {
            // A slot with no ring is a trace that would draw a dimmed field and nothing in it -
            // indistinguishable, on a monitor, from a flat line. Refused here rather than deferred
            // per frame, because it is a defect in how the caller assembled its slots.
            //
            // Its own error rather than `MalformedTraceStyle`, for the reason stated where the two
            // colour-token failures are kept apart: an absent ring is a slot the caller never
            // finished filling in, while a malformed style is a slot filled in wrongly. They send an
            // integrator to different places.
            return mdux::core::err(ScreenError::MissingSampleRing);
        }
        if (slot.style.strokeWidth < 1 || slot.style.strokeWidth > maxStrokeWidth) {
            return mdux::core::err(ScreenError::MalformedTraceStyle);
        }
        if (!std::isfinite(slot.style.minimum) || !std::isfinite(slot.style.maximum) || !(slot.style.maximum > slot.style.minimum)) {
            return mdux::core::err(ScreenError::MalformedTraceStyle);
        }

        // Quadratic in the slot count, which is a handful: a screen holds tens of nodes and rather
        // fewer traces, and a set would allocate. `ScreenPackage::find()` makes the same trade.
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (slots[earlier].streamSource == slot.streamSource) {
                return mdux::core::err(ScreenError::DuplicateStream);
            }
        }

        const bool named = std::ranges::any_of(screen.nodes, [&](const CompiledNode& node) {
            const NodePayload payload = node.payload;
            const auto*       trace   = std::get_if<SignalTraceSpec>(&payload);
            return trace != nullptr && trace->streamSource == slot.streamSource;
        });
        if (!named) {
            // The check that earns this type. A mistyped stream name would otherwise leave the trace
            // drawing its reserved field forever, and the caller with no way to tell that from a
            // stream that has simply not started.
            return mdux::core::err(ScreenError::UnknownStreamSource);
        }
    }

    return SignalBinding{screen.id, slots};
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
        case ScreenError::PackageNotApproved:
            return "the screen was not compiled against this text package";
        case ScreenError::TextOverflowsNode:
            return "a run's ink is larger than the node that names it";
        case ScreenError::ImageSidecarMismatch:
            return "the RGBA sidecar is not the one the image package describes";
        case ScreenError::ImageNotApproved:
            return "the screen was not compiled against this image package";
        case ScreenError::UnknownStreamSource:
            return "a signal slot names a stream no SignalTrace on this screen carries";
        case ScreenError::DuplicateStream:
            return "two signal slots name the same stream";
        case ScreenError::MissingSampleRing:
            return "a signal slot names a stream but carries no ring to read samples from";
        case ScreenError::MalformedTraceStyle:
            return "a signal slot's sample range is empty or not finite, or its stroke width is out of range";
        case ScreenError::MalformedSampleRing:
            return "a bound ring's oldest index or live count is not a position in its storage";
        case ScreenError::NonFiniteSample:
            return "a live sample is not a finite number";
        case ScreenError::TraceTooLong:
            return "a bound ring holds more samples than this runtime will expand in one trace";
        case ScreenError::TraceBandTooSmall:
            return "a bound trace's node is too small to hold its stroke";
        case ScreenError::ScreenNotApproved:
            return "the signal binding was built for a different screen";
    }
    // Unreachable for a value of the enumeration, and named rather than defaulted so that adding an
    // enumerator without a case here is a warning at this switch instead of a blank string later.
    return "unknown screen error";
}

namespace {

/// A `TraceError` as the screen runtime's caller sees it.
///
/// One-for-one rather than collapsed to a single "the trace was refused", for the reason the two
/// colour-token failures are kept apart: a malformed ring is the producer's defect, a too-long one is
/// a caller asking for more than the cap admits, and a non-finite sample is a driver fault. Sending
/// all three to the same integrator would send two of them to the wrong person.
[[nodiscard]] ScreenError asScreenError(mdux::medui::TraceError error) noexcept {
    switch (error) {
        case TraceError::MalformedRing:
            return ScreenError::MalformedSampleRing;
        case TraceError::TooManySamples:
            return ScreenError::TraceTooLong;
        case TraceError::NonFiniteSample:
            return ScreenError::NonFiniteSample;
        case TraceError::MalformedStyle:
            return ScreenError::MalformedTraceStyle;
        case TraceError::BandTooSmall:
            return ScreenError::TraceBandTooSmall;
        case TraceError::ListRejected:
            return ScreenError::BudgetExhausted;
    }
    // Named rather than defaulted, so a new TraceError is a warning here rather than a frame refused
    // with a reason that names the wrong thing.
    return ScreenError::BudgetExhausted;
}

}  // namespace

mdux::core::Result<FrameStats, ScreenError>
render(const ScreenPackage& screen, mdux::draw::DrawList& list, const TextBinding& text, const ImageBinding& image, const SignalBinding& signals) noexcept {
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

    // A binding retains the identity create() authenticated. Re-checking it against the target
    // screen closes the cross-screen substitution path without rehashing or allocating.
    if (!text.approvedBy(screen)) {
        return refuse(ScreenError::PackageNotApproved);
    }
    if (!image.approvedBy(screen)) {
        return refuse(ScreenError::ImageNotApproved);
    }

    // The same closure for signals, and the same reason: slots validated against screen A's traces
    // say nothing about screen B's. Weaker than the text binding's check by exactly as much as the
    // available evidence is weaker - an id rather than a digest - which `approvedBy()` says.
    if (!signals.approvedBy(screen)) {
        return refuse(ScreenError::ScreenNotApproved);
    }

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

        if (const auto* imageSpec = std::get_if<ImageSpec>(&node.payload); imageSpec != nullptr) {
            if (!image.bound()) {
                ++stats.deferred;
                continue;
            }
            const auto approval = std::ranges::find_if(screen.approvedImagePackages, [imageSpec](const ImagePackageApproval& candidate) {
                return candidate.packageId == imageSpec->source;
            });
            if (approval == screen.approvedImagePackages.end() || !image.matches(*approval) || approval->width != static_cast<std::uint32_t>(node.bounds.width)
                || approval->height != static_cast<std::uint32_t>(node.bounds.height)) {
                return refuse(ScreenError::ImageNotApproved);
            }
            if (const auto recorded = list.addRect(toRect(node.bounds),
                                                   mdux::core::ColorRgba8{255, 255, 255, 255},
                                                   mdux::draw::DrawMode::SampledRgba,
                                                   mdux::draw::UvRect{.u0 = 0.0F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F});
                !recorded.has_value()) {
                return refuse(ScreenError::BudgetExhausted);
            }
            if (!withinScreenBudget()) {
                return refuse(ScreenError::BudgetExhausted);
            }
            ++stats.rects;
            ++stats.steps;
            continue;
        }

        if (const auto* label = std::get_if<LabelSpec>(&node.payload); label != nullptr) {
            if (!text.bound()) {
                // Nothing to join to. Deferred rather than refused: a caller that has not bound a
                // locale yet is in a normal state, not a broken one.
                ++stats.deferred;
                continue;
            }

            const auto labelColour = resolveColorToken(label->colorToken);
            if (!labelColour.has_value()) {
                return refuse(labelColour.error() == ThemeError::MalformedToken ? ScreenError::MalformedColorToken : ScreenError::UnknownColorToken);
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
            if (ink->width() > static_cast<mdux::core::Px>(node.bounds.width) || ink->height() > static_cast<mdux::core::Px>(node.bounds.height)) {
                return refuse(ScreenError::TextOverflowsNode);
            }

            // The ink box's corner goes to the node's corner. `originX`/`originY` are added to every
            // record, so subtracting where the ink starts puts that corner exactly on the origin.
            const auto originX = static_cast<mdux::core::Px>(node.bounds.x) - ink->left;
            const auto originY = static_cast<mdux::core::Px>(node.bounds.y) - ink->top;

            const std::size_t verticesBefore = list.vertices().size();
            if (const auto recorded = mdux::text::draw::recordRun(list, *text.font(), *records, originX, originY, quantise(*labelColour));
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

        // A `SignalTrace` the caller has samples for. Everything else about the node - where it is,
        // which token it draws with - is still the artifact's; what the binding adds is the samples
        // and the scale, which no compiled screen can carry (see `SignalSlot`).
        if (const auto* trace = std::get_if<SignalTraceSpec>(&node.payload); trace != nullptr) {
            if (const SignalSlot* slot = signals.find(trace->streamSource); slot != nullptr) {
                const auto colour = resolveColorToken(trace->colorToken);
                if (!colour.has_value()) {
                    return refuse(colour.error() == ThemeError::MalformedToken ? ScreenError::MalformedColorToken : ScreenError::UnknownColorToken);
                }

                // The field at reduced coverage, then the stroke at full tint. See Screen.cppm,
                // `boundTraceFieldCoverage`, for why one tint at two coverages is the only
                // composition a `ColorHash` golden and an additive draw list both admit.
                mdux::core::ColorRgba8 fieldColour = quantise(*colour);
                fieldColour.a                      = quantise((*colour)[3] * boundTraceFieldCoverage);
                if (const auto recorded = list.addSolidRect(toRect(node.bounds), fieldColour); !recorded.has_value()) {
                    return refuse(ScreenError::BudgetExhausted);
                }
                ++stats.rects;

                const std::size_t verticesBefore = list.vertices().size();
                if (const auto recorded = mdux::medui::recordTrace(list, toRect(node.bounds), *slot->ring, slot->style, quantise(*colour));
                    !recorded.has_value()) {
                    // `recordTrace()` rolls its own trace back and this rolls the whole frame back.
                    // Its error *is* forwarded, unlike the label path's: every way it fails is a
                    // distinct thing the caller can act on, and none of them was already refused here.
                    return refuse(asScreenError(recorded.error()));
                }
                if (!withinScreenBudget()) {
                    return refuse(ScreenError::BudgetExhausted);
                }

                // Payload-proportional work, bounded by `maxSamplesPerTrace` exactly as a label's is
                // by `maxGlyphsPerRun`. Counted here so the bounded-work tests keep reporting a
                // number that is still a function of what this frame actually did.
                stats.steps += static_cast<std::uint32_t>(slot->ring->count);
                stats.rects += static_cast<std::uint32_t>((list.vertices().size() - verticesBefore) / 4);
                ++stats.traces;
                ++stats.steps;
                continue;
            }
        }

        const std::optional<std::string_view> field = fieldColorToken(node.payload);
        if (!field.has_value()) {
            // Visited and left undrawn. The module comment says which components these are and why
            // each one's appearance is not decidable from a compiled screen alone.
            ++stats.deferred;
            continue;
        }

        // One path for every node whose whole rectangle is filled with one token - the `Row`
        // background the solver synthesised, and since #255 the field a `NumericDisplay` or a
        // `SignalTrace` reserves. They differ in what will later be drawn *inside* the rectangle and
        // not at all in what fills it, so a second copy of this arithmetic would only be a place for
        // the two to drift.
        const auto colour = resolveColorToken(*field);
        if (!colour.has_value()) {
            return refuse(colour.error() == ThemeError::MalformedToken ? ScreenError::MalformedColorToken : ScreenError::UnknownColorToken);
        }

        if (const auto recorded = list.addSolidRect(toRect(node.bounds), quantise(*colour)); !recorded.has_value()) {
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
