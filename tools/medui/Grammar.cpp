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

/// Every `TokenKind`, in enumeration order.
///
/// Listed rather than iterated because C++ has no enumerator range, and pinned by a count assertion
/// in `GrammarTests.cpp` so that adding a token without adding it here fails a test rather than
/// publishing a lexicon missing one.
constexpr std::array tokenKinds{TokenKind::Identifier,
                                TokenKind::Number,
                                TokenKind::String,
                                TokenKind::At,
                                TokenKind::LBrace,
                                TokenKind::RBrace,
                                TokenKind::LParen,
                                TokenKind::RParen,
                                TokenKind::LBracket,
                                TokenKind::RBracket,
                                TokenKind::Colon,
                                TokenKind::Semicolon,
                                TokenKind::Comma,
                                TokenKind::Dot,
                                TokenKind::EndOfFile};

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

constexpr std::array fieldDomains{FieldDomain::Identifier,
                                  FieldDomain::Size,
                                  FieldDomain::Point,
                                  FieldDomain::String,
                                  FieldDomain::TextKey,
                                  FieldDomain::TextKeyList,
                                  FieldDomain::ColorToken,
                                  FieldDomain::ColorTokenList,
                                  FieldDomain::ImageRef,
                                  FieldDomain::Number,
                                  FieldDomain::ClockFormatName,
                                  FieldDomain::SystemEventName};

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

constexpr std::array<Production, 7> productions{
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
      .rule    = "component = identifier \"{\" { field | node } \"}\" ;",
      .note    = "The component name must be one the dictionary carries, and its fields exactly the "
                 "set that entry admits: every required one present, no unknown one. See components.",
      .accepts = componentAccepts,
      .rejects = componentRejects},
     {.name    = "row",
      .rule    = "row = \"Row\" \"{\" { field | component } \"}\" ;",
      .note    = "A single-level horizontal group, flattened at compile time. A Row inside a Row is "
                 "refused rather than flattened, because the solver has no second axis to give it.",
      .accepts = rowAccepts,
      .rejects = rowRejects},
     {.name    = "annotation",
      .rule    = "annotation = \"@\" identifier [ \"(\" field { \",\" field } \")\" ] ;",
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
     {.name    = "forbidden",
      .rule    = "(* no production: loops, conditionals, recursion and scripting are absent *)",
      .note    = "Absent by design rather than unimplemented. A screen whose shape depends on data "
                 "is a screen whose worst case no compiler can measure, which is what every bound in "
                 "this language rests on.",
      .accepts = forbiddenAccepts,
      .rejects = forbiddenRejects}}
};

[[nodiscard]] json::Value tokenSection() {
    std::vector<json::Value> rows;
    rows.reserve(tokenKinds.size());
    for (const TokenKind kind : tokenKinds) {
        json::Value row = json::Value::emptyObject();
        put(row, "name", text(tokenName(kind)));
        put(row, "description", text(describe(kind)));
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value domainSection() {
    std::vector<json::Value> rows;
    rows.reserve(fieldDomains.size());
    for (const FieldDomain domain : fieldDomains) {
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
    const auto members = [](auto&& values) {
        std::vector<json::Value> rows;
        for (const auto value : values) {
            const std::string_view wire = mdux::medui::toWire(value);
            if (wire.empty()) {
                continue;
            }
            rows.push_back(text(wire));
        }
        return json::Value::array(std::move(rows));
    };

    json::Value section = json::Value::emptyObject();
    put(section,
        "format",
        members(std::array{mdux::medui::ClockFormat::Unspecified, mdux::medui::ClockFormat::TimeSeconds, mdux::medui::ClockFormat::DateTimeSeconds}));
    put(section, "on_press", members(std::array{mdux::medui::SystemEvent::Unspecified, mdux::medui::SystemEvent::NoOp, mdux::medui::SystemEvent::TriggerHalt}));
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
