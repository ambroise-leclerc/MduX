/**
 * @file Verify.cppm
 * @brief Rendered-truth checks: pure functions over a CPU framebuffer and an artifact-derived
 *        expectation, answering whether a frame shows what the compiled screen said it would.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-015 Versioned sibling observations (decision 2: name the predicate, not its alias)
 * @compliance ADR-016 Locally versioned observation profiles for rendered checks
 *
 * Part of MduXCore, and that placement is ADR-014 decision 1 rather than an inheritance: the checks
 * have the same shape as the screen runtime - bounded arithmetic over caller-owned storage, no
 * allocation, no throw, no file - and putting them in the governed zone is what turns "the verifier
 * does no work a device could not do" from a convention into a lint result. `mdux-governed-lint`
 * and `governed.noThrow.symbolScan` cover this module with no second registration.
 *
 * **The driver is not here.** Rendering a frame, reading `goldens.json`, enumerating obligations and
 * writing `verification.json` are what ADR-004 and ADR-005 keep out of this zone; they are #253's
 * and #254's, and they call in. What this module owns is the questions themselves.
 *
 * ## The checks, and which are opt-in
 *
 * - **`goldenBounds()`** - the golden node's rendered content occupies the declared rectangle;
 * - **`colorHash()`** - that content carries the tint its colour token resolves to;
 * - **`inkContainment()`** - a compiled text node's run stays inside its node bounds;
 * - **`localizedTextPresence()`** - the approved locale's bound run is the one on screen;
 * - **`rawImageDigest()`** - a region's tightly packed RGBA8 hashes to a committed baseline.
 *
 * The first two are `Bounds` and `ColorHash`, the closed `CvCheck` set an author opts into per node.
 * That set belongs to the shared language's `safety` capability, pinned in `medui-conformance.toml`,
 * and `MEDUI-E071` rejects a name outside it - so widening it is an upstream change and a re-pin,
 * never an edit here. `CvCheck` is *defined* in this module and aliased by
 * `mdux.tools.medui.goldens` rather than declared twice, for the reason ADR-012 decision 4 gives
 * about the golden predicate: two implementations of one closed set agree until the day they matter.
 *
 * `inkContainment()` and `localizedTextPresence()` are **not** opt-in and are deliberately not
 * `CvCheck` enumerators. They apply to every compiled node whose spec carries a `textKey`, in every
 * approved locale, whether or not that node has a golden entry. A glyph run leaving the box that was
 * budgeted for it is a defect however the author annotated the node, and #195 already measured that
 * box at compile time - these are the same claim, verified against pixels.
 *
 * `rawImageDigest()` is neither opt-in nor mandatory: it has **no committed baseline and no
 * production caller**. It exists so that the observation TrustSC's `ColorHash` actually performs -
 * a raw-pixel digest - has a name of its own here (ADR-015 decision 2), and so #314 can compare the
 * two implementations without conflating a tint predicate with a pixel hash. ADR-014 decision 4
 * keeps a driver-tuple-dependent digest out of the byte-compared bundle; ADR-016 records why that
 * makes this a fixture-and-future-consumer tool rather than a gate, and why `Finding::NoBaseline`
 * (its answer for every production call) is distinct from a pass.
 *
 * ## Observation profiles
 *
 * ADR-015 decision 2: a shared check *name* is not evidence of a shared *observation*. Every
 * `CheckOutcome` therefore carries an `ObservationProfile` - an id naming what was measured and a
 * version that moves only on a reviewed change - and `verification.json` records it beside each
 * outcome. `profileOf()` maps a `CvCheck` or `TextCheck` to its profile; `rawImageDigest()` carries
 * `rawImageDigestProfile`. The ids are implementation-local (`localProfilePrefix`) until
 * MEDUI-DEC-007 delivers a canonical set (ADR-016).
 *
 * ## What a check is given, and what it may never be given
 *
 * A check takes a `FramebufferView` - bytes, extent, row stride, format - and one of two expectation
 * views. Both views are read-only, caller-owned, and obtainable only through a factory that resolves
 * them against a compiled screen; neither carries a Vulkan type, allocates, throws or opens a file.
 * The GPU's involvement ended at the readback that already exists in `mdux.render.offscreen`.
 *
 * ADR-014 decision 2 is the rule the `create()` functions enforce: **every expectation is derived
 * from a committed artifact, and none is supplied by the caller.** A golden expectation is built
 * from a `goldens.json` entry *and* the compiled node it names, and it fails closed when the id does
 * not resolve or when the entry's duplicated bounds, text key or colour token disagree with that
 * node. A text expectation is built from the screen, the node and a `mdux::medui::TextBinding` -
 * the one type in this repository that has already proved the screen's manifest approves this
 * locale, package id and canonical digest, that the text package was baked against this font, and
 * that every run's range hashes to what the package recorded. It looks the run up by the node's own
 * `textKey`; the caller supplies no locale, no bytes and no font.
 *
 * The one admitted exception is `TextExpectation::createSynthetic()`, which ADR-014 decision 1
 * names and bounds in the same sentence: a unit test may construct a synthetic view to exercise a
 * pure check, and production expectations have exactly the artifact sources decision 2 gives them.
 * It establishes no provenance and says so; a driver reaching for it is a review finding on #253.
 *
 * The verifier **never re-applies ADR-011's golden predicate**. `collectGoldens()` is its single
 * implementation, it runs in the baker while the AST still carries the predicate's inputs, and
 * `evidence.screen.<id>` re-derives `goldens.json` and byte-compares it on all four automatic CI
 * legs. A golden missing for a node the predicate selects fails four byte comparisons before it can
 * reach this module. What this module owns is the other direction: a golden naming a node that does
 * not exist, or naming one whose rectangle it does not agree with.
 *
 * ## Two resolved colours, and why one is not enough
 *
 * Both expectation views carry a **tint** and a **ground**, and both are resolved through the same
 * governed table (`mdux::medui::resolveColorToken()`) rather than measured from any frame.
 *
 * The tint is what a check compares *against*: it is the colour the node's token names. The ground
 * is what the frame would show at that node if the node painted nothing - the surface the driver
 * cleared to, or the tint of the compiled panel underneath. Deciding whether a pixel was painted at
 * all needs that second value, and no amount of knowing the tint supplies it: a node drawn in
 * `Theme.Colors.Nominal` over a cleared surface and the same node not drawn at all differ only in
 * what the untouched pixels are. The ground is a property of the screen and of the driver's own
 * fixed clear colour, so it is still derived rather than argued - what it must never be is a value
 * read back out of the frame under test, which would make every check compare a measurement against
 * itself.
 *
 * With both in hand the two colour claims are exact, and stay exact without a tolerance:
 *
 * - a pixel the run or the node painted is an alpha blend of the tint over the ground at **one**
 *   coverage, so every channel has to agree on which coverage that was. Channel-wise membership of
 *   the interval between ground and tint is necessary and is *not* sufficient: with a black ground
 *   and `Theme.Colors.ScoreDigits`, the pixel `(33, 0, 107)` sits inside every channel's interval
 *   while demanding full coverage of red and blue and none of green, which no blend can produce.
 *   `colorHash()` therefore intersects the coverage interval each channel implies, in integer
 *   cross-products, and reports `Finding::ForeignColour` when that intersection is empty.
 * - a **fully covered** pixel is exactly the tint, because coverage modulates alpha and never rgb.
 *   So "carries its tint" is an equality rather than a proximity, which is the same discipline
 *   `tests/render/PixelTests.cpp` applies to a whole frame: a one-channel difference is the smallest
 *   wrong answer a renderer can give, and the one a tolerance would wave through.
 *
 * The cost of that second rule is stated rather than discovered: content that never reaches full
 * coverage anywhere - a hairline trace, or a glyph run rasterised too small for any pixel to be
 * solid - cannot be said to carry its tint, and `colorHash()` reports `TintAbsent` rather than
 * passing on a blend. That is fail-closed in the direction this epic wants.
 *
 * ## What each check can see, and what it cannot
 *
 * Worth stating here rather than leaving a reader to infer it from the implementation, because the
 * limits are structural rather than temporary.
 *
 * `goldenBounds()` asks whether the content inside the declared rectangle *is* that rectangle: the
 * bounding box of everything painted there has to be the box the compiler resolved, edge for edge.
 * ADR-014's worked example is what fixes that reading rather than the weaker "there is something in
 * there" - "a unit test paints a rectangle into one and asserts that `GoldenBounds` passes, then
 * moves it by a pixel and asserts that it fails", and only an equality fails on a rectangle that
 * moved by one pixel while still overlapping its own box.
 *
 * Two consequences, both stated because a reader would otherwise meet them as surprises. Content
 * that deliberately fills part of its node - digits inset in a `NumericDisplay`'s box - does not
 * discharge `Bounds` against that node's rectangle, because the rectangle the golden pins is the
 * node's; a component whose painted extent is smaller than its box needs a golden that says so, and
 * that is a question for whoever gives such a component its geometry. And overflow *into a
 * neighbour* is not attributable to this node at all: the committed screen packs
 * `insufflation-pressure` and `ecg-lead-ii` edge to edge, so a band scanned outside the rectangle
 * would report the neighbour's pixels as this node's spill. Attribution from a framebuffer alone is
 * not available, and a check that guessed at it would fail on a correct screen - which trains a team
 * to ignore it.
 *
 * `inkContainment()` compares the ink the committed run and font metrics *predict*, placed by the
 * rule `mdux.medui.screen` fixes, against the ink the frame actually shows inside the node. A run
 * whose predicted box leaves the node fails on the prediction; a frame that clipped, moved or lost
 * the run fails on the comparison.
 *
 * `localizedTextPresence()` is the one that distinguishes one locale from another, and it does so
 * against the **baked coverage** rather than against the glyph boxes. Every pixel inside a placed
 * glyph must be what that texel's coverage produces for this node's tint over its ground, and every
 * pixel of the node outside every placed glyph must still be the ground. Metrics alone would only
 * support "something is in the right rectangles", which a sparse handful of pixels satisfies and
 * which two different letters of the same size satisfy equally - so the view carries the sheet, and
 * the check compares the shape the approved run actually has. Its limit is the mirror of
 * `goldenBounds()`': a second node overlapping this one would show up as ink where this locale's run
 * paints none.
 *
 * ## No allocation, no throw, and a `create()` that cannot be brace-elided
 *
 * Every check is a bounded walk over the caller's pixels: the loops are over the node's rectangle
 * and the run's records, both fixed before the check runs, and nothing is accumulated into storage
 * this module owns. `FramebufferView`, `GoldenExpectation` and `TextExpectation` are classes with
 * private members rather than aggregates, so `GoldenExpectation{...}` cannot brace-elide its way
 * past the checks `create()` performs - the same guarantee, held the same way, as
 * `mdux::medui::TextBinding`.
 */
