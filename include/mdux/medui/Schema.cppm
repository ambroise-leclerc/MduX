/**
 * @file Schema.cppm
 * @brief Governed-zone compiled-screen types: what a `.medui` screen becomes once the compiler has
 *        resolved it, in the form a device holds it.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Part of MduXCore, and canonical: the emitters (#197), the compiler driver (#198) and the
 * allocation-free runtime (#199) import this rather than restating its records. Two files
 * describing the same artifact disagree eventually, and the disagreement surfaces as a
 * byte-comparison failure nobody can localise - the Wave 2 lesson, applied here before there is
 * anything to re-learn it on.
 *
 * Header-only by design; there is no `src/medui/Schema.cpp`. Everything is `constexpr`, because
 * ADR-012 decision 3 puts `static_assert(package.validate().has_value())` in the generated source:
 * a malformed screen has to be a compile error on the device build, not a startup failure in a
 * theatre. `mdux.ml.schema` is the model followed here. `mdux.text.schema`'s `TextPackage`
 * deliberately is not - it owns `std::string` and `std::vector`, so it could not appear in a
 * `static_assert` at all.
 *
 * ## Non-owning, and what that costs the caller
 *
 * Every string is a `std::string_view` and the node list is a `std::span`. The generated
 * translation unit owns the storage, statically, so a `ScreenPackage` is a handful of pointers that
 * costs nothing to copy and needs no lifetime management. The cost is that a host tool assembling a
 * screen cannot use this type as its accumulator - it builds an owning form and takes a view of it.
 * That is the same split `mdux.ml.schema` makes with `mdux-mlbake`, and it is the price of a type a
 * device can hold in `.rodata`.
 *
 * ## The package is locale-free, and that is load-bearing
 *
 * ADR-011, as amended by #203, carries `textKey` and `colorToken` as **validated names** rather than
 * as glyph runs and RGBA8. The device resolves each against a governed table for the locale it is
 * running - a bounded lookup, not a parse - and the per-locale glyph runs stay in the text package
 * where ADR-010 put them.
 *
 * The consequence that decides the file layout: **adding an approved locale rewrites no screen
 * artifact.** Translations change far more often than layouts, and a package carrying runs would
 * make the frequent change rewrite the stable artifact - and its digest, and its review.
 *
 * The cost, stated as a cost: one rectangle serves every locale, so it must be the one that
 * survives the widest approved translation. That is what #195 measures, and why the budget below
 * cannot be derived from the node count.
 *
 * ## The colour table, and why its contents are here rather than supplied
 *
 * ADR-011 puts the *resolution* of `Theme.Colors.<Token>` on the device, as a bounded scan of a
 * governed table - TrustSC's `THEME_COLORS` and `resolve_color_token()` are the reference, and both
 * live in its governed crate. So `themeColors`, `ThemeColor` and `resolveColorToken()` are here,
 * beside the package that carries the names: a consumer holding this schema holds the whole
 * device-side contract, and #199 inherits a resolver rather than writing a second one.
 *
 * The table's *contents* are here for the same reason, and it is worth stating because the opposite
 * is defensible in isolation: a palette looks like a product decision, and the theme names semantic
 * analysis validates against are indeed a compiler input (#193). But the parity programme's purpose
 * is that one `.medui` screen means the same thing in both projects, and it does not if a token
 * renders one colour here and another there. TrustSC settles this in its governed crate; MduX
 * matches it entry for entry, and a change to the palette is a change to make in both projects at
 * once.
 *
 * ## What `validate()` checks, and what it deliberately does not
 *
 * It checks what a consumer is entitled to assume without looking: an identified screen, a positive
 * surface, nodes with unique ids and rectangles that lie inside that surface, colour tokens the
 * governed table actually defines, and a budget the index width can address.
 *
 * It does **not** check that the budget is large enough for what the screen draws. That number is
 * not derivable from anything here: a `Label` draws one rectangle per glyph of the widest approved
 * translation, and this package carries no glyph runs by design. The compiler computes the budget
 * from the text packages it measured (#195) and the components' own draw shapes (#17); a rule
 * invented here would be a second, weaker opinion about a number this type only carries. What it
 * *can* say is that a screen with nodes and an empty budget draws nothing, and that a budget past
 * `mdux::draw::maxIndexableVertices` cannot be indexed - both are refused.
 */
module;

export module mdux.medui.schema;

import std;
import mdux.core.result;
import mdux.draw;
import mdux.evidence.report;

