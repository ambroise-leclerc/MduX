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
 * surface, nodes with unique ids and rectangles that lie inside that surface, colour tokens that are
 * names rather than values, and a budget the index width can address.
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
import mdux.core.units;
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
 * @brief One compiled node: what it draws, where, and what it is traced to.
 *
 * `textKey` and `colorToken` are the validated *names* ADR-011 carries rather than the values they
 * resolve to - the compiler has already proved the key exists in every approved locale (#193) and
 * that the token is in the governed table, so the device performs a bounded lookup and no parse.
 *
 * `requirement` is empty for a node that declares none, and non-empty for every node that is
 * safety-critical, because #196 refuses the annotation without it. Carrying it here is what makes
 * the requirement-to-node mapping diffable in a committed artifact.
 */
struct CompiledNode {
    std::string_view id;
    std::string_view component;    ///< the dictionary name, e.g. `Label`
    NodeRect         bounds{};
    std::string_view textKey;      ///< empty when the node draws no static text
    std::string_view colorToken;   ///< empty when the node declares no tint
    std::string_view requirement;  ///< empty when the node declares none

    [[nodiscard]] constexpr bool operator==(const CompiledNode&) const noexcept = default;
};

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
        // The same shape rule the resolver applies, so a screen that validates here cannot fail the
        // device lookup for a reason `validate()` could have seen. `Theme.Colors.` names nothing and
        // `Theme.Colors.#` is not a name at all; both would otherwise pass the generated
        // `static_assert` and fail only where nobody is watching.
        if (!node.colorToken.empty() && !isColorToken(node.colorToken)) {
            return err(SchemaError::MalformedColorToken);
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