module;

export module mdux.verify;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.draw;
import mdux.text.schema;

export namespace mdux::verify {

/**
 * @brief Why an expectation could not be built, or a framebuffer could not be read.
 *
 * Distinct from `Finding`, and the split carries the distinction ADR-014 decision 3 requires CI to
 * be able to make: a `VerifyError` says the check could not be *made*, a `Finding` says it was made
 * and did not hold. A driver that collapsed the two would report a dangling golden id as a rendering
 * defect, or worse, as a pass.
 */
enum class VerifyError : std::uint8_t {
    EmptyFramebuffer,           ///< zero width or height; there is nothing to look at
    UnsupportedFormat,          ///< the format is not one this module can decode
    RowStrideTooSmall,          ///< a row cannot hold `width` pixels of the declared format
    FramebufferTooSmall,        ///< the byte span is shorter than the declared extent needs
    DanglingGoldenId,           ///< the golden names a node the compiled screen does not have
    GoldenBoundsDisagree,       ///< the golden's rectangle is not the named node's rectangle
    GoldenTextKeyDisagrees,     ///< the golden's text key is not the named node's
    GoldenColorTokenDisagrees,  ///< the golden's colour token is not the named node's
    NoChecksDeclared,           ///< the entry opts into nothing, so it can discharge nothing
    ChecksNotCanonical,         ///< `cvChecks` is not sorted and deduplicated as the sidecar is
    ColorHashWithoutTint,       ///< a `ColorHash` entry names no colour token to compare against
    UnresolvedColorToken,       ///< the governed table does not define the named tint
    NodeCarriesNoTextKey,       ///< a text obligation was raised over a node that draws no text
    ScopeIsLocaleFree,          ///< a text obligation must name the approved locale it runs in
    MalformedRun,               ///< the run's bytes are not a whole number of records
    EmptyRun,                   ///< the approved locale's run paints nothing for this node
    RunTooLong,                 ///< more records than `mdux::medui::maxGlyphsPerRun` admits
    GlyphIndexOutOfRange,       ///< a record names a glyph the font package does not hold
    TextBindingUnbound,         ///< the caller offered a binding carrying no packages
    BindingNotApproved,         ///< the screen's manifest does not approve the bound package
    NodeNotInScreen,            ///< the node is not one of this screen's, so nothing authorises it
    ScopeIsNotTheBoundLocale,   ///< the render scope names a locale other than the bound package's
    UnknownTextKey,             ///< the bound package carries no run for this node's text key
    AtlasSizeMismatch,          ///< the coverage sheet is not the size the font package declares
    GlyphOutsideAtlas,          ///< a glyph's slot leaves the coverage sheet
    DigestRoiDegenerate,        ///< a raw-image-digest ROI has a non-positive width or height
};

[[nodiscard]] std::string_view describe(VerifyError error) noexcept;

/**
 * @brief The pixel formats a framebuffer may be read in.
 *
 * One entry, and it is the one `mdux.render.offscreen` reads back: `VK_FORMAT_R8G8B8A8_UNORM` maps
 * onto `mdux::core::ColorRgba8` with no conversion and no channel swizzle. Named as an enumeration
 * anyway, so that a second format arrives as a new enumerator and a refused `create()` rather than
 * as a silently misread buffer.
 */
enum class PixelFormat : std::uint8_t {
    Rgba8Unorm,
};

/// Bytes one pixel of `format` occupies.
[[nodiscard]] constexpr std::size_t bytesPerPixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::Rgba8Unorm:
            return 4;
    }
    // Named rather than defaulted, so a new enumerator is a warning at this switch. Zero for an
    // out-of-range cast, which `FramebufferView::create()` then refuses rather than dividing by.
    return 0;
}

/**
 * @brief A read-only CPU framebuffer: somebody else's pixels, described well enough to index.
 *
 * The whole of the GPU's contribution to verification, and deliberately the last of it. A caller
 * hands over the span `OffscreenTarget::renderAndRead()` returned - or, in a unit test, an array it
 * painted by hand - and every check from here on is arithmetic.
 *
 * Row stride is separate from width because a readback is not obliged to be tightly packed, and a
 * check that assumed it was would read the next row's first pixels as this row's last ones: a
 * plausible wrong answer rather than a failure.
 */
class FramebufferView {
public:
    /**
     * @brief Describes `bytes` as a `width` x `height` image, or refuses.
     *
     * Refuses an empty extent, a stride too small to hold a row, a span too small to hold the image
     * and a format outside the enumeration. Every `pixelAt()` on the result is therefore in range by
     * construction rather than by the caller's discipline.
     *
     * The last row is allowed to be unpadded: the requirement is `(height - 1) * rowStride` plus one
     * row of pixels, computed in 64-bit arithmetic so a hostile extent cannot wrap into admitting
     * the buffer it should refuse.
     */
    [[nodiscard]] static mdux::core::Result<FramebufferView, VerifyError>
    create(std::span<const std::byte> bytes, mdux::core::Px width, mdux::core::Px height, std::size_t rowStride, PixelFormat format) noexcept;

    /// Describes `pixels` as a tightly packed `width` x `height` `Rgba8Unorm` image.
    ///
    /// The shape `OffscreenTarget::renderAndRead()` actually returns, so a driver does not restate
    /// the stride and the format at every call site - and so a test painting an expectation does not
    /// either.
    [[nodiscard]] static mdux::core::Result<FramebufferView, VerifyError>
    createPacked(std::span<const mdux::core::ColorRgba8> pixels, mdux::core::Px width, mdux::core::Px height) noexcept;

    [[nodiscard]] constexpr mdux::core::Px width() const noexcept {
        return width_;
    }
    [[nodiscard]] constexpr mdux::core::Px height() const noexcept {
        return height_;
    }
    [[nodiscard]] constexpr std::size_t rowStride() const noexcept {
        return rowStride_;
    }
    [[nodiscard]] constexpr PixelFormat format() const noexcept {
        return format_;
    }

    /// Whether `rect` lies wholly inside this image. Non-positive extents are not inside anything.
    [[nodiscard]] constexpr bool contains(mdux::medui::NodeRect rect) const noexcept {
        if (rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0) {
            return false;
        }
        const auto right  = static_cast<std::int64_t>(rect.x) + rect.width;
        const auto bottom = static_cast<std::int64_t>(rect.y) + rect.height;
        return right <= static_cast<std::int64_t>(width_) && bottom <= static_cast<std::int64_t>(height_);
    }

