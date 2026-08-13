/**
 * @file Ast.cppm
 * @brief The `.medui` abstract syntax tree.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, and deliberately *unresolved*. Component names, field names and theme tokens are all
 * carried as the strings the author wrote, with their positions. Nothing here is checked against
 * the component dictionary or the theme table.
 *
 * That is the boundary between this stage (#192) and the next (#193), and it is worth stating
 * because the opposite arrangement is the tempting one. A parser that knew the dictionary could
 * reject an unknown component at the point it reads the name, which reads like an improvement -
 * until adding a component means touching the parser, and until a field's meaning depends on which
 * component encloses it, at which point the parser is doing semantic analysis with a worse error
 * vocabulary. Keeping names as names means #193 can be replaced without this file changing.
 *
 * What the parser *does* enforce is structure that no dictionary can express: nesting depth, the
 * absence of control flow, and id uniqueness. Those are properties of the grammar rather than of
 * any particular component, so they belong here (ADR-011 decision 5).
 *
 * Every node carries the line and column of the token that introduced it, so a diagnostic raised
 * three stages later can still point at the source. Positions are cheap here and impossible to
 * reconstruct later.
 */
module;

export module mdux.tools.medui.ast;

import std;

export namespace mdux::tools::medui::ast {

/// Where something was written. 1-based, matching the shared diagnostic envelope.
struct Position {
    std::size_t line{0};
    std::size_t column{0};
};

/// A `Npx` length, or `Fill`.
struct Size {
    bool fill{false};
    std::int64_t pixels{0};
    Position position{};
};

/// `position: Xpx, Ypx` - out-of-flow placement at exact coordinates.
struct Point {
    std::int64_t x{0};
    std::int64_t y{0};
    Position position{};
};

/// What a field's value is. `Identifier` covers bare words such as `Vertical` or `Fill`; the
/// parser does not know which are meaningful, only which are syntactically possible.
enum class ValueKind : std::uint8_t {
    Size,        ///< `512px` or `Fill`
    Point,       ///< `1392px, 80px`
    String,      ///< `"REQ-NS-001"` - a literal, which most fields must not accept (#193)
    TextKey,     ///< `t("STR-KEY")`
    ColorToken,  ///< `Theme.Colors.ScoreDigits`, carried unresolved
    Identifier,  ///< `Vertical`, `sedation-index`
    Number,      ///< a bare integer, e.g. `spacing: 8` without a unit
    List,        ///< `[Bounds, ColorHash]`
};

struct Value;

/// One `name: value;` inside a node or a layout block.
struct Field {
    std::string name;
    Position namePosition{};
    std::shared_ptr<Value> value;  ///< shared_ptr because Value is incomplete here and lists nest
};

struct Value {
    ValueKind kind{ValueKind::Identifier};
    Position position{};

    Size size{};                       ///< kind == Size
    Point point{};                     ///< kind == Point
    std::string text;                  ///< String, TextKey, ColorToken (the token name), Identifier
    std::int64_t number{0};            ///< Number
    std::vector<std::shared_ptr<Value>> list;  ///< List
};

/// `@safety_critical(cv_check: [Bounds, ColorHash])`, carried unresolved.
struct Annotation {
    std::string name;
    Position position{};
    std::vector<Field> arguments;
};

/**
 * @brief A component instance, or a `Row`.
 *
 * `children` is non-empty only for `Row`. The parser rejects a `Row` inside a `Row` (`MEDUI-E015`),
 * so this is one level deep by construction rather than by convention - #194's solver relies on
 * that, and ADR-011 decision 5 records why the restriction is a solver argument rather than a
 * budget one.
 */
struct Node {
    std::string component;   ///< as written; not checked against the dictionary here
    Position position{};
    std::vector<Annotation> annotations;
    std::vector<Field> fields;
    std::vector<Node> children;
};

/// One `Screen <Name> { ... }`. A source declares exactly one.
struct Screen {
    std::string name;
    Position position{};
    std::vector<Field> layout;   ///< the `layout:` block's fields, e.g. spacing and padding
    std::string layoutKind;      ///< `Vertical`; empty when no `layout:` was declared
    Position layoutPosition{};
    std::optional<Point> surface;
    std::vector<Node> nodes;
};

}  // namespace mdux::tools::medui::ast
