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
 * Nothing *holds* a future component to that, which is what #199 set out to fix. `FrameStats::steps`
 * counts each unit of per-node work this runtime performs, and two tests read it: one renders a
 * screen twice and asserts the count is identical - so the work is a pure function of the package,
 * not of anything ambient - and one renders screens of *n* and *2n* nodes and asserts the count
 * scales. A component that looped until a condition would break the first; one whose work grew with
 * its data would break the second.
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
    UnknownColorToken,  ///< a node names a colour the governed table does not define
    BudgetExhausted,    ///< a write would exceed the `DrawBudget` the screen declares
};

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
 * The caller is responsible for having created `list` against this screen's budget - `DrawList` is
 * the thing that fails closed if it did not, and this reports that as `BudgetExhausted` rather than
 * drawing what fits.
 */
[[nodiscard]] mdux::core::Result<FrameStats, ScreenError> render(const ScreenPackage& screen, mdux::draw::DrawList& list) noexcept;

}  // namespace mdux::medui