    /// The pixel at (x, y), or nothing when it is outside the image.
    ///
    /// An optional rather than an index, for `OffscreenTarget::pixelAt()`'s reason: an out-of-range
    /// read is a mistake in an expectation, and it should say so rather than compare whatever was
    /// next in memory.
    [[nodiscard]] std::optional<mdux::core::ColorRgba8> pixelAt(mdux::core::Px x, mdux::core::Px y) const noexcept;

private:
    constexpr FramebufferView(std::span<const std::byte> bytes, mdux::core::Px width, mdux::core::Px height, std::size_t rowStride, PixelFormat format) noexcept
        : bytes_{bytes}, width_{width}, height_{height}, rowStride_{rowStride}, format_{format} {}

    std::span<const std::byte> bytes_{};
    mdux::core::Px             width_{0};
    mdux::core::Px             height_{0};
    std::size_t                rowStride_{0};
    PixelFormat                format_{PixelFormat::Rgba8Unorm};
};

static_assert(!std::is_aggregate_v<FramebufferView>, "a FramebufferView must only be obtainable through create()");

/**
 * @brief The identity of one rendered-check observation: what it measures, at which revision.
 *
 * [ADR-015](../../../docs/adr/ADR-015-versioned-sibling-observations.md) decision 2 is the reason
 * this type exists: `ColorHash` is one name for two different observations - a tint-composition
 * predicate here, a raw-pixel digest in TrustSC - and a shared spelling is not evidence of a shared
 * measurement. Each predicate in this module therefore carries a profile whose `id` names *what* it
 * observes and whose `version` moves only when a reviewed migration changes the answer it can give.
 * `verification.json` records the profile beside every outcome, so a reader (and #314's
 * cross-implementation gate) compares like with like.
 *
 * The ids are **implementation-local** - see [ADR-016](../../../docs/adr/ADR-016-locally-versioned-observation-profiles.md).
 * MEDUI-DEC-007 accepts the *direction* of shared rendered-check profiles but delivers no canonical
 * identifiers, schema or corpus, so every id here begins with `localProfilePrefix` and no manifest
 * or pin claims a shared profile. A later reviewed migration maps these onto the canonical set.
 *
 * A value type with a string view and an integer: `constexpr`, no allocation, trivially comparable.
 * The `id` is a pointer into a string literal that outlives every use.
 */
class ObservationProfile {
public:
    constexpr ObservationProfile() noexcept = default;
    constexpr ObservationProfile(std::string_view id, std::uint32_t version) noexcept : id_{id}, version_{version} {}

    [[nodiscard]] constexpr std::string_view id() const noexcept {
        return id_;
    }
    [[nodiscard]] constexpr std::uint32_t version() const noexcept {
        return version_;
    }
    /// Whether this names a profile at all. False for a default-constructed one.
    [[nodiscard]] constexpr bool valid() const noexcept {
        return !id_.empty();
    }

    [[nodiscard]] constexpr bool operator==(const ObservationProfile&) const noexcept = default;

private:
    std::string_view id_{};
    std::uint32_t    version_{0};
};

/// The prefix every profile id in this module carries until MEDUI-DEC-007 delivers canonical ids
/// (ADR-016). A reader seeing it knows the id is MduX's own, not a shared-contract identifier.
inline constexpr std::string_view localProfilePrefix = "mdux.local/";

/// `goldenBounds()`: the golden node's content occupies exactly its declared rectangle.
inline constexpr ObservationProfile extentEqualityProfile{"mdux.local/extent-equality", 1};
/// `colorHash()`: painted pixels are a blend of ground and tint at one coverage, and one is the tint.
inline constexpr ObservationProfile tintCompositionProfile{"mdux.local/tint-composition", 1};
/// `inkContainment()`: a text run's placed ink stays inside the node that names it.
inline constexpr ObservationProfile inkContainmentProfile{"mdux.local/ink-containment", 1};
/// `localizedTextPresence()`: the approved locale's run is the shape on screen, per baked coverage.
inline constexpr ObservationProfile inkCoverageProfile{"mdux.local/ink-coverage", 1};
/// `rawImageDigest()`: SHA-256 over tightly packed row-major RGBA8 of a rectangle. Uncommitted;
/// see ADR-016 for why it exists without a committed baseline.
inline constexpr ObservationProfile rawImageDigestProfile{"mdux.local/raw-image-digest", 1};

/**
 * @brief A verification the shared language lets an author opt a node into.
 *
 * The closed set, defined here and aliased by `mdux.tools.medui.goldens` so that the compiler that
 * writes `cvChecks` and the verifier that reads them cannot drift apart. Order is the serialisation
 * order: a sidecar's `cvChecks` is sorted by this enumeration so that one screen has one canonical
 * form.
 *
 * Widening this is an upstream change to the shared `safety` capability followed by a re-pin of
 * `medui-conformance.toml`, never an edit here. In particular the two mandatory text checks are not
 * members: they are not opt-in, and adding them to make the four checks symmetrical would put a
 * local invention in a contract-owned set.
 */
enum class CvCheck : std::uint8_t {
    Bounds,     ///< the node's content occupies the rectangle the compiler resolved
    ColorHash,  ///< the node's content carries the tint its colour token resolves to
};

/// The spelling an author writes and the serialiser emits, e.g. `Bounds`.
[[nodiscard]] constexpr std::string_view spell(CvCheck check) noexcept {
    switch (check) {
        case CvCheck::Bounds:
            return "Bounds";
        case CvCheck::ColorHash:
            return "ColorHash";
    }
    return {};
}

/// The check `name` spells, or nothing when the name is not one of the closed set.
///
/// Nothing rather than a default is the whole point: ADR-014 decision 3 requires a driver to fail on
/// an entry naming a check it does not know, because such an entry did not come from a compiler this
/// repository builds.
[[nodiscard]] constexpr std::optional<CvCheck> parseCvCheck(std::string_view name) noexcept {
    if (name == spell(CvCheck::Bounds)) {
        return CvCheck::Bounds;
    }
    if (name == spell(CvCheck::ColorHash)) {
        return CvCheck::ColorHash;
    }
    return std::nullopt;
}

/**
 * @brief A verification every compiled node carrying a `textKey` receives, in every approved locale.
 *
 * Separate from `CvCheck` because these are separate things: an author selects a `CvCheck`, and
 * nobody selects one of these. Keeping the two types apart is what stops the mandatory pair from
 * being written into a sidecar, and stops a golden from being required before a text node can be
 * checked at all.
 */
enum class TextCheck : std::uint8_t {
    InkContainment,         ///< the run's ink stays inside the node that names it
    LocalizedTextPresence,  ///< the approved locale's bound run is the one on screen
};

/// The spelling a report uses, e.g. `InkContainment`.
[[nodiscard]] constexpr std::string_view spell(TextCheck check) noexcept {
    switch (check) {
        case TextCheck::InkContainment:
            return "InkContainment";
        case TextCheck::LocalizedTextPresence:
            return "LocalizedTextPresence";
    }
    return {};
}

/// The mandatory text check `name` spells, or nothing when the name is not one of the two.
///
/// The counterpart of `parseCvCheck` for the checks that have no `CvCheck` enumerator, so a reader
/// of `verification.json` can resolve any recorded check name back to its kind and its profile.
[[nodiscard]] constexpr std::optional<TextCheck> parseTextCheck(std::string_view name) noexcept {
    if (name == spell(TextCheck::InkContainment)) {
        return TextCheck::InkContainment;
    }
    if (name == spell(TextCheck::LocalizedTextPresence)) {
        return TextCheck::LocalizedTextPresence;
    }
    return std::nullopt;
}

/// The observation profile the golden check `check` reports under (ADR-015 D2 / ADR-016). A pure
/// mapping: `Bounds` observes extent equality, `ColorHash` observes tint composition. The verifier
/// and any reader of `verification.json` resolve a check's profile the same way.
[[nodiscard]] constexpr ObservationProfile profileOf(CvCheck check) noexcept {
    switch (check) {
        case CvCheck::Bounds:
            return extentEqualityProfile;
        case CvCheck::ColorHash:
            return tintCompositionProfile;
    }
    return {};
}