export namespace mdux::medui {

/// The `<kind>` component of `generated/<kind>/<id>/`, and the value of a package's `kind` member.
inline constexpr std::string_view packageKind = "screen";

/// The prefix every colour a node draws with must carry, and which it must carry *something* after:
/// the package holds names, never values, and `Theme.Colors.` on its own is not a name.
inline constexpr std::string_view colorTokenPrefix = "Theme.Colors.";

enum class SchemaError : std::uint8_t {
    UnsupportedSchemaVersion,  ///< the package declares a version this module does not read
    EmptyId,                   ///< the screen has no id, so no directory and no evidence entry
    NonPositiveSurface,        ///< a surface with no extent cannot contain a rectangle
    EmptyNodeId,               ///< a node with no id cannot be named by a golden or a requirement
    DuplicateNodeId,           ///< two nodes share an id, so a golden could name either
    DegenerateBounds,          ///< a rectangle with no extent, which `DrawList` refuses to record
    BoundsOutsideSurface,      ///< a rectangle the declared surface does not contain
    MalformedColorToken,       ///< a colour that is not a `Theme.Colors.<Token>` name
    UnknownColorToken,         ///< a well-formed name the governed table does not define
    UnknownPayload,            ///< a payload this module cannot name, or one left valueless
    EmptyRequiredName,         ///< a spec field the component dictionary requires is empty
    NoStates,                  ///< a status indicator that can show nothing
    StateColorCountMismatch,   ///< per-state tints that do not pair one-to-one with the states
    NonPositiveMaxLength,      ///< a text input that can hold no character
    EmptyBudget,               ///< a screen with nodes whose budget can hold no primitive
    BudgetExceedsIndexWidth,   ///< more vertices than a 16-bit index can address
};

[[nodiscard]] constexpr std::string_view describe(SchemaError error) noexcept {
    switch (error) {
        case SchemaError::UnsupportedSchemaVersion:
            return "the package declares an unsupported schemaVersion";
        case SchemaError::EmptyId:
            return "the screen has no id";
        case SchemaError::NonPositiveSurface:
            return "the surface has no extent";
        case SchemaError::EmptyNodeId:
            return "a node has no id";
        case SchemaError::DuplicateNodeId:
            return "two nodes share an id";
        case SchemaError::DegenerateBounds:
            return "a node's rectangle has no extent";
        case SchemaError::BoundsOutsideSurface:
            return "a node's rectangle leaves the declared surface";
        case SchemaError::MalformedColorToken:
            return "a colour is not a Theme.Colors.<Token> name";
        case SchemaError::UnknownColorToken:
            return "a colour names a token the governed table does not define";
        case SchemaError::UnknownPayload:
            return "a node carries a payload this module cannot name";
        case SchemaError::EmptyRequiredName:
            return "a name the component dictionary requires is empty";
        case SchemaError::NoStates:
            return "a status indicator declares no state to show";
        case SchemaError::StateColorCountMismatch:
            return "per-state colours do not pair one-to-one with the states";
        case SchemaError::NonPositiveMaxLength:
            return "a text input accepts no character";
        case SchemaError::EmptyBudget:
            return "the screen has nodes and a budget that can hold no primitive";
        case SchemaError::BudgetExceedsIndexWidth:
            return "the vertex budget exceeds what a 16-bit index can address";
    }
    return "unknown schema error";
}

/**
 * @brief One entry of the governed token to RGBA table: a name a screen may carry, and its colour.
 *
 * The device side of ADR-011's boundary. A compiled screen carries `Theme.Colors.<Token>` as a
 * *name*, and the runtime turns it into a colour by scanning this table - a bounded scan over fixed
 * data, which is neither parsing nor unbounded work, and therefore not what the compile boundary
 * exists to keep off a device.
 *
 * **Linear RGBA, straight alpha, in 0..1**, and `float` rather than the `ColorRgba8` a vertex
 * carries. Both are parity decisions rather than local preferences: TrustSC's `THEME_COLORS` is
 * `&[(&str, [f32; 4])]` and its `resolve_color_token()` returns `Option<[f32; 4]>`, so this is the
 * same value in the same space. Converting to the `R8G8B8A8_UNORM` form `mdux.draw` records is the
 * adapter's step, and is deliberately not folded in here - a table that stored bytes would have
 * chosen a rounding rule that the two projects could then disagree about silently.
 */
struct ThemeColor {
    std::string_view     token;    ///< the full name, e.g. `Theme.Colors.ScoreDigits`
    std::array<float, 4> value{};  ///< linear RGBA, straight alpha, each channel in 0..1

