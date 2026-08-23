/**
 * @file Lexer.cppm
 * @brief Hand-written `.medui` lexer: source text to positioned tokens.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, like the rest of the compiler. Hand-written rather than generated, for the reason
 * `mdux.tools.truetype` and `mdux.tools.toml` are: a generator is a SOUP entry, a build-time
 * dependency and a second language in the tree, and this grammar is small enough that none of that
 * buys anything. It is also the shape a reviewer can read.
 *
 * ## Every token carries a line and a column
 *
 * Not a convenience. The shared diagnostic envelope (#118) defines `line` and `column` as
 * independent, 1-based, with 0 meaning "no precise position on this axis" - and #192 requires a
 * fixture to assert the position rather than only the code. A lexer that tracked lines alone would
 * make that impossible for every diagnostic downstream, so positions are carried from the first
 * stage rather than reconstructed later.
 *
 * Columns count UTF-8 *bytes*, not code points, and the header comment on `Token` says so. A
 * multi-byte character before a token therefore shifts its reported column. That is the same
 * convention `mdux-docs-lint` and `mdux-governed-lint` use, and matching them matters more than
 * being clever: an agent consuming the shared envelope should not need to know which tool produced
 * a finding to interpret its column.
 *
 * ## Comments
 *
 * `//` to end of line. The `medui-authoring` skill's grammar block does not show a comment, and
 * this is the one place the lexer accepts something the skill does not illustrate. It is included
 * deliberately: a screen carries `requirement:` strings and `@safety_critical` annotations, and a
 * language for describing safety-relevant UI in which an author cannot write down *why* is a
 * language that pushes that reasoning somewhere the compiler never sees. Recorded here rather than
 * assumed, so the decision is reviewable; if it is rejected, deleting `skipComment()` and the
 * fixture that covers it is the whole change.
 *
 * Block comments are deliberately absent. They need nesting rules, they invite commented-out
 * screens, and nothing here needs them.
 */
module;

export module mdux.tools.medui.lexer;

import std;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;

export namespace mdux::tools::medui {

/**
 * @brief What a token is.
 *
 * Deliberately small. The parser decides what an identifier *means* - `Screen`, `Row`, a component
 * name, a field name and a theme token are all `Identifier` here - because a lexer that classified
 * them would need the component dictionary, and the dictionary belongs to a later stage (#193).
 * Keeping that knowledge out means adding a component never touches this file.
 */
enum class TokenKind : std::uint8_t {
    Identifier,   ///< `Screen`, `NumericDisplay`, `width`, `Fill`, `sedation-index`
    Number,       ///< an integer literal; `px` arrives as a separate Identifier token
    String,       ///< `"REQ-NS-001"`, with the quotes removed and escapes resolved
    At,           ///< `@`, opening an annotation
    LBrace,       ///< `{`
    RBrace,       ///< `}`
    LParen,       ///< `(`
    RParen,       ///< `)`
    LBracket,     ///< `[`
    RBracket,     ///< `]`
    Colon,        ///< `:`
    Semicolon,    ///< `;`
    Comma,        ///< `,`
    Dot,          ///< `.`, joining `Theme.Colors.<Token>`
    EndOfFile,
};

[[nodiscard]] std::string_view describe(TokenKind kind) noexcept;

/**
 * @brief One token, with where it started.
 *
 * `line` and `column` are 1-based, matching the shared envelope. `column` counts UTF-8 bytes from
 * the line start, not code points - see the file header.
 *
 * `text` borrows from the source buffer for identifiers and numbers, and owns for strings, whose
 * escapes are resolved during lexing. One `std::string` either way keeps the type simple; this is
 * a host tool compiling one screen, and a borrow-versus-own union would be optimising the wrong
 * thing.
 */
struct Token {
    TokenKind kind{TokenKind::EndOfFile};
    std::string text;
    std::size_t line{0};
    std::size_t column{0};
};

/// The tokens of one source, or the diagnostics that stopped it being tokenised.
struct LexResult {
    std::vector<Token> tokens;
    std::vector<cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

/**
 * @brief Tokenises `source`, attributing every diagnostic to `file`.
 *
 * Never throws for malformed input: a bad character or an unterminated string becomes a
 * diagnostic, because a compiler that throws on the input it exists to reject is a compiler that
 * cannot report more than one problem per run. It lexes as far as it can, so a run reports several
 * findings rather than one at a time.
 *
 * A source that is not valid UTF-8 is rejected whole, with `MEDUI-E004`. Continuing past that would
 * mean reporting columns into a byte sequence that has no defined character boundaries.
 */
[[nodiscard]] LexResult lex(std::string_view source, std::string file);

}  // namespace mdux::tools::medui