/// The observation profile the mandatory text check `check` reports under (ADR-015 D2 / ADR-016).
[[nodiscard]] constexpr ObservationProfile profileOf(TextCheck check) noexcept {
    switch (check) {
        case TextCheck::InkContainment:
            return inkContainmentProfile;
        case TextCheck::LocalizedTextPresence:
            return inkCoverageProfile;
    }
    return {};
}

/// The observation profile a check *spelled* `name` must report under, or nothing when `name` is
/// not a check this module defines.
///
/// One resolver for every consumer, so the driver that sets a profile and the artifact writer that
/// checks one before committing it cannot disagree about which profile a check name implies. A name
/// outside the closed set - anything but the four `spell()` strings and `RawImageDigest` - returns
/// nothing, which the writer treats as a reason to refuse rather than to serialise an
/// unidentifiable observation.
[[nodiscard]] constexpr std::optional<ObservationProfile> profileForCheckName(std::string_view name) noexcept {
    if (const auto cv = parseCvCheck(name); cv.has_value()) {
        return profileOf(*cv);
    }
    if (const auto text = parseTextCheck(name); text.has_value()) {
        return profileOf(*text);
    }
    if (name == "RawImageDigest") {
        return rawImageDigestProfile;
    }
    return std::nullopt;
}

/// How a locale-free render scope names itself in a report.
inline constexpr std::string_view localeFreeScopeName = "(locale-free)";

/**
 * @brief One render scope: an approved locale, or the single locale-free scope a textless screen has.
 *
 * A type rather than a `std::string_view` that happens to be empty, because ADR-014 decision 3 makes
 * the locale-free scope *explicit* and load-bearing: a textless screen's `Bounds` and `ColorHash`
 * obligations would otherwise vanish in a Cartesian product with an empty locale set, and a run that
 * verified nothing would report success. Spelling it as a named constructor is what makes "this
 * screen has one scope, deliberately" something a reader sees rather than infers from an empty
 * string.
 */
class RenderScope {
public:
    /// The one scope a screen carrying no text is rendered in.
    [[nodiscard]] static constexpr RenderScope localeFree() noexcept {
        return RenderScope{};
    }

    /// The scope for one approved locale, named by its BCP 47 tag.
    ///
    /// An empty tag names no locale, so it collapses to the locale-free scope rather than to a
    /// locale called "". `TextExpectation::create()` then refuses it, which is where a caller that
    /// lost a locale tag finds out.
    [[nodiscard]] static constexpr RenderScope forLocale(std::string_view tag) noexcept {
        return RenderScope{tag};
    }

    [[nodiscard]] constexpr bool isLocaleFree() const noexcept {
        return tag_.empty();
    }

    /// The locale tag, empty for the locale-free scope.
    [[nodiscard]] constexpr std::string_view tag() const noexcept {
        return tag_;
    }

    /// What a report calls this scope: the tag, or `localeFreeScopeName`.
    [[nodiscard]] constexpr std::string_view name() const noexcept {
        return tag_.empty() ? localeFreeScopeName : tag_;
    }

    [[nodiscard]] constexpr bool operator==(const RenderScope&) const noexcept = default;

private:
    constexpr RenderScope() noexcept = default;
    constexpr explicit RenderScope(std::string_view tag) noexcept : tag_{tag} {}

    std::string_view tag_{};
};

/**
 * @brief One entry of `goldens.json`, as a governed reader sees it.
 *
 * Spans and views over storage the caller owns - the parsed sidecar - so this module reads a golden
 * without owning one. Members mirror the canonical JSON one for one, the same vocabulary
 * `mdux::tools::medui::GoldenReference` writes: `textKey` and `colorToken` are empty when the node
 * draws neither, and `cvChecks` is sorted and deduplicated.
 *
 * The duplication of `bounds`, `textKey` and `colorToken` between the sidecar and the compiled
 * package is not redundancy to be trusted away. `GoldenExpectation::create()` requires every
 * duplicated field to agree with the node the entry names, so a sidecar that drifted from the
 * package addresses content no verifier could find, and says so on the first lookup.
 */
struct GoldenEntry {
    std::string_view         nodeId{};
    mdux::medui::NodeRect    bounds{};
    std::string_view         textKey{};
    std::string_view         colorToken{};
    std::span<const CvCheck> cvChecks{};
};

/// The single static text key a node's compiled spec carries, or empty when it carries none.
///
/// The governed counterpart of the compiler's `textKeyOf()`, over a `CompiledNode` rather than an
/// AST node, and selecting the same fields: a `Label`'s `textKey` and a `Button`'s or
/// `CriticalButton`'s `labelKey`. A `StatusIndicator`'s `states:` is a list and is deliberately not
/// read - the state on screen is the varying part - and a `TextInput`'s `source:` names live data
/// rather than a key.
[[nodiscard]] constexpr std::string_view textKeyOf(const mdux::medui::CompiledNode& node) noexcept {
    if (const auto* label = std::get_if<mdux::medui::LabelSpec>(&node.payload); label != nullptr) {
        return label->textKey;
    }
    if (const auto* button = std::get_if<mdux::medui::ButtonSpec>(&node.payload); button != nullptr) {
        return button->labelKey;
    }
    if (const auto* critical = std::get_if<mdux::medui::CriticalButtonSpec>(&node.payload); critical != nullptr) {
        return critical->labelKey;
    }
    return {};
}

/// The single colour token a node's compiled spec carries, or empty when it carries none or several.
///
/// The governed counterpart of the compiler's `colorTokenOf()`. A `StatusIndicator`'s `colors:` is
/// one token per state, so which one is on screen is the varying part and none of them is the node's
/// tint; that is the same rule that makes `collectGoldens()` refuse `ColorHash` for such a node.
[[nodiscard]] constexpr std::string_view colorTokenOf(const mdux::medui::CompiledNode& node) noexcept {
    if (const auto* panel = std::get_if<mdux::medui::PanelSpec>(&node.payload); panel != nullptr) {
        return panel->colorToken;
    }
    if (const auto* label = std::get_if<mdux::medui::LabelSpec>(&node.payload); label != nullptr) {
        return label->colorToken;
    }
    if (const auto* trace = std::get_if<mdux::medui::SignalTraceSpec>(&node.payload); trace != nullptr) {
        return trace->colorToken;
    }
    if (const auto* button = std::get_if<mdux::medui::ButtonSpec>(&node.payload); button != nullptr) {
        return button->colorToken;
    }
    if (const auto* critical = std::get_if<mdux::medui::CriticalButtonSpec>(&node.payload); critical != nullptr) {
        return critical->colorToken;
    }
    if (const auto* numeric = std::get_if<mdux::medui::NumericDisplaySpec>(&node.payload); numeric != nullptr) {
        return numeric->colorToken;
    }
    if (const auto* input = std::get_if<mdux::medui::TextInputSpec>(&node.payload); input != nullptr) {
        return input->colorToken;
    }
    return {};
}

/**
 * @brief One golden entry, resolved against the screen it describes, in one render scope.
 *
 * Obtainable only through `create()`, which is where ADR-014 decision 2's "derive, don't trust"
 * stops being a slogan: the entry's node id is looked up in the compiled package, and every field
 * the sidecar duplicates has to agree with what it found. A caller cannot amend any of it
 * afterwards, and there is no constructor that skips the checks.
 */
class GoldenExpectation {
public:
    /**
     * @brief Resolves `entry` against `screen`, or refuses.
     *
     * @param entry  one entry of the committed `goldens.json`
     * @param screen the compiled screen the sidecar was emitted beside
     * @param scope  the render scope this obligation is discharged in
     * @param ground what this node's rectangle shows when the node paints nothing
     * @param composites how many layers the runtime paints over that ground for this node; see below
     *
     * Refuses a dangling node id, a rectangle or a duplicated name that disagrees with the node, an
     * empty or non-canonical `cvChecks`, a `ColorHash` with no colour token to compare against, and
     * a colour token the governed table does not define.
     *
     * `ground` is the driver's resolved value - its fixed clear colour, or the tint of the compiled
     * panel beneath this node - and never a colour read back out of the frame under test. See this
     * file's header for why a check needs it as well as the tint.
     *
     * ## `composites`, and why one was the wrong constant
     *
     * `colorHash()` allows one UNORM step per composite the device performed, because a device blends
     * in floating point and quantises back to eight bits at every one of them. Until #261 that number
     * was fixed at one, which was right while every node a golden pinned was painted in a single
     * composite - and a bound `Button` is the first that is not, painting its field and then its word
     * as one tint over one ground at two coverages.
     *
     * **One is still the default, and still exact enough**, because a wide-span channel loses almost
     * nothing to the slack: what makes the second step necessary is a channel whose ground and tint
     * are close, where one step of rounding implies a wide interval of coverage. `couldBeBlend()`
     * carries the worked example. A caller that does not know the depth should pass nothing rather
     * than guess high: slack granted where none is needed is a wrong tint waved through.
     */
    [[nodiscard]] static mdux::core::Result<GoldenExpectation, VerifyError> create(const GoldenEntry&                entry,
                                                                                   const mdux::medui::ScreenPackage& screen,
                                                                                   RenderScope                       scope,
                                                                                   mdux::core::ColorRgba8            ground,
                                                                                   std::size_t                       composites = 1) noexcept;