    [[nodiscard]] constexpr bool operator==(const ThemeColor&) const noexcept = default;
};

enum class ThemeError : std::uint8_t {
    MalformedToken,  ///< the name is not of the form `Theme.Colors.<Token>`
    UnknownToken,    ///< well-formed, but the governed table does not define it
};

[[nodiscard]] constexpr std::string_view describe(ThemeError error) noexcept {
    switch (error) {
        case ThemeError::MalformedToken:
            return "the colour is not a Theme.Colors.<Token> name";
        case ThemeError::UnknownToken:
            return "the governed colour table does not define this token";
    }
    return "unknown theme error";
}

/**
 * @brief The governed token to RGBA table: the single approved source of truth for what a name
 *        renders as.
 *
 * Entry for entry, and value for value, TrustSC's `THEME_COLORS` (its ADR-014). That is the point
 * rather than an implementation detail: the parity programme's purpose is that one `.medui` screen
 * means the same thing in both projects, and a screen means a different thing if the same token
 * renders a different colour. A rendered-truth check (#16) comparing a tint against this table is
 * comparing against the same numbers TrustSC's verifier uses.
 *
 * Consequently this table is **not** a product input. An earlier revision of this module left the
 * contents to the caller on the argument that a palette is a clinical decision; TrustSC settles it
 * the other way, in its governed crate, and the parity criterion decides between the two. A product
 * that needs a different palette is a change to make in both projects at once, not one to make here
 * by supplying a different span.
 */
inline constexpr std::array<ThemeColor, 8> themeColors{
    ThemeColor{.token = "Theme.Colors.TopbarBackground", .value = {0.82F, 0.84F, 0.86F, 1.0F}},
    ThemeColor{           .token = "Theme.Colors.Title", .value = {0.10F, 0.12F, 0.16F, 1.0F}},
    ThemeColor{     .token = "Theme.Colors.ScoreDigits", .value = {0.13F, 0.72F, 0.42F, 1.0F}},
    ThemeColor{         .token = "Theme.Colors.Nominal", .value = {0.13F, 0.72F, 0.42F, 1.0F}},
    ThemeColor{           .token = "Theme.Colors.Alert", .value = {0.95F, 0.65F, 0.15F, 1.0F}},
    ThemeColor{           .token = "Theme.Colors.Fault", .value = {0.86F, 0.20F, 0.18F, 1.0F}},
    ThemeColor{         .token = "Theme.Colors.Neutral", .value = {0.62F, 0.66F, 0.70F, 1.0F}},
    ThemeColor{   .token = "Theme.Colors.PrimaryAction", .value = {0.16F, 0.44F, 0.86F, 1.0F}}
};

/**
 * @brief Whether `token` has the shape a colour name must have.
 *
 * The prefix, then a non-empty suffix of the characters an identifier may contain - ASCII letters,
 * digits, `_` and `-` - with `.` admitted between segments, because the parser builds a dotted path
 * and a name such as `Theme.Colors.Status.Ok` is expressible. No empty segment: a trailing dot or a
 * `..` names nothing.
 *
 * Shape is not existence. A well-formed token may still be absent from the governed table, which is
 * what `resolveColorToken()` reports separately - and the distinction matters, because a malformed
 * name is an emitter defect while an absent one is a table that does not define it.
 */
[[nodiscard]] constexpr bool isColorToken(std::string_view token) noexcept {
    if (!token.starts_with(colorTokenPrefix) || token.size() == colorTokenPrefix.size()) {
        return false;
    }

    bool segmentEmpty = true;
    for (const char character : token.substr(colorTokenPrefix.size())) {
        if (character == '.') {
            if (segmentEmpty) {
                return false;
            }
            segmentEmpty = true;
            continue;
        }
        const bool admitted = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')
                              || character == '_' || character == '-';
        if (!admitted) {
            return false;
        }
        segmentEmpty = false;
    }
    return !segmentEmpty;
}

/**
 * @brief Resolves a name a compiled screen carries against the governed table.
 *
 * A bounded scan with a `Result` on miss - never an allocation, never a throw, as ADR-011 requires
 * and as `mdux-governed-lint` and `governed.noThrow.symbolScan` hold this module to. Linear, like
 * TrustSC's `resolve_color_token()`, over a table of eight entries.
 *
 * Two errors rather than one, which is where this parts from the sibling's `Option`: a name that is
 * not a name at all is an emitter defect, and a well-formed name the table does not define is a
 * screen compiled against a different palette. Both are misses to a caller that only asks whether
 * it got a colour, and they need different people to fix them.
 */
[[nodiscard]] constexpr mdux::core::Result<std::array<float, 4>, ThemeError> resolveColorToken(std::string_view token) noexcept {
    if (!isColorToken(token)) {
        return mdux::core::err(ThemeError::MalformedToken);
    }
    for (const ThemeColor& entry : themeColors) {
        if (entry.token == token) {
            return entry.value;
        }
    }
    return mdux::core::err(ThemeError::UnknownToken);
}

/**
 * @brief One node's absolute rectangle, in integer surface pixels.
 *
 * Integer throughout, as ADR-011 decision 5 requires: the solver never divides into a fraction, so
 * two toolchains cannot disagree about where a rectangle is. Signed rather than unsigned because
 * `mdux::core::Px` is signed and a mixed-signedness comparison is a defect waiting for a reviewer.
 */
struct NodeRect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t width{0};
    std::int32_t height{0};

