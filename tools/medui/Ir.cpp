/**
 * @file Ir.cpp
 * @brief Implementation of the resolved-IR dump.
 */
module;

module mdux.tools.medui.ir;

import std;
import mdux.evidence.json;
import mdux.medui.schema;
import mdux.tools.medui.ast;
import mdux.tools.medui.layout;
import mdux.tools.medui.textbudget;

namespace mdux::tools::medui {

namespace {

namespace json = mdux::evidence::json;

/// Sets one member or fails loudly. A document silently missing a section is the one outcome an
/// intermediate representation must not have - see `Grammar.cpp`, which says the same for the same
/// reason.
void put(json::Value& object, std::string key, json::Value value) {
    if (auto set = object.set(key, std::move(value)); !set.has_value()) {
        throw std::logic_error(std::format("the IR document could not take member '{}'", key));
    }
}

[[nodiscard]] json::Value text(std::string_view value) {
    return json::Value::string(std::string{value});
}

[[nodiscard]] json::Value integer(std::int64_t value) {
    return json::Value::integer(value);
}

[[nodiscard]] std::string_view kindName(ast::ValueKind kind) noexcept {
    switch (kind) {
        case ast::ValueKind::Size:
            return "Size";
        case ast::ValueKind::Point:
            return "Point";
        case ast::ValueKind::String:
            return "String";
        case ast::ValueKind::TextKey:
            return "TextKey";
        case ast::ValueKind::ImageRef:
            return "ImageRef";
        case ast::ValueKind::ColorToken:
            return "ColorToken";
        case ast::ValueKind::Identifier:
            return "Identifier";
        case ast::ValueKind::Number:
            return "Number";
        case ast::ValueKind::List:
            return "List";
    }
    // Named rather than defaulted, so a value kind added to the AST is a warning here rather than
    // an unlabelled entry in a document a consumer is entitled to trust.
    return {};
}

/// One authored value as the source wrote it, so a reader can match a dump against a `.medui` file.
[[nodiscard]] std::string spell(const ast::Value& value) {
    switch (value.kind) {
        case ast::ValueKind::Size:
            return value.size.fill ? "Fill" : std::format("{}px", value.size.pixels);
        case ast::ValueKind::Point:
            return std::format("{}px, {}px", value.point.x, value.point.y);
        case ast::ValueKind::String:
            return std::format("\"{}\"", value.text);
        case ast::ValueKind::TextKey:
            return std::format("t(\"{}\")", value.text);
        case ast::ValueKind::ImageRef:
            return std::format("img(\"{}\")", value.text);
        case ast::ValueKind::ColorToken:
        case ast::ValueKind::Identifier:
            return value.text;
        case ast::ValueKind::Number:
            return std::format("{}", value.number);
        case ast::ValueKind::List: {
            std::string joined = "[";
            for (std::size_t index = 0; index < value.list.size(); ++index) {
                if (index > 0) {
                    joined += ", ";
                }
                joined += value.list[index] == nullptr ? "" : spell(*value.list[index]);
            }
            return joined + "]";
        }
    }
    return {};
}

/**
 * @brief The colour a token resolves to, as four `u32` bit patterns, or nothing.
 *
 * The governed table's own resolver rather than a second reading of it, so a dump cannot disagree
 * with what the device will draw. Bit patterns for ADR-007 decision 2's reason: a decimal rendering
 * would carry a rounding rule this project would have to pin across three toolchains, and a dump
 * exists to be diffed.
 */
[[nodiscard]] std::optional<json::Value> resolvedColor(std::string_view token) {
    const auto resolved = mdux::medui::resolveColorToken(token);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    json::Value channels = json::Value::emptyObject();
    put(channels, "r", json::Value::float32((*resolved)[0]));
    put(channels, "g", json::Value::float32((*resolved)[1]));
    put(channels, "b", json::Value::float32((*resolved)[2]));
    put(channels, "a", json::Value::float32((*resolved)[3]));
    return channels;
}

/// Every value a list may contain, so a `states:` or `colors:` list is readable element by element
/// rather than only as the joined spelling.
[[nodiscard]] json::Value elements(const ast::Value& value) {
    std::vector<json::Value> rows;
    rows.reserve(value.list.size());
    for (const std::shared_ptr<ast::Value>& element : value.list) {
        if (element == nullptr) {
            continue;
        }
        json::Value row = json::Value::emptyObject();
        put(row, "kind", text(kindName(element->kind)));
        put(row, "spelling", text(spell(*element)));
        if (element->kind == ast::ValueKind::ColorToken) {
            if (std::optional<json::Value> colour = resolvedColor(element->text); colour.has_value()) {
                put(row, "resolved", std::move(*colour));
            }
        }
        rows.push_back(std::move(row));
    }
    return json::Value::array(std::move(rows));
}

[[nodiscard]] json::Value fieldIr(const ast::Field& field) {
    json::Value row = json::Value::emptyObject();
    put(row, "name", text(field.name));
    put(row, "line", integer(static_cast<std::int64_t>(field.namePosition.line)));
    put(row, "column", integer(static_cast<std::int64_t>(field.namePosition.column)));
    if (field.value == nullptr) {
        // A field the parser accepted with no value cannot occur - `parseField()` refuses one - but
        // a null here would otherwise be a dereference rather than a document saying so.
        put(row, "kind", text(""));
        put(row, "spelling", text(""));
        return row;
    }
    put(row, "kind", text(kindName(field.value->kind)));
    put(row, "spelling", text(spell(*field.value)));
    if (field.value->kind == ast::ValueKind::ColorToken) {
        // The one place a name becomes a value in this document. Every other field stays a validated
        // name, exactly as ADR-011 keeps it in the artifact.
        if (std::optional<json::Value> colour = resolvedColor(field.value->text); colour.has_value()) {
            put(row, "resolved", std::move(*colour));
        }
    }
    if (field.value->kind == ast::ValueKind::List) {
        put(row, "elements", elements(*field.value));
    }
    return row;
}

[[nodiscard]] json::Value nodeIr(const ResolvedNode& node) {
    json::Value bounds = json::Value::emptyObject();
    put(bounds, "x", integer(node.bounds.x));
    put(bounds, "y", integer(node.bounds.y));
    put(bounds, "width", integer(node.bounds.width));
    put(bounds, "height", integer(node.bounds.height));

    std::vector<json::Value> fields;
    fields.reserve(node.source.fields.size());
    for (const ast::Field& field : node.source.fields) {
        fields.push_back(fieldIr(field));
    }

    std::vector<json::Value> annotations;
    annotations.reserve(node.source.annotations.size());
    for (const ast::Annotation& annotation : node.source.annotations) {
        std::vector<json::Value> arguments;
        arguments.reserve(annotation.arguments.size());
        for (const ast::Field& argument : annotation.arguments) {
            arguments.push_back(fieldIr(argument));
        }
        json::Value row = json::Value::emptyObject();
        put(row, "name", text(annotation.name));
        put(row, "arguments", json::Value::array(std::move(arguments)));
        annotations.push_back(std::move(row));
    }

    json::Value row = json::Value::emptyObject();
    put(row, "id", text(node.id));
    put(row, "component", text(node.component));
    put(row, "bounds", std::move(bounds));
    // Both are the layout's own conclusions rather than the source's: `positioned` is what put this
    // node out of flow, and `synthetic` marks the `Panel` a `Row` background produces, which no
    // author wrote and which therefore appears in no `.medui` file a reader could compare against.
    put(row, "positioned", json::Value::boolean(node.positioned));
    put(row, "synthetic", json::Value::boolean(node.synthetic));
    put(row, "annotations", json::Value::array(std::move(annotations)));
    put(row, "fields", json::Value::array(std::move(fields)));
    return row;
}

[[nodiscard]] json::Value measurementIr(const TextMeasurement& measurement) {
    json::Value extent = json::Value::emptyObject();
    put(extent, "width", integer(measurement.extent.width));
    put(extent, "height", integer(measurement.extent.height));

    json::Value row = json::Value::emptyObject();
    put(row, "nodeId", text(measurement.nodeId));
    put(row, "field", text(measurement.field));
    put(row, "textKey", text(measurement.textKey));
    // The locale that produced the widest *width*. On a screen whose tallest and widest
    // translations differ this does not name the one `extent.height` came from - `TextMeasurement`
    // says so, and repeating it here keeps a reader of the dump from concluding otherwise.
    put(row, "widestLocale", text(measurement.locale));
    put(row, "extent", std::move(extent));
    return row;
}

}  // namespace

json::Value screenIr(std::string_view screenId, const ast::Screen& screen, const LayoutResult& layout, std::span<const TextMeasurement> measurements) {
    std::vector<json::Value> nodes;
    nodes.reserve(layout.nodes.size());
    for (const ResolvedNode& node : layout.nodes) {
        nodes.push_back(nodeIr(node));
    }

    std::vector<json::Value> budgets;
    budgets.reserve(measurements.size());
    for (const TextMeasurement& measurement : measurements) {
        budgets.push_back(measurementIr(measurement));
    }

    std::vector<json::Value> layoutFields;
    layoutFields.reserve(screen.layout.size());
    for (const ast::Field& field : screen.layout) {
        layoutFields.push_back(fieldIr(field));
    }

    json::Value surface = json::Value::emptyObject();
    put(surface, "width", integer(layout.surfaceWidth));
    put(surface, "height", integer(layout.surfaceHeight));
    // Whether the *source* declared one, as against the recipe supplying it. The solver checks the
    // two agree, so a reader seeing `false` knows the numbers above came from the recipe alone.
    put(surface, "declaredInSource", json::Value::boolean(screen.surface.has_value()));

    json::Value document = json::Value::emptyObject();
    put(document, "schemaVersion", json::Value::unsignedInteger(irSchemaVersion));
    put(document, "id", text(screenId));
    put(document, "screen", text(screen.name));
    put(document, "layoutKind", text(screen.layoutKind));
    put(document, "layout", json::Value::array(std::move(layoutFields)));
    put(document, "surface", std::move(surface));
    put(document, "nodes", json::Value::array(std::move(nodes)));
    put(document, "textBudgets", json::Value::array(std::move(budgets)));
    return document;
}

std::string screenIrJson(std::string_view screenId, const ast::Screen& screen, const LayoutResult& layout, std::span<const TextMeasurement> measurements) {
    const auto written = json::write(screenIr(screenId, screen, layout, measurements));
    if (!written.has_value()) {
        throw std::logic_error("the IR document could not be serialised as canonical JSON");
    }
    return *written;
}

}  // namespace mdux::tools::medui
