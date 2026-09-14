/**
 * @file Document.cpp
 * @brief Implementation of the `.medui` AST JSON document view.
 */

module;

module mdux.tools.medui.document;

import std;
import mdux.evidence.json;
import mdux.tools.medui.ast;
import mdux.tools.medui.parser;
import mdux.tools.medui.serialize;

namespace mdux::tools::medui {

namespace {

namespace json = mdux::evidence::json;
using Value    = json::Value;

/// A coordinate or length the canonical text can carry and the parser reads back unchanged.
constexpr std::int64_t maxMagnitude = std::numeric_limits<std::int32_t>::max();

struct Refusal {
    std::string message;
};

void put(Value& object, std::string key, Value value) {
    if (auto set = object.set(std::move(key), std::move(value)); !set.has_value()) {
        throw std::logic_error("screenDocument() built a duplicate member");
    }
}

[[noreturn]] void refuse(const std::string& path, std::string_view what) {
    throw Refusal{std::format("{}: {}", path, what)};
}

const Value& member(const Value& object, std::string_view key, const std::string& path) {
    const Value* found = object.find(key);
    if (found == nullptr) {
        refuse(path, std::format("missing member '{}'", key));
    }
    return *found;
}

void exactMembers(const Value& object, std::initializer_list<std::string_view> allowed, const std::string& path) {
    if (object.kind() != Value::Kind::Object) {
        refuse(path, "expected an object");
    }
    for (const auto& m : object.members()) {
        if (std::ranges::find(allowed, m.key) == allowed.end()) {
            refuse(path, std::format("unknown member '{}'", m.key));
        }
    }
    for (std::string_view key : allowed) {
        static_cast<void>(member(object, key, path));
    }
}

std::string text(const Value& value, const std::string& path) {
    auto s = value.asString();
    if (!s) {
        refuse(path, "expected a string");
    }
    return std::string{*s};
}

std::int64_t magnitude(const Value& value, const std::string& path) {
    auto n = value.asInt();
    if (!n || *n < 0 || *n > maxMagnitude) {
        refuse(path, std::format("expected an integer from 0 to {}", maxMagnitude));
    }
    return *n;
}

std::span<const Value> elements(const Value& value, const std::string& path) {
    if (value.kind() != Value::Kind::Array) {
        refuse(path, "expected an array");
    }
    return value.elements();
}

bool isIdentifier(std::string_view s) noexcept {
    const auto start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    const auto rest = [&](char c) {
        return start(c) || (c >= '0' && c <= '9') || c == '-';
    };
    return !s.empty() && start(s.front()) && std::ranges::all_of(s.substr(1), rest);
}

/// `Theme.Colors.Token`: identifiers joined by single dots, as the parser reads a colour token.
bool isDottedIdentifier(std::string_view s) noexcept {
    if (s.empty() || s.back() == '.') {
        return false;
    }
    for (std::size_t begin = 0; begin <= s.size();) {
        const auto end = std::min(s.find('.', begin), s.size());
        if (!isIdentifier(s.substr(begin, end - begin))) {
            return false;
        }
        begin = end + 1;
    }
    return true;
}

std::string name(const Value& value, const std::string& path) {
    auto s = text(value, path);
    if (!isIdentifier(s)) {
        refuse(path, "expected a .medui identifier");
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------
// AST -> document
// ---------------------------------------------------------------------------------------------------

Value valueDocument(const ast::Value& value);

Value fieldsDocument(const std::vector<ast::Field>& fields) {
    std::vector<Value> out;
    out.reserve(fields.size());
    for (const ast::Field& field : fields) {
        if (field.value == nullptr) {
            throw std::logic_error(std::format("screenDocument() received a null value for field '{}'", field.name));
        }
        Value row = Value::emptyObject();
        put(row, "name", Value::string(field.name));
        put(row, "value", valueDocument(*field.value));
        out.push_back(std::move(row));
    }
    return Value::array(std::move(out));
}

Value valueDocument(const ast::Value& value) {
    Value row = Value::emptyObject();
    switch (value.kind) {
        case ast::ValueKind::Size:
            put(row, "kind", Value::string("Size"));
            put(row, "fill", Value::boolean(value.size.fill));
            put(row, "pixels", Value::integer(value.size.pixels));
            break;
        case ast::ValueKind::Point:
            put(row, "kind", Value::string("Point"));
            put(row, "x", Value::integer(value.point.x));
            put(row, "y", Value::integer(value.point.y));
            break;
        case ast::ValueKind::String:
        case ast::ValueKind::TextKey:
        case ast::ValueKind::ImageRef:
        case ast::ValueKind::ColorToken:
        case ast::ValueKind::Identifier: {
            constexpr std::array<std::string_view, 5> names{"String", "TextKey", "ImageRef", "ColorToken", "Identifier"};
            put(row, "kind", Value::string(std::string{names[static_cast<std::size_t>(value.kind) - static_cast<std::size_t>(ast::ValueKind::String)]}));
            put(row, "text", Value::string(value.text));
            break;
        }
        case ast::ValueKind::Number:
            put(row, "kind", Value::string("Number"));
            put(row, "number", Value::integer(value.number));
            break;
        case ast::ValueKind::List: {
            std::vector<Value> items;
            items.reserve(value.list.size());
            for (const auto& item : value.list) {
                if (item == nullptr) {
                    throw std::logic_error("screenDocument() received a null list element");
                }
                items.push_back(valueDocument(*item));
            }
            put(row, "kind", Value::string("List"));
            put(row, "items", Value::array(std::move(items)));
            break;
        }
    }
    return row;
}

Value nodeDocument(const ast::Node& node) {
    std::vector<Value> annotations;
    for (const ast::Annotation& annotation : node.annotations) {
        Value row = Value::emptyObject();
        put(row, "name", Value::string(annotation.name));
        put(row, "arguments", fieldsDocument(annotation.arguments));
        annotations.push_back(std::move(row));
    }
    std::vector<Value> children;
    for (const ast::Node& child : node.children) {
        children.push_back(nodeDocument(child));
    }
    Value row = Value::emptyObject();
    put(row, "component", Value::string(node.component));
    put(row, "annotations", Value::array(std::move(annotations)));
    put(row, "fields", fieldsDocument(node.fields));
    put(row, "children", Value::array(std::move(children)));
    return row;
}

// ---------------------------------------------------------------------------------------------------
// document -> AST
// ---------------------------------------------------------------------------------------------------

std::shared_ptr<ast::Value> readValue(const Value& document, const std::string& path);

std::vector<ast::Field> readFields(const Value& document, const std::string& path) {
    std::vector<ast::Field> fields;
    const auto              rows = elements(document, path);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto at = std::format("{}[{}]", path, i);
        exactMembers(rows[i], {"name", "value"}, at);
        fields.push_back(
            {.name = name(member(rows[i], "name", at), at + ".name"), .namePosition = {}, .value = readValue(member(rows[i], "value", at), at + ".value")});
    }
    return fields;
}

std::shared_ptr<ast::Value> readValue(const Value& document, const std::string& path) {
    if (document.kind() != Value::Kind::Object) {
        refuse(path, "expected an object");
    }
    const auto kind  = text(member(document, "kind", path), path + ".kind");
    auto       value = std::make_shared<ast::Value>();
    if (kind == "Size") {
        exactMembers(document, {"kind", "fill", "pixels"}, path);
        auto fill = member(document, "fill", path).asBool();
        if (!fill) {
            refuse(path + ".fill", "expected a boolean");
        }
        value->kind = ast::ValueKind::Size;
        value->size = {.fill = *fill, .pixels = magnitude(member(document, "pixels", path), path + ".pixels"), .position = {}};
        if (value->size.fill && value->size.pixels != 0) {
            refuse(path + ".pixels", "a Fill size carries no pixels");
        }
    } else if (kind == "Point") {
        exactMembers(document, {"kind", "x", "y"}, path);
        value->kind  = ast::ValueKind::Point;
        value->point = {.x = magnitude(member(document, "x", path), path + ".x"), .y = magnitude(member(document, "y", path), path + ".y"), .position = {}};
    } else if (kind == "String" || kind == "TextKey" || kind == "ImageRef" || kind == "ColorToken" || kind == "Identifier") {
        exactMembers(document, {"kind", "text"}, path);
        value->kind = kind == "String"       ? ast::ValueKind::String
                      : kind == "TextKey"    ? ast::ValueKind::TextKey
                      : kind == "ImageRef"   ? ast::ValueKind::ImageRef
                      : kind == "ColorToken" ? ast::ValueKind::ColorToken
                                             : ast::ValueKind::Identifier;
        value->text = text(member(document, "text", path), path + ".text");
        if (value->kind == ast::ValueKind::Identifier && !isIdentifier(value->text)) {
            refuse(path + ".text", "expected a .medui identifier");
        }
        if (value->kind == ast::ValueKind::ColorToken && !isDottedIdentifier(value->text)) {
            refuse(path + ".text", "expected a dotted colour token");
        }
    } else if (kind == "Number") {
        exactMembers(document, {"kind", "number"}, path);
        value->kind   = ast::ValueKind::Number;
        value->number = magnitude(member(document, "number", path), path + ".number");
    } else if (kind == "List") {
        exactMembers(document, {"kind", "items"}, path);
        value->kind     = ast::ValueKind::List;
        const auto rows = elements(member(document, "items", path), path + ".items");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            value->list.push_back(readValue(rows[i], std::format("{}.items[{}]", path, i)));
        }
    } else {
        refuse(path + ".kind", "unknown value kind");
    }
    return value;
}

ast::Node readNode(const Value& document, const std::string& path) {
    exactMembers(document, {"component", "annotations", "fields", "children"}, path);
    ast::Node node;
    node.component         = name(member(document, "component", path), path + ".component");
    const auto annotations = elements(member(document, "annotations", path), path + ".annotations");
    for (std::size_t i = 0; i < annotations.size(); ++i) {
        const auto at = std::format("{}.annotations[{}]", path, i);
        exactMembers(annotations[i], {"name", "arguments"}, at);
        node.annotations.push_back({.name      = name(member(annotations[i], "name", at), at + ".name"),
                                    .position  = {},
                                    .arguments = readFields(member(annotations[i], "arguments", at), at + ".arguments")});
    }
    node.fields         = readFields(member(document, "fields", path), path + ".fields");
    const auto children = elements(member(document, "children", path), path + ".children");
    for (std::size_t i = 0; i < children.size(); ++i) {
        node.children.push_back(readNode(children[i], std::format("{}.children[{}]", path, i)));
    }
    return node;
}

// ---------------------------------------------------------------------------------------------------
// Equality and safety metadata
// ---------------------------------------------------------------------------------------------------

bool sameValue(const std::shared_ptr<ast::Value>& a, const std::shared_ptr<ast::Value>& b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
        case ast::ValueKind::Size:
            return a->size.fill == b->size.fill && a->size.pixels == b->size.pixels;
        case ast::ValueKind::Point:
            return a->point.x == b->point.x && a->point.y == b->point.y;
        case ast::ValueKind::Number:
            return a->number == b->number;
        case ast::ValueKind::List:
            return std::ranges::equal(a->list, b->list, sameValue);
        case ast::ValueKind::String:
        case ast::ValueKind::TextKey:
        case ast::ValueKind::ImageRef:
        case ast::ValueKind::ColorToken:
        case ast::ValueKind::Identifier:
            return a->text == b->text;
    }
    return false;
}

bool sameFields(const std::vector<ast::Field>& a, const std::vector<ast::Field>& b) {
    return std::ranges::equal(a, b, [](const ast::Field& x, const ast::Field& y) {
        return x.name == y.name && sameValue(x.value, y.value);
    });
}

bool sameNode(const ast::Node& a, const ast::Node& b) {
    return a.component == b.component && sameFields(a.fields, b.fields)
           && std::ranges::equal(a.annotations,
                                 b.annotations,
                                 [](const ast::Annotation& x, const ast::Annotation& y) {
                                     return x.name == y.name && sameFields(x.arguments, y.arguments);
                                 })
           && std::ranges::equal(a.children, b.children, sameNode);
}

/// Canonical JSON of a node's annotations and `requirement:` field, or empty when it carries neither.
std::string safetyMetadata(const ast::Node& node) {
    const auto requirement = std::ranges::find(node.fields, "requirement", &ast::Field::name);
    if (node.annotations.empty() && requirement == node.fields.end()) {
        return {};
    }
    const auto full = nodeDocument(node);
    Value      meta = Value::emptyObject();
    put(meta, "annotations", *full.find("annotations"));
    put(meta, "requirement", requirement == node.fields.end() || requirement->value == nullptr ? Value::null() : valueDocument(*requirement->value));
    auto written = json::write(meta);
    if (!written) {
        throw std::logic_error("safety metadata could not be written");
    }
    return *written;
}

void collectMetadata(const std::vector<ast::Node>& nodes, const std::string& path, std::map<std::string, std::string>& out) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const ast::Node& node = nodes[i];
        const auto       id   = std::ranges::find(node.fields, "id", &ast::Field::name);
        const auto       at   = std::format("{}[{}]", path, i);
        const auto       key  = id != node.fields.end() && id->value != nullptr && id->value->kind == ast::ValueKind::Identifier ? id->value->text : at;
        out.try_emplace(key, safetyMetadata(node));
        collectMetadata(node.children, at + ".children", out);
    }
}

}  // namespace

