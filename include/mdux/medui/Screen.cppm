/**
 * @file Screen.cppm
 * @brief The governed screen runtime: a compiled screen becomes draw commands, with no allocation,
 *        no parsing and no work a device cannot bound before it runs.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Part of MduXCore, which is what puts it under `mdux-governed-lint` and
 * `governed.noThrow.symbolScan` without either being told about it. The scratch is the caller's, the
 * bound is the screen's own `DrawBudget`, and every refusal is a `Result`.
 *
 * ## What this runtime draws, and what it does not
 *
 * It draws a `Panel`: a filled rectangle in the colour its token resolves to. Every other component
 * is visited, counted, and left undrawn - and that is a stated limit rather than an omission, so it
 * is worth saying exactly why for each.
 *
 * - `Label`, `Button`, `CriticalButton`, `TextInput` draw **text**. A compiled screen carries a
 *   `textKey`, not glyphs (ADR-011), so drawing them is a join with a baked text package for the
 *   locale the device is running. No text package is baked in this repository yet, so the join has
 *   nothing to join to; it belongs with #201, which lands the text half.
 * - `Clock`, `NumericDisplay`, `SignalTrace`, `StatusIndicator` draw from **live data**. Their
 *   geometry does not exist until the frame does - that is ADR-012's reason a screen bakes layout
 *   rather than vertices - so what they paint is a function of a sample this module is not given.
 * - `Image` draws a **baked image package**, which this repository does not yet produce.
 *
 * What is deliberately *not* claimed: that a `Label`'s box should be filled with its colour, or that
 * a `Button` has a face in its. Those are per-component appearance decisions, and nothing in this
 * project settles them today - not the ADRs, which stop at "where each node is and which validated
 * token it draws with", and not the sibling, whose `render_frame` returns frame statistics rather
 * than geometry (`crates/trustsc-ui/src/lib.rs:539`). Inventing them here would make this module
 * authoritative over a question it has no evidence for, so it counts what it cannot decide and says
 * so in `FrameStats::deferred`. A device integrator sees "eleven nodes, one drawn" rather than a
 * screen that quietly renders less than it looks like it should.
 *
 * ## Bounded work, and the counter that proves it
 *
 * Work per frame is the node count times per-node work, both known before the device runs: the node
 * count is fixed in the package, and per-node work is bounded by the `DrawBudget` every write fails
 * closed against. The source language bans loops and recursion, so nothing can make either factor
 * depend on data.
 *
 * `FrameStats::steps` counts each unit of per-node work this runtime performs, and three tests read
 * it: identical screens rendered twice do identical work, *n* and *2n* nodes scale linearly, and -
 * the one that carries the weight - two screens with the *same* node count but different geometry
 * and different colours do the *same* work. That third case is what would catch work proportional to
 * a rectangle's width; the first two would not, since duplicating identical nodes doubles any
 * per-node cost whatever it depends on.
 *
 * What these tests establish is bounded, and the bound is worth stating rather than implying.
 * `steps` is self-reported: a future inner loop that performed work without incrementing it would
 * leave all three green. What they do establish is that the work this runtime performs does not vary
 * with a node's payload or geometry at equal node count, and scales linearly with the node count.
 * Making the per-node half a fact rather than a measurement needs a type-level cap - TrustSC does it
 * with `TextRuntime::<MAX_GLYPH_COMMANDS_PER_RUN>` - and that belongs with the first component whose
 * geometry is variable, which is #17's ground rather than this module's today.
 *
 * ## Two things this deliberately does not do
 *
 * **It does not validate the screen.** `ScreenPackage::validate()` is `constexpr` and generated code
 * carries a `static_assert` over it, so validity is a property of the binary rather than of the
 * frame. Re-checking per frame would pay a quadratic id comparison for something already proved.
 * What this runtime still refuses is a colour token the governed table does not define, because a
 * screen built by hand at run time never met that `static_assert`.
 *
 * **It does not leave a half-drawn frame.** A refused write rolls the list back to where the frame
 * began, so a frame is whole or absent. A partial frame is the worst outcome available on a medical
 * display: it looks like a reading.
 */
module;

export module mdux.medui.screen;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.medui.schema;

export namespace mdux::medui {

/// Why a frame was refused. Every one leaves the draw list exactly as it was found.
enum class ScreenError : std::uint8_t {
    MalformedColorToken,  ///< a node's colour is not of the form `Theme.Colors.<Token>`
    UnknownColorToken,    ///< well-formed, and the governed table does not define it
    BudgetExhausted,      ///< a write would exceed a `DrawBudget` this frame is held to
};

// The two token failures are kept apart because the schema keeps them apart, and for its reason: a
// malformed name is a defect in whatever emitted the screen, while an absent one is a table that
// does not define it. Collapsing them would tell an integrator to look in the wrong place.

[[nodiscard]] std::string_view describe(ScreenError error) noexcept;

/**
 * @brief What one frame did, and what it left undone.
 *
 * `deferred` is the honest half: it counts nodes this runtime visited and could not paint, for the
 * reasons the module comment gives one by one. A caller that expects a screen to be fully drawn can
 * assert it is zero; today, on any screen carrying text or live data, it will not be.
 */
struct FrameStats {
    std::uint32_t nodes{0};     ///< nodes visited
    std::uint32_t rects{0};     ///< rectangles recorded
    std::uint32_t deferred{0};  ///< nodes visited and left undrawn
    std::uint32_t steps{0};     ///< units of per-node work, for the bounded-work tests

    [[nodiscard]] constexpr bool operator==(const FrameStats&) const noexcept = default;
};

/**
 * @brief Records one frame of `screen` into `list`.
 *
 * @param screen a compiled screen, normally the `constexpr` one a generated translation unit holds
 * @param list   a draw list the caller created over storage sized from `screen.budget`
 *
 * Allocation-free and `noexcept`: the list is the only storage written, and it was sized before the
 * first frame. On any error the list is restored to its state at entry.
 *
 * Two budgets are in play and both are enforced. `DrawList` fails closed against the budget it was
 * created with, and this function additionally holds the frame to `screen.budget` - the ceiling the
 * screen itself declares - by measuring what it added. A list may legitimately be larger, because one
 * list can carry several screens; without the second check the screen's declared budget would be
 * decorative, and a mistake in a baked budget would be bypassed rather than observed.
 */
[[nodiscard]] mdux::core::Result<FrameStats, ScreenError> render(const ScreenPackage& screen, mdux::draw::DrawList& list) noexcept;

}  // namespace mdux::medui
