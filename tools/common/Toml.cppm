/**
 * @file Toml.cppm
 * @brief A deliberately minimal TOML reader for baker recipe files.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: never linked into MduXCore or MduX)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * ## Scope, and why it is this small
 *
 * Supported: tables, bare keys, basic strings, integers, booleans, and arrays of those.
 *
 * Not supported, on purpose: dotted keys, dotted table headers, arrays of tables, inline
 * tables, multi-line strings, literal (single-quoted) strings, floats, datetimes, hex/octal/
 * binary integers, and digit separators.
 *
 * The narrow scope is the point rather than a limitation to be fixed later. Recipe files are
 * authored by this project, TOML's full grammar is large, and every line here is code a
 * manufacturer may have to qualify. If a recipe needs a feature outside this subset, the recipe
 * format is wrong, not the parser.
 *
 * **Floats are rejected outright.** A recipe cannot express a real number as decimal text,
 * because ADR-007's byte-identity guarantee depends on real numbers being bit patterns. A recipe
 * that needs one states it as an integer bit pattern, and the diagnostic says so.
 *
 * ## Errors
 *
 * Throws TomlError, which carries a line number so a tool can render it through the shared
 * diagnostic envelope in mdux.tools.cli. This is the host-tools zone, so throwing is permitted -
 * a recipe error is a fail-and-report situation, not something a baker recovers from.
 */
module;

export module mdux.tools.toml;

import std;

export namespace mdux::tools::toml {

/// A recipe syntax or type error, with the 1-based line it was found on.
class TomlError : public std::runtime_error {
public:
    TomlError(std::size_t line, std::string message)
        : std::runtime_error{std::move(message)}, line_{line} {}

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

class Value {
public:
    enum class Kind : std::uint8_t { String, Integer, Boolean, Array };

    [[nodiscard]] static Value string(std::string text, std::size_t line);
    [[nodiscard]] static Value integer(std::int64_t number, std::size_t line);
    [[nodiscard]] static Value boolean(bool flag, std::size_t line);
    [[nodiscard]] static Value array(std::vector<Value> elements, std::size_t line);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    /// The line this value appeared on, for diagnostics.
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

    /// Typed accessors. Each throws TomlError naming the expected and actual type, so a baker
    /// reading an option gets a recipe-author-facing message without writing one itself.
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] std::int64_t asInteger() const;
    [[nodiscard]] bool asBoolean() const;
    [[nodiscard]] std::span<const Value> asArray() const;

    /// Convenience for the common "array of strings" and "array of integers" recipe shapes.
    /// Throws if any element is of the wrong type, naming the offending index.
    [[nodiscard]] std::vector<std::string> asStringArray() const;
    [[nodiscard]] std::vector<std::int64_t> asIntegerArray() const;

    [[nodiscard]] static std::string_view describe(Kind kind) noexcept;

private:
    Kind kind_{Kind::String};
    std::size_t line_{0};
    std::string string_;
    std::int64_t integer_{0};
    bool boolean_{false};
    std::vector<Value> elements_;
};

/**
 * @brief A table: an ordered set of key/value pairs.
 *
 * Insertion order is preserved so that a diagnostic can report keys in the order the author
 * wrote them. Lookup is by key and is exact - there is no case folding and no dotted-path
 * traversal, because neither exists in this subset.
 */
class Table {
public:
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    /// find() that throws TomlError if the key is absent, for required options.
    [[nodiscard]] const Value& require(std::string_view key) const;

    [[nodiscard]] bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    [[nodiscard]] std::span<const std::pair<std::string, Value>> entries() const noexcept {
        return entries_;
    }

    /// The line the table's header appeared on, or 0 for the root table.
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

    void setLine(std::size_t line) noexcept { line_ = line; }

    /// Throws on a duplicate key rather than overwriting - a recipe with a key twice is
    /// ambiguous, and silently taking the last one is how an author's intent gets lost.
    void insert(std::string key, Value value, std::size_t line);

private:
    std::vector<std::pair<std::string, Value>> entries_;
    std::size_t line_{0};
};

/**
 * @brief A parsed recipe: a root table plus named tables.
 *
 * Flat by construction. `[font]` and `[atlas]` are two tables; `[font.atlas]` is a dotted header
 * and is rejected.
 */
class Document {
public:
    [[nodiscard]] const Table& root() const noexcept { return root_; }

    [[nodiscard]] const Table* table(std::string_view name) const noexcept;

    /// table() that throws TomlError if the table is absent, for required sections.
    [[nodiscard]] const Table& requireTable(std::string_view name) const;

    [[nodiscard]] std::span<const std::pair<std::string, Table>> tables() const noexcept {
        return tables_;
    }

    friend Document parse(std::string_view text);

private:
    Table root_;
    std::vector<std::pair<std::string, Table>> tables_;
};

/// Parses TOML-subset `text`. Throws TomlError on anything outside the subset.
[[nodiscard]] Document parse(std::string_view text);

}  // namespace mdux::tools::toml
