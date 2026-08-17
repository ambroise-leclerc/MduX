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
import mdux.draw;
import mdux.evidence.report;

export namespace mdux::medui {

/// The `<kind>` component of `generated/<kind>/<id>/`, and the value of a package's `kind` member.
inline constexpr std::string_view packageKind = "screen";

/// The prefix every colour a node draws with must carry. The package holds names, never values.
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
        if (!node.colorToken.empty() && !node.colorToken.starts_with(colorTokenPrefix)) {
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
