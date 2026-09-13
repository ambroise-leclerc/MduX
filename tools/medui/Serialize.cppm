/**
 * @file Serialize.cppm
 * @brief Turns a parsed `.medui` AST back into canonical source text.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-023 MedUI host editing API and round-trip source contract
 *
 * Host-only, and the missing half of `Parser.cpp`: nothing in `tools/medui/` previously turned an
 * `ast::Screen` back into text an author could read or a `mdux-meduic` invocation could reparse.
 * ADR-023 decision 3 is what this module is: **canonical, not lossless**. It always produces one
 * fixed rendering of a screen's content - never a preservation of the original file's exact
 * whitespace, quoting style or `//` comments, because `Ast.cppm` carries no trivia slot for any of
 * those and nothing here invents one.
 *
 * ADR-023 decision 4 is what makes that boundary safe rather than lossy in the way that matters:
 * every *semantic* thing the AST carries - fields (in the order the node stores them), every
 * `ast::Value` kind, every annotation and its arguments, and a field name the current component
 * dictionary does not even recognise - round-trips exactly, because this module walks the AST's own
 * generic shape rather than a fixed per-component field list. There is nothing here to consult that
 * could reject or drop an unfamiliar name.
 */
module;

export module mdux.tools.medui.serialize;

import std;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

/**
 * @brief Renders `screen` as canonical `.medui` source text.
 *
 * Pure: it reads `screen`'s own fields and touches no file, no clock and no environment. Two calls
 * over one screen, and a call over the result of reparsing that text, produce the same string -
 * `SerializeTests.cpp` pins this as the round-trip contract ADR-023 decision 4 states, not as an
 * incidental property.
 *
 * The one thing this function does not reproduce is a `//` comment from the original source: the
 * AST that reaches this function never carried one (ADR-023 decision 3). Every other syntactic
 * construct `Parser.cpp` accepts round-trips through this function unchanged.
 */
[[nodiscard]] std::string serializeScreen(const ast::Screen& screen);

}  // namespace mdux::tools::medui
