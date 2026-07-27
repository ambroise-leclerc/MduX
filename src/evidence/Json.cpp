/**
 * @file Json.cpp
 * @brief Canonical JSON writer and strict reader for the governed evidence zone.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Two halves that must agree exactly: write() produces canonical form, parse() accepts
 * canonical form and very little else. Any divergence between them shows up as a round-trip
 * test failure, which is why the tests round-trip every shape rather than checking each half
 * in isolation.
 */
module;

module mdux.evidence.json;

import std;
import mdux.core.result;

namespace mdux::evidence::json {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;

namespace {

[[nodiscard]] Error makeError(ErrorCode code, std::size_t offset, std::string detail) noexcept {
    return Error{.code = code, .offset = offset, .detail = std::move(detail)};
}

// ---------------------------------------------------------------------------
// UTF-8 validation
// ---------------------------------------------------------------------------

/// Validates `text` as UTF-8, rejecting overlong encodings, surrogates and out-of-range code
/// points - the three classes a naive length-driven decoder waves through. Returns the byte
/// offset of the first invalid sequence, or npos if the whole string is well-formed.
///
/// This matters for byte-identity as much as for correctness: a writer that emits an invalid
/// sequence produces a file whose bytes depend on whatever produced the bad input, and the
/// promise is UTF-8 without a byte-order mark, not "whatever bytes were handed to us".
[[nodiscard]] std::size_t findInvalidUtf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte0 = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t codePoint = 0;

        if (byte0 < 0x80) {
            i += 1;
            continue;
        }
        if ((byte0 & 0xe0u) == 0xc0u) {
            length = 2;
            codePoint = byte0 & 0x1fu;
        } else if ((byte0 & 0xf0u) == 0xe0u) {
            length = 3;
            codePoint = byte0 & 0x0fu;
        } else if ((byte0 & 0xf8u) == 0xf0u) {
            length = 4;
            codePoint = byte0 & 0x07u;
        } else {
            return i;  // continuation byte or 0xf8+ as a lead byte
        }

        if (i + length > text.size()) {
            return i;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xc0u) != 0x80u) {
                return i;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3fu);
        }

        // Overlong: a code point encodable in fewer bytes than were used.
        static constexpr std::array<std::uint32_t, 5> minimum{0, 0, 0x80, 0x800, 0x10000};
        if (codePoint < minimum[length]) {
            return i;
        }
        // UTF-16 surrogate halves are not valid scalar values in UTF-8.
        if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
            return i;
        }
        if (codePoint > 0x10ffff) {
            return i;
        }
        i += length;
    }
    return std::string_view::npos;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

