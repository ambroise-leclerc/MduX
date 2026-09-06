/**
 * @file Grammar.cpp
 * @brief Implementation of the machine-readable `.medui` contract and `--explain`.
 */
module;

module mdux.tools.medui.grammar;

import std;
import mdux.evidence.json;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.lexer;
import mdux.tools.medui.semantic;

namespace mdux::tools::medui {

namespace {

namespace json = mdux::evidence::json;

/// Sets one member or fails loudly.
///
/// A refusal means a duplicate key or a key this module built wrong, both of which are defects here
/// rather than anything a caller did - and a document silently missing a section is the one outcome
/// a contract must not have.
void put(json::Value& object, std::string key, json::Value value) {
    if (auto set = object.set(key, std::move(value)); !set.has_value()) {
        throw std::logic_error(std::format("the grammar document could not take member '{}'", key));
    }
}

[[nodiscard]] json::Value text(std::string_view value) {
    return json::Value::string(std::string{value});
}

/**
 * @brief Every member of a `std::uint8_t`-backed enumeration that `name` gives a spelling to.
 *
 * The enumerators are found by scanning the underlying value range rather than by listing them, and
 * that is the whole mechanism by which this document cannot omit one. A hand-written list is a
 * second place to remember, and an emitter that forgot an entry would publish a lexicon missing a
 * token while every test that counted the list went on passing.
 *
 * What makes the scan safe is that each `name` below is a switch with **no default**, so `-Wswitch`
 * under `-Werror` makes adding an enumerator a build failure until that switch handles it - and once
 * it does, this picks the new member up with no further edit. Casting an out-of-range value to a
 * scoped enumeration with a fixed underlying type is well defined, which is what lets the scan ask
 * the question at all.
 *
 * A member the spelling function deliberately leaves empty is skipped, which is how `Unspecified`
 * stays out of the closed named-value sets: it is the aggregate default a compiled screen may never
 * carry, not a spelling an author can write.
 */
template <typename Enum, typename Name>
[[nodiscard]] std::vector<Enum> members(Name name) {
    static_assert(std::is_same_v<std::underlying_type_t<Enum>, std::uint8_t>, "the scan is over a uint8_t range");
    std::vector<Enum> found;
    for (std::uint32_t value = 0; value <= std::numeric_limits<std::uint8_t>::max(); ++value) {
        const auto candidate = static_cast<Enum>(value);
        if (!name(candidate).empty()) {
            found.push_back(candidate);
        }
    }
    return found;
}

[[nodiscard]] std::string_view tokenName(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Identifier:
            return "Identifier";
        case TokenKind::Number:
            return "Number";
        case TokenKind::String:
            return "String";
        case TokenKind::At:
            return "At";
        case TokenKind::LBrace:
            return "LBrace";
        case TokenKind::RBrace:
            return "RBrace";
        case TokenKind::LParen:
            return "LParen";
        case TokenKind::RParen:
            return "RParen";
        case TokenKind::LBracket:
            return "LBracket";
        case TokenKind::RBracket:
            return "RBracket";
        case TokenKind::Colon:
            return "Colon";
        case TokenKind::Semicolon:
            return "Semicolon";
        case TokenKind::Comma:
            return "Comma";
        case TokenKind::Dot:
            return "Dot";
        case TokenKind::EndOfFile:
            return "EndOfFile";
    }
    // Named rather than defaulted, so a token added to the lexer is a warning at this switch rather
    // than an unnamed entry in a document an agent is entitled to trust.
    return {};
}

[[nodiscard]] std::string_view domainName(FieldDomain domain) noexcept {
    switch (domain) {
        case FieldDomain::Identifier:
            return "Identifier";
        case FieldDomain::Size:
            return "Size";
        case FieldDomain::Point:
            return "Point";
        case FieldDomain::String:
            return "String";
        case FieldDomain::TextKey:
            return "TextKey";
        case FieldDomain::TextKeyList:
            return "TextKeyList";
        case FieldDomain::ColorToken:
            return "ColorToken";
        case FieldDomain::ColorTokenList:
            return "ColorTokenList";
        case FieldDomain::ImageRef:
            return "ImageRef";
        case FieldDomain::Number:
            return "Number";
        case FieldDomain::ClockFormatName:
            return "ClockFormatName";
        case FieldDomain::SystemEventName:
            return "SystemEventName";
    }
    return {};
}

/// The written form of a domain, which is what an author has to produce.
///
/// Deliberately *not* `Semantic.cpp`'s `describe()`, which collapses both closed-set domains to "a
/// named value" - right in a diagnostic, where the following sentence names the members, and wrong
/// here, where a consumer is reading the domain on its own.
[[nodiscard]] std::string_view domainForm(FieldDomain domain) noexcept {
    switch (domain) {
        case FieldDomain::Identifier:
            return "an unquoted name";
        case FieldDomain::Size:
            return "Npx or Fill";
        case FieldDomain::Point:
            return "Xpx, Ypx";
        case FieldDomain::String:
            return "a quoted string";
        case FieldDomain::TextKey:
            return "t(\"STR-KEY\")";
        case FieldDomain::TextKeyList:
            return "a non-empty list of t(\"STR-KEY\") values";
        case FieldDomain::ColorToken:
            return "Theme.Colors.<Token>";
        case FieldDomain::ColorTokenList:
            return "a non-empty list of Theme.Colors.<Token> values";
        case FieldDomain::ImageRef:
            return "img(\"ID\")";
        case FieldDomain::Number:
            return "a positive integer";
        case FieldDomain::ClockFormatName:
            return "a member of namedValues.format";
        case FieldDomain::SystemEventName:
            return "a member of namedValues.on_press";
    }
    return {};
}

/// One written production, with the sources that hold it to the parser.
struct Production {
    std::string_view                                   name;
    std::string_view                                   rule;
    std::string_view                                   note;
    std::span<const std::string_view>                  accepts;
    std::span<const std::pair<std::string_view, Code>> rejects;
};

// The sources below are whole screens rather than fragments, because the parser's entry point is a
// screen and an example an agent cannot paste into a file is an example that has been simplified
// into something else. They are minimal: nothing in one is there that the production does not need.

constexpr std::string_view screenAccept = "Screen Minimal {\n"
                                          "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                          "}\n";

