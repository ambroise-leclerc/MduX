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
 * too, when the caller supplies the packages a locale-free screen has to be joined to. Since #255 it
 * also draws the **field** a `NumericDisplay` or a `SignalTrace` reserves: the node's whole resolved
 * rectangle, in the single colour token that node carries. Every other component is visited,
 * counted, and left undrawn - and that is a stated limit rather than an omission, so it is worth
 * saying exactly why for each.
 *
 * - `Label` draws **text**, and does so through `TextBinding` (#242). A compiled screen carries a
 *   `textKey`, not glyphs (ADR-011), so drawing one is a join with a baked text package for the
 *   locale the device is running. Without a binding there is nothing to join to and the node is
 *   deferred exactly as before, which is also what a screen with no text costs: nothing.
 * - `NumericDisplay` draws its field, and since #258 the **digits inside it** as well, when the
 *   caller supplies a `ReadingBinding` naming that node. Without one the node is exactly what it was
 *   after #255: the opaque field it reserves. The expansion is `mdux.medui.reading`'s, and what a
 *   `templateId` stands for arrives with the value rather than out of the artifact - see
 *   `ReadingSlot` for why, and for what the runtime re-checks so that cannot go wrong quietly.
 * - `SignalTrace` draws its field, and since #257 the **waveform inside it** as well, when the
 *   caller supplies a `SignalBinding` naming that node's stream. Without one the node is exactly
 *   what it was after #255: the opaque field it reserves. The expansion itself is
 *   `mdux.medui.trace`'s, split out for the reason `mdux.text.draw` is - geometry that can be tested
 *   against numbers rather than only against a rendered frame.
 * - `Button`, `CriticalButton`, `TextInput` carry text keys or live sources as well, and are still
 *   deferred whole. Their text is not the whole of their appearance - a button has a face, a text
 *   input has a caret and a selection - and this module will not invent those. They arrive with #17.
 * - `Clock` carries no colour token at all, and a `StatusIndicator` carries one per state, so
 *   neither has a single tint a *field* could be painted in - which is the same reason
 *   `collectGoldens()` refuses `ColorHash` for such a node. A `StatusIndicator` therefore stays
 *   deferred until #259. A `Clock` draws since #258, and draws its **reading and nothing else**:
 *   there is no field under it, because there is no token to paint one in. Its tint comes from the
 *   `ReadingBinding`, which is the one appearance decision in this module a caller makes rather than
 *   an artifact - see `ReadingBinding` for why that contradicts no golden.
 * - `Image` draws when the caller supplies the exact baked package the screen approved (#256).
 *   Without an `ImageBinding` it is deferred, just as a Label is without text.
 *
 * ## Why the field is read off the artifact rather than invented here
 *
 * This module used to defer `NumericDisplay` and `SignalTrace` too, on the ground that "what they
 * paint is a function of a sample this module is not given" - and that remains true of the reading.
 * It was never true of the field, and the artifact says so.
 *
 * `collectGoldens()` applies ADR-011's predicate while the AST still carries its inputs and writes
 * one entry per selected node into `goldens.json`: `bounds` is that node's **whole** resolved
 * rectangle and `colorToken` is the single token the author gave it. ADR-014 decision 2 makes those
 * two values the verifier's expectation, and `mdux::verify::goldenBounds()` reads the first as an
 * equality - the content inside the declared rectangle has to *be* that rectangle, edge for edge.
 * So the compiled artifact already asserts, on the author's behalf and in a file four CI legs
 * byte-compare, that this node's whole rectangle carries that tint. A runtime that painted nothing
 * there would not be declining to invent an appearance; it would be disagreeing with the artifact
 * its own compiler emitted, which is the state ADR-014's consequences recorded and #255 closes.
 *
 * What is still deliberately *not* claimed: that a `Label`'s box should be filled with its colour,
 * or that a `Button` has a face in its. Neither has a golden that says so - a `Label`'s token is its
 * *text* colour and its box is text-sized, and no golden can name the synthetic `Panel` a `Row`
 * produces. Those remain per-component appearance decisions this project has not settled, not in the
 * ADRs, which stop at "where each node is and which validated token it draws with", and not in the
 * sibling, whose `render_frame` returns frame statistics rather than geometry
 * (`crates/trustsc-ui/src/lib.rs:539`). So this module still counts what it cannot decide and says
 * so in `FrameStats::deferred`.
 *
 * One consequence of the field rule is worth stating rather than discovering, because it constrains
 * #257 and #258 rather than being free. `ColorHash` admits only pixels that are a blend of the
 * node's ground and its tint at one coverage, so a reading drawn *inside* a field in some third
 * colour fails the golden its own screen was compiled with. A component whose field a golden pins
 * with `ColorHash` therefore has two ways to show a reading and no others: in the field's own tint,
 * or knocked out of it back to the ground.
 *
 * #257 met that constraint head on and neither option was available as written. An additive draw
 * list cannot knock a stroke back to the ground - there is no erase - and a stroke in the field's
 * own tint over an opaque field of that tint is invisible. What was available is the *third* reading
 * of the same rule, which the sentence above did not have to spell out because nothing had needed it
 * yet: one tint at **two coverages**. A bound trace paints its field at `boundFieldCoverage`
 * and its stroke at full tint, so every pixel is still a blend of ground and tint at one coverage -
 * what `ColorHash` asks - the stroke supplies the fully covered pixels `ColorHash` additionally
 * requires, and the field keeps `goldenBounds()` seeing the node's whole rectangle as painted, which
 * a stroke alone would not. An unbound trace is unchanged, which is why the committed screen's
 * pixels and its `verify` leg are unchanged too: both render it without signals.
 *
 * That paragraph is an argument, and arguments about what a check admits belong to the check. So it
 * is also a scenario: `verify-golden-two-coverage-composition` in `tests/verify/GoldenCheckTests.cpp`
 * paints exactly this composition and asserts that `goldenBounds()` and `colorHash()` both hold -
 * and that the field alone, without the stroke, is the `TintAbsent` the stroke exists to answer.
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
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.image.schema;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.trace;
import mdux.text.draw;
import mdux.text.schema;

export namespace mdux::medui {

/**
 * @brief The token a node paints its whole resolved rectangle with, or `nullopt` for a node that has
 *        no such rectangle to paint.
 *
 * One spelling of the field rule, so `render()` and any test that wants to name the drawn set read
 * the same function rather than two lists that agree until a component is added to one of them.
 *
 * A `Panel` is here because a `Row` declared a background and a background *is* a filled box. A
 * `NumericDisplay` and a `SignalTrace` are here because the golden entry their own compiler emits
 * pairs their whole rectangle with their single token, and `goldenBounds()` reads that pairing as an
 * equality - see the module comment. Everything else is deferred: a `Clock` has no token, a
 * `StatusIndicator` has one per state and therefore no single tint, and the rest have an appearance
 * with more than one part that nothing in this project has settled.
 *
 * **`nullopt` and an empty token are different answers**, and collapsing them would lose a refusal.
 * `nullopt` means "this component does not paint a field", which is a deferral. An engaged optional
 * holding an empty string means "this component paints a field and its token is blank", which
 * `validatePayload()` refuses at compile time and `render()` must refuse at run time - a screen built
 * by hand never met that `static_assert`, and a deferral would let it draw a frame that silently
 * omits a node instead.
 */
[[nodiscard]] constexpr std::optional<std::string_view> fieldColorToken(const NodePayload& payload) noexcept {
    if (const auto* panel = std::get_if<PanelSpec>(&payload); panel != nullptr) {
        return panel->colorToken;
    }
    if (const auto* numeric = std::get_if<NumericDisplaySpec>(&payload); numeric != nullptr) {
        return numeric->colorToken;
    }
    if (const auto* trace = std::get_if<SignalTraceSpec>(&payload); trace != nullptr) {
        return trace->colorToken;
    }
    return std::nullopt;
}

/// Why a frame was refused. Every one leaves the draw list exactly as it was found.
enum class ScreenError : std::uint8_t {
    MalformedColorToken,   ///< a node's colour is not of the form `Theme.Colors.<Token>`
    UnknownColorToken,     ///< well-formed, and the governed table does not define it
    BudgetExhausted,       ///< a write would exceed a `DrawBudget` this frame is held to
    UnknownTextKey,        ///< a bound text package carries no run for a node's `textKey`
    MalformedTextRun,      ///< a run's range leaves the sidecar, or its bytes are not whole records
    RunTooLong,            ///< a run holds more than `maxGlyphsPerRun` records
    AtlasMismatch,         ///< the text package was baked against a different font package
    SidecarMismatch,       ///< the sidecar is not the one the text package describes
    PackageNotApproved,    ///< the screen was not compiled against this locale/package/digest
    TextOverflowsNode,     ///< a run's ink is wider or taller than the node that names it
    ImageSidecarMismatch,  ///< RGBA bytes differ from the baked image package
    ImageNotApproved,      ///< the screen did not approve this image id/digest/extent
    UnknownStreamSource,   ///< a signal slot names a stream no `SignalTrace` on this screen carries
    DuplicateStream,       ///< two signal slots name the same stream
    MissingSampleRing,     ///< a signal slot carries no ring, so its trace could never draw a sample
    UnknownReadingNode,    ///< a reading slot names no `NumericDisplay` on this screen
    DuplicateReading,      ///< two reading slots name the same node
    MalformedPattern,      ///< a reading slot's rendering is empty or longer than `maxPatternLength`
    ReadingRefused,        ///< a reading could not be drawn - see `ReadingError` for which way
    ReadingOverflowsNode,  ///< a drawn reading's ink is wider or taller than the node that holds it
    MalformedTraceStyle,   ///< a slot's sample range is empty or not finite, or its stroke is not 1-3px
    MalformedSampleRing,   ///< a bound ring's oldest index or live count is not a position in it
    NonFiniteSample,       ///< a live sample is a NaN or an infinity
    TraceTooLong,          ///< a bound ring holds more than `maxSamplesPerTrace` samples
    TraceBandTooSmall,     ///< a bound trace's node is too small to hold its stroke
    ScreenNotApproved,     ///< a signal binding built for one screen was offered to another
};

// The two token failures are kept apart because the schema keeps them apart, and for its reason: a
// malformed name is a defect in whatever emitted the screen, while an absent one is a table that
// does not define it. Collapsing them would tell an integrator to look in the wrong place.

[[nodiscard]] std::string_view describe(ScreenError error) noexcept;

// `AtlasMismatch` and `SidecarMismatch` are `create()`'s, not a frame's: they are properties of the
// artifacts a caller bound together, decided once. `PackageNotApproved` can also be a frame's when
// a valid binding is offered to a different screen, and `TextOverflowsNode` remains defense in depth
// after package identity is authenticated.

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
 * So `create()` proves all of that once and is the only way to obtain a bound `TextBinding`. The
 * binding retains the approved locale/id/digest, and `render()` checks that fixed identity against
 * its target screen before recording anything; this prevents a binding approved by screen A from
 * being reused for screen B. A default-constructed binding is *unbound* and means "this caller has
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
     * Hashes the canonical package bytes, independently hashes the canonical form represented by
     * `text`, and hashes the sidecar, so approved bytes cannot be paired with different parsed
     * wording. This is linear in their combined size - once, at start-up, never in a frame - and
     * all three hashes are allocation-free.
     *
     * @param screen the compiled screen whose approval manifest authorizes the text package
     * @param font the font package the runs were positioned against
     * @param text the text package for the locale being run
     * @param packageJson the canonical package bytes from which `text` was loaded
     * @param runs the sidecar bytes `text` addresses ranges of
     */
    [[nodiscard]] static mdux::core::Result<TextBinding, ScreenError> create(const ScreenPackage&           screen,
                                                                             const mdux::font::FontPackage& font,
                                                                             const mdux::text::TextPackage& text,
                                                                             std::span<const std::byte>     packageJson,
                                                                             std::span<const std::byte>     runs) noexcept;

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

    /// Whether `screen` carries the exact locale/id/digest retained by this binding.
    [[nodiscard]] constexpr bool approvedBy(const ScreenPackage& screen) const noexcept {
        if (!bound()) {
            return true;
        }
        for (const TextPackageApproval& candidate : screen.approvedTextPackages) {
            if (candidate.locale == locale_ && candidate.packageId == packageId_ && candidate.packageSha256 == packageSha256_) {
                return true;
            }
        }
        return false;
    }

private:
    constexpr TextBinding(const mdux::font::FontPackage* font,
                          const mdux::text::TextPackage* text,
                          std::span<const std::byte>     runs,
                          mdux::evidence::Digest         packageSha256) noexcept
        : font_{font}, text_{text}, runs_{runs}, locale_{text->locale}, packageId_{text->header.id}, packageSha256_{packageSha256} {}

    const mdux::font::FontPackage* font_{nullptr};
    const mdux::text::TextPackage* text_{nullptr};
    std::span<const std::byte>     runs_{};
    std::string_view               locale_{};
    std::string_view               packageId_{};
    mdux::evidence::Digest         packageSha256_{};
};

// The guarantee `create()` is documented to give, held by the language rather than by discipline.
// A class with private data members is not an aggregate, so `TextBinding{...}` cannot brace-elide
// its way past the checks - and if a future edit makes the members public "for convenience", this
// fails here rather than silently reopening the bypass.
static_assert(!std::is_aggregate_v<TextBinding>, "a TextBinding must only be obtainable through create()");

/**
 * @brief One authenticated baked image joined to a compiled screen.
 *
 * Creation hashes the canonical package and its RGBA8 sidecar once at start-up. Rendering then
 * compares the retained package digest and intrinsic extent with the approval named by each Image
 * node before recording one full-sheet sampled rectangle. The binding retains values only: it has
 * no pointer or view into the caller's `ImagePackage`, package JSON or pixels, so those inputs may
 * be moved or released after `create()` returns. Recording the governed DrawList does not read the
 * pixels; the Vulkan renderer separately copies its atlas input into an immutable image allocation
 * during renderer creation.
 */
class ImageBinding {
public:
    constexpr ImageBinding() noexcept = default;

    [[nodiscard]] static mdux::core::Result<ImageBinding, ScreenError> create(const ScreenPackage&             screen,
                                                                              const mdux::image::ImagePackage& image,
                                                                              std::span<const std::byte>       packageJson,
                                                                              std::span<const std::byte>       pixels) noexcept;

    [[nodiscard]] constexpr bool bound() const noexcept {
        return isBound;
    }

    /// Whether one screen approval is the immutable identity retained by this binding.
    [[nodiscard]] constexpr bool matches(const ImagePackageApproval& candidate) const noexcept {
        return isBound && candidate.packageSha256 == packageSha256 && candidate.width == width && candidate.height == height;
    }

    [[nodiscard]] constexpr bool approvedBy(const ScreenPackage& screen) const noexcept {
        if (!bound())
            return true;
        return std::ranges::any_of(screen.approvedImagePackages, [this](const ImagePackageApproval& candidate) {
            return matches(candidate);
        });
    }

private:
    constexpr ImageBinding(mdux::evidence::Digest packageDigest, std::uint32_t imageWidth, std::uint32_t imageHeight) noexcept
        : isBound{true}, packageSha256{packageDigest}, width{imageWidth}, height{imageHeight} {}

    bool                   isBound{false};
    mdux::evidence::Digest packageSha256{};
    std::uint32_t          width{0};
    std::uint32_t          height{0};
};

static_assert(!std::is_aggregate_v<ImageBinding>, "an ImageBinding must only be obtainable through create()");

/**
 * @brief The coverage a live component's field is painted at once its reading is on screen.
 *
 * The field is opaque while the component is unbound - that is #255's rule and nothing here changes
 * it - and drops to this coverage of the same tint the moment a reading is drawn over it. The reason
 * is arithmetic rather than taste: a `SignalTraceSpec` and a `NumericDisplaySpec` each carry **one**
 * colour token, so content drawn over an opaque field of that token is the field, pixel for pixel,
 * and the component would have gained a feature nobody could see.
 *
 * Named for the field rather than for the trace since #258, because both live components use it. The
 * argument below was written for a waveform and holds unchanged for a reading: it is about how many
 * tints a spec carries and what an additive draw list can do with them, not about what shape the
 * content is.
 *
 * The module comment above states the constraint this has to live inside: a reading drawn in a third
 * colour fails a `ColorHash` golden, so the only tints available are the node's own and the ground
 * it sits on, and an additive draw list cannot knock a stroke back to the ground. What is left is
 * one tint at two coverages - which is exactly what `ColorHash` admits, since it asks that every
 * pixel be a blend of ground and tint at *some* single coverage, and that a fully covered pixel be
 * the tint exactly. The stroke supplies the second; the field supplies the first and keeps
 * `goldenBounds()` seeing the node's whole rectangle as painted, which a stroke alone would not.
 *
 * A quarter, then, and not a value chosen by eye: low enough that a 1px stroke at full tint is
 * unambiguous against it, and high enough to stay clear of the ground at every quantisation. The
 * number itself is this module's; nothing in the artifacts or the shared contract fixes it, and it is
 * recorded here rather than inlined so a screen's appearance has one place to be argued about.
 */
inline constexpr float boundFieldCoverage = 0.25F;

/**
 * @brief One live waveform the caller offers this screen: which stream, whose samples, at what scale.
 *
 * `streamSource` is matched against the name a `SignalTrace` node carries - `stream_source:` in the
 * source, `streamSource` in the compiled artifact - so the screen decides *where* the waveform goes
 * and the caller supplies only *what* it is.
 *
 * `ring` is a pointer rather than a value because the producer writes into it between frames: the
 * binding is made once and reads the ring's current `oldest` and `count` on every frame. Nothing is
 * owned; the ring and its storage outlive the frame, exactly as the text packages do.
 */
struct SignalSlot {
    std::string_view  streamSource{};  ///< the stream name the compiled node carries
    const SampleRing* ring{nullptr};   ///< the caller's ring, read afresh each frame
    TraceStyle        style{};         ///< the range those samples are read against, and the stroke
};

/**
 * @brief The live waveforms a screen's `SignalTrace` nodes are joined to, once the join is proved.
 *
 * `TextBinding`'s counterpart for signals, and deliberately a much cheaper object: what it proves is
 * that the caller and the screen agree about *names*, not that four artifacts hash to each other.
 * There is nothing to hash - samples are produced at run time and no committed artifact describes
 * them - so the checks are the ones that are actually available:
 *
 * - every slot names a stream some `SignalTrace` on this screen carries, because a typo in a stream
 *   name is otherwise a trace that silently stays empty, and an empty waveform on a monitor is a
 *   flat line rather than a missing feature;
 * - no two slots name the same stream, because which of them would win is arbitrary and the wrong
 *   answer is undetectable from the frame;
 * - every slot has a ring and a style the expansion can use, checked once here rather than
 *   rediscovered on the first frame of a device's life.
 *
 * A screen need **not** have a slot for every trace. A stream that has not started yet is a normal
 * state, not a broken one, and its node draws the opaque field it reserved - which is exactly what
 * this runtime did for every trace before #257.
 *
 * A default-constructed binding is *unbound* and means "this caller has no live signals": every
 * trace draws its field, which is the behaviour every existing caller already has. That is what
 * keeps `render(screen, list)` and `render(screen, list, text)` meaning what they meant.
 *
 * Nothing here is owned, and the slot span is the caller's storage, so `render()` allocates nothing
 * by construction rather than by discipline.
 */
class SignalBinding {
public:
    /// An unbound binding: no live signals, every trace drawing the field it reserves.
    constexpr SignalBinding() noexcept = default;

    /// Proves every slot names a distinct `SignalTrace` on `screen` and carries a usable ring.
    [[nodiscard]] static mdux::core::Result<SignalBinding, ScreenError> create(const ScreenPackage& screen, std::span<const SignalSlot> slots) noexcept;

    /// Whether this binding carries slots. False for a default-constructed one.
    [[nodiscard]] constexpr bool bound() const noexcept {
        return !slots_.empty();
    }

    [[nodiscard]] constexpr std::span<const SignalSlot> slots() const noexcept {
        return slots_;
    }

    /// The slot for `streamSource`, or nullptr when this caller offers none.
    ///
    /// Linear, for `ScreenPackage::find()`'s reason: a screen holds a handful of traces, and a map
    /// would cost more to build than every lookup it could serve.
    [[nodiscard]] constexpr const SignalSlot* find(std::string_view streamSource) const noexcept {
        for (const SignalSlot& slot : slots_) {
            if (slot.streamSource == streamSource) {
                return &slot;
            }
        }
        return nullptr;
    }

    /// Whether this binding was built for `screen`.
    ///
    /// The identity is the screen's id, which is what an artifact is addressed by: it names a
    /// directory under `generated/`, a CTest entry and a generated C++ identifier, so two screens in
    /// one build cannot share it. Retaining it closes the substitution path `TextBinding::approvedBy()`
    /// closes with a digest - weaker, because there is no digest to hold, and worth having anyway: a
    /// binding whose slots were validated against screen A says nothing about screen B's traces.
    [[nodiscard]] constexpr bool approvedBy(const ScreenPackage& screen) const noexcept {
        return !bound() || screen.id == screenId_;
    }

private:
    constexpr SignalBinding(std::string_view screenId, std::span<const SignalSlot> slots) noexcept : slots_{slots}, screenId_{screenId} {}

    std::span<const SignalSlot> slots_{};
    std::string_view            screenId_{};
};

// The guarantee `create()` is documented to give, held by the language rather than by discipline -
// `TextBinding`'s static_assert, for its reason. A class with private data members is not an
// aggregate, so `SignalBinding{...}` cannot brace-elide its way past the checks.
static_assert(!std::is_aggregate_v<SignalBinding>, "a SignalBinding must only be obtainable through create()");

/**
 * @brief One live reading the caller offers a `NumericDisplay`: which node, what shape, what value.
 *
 * `nodeId` rather than the node's `source:` name, and that is worth a sentence. A `SignalTrace` is
 * addressed by its stream because several nodes may legitimately show one stream; a `NumericDisplay`
 * shows one reading in one box, and its id is the name a golden reference, a requirement trace and
 * the layout solver already address it by. Using the same handle means a caller wiring readings and
 * a reviewer reading `goldens.json` are naming the same thing.
 *
 * ## Why the pattern travels here rather than in the artifact
 *
 * The compiled node carries `templateId` - a validated *name*, as every other field is - and not the
 * shape that name stands for. Resolving it into the artifact was the alternative, and it was not
 * taken: it would put a product's rendering table inside a byte-compared screen package and, through
 * that, inside the shared contract's compiled-screen semantics, which is an upstream change this
 * needs no part of.
 *
 * So the shape arrives with the value, from the host that owns the table - and the obvious risk is
 * equally plain: the compiler certified the node against table A and the device could draw table B.
 * That is the same class of exposure `TextBinding` closes with a digest, and there is no digest to
 * hold here. What closes it instead is the same thing that closes it for a `Label` whose bound
 * package the runtime cannot authenticate: **the drawn extent is measured again, against the node,
 * and a reading that does not fit refuses the frame as `ReadingOverflowsNode`.** The build-time
 * check remains the one that reports a useful diagnostic to the person who can fix it; this one
 * exists so that a drifted table can never put digits over a neighbour.
 *
 * `value` is in the template's own fixed-point units - `1234` under `###.#` is `123.4` - so no float
 * is formatted on device. That is not only about allocation: a decimal conversion has rounding this
 * project would then have to pin across toolchains, and the host that owns the units has already
 * made that decision once.
 */
struct ReadingSlot {
    std::string_view nodeId{};     ///< the `NumericDisplay` node this reading is for
    std::string_view rendering{};  ///< what its `templateId` stands for, e.g. `###.# mmHg`
    std::int64_t     value{0};     ///< the reading, in the template's own fixed-point units
};

/**
 * @brief The live readings a screen's `NumericDisplay` and `Clock` nodes are joined to.
 *
 * `TextBinding`'s counterpart for readings, and `SignalBinding`'s sibling. What it proves is that
 * the caller and the screen agree about *nodes* and that each pattern is one the runtime will draw:
 *
 * - every slot names a `NumericDisplay` on this screen, because a mistyped node id is otherwise a
 *   reading that silently never appears while its field goes on being painted;
 * - no two slots name the same node, because which would win is arbitrary and undetectable;
 * - every pattern is non-empty and within `maxPatternLength`, checked once here rather than
 *   rediscovered on the first frame of a device's life.
 *
 * A screen need **not** have a slot for every `NumericDisplay`, and need not carry a time. A reading
 * that has not arrived is a normal state, and its node draws the opaque field it reserved - which is
 * exactly what this runtime did for every such node before #258.
 *
 * ## The clock's two arguments
 *
 * `now` is one time for the whole binding rather than one per node: a screen showing two clocks
 * showing different times is not a screen anybody means to build, and offering the option would make
 * that mistake expressible.
 *
 * `clockColorToken` is the awkward one, and it is stated rather than buried. The shared component
 * model gives `Clock` no `color:` field - `id`, `width`, `height`, `format`, and an optional
 * `position:` - so unlike every other drawn component, the artifact names no tint for it. Something
 * must, and the host is the only other party. It is a *token*, resolved through the governed table
 * exactly as a screen's own would be, so what a host can choose is which approved colour rather than
 * any colour. And it contradicts no artifact: `collectGoldens()` already refuses `ColorHash` for a
 * node with no single declared token, so no golden can ever have pinned a clock's tint.
 *
 * A default-constructed binding is *unbound* and means "this caller has no readings": every such
 * node draws its field, and a `Clock` stays deferred, which is what every existing caller has.
 *
 * ## The font is the text binding's, and that is not an accident of wiring
 *
 * A reading needs glyph metrics and nothing else the text pipeline provides - no translations, no
 * keys, no sidecar - so carrying a `const FontPackage*` here looked like the independent design. It
 * is not the one the schema already chose. `needsTextPackageApproval()` puts `NumericDisplay` and
 * `Clock` among the components whose screen **must** list an approved text package, and
 * `ScreenPackage::validate()` refuses one that does not: a screen carrying either of them and no
 * `Label` is not a screen this project admits. So a `TextBinding` is always obtainable for a screen
 * that has a reading to draw, and taking the font from it means the metrics come from a package
 * whose identity `TextBinding::create()` authenticated against the screen's own approval manifest -
 * rather than from a second font this type would have nothing to check.
 *
 * The consequence a caller meets is therefore real and small: drawing a number requires binding a
 * locale. That is a start-up cost, paid once, on a screen the schema already required to declare
 * one.
 *
 * Nothing here is owned, and the slot span is the caller's storage, so `render()` allocates nothing
 * by construction rather than by discipline.
 */
class ReadingBinding {
public:
    /// An unbound binding: no readings, no time, every such node as it was before #258.
    constexpr ReadingBinding() noexcept = default;

    /// Proves every slot names a distinct `NumericDisplay` on `screen` and carries a usable pattern.
    ///
    /// `now` may be null, which means this caller has no clock and every `Clock` stays deferred.
    /// When it is not null, `clockColorToken` must resolve through the governed colour table.
    [[nodiscard]] static mdux::core::Result<ReadingBinding, ScreenError>
    create(const ScreenPackage& screen, std::span<const ReadingSlot> readings, const CivilTime* now = nullptr, std::string_view clockColorToken = {}) noexcept;

    /// Whether this binding carries anything. False for a default-constructed one.
    [[nodiscard]] constexpr bool bound() const noexcept {
        return !readings_.empty() || now_ != nullptr;
    }

    [[nodiscard]] constexpr std::span<const ReadingSlot> readings() const noexcept {
        return readings_;
    }

    /// The time every `Clock` on this screen shows, or nullptr when this caller has none.
    [[nodiscard]] constexpr const CivilTime* now() const noexcept {
        return now_;
    }

    [[nodiscard]] constexpr std::string_view clockColorToken() const noexcept {
        return clockColorToken_;
    }

    /// The reading for `nodeId`, or nullptr when this caller offers none.
    ///
    /// Linear, for `ScreenPackage::find()`'s reason: a screen holds a handful of readings, and a map
    /// would cost more to build than every lookup it could serve.
    [[nodiscard]] constexpr const ReadingSlot* find(std::string_view nodeId) const noexcept {
        for (const ReadingSlot& slot : readings_) {
            if (slot.nodeId == nodeId) {
                return &slot;
            }
        }
        return nullptr;
    }

    /// Whether this binding was built for `screen`. `SignalBinding::approvedBy()`'s rule, verbatim.
    [[nodiscard]] constexpr bool approvedBy(const ScreenPackage& screen) const noexcept {
        return !bound() || screen.id == screenId_;
    }

private:
    constexpr ReadingBinding(std::string_view screenId, std::span<const ReadingSlot> readings, const CivilTime* now, std::string_view clockColorToken) noexcept
        : readings_{readings}, now_{now}, screenId_{screenId}, clockColorToken_{clockColorToken} {}

    std::span<const ReadingSlot> readings_{};
    const CivilTime*             now_{nullptr};
    std::string_view             screenId_{};
    std::string_view             clockColorToken_{};
};

// The guarantee `create()` is documented to give, held by the language rather than by discipline -
// `TextBinding`'s static_assert, for its reason.
static_assert(!std::is_aggregate_v<ReadingBinding>, "a ReadingBinding must only be obtainable through create()");

/**
 * @brief What one frame did, and what it left undone.
 *
 * `deferred` is the honest half: it counts nodes this runtime visited and could not paint at all,
 * for the reasons the module comment gives one by one. A caller that expects a screen to be fully
 * drawn can assert it is zero; today, on a screen carrying an `Image`, a `VulkanViewport`, a
 * `StatusIndicator` or any of the three interactive components, it will not be - and on one carrying
 * text or an unbound `Clock` it will not be either unless the matching binding was supplied.
 *
 * A `NumericDisplay` or `SignalTrace` is **not** counted here since #255: its field is drawn even
 * with nothing bound. That distinction is the point of the counter - a node whose rectangle is
 * painted is a node a golden reference can check, whatever it will later show in it. A `Clock` has
 * no such rectangle, so an unbound one is a deferral in the ordinary sense.
 *
 * `readings` and `traces` count the nodes whose *live* content was drawn, which is the fact
 * `deferred` cannot carry: a bound and an unbound `NumericDisplay` are both undeferred, and only
 * one of them is showing a number.
 */
struct FrameStats {
    std::uint32_t nodes{0};     ///< nodes visited
    std::uint32_t rects{0};     ///< rectangles recorded
    std::uint32_t deferred{0};  ///< nodes visited and left undrawn
    std::uint32_t steps{0};     ///< units of per-node work, for the bounded-work tests
    std::uint32_t traces{0};    ///< `SignalTrace` nodes whose samples were expanded
    std::uint32_t readings{0};  ///< `NumericDisplay` and `Clock` nodes whose value was drawn

    [[nodiscard]] constexpr bool operator==(const FrameStats&) const noexcept = default;
};

/**
 * @brief Records one frame of `screen` into `list`.
 *
 * @param screen  a compiled screen, normally the `constexpr` one a generated translation unit holds
 * @param list    a draw list the caller created over storage sized from `screen.budget`
 * @param text    the packages this screen's text keys resolve against; default means "no text", and
 *                every text node is then deferred rather than refused
 * @param image   the authenticated baked image this screen's `Image` nodes resolve against;
 *                default means "no image", and every `Image` node is then deferred
 * @param signals the live rings this screen's stream sources resolve against; default means "no
 *                signals", and every trace then draws the opaque field it reserves
 * @param readings the live values this screen's `NumericDisplay` and `Clock` nodes show; default
 *                means "no readings", and each such node is then what it was before #258
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
[[nodiscard]] mdux::core::Result<FrameStats, ScreenError> render(const ScreenPackage&  screen,
                                                                 mdux::draw::DrawList& list,
                                                                 const TextBinding&    text     = {},
                                                                 const ImageBinding&   image    = {},
                                                                 const SignalBinding&  signals  = {},
                                                                 const ReadingBinding& readings = {}) noexcept;

}  // namespace mdux::medui
