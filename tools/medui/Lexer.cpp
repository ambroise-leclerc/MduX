/**
 * @file Lexer.cpp
 * @brief The `.medui` lexer.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * One pass, one character of lookahead, no backtracking. The grammar does not need more, and a
 * lexer a reviewer can hold in their head is worth more here than one that is clever.
 */
module;

module mdux.tools.medui.lexer;

import std;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;

namespace mdux::tools::medui {

namespace {

/// An identifier body. Hyphens are included because node ids use them - `id: sedation-index;` in
/// the skill's own example - and excluding them would force quotes on every id.
[[nodiscard]] constexpr bool isIdentifierChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

/// An identifier start. A digit cannot begin one, or `1920px` would lex as one identifier rather
/// than a number followed by a unit - and the parser needs those separate to reject `1920rem`.
[[nodiscard]] constexpr bool isIdentifierStart(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] constexpr bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

/**
 * @brief Validates UTF-8 and reports the byte offset of the first bad sequence.
 *
 * Written out rather than delegated: the standard library has no UTF-8 validator, and the
 * alternative is a SOUP entry for one function. Rejects overlong encodings, surrogates and
 * out-of-range code points, because "it decoded to something" is not the same as "it was valid" -
 * an overlong NUL is the classic way a filter is bypassed, and this text ends up in a committed
 * artifact.
 */
[[nodiscard]] std::optional<std::size_t> firstInvalidUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        std::uint32_t lowest = 0;

        if (b0 < 0x80) {
            i += 1;
            continue;
        }
        if ((b0 & 0xE0u) == 0xC0u) { extra = 1; cp = b0 & 0x1Fu; lowest = 0x80; }
        else if ((b0 & 0xF0u) == 0xE0u) { extra = 2; cp = b0 & 0x0Fu; lowest = 0x800; }
        else if ((b0 & 0xF8u) == 0xF0u) { extra = 3; cp = b0 & 0x07u; lowest = 0x10000; }
        else { return i; }

        if (i + extra >= s.size()) {
            return i;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(s[i + k]);
            if ((bk & 0xC0u) != 0x80u) {
                return i;
            }
            cp = (cp << 6) | (bk & 0x3Fu);
        }
        if (cp < lowest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return i;  // overlong, out of range, or a surrogate
        }
        i += extra + 1;
    }
    return std::nullopt;
}

/// Tracks the cursor and its 1-based position, so every token can be stamped without recomputing.
class Cursor {
public:
    explicit Cursor(std::string_view source) noexcept : source_{source} {}

    [[nodiscard]] bool atEnd() const noexcept { return offset_ >= source_.size(); }
    [[nodiscard]] char peek() const noexcept { return atEnd() ? '\0' : source_[offset_]; }
    [[nodiscard]] char peekNext() const noexcept {
        return offset_ + 1 < source_.size() ? source_[offset_ + 1] : '\0';
    }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

    char advance() noexcept {
        const char c = peek();
        ++offset_;
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

private:
    std::string_view source_;
    std::size_t offset_{0};
    std::size_t line_{1};
    std::size_t column_{1};
};

}  // namespace

std::string_view describe(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Identifier: return "an identifier";
        case TokenKind::Number:     return "a number";
        case TokenKind::String:     return "a string";
        case TokenKind::At:         return "'@'";
        case TokenKind::LBrace:     return "'{'";
        case TokenKind::RBrace:     return "'}'";
        case TokenKind::LParen:     return "'('";
        case TokenKind::RParen:     return "')'";
        case TokenKind::LBracket:   return "'['";
        case TokenKind::RBracket:   return "']'";
        case TokenKind::Colon:      return "':'";
        case TokenKind::Semicolon:  return "';'";
        case TokenKind::Comma:      return "','";
        case TokenKind::Dot:        return "'.'";
        case TokenKind::EndOfFile:  return "end of file";
    }
    return "an unknown token";
}