    [[nodiscard]] constexpr bool operator==(const NodeRect&) const noexcept = default;
};

/**
 * @brief What one component needs the device to know about it, one type per component.
 *
 * The dictionary is closed and each entry admits a different set of fields, so a single record with
 * every field on it would carry a `format` for a `Label` and a `stateKeys` for a `Clock`. An earlier
 * revision of this module did exactly that, and it was worse than untidy: it was **lossy**. A flat
 * node had nowhere to put a `Clock`'s format, a `NumericDisplay`'s template and source, a
 * `StatusIndicator`'s state keys and per-state tints, a `CriticalButton`'s `on_press`, or a
 * `VulkanViewport`'s stream - so a device holding the package could not have rendered four of the
 * eleven components at all.
 *
 * Every field is a **validated name**, never a resolved value, exactly as `colorToken` is. The
 * compiler has already proved each one resolves - keys against every approved locale (#193), colour
 * tokens against the governed table, named values against the tables a build supplies (#195) - so
 * the device performs bounded lookups and no parsing.
 *
 * One consequence worth stating because it diverges from the sibling: `format`, `charset` and
 * `onPress` stay `std::string_view` rather than becoming enumerations. TrustSC closes those sets in
 * its own crate (`ClockFormat`, `SystemEvent`); MduX settled in #195 that such named values resolve
 * against tables a *product* supplies, so closing them here would make this library authoritative
 * over a set it does not own. The package therefore carries names throughout, which is also the one
 * rule a reader has to remember about it.
 */
/**
 * @brief The wall-clock renderings a screen may ask a `Clock` for.
 *
 * Closed, and this library's to own, which is a deliberate exception to the rule that named values
 * resolve against tables a product supplies (#195). TrustSC closes the same set in its governed
 * crate, and the parity criterion decides it; what makes the exception defensible on its own terms
 * is that a closed set lets the *compiler* know what a clock renders - `TimeSeconds` is `HH:MM:SS`,
 * eight glyphs of known width - and therefore lets it measure a clock against its bounds. An open
 * name cannot be measured, only looked up.
 *
 * `charset` on a `TextInput` deliberately stays a name: TrustSC leaves its equivalent
 * (`glyph_set_id`) open too, because a glyph set is a product's to define.
 */
enum class ClockFormat : std::uint8_t {
    TimeSeconds,      ///< `HH:MM:SS`
    DateTimeSeconds,  ///< `YYYY-MM-DD HH:MM:SS`
};

/**
 * @brief What a `CriticalButton` asks the platform to do when it is pressed.
 *
 * Two members, and deliberately not a product's list of actions: a screen that could name any event
 * could name one nothing implements, and the press of a critical button is the worst place to find
 * that out. A product's own actions reach the device through `source:` and the realtime path, not
 * through this.
 */
enum class SystemEvent : std::uint8_t {
    NoOp,
    TriggerHalt,
};

/// The spelling the emitter writes and a reader recognises, e.g. `TimeSeconds`.
[[nodiscard]] constexpr std::string_view spell(ClockFormat format) noexcept {
    return format == ClockFormat::TimeSeconds ? "TimeSeconds" : "DateTimeSeconds";
}

/// The spelling the emitter writes, e.g. `NoOp`. A `.medui` source writes `SystemEvent.NoOp`; that
/// mapping is the compiler's, and the package carries the bare enumerator.
[[nodiscard]] constexpr std::string_view spell(SystemEvent event) noexcept {
    return event == SystemEvent::NoOp ? "NoOp" : "TriggerHalt";
}

/// The format `name` spells, or nothing when it is not one of the closed set.
[[nodiscard]] constexpr std::optional<ClockFormat> parseClockFormat(std::string_view name) noexcept {
    if (name == spell(ClockFormat::TimeSeconds)) {
        return ClockFormat::TimeSeconds;
    }
    if (name == spell(ClockFormat::DateTimeSeconds)) {
        return ClockFormat::DateTimeSeconds;
    }
    return std::nullopt;
}

/// The event `name` spells, or nothing when it is not one of the closed set.
[[nodiscard]] constexpr std::optional<SystemEvent> parseSystemEvent(std::string_view name) noexcept {
    if (name == spell(SystemEvent::NoOp)) {
        return SystemEvent::NoOp;
    }
    if (name == spell(SystemEvent::TriggerHalt)) {
        return SystemEvent::TriggerHalt;
    }
    return std::nullopt;
}

