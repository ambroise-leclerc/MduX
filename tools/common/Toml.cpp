/**
 * @file Toml.cpp
 * @brief Implementation of the TOML-subset recipe reader.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Line-oriented: this subset has no construct spanning a newline except an array, which is
 * handled by joining continuation lines before parsing the value. That keeps the parser small
 * and every diagnostic anchored to a line an author can find.
 */
module;

module mdux.tools.toml;

import std;

namespace mdux::tools::toml {

namespace {

[[nodiscard]] bool isBareKeyChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const auto notSpace = [](char c) { return c != ' ' && c != '\t' && c != '\r'; };
    while (!text.empty() && !notSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && !notSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// Strips a trailing `#` comment, honouring quoting so a `#` inside a string survives.
[[nodiscard]] std::string_view stripComment(std::string_view line) noexcept {
    bool inString = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && inString) {
            ++i;  // skip the escaped character
            continue;
        }
        if (line[i] == '"') {
            inString = !inString;
            continue;
        }
        if (line[i] == '#' && !inString) {
            return line.substr(0, i);
        }
    }
    return line;
}

/// True when `text` has an unclosed `[` at top level, meaning the array continues on the next
/// line. Quote-aware, so a bracket inside a string does not count.
[[nodiscard]] bool hasUnclosedBracket(std::string_view text) noexcept {
    bool inString = false;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && inString) {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }
        if (text[i] == '[') {
            ++depth;
        } else if (text[i] == ']') {
            --depth;
        }
    }
    return depth > 0;
}

class ValueParser {
public:
    ValueParser(std::string_view text, std::size_t line) noexcept : text_{text}, line_{line} {}

    /// Parses one value and requires that nothing follows it.
    [[nodiscard]] Value parseComplete() {
        Value value = parseValue();
        skipSpace();
        if (position_ != text_.size()) {
            throw TomlError{line_, "unexpected text after the value: '" +
                                        std::string{trim(text_.substr(position_))} + "'"};
        }
        return value;
    }

private:
    void skipSpace() noexcept {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\n' ||
                text_[position_] == '\r')) {
            ++position_;
        }
    }

    [[nodiscard]] Value parseValue() {
        skipSpace();
        if (position_ >= text_.size()) {
            throw TomlError{line_, "expected a value"};
        }
        const char c = text_[position_];
        if (c == '"') {
            return Value::string(parseString(), line_);
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '\'') {
            throw TomlError{line_,
                            "literal (single-quoted) strings are outside this TOML subset; use a "
                            "double-quoted string"};
        }
        if (c == '{') {
            throw TomlError{line_, "inline tables are outside this TOML subset; use a [table] "
                                    "header instead"};
        }
        if (text_.substr(position_).starts_with("true")) {
            position_ += 4;
            return Value::boolean(true, line_);
        }
        if (text_.substr(position_).starts_with("false")) {
            position_ += 5;
            return Value::boolean(false, line_);
        }
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }
        throw TomlError{line_, "unrecognized value starting at '" +
                                    std::string{text_.substr(position_, 12)} + "'"};
    }

    [[nodiscard]] std::string parseString() {
        ++position_;  // opening quote
        std::string out;
        while (true) {
            if (position_ >= text_.size()) {
                throw TomlError{line_, "string is not terminated"};
            }
            const char c = text_[position_];
            if (c == '"') {
                ++position_;
                return out;
            }
            if (c == '\n') {
                throw TomlError{line_, "multi-line strings are outside this TOML subset"};
            }
            if (c != '\\') {
                out.push_back(c);
                ++position_;
                continue;
            }
            ++position_;
            if (position_ >= text_.size()) {
                throw TomlError{line_, "string ends inside an escape sequence"};
            }
            const char escape = text_[position_++];
            switch (escape) {
            case '"':  out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u':  appendUtf8(out, parseHex(4)); break;
            case 'U':  appendUtf8(out, parseHex(8)); break;
            default:
                throw TomlError{line_, std::string{"unrecognized escape '\\"} + escape + "'"};
            }
        }
    }

    [[nodiscard]] std::uint32_t parseHex(std::size_t digits) {
        if (position_ + digits > text_.size()) {
            throw TomlError{line_, "\\u escape needs " + std::to_string(digits) + " hex digits"};
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < digits; ++i) {
            const char c = text_[position_ + i];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a') + 10u;
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A') + 10u;
            } else {
                throw TomlError{line_,
                                "\\u escape needs " + std::to_string(digits) + " hex digits"};
            }
            value = (value << 4) | digit;
        }
        position_ += digits;
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            throw TomlError{line_, "escape does not denote a Unicode scalar value"};
        }
        return value;
    }

    static void appendUtf8(std::string& out, std::uint32_t codePoint) {
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

    [[nodiscard]] Value parseNumber() {
        const std::size_t start = position_;
        bool negative = false;
        if (text_[position_] == '-' || text_[position_] == '+') {
            negative = text_[position_] == '-';
            ++position_;
        }
        if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') {
            throw TomlError{line_, "expected a digit"};
        }
        // Reject the bases and separators TOML permits but this subset does not, before the
        // digit loop, so the diagnostic names the actual feature rather than "unexpected text".
        if (text_[position_] == '0' && position_ + 1 < text_.size()) {
            const char next = text_[position_ + 1];
            if (next == 'x' || next == 'o' || next == 'b') {
                throw TomlError{line_, "hexadecimal, octal and binary integers are outside this "
                                        "TOML subset; write the value in decimal"};
            }
        }

        std::uint64_t magnitude = 0;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            const auto digit = static_cast<std::uint64_t>(text_[position_] - '0');
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
                throw TomlError{line_, "integer does not fit in 64 bits"};
            }
            magnitude = magnitude * 10u + digit;
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '_') {
            throw TomlError{line_, "digit separators are outside this TOML subset"};
        }
        if (position_ < text_.size() &&
            (text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E')) {
            // The ADR-007 rule, surfaced at the recipe level: a real number cannot be authored
            // as decimal text anywhere in this pipeline.
            throw TomlError{line_,
                            "floats are rejected: a real number in this pipeline is a u32 bit "
                            "pattern, not decimal text (see ADR-007). Write the bit pattern as "
                            "an integer."};
        }
        if (position_ < text_.size() && text_[position_] == '-') {
            throw TomlError{line_, "datetimes are outside this TOML subset"};
        }

        const auto limit =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
        if (magnitude > limit) {
            throw TomlError{line_, "integer '" + std::string{text_.substr(start, position_ - start)} +
                                        "' does not fit in a signed 64-bit integer"};
        }
        if (negative) {
            if (magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u) {
                return Value::integer(std::numeric_limits<std::int64_t>::min(), line_);
            }
            return Value::integer(-static_cast<std::int64_t>(magnitude), line_);
        }
        return Value::integer(static_cast<std::int64_t>(magnitude), line_);
    }

    [[nodiscard]] Value parseArray() {
        ++position_;  // '['
        std::vector<Value> elements;
        while (true) {
            skipSpace();
            if (position_ >= text_.size()) {
                throw TomlError{line_, "array is not terminated"};
            }
            if (text_[position_] == ']') {
                ++position_;
                return Value::array(std::move(elements), line_);
            }
            elements.push_back(parseValue());
            skipSpace();
            if (position_ >= text_.size()) {
                throw TomlError{line_, "array is not terminated"};
            }
            if (text_[position_] == ',') {
                ++position_;
                continue;  // a trailing comma before ']' is legal TOML, so the loop head handles it
            }
            if (text_[position_] == ']') {
                ++position_;
                return Value::array(std::move(elements), line_);
            }
            throw TomlError{line_, "expected ',' or ']' in an array"};
        }
    }

    std::string_view text_;
    std::size_t line_;
    std::size_t position_{0};
};