LexResult lex(std::string_view source, std::string file) {
    LexResult result;

    if (const std::optional<std::size_t> bad = firstInvalidUtf8(source)) {
        // Rejected whole rather than at the offending byte. Past an invalid sequence there are no
        // defined character boundaries, so every column after it would be a guess.
        result.diagnostics.push_back(diagnose(
            Code::SourceNotUtf8, std::move(file), 0, 0,
            std::format("not valid UTF-8: the first invalid byte sequence begins at offset {}",
                        *bad)));
        return result;
    }

    Cursor cursor{source};
    const auto push = [&result](TokenKind kind, std::string text, std::size_t line,
                                std::size_t column) {
        result.tokens.push_back(Token{.kind = kind,
                                      .text = std::move(text),
                                      .line = line,
                                      .column = column});
    };

    while (!cursor.atEnd()) {
        const char c = cursor.peek();

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            static_cast<void>(cursor.advance());
            continue;
        }
        if (c == '/' && cursor.peekNext() == '/') {
            while (!cursor.atEnd() && cursor.peek() != '\n') {
                static_cast<void>(cursor.advance());
            }
            continue;
        }

        const std::size_t line = cursor.line();
        const std::size_t column = cursor.column();

        if (isIdentifierStart(c)) {
            const std::size_t start = cursor.offset();
            while (!cursor.atEnd() && isIdentifierChar(cursor.peek())) {
                static_cast<void>(cursor.advance());
            }
            push(TokenKind::Identifier,
                 std::string{source.substr(start, cursor.offset() - start)}, line, column);
            continue;
        }

        if (isDigit(c)) {
            const std::size_t start = cursor.offset();
            while (!cursor.atEnd() && isDigit(cursor.peek())) {
                static_cast<void>(cursor.advance());
            }
            push(TokenKind::Number, std::string{source.substr(start, cursor.offset() - start)},
                 line, column);
            continue;
        }

        if (c == '"') {
            static_cast<void>(cursor.advance());  // the opening quote
            std::string value;
            bool terminated = false;
            while (!cursor.atEnd()) {
                const char ch = cursor.peek();
                if (ch == '"') {
                    static_cast<void>(cursor.advance());
                    terminated = true;
                    break;
                }
                if (ch == '\n') {
                    break;  // a string does not span lines; report it at the opening quote
                }
                if (ch == '\\') {
                    static_cast<void>(cursor.advance());
                    const char esc = cursor.peek();
                    switch (esc) {
                        case '"':  value += '"';  static_cast<void>(cursor.advance()); break;
                        case '\\': value += '\\'; static_cast<void>(cursor.advance()); break;
                        case 'n':  value += '\n'; static_cast<void>(cursor.advance()); break;
                        case 't':  value += '\t'; static_cast<void>(cursor.advance()); break;
                        default:
                            // An unknown escape is an error rather than a literal backslash. A
                            // silently-kept `\d` in a requirement id is the kind of thing that
                            // survives review and then fails a trace years later.
                            result.diagnostics.push_back(diagnose(
                                Code::UnexpectedToken, file, cursor.line(), cursor.column(),
                                std::format("unknown escape '\\{}' in a string",
                                            esc == '\0' ? '?' : esc),
                                "the supported escapes are \\\" \\\\ \\n and \\t"));
                            static_cast<void>(cursor.advance());
                            break;
                    }
                    continue;
                }
                value += cursor.advance();
            }
            if (!terminated) {
                result.diagnostics.push_back(diagnose(
                    Code::UnexpectedToken, file, line, column, "unterminated string",
                    "add the closing quote; a string may not span lines"));
                continue;
            }
            push(TokenKind::String, std::move(value), line, column);
            continue;
        }

        const auto single = [&](TokenKind kind) {
            static_cast<void>(cursor.advance());
            push(kind, std::string{c}, line, column);
        };

        switch (c) {
            case '@': single(TokenKind::At);        continue;
            case '{': single(TokenKind::LBrace);    continue;
            case '}': single(TokenKind::RBrace);    continue;
            case '(': single(TokenKind::LParen);    continue;
            case ')': single(TokenKind::RParen);    continue;
            case '[': single(TokenKind::LBracket);  continue;
            case ']': single(TokenKind::RBracket);  continue;
            case ':': single(TokenKind::Colon);     continue;
            case ';': single(TokenKind::Semicolon); continue;
            case ',': single(TokenKind::Comma);     continue;
            case '.': single(TokenKind::Dot);       continue;
            default: break;
        }

        result.diagnostics.push_back(diagnose(
            Code::UnexpectedToken, file, line, column,
            std::format("unexpected character '{}'", c)));
        static_cast<void>(cursor.advance());
    }

    push(TokenKind::EndOfFile, {}, cursor.line(), cursor.column());
    return result;
}

}  // namespace mdux::tools::medui
