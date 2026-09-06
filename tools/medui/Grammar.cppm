/**
 * @file Grammar.cppm
 * @brief The `.medui` contract in machine-readable form, and the explanation of one diagnostic code.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON, stable output)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, like the rest of the compiler. What this module publishes is the other half of #118: a
 * stable diagnostic envelope tells an agent *what went wrong* in a form it need not parse prose to
 * read, and this tells it *what the language is* in the same form. Between them they are the
 * difference between an agent guessing at the DSL and being handed its contract.
 *
 * ## Derived, not restated
 *
 * The rule #263 sets is that "a hand-maintained copy would drift, and a grammar that disagrees with
 * the parser is worse than none". So every section that *can* be read off a table is:
 *
 * | Section | Read from |
 * |---|---|
 * | `tokens` | `TokenKind` and `describe(TokenKind)` |
 * | `components` | `componentDictionary()`, which semantic analysis owns |
 * | `fieldDomains` | `FieldDomain`, through a switch with no default |
 * | `namedValues` | `mdux.medui.schema`'s `toWire()` tables |
 * | `themeTokens` | `mdux::medui::themeColors` |
 * | `diagnostics` | `registry()` and `retired()` |
 *
 * Adding a component, a field, a domain, a closed-set member, a theme token or a diagnostic changes
 * this document without anybody editing it, which is the only arrangement under which the document
 * can be believed.
 *
 * ## The one section that is written rather than derived, and how it is held honest
 *
 * `productions` is prose. The parser is hand-written recursive descent (`Parser.cpp`), so there is
 * no production table to emit from - a generator would have been one, and `Lexer.cppm` records why
 * this project declines generators for a grammar this size.
 *
 * What holds it to the parser is that **every production carries executable examples**: sources it
 * accepts, and sources it rejects together with the code the rejection must carry.
 * `GrammarTests.cpp` runs the real lexer, parser and analyzer over every one of them, so a
 * production whose rule stops describing the implementation fails a test rather than misleading a
 * reader. The limit is worth stating rather than discovering: that makes the *examples* verified and
 * the EBNF text checked only as far as they reach. It is not a proof that the rule is complete.
 *
 * ## What is deliberately absent
 *
 * The pinned upstream contract - repository, version, commit - is **not** embedded here.
 * `medui-conformance.toml` is that pin, it is already machine-readable, and a second copy compiled
 * into a tool is exactly the drift this issue exists to prevent. An agent that needs to know which
 * contract version this grammar realises reads that file.
 *
 * ## Stability
 *
 * The output is canonical JSON through `mdux.evidence.json`, so members are sorted and the encoding
 * is fixed. It carries no timestamp, no absolute path, no build identity and no host detail:
 * ADR-007 decision 5's reasoning is about any artifact a tool emits, not only about baked ones, and
 * a contract document that changed byte for byte between two runs of the same commit could not be
 * diffed by the agent it exists to serve.
 */
module;

export module mdux.tools.medui.grammar;

import std;
import mdux.evidence.json;

export namespace mdux::tools::medui {

/// The schema version of the emitted document, bumped when a section is added, removed or reshaped.
///
/// Separate from the diagnostic registry's own stability rule: a consumer keyed to `MEDUI-E030`
/// should not have to re-read this document when a section it does not use grows a member.
inline constexpr std::uint64_t grammarSchemaVersion = 1;

/**
 * @brief The whole contract as a canonical JSON object.
 *
 * Pure: it reads the compiler's own tables and touches no file, no clock and no environment. Two
 * calls in one process, and two runs of one binary, produce the same value.
 */
[[nodiscard]] mdux::evidence::json::Value grammar();

/**
 * @brief `grammar()` serialised as canonical JSON text.
 *
 * Canonical form already ends with a newline, so the returned string is exactly what the committed
 * file holds - `mdux-meduic --grammar > docs/medui/grammar.json` is the whole publication step.
 *
 * @throws std::logic_error if the document cannot be serialised, which would mean this module built
 *         a value `mdux.evidence.json` refuses - a defect here rather than anything a caller did.
 */
[[nodiscard]] std::string grammarJson();

/**
 * @brief The registered explanation of one published diagnostic code, or nothing.
 *
 * `code` is the published identifier - `"MEDUI-E030"` - or its one-release compatibility alias,
 * `"MDX-E030"`, because an agent that pinned the old spelling should get an answer rather than a
 * shrug while the alias is still documented.
 *
 * **Nothing, rather than an empty explanation, for a code no row names.** That is #263's own
 * acceptance and it is the point of having a registry: a tool that answered every input with
 * something would tell an agent that `MEDUI-E999` is a real code with nothing to say about it, which
 * is worse than saying it does not exist. `retired()` numbers are reported as retired rather than as
 * unknown, so a consumer pinned to one learns which of the two it is met.
 */
[[nodiscard]] std::optional<std::string> explain(std::string_view code);

}  // namespace mdux::tools::medui