/// Validates a bare key and produces the diagnostics that distinguish "outside this subset" from
/// "not a key at all" - the difference matters to whoever is fixing the recipe.
void validateBareKey(std::string_view key, std::size_t line, std::string_view what) {
    if (key.empty()) {
        throw TomlError{line, std::string{what} + " is empty"};
    }
    if (key.find('.') != std::string_view::npos) {
        throw TomlError{line, "dotted " + std::string{what} + "s are outside this TOML subset: '" +
                                  std::string{key} + "'"};
    }
    if (key.front() == '"' || key.front() == '\'') {
        throw TomlError{line, "quoted " + std::string{what} + "s are outside this TOML subset: '" +
                                  std::string{key} + "'"};
    }
    for (const char c : key) {
        if (!isBareKeyChar(c)) {
            throw TomlError{line, std::string{what} + " '" + std::string{key} +
                                      "' contains a character outside [A-Za-z0-9_-]"};
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value Value::string(std::string text, std::size_t line) {
    Value value;
    value.kind_ = Kind::String;
    value.line_ = line;
    value.string_ = std::move(text);
    return value;
}

Value Value::integer(std::int64_t number, std::size_t line) {
    Value value;
    value.kind_ = Kind::Integer;
    value.line_ = line;
    value.integer_ = number;
    return value;
}

Value Value::boolean(bool flag, std::size_t line) {
    Value value;
    value.kind_ = Kind::Boolean;
    value.line_ = line;
    value.boolean_ = flag;
    return value;
}

Value Value::array(std::vector<Value> elements, std::size_t line) {
    Value value;
    value.kind_ = Kind::Array;
    value.line_ = line;
    value.elements_ = std::move(elements);
    return value;
}

std::string_view Value::describe(Kind kind) noexcept {
    switch (kind) {
    case Kind::String:  return "string";
    case Kind::Integer: return "integer";
    case Kind::Boolean: return "boolean";
    case Kind::Array:   return "array";
    }
    return "unrecognized";
}

const std::string& Value::asString() const {
    if (kind_ != Kind::String) {
        throw TomlError{line_, std::string{"expected a string, found a "} +
                                   std::string{describe(kind_)}};
    }
    return string_;
}

std::int64_t Value::asInteger() const {
    if (kind_ != Kind::Integer) {
        throw TomlError{line_, std::string{"expected an integer, found a "} +
                                   std::string{describe(kind_)}};
    }
    return integer_;
}

bool Value::asBoolean() const {
    if (kind_ != Kind::Boolean) {
        throw TomlError{line_, std::string{"expected a boolean, found a "} +
                                   std::string{describe(kind_)}};
    }
    return boolean_;
}

std::span<const Value> Value::asArray() const {
    if (kind_ != Kind::Array) {
        throw TomlError{line_, std::string{"expected an array, found a "} +
                                   std::string{describe(kind_)}};
    }
    return elements_;
}

std::vector<std::string> Value::asStringArray() const {
    const std::span<const Value> elements = asArray();
    std::vector<std::string> out;
    out.reserve(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].kind() != Kind::String) {
            throw TomlError{elements[i].line(),
                            "array element " + std::to_string(i) + " is a " +
                                std::string{describe(elements[i].kind())} + ", expected a string"};
        }
        out.push_back(elements[i].string_);
    }
    return out;
}

std::vector<std::int64_t> Value::asIntegerArray() const {
    const std::span<const Value> elements = asArray();
    std::vector<std::int64_t> out;
    out.reserve(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].kind() != Kind::Integer) {
            throw TomlError{elements[i].line(),
                            "array element " + std::to_string(i) + " is a " +
                                std::string{describe(elements[i].kind())} + ", expected an integer"};
        }
        out.push_back(elements[i].integer_);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

const Value* Table::find(std::string_view key) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

const Value& Table::require(std::string_view key) const {
    if (const Value* found = find(key); found != nullptr) {
        return *found;
    }
    throw TomlError{line_, "required key '" + std::string{key} + "' is missing"};
}

void Table::insert(std::string key, Value value, std::size_t line) {
    if (find(key) != nullptr) {
        throw TomlError{line, "duplicate key '" + key + "'"};
    }
    entries_.emplace_back(std::move(key), std::move(value));
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

const Table* Document::table(std::string_view name) const noexcept {
    for (const auto& entry : tables_) {
        if (entry.first == name) {
            return &entry.second;
        }
    }
    return nullptr;
}

const Table& Document::requireTable(std::string_view name) const {
    if (const Table* found = table(name); found != nullptr) {
        return *found;
    }
    throw TomlError{0, "required table '[" + std::string{name} + "]' is missing"};
}

// ---------------------------------------------------------------------------
// parse()
// ---------------------------------------------------------------------------

Document parse(std::string_view text) {
    Document document;
    Table* current = &document.root_;

    // Split into lines up front so a diagnostic can always name a 1-based line number.
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::size_t lineNumber = index + 1;
        const std::string_view stripped = trim(stripComment(lines[index]));
        if (stripped.empty()) {
            continue;
        }

        if (stripped.front() == '[') {
            if (stripped.starts_with("[[")) {
                throw TomlError{lineNumber,
                                "arrays of tables are outside this TOML subset"};
            }
            if (stripped.back() != ']') {
                throw TomlError{lineNumber, "table header is missing its closing ']'"};
            }
            const std::string_view name = trim(stripped.substr(1, stripped.size() - 2));
            validateBareKey(name, lineNumber, "table name");
            if (document.table(name) != nullptr) {
                throw TomlError{lineNumber, "duplicate table '[" + std::string{name} + "]'"};
            }
            Table table;
            table.setLine(lineNumber);
            document.tables_.emplace_back(std::string{name}, std::move(table));
            current = &document.tables_.back().second;
            continue;
        }

        const std::size_t equals = stripped.find('=');
        if (equals == std::string_view::npos) {
            throw TomlError{lineNumber, "expected 'key = value' or a [table] header"};
        }
        const std::string_view key = trim(stripped.substr(0, equals));
        validateBareKey(key, lineNumber, "key");

        // An array may span lines; join continuation lines before handing the text to the value
        // parser. Comments are stripped per line first, so a comment inside a multi-line array
        // is handled the same as anywhere else.
        std::string valueText{trim(stripped.substr(equals + 1))};
        while (hasUnclosedBracket(valueText)) {
            ++index;
            if (index >= lines.size()) {
                throw TomlError{lineNumber, "array is not terminated before end of file"};
            }
            valueText.push_back('\n');
            valueText += trim(stripComment(lines[index]));
        }

        ValueParser parser{valueText, lineNumber};
        current->insert(std::string{key}, parser.parseComplete(), lineNumber);
    }

    return document;
}

}  // namespace mdux::tools::toml