mdux::evidence::json::Value screenDocument(const ast::Screen& screen) {
    std::vector<Value> nodes;
    for (const ast::Node& node : screen.nodes) {
        nodes.push_back(nodeDocument(node));
    }
    Value surface = Value::null();
    if (screen.surface) {
        surface = Value::emptyObject();
        put(surface, "x", Value::integer(screen.surface->x));
        put(surface, "y", Value::integer(screen.surface->y));
    }
    Value document = Value::emptyObject();
    put(document, "schemaVersion", Value::unsignedInteger(documentSchemaVersion));
    put(document, "name", Value::string(screen.name));
    put(document, "layoutKind", Value::string(screen.layoutKind));
    put(document, "layout", fieldsDocument(screen.layout));
    put(document, "surface", std::move(surface));
    put(document, "nodes", Value::array(std::move(nodes)));
    return document;
}

std::expected<ast::Screen, std::string> readScreenDocument(const mdux::evidence::json::Value& document) {
    try {
        const std::string root = "document";
        exactMembers(document, {"schemaVersion", "name", "layoutKind", "layout", "surface", "nodes"}, root);
        auto version = member(document, "schemaVersion", root).asUInt();
        if (!version || *version != documentSchemaVersion) {
            refuse(root + ".schemaVersion", std::format("unsupported document schema version; this build reads {}", documentSchemaVersion));
        }
        ast::Screen screen;
        screen.name       = name(member(document, "name", root), root + ".name");
        screen.layoutKind = text(member(document, "layoutKind", root), root + ".layoutKind");
        if (!screen.layoutKind.empty() && !isIdentifier(screen.layoutKind)) {
            refuse(root + ".layoutKind", "expected an empty string or a .medui identifier");
        }
        screen.layout = readFields(member(document, "layout", root), root + ".layout");
        if (screen.layoutKind.empty() && !screen.layout.empty()) {
            refuse(root + ".layout", "layout fields need a layoutKind");
        }
        const Value& surface = member(document, "surface", root);
        if (surface.kind() != Value::Kind::Null) {
            exactMembers(surface, {"x", "y"}, root + ".surface");
            screen.surface = ast::Point{.x        = magnitude(member(surface, "x", root), root + ".surface.x"),
                                        .y        = magnitude(member(surface, "y", root), root + ".surface.y"),
                                        .position = {}};
        }
        const auto nodes = elements(member(document, "nodes", root), root + ".nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            screen.nodes.push_back(readNode(nodes[i], std::format("{}.nodes[{}]", root, i)));
        }
        return screen;
    } catch (const Refusal& refusal) {
        return std::unexpected(refusal.message);
    }
}