    /// The compiled node the golden names. Never null for an expectation that exists.
    [[nodiscard]] const mdux::medui::CompiledNode& node() const noexcept {
        return *node_;
    }
    [[nodiscard]] std::string_view nodeId() const noexcept {
        return node_->id;
    }
    /// The declared rectangle, which `create()` has proved is also the node's.
    [[nodiscard]] mdux::medui::NodeRect bounds() const noexcept {
        return node_->bounds;
    }
    [[nodiscard]] RenderScope scope() const noexcept {
        return scope_;
    }
    [[nodiscard]] mdux::core::ColorRgba8 ground() const noexcept {
        return ground_;
    }
    /// Whether a tint was resolved. False only for an entry that opts into `Bounds` alone and names
    /// no colour token - a positioned `VulkanViewport`, say.
    [[nodiscard]] bool hasTint() const noexcept {
        return hasTint_;
    }
    /// The resolved tint. Meaningless unless `hasTint()`; `colorHash()` refuses to run without it.
    [[nodiscard]] mdux::core::ColorRgba8 tint() const noexcept {
        return tint_;
    }
    [[nodiscard]] std::span<const CvCheck> checks() const noexcept {
        return checks_;
    }
    /// How many layers the runtime paints over the ground for this node, and therefore how many
    /// UNORM steps of device rounding `colorHash()` allows. Never zero.
    [[nodiscard]] std::size_t composites() const noexcept {
        return composites_;
    }
    /// Whether the author opted this node into `check`.
    [[nodiscard]] bool declares(CvCheck check) const noexcept {
        return std::ranges::find(checks_, check) != checks_.end();
    }

private:
    GoldenExpectation(const mdux::medui::CompiledNode* node,
                      RenderScope                      scope,
                      std::span<const CvCheck>         checks,
                      mdux::core::ColorRgba8           ground,
                      bool                             hasTint,
                      mdux::core::ColorRgba8           tint,
                      std::size_t                      composites) noexcept
        : node_{node}, scope_{scope}, checks_{checks}, ground_{ground}, tint_{tint}, hasTint_{hasTint}, composites_{composites} {}

    const mdux::medui::CompiledNode* node_{nullptr};
    RenderScope                      scope_{RenderScope::localeFree()};
    std::span<const CvCheck>         checks_{};
    mdux::core::ColorRgba8           ground_{};
    mdux::core::ColorRgba8           tint_{};
    bool                             hasTint_{false};
    std::size_t                      composites_{1};
};

static_assert(!std::is_aggregate_v<GoldenExpectation>, "a GoldenExpectation must only be obtainable through create()");

/**
 * @brief A rectangle to hash, and the baseline digest to compare it against - if there is one.
 *
 * The expectation `rawImageDigest()` consumes, and the whole of what the `raw-image-digest` profile
 * (`mdux.local/raw-image-digest`, ADR-016) needs: a region of interest, and optionally a committed
 * SHA-256 to check it against.
 * This is TrustSC's `ColorHash` observation - a raw-pixel digest - given a name of its own so the
 * two implementations' `ColorHash` results are never mistaken for each other.
 *
 * **There is no production caller that supplies a baseline**, and there is deliberately no committed
 * one. [ADR-014](../../../docs/adr/ADR-014-rendered-truth-verification.md) decision 4 and its
 * alternative 6 keep measured pixel values out of the byte-compared bundle: a committed RGBA8
 * digest is a property of the driver tuple that produced the frame - lavapipe, MoltenVK, Mesa's
 * Windows build - not of the screen's declared inputs, so it would fail an evidence leg on any
 * rendering change while every check still held. TrustSC commits none either. So `createWithBaseline()`
 * exists for the unit suite and for a future consumer that has declared a presentation/backend
 * profile (PAR-REQ-009); `create()` is the shape everything else uses, and it resolves to
 * `Finding::NoBaseline`, which never discharges an obligation.
 *
 * Obtainable only through a factory, and not an aggregate, so a `roi` that was never bounded cannot
 * reach the check.
 */
class RawImageExpectation {
public:
    /// A region to hash with no baseline to compare it to. The result is `Finding::NoBaseline`.
    ///
    /// Refuses a non-positive `roi` extent - `VerifyError::DigestRoiDegenerate` - so the check's
    /// own bounds walk is over a rectangle by construction.
    [[nodiscard]] static mdux::core::Result<RawImageExpectation, VerifyError>
    create(std::string_view nodeId, RenderScope scope, mdux::medui::NodeRect roi) noexcept;

    /// A region to hash and the committed digest it must equal. For the unit suite and a future
    /// declared-profile consumer; there is no such consumer today. Same `roi` refusal as `create()`.
    [[nodiscard]] static mdux::core::Result<RawImageExpectation, VerifyError>
    createWithBaseline(std::string_view nodeId, RenderScope scope, mdux::medui::NodeRect roi, mdux::evidence::Digest baseline) noexcept;

    [[nodiscard]] std::string_view nodeId() const noexcept {
        return nodeId_;
    }
    [[nodiscard]] RenderScope scope() const noexcept {
        return scope_;
    }
    [[nodiscard]] mdux::medui::NodeRect roi() const noexcept {
        return roi_;
    }
    /// Whether a baseline was supplied. When false, `rawImageDigest()` reports `NoBaseline`.
    [[nodiscard]] bool hasBaseline() const noexcept {
        return hasBaseline_;
    }
    /// The committed digest. Meaningless unless `hasBaseline()`.
    [[nodiscard]] const mdux::evidence::Digest& baseline() const noexcept {
        return baseline_;
    }

private:
    RawImageExpectation(std::string_view nodeId, RenderScope scope, mdux::medui::NodeRect roi, bool hasBaseline, const mdux::evidence::Digest& baseline) noexcept
        : nodeId_{nodeId}, scope_{scope}, roi_{roi}, baseline_{baseline}, hasBaseline_{hasBaseline} {}

    std::string_view       nodeId_{};
    RenderScope            scope_{RenderScope::localeFree()};
    mdux::medui::NodeRect  roi_{};
    mdux::evidence::Digest baseline_{};
    bool                   hasBaseline_{false};
};

static_assert(!std::is_aggregate_v<RawImageExpectation>, "a RawImageExpectation must only be obtainable through a factory");

/**
 * @brief Whether `inner` lies wholly inside `outer`. Touching an edge counts as inside.
 *
 * 64-bit intermediates so two `int32_t` rectangles at their extremes cannot overflow on the
 * right/bottom edge addition and admit a containment that does not hold. A non-positive extent on
 * `inner` still tests by edges - a zero-area rectangle at a point inside `outer` is inside it.
 *
 * `goldenBounds()` and the text checks use this to ask whether measured ink stayed in the box that
 * authorises it. It is exported because MedUI's `MEDUI-PROFILE-RENDERED` rule R02 (ink-containment)
 * is the same predicate over `inflate(golden, margin)`, and the shared corpus must run the check
 * this module ships rather than a harness copy of it (ADR-008 decision 1, applied to the verifier).
 */
[[nodiscard]] bool rectContainedBy(mdux::medui::NodeRect inner, mdux::medui::NodeRect outer) noexcept;

/**
 * @brief `rect` grown by `margin` pixels on every side: `{x - m, y - m, width + 2m, height + 2m}`.
 *
 * `margin` is expected to be nonnegative, matching `MEDUI-PROFILE-RENDERED` rule R02's margin, which
 * is a nonnegative integer; a negative value shrinks the rectangle and is left to the caller to rule
 * out. 64-bit intermediates, then a checked narrow back to `int32_t`: a hostile extent that would
 * overflow saturates rather than wrapping into a smaller rectangle that wrongly contains its ink.
 */
[[nodiscard]] mdux::medui::NodeRect inflate(mdux::medui::NodeRect rect, std::int32_t margin) noexcept;

