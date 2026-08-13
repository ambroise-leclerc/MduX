/**
 * @file Parser.cppm
 * @brief Recursive-descent `.medui` parser: tokens to an unresolved AST.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, hand-written, one token of lookahead. What it enforces and what it deliberately does
 * not are both worth stating, because the boundary is the design.
 *
 * ## What it enforces
 *
 * - The shape: one `Screen`, an optional `layout:` and `surface:`, then nodes with fields.
 * - **No control flow.** `if`, `else`, `for`, `while`, `loop`, `match`, `fn`, `let`, `return` and
 *   `import` are rejected with `MDX-E016` wherever they appear. ADR-011 decision 5: each makes the
 *   primitive count depend on data the compiler cannot see, and a `DrawBudget` that cannot be
 *   computed exactly is not a budget.
 * - **No nested `Row`** (`MDX-E015`). ADR-011 is explicit that this one is a *solver* restriction,
 *   not a budget one - a nested Row's depth is visible and the primitive count stays exact - so the
 *   diagnostic says flatten it, and does not cite the budget argument that does not apply.
 * - **No duplicate node ids within a screen** (`MDX-E014`). Ids address nodes in golden references
 *   (#196) and requirement traces, so a duplicate makes those ambiguous.
 *
 * ## What it does not
 *
 * Component names, field names and theme tokens are carried unresolved. The component dictionary
 * and the theme table belong to #193, and a parser that consulted them would have to change every
 * time a component is added. See `Ast.cppm` for the argument in full.
 *
 * ## Recovery
 *
 * On an unexpected token the parser skips to the next `;` or `}` and continues, so one run reports
 * several problems rather than the first. A compiler that stops at the first error makes fixing a
 * screen an iterative guessing game, and #200's `mdux-medui-check` exists precisely to give an
 * author everything at once.
 */
module;

export module mdux.tools.medui.parser;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

/// The parsed screen, or the diagnostics that stopped it parsing.
struct ParseResult {
    std::optional<ast::Screen> screen;
    std::vector<cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return screen.has_value() && diagnostics.empty(); }
};

/**
 * @brief Lexes and parses `source`, attributing diagnostics to `file`.
 *
 * Never throws for malformed input. A `screen` is returned whenever enough parsed to be worth
 * examining, *even alongside diagnostics* - #200 wants to report a text-budget problem and a
 * syntax problem in the same run where it can. Callers must therefore check `ok()` or the
 * diagnostics, not merely whether `screen` is engaged.
 */
[[nodiscard]] ParseResult parse(std::string_view source, std::string file);

}  // namespace mdux::tools::medui