constexpr std::string_view screenNoBrace = "Screen Minimal\n";

constexpr std::array                                       screenAccepts{screenAccept};
constexpr std::array<std::pair<std::string_view, Code>, 1> screenRejects{{{screenNoBrace, Code::UnexpectedToken}}};

constexpr std::string_view surfaceAccept = "Screen Sized {\n"
                                           "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                           "    surface: 1280px, 720px;\n"
                                           "}\n";

constexpr std::string_view surfaceBadUnit = "Screen Sized {\n"
                                            "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                            "    surface: 1280pt, 720px;\n"
                                            "}\n";

constexpr std::array                                       surfaceAccepts{surfaceAccept};
constexpr std::array<std::pair<std::string_view, Code>, 1> surfaceRejects{{{surfaceBadUnit, Code::UnexpectedToken}}};

constexpr std::string_view componentAccept = "Screen WithLabel {\n"
                                             "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                             "    Label {\n"
                                             "        id: title;\n"
                                             "        width: 120px;\n"
                                             "        height: 20px;\n"
                                             "        text: t(\"STR-TITLE\");\n"
                                             "        color: Theme.Colors.Title;\n"
                                             "    }\n"
                                             "}\n";

constexpr std::string_view componentUnknown = "Screen WithLabel {\n"
                                              "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                              "    Gauge {\n"
                                              "        id: dial;\n"
                                              "    }\n"
                                              "}\n";

constexpr std::string_view componentMissingField = "Screen WithLabel {\n"
                                                   "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                                   "    Label {\n"
                                                   "        id: title;\n"
                                                   "        width: 120px;\n"
                                                   "        height: 20px;\n"
                                                   "        color: Theme.Colors.Title;\n"
                                                   "    }\n"
                                                   "}\n";

constexpr std::array                                       componentAccepts{componentAccept};
constexpr std::array<std::pair<std::string_view, Code>, 2> componentRejects{
    {{componentUnknown, Code::UnknownComponent}, {componentMissingField, Code::MissingRequiredField}}
};

constexpr std::string_view rowAccept = "Screen Banded {\n"
                                       "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                       "    Row {\n"
                                       "        id: topbar;\n"
                                       "        height: 72px;\n"
                                       "        Label {\n"
                                       "            id: title;\n"
                                       "            width: 120px;\n"
                                       "            height: 20px;\n"
                                       "            text: t(\"STR-TITLE\");\n"
                                       "            color: Theme.Colors.Title;\n"
                                       "        }\n"
                                       "    }\n"
                                       "}\n";

constexpr std::string_view rowNested = "Screen Banded {\n"
                                       "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                       "    Row {\n"
                                       "        id: topbar;\n"
                                       "        height: 72px;\n"
                                       "        Row {\n"
                                       "            id: inner;\n"
                                       "            height: 20px;\n"
                                       "        }\n"
                                       "    }\n"
                                       "}\n";

constexpr std::array                                       rowAccepts{rowAccept};
constexpr std::array<std::pair<std::string_view, Code>, 1> rowRejects{{{rowNested, Code::NestedRow}}};

constexpr std::string_view annotationAccept = "Screen Traced {\n"
                                              "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                              "    @safety_critical(cv_check: [Bounds, ColorHash])\n"
                                              "    NumericDisplay {\n"
                                              "        id: pressure;\n"
                                              "        width: 200px;\n"
                                              "        height: 60px;\n"
                                              "        requirement: \"REQ-1\";\n"
                                              "        template: \"TPL-1\";\n"
                                              "        source: \"PRESSURE\";\n"
                                              "        color: Theme.Colors.ScoreDigits;\n"
                                              "    }\n"
                                              "}\n";

constexpr std::string_view annotationUntraced = "Screen Traced {\n"
                                                "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                                "    @safety_critical(cv_check: [Bounds])\n"
                                                "    Label {\n"
                                                "        id: title;\n"
                                                "        width: 120px;\n"
                                                "        height: 20px;\n"
                                                "        text: t(\"STR-TITLE\");\n"
                                                "        color: Theme.Colors.Title;\n"
                                                "    }\n"
                                                "}\n";

constexpr std::array                                       annotationAccepts{annotationAccept};
constexpr std::array<std::pair<std::string_view, Code>, 1> annotationRejects{{{annotationUntraced, Code::SafetyCriticalWithoutRequirement}}};

constexpr std::string_view valueAccept = "Screen Values {\n"
                                         "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                         "    surface: 400px, 200px;\n"
                                         "    Clock {\n"
                                         "        id: wall;\n"
                                         "        width: 120px;\n"
                                         "        height: 20px;\n"
                                         "        position: 0px, 0px;\n"
                                         "        format: TimeSeconds;\n"
                                         "    }\n"
                                         "}\n";

constexpr std::string_view valueHardcoded = "Screen Values {\n"
                                            "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                            "    Label {\n"
                                            "        id: title;\n"
                                            "        width: 120px;\n"
                                            "        height: 20px;\n"
                                            "        text: \"Endoscope Monitor\";\n"
                                            "        color: Theme.Colors.Title;\n"
                                            "    }\n"
                                            "}\n";

constexpr std::string_view valueOutsideSet = "Screen Values {\n"
                                             "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                             "    Clock {\n"
                                             "        id: wall;\n"
                                             "        width: 120px;\n"
                                             "        height: 20px;\n"
                                             "        format: HH_MM;\n"
                                             "    }\n"
                                             "}\n";

constexpr std::array                                       valueAccepts{valueAccept};
constexpr std::array<std::pair<std::string_view, Code>, 2> valueRejects{
    {{valueHardcoded, Code::HardcodedString}, {valueOutsideSet, Code::NamedValueOutsideSet}}
};

constexpr std::string_view forbiddenSource = "Screen Scripted {\n"
                                             "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                             "    if (true) {\n"
                                             "    }\n"
                                             "}\n";

constexpr std::array<std::string_view, 0>                  forbiddenAccepts{};
constexpr std::array<std::pair<std::string_view, Code>, 1> forbiddenRejects{{{forbiddenSource, Code::ForbiddenConstruct}}};

// The forms the productions above and below refer to. One accepted screen each, minimal and
// self-contained: an agent reading `pixels` should not have to follow a cross-reference to see one.

constexpr std::string_view memberAccept = "Screen Members {\n"
                                          "    layout: Vertical { spacing: 8px; padding: 0px; }\n"
                                          "    surface: 400px, 200px;\n"
                                          "    Label {\n"
                                          "        id: title;\n"
                                          "        width: 120px;\n"
                                          "        height: 20px;\n"
                                          "        text: t(\"STR-TITLE\");\n"
                                          "        color: Theme.Colors.Title;\n"
                                          "    }\n"
                                          "}\n";

constexpr std::string_view layoutBadKind = "Screen Members {\n"
                                           "    layout: if { spacing: 0px; }\n"
                                           "}\n";

constexpr std::string_view fieldNoColon = "Screen Members {\n"
                                          "    layout: Vertical { spacing 0px; padding: 0px; }\n"
                                          "}\n";

constexpr std::string_view listAccept = "Screen Listed {\n"
                                        "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                        "    StatusIndicator {\n"
                                        "        id: state;\n"
                                        "        width: 100px;\n"
                                        "        height: 20px;\n"
                                        "        requirement: \"REQ-1\";\n"
                                        "        source: \"STATE\";\n"
                                        "        states: [t(\"STR-OK\"), t(\"STR-ALARM\")];\n"
                                        "        colors: [Theme.Colors.Nominal, Theme.Colors.Fault];\n"
                                        "    }\n"
                                        "}\n";

constexpr std::string_view imageAccept = "Screen Branded {\n"
                                         "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                         "    Image {\n"
                                         "        id: brand;\n"
                                         "        width: 240px;\n"
                                         "        height: 72px;\n"
                                         "        source: img(\"brand-mark\");\n"
                                         "    }\n"
                                         "}\n";

constexpr std::string_view colorUnknown = "Screen Tinted {\n"
                                          "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                          "    Label {\n"
                                          "        id: title;\n"
                                          "        width: 120px;\n"
                                          "        height: 20px;\n"
                                          "        text: t(\"STR-TITLE\");\n"
                                          "        color: Theme.Colors.NotInTheTable;\n"
                                          "    }\n"
                                          "}\n";

// A child inside a component that is not a `Row`. The parser reads the component name as a field
// name and meets `{` where a `:` belongs, which is what makes nesting a Row-only shape rather than a
// general one.
constexpr std::string_view leafNested = "Screen Nested {\n"
                                        "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                        "    Label {\n"
                                        "        id: outer;\n"
                                        "        width: 120px;\n"
                                        "        height: 20px;\n"
                                        "        text: t(\"STR-TITLE\");\n"
                                        "        color: Theme.Colors.Title;\n"
                                        "        Label {\n"
                                        "            id: inner;\n"
                                        "            width: 10px;\n"
                                        "            height: 10px;\n"
                                        "            text: t(\"STR-TITLE\");\n"
                                        "            color: Theme.Colors.Title;\n"
                                        "        }\n"
                                        "    }\n"
                                        "}\n";

// An annotation argument terminated as though it were a field. It is not one: the parser reads an
// annotation's arguments unterminated, and the comma or the closing parenthesis is what ends them.
constexpr std::string_view annotationSemicolon = "Screen Traced {\n"
                                                 "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                                 "    @safety_critical(cv_check: [Bounds, ColorHash];)\n"
                                                 "    NumericDisplay {\n"
                                                 "        id: pressure;\n"
                                                 "        width: 200px;\n"
                                                 "        height: 60px;\n"
                                                 "        requirement: \"REQ-1\";\n"
                                                 "        template: \"TPL-1\";\n"
                                                 "        source: \"PRESSURE\";\n"
                                                 "        color: Theme.Colors.ScoreDigits;\n"
                                                 "    }\n"
                                                 "}\n";

constexpr std::array<std::pair<std::string_view, Code>, 1> leafRejects{{{leafNested, Code::UnexpectedToken}}};
constexpr std::array<std::pair<std::string_view, Code>, 1> annotationArgumentRejects{{{annotationSemicolon, Code::UnexpectedToken}}};

constexpr std::array memberAccepts{memberAccept};
constexpr std::array listAccepts{listAccept};
constexpr std::array imageAccepts{imageAccept};

constexpr std::array<std::pair<std::string_view, Code>, 1> layoutRejects{{{layoutBadKind, Code::ForbiddenConstruct}}};
constexpr std::array<std::pair<std::string_view, Code>, 1> fieldRejects{{{fieldNoColon, Code::UnexpectedToken}}};
constexpr std::array<std::pair<std::string_view, Code>, 1> colorRejects{{{colorUnknown, Code::UnknownColorToken}}};
constexpr std::array<std::pair<std::string_view, Code>, 0> noRejects{};

constexpr std::array<Production, 20> productions{
    {{.name    = "screen",
      .rule    = "screen = \"Screen\" identifier \"{\" { screen-member } \"}\" ;",
      .note    = "The whole file is one screen. Its name is CamelCase; the recipe records the pairing "
                 "with the lowercase artifact id rather than either being derived from the other.",
      .accepts = screenAccepts,
      .rejects = screenRejects},
     {.name    = "surface",
      .rule    = "surface = \"surface\" \":\" pixels \",\" pixels \";\" ;",
      .note    = "Optional. A file without one still parses and is still checked, but layout, overflow "
                 "and golden bounds are not - mdux-medui-check reports that as MDC002 rather than "
                 "passing quietly.",
      .accepts = surfaceAccepts,
      .rejects = surfaceRejects},
     {.name    = "component",
      .rule    = "component = leaf-component | row ;",
      .note    = "Only a Row takes children. Every other component is a leaf, which is why this is an "
                 "alternation rather than one recursive rule - a rule that let any component nest "
                 "would describe a language this parser rejects.",
      .accepts = componentAccepts,
      .rejects = componentRejects},
     {.name    = "row",
      .rule    = "row = \"Row\" \"{\" { field | row-child } \"}\" ;",
      .note    = "A single-level horizontal group, flattened at compile time. Its children are leaf "
                 "components and may be annotated; a Row inside a Row is refused rather than "
                 "flattened, because the solver has no second axis to give it.",
      .accepts = rowAccepts,
      .rejects = rowRejects},
     {.name    = "annotation",
      .rule    = "annotation = \"@\" identifier [ \"(\" annotation-argument { \",\" annotation-argument } \")\" ] ;",
      .note    = "Precedes the node it annotates. @safety_critical(cv_check: [...]) is the one "
                 "annotation with a rule attached: the node it marks must carry a requirement.",
      .accepts = annotationAccepts,
      .rejects = annotationRejects},
     {.name    = "value",
      .rule    = "value = pixels | \"Fill\" | number | string | text-key | image-ref | color-token | "
                 "identifier | point | list ;",
      .note    = "Which forms a field admits is the field's domain rather than the grammar's - see "
                 "fieldDomains. Localizable text is always t(\"STR-KEY\"): a literal in such a field "
                 "is refused, because a screen carrying one locale's words is not locale-free.",
      .accepts = valueAccepts,
      .rejects = valueRejects},
     {.name    = "screen-member",
      .rule    = "screen-member = layout | surface | node ;",
      .note    = "A screen's body is these three in any order. `layout` and `surface` may each appear "
                 "once; everything else is a node.",
      .accepts = memberAccepts,
      .rejects = noRejects},
     {.name    = "layout",
      .rule    = "layout = \"layout\" \":\" identifier [ \"{\" { field } \"}\" ] [ \";\" ] ;",
      .note    = "The identifier is the layout kind - `Vertical` is the one the solver implements. "
                 "The block carries `spacing` and `padding`, both sizes.",
      .accepts = memberAccepts,
      .rejects = layoutRejects},
     {.name    = "node",
      .rule    = "node = { annotation } component ;",
      .note    = "Annotations precede the component they mark, and there may be more than one.",
      .accepts = memberAccepts,
      .rejects = noRejects},
     {.name    = "leaf-component",
      .rule    = "leaf-component = identifier \"{\" { field } \"}\" ;",
      .note    = "Every component but Row. The name must be one the dictionary carries and its fields "
                 "exactly the set that entry admits: every required one present, no unknown one. See "
                 "components. A child here is read as a field name, which is why nesting fails at the "
                 "brace rather than at the dictionary.",
      .accepts = componentAccepts,
      .rejects = leafRejects},
     {.name    = "row-child",
      .rule    = "row-child = { annotation } leaf-component ;",
      .note    = "What a Row may contain besides its own fields. Annotated, because a golden "
                 "reference on a child of a Row is a thing an author writes.",
      .accepts = rowAccepts,
      .rejects = noRejects},
     {.name    = "annotation-argument",
      .rule    = "annotation-argument = identifier \":\" value ;",
      .note    = "A field's shape **without** the terminating semicolon - the comma or the closing "
                 "parenthesis ends it. Writing one as a field is the mistake this form exists to "
                 "rule out.",
      .accepts = annotationAccepts,
      .rejects = annotationArgumentRejects},
     {.name    = "field",
      .rule    = "field = identifier \":\" value \";\" ;",
      .note    = "One property per line by convention, and the sibling implementation requires it - "
                 "it splits a component body on the first colon of each line. This parser is "
                 "token-based and would accept several on one line, so writing one per line is what "
                 "keeps a screen portable in both directions.",
      .accepts = memberAccepts,
      .rejects = fieldRejects},
     {.name    = "pixels",
      .rule    = "pixels = number \"px\" ;",
      .note    = "The only unit. `Fill` is the other size form and takes no number; there is no "
                 "percentage, em or point.",
      .accepts = memberAccepts,
      .rejects = surfaceRejects},
     {.name    = "point",
      .rule    = "point = pixels \",\" pixels ;",
      .note    = "What `position:` and `surface:` take. A lone `Npx` is a size; the comma is what "
                 "makes it a point.",
      .accepts = memberAccepts,
      .rejects = noRejects},
     {.name    = "list",
      .rule    = "list = \"[\" [ value { \",\" value } ] \"]\" ;",
      .note    = "Used by `states:` and `colors:` on a StatusIndicator, which must pair one to one.",
      .accepts = listAccepts,
      .rejects = noRejects},
     {.name    = "text-key",
      .rule    = "text-key = \"t\" \"(\" string \")\" ;",
      .note    = "The only way to write localizable text. A literal in such a field is refused, "
                 "because a compiled screen is locale-free and carries the key rather than the words.",
      .accepts = memberAccepts,
      .rejects = valueRejects},
     {.name    = "image-ref",
      .rule    = "image-ref = \"img\" \"(\" string \")\" ;",
      .note    = "Names a baked image package by id. The screen approves that package's identity and "
                 "intrinsic extent; no compressed bytes and no decoder reach a device.",
      .accepts = imageAccepts,
      .rejects = noRejects},
     {.name    = "color-token",
      .rule    = "color-token = identifier { \".\" identifier } ;",
      .note    = "Written `Theme.Colors.<Token>`, and validated against the governed table rather "
                 "than against the shape - see themeTokens for the set. The parser keeps the dotted "
                 "path whole and unresolved; semantic analysis is what refuses an unknown one.",
      .accepts = memberAccepts,
      .rejects = colorRejects},
     {.name    = "forbidden",
      .rule    = "(* no production: loops, conditionals, recursion and scripting are absent *)",
      .note    = "Absent by design rather than unimplemented. A screen whose shape depends on data "
                 "is a screen whose worst case no compiler can measure, which is what every bound in "
                 "this language rests on.",
      .accepts = forbiddenAccepts,
      .rejects = forbiddenRejects}}
};

[[nodiscard]] json::Value tokenSection() {
    const std::vector<TokenKind> kinds = members<TokenKind>(tokenName);
    std::vector<json::Value>     rows;
    rows.reserve(kinds.size());
    for (const TokenKind kind : kinds) {
        json::Value row = json::Value::emptyObject();
        put(row, "name", text(tokenName(kind)));
        put(row, "description", text(describe(kind)));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value domainSection() {
    const std::vector<FieldDomain> domains = members<FieldDomain>(domainName);
    std::vector<json::Value>       rows;
    rows.reserve(domains.size());
    for (const FieldDomain domain : domains) {
        json::Value row = json::Value::emptyObject();
        put(row, "name", text(domainName(domain)));
        put(row, "form", text(domainForm(domain)));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value componentSection() {
    std::vector<json::Value> rows;
    for (const ComponentRule& component : componentDictionary()) {
        std::vector<json::Value> fields;
        fields.reserve(component.fields.size());
        for (const FieldRule& field : component.fields) {
            json::Value entry = json::Value::emptyObject();
            put(entry, "name", text(field.name));
            put(entry, "required", json::Value::boolean(field.required));
            put(entry, "domain", text(domainName(field.domain)));
            fields.push_back(std::move(entry));
        }
        json::Value row = json::Value::emptyObject();
        put(row, "name", text(component.name));
        put(row, "fields", json::Value::array(std::move(fields)));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

/// The two closed sets, keyed by the field that admits each.
///
/// Read through `toWire()` rather than listed, so a member added upstream and taken into
/// `mdux.medui.schema` appears here without this file being touched. `Unspecified` is skipped: it is
/// the aggregate default a compiled screen may never carry, not a spelling an author can write.
[[nodiscard]] json::Value namedValueSection() {
    // `toWire()` is the spelling function, and it answers empty for `Unspecified` and for any value
    // the enumeration has no name for - so the scan yields exactly the members an author may write,
    // with the sentinel excluded by that same rule rather than by a special case here.
    const auto wire = [](auto value) {
        return mdux::medui::toWire(value);
    };

    const auto spellings = [&wire](auto&& found) {
        std::vector<json::Value> rows;
        rows.reserve(found.size());
        for (const auto value : found) {
            rows.push_back(text(wire(value)));
        }
        return json::Value::array(std::move(rows));
    };

    json::Value section = json::Value::emptyObject();
    put(section, "format", spellings(members<mdux::medui::ClockFormat>(wire)));
    put(section, "on_press", spellings(members<mdux::medui::SystemEvent>(wire)));
    return section;
}

[[nodiscard]] json::Value themeSection() {
    std::vector<json::Value> rows;
    rows.reserve(mdux::medui::themeColors.size());
    for (const mdux::medui::ThemeColor& colour : mdux::medui::themeColors) {
        rows.push_back(text(colour.token));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value diagnosticSection() {
    std::vector<json::Value> rows;
    for (const CodeInfo& entry : registry()) {
        json::Value row = json::Value::emptyObject();
        put(row, "code", text(entry.id));
        put(row, "severity", text(cli::describe(entry.severity)));
        put(row, "summary", text(entry.summary));
        put(row, "fixHint", text(entry.fixHint));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value productionSection() {
    std::vector<json::Value> rows;
    rows.reserve(productions.size());
    for (const Production& production : productions) {
        std::vector<json::Value> accepts;
        accepts.reserve(production.accepts.size());
        for (const std::string_view source : production.accepts) {
            accepts.push_back(text(source));
        }

        std::vector<json::Value> rejects;
        rejects.reserve(production.rejects.size());
        for (const auto& [source, code] : production.rejects) {
            json::Value entry = json::Value::emptyObject();
            put(entry, "source", text(source));
            put(entry, "code", text(id(code)));
            rejects.push_back(std::move(entry));
        }

        json::Value row = json::Value::emptyObject();
        put(row, "name", text(production.name));
        put(row, "rule", text(production.rule));
        put(row, "note", text(production.note));
        put(row, "accepts", json::Value::array(std::move(accepts)));
        put(row, "rejects", json::Value::array(std::move(rejects)));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

}  // namespace

json::Value grammar() {
    json::Value document = json::Value::emptyObject();
    put(document, "schemaVersion", json::Value::unsignedInteger(grammarSchemaVersion));
    put(document, "language", text("medui"));
    put(document, "tokens", tokenSection());
    put(document, "productions", productionSection());
    put(document, "components", componentSection());
    put(document, "fieldDomains", domainSection());
    put(document, "namedValues", namedValueSection());
    put(document, "themeTokens", themeSection());
    put(document, "diagnostics", diagnosticSection());

    std::vector<json::Value> retiredCodes;
    for (const std::string_view code : retired()) {
        retiredCodes.push_back(text(code));
    }
    put(document, "retiredDiagnostics", json::Value::array(std::move(retiredCodes)));
    return document;
}

std::string grammarJson() {
    const auto written = json::write(grammar());
    if (!written.has_value()) {
        throw std::logic_error("the grammar document could not be serialised as canonical JSON");
    }
    // `json::write()` already ends the document with a newline - the canonical form includes it - so
    // adding one here would leave a blank line at the end of every emission and of the committed
    // file, which is a byte a diff would show and nobody meant.
    return *written;
}

std::optional<std::string> explain(std::string_view code) {
    for (const std::string_view number : retired()) {
        if (code == number) {
            return std::format("{}: retired. This number was published and then withdrawn, and is never reused - a rule "
                               "that replaced it carries a new number. Nothing emits it.",
                               number);
        }
    }

    for (const CodeInfo& entry : registry()) {
        // The alias is accepted alongside the published identifier while the one-release transition
        // documented in `Diagnostics.cppm` stands. An agent that pinned the old spelling gets the
        // explanation rather than a shrug, and the answer names the current spelling so the pin can
        // be moved.
        const std::string alias = legacyId(entry.code);
        if (code != entry.id && code != alias) {
            continue;
        }

        std::string answer = std::format("{} [{}] {}", entry.id, cli::describe(entry.severity), entry.summary);
        if (!entry.fixHint.empty()) {
            answer += std::format("\n  fix: {}", entry.fixHint);
        }
        if (code == alias && alias != entry.id) {
            answer += std::format("\n  note: {} is the one-release compatibility alias for {}; canonical output uses {}.", alias, entry.id, entry.id);
        }
        return answer;
    }
    return std::nullopt;
}

}  // namespace mdux::tools::medui