/**
 * @brief Whether `pixel` could be `tint` composited over `ground` at a single coverage in [0, 1].
 *
 * Channel-wise membership of the interval between ground and tint is necessary and not sufficient:
 * alpha blending applies one coverage to every channel, so each channel constrains that coverage to
 * an interval and the pixel is possible exactly when those intervals intersect within [0, 1]. With a
 * black ground and `Theme.Colors.ScoreDigits` at `(33, 184, 107)`, the pixel `(33, 0, 107)` lies in
 * every channel's range while demanding full coverage of red and blue and none of green - a
 * per-channel test accepts it and no blend can produce it. Integer cross-products, no division and
 * no float.
 *
 * `allowance` is the UNORM slack per composite the device performed (one for a single blend, more
 * for a stacked one): a channel constrains coverage only as tightly as its own span allows, so one
 * step of device rounding on a narrow span implies a wide coverage interval. A channel whose tint
 * and ground are equal admits any coverage and only requires the pixel to match within `allowance`.
 *
 * **Precondition: `ground` and `tint` share their alpha** - both `colorHash()` (opaque governed
 * theme colours) and rule R03 (opaque RGB8 samples, per `spec/profiles.md`) meet it. Alpha then
 * contributes an equal-endpoints channel and drops out. This function models `blend()`, which
 * preserves the framebuffer's alpha rather than interpolating it, only under that precondition; a
 * caller passing mismatched alphas gets the fourth channel treated as interpolated, which `blend()`
 * is not - correcting that is a separate reviewed change with its own profile version.
 *
 * `colorHash()` uses this. It is exported because `MEDUI-PROFILE-RENDERED` rule R03
 * (tint-composition) is this predicate per sample, with `allowance` = the vector's `composites`
 * count; the spec's R03 `n/2` -> `n` amendment was written against this exact arithmetic and the
 * MduX narrow-red regression it produced.
 */
[[nodiscard]] bool couldBeBlend(mdux::core::ColorRgba8 pixel,
                                mdux::core::ColorRgba8 ground,
                                mdux::core::ColorRgba8 tint,
                                std::int64_t           allowance) noexcept;

/**
 * @brief The colour a coverage value paints when the tint is composited over the ground.
 *
 * The blend the coverage draw path performs, written down where a check can compute it:
 * `result = tint * a + ground * (1 - a)`, with `a = (tint.a / 255) * (coverage / 255)`.
 * `tests/render/TextPixelTests.cpp` derives its expectations from the same equation and compares
 * them to real pixels with no tolerance under lavapipe and MoltenVK, so this is the repository's
 * established statement of what a covered texel produces rather than a second opinion about it.
 *
 * Integer throughout, rounding to nearest: a governed check that reached for a float here would make
 * its own answer depend on the host's rounding mode, which is the property ADR-007 exists to keep
 * out of anything evidence-adjacent.
 */
[[nodiscard]] mdux::core::ColorRgba8 blend(mdux::core::ColorRgba8 ground, mdux::core::ColorRgba8 tint, std::uint8_t coverage) noexcept;

/**
 * @brief One placed glyph: where it paints, and which texels of the sheet it paints from.
 *
 * Both halves, because a check that had only the rectangle could establish that *something* was
 * painted in the right box and nothing more - which is exactly the gap that made an earlier revision
 * of `localizedTextPresence()` claim more than it verified.
 */
struct PlacedGlyph {
    mdux::medui::NodeRect rect{};     ///< where it paints, in frame coordinates
    std::uint32_t         atlasX{0};  ///< the slot's left edge in the coverage sheet
    std::uint32_t         atlasY{0};  ///< the slot's top edge in the coverage sheet
};

/**
 * @brief One compiled text node, in one approved locale, with the run that locale binds to it.
 *
 * The mandatory pair's view. It carries no golden entry and needs none: a `textKey` node's two
 * obligations exist whether or not ADR-011's predicate selected it.
 *
 * ## Where its authority comes from
 *
 * A production expectation is built from a `mdux::medui::TextBinding` and the screen the obligation
 * is about, and from nothing else. That is not a convenience: `TextBinding::create()` is the one
 * place in this repository that proves the four artifacts agree - the screen's manifest approves
 * this locale, package id and canonical digest; the text package was baked against this font; the
 * sidecar is the one the package describes; and every run's range hashes to what the package
 * recorded. An expectation that took a locale string and a span of bytes could be pointed at an
 * unapproved translation and would then produce a *passing* verification outcome, which is a worse
 * failure than a refusal because it ends up in an evidence artifact.
 *
 * So `create()` takes the binding, checks it against the screen and the node, and looks the run up
 * by the node's own `textKey`. `createSynthetic()` exists beside it for the unit suite and
 * establishes none of that - see its comment.
 *
 * ## The placement rule is not re-decided here
 *
 * `mdux.medui.screen` puts the run's *ink* box at the node's top-left corner - the box #195 measured
 * against that rectangle - and this view computes the same origin from the same records and the same
 * font, so a check compares the frame against where the runtime would have drawn rather than against
 * a second opinion about where text belongs.
 *
 * ## The coverage sheet, and why the view carries it
 *
 * The glyph metrics say where a glyph paints; only the baked atlas says *what* it paints. A view
 * holding metrics alone can support "something is inside this rectangle", which a sparse handful of
 * pixels satisfies and which two different letters of the same size satisfy equally. Carrying the
 * sheet is what lets `localizedTextPresence()` compare against the shape the approved run actually
 * has, and it costs nothing on the device side: the bytes are the ones the renderer already uploaded
 * as its atlas.
 */
class TextExpectation {
public:
    /**
     * @brief Raises this node's text obligation for the locale `binding` was proved against.
     *
     * @param screen  the compiled screen whose manifest authorises the bound package
     * @param node    the compiled node whose spec carries a `textKey`; must be one of `screen`'s
     * @param binding the packages `TextBinding::create()` has already proved describe each other
     * @param atlas   the coverage sheet the bound font package describes
     * @param scope   the render scope; must name the locale the binding carries
     * @param ground  what this node's rectangle shows where the run paints nothing
     * @param groundComposites how many device composites produced that ground; see below
     *
     * Refuses an unbound binding, a binding the screen does not approve, a node the screen does not
     * contain, a scope naming a different locale from the bound package's, a node carrying no text
     * key, a text key the package has no run for, an atlas that is not the size the font package
     * declares, a glyph slot outside that sheet, a node whose colour token does not resolve, and a
     * run that paints no ink at all.
     *
     * That last refusal is a policy rather than an accident. A run whose glyphs are all blank - a
     * single space - is legitimate for the runtime, which draws nothing and does not count the node
     * as deferred. It is not something a rendered-truth obligation can discharge: there is no ink to
     * find, so a pass would be indistinguishable from a screen that lost its text. ADR-014 decision
     * 3 says a check this build cannot perform fails, so this refuses rather than passing vacuously,
     * and the screen is what changes.
     *
     * ## `groundComposites`, and why zero is not the only honest answer
     *
     * Both text checks ask whether the pixels a glyph does not cover are still the ground, and until
     * #261 that was an **exact** comparison, correctly: every ground this driver could supply was a
     * value no device had computed - the clear colour, or an opaque panel, which `SRC_ALPHA` blending
     * reproduces byte for byte.
     *
     * A `Button` and a `CriticalButton` paint their own field before their label goes over it, so
     * the pixels under that label are a composite the *device* produced, and a device is allowed to
     * produce it in whatever precision its blend unit has. Measured on lavapipe and radv, a field at
     * `boundFieldCoverage` lands one UNORM step below the integer `blend()` predicts on the channel
     * whose exact value falls on a half - which is the same last-bit disagreement this file already
     * admits for a glyph, arising in the same way and for the same reason.
     *
     * So the ground carries the number of composites that made it, and the checks allow that many
     * steps on a ground pixel and add it to the glyph allowance on a painted one. **Zero keeps the
     * old exactness**, which is what every existing caller passes and what a clear colour or a panel
     * deserves: a slack granted where none is needed is a wrong tint waved through.
     */
    [[nodiscard]] static mdux::core::Result<TextExpectation, VerifyError> create(const mdux::medui::ScreenPackage& screen,
                                                                                 const mdux::medui::CompiledNode&  node,
                                                                                 const mdux::medui::TextBinding&   binding,
                                                                                 std::span<const std::byte>        atlas,
                                                                                 RenderScope                       scope,
                                                                                 mdux::core::ColorRgba8            ground,
                                                                                 std::size_t                       groundComposites = 0) noexcept;

