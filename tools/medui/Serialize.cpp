/**
 * @file Serialize.cpp
 * @brief Implementation of the canonical `.medui` source serializer.
 */

module;

module mdux.tools.medui.serialize;

import std;
import mdux.tools.medui.ast;

namespace mdux::tools::medui {

namespace {

/// Four spaces per nesting level, matching every hand-authored fixture in `tests/medui/fixtures/`.
constexpr std::string_view indentUnit = "    ";

void appendIndent(std::string& out, int depth) {
    for (int i = 0; i < depth; ++i) {
        out += indentUnit;
    }
}

/// The inverse of `Lexer.cpp`'s string scanner: that scanner resolves exactly `\"`, `\\`, `\n` and
/// `\t` into their literal characters, so this is the only escaping a round trip ever needs to undo.
void appendEscaped(std::string& out, std::string_view text) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    out += '"';
}

void appendValue(std::string& out, const ast::Value& value);

/// `ast::Field::value`, an `ast::Annotation` argument's value, and each `ast::Value::list` element
/// are all `std::shared_ptr<ast::Value>` a caller could leave null while building or editing an AST
/// by hand - `Parser.cpp` never produces one, but nothing before this stage checks. There is no
/// `.medui` syntax a null value could stand for, so this fails loudly (`Layout.cpp`'s own "a gate
/// was bypassed" precedent) rather than dereferencing it.
const ast::Value& require(const std::shared_ptr<ast::Value>& value, std::string_view what) {
    if (value == nullptr) {
        throw std::logic_error(std::format("serializeScreen() received a null value for {}", what));
    }
    return *value;
}

/// True when serializing `value` ends in a bare `Npx` token with nothing closing it. That is the one
/// shape `Parser.cpp`'s `parseValue()` extends across a following token: once `parsePixels()`
/// succeeds, it commits to a `Point` the instant the *next* token is a comma, with no lookahead and
/// no backtracking. Every other value kind ends in something that stops it there - `]`, `)`, a bare
/// word, `Fill` - so only this one makes a following comma ambiguous between "the next coordinate of
/// this value" and "the next element of an enclosing list or argument list".
bool endsInBarePixels(const ast::Value& value) {
    return value.kind == ast::ValueKind::Size && !value.size.fill;
}

/// The separator to place after `previous` and before the next list element or annotation argument.
/// `Parser.cpp`'s list loop and its annotation-argument loop both treat the comma between entries as
/// optional (`if (at(Comma)) advance();`), so a plain space is a complete substitute exactly when a
/// comma would instead be swallowed into `previous` as a `Point`'s second coordinate
/// (`endsInBarePixels()`) - not a special case bolted onto the grammar, a different but already-legal
/// spelling of the same separator.
std::string_view separatorAfter(const ast::Value& previous) {
    return endsInBarePixels(previous) ? " " : ", ";
}

void appendList(std::string& out, const std::vector<std::shared_ptr<ast::Value>>& elements) {
    out                       += '[';
    const ast::Value* previous = nullptr;
    for (const std::shared_ptr<ast::Value>& element : elements) {
        const ast::Value& value = require(element, "a list element");
        if (previous != nullptr) {
            out += separatorAfter(*previous);
        }
        appendValue(out, value);
        previous = &value;
    }
    out += ']';
}

void appendValue(std::string& out, const ast::Value& value) {
    switch (value.kind) {
        case ast::ValueKind::Size:
            if (value.size.fill) {
                out += "Fill";
            } else {
                out += std::to_string(value.size.pixels);
                out += "px";
            }
            return;
        case ast::ValueKind::Point:
            out += std::to_string(value.point.x);
            out += "px, ";
            out += std::to_string(value.point.y);
            out += "px";
            return;
        case ast::ValueKind::String:
            appendEscaped(out, value.text);
            return;
        case ast::ValueKind::TextKey:
            out += "t(";
            appendEscaped(out, value.text);
            out += ')';
            return;
        case ast::ValueKind::ImageRef:
            out += "img(";
            appendEscaped(out, value.text);
            out += ')';
            return;
        case ast::ValueKind::ColorToken:
        case ast::ValueKind::Identifier:
            out += value.text;
            return;
        case ast::ValueKind::Number:
            out += std::to_string(value.number);
            return;
        case ast::ValueKind::List:
            appendList(out, value.list);
            return;
    }
}

void appendField(std::string& out, const ast::Field& field, int depth) {
    appendIndent(out, depth);
    out += field.name;
    out += ": ";
    appendValue(out, require(field.value, std::format("field '{}'", field.name)));
    out += ";\n";
}

/// `@name` alone, or `@name(argument: value, argument: value)` - `Parser.cpp`'s `parseAnnotation()`
/// separates arguments with commas and each argument is an unterminated field.
void appendAnnotation(std::string& out, const ast::Annotation& annotation, int depth) {
    appendIndent(out, depth);
    out += '@';
    out += annotation.name;
    if (!annotation.arguments.empty()) {
        out                       += '(';
        const ast::Value* previous = nullptr;
        for (const ast::Field& argument : annotation.arguments) {
            const ast::Value& value = require(argument.value, std::format("annotation '{}' argument '{}'", annotation.name, argument.name));
            if (previous != nullptr) {
                out += separatorAfter(*previous);
            }
            out += argument.name;
            out += ": ";
            appendValue(out, value);
            previous = &value;
        }
        out += ')';
    }
    out += '\n';
}

void appendNode(std::string& out, const ast::Node& node, int depth) {
    for (const ast::Annotation& annotation : node.annotations) {
        appendAnnotation(out, annotation, depth);
    }
    appendIndent(out, depth);
    out += node.component;
    out += " {\n";
    for (const ast::Field& field : node.fields) {
        appendField(out, field, depth + 1);
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i == 0 && !node.fields.empty()) {
            out += '\n';
        }
        appendNode(out, node.children[i], depth + 1);
    }
    appendIndent(out, depth);
    out += "}\n";
}

}  // namespace

std::string serializeScreen(const ast::Screen& screen) {
    std::string out;
    out += "Screen ";
    out += screen.name;
    out += " {\n";

    if (!screen.layoutKind.empty()) {
        appendIndent(out, 1);
        out += "layout: ";
        out += screen.layoutKind;
        if (!screen.layout.empty()) {
            out += " { ";
            for (const ast::Field& field : screen.layout) {
                out += field.name;
                out += ": ";
                appendValue(out, require(field.value, std::format("layout field '{}'", field.name)));
                out += "; ";
            }
            out += '}';
        }
        out += '\n';
    }

    if (screen.surface.has_value()) {
        appendIndent(out, 1);
        out += "surface: ";
        out += std::to_string(screen.surface->x);
        out += "px, ";
        out += std::to_string(screen.surface->y);
        out += "px;\n";
    }

    for (const ast::Node& node : screen.nodes) {
        out += '\n';
        appendNode(out, node, 1);
    }

    out += "}\n";
    return out;
}

}  // namespace mdux::tools::medui