// Every spec member carries a default initialiser. `-Wmissing-field-initializers` is an error in
// this tree, and the emitter (#197) writes these initialisers from a screen - a type whose optional
// fields still have to be spelled out at every site is a trap for generated code, and an empty name
// is what "absent" means for all of them anyway.
struct PanelSpec {
    std::string_view colorToken{};  ///< the Row background that produced this synthetic node

    [[nodiscard]] constexpr bool operator==(const PanelSpec&) const noexcept = default;
};

struct LabelSpec {
    std::string_view textKey{};
    std::string_view colorToken{};

    [[nodiscard]] constexpr bool operator==(const LabelSpec&) const noexcept = default;
};

struct ClockSpec {
    ClockFormat format{};

    [[nodiscard]] constexpr bool operator==(const ClockSpec&) const noexcept = default;
};

struct ImageSpec {
    std::string_view source{};  ///< the baked image package's id, from `img("ID")`

    [[nodiscard]] constexpr bool operator==(const ImageSpec&) const noexcept = default;
};

struct VulkanViewportSpec {
    std::string_view streamSource{};

    [[nodiscard]] constexpr bool operator==(const VulkanViewportSpec&) const noexcept = default;
};

struct SignalTraceSpec {
    std::string_view streamSource{};
    std::string_view colorToken{};

    [[nodiscard]] constexpr bool operator==(const SignalTraceSpec&) const noexcept = default;
};

struct ButtonSpec {
    std::string_view labelKey{};
    std::string_view colorToken{};
    std::string_view source{};
    std::string_view requirement{};  ///< optional on a Button; empty when it declares none

    [[nodiscard]] constexpr bool operator==(const ButtonSpec&) const noexcept = default;
};

struct CriticalButtonSpec {
    std::string_view requirement{};  ///< required by the dictionary, and by #196's annotation rule
    std::string_view labelKey{};
    std::string_view colorToken{};
    SystemEvent      onPress{};

    [[nodiscard]] constexpr bool operator==(const CriticalButtonSpec&) const noexcept = default;
};

struct NumericDisplaySpec {
    std::string_view requirement{};
    std::string_view templateId{};  ///< `template:` in the source; `template` is a keyword here
    std::string_view source{};
    std::string_view colorToken{};

    [[nodiscard]] constexpr bool operator==(const NumericDisplaySpec&) const noexcept = default;
};

/**
 * @brief A status indicator's states, and the tint each one shows.
 *
 * `stateKeys` and `colorTokens` are parallel spans over storage the generated translation unit owns.
 * `colorTokens` is either empty - the component declares no per-state tint - or exactly as long as
 * `stateKeys`, which `validate()` enforces, because a shorter list would leave a state with no tint
 * and no way to say which.
 *
 * This is where per-state colour belongs, and settling it here answers the question #196 left open:
 * a golden reference pins one tint, so it refuses `ColorHash` on a node whose tint varies. The
 * variation lives in the node, not in the expectation.
 */
struct StatusIndicatorSpec {
    std::string_view                  requirement{};
    std::string_view                  source{};
    std::span<const std::string_view> stateKeys{};
    std::span<const std::string_view> colorTokens{};

    [[nodiscard]] constexpr bool operator==(const StatusIndicatorSpec& other) const noexcept {
        return requirement == other.requirement && source == other.source && std::ranges::equal(stateKeys, other.stateKeys)
               && std::ranges::equal(colorTokens, other.colorTokens);
    }
};

struct TextInputSpec {
    std::string_view source{};
    std::string_view colorToken{};
    std::int64_t     maxLength{0};
    std::string_view charset{};      ///< empty when the component narrows nothing
    std::string_view requirement{};  ///< optional on a TextInput

    [[nodiscard]] constexpr bool operator==(const TextInputSpec&) const noexcept = default;
};

/**
 * @brief The payload of one compiled node: exactly one component's spec.
 *
 * `std::variant` rather than a hand-rolled union, and accessed only through `std::holds_alternative`
 * and `std::get_if`. Both are `noexcept` and `constexpr`; `std::get` is deliberately never used
 * anywhere in this module or its consumers, because it throws on the wrong alternative and ADR-005
 * does not allow a governed type to have a throwing accessor as its natural spelling.
 *
 * The alternative order is *not* a wire contract. Nothing serialises the index: the emitter writes
 * the component's dictionary name, which `kindName()` returns, so an alternative may be added
 * without renumbering an artifact.
 */