    /**
     * @brief Builds a view over caller-supplied parts, establishing no provenance whatever.
     *
     * **For unit tests.** ADR-014 decision 1 admits exactly this and bounds it in the same sentence:
     * "A unit test may construct a synthetic view to exercise a pure check, but production
     * expectations have exactly the artifact sources decision 2 names." A check is a pure function,
     * so proving it needs a framebuffer and a view - not a screen, a digest and a baked package -
     * and refusing to offer that path would mean every scenario carried four real artifacts to test
     * arithmetic.
     *
     * What it does not do is anything `create()` does. It does not see a screen, so it cannot know
     * that this locale was approved; it does not see a `TextBinding`, so it cannot know that these
     * records came from the package the screen names, or that the font they index is the one they
     * were baked against. A driver that reached for this would be producing verification outcomes
     * about a run nothing authorised, and that is the review finding to raise on #253 rather than a
     * runtime condition this module can detect.
     *
     * The structural refusals `create()` makes are still made here: a node with no text key, a
     * locale-free scope, a partial or over-long run, a dangling glyph index, an atlas of the wrong
     * size, a slot outside it, an unresolvable colour token and a run with no ink.
     */
    [[nodiscard]] static mdux::core::Result<TextExpectation, VerifyError> createSynthetic(const mdux::medui::CompiledNode& node,
                                                                                          RenderScope                      scope,
                                                                                          std::span<const std::byte>       records,
                                                                                          const mdux::font::FontPackage&   font,
                                                                                          std::span<const std::byte>       atlas,
                                                                                          mdux::core::ColorRgba8           ground,
                                                                                          std::size_t                      groundComposites = 0) noexcept;

    [[nodiscard]] const mdux::medui::CompiledNode& node() const noexcept {
        return *node_;
    }
    [[nodiscard]] std::string_view nodeId() const noexcept {
        return node_->id;
    }
    [[nodiscard]] std::string_view textKey() const noexcept {
        return textKeyOf(*node_);
    }
    [[nodiscard]] mdux::medui::NodeRect bounds() const noexcept {
        return node_->bounds;
    }
    [[nodiscard]] RenderScope scope() const noexcept {
        return scope_;
    }
    /// The approved locale's tag. Never empty: both factories refuse a locale-free scope.
    [[nodiscard]] std::string_view locale() const noexcept {
        return scope_.tag();
    }
    [[nodiscard]] std::span<const std::byte> records() const noexcept {
        return records_;
    }
    [[nodiscard]] const mdux::font::FontPackage& font() const noexcept {
        return *font_;
    }
    /// The coverage sheet the run paints from.
    [[nodiscard]] std::span<const std::byte> atlas() const noexcept {
        return atlas_;
    }
    [[nodiscard]] mdux::core::ColorRgba8 tint() const noexcept {
        return tint_;
    }
    [[nodiscard]] mdux::core::ColorRgba8 ground() const noexcept {
        return ground_;
    }
    /// How many device composites produced `ground()`. Zero means it was never blended, and a
    /// ground pixel then has to equal it exactly.
    [[nodiscard]] std::size_t groundComposites() const noexcept {
        return groundComposites_;
    }
    /// How many records the run holds, blanks included.
    [[nodiscard]] std::size_t glyphCount() const noexcept {
        return records_.size() / mdux::text::draw::recordSize;
    }
    /// Where the run's ink lands once placed by the runtime's rule. Never empty.
    [[nodiscard]] mdux::medui::NodeRect ink() const noexcept {
        return ink_;
    }
    /// Where record `index` paints and paints from, or nothing when it is blank or out of range.
    [[nodiscard]] std::optional<PlacedGlyph> glyph(std::size_t index) const noexcept;

    /// The coverage the sheet holds for the texel at (`dx`, `dy`) inside `placed`.
    ///
    /// Zero for a coordinate outside the glyph, which `create()` has already made unreachable -
    /// answered rather than assumed, because the alternative is indexing a span with a number this
    /// function did not check.
    [[nodiscard]] std::uint8_t coverage(const PlacedGlyph& placed, mdux::core::Px dx, mdux::core::Px dy) const noexcept;

private:
    /// The structural half both factories share: the run, the sheet, the tint and the ink box.
    ///
    /// Deliberately private. What separates a production expectation from a synthetic one is
    /// provenance, and provenance is established by the caller-facing factories - so the shared part
    /// must not be reachable as a third way to build one.
    [[nodiscard]] static mdux::core::Result<TextExpectation, VerifyError> build(const mdux::medui::CompiledNode& node,
                                                                                RenderScope                      scope,
                                                                                std::span<const std::byte>       records,
                                                                                const mdux::font::FontPackage&   font,
                                                                                std::span<const std::byte>       atlas,
                                                                                mdux::core::ColorRgba8           ground,
                                                                                std::size_t                      groundComposites) noexcept;

    TextExpectation(const mdux::medui::CompiledNode* node,
                    RenderScope                      scope,
                    std::span<const std::byte>       records,
                    const mdux::font::FontPackage*   font,
                    std::span<const std::byte>       atlas,
                    mdux::core::ColorRgba8           tint,
                    mdux::core::ColorRgba8           ground,
                    mdux::medui::NodeRect            ink,
                    mdux::core::Px                   originX,
                    mdux::core::Px                   originY,
                    std::size_t                      groundComposites) noexcept
        : node_{node},
          scope_{scope},
          records_{records},
          font_{font},
          atlas_{atlas},
          tint_{tint},
          ground_{ground},
          ink_{ink},
          originX_{originX},
          originY_{originY},
          groundComposites_{groundComposites} {}

    const mdux::medui::CompiledNode* node_{nullptr};
    RenderScope                      scope_{RenderScope::localeFree()};
    std::span<const std::byte>       records_{};
    const mdux::font::FontPackage*   font_{nullptr};
    std::span<const std::byte>       atlas_{};
    mdux::core::ColorRgba8           tint_{};
    mdux::core::ColorRgba8           ground_{};
    mdux::medui::NodeRect            ink_{};
    mdux::core::Px                   originX_{0};
    mdux::core::Px                   originY_{0};
    std::size_t                      groundComposites_{0};
};

static_assert(!std::is_aggregate_v<TextExpectation>, "a TextExpectation must only be obtainable through create()");

/**
 * @brief What a check found, when it did not hold.
 *
 * One enumerator per way a frame can disagree with its expectation, rather than a single "failed":
 * ADR-014 decision 1 says a failure has to be a sentence someone can act on, and "the region the
 * golden pins shows nothing" and "the region shows something in the wrong colour" send a reader to
 * different places.
 */
enum class Finding : std::uint8_t {
    Held,                ///< the check passed
    RegionOutsideFrame,  ///< the rectangle to inspect is not inside the framebuffer
    NoTintToCompare,     ///< `colorHash()` was asked about an expectation that resolved no tint
    NothingPainted,      ///< the rectangle shows the ground everywhere; nothing was drawn
    BoundsDiffer,        ///< content is there, and its extent is not the declared rectangle
    TintAbsent,          ///< content is there, and no pixel of it carries the resolved tint
    ForeignColour,       ///< a painted pixel is not a blend of the ground and the resolved tint
    InkLeftItsNode,      ///< the run's placed ink box is not inside the node's rectangle
    InkExtentDiffers,    ///< the frame's ink is not where the committed run says it would be
    GlyphMissing,        ///< a non-blank record of the approved run painted nothing
    CoverageDiffers,     ///< a glyph's pixels are not what its baked coverage would produce
    InkOutsideTheRun,    ///< the node shows ink where this locale's run paints none
    NoBaseline,          ///< `rawImageDigest()` had no committed digest to compare against
    DigestMismatch,      ///< the region's SHA-256 is not the committed baseline's
};

[[nodiscard]] std::string_view describe(Finding finding) noexcept;

/**
 * @brief The enumerator's own name, e.g. `NothingPainted` - the spelling a serialiser emits.
 *
 * Distinct from `describe()`, and the distinction is the point. `describe()` returns the sentence a
 * driver prints for a human, and improving that wording should stay free; #254 commits findings into
 * a byte-compared artifact, where a reworded sentence would fail an evidence comparison on every leg
 * while nothing about the verification had changed. The same split `spell(CvCheck)` already makes.
 */