std::expected<std::string, std::string> sourceFromDocument(const mdux::evidence::json::Value& document) {
    auto screen = readScreenDocument(document);
    if (!screen) {
        return std::unexpected(std::move(screen.error()));
    }
    auto       source   = serializeScreen(*screen);
    const auto reparsed = parse(source, "document.medui");
    if (reparsed.ok() && !sameScreen(*reparsed.screen, *screen)) {
        return std::unexpected(std::string{"document: the document cannot be represented exactly as .medui source"});
    }
    return source;
}

bool sameScreen(const ast::Screen& a, const ast::Screen& b) {
    return a.name == b.name && a.layoutKind == b.layoutKind && sameFields(a.layout, b.layout) && a.surface.has_value() == b.surface.has_value()
           && (!a.surface || (a.surface->x == b.surface->x && a.surface->y == b.surface->y)) && std::ranges::equal(a.nodes, b.nodes, sameNode);
}

bool containsComments(std::string_view source) noexcept {
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '"') {
            // A string ends at its closing quote or, unterminated, at the end of the line.
            for (++i; i < source.size() && source[i] != '"' && source[i] != '\n'; ++i) {
                if (source[i] == '\\' && i + 1 < source.size() && source[i + 1] != '\n') {
                    ++i;
                }
            }
            continue;
        }
        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            return true;
        }
    }
    return false;
}

std::vector<SafetyChange> safetyChanges(const ast::Screen& before, const ast::Screen& after) {
    std::map<std::string, std::string> old;
    std::map<std::string, std::string> edited;
    collectMetadata(before.nodes, "nodes", old);
    collectMetadata(after.nodes, "nodes", edited);
    std::set<std::string> ids;
    for (const auto& [id, meta] : old) {
        ids.insert(id);
    }
    for (const auto& [id, meta] : edited) {
        ids.insert(id);
    }
    std::vector<SafetyChange> changes;
    for (const auto& id : ids) {
        const auto a = old.find(id);
        const auto b = edited.find(id);
        const auto x = a == old.end() ? std::string{} : a->second;
        const auto y = b == edited.end() ? std::string{} : b->second;
        if (x == y) {
            continue;
        }
        const auto change = x.empty() ? "added" : y.empty() ? "removed" : "changed";
        changes.push_back({.nodeId = id, .change = change, .before = x, .after = y});
    }
    return changes;
}

}  // namespace mdux::tools::medui
