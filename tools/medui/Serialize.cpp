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

void appendList(std::string& out, const std::vector<std::shared_ptr<ast::Value>>& elements) {
    out += '[';
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        appendValue(out, *elements[i]);
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
    if (field.value != nullptr) {
        appendValue(out, *field.value);
    }
    out += ";\n";
}

/// `@name` alone, or `@name(argument: value, argument: value)` - `Parser.cpp`'s `parseAnnotation()`
/// separates arguments with commas and each argument is an unterminated field.
void appendAnnotation(std::string& out, const ast::Annotation& annotation, int depth) {
    appendIndent(out, depth);
    out += '@';
    out += annotation.name;
    if (!annotation.arguments.empty()) {
        out += '(';
        for (std::size_t i = 0; i < annotation.arguments.size(); ++i) {
            if (i != 0) {
                out += ", ";
            }
            const ast::Field& argument = annotation.arguments[i];
            out                       += argument.name;
            out                       += ": ";
            if (argument.value != nullptr) {
                appendValue(out, *argument.value);
            }
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
                if (field.value != nullptr) {
                    appendValue(out, *field.value);
                }
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