[[nodiscard]] constexpr std::string_view spell(Finding finding) noexcept {
    switch (finding) {
        case Finding::Held:
            return "Held";
        case Finding::RegionOutsideFrame:
            return "RegionOutsideFrame";
        case Finding::NoTintToCompare:
            return "NoTintToCompare";
        case Finding::NothingPainted:
            return "NothingPainted";
        case Finding::BoundsDiffer:
            return "BoundsDiffer";
        case Finding::TintAbsent:
            return "TintAbsent";
        case Finding::ForeignColour:
            return "ForeignColour";
        case Finding::InkLeftItsNode:
            return "InkLeftItsNode";
        case Finding::InkExtentDiffers:
            return "InkExtentDiffers";
        case Finding::GlyphMissing:
            return "GlyphMissing";
        case Finding::CoverageDiffers:
            return "CoverageDiffers";
        case Finding::InkOutsideTheRun:
            return "InkOutsideTheRun";
        case Finding::NoBaseline:
            return "NoBaseline";
        case Finding::DigestMismatch:
            return "DigestMismatch";
    }
    return {};
}

/**
 * @brief One obligation's result: what was expected, what was found, and where.
 *
 * Structured rather than a formatted string, because this module allocates nothing and a governed
 * type that carried a message would have to. #253's driver renders these into the sentence ADR-014
 * asks for; everything that sentence needs is here, including the render scope, so a reader can tell
 * a failure in `de-DE` from the same node's pass in `en-US`.
 *
 * `expected` is **the rectangle this outcome's claim is about**, which is not always the node's box.
 * `goldenBounds()` and `colorHash()` name the golden's declared rectangle; `inkContainment()` names
 * the node's rectangle while the failure is about containment and the predicted ink box once the
 * comparison has moved on to the frame; `localizedTextPresence()` names one glyph's rectangle for a
 * glyph-level finding and the node's otherwise. `found` is what was measured against it, and is only
 * meaningful for the findings that measured something - `foundValid` says which. Spelled out here
 * because a driver formatting "expected X, found Y" has to know what X refers to.
 */
struct CheckOutcome {
    Finding                finding{Finding::Held};
    std::string_view       nodeId{};
    std::string_view       scope{};     ///< the locale tag, or `localeFreeScopeName`
    std::string_view       check{};     ///< the check's spelling, e.g. `Bounds`
    mdux::medui::NodeRect  expected{};  ///< the rectangle the expectation named
    mdux::medui::NodeRect  found{};     ///< what the frame showed, when the finding measured it
    bool                   foundValid{false};
    mdux::core::ColorRgba8 expectedColor{};
    mdux::core::ColorRgba8 foundColor{};
    bool                   foundColorValid{false};
    std::size_t            glyphIndex{0};  ///< which record, for a glyph-level finding
    // Appended rather than slotted beside `check`: this is an aggregate a caller may initialise
    // positionally (see the same note on `Invocation` in the driver), so a new field goes last.
    ObservationProfile     profile{};  ///< which observation this outcome is: id and version (ADR-016)

    [[nodiscard]] constexpr bool held() const noexcept {
        return finding == Finding::Held;
    }
};

/**
 * @brief `Bounds`: the golden node's rendered content occupies the declared rectangle.
 *
 * The bounding box of everything painted inside the declared rectangle has to be that rectangle.
 * Fails when the rectangle is not inside the frame, when it shows the ground everywhere - a node
 * that vanished or was never drawn - and when what it shows does not reach every edge, which is what
 * a node that moved looks like from inside its own box. `found` carries the extent that was actually
 * measured, so a report says where the content was as well as where it should have been.
 *
 * What it cannot see is content that overflowed into a neighbouring node's rectangle; see this
 * file's header for that limit and for the component whose partial fill this reading excludes.
 */
[[nodiscard]] CheckOutcome goldenBounds(const FramebufferView& frame, const GoldenExpectation& expectation) noexcept;

/**
 * @brief `ColorHash`: the golden node's rendered content carries the tint its token resolves to.
 *
 * Two claims, both exact. Every painted pixel lies channel-wise between the ground and the tint, so
 * nothing in the rectangle was painted by something else; and at least one is exactly the tint, so
 * the content really carries it rather than merely tending towards it.
 *
 * Refuses to run - `Finding::NoTintToCompare` - on an expectation that resolved no tint, rather than
 * reporting a pass it did not establish.
 */
[[nodiscard]] CheckOutcome colorHash(const FramebufferView& frame, const GoldenExpectation& expectation) noexcept;

/**
 * @brief `InkContainment`: a compiled text node's run stays inside its node bounds.
 *
 * The compile-time claim #195 made, verified against pixels. The committed run and font metrics
 * predict an ink box, the runtime's placement rule puts its corner on the node's corner, and this
 * fails when that box leaves the node - and, separately, when the frame's ink inside the node is not
 * the box the artifacts predicted, which is what a clipped, moved or partially drawn run looks like.
 *
 * An **extent** claim, deliberately and only. It says the run's ink occupies the box the artifacts
 * predict, to the precision the frame can distinguish - see the note under the declaration for the
 * band it cannot - and no more; it does not say the glyphs in that box are the approved locale's, and a few
 * pixels at the box's corners would preserve the extent while saying nothing about what is between
 * them. Establishing the run's shape is `localizedTextPresence()`'s job, which is why that one
 * carries the coverage sheet and this one does not.
 */
[[nodiscard]] CheckOutcome inkContainment(const FramebufferView& frame, const TextExpectation& expectation) noexcept;

// The rendered half is a containment rather than an equality whenever the ground carries composites,
// and the reason is a limit rather than a preference: over a field the device itself composited, a
// glyph texel at coverage 1 or 2 lands within one UNORM step of that field, so it cannot be told
// from a field pixel the device rounded. The check therefore measures what was *certainly* painted
// and what was *possibly* painted and asks that the prediction sit between them. With an
// uncomposited ground the two coincide and the equality is unchanged.


/**
 * @brief `LocalizedTextPresence`: the approved locale's bound run is the one on screen.
 *
 * Per glyph and per pixel, in both directions.
 *
 * Inside the run's ink, every pixel must be what the *baked coverage* reaching it produces when the
 * node's tint is composited over the ground - `blend()`, within one UNORM step per composite. Where
 * two placed glyphs overlap, the coverages compose as `1 - (1 - a1)(1 - a2)`, because the draw path
 * records one quad per glyph and blends them in turn and nothing in `FontPackage::validate()`
 * requires an advance to clear its own bitmap. Comparing such a pixel against either glyph alone
 * would fail a correct frame. That is the
 * half that makes this a claim about the approved run rather than about ink in the right boxes: a
 * texel the atlas leaves at zero must be exactly the ground, a fully covered one must be exactly the
 * tint, and every value between is pinned to the coverage the baker recorded. A different letter of
 * the same size, a run drawn from another package's slots, and a sparse handful of pixels that
 * merely occupy the rectangles all fail it.
 *
 * Outside every placed glyph, every pixel of the node must still be the ground. The atlas slot is
 * exactly the glyph's bitmap, so a correct frame paints nothing there.
 *
 * The allowance is for UNORM rounding and nothing else: the blend is computed in floating point on
 * the device and quantised back to eight bits at every composite, so it is one step per glyph
 * covering the pixel rather than a fixed slack. It is not a similarity threshold - a wrong shape
 * misses by far more, and a wrong tint by more still.
 */
[[nodiscard]] CheckOutcome localizedTextPresence(const FramebufferView& frame, const TextExpectation& expectation) noexcept;

/**
 * @brief `raw-image-digest`: the region's tightly packed RGBA8 hashes to a committed baseline.
 *
 * TrustSC's `ColorHash` observation, named separately here so that the two implementations'
 * `ColorHash` results are never compared as though they measured the same thing (ADR-015 D2). The
 * digest is SHA-256 over the region of interest's pixels, row by row, four bytes per pixel, with no
 * stride padding - the same "tightly packed row-major RGBA8" TrustSC hashes.
 *
 * **It reports `Finding::NoBaseline` for every production call**, because there is no committed
 * baseline and deliberately never will be: ADR-014 decision 4 keeps a driver-tuple-dependent
 * measurement out of the byte-compared bundle, and ADR-016 records why that makes this profile a
 * fixture-and-future-consumer tool rather than a committed gate. `NoBaseline` never discharges an
 * obligation - it is distinct from a pass, the way TrustSC's own `NoBaseline` is. With a baseline
 * (a unit test, or a consumer that has declared a presentation profile) it reports `Held` on a
 * match and `Finding::DigestMismatch` otherwise. A region outside the frame is
 * `Finding::RegionOutsideFrame`, a failure rather than a skip.
 */
[[nodiscard]] CheckOutcome rawImageDigest(const FramebufferView& frame, const RawImageExpectation& expectation) noexcept;

}  // namespace mdux::verify
