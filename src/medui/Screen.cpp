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
import mdux.medui.schema;

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

}  // namespace

std::string_view describe(ScreenError error) noexcept {
    switch (error) {
        case ScreenError::MalformedColorToken:
            return "a node's colour token is not of the form Theme.Colors.<Token>";
        case ScreenError::UnknownColorToken:
            return "a node names a colour token the governed table does not define";
        case ScreenError::BudgetExhausted:
            return "the frame would exceed a draw budget it is held to";
    }
    // Unreachable for a value of the enumeration, and named rather than defaulted so that adding an
    // enumerator without a case here is a warning at this switch instead of a blank string later.
    return "unknown screen error";
}

mdux::core::Result<FrameStats, ScreenError> render(const ScreenPackage& screen, mdux::draw::DrawList& list) noexcept {
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