using NodePayload = std::variant<PanelSpec,
                                 LabelSpec,
                                 ClockSpec,
                                 ImageSpec,
                                 VulkanViewportSpec,
                                 SignalTraceSpec,
                                 ButtonSpec,
                                 CriticalButtonSpec,
                                 NumericDisplaySpec,
                                 StatusIndicatorSpec,
                                 TextInputSpec>;

/**
 * @brief One compiled node: where it is, and everything the device needs to draw it.
 *
 * `id` and `bounds` are common to every component because a golden reference, a requirement trace
 * and the layout solver all address a node the same way. Everything else is the component's own.
 *
 * Deliberately *not* here: whether the source carried `@safety_critical` or an explicit `position:`.
 * A revision of this module carried both so that golden completeness could be re-derived from the
 * committed package; ADR-012 decision 4 now takes TrustSC's arrangement instead - one pass applies
 * the predicate once, and the tests that guard it live with the predicate (#196) rather than with the
 * artifact. A device therefore carries nothing a verifier reads, which is what decision 4 said in the
 * first place.
 */
struct CompiledNode {
    std::string_view id{};
    NodeRect         bounds{};
    NodePayload      payload{PanelSpec{}};

    [[nodiscard]] constexpr bool operator==(const CompiledNode&) const noexcept = default;
};

/// The dictionary name of a node's component, e.g. `Label`. The spelling an emitter writes and a
/// reader recognises; see `NodePayload` for why the variant's index is not that spelling.
[[nodiscard]] constexpr std::string_view kindName(const CompiledNode& node) noexcept {
    if (std::holds_alternative<PanelSpec>(node.payload)) {
        return "Panel";
    }
    if (std::holds_alternative<LabelSpec>(node.payload)) {
        return "Label";
    }
    if (std::holds_alternative<ClockSpec>(node.payload)) {
        return "Clock";
    }
    if (std::holds_alternative<ImageSpec>(node.payload)) {
        return "Image";
    }
    if (std::holds_alternative<VulkanViewportSpec>(node.payload)) {
        return "VulkanViewport";
    }
    if (std::holds_alternative<SignalTraceSpec>(node.payload)) {
        return "SignalTrace";
    }
    if (std::holds_alternative<ButtonSpec>(node.payload)) {
        return "Button";
    }
    if (std::holds_alternative<CriticalButtonSpec>(node.payload)) {
        return "CriticalButton";
    }
    if (std::holds_alternative<NumericDisplaySpec>(node.payload)) {
        return "NumericDisplay";
    }
    if (std::holds_alternative<StatusIndicatorSpec>(node.payload)) {
        return "StatusIndicator";
    }
    if (std::holds_alternative<TextInputSpec>(node.payload)) {
        return "TextInput";
    }
    // An alternative this function does not know, or a valueless payload. Named as nothing rather
    // than as the last kind checked: labelling an unknown component `TextInput` is how a package
    // acquires a plausible wrong answer, and `validate()` refuses the same case outright.
    return {};
}

// Adding an alternative to `NodePayload` without teaching `kindName()` and `validatePayload()` about
// it would leave a node that names itself nothing and fails validation - which is fail-closed, but
// only discovered at run time. This makes it a build failure at the point of the change instead.
static_assert(std::variant_size_v<NodePayload> == 11, "an alternative was added or removed: update kindName() and validatePayload() to match");

/**
 * @brief The requirement a node is traced to, or empty when it declares none.
 *
 * Five components can carry one, and #196 refuses `@safety_critical` without it - so this is the
 * function a traceability export walks rather than a field every node pretends to have.
 */
[[nodiscard]] constexpr std::string_view requirementOf(const CompiledNode& node) noexcept {
    if (const auto* spec = std::get_if<ButtonSpec>(&node.payload)) {
        return spec->requirement;
    }
    if (const auto* spec = std::get_if<CriticalButtonSpec>(&node.payload)) {
        return spec->requirement;
    }
    if (const auto* spec = std::get_if<NumericDisplaySpec>(&node.payload)) {
        return spec->requirement;
    }
    if (const auto* spec = std::get_if<StatusIndicatorSpec>(&node.payload)) {
        return spec->requirement;
    }
    if (const auto* spec = std::get_if<TextInputSpec>(&node.payload)) {
        return spec->requirement;
    }
    return {};
}

/**
 * @brief A whole compiled screen as generated code exposes it and the runtime consumes it.
 *
 * Non-owning and `constexpr`-constructible throughout, so a generated translation unit can place one
 * in read-only memory and `static_assert` that it validates.
 */
struct ScreenPackage {
    std::string_view              id;
    std::uint64_t                 schemaVersion{evidence::kSchemaVersion};
    std::int32_t                  surfaceWidth{0};
    std::int32_t                  surfaceHeight{0};
    std::span<const CompiledNode> nodes;
    mdux::draw::DrawBudget        budget{};

