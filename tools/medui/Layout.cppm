/**
 * @brief Integer-only build-time layout resolution for `.medui` screens.
 * @file Layout.cppm
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * This host-only stage turns the parser's unresolved Vertical/Row tree into a flat sequence of
 * absolute rectangles. It owns no device behavior: later compiler stages consume this result,
 * while the governed runtime receives only emitted, already-resolved layout.
 */
module;

export module mdux.tools.medui.layout;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

/// Build-selected surface dimensions. A source-level `surface:` declaration, when present, must
/// agree with these values.
struct LayoutInputs {
    std::int64_t surfaceWidth{0};
    std::int64_t surfaceHeight{0};
};

/// An absolute rectangle in integer surface pixels.
struct LayoutRect {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t width{0};
    std::int64_t height{0};

    auto operator<=>(const LayoutRect&) const = default;
};

/**
 * @brief One flat compiled-layout entry.
 *
 * `source` is an owned copy, not a pointer into the input screen, so a result remains valid after
 * its AST is released. For an authored leaf, `id` and `component` match `source`. A Row background
 * is represented as a synthetic `Panel`: `source` is the originating Row and `id` is
 * `<row-id>-background`.
 */
struct ResolvedNode {
    std::string id;
    std::string component;
    LayoutRect  bounds{};
    ast::Node   source;
    bool        positioned{false};
    bool        synthetic{false};
};

/// Flat nodes and any diagnostic that prevented a complete layout. `nodes` is empty whenever a
/// diagnostic is present, so a caller cannot accidentally consume a partial screen.
struct LayoutResult {
    std::int64_t                              surfaceWidth{0};
    std::int64_t                              surfaceHeight{0};
    std::vector<ResolvedNode>                 nodes;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Resolves a semantically-valid screen to flat absolute rectangles.
 *
 * The algorithm uses integer arithmetic only. Multiple `Fill` items receive equal integer shares;
 * an indivisible trailing remainder stays unused, matching the TrustSC reference behavior. Flow
 * overflow, a `Fill` that resolves to no space, containment failures, and positioned overlaps are
 * diagnostics; dimensions are never clamped.
 * Positioned nodes are removed from flow. Their coordinates are absolute surface coordinates;
 * top-level nodes must remain inside the padded content box, and Row children inside the Row's
 * already-resolved absolute band. A Row contributes one vertical flow item and its children are
 * resolved horizontally in the same single pass.
 *
 * The parser already rejects nested Row. Because the AST is public and can be built directly,
 * this function checks that invariant and throws `std::logic_error` if a caller bypassed the
 * parser. Other malformed-AST conditions are treated the same way: semantic analysis is a
 * required gate before layout.
 */
[[nodiscard]] LayoutResult resolveLayout(const ast::Screen& screen, std::string file, LayoutInputs inputs);

}  // namespace mdux::tools::medui