/// JSON string escaping, fixed so that the same input always produces the same bytes.
///
/// Escapes exactly what RFC 8259 requires - quote, backslash, and everything below 0x20 -
/// using the two-character shorthand where one exists and \u00XX otherwise. Notably does *not*
/// escape `/` (legal but optional, and escaping it is a common source of two writers
/// disagreeing) and does not escape non-ASCII, which stays raw UTF-8.
void appendEscaped(std::string& out, std::string_view text) noexcept {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                constexpr std::string_view digits = "0123456789abcdef";
                const auto value = static_cast<unsigned char>(c);
                out += "\\u00";
                out.push_back(digits[(value >> 4) & 0x0fu]);
                out.push_back(digits[value & 0x0fu]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

/// Integers are rendered by hand rather than through std::format, so that no locale, no
/// formatting library version and no floating-point path can influence the bytes. std::to_chars
/// would also be correct here; doing it manually keeps the whole writer free of any
/// number-formatting facility, which is easier to state as an invariant than to audit for.
void appendUnsigned(std::string& out, std::uint64_t value) noexcept {
    std::array<char, 20> buffer{};
    std::size_t length = 0;
    do {
        buffer[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    for (std::size_t i = length; i > 0; --i) {
        out.push_back(buffer[i - 1]);
    }
}

void appendSigned(std::string& out, std::int64_t value) noexcept {
    if (value < 0) {
        out.push_back('-');
        // Negate through the unsigned domain so INT64_MIN does not overflow.
        appendUnsigned(out, ~static_cast<std::uint64_t>(value) + 1u);
    } else {
        appendUnsigned(out, static_cast<std::uint64_t>(value));
    }
}

void appendIndent(std::string& out, std::size_t depth) noexcept {
    out.append(depth * 2, ' ');
}

ResultVoid<Error> writeValue(std::string& out, const Value& value, std::size_t depth) noexcept;

/// Emits a float as the canonical `{"bits": N}` object, laid out exactly as a hand-built object
/// of the same shape would be, so the two are indistinguishable in the output.
void writeFloat32(std::string& out, std::uint32_t bits, std::size_t depth) noexcept {
    out += "{\n";
    appendIndent(out, depth + 1);
    out += "\"bits\": ";
    appendUnsigned(out, bits);
    out.push_back('\n');
    appendIndent(out, depth);
    out.push_back('}');
}

ResultVoid<Error> writeValue(std::string& out, const Value& value, std::size_t depth) noexcept {
    if (depth > kMaxDepth) {
        return err(makeError(ErrorCode::DepthExceeded, 0,
                              "value nests deeper than " + std::to_string(kMaxDepth) + " levels"));
    }

    switch (value.kind()) {
    case Value::Kind::Null:
        out += "null";
        return {};

    case Value::Kind::Bool:
        out += value.asBool().value_or(false) ? "true" : "false";
        return {};

    case Value::Kind::Int: {
        const auto result = value.asInt();
        if (!result.has_value()) {
            return err(result.error());
        }
        appendSigned(out, *result);
        return {};
    }

    case Value::Kind::UInt: {
        const auto result = value.asUInt();
        if (!result.has_value()) {
            return err(result.error());
        }
        appendUnsigned(out, *result);
        return {};
    }

    case Value::Kind::Float32: {
        const auto result = value.asFloat32();
        if (!result.has_value()) {
            return err(result.error());
        }
        writeFloat32(out, std::bit_cast<std::uint32_t>(*result), depth);
        return {};
    }

    case Value::Kind::String: {
        const auto result = value.asString();
        if (!result.has_value()) {
            return err(result.error());
        }
        if (const std::size_t bad = findInvalidUtf8(*result); bad != std::string_view::npos) {
            return err(makeError(ErrorCode::InvalidUtf8, 0,
                                  "string contains invalid UTF-8 at byte " + std::to_string(bad)));
        }
        appendEscaped(out, *result);
        return {};
    }

    case Value::Kind::Array: {
        const std::span<const Value> elements = value.elements();
        if (elements.empty()) {
            out += "[]";
            return {};
        }
        out += "[\n";
        for (std::size_t i = 0; i < elements.size(); ++i) {
            appendIndent(out, depth + 1);
            if (auto written = writeValue(out, elements[i], depth + 1); !written.has_value()) {
                return written;
            }
            out += (i + 1 < elements.size()) ? ",\n" : "\n";
        }
        appendIndent(out, depth);
        out.push_back(']');
        return {};
    }

    case Value::Kind::Object: {
        const std::span<const Member> members = value.members();
        if (members.empty()) {
            out += "{}";
            return {};
        }
        // Members are kept sorted by set(), but sort a local index here anyway rather than
        // trusting that invariant: a Value produced by parse() or by a future construction path
        // must serialize canonically too, and the cost is negligible at these sizes.
        std::vector<const Member*> ordered;
        ordered.reserve(members.size());
        for (const Member& member : members) {
            ordered.push_back(&member);
        }
        std::ranges::stable_sort(ordered, [](const Member* a, const Member* b) {
            // std::string_view's comparison is by unsigned char value, which for UTF-8 is the
            // same order as by code point - so "sorted by UTF-8 code unit" needs no special
            // collation, and must not acquire any.
            return std::string_view{a->key} < std::string_view{b->key};
        });
        for (std::size_t i = 1; i < ordered.size(); ++i) {
            if (ordered[i - 1]->key == ordered[i]->key) {
                return err(makeError(ErrorCode::DuplicateKey, 0,
                                      "object holds two members named '" + ordered[i]->key + "'"));
            }
        }

        out += "{\n";
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const Member& member = *ordered[i];
            if (const std::size_t bad = findInvalidUtf8(member.key);
                bad != std::string_view::npos) {
                return err(makeError(ErrorCode::InvalidUtf8, 0,
                                      "object key contains invalid UTF-8 at byte " +
                                          std::to_string(bad)));
            }
            appendIndent(out, depth + 1);
            appendEscaped(out, member.key);
            out += ": ";
            if (auto written = writeValue(out, member.value, depth + 1); !written.has_value()) {
                return written;
            }
            out += (i + 1 < ordered.size()) ? ",\n" : "\n";
        }
        appendIndent(out, depth);
        out.push_back('}');
        return {};
    }
    }

    return err(makeError(ErrorCode::WrongKind, 0, "value has no recognized kind"));
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

/// Recursive-descent parser over a string_view. Holds no allocation of its own beyond the Values
/// it builds, and every failure path produces an offset so a diagnostic can point at the byte.
class Parser {
public:
    explicit Parser(std::string_view text) noexcept : text_{text} {}

    [[nodiscard]] Result<Value, Error> run() noexcept {
        // A byte-order mark is valid UTF-8 and invisible in an editor, which makes it exactly
        // the kind of difference that would break byte-identity while looking like nothing.
        if (text_.starts_with("\xef\xbb\xbf")) {
            return err(makeError(ErrorCode::ByteOrderMarkRejected, 0,
                                  "input begins with a UTF-8 byte-order mark"));
        }
        if (const std::size_t bad = findInvalidUtf8(text_); bad != std::string_view::npos) {
            return err(makeError(ErrorCode::InvalidUtf8, bad, "invalid UTF-8 sequence"));
        }

        skipWhitespace();
        auto value = parseValue(0);
        if (!value.has_value()) {
            return value;
        }
        skipWhitespace();
        if (position_ != text_.size()) {
            return err(makeError(ErrorCode::TrailingContent, position_,
                                  "unexpected content after the top-level value"));
        }
        return value;
    }

private:
    [[nodiscard]] bool atEnd() const noexcept { return position_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return text_[position_]; }

    /// Skips space, tab, CR and LF. Deliberately not a general "skip anything non-token" - a
    /// comment must reach parseValue() so it can be reported as CommentRejected rather than
    /// silently ignored.
    void skipWhitespace() noexcept {
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                return;
            }
        }
    }

    [[nodiscard]] Error unexpected(std::string detail) const noexcept {
        if (position_ >= text_.size()) {
            return makeError(ErrorCode::UnexpectedEnd, position_, std::move(detail));
        }
        return makeError(ErrorCode::UnexpectedCharacter, position_, std::move(detail));
    }

    /// Rejects both comment syntaxes explicitly. JSON has no comments; a file containing one was
    /// written or edited by hand, which is the case this reader exists to catch.
    [[nodiscard]] std::optional<Error> rejectComment() const noexcept {
        if (!atEnd() && peek() == '/') {
            return makeError(ErrorCode::CommentRejected, position_,
                              "comments are not valid JSON and are rejected");
        }
        return std::nullopt;
    }

    [[nodiscard]] Result<Value, Error> parseValue(std::size_t depth) noexcept {
        if (depth > kMaxDepth) {
            return err(makeError(ErrorCode::DepthExceeded, position_,
                                  "nesting exceeds " + std::to_string(kMaxDepth) + " levels"));
        }
        if (atEnd()) {
            return err(makeError(ErrorCode::UnexpectedEnd, position_, "expected a value"));
        }
        if (auto comment = rejectComment(); comment.has_value()) {
            return err(*comment);
        }

        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': {
            auto text = parseString();
            if (!text.has_value()) {
                return err(text.error());
            }
            return Value::string(std::move(*text));
        }
        case 't':
            return expectLiteral("true", Value::boolean(true));
        case 'f':
            return expectLiteral("false", Value::boolean(false));
        case 'n':
            // Distinguish `null` from `nan` before the generic literal check, so the nonfinite
            // case gets its own error code rather than a confusing "expected null".
            if (text_.substr(position_).starts_with("nan")) {
                return err(makeError(ErrorCode::NonFiniteLiteralRejected, position_,
                                      "NaN is not valid JSON and is rejected"));
            }
            return expectLiteral("null", Value::null());
        case 'N':
            return err(makeError(ErrorCode::NonFiniteLiteralRejected, position_,
                                  "NaN is not valid JSON and is rejected"));
        case 'I':
            return err(makeError(ErrorCode::NonFiniteLiteralRejected, position_,
                                  "Infinity is not valid JSON and is rejected"));
        default:
            if (peek() == '-' && text_.substr(position_).starts_with("-Infinity")) {
                return err(makeError(ErrorCode::NonFiniteLiteralRejected, position_,
                                      "-Infinity is not valid JSON and is rejected"));
            }
            return parseNumber();
        }
    }

    [[nodiscard]] Result<Value, Error> expectLiteral(std::string_view literal,
                                                      Value result) noexcept {
        if (!text_.substr(position_).starts_with(literal)) {
            return err(unexpected("expected '" + std::string{literal} + "'"));
        }
        position_ += literal.size();
        return result;
    }

    [[nodiscard]] Result<Value, Error> parseNumber() noexcept {
        const std::size_t start = position_;
        const bool negative = !atEnd() && peek() == '-';
        if (negative) {
            ++position_;
        }
        if (atEnd() || peek() < '0' || peek() > '9') {
            return err(unexpected("expected a digit"));
        }
        // Leading zeros are invalid JSON and would also break byte-identity, since "01" and "1"
        // denote the same number but differ as bytes.
        if (peek() == '0' && position_ + 1 < text_.size()) {
            const char next = text_[position_ + 1];
            if (next >= '0' && next <= '9') {
                return err(makeError(ErrorCode::InvalidNumber, position_,
                                      "number has a leading zero"));
            }
        }
        const std::size_t digitsStart = position_;
        while (!atEnd() && peek() >= '0' && peek() <= '9') {
            ++position_;
        }
        if (!atEnd() && (peek() == '.' || peek() == 'e' || peek() == 'E')) {
            return err(makeError(ErrorCode::FractionalNumberRejected, position_,
                                  "canonical form encodes real numbers as {\"bits\": N}; a "
                                  "fraction or exponent here means the file was not written by "
                                  "this writer"));
        }

        const std::string_view digits = text_.substr(digitsStart, position_ - digitsStart);
        std::uint64_t magnitude = 0;
        for (const char c : digits) {
            const auto digit = static_cast<std::uint64_t>(c - '0');
            // Overflow check before multiplying, so an oversized literal is a clean error
            // rather than a wrapped value that would silently corrupt an artifact.
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
                return err(makeError(ErrorCode::NumberOutOfRange, start,
                                      "integer does not fit in 64 bits"));
            }
            magnitude = magnitude * 10u + digit;
        }

        if (!negative) {
            return Value::unsignedInteger(magnitude);
        }
        // -0 is representable but denotes the same value as 0 and would give two byte sequences
        // for one number, so canonical form has no place for it.
        if (magnitude == 0) {
            return err(makeError(ErrorCode::InvalidNumber, start,
                                  "negative zero has no canonical representation"));
        }
        constexpr auto minMagnitude =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u;
        if (magnitude > minMagnitude) {
            return err(makeError(ErrorCode::NumberOutOfRange, start,
                                  "negative integer does not fit in 64 bits"));
        }
        if (magnitude == minMagnitude) {
            return Value::integer(std::numeric_limits<std::int64_t>::min());
        }
        return Value::integer(-static_cast<std::int64_t>(magnitude));
    }

    [[nodiscard]] Result<std::string, Error> parseString() noexcept {
        if (atEnd() || peek() != '"') {
            return err(unexpected("expected '\"'"));
        }
        ++position_;
        std::string out;
        while (true) {
            if (atEnd()) {
                return err(makeError(ErrorCode::UnexpectedEnd, position_,
                                      "string is not terminated"));
            }
            const char c = peek();
            if (c == '"') {
                ++position_;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return err(makeError(ErrorCode::UnescapedControlCharacter, position_,
                                      "control character in a string must be escaped"));
            }
            if (c != '\\') {
                out.push_back(c);
                ++position_;
                continue;
            }

            ++position_;  // consume the backslash
            if (atEnd()) {
                return err(makeError(ErrorCode::UnexpectedEnd, position_,
                                      "string ends inside an escape sequence"));
            }
            const char escape = peek();
            ++position_;
            switch (escape) {
            case '"':  out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/'); break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                auto decoded = parseUnicodeEscape();
                if (!decoded.has_value()) {
                    return err(decoded.error());
                }
                appendUtf8(out, *decoded);
                break;
            }
            default:
                return err(makeError(ErrorCode::InvalidEscape, position_ - 1,
                                      "unrecognized escape '\\" + std::string(1, escape) + "'"));
            }
        }
    }

    /// Reads the four hex digits after `\u`, handling a surrogate pair as one code point. The
    /// writer never emits `\u` above 0x1f, but a hand-written recipe or a third-party tool might,
    /// and rejecting it outright would be stricter than the format requires.
    [[nodiscard]] Result<std::uint32_t, Error> parseUnicodeEscape() noexcept {
        auto readFourHex = [this]() -> Result<std::uint32_t, Error> {
            if (position_ + 4 > text_.size()) {
                return err(makeError(ErrorCode::InvalidEscape, position_,
                                      "\\u needs four hexadecimal digits"));
            }
            std::uint32_t value = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                const char c = text_[position_ + i];
                std::uint32_t digit = 0;
                if (c >= '0' && c <= '9') {
                    digit = static_cast<std::uint32_t>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    digit = static_cast<std::uint32_t>(c - 'a') + 10u;
                } else if (c >= 'A' && c <= 'F') {
                    digit = static_cast<std::uint32_t>(c - 'A') + 10u;
                } else {
                    return err(makeError(ErrorCode::InvalidEscape, position_ + i,
                                          "\\u needs four hexadecimal digits"));
                }
                value = (value << 4) | digit;
            }
            position_ += 4;
            return value;
        };

        auto high = readFourHex();
        if (!high.has_value()) {
            return high;
        }
        if (*high < 0xd800 || *high > 0xdbff) {
            if (*high >= 0xdc00 && *high <= 0xdfff) {
                return err(makeError(ErrorCode::InvalidEscape, position_,
                                      "unpaired low surrogate in a \\u escape"));
            }
            return high;
        }
        if (!text_.substr(position_).starts_with("\\u")) {
            return err(makeError(ErrorCode::InvalidEscape, position_,
                                  "high surrogate is not followed by a low surrogate"));
        }
        position_ += 2;
        auto low = readFourHex();
        if (!low.has_value()) {
            return low;
        }
        if (*low < 0xdc00 || *low > 0xdfff) {
            return err(makeError(ErrorCode::InvalidEscape, position_,
                                  "high surrogate is not followed by a low surrogate"));
        }
        return 0x10000u + ((*high - 0xd800u) << 10) + (*low - 0xdc00u);
    }

    static void appendUtf8(std::string& out, std::uint32_t codePoint) noexcept {
        if (codePoint < 0x80) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
            out.push_back(static_cast<char>(0xc0u | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
        } else if (codePoint < 0x10000) {
            out.push_back(static_cast<char>(0xe0u | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
        } else {
            out.push_back(static_cast<char>(0xf0u | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
        }
    }

    [[nodiscard]] Result<Value, Error> parseArray(std::size_t depth) noexcept {
        ++position_;  // consume '['
        std::vector<Value> elements;
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            ++position_;
            return Value::array(std::move(elements));
        }
        while (true) {
            skipWhitespace();
            if (!atEnd() && peek() == ']') {
                return err(makeError(ErrorCode::TrailingComma, position_,
                                      "trailing comma before ']'"));
            }
            auto element = parseValue(depth + 1);
            if (!element.has_value()) {
                return element;
            }
            elements.push_back(std::move(*element));

            skipWhitespace();
            if (atEnd()) {
                return err(makeError(ErrorCode::UnexpectedEnd, position_,
                                      "array is not terminated"));
            }
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == ']') {
                ++position_;
                return Value::array(std::move(elements));
            }
            return err(unexpected("expected ',' or ']'"));
        }
    }

    [[nodiscard]] Result<Value, Error> parseObject(std::size_t depth) noexcept {
        ++position_;  // consume '{'
        Value object = Value::emptyObject();
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            ++position_;
            return object;
        }
        while (true) {
            skipWhitespace();
            if (!atEnd() && peek() == '}') {
                return err(makeError(ErrorCode::TrailingComma, position_,
                                      "trailing comma before '}'"));
            }
            if (auto comment = rejectComment(); comment.has_value()) {
                return err(*comment);
            }
            const std::size_t keyOffset = position_;
            auto key = parseString();
            if (!key.has_value()) {
                return err(key.error());
            }

            skipWhitespace();
            if (atEnd() || peek() != ':') {
                return err(unexpected("expected ':' after an object key"));
            }
            ++position_;
            skipWhitespace();

            auto value = parseValue(depth + 1);
            if (!value.has_value()) {
                return value;
            }
            // set() rejects duplicates, which is where DuplicateKey comes from - but its error
            // has no offset, so re-report with the offending key's position.
            if (auto inserted = object.set(*key, std::move(*value)); !inserted.has_value()) {
                if (inserted.error().code == ErrorCode::DuplicateKey) {
                    return err(makeError(ErrorCode::DuplicateKey, keyOffset,
                                          "duplicate key '" + *key + "'"));
                }
                return err(inserted.error());
            }

            skipWhitespace();
            if (atEnd()) {
                return err(makeError(ErrorCode::UnexpectedEnd, position_,
                                      "object is not terminated"));
            }
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == '}') {
                ++position_;
                return object;
            }
            return err(unexpected("expected ',' or '}'"));
        }
    }

    std::string_view text_;
    std::size_t position_{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// describe()
// ---------------------------------------------------------------------------

std::string_view describe(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::UnexpectedEnd:            return "unexpected end of input";
    case ErrorCode::UnexpectedCharacter:      return "unexpected character";
    case ErrorCode::InvalidNumber:            return "invalid number";
    case ErrorCode::FractionalNumberRejected: return "fractional number rejected";
    case ErrorCode::NumberOutOfRange:         return "number out of range";
    case ErrorCode::NonFiniteLiteralRejected: return "non-finite literal rejected";
    case ErrorCode::DuplicateKey:             return "duplicate object key";
    case ErrorCode::TrailingComma:            return "trailing comma";
    case ErrorCode::CommentRejected:          return "comment rejected";
    case ErrorCode::InvalidEscape:            return "invalid escape sequence";
    case ErrorCode::UnescapedControlCharacter: return "unescaped control character";
    case ErrorCode::InvalidUtf8:              return "invalid UTF-8";
    case ErrorCode::ByteOrderMarkRejected:    return "byte-order mark rejected";
    case ErrorCode::TrailingContent:          return "trailing content";
    case ErrorCode::DepthExceeded:            return "nesting too deep";
    case ErrorCode::WrongKind:                return "wrong value kind";
    case ErrorCode::MissingMember:            return "missing object member";
    case ErrorCode::NotExactlyRepresentable:  return "not exactly representable";
    }
    return "unrecognized error";
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value Value::null() noexcept {
    return Value{};
}

Value Value::boolean(bool value) noexcept {
    Value result;
    result.kind_ = Kind::Bool;
    result.boolean_ = value;
    return result;
}

Value Value::integer(std::int64_t value) noexcept {
    Value result;
    result.kind_ = Kind::Int;
    result.int_ = value;
    return result;
}

Value Value::unsignedInteger(std::uint64_t value) noexcept {
    Value result;
    result.kind_ = Kind::UInt;
    result.uint_ = value;
    return result;
}

Value Value::float32(float value) noexcept {
    Value result;
    result.kind_ = Kind::Float32;
    result.floatBits_ = std::bit_cast<std::uint32_t>(value);
    return result;
}

Value Value::string(std::string value) noexcept {
    Value result;
    result.kind_ = Kind::String;
    result.string_ = std::move(value);
    return result;
}

Value Value::array(std::vector<Value> elements) noexcept {
    Value result;
    result.kind_ = Kind::Array;
    result.elements_ = std::move(elements);
    return result;
}

Value Value::emptyObject() noexcept {
    Value result;
    result.kind_ = Kind::Object;
    return result;
}

Result<bool, Error> Value::asBool() const noexcept {
    if (kind_ != Kind::Bool) {
        return err(makeError(ErrorCode::WrongKind, 0, "value is not a boolean"));
    }
    return boolean_;
}

Result<std::int64_t, Error> Value::asInt() const noexcept {
    if (kind_ == Kind::Int) {
        return int_;
    }
    if (kind_ == Kind::UInt) {
        // A parsed non-negative literal arrives as UInt; converting is legitimate exactly when
        // the value fits, which is the "exactly representable in the declared type" rule.
        if (uint_ > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return err(makeError(ErrorCode::NotExactlyRepresentable, 0,
                                  "unsigned value does not fit in a signed 64-bit integer"));
        }
        return static_cast<std::int64_t>(uint_);
    }
    return err(makeError(ErrorCode::WrongKind, 0, "value is not an integer"));
}

Result<std::uint64_t, Error> Value::asUInt() const noexcept {
    if (kind_ == Kind::UInt) {
        return uint_;
    }
    if (kind_ == Kind::Int) {
        if (int_ < 0) {
            return err(makeError(ErrorCode::NotExactlyRepresentable, 0,
                                  "negative value is not representable as unsigned"));
        }
        return static_cast<std::uint64_t>(int_);
    }
    return err(makeError(ErrorCode::WrongKind, 0, "value is not an integer"));
}

Result<std::string_view, Error> Value::asString() const noexcept {
    if (kind_ != Kind::String) {
        return err(makeError(ErrorCode::WrongKind, 0, "value is not a string"));
    }
    return std::string_view{string_};
}

Result<float, Error> Value::asFloat32() const noexcept {
    if (kind_ == Kind::Float32) {
        return std::bit_cast<float>(floatBits_);
    }
    if (kind_ != Kind::Object) {
        return err(makeError(ErrorCode::WrongKind, 0,
                              "value is neither a float nor a {\"bits\": N} object"));
    }
    if (members_.size() != 1 || members_.front().key != "bits") {
        return err(makeError(ErrorCode::WrongKind, 0,
                              "a canonical float is an object with exactly one member 'bits'"));
    }
    const auto bits = members_.front().value.asUInt();
    if (!bits.has_value()) {
        return err(bits.error());
    }
    if (*bits > std::numeric_limits<std::uint32_t>::max()) {
        return err(makeError(ErrorCode::NumberOutOfRange, 0,
                              "float bit pattern does not fit in 32 bits"));
    }
    return std::bit_cast<float>(static_cast<std::uint32_t>(*bits));
}

std::span<const Value> Value::elements() const noexcept {
    if (kind_ != Kind::Array) {
        return {};
    }
    return elements_;
}

std::span<const Member> Value::members() const noexcept {
    if (kind_ != Kind::Object) {
        return {};
    }
    return members_;
}

const Value* Value::find(std::string_view key) const noexcept {
    if (kind_ != Kind::Object) {
        return nullptr;
    }
    // members_ is kept sorted by set(), so a binary search is correct here.
    const auto position = std::ranges::lower_bound(
        members_, key, {}, [](const Member& member) { return std::string_view{member.key}; });
    if (position == members_.end() || std::string_view{position->key} != key) {
        return nullptr;
    }
    return &position->value;
}

Result<const Value*, Error> Value::require(std::string_view key) const noexcept {
    if (kind_ != Kind::Object) {
        return err(makeError(ErrorCode::WrongKind, 0, "value is not an object"));
    }
    if (const Value* found = find(key); found != nullptr) {
        return found;
    }
    return err(makeError(ErrorCode::MissingMember, 0,
                          "object has no member '" + std::string{key} + "'"));
}

ResultVoid<Error> Value::set(std::string key, Value value) noexcept {
    if (kind_ != Kind::Object) {
        return err(makeError(ErrorCode::WrongKind, 0, "value is not an object"));
    }
    const auto position = std::ranges::lower_bound(
        members_, std::string_view{key}, {},
        [](const Member& member) { return std::string_view{member.key}; });
    if (position != members_.end() && std::string_view{position->key} == std::string_view{key}) {
        return err(makeError(ErrorCode::DuplicateKey, 0, "object already has a member '" + key + "'"));
    }
    members_.insert(position, Member{.key = std::move(key), .value = std::move(value)});
    return {};
}

ResultVoid<Error> Value::push(Value value) noexcept {
    if (kind_ != Kind::Array) {
        return err(makeError(ErrorCode::WrongKind, 0, "value is not an array"));
    }
    elements_.push_back(std::move(value));
    return {};
}

// ---------------------------------------------------------------------------
// write() / parse()
// ---------------------------------------------------------------------------

Result<std::string, Error> write(const Value& value) noexcept {
    std::string out;
    if (auto written = writeValue(out, value, 0); !written.has_value()) {
        return err(written.error());
    }
    out.push_back('\n');  // canonical form ends with exactly one LF
    return out;
}

Result<Value, Error> parse(std::string_view text) noexcept {
    Parser parser{text};
    return parser.run();
}

}  // namespace mdux::evidence::json