    /// Checks every invariant a consumer is entitled to assume. See the module comment for the one
    /// invariant it deliberately leaves to the compiler: whether the budget is *large enough*.
    [[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> validate() const noexcept;

    /// The node with this id, or nullptr. Linear: a screen holds tens of nodes, and a map would
    /// cost more to build than every lookup it could serve - and could not be `constexpr` data.
    [[nodiscard]] constexpr const CompiledNode* find(std::string_view nodeId) const noexcept {
        for (const CompiledNode& node : nodes) {
            if (node.id == nodeId) {
                return &node;
            }
        }
        return nullptr;
    }
};

/// Whether `bounds` lies wholly inside a `width` x `height` surface.
///
/// Computed in 64-bit arithmetic: `x + width` on two `std::int32_t` at their extremes overflows,
/// and an overflowed comparison would admit exactly the rectangle this is written to refuse.
[[nodiscard]] constexpr bool containedBy(NodeRect bounds, std::int32_t width, std::int32_t height) noexcept {
    if (bounds.x < 0 || bounds.y < 0) {
        return false;
    }
    const std::int64_t right  = static_cast<std::int64_t>(bounds.x) + bounds.width;
    const std::int64_t bottom = static_cast<std::int64_t>(bounds.y) + bounds.height;
    return right <= width && bottom <= height;
}

/**
 * @brief Checks one colour name through the resolver the device will use.
 *
 * The resolver rather than the shape rule alone, so a screen that validates cannot fail the device
 * lookup for any reason `validate()` could have seen. The two errors stay apart because they are
 * different people's defects: a malformed name is the emitter's, and an unknown one is a screen
 * compiled against a palette this build does not have.
 */
[[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> checkColor(std::string_view token) noexcept {
    if (token.empty()) {
        return {};
    }
    const auto resolved = resolveColorToken(token);
    if (resolved.has_value()) {
        return {};
    }
    return mdux::core::err(resolved.error() == ThemeError::MalformedToken ? SchemaError::MalformedColorToken : SchemaError::UnknownColorToken);
}

/// Requires a colour the dictionary marks required: present, and resolving through the table.
///
/// Worth separating from `checkColor()`, which permits an absent one. The dictionary makes `color`
/// required on `Label`, `Button`, `CriticalButton`, `SignalTrace`, `NumericDisplay` and `TextInput`,
/// and optional nowhere except `StatusIndicator`'s per-state list - a distinction the flat node this
/// replaces could not express, since one shared field cannot be required for some components and
/// absent for others.
[[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> requireColor(std::string_view token) noexcept;

/// Requires a name the dictionary marks required.
[[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> require(std::string_view name) noexcept {
    return name.empty() ? mdux::core::ResultVoid<SchemaError>{mdux::core::err(SchemaError::EmptyRequiredName)} : mdux::core::ResultVoid<SchemaError>{};
}

/**
 * @brief Checks one node's payload against what its component's dictionary entry requires.
 *
 * Only what the dictionary already fixes, and nothing this module invents on its own: a name a
 * component must declare is non-empty, every colour resolves, a status indicator has states to show,
 * and its per-state tints - if it declares any - pair one-to-one with them.
 *
 * Optional fields are checked when present and ignored when empty, because "absent" is a legal
 * value the dictionary allows for `requirement` on a `Button` or `charset` on a `TextInput`.
 */
[[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> requireColor(std::string_view token) noexcept {
    if (const auto named = require(token); !named.has_value()) {
        return named;
    }
    return checkColor(token);
}

[[nodiscard]] constexpr mdux::core::ResultVoid<SchemaError> validatePayload(const NodePayload& payload) noexcept {
    if (const auto* spec = std::get_if<PanelSpec>(&payload)) {
        // A Panel exists because a Row declared a background, so it always has one.
        return requireColor(spec->colorToken);
    }
    if (const auto* spec = std::get_if<LabelSpec>(&payload)) {
        if (const auto named = require(spec->textKey); !named.has_value()) {
            return named;
        }
        return requireColor(spec->colorToken);
    }
    if (std::holds_alternative<ClockSpec>(payload)) {
        // Nothing to check. `format` is a closed enumeration, so an unrepresentable value cannot be
        // built - which is most of the argument for closing it rather than carrying a name.
        return {};
    }
    if (const auto* spec = std::get_if<ImageSpec>(&payload)) {
        return require(spec->source);
    }
    if (const auto* spec = std::get_if<VulkanViewportSpec>(&payload)) {
        return require(spec->streamSource);
    }
    if (const auto* spec = std::get_if<SignalTraceSpec>(&payload)) {
        if (const auto named = require(spec->streamSource); !named.has_value()) {
            return named;
        }
        return requireColor(spec->colorToken);
    }
    if (const auto* spec = std::get_if<ButtonSpec>(&payload)) {
        if (const auto named = require(spec->labelKey); !named.has_value()) {
            return named;
        }
        if (const auto named = require(spec->source); !named.has_value()) {
            return named;
        }
        return requireColor(spec->colorToken);
    }
    if (const auto* spec = std::get_if<CriticalButtonSpec>(&payload)) {
        for (const std::string_view name : {spec->requirement, spec->labelKey}) {
            if (const auto named = require(name); !named.has_value()) {
                return named;
            }
        }
        return requireColor(spec->colorToken);
    }
    if (const auto* spec = std::get_if<NumericDisplaySpec>(&payload)) {
        for (const std::string_view name : {spec->requirement, spec->templateId, spec->source}) {
            if (const auto named = require(name); !named.has_value()) {
                return named;
            }
        }
        return requireColor(spec->colorToken);
    }
    if (const auto* spec = std::get_if<StatusIndicatorSpec>(&payload)) {
        for (const std::string_view name : {spec->requirement, spec->source}) {
            if (const auto named = require(name); !named.has_value()) {
                return named;
            }
        }
        if (spec->stateKeys.empty()) {
            return mdux::core::err(SchemaError::NoStates);
        }
        for (const std::string_view key : spec->stateKeys) {
            if (const auto named = require(key); !named.has_value()) {
                return named;
            }
        }
        // Empty is legal - the component declares no per-state tint. Anything else has to pair with
        // the states one for one, because a shorter list leaves a state with no tint and no way to
        // say which state that is.
        if (!spec->colorTokens.empty() && spec->colorTokens.size() != spec->stateKeys.size()) {
            return mdux::core::err(SchemaError::StateColorCountMismatch);
        }
        for (const std::string_view token : spec->colorTokens) {
            if (const auto colour = requireColor(token); !colour.has_value()) {
                return colour;
            }
        }
        return {};
    }
    // Fail closed. An earlier revision returned success here, which made this a fail-open contract:
    // a payload left valueless by an exceptional emplacement, or an alternative added without
    // teaching this function about it, would have been accepted as a valid node by the one function
    // consumers are entitled to trust. `std::visit` would give exhaustiveness, but it throws on a
    // valueless variant, which ADR-005 does not allow here - so the residual case is named instead,
    // and the `static_assert` above turns "an alternative was added" into a build failure.
    const auto* spec = std::get_if<TextInputSpec>(&payload);
    if (spec == nullptr) {
        return mdux::core::err(SchemaError::UnknownPayload);
    }
    if (const auto named = require(spec->source); !named.has_value()) {
        return named;
    }
    if (spec->maxLength <= 0) {
        return mdux::core::err(SchemaError::NonPositiveMaxLength);
    }
    return requireColor(spec->colorToken);
}

constexpr mdux::core::ResultVoid<SchemaError> ScreenPackage::validate() const noexcept {
    using mdux::core::err;

    if (schemaVersion != evidence::kSchemaVersion) {
        return err(SchemaError::UnsupportedSchemaVersion);
    }
    if (id.empty()) {
        return err(SchemaError::EmptyId);
    }
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return err(SchemaError::NonPositiveSurface);
    }

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const CompiledNode& node = nodes[index];

        if (node.id.empty()) {
            return err(SchemaError::EmptyNodeId);
        }
        // Quadratic, and deliberately so: a screen holds tens of nodes, `constexpr` evaluation has
        // no allocator, and a sorted copy would need one. The alternative is not checking.
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (nodes[earlier].id == node.id) {
                return err(SchemaError::DuplicateNodeId);
            }
        }
        if (node.bounds.width <= 0 || node.bounds.height <= 0) {
            return err(SchemaError::DegenerateBounds);
        }
        if (!containedBy(node.bounds, surfaceWidth, surfaceHeight)) {
            return err(SchemaError::BoundsOutsideSurface);
        }
        if (const auto payload = validatePayload(node.payload); !payload.has_value()) {
            return payload;
        }
    }

    if (budget.maxVertices > mdux::draw::maxIndexableVertices) {
        return err(SchemaError::BudgetExceedsIndexWidth);
    }
    // A screen with nothing to draw may carry an empty budget; one with nodes may not, because the
    // first rectangle it records would be refused and the frame would silently be blank.
    if (!nodes.empty() && (budget.maxVertices == 0 || budget.maxIndices == 0 || budget.maxCommands == 0)) {
        return err(SchemaError::EmptyBudget);
    }

    return {};
}

}  // namespace mdux::medui
