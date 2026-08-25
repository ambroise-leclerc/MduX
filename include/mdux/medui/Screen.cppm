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
 * It draws a `Panel`: a filled rectangle in the colour its token resolves to. It draws a `Label`
 * too, when the caller supplies the packages a locale-free screen has to be joined to. Every other
 * component is visited, counted, and left undrawn - and that is a stated limit rather than an
 * omission, so it is worth saying exactly why for each.
 *
 * - `Label` draws **text**, and does so through `TextBinding` (#242). A compiled screen carries a
 *   `textKey`, not glyphs (ADR-011), so drawing one is a join with a baked text package for the
 *   locale the device is running. Without a binding there is nothing to join to and the node is
 *   deferred exactly as before, which is also what a screen with no text costs: nothing.
 * - `Button`, `CriticalButton`, `TextInput` carry text keys as well, and are still deferred. Their
 *   text is not the whole of their appearance - a button has a face, a text input has a caret and a
 *   selection - and this module will not invent those. They arrive with #17.
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
 * ## Where a label's glyphs go, and why that is not an invention
 *
 * Drawing text needs one decision this module cannot avoid making: where the run sits inside the
 * node's rectangle. The shared component model does not say - it constrains which fields a `Label`
 * carries and that its text must fit its box, and stops there - and the font package carries no
 * ascent, so there is no baseline metric to derive a placement from either.
 *
 * The rule here is therefore chosen, and chosen to be the one that is **already validated**: the
 * run's *ink* box is placed at the node's top-left corner. #195 measures ink - the union of the
 * rectangles the atlas actually paints, blanks skipped - against the node's resolved bounds, and
 * refuses the screen when it does not fit. Placing that same box at that same origin makes the
 * property the compiler checked and the property the frame has the identical property. Centring, or
 * a baseline offset, would leave the build-time guarantee true of a rectangle nobody draws.
 *
 * That argument holds only while the bound package is the one the compiler measured, and nothing in
 * the artifacts says so (see `TextBinding`). So the same box is measured again here and refused as
 * `TextOverflowsNode` when it does not fit, which costs one comparison over a walk the placement
 * needs anyway. The build-time check remains the one that reports a *useful* diagnostic, at the
 * right time, to the person who can fix it; this one exists so that a screen can never draw text
 * over its neighbours because it was joined to the wrong package.
 *
 * Two consequences worth stating rather than discovering. Leading placement means a right-to-left
 * locale would need a different rule - and cannot arrive without one, since `mdux-textbake` refuses
 * every RTL code point (ADR-010's v1 repertoire). And ink placement means a label whose text has no
 * ink at all - a single space - draws nothing and is *not* deferred: it was joined, measured, and
 * found to paint nothing, which is a different fact from having no package to join to.
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
 * leave all three green. What they do establish is that the work this runtime performs scales
 * linearly with the node count.
 *
 * A `Label` is the first component whose per-node work is *not* constant: it is proportional to the
 * run's length. This paragraph used to promise the cap that fixes it - "a type-level cap, as TrustSC
 * does with `TextRuntime::<MAX_GLYPH_COMMANDS_PER_RUN>`, belonging with the first component whose
 * geometry is variable" - and `maxGlyphsPerRun` is it. A run longer than that is refused rather than
 * truncated, so per-node work is bounded by a constant a device knows before it runs, and the bound
 * on a frame is `nodes * maxGlyphsPerRun` rather than a number that depends on what a translator
 * wrote.
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
import mdux.font.schema;
import mdux.medui.schema;
import mdux.text.draw;
import mdux.text.schema;

export namespace mdux::medui {

/// Why a frame was refused. Every one leaves the draw list exactly as it was found.
enum class ScreenError : std::uint8_t {
    MalformedColorToken,  ///< a node's colour is not of the form `Theme.Colors.<Token>`
    UnknownColorToken,    ///< well-formed, and the governed table does not define it
    BudgetExhausted,      ///< a write would exceed a `DrawBudget` this frame is held to
    UnknownTextKey,       ///< a bound text package carries no run for a node's `textKey`
    MalformedTextRun,     ///< a run's range leaves the sidecar, or its bytes are not whole records
    RunTooLong,           ///< a run holds more than `maxGlyphsPerRun` records
    AtlasMismatch,        ///< the text package was baked against a different font package
    SidecarMismatch,      ///< the sidecar is not the one the text package describes
    PackageNotApproved,   ///< the screen was not compiled against this locale/package/digest
    TextOverflowsNode,    ///< a run's ink is wider or taller than the node that names it
};

// The two token failures are kept apart because the schema keeps them apart, and for its reason: a
// malformed name is a defect in whatever emitted the screen, while an absent one is a table that
// does not define it. Collapsing them would tell an integrator to look in the wrong place.

[[nodiscard]] std::string_view describe(ScreenError error) noexcept;

// `AtlasMismatch`, `SidecarMismatch` and `PackageNotApproved` are `create()`'s, not a frame's: they
// are properties of the four artifacts a caller bound together, decided once. `TextOverflowsNode`
// is a frame's, and remains defense in depth after the package identity is authenticated.

/**
 * @brief The largest run this runtime will draw, in records.
 *
 * The type-level cap the bounded-work section above promises. Its job is not to be generous - it is
 * to make per-node work a constant a device can multiply by its node count before it runs, rather
 * than a number that moves when somebody edits a translation.
 *
 * 256 because it is comfortably past any label a 1280px panel can hold at a legible size - the
 * committed screen's title is 17 - while staying a number a reviewer can multiply in their head
 * against a `DrawBudget`. A run beyond it is `RunTooLong`, never truncated: half a sentence on a
 * medical display reads as a whole one.
 */
inline constexpr std::size_t maxGlyphsPerRun = 256;

/**
 * @brief The packages a locale-independent screen layout is joined to, once consistency is proved.
 *
 * A compiled screen carries `textKey` rather than glyphs (ADR-011 as amended by #203), which is what
 * lets one screen serve every approved locale. The join is the device's, once, for the locale it is
 * running - and it is passed rather than looked up because this module performs no I/O and holds no
 * state.
 *
 * ## Why this is not an aggregate
 *
 * Four artifacts have to agree before any of them can be trusted, and none of the agreements can be
 * checked cheaply enough to repeat per frame:
 *
 * - the screen must list this text package's locale, id and canonical-package digest, or a second,
 *   individually valid package could display wording the screen's review never approved;
 * - the text package must be the one baked against *this* font, or the run records index a glyph
 *   table that assigns different shapes to the same numbers, and the frame draws a plausible
 *   sentence made of the wrong letters;
 * - the sidecar must be the one the package describes, which `sidecarByteLength` and
 *   `sidecarSha256` state exactly - a different sidecar of the same length passes every structural
 *   check and renders different words;
 * - every run's range must lie inside it, be a whole number of records, and hash to what the
 *   package recorded.
 *
 * So `create()` proves all of that once and is the only way to obtain a bound `TextBinding`. What
 * `render()` does per frame is a key lookup and a bounds comparison, which is what keeps the
 * bounded-work claim intact. A default-constructed binding is *unbound* and means "this caller has
 * no text": every text node is deferred, exactly as before #242, which is not an error.
 *
 * Nothing here is owned. The packages outlive the frame - on a device they are static - and the span
 * is the caller's storage, so `render()` allocates nothing by construction rather than by
 * discipline.
 */
class TextBinding {
public:
    /// An unbound binding: no text, every text node deferred.
    constexpr TextBinding() noexcept = default;

    /**
     * @brief Proves the screen approved `text`, and that `font`, `text` and `runs` agree.
     *
     * Hashes the sidecar, so it is linear in its size - once, at start-up, never in a frame.
     *
     * @param screen the compiled screen whose approval manifest authorizes the text package
     * @param font the font package the runs were positioned against
     * @param text the text package for the locale being run
     * @param runs the sidecar bytes `text` addresses ranges of
     */
    [[nodiscard]] static mdux::core::Result<TextBinding, ScreenError>
    create(const ScreenPackage& screen, const mdux::font::FontPackage& font, const mdux::text::TextPackage& text, std::span<const std::byte> runs) noexcept;

    /// Whether this binding carries packages. False for a default-constructed one.
    [[nodiscard]] constexpr bool bound() const noexcept {
        return font_ != nullptr && text_ != nullptr;
    }

    [[nodiscard]] constexpr const mdux::font::FontPackage* font() const noexcept {
        return font_;
    }
    [[nodiscard]] constexpr const mdux::text::TextPackage* text() const noexcept {
        return text_;
    }
    [[nodiscard]] constexpr std::span<const std::byte> runs() const noexcept {
        return runs_;
    }

private:
    constexpr TextBinding(const mdux::font::FontPackage* font, const mdux::text::TextPackage* text, std::span<const std::byte> runs) noexcept
        : font_{font}, text_{text}, runs_{runs} {}

    const mdux::font::FontPackage* font_{nullptr};
    const mdux::text::TextPackage* text_{nullptr};
    std::span<const std::byte>     runs_{};
};

// The guarantee `create()` is documented to give, held by the language rather than by discipline.
// A class with private data members is not an aggregate, so `TextBinding{...}` cannot brace-elide
// its way past the checks - and if a future edit makes the members public "for convenience", this
// fails here rather than silently reopening the bypass.
static_assert(!std::is_aggregate_v<TextBinding>, "a TextBinding must only be obtainable through create()");

/**
 * @brief What one frame did, and what it left undone.
 *
 * `deferred` is the honest half: it counts nodes this runtime visited and could not paint, for the
 * reasons the module comment gives one by one. A caller that expects a screen to be fully drawn can
 * assert it is zero; today, on any screen carrying live data, it will not be - and on one carrying
 * text it will not be either unless a `TextBinding` was supplied.
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
 * @param text   the packages this screen's text keys resolve against; default means "no text", and
 *               every text node is then deferred rather than refused
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
[[nodiscard]] mdux::core::Result<FrameStats, ScreenError>
render(const ScreenPackage& screen, mdux::draw::DrawList& list, const TextBinding& text = {}) noexcept;

}  // namespace mdux::medui
