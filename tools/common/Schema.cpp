/**
 * @file Schema.cpp
 * @brief Implementation of the JSON Schema subset validator (a port of MedUI's schema_check.py).
 */
module;

module mdux.tools.schema;

import std;
import mdux.evidence.json;

namespace mdux::tools::schema {

// Defined at namespace scope below; forward-declared so the anonymous-namespace validator can call
// it and so it is the one exported name.
[[nodiscard]] bool jsonEqual(const mdux::evidence::json::Value& left, const mdux::evidence::json::Value& right);

namespace {

namespace json = mdux::evidence::json;

using Value = json::Value;
using Kind  = json::Value::Kind;

constexpr std::array<std::string_view, 21> supportedKeywords{
    "$schema", "$id",     "$defs",         "$ref",           "title",   "description", "type",
    "properties", "additionalProperties",  "required",       "items",   "minItems",    "maxItems",
    "uniqueItems", "minLength", "pattern",  "minimum",        "maximum", "enum",        "const",
    "anyOf"};

[[nodiscard]] bool isSupported(std::string_view keyword) {
    return std::ranges::find(supportedKeywords, keyword) != supportedKeywords.end();
}

// --- number comparison, exact for integers of either sign -------------------

[[nodiscard]] bool isNumberKind(Kind kind) {
    return kind == Kind::Int || kind == Kind::UInt || kind == Kind::Float32;
}

[[nodiscard]] std::optional<double> asDouble(const Value& value) {
    if (const auto f = value.asFloat32()) {
        return static_cast<double>(*f);
    }
    if (const auto i = value.asInt()) {
        return static_cast<double>(*i);
    }
    if (const auto u = value.asUInt()) {
        return static_cast<double>(*u);
    }
    return std::nullopt;
}

/**
 * @brief Orders two JSON numbers.
 *
 * Exact for integers of either sign across the whole `std::int64_t` / `std::uint64_t` range - a
 * signed value and an unsigned one are ordered by sign first, then by magnitude, without narrowing
 * either through the other. Only a genuine float operand falls to a `double` comparison, and MduX
 * canonical JSON has no float literal, so that path is reachable only from a constructed `Value`.
 * `std::partial_ordering::unordered` when a value is not a number this reader can read.
 */
[[nodiscard]] std::partial_ordering compareNumbers(const Value& left, const Value& right) {
    if (left.kind() != Kind::Float32 && right.kind() != Kind::Float32) {
        const bool leftUnsigned  = left.kind() == Kind::UInt;
        const bool rightUnsigned = right.kind() == Kind::UInt;
        if (leftUnsigned && rightUnsigned) {
            const auto l = left.asUInt();
            const auto r = right.asUInt();
            return (l && r) ? (*l <=> *r) : std::partial_ordering::unordered;
        }
        if (!leftUnsigned && !rightUnsigned) {
            const auto l = left.asInt();
            const auto r = right.asInt();
            return (l && r) ? (*l <=> *r) : std::partial_ordering::unordered;
        }
        // One signed, one unsigned: order the signed value against the unsigned one, then re-orient.
        const auto signedValue   = (leftUnsigned ? right : left).asInt();
        const auto unsignedValue = (leftUnsigned ? left : right).asUInt();
        if (!signedValue || !unsignedValue) {
            return std::partial_ordering::unordered;
        }
        const std::partial_ordering signedVsUnsigned =
            (*signedValue < 0) ? std::partial_ordering::less
                               : (static_cast<std::uint64_t>(*signedValue) <=> *unsignedValue);
        if (!leftUnsigned) {
            return signedVsUnsigned;  // left was the signed value
        }
        // `signedVsUnsigned` ordered right (signed) against left (unsigned); reverse it.
        if (signedVsUnsigned == std::partial_ordering::less) {
            return std::partial_ordering::greater;
        }
        if (signedVsUnsigned == std::partial_ordering::greater) {
            return std::partial_ordering::less;
        }
        return signedVsUnsigned;
    }
    const auto l = asDouble(left);
    const auto r = asDouble(right);
    return (l && r) ? (*l <=> *r) : std::partial_ordering::unordered;
}

[[nodiscard]] bool numbersEqual(const Value& left, const Value& right) {
    return compareNumbers(left, right) == std::partial_ordering::equivalent;
}

/// The integer value of `value`, for schema bounds (`minItems`, `maxItems`, `minLength`) that are
/// always small nonnegative integers in the contract schemas.
[[nodiscard]] std::optional<std::int64_t> asIntegerNumber(const Value& value) {
    if (const auto number = value.asInt()) {
        return *number;
    }
    if (const auto number = value.asUInt()) {
        return static_cast<std::int64_t>(*number);
    }
    return std::nullopt;
}

// --- local `$ref` resolution ---------------------------------------------------

[[nodiscard]] const Value* resolveReference(const Value& root, std::string_view ref) {
    if (!ref.starts_with("#/")) {
        return nullptr;  // only local references are supported
    }
    const Value* target = &root;
    std::size_t  start   = 2;
    while (start <= ref.size()) {
        const std::size_t slash   = ref.find('/', start);
        const std::size_t end     = slash == std::string_view::npos ? ref.size() : slash;
        std::string       segment{ref.substr(start, end - start)};
        // JSON Pointer unescaping: ~1 -> '/', ~0 -> '~', in that order.
        for (std::size_t pos = segment.find("~1"); pos != std::string::npos; pos = segment.find("~1", pos + 1)) {
            segment.replace(pos, 2, "/");
        }
        for (std::size_t pos = segment.find("~0"); pos != std::string::npos; pos = segment.find("~0", pos + 1)) {
            segment.replace(pos, 2, "~");
        }

        if (target->kind() == Kind::Object) {
            target = target->find(segment);
        } else if (target->kind() == Kind::Array) {
            std::size_t index = 0;
            const auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if (ec != std::errc{} || ptr != segment.data() + segment.size() || index >= target->elements().size()) {
                return nullptr;
            }
            target = &target->elements()[index];
        } else {
            return nullptr;
        }
        if (target == nullptr) {
            return nullptr;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return target->kind() == Kind::Object ? target : nullptr;
}

// --- schema well-formedness -------------------------------------------------

void checkSchemaInto(const Value& schema, const Value& root, std::vector<std::string>& problems) {
    if (schema.kind() != Kind::Object) {
        problems.emplace_back("a schema must be an object");
        return;
    }
    for (const json::Member& entry : schema.members()) {
        if (!isSupported(entry.key)) {
            problems.push_back(std::format("unsupported schema keyword '{}'", entry.key));
        }
    }
    if (const Value* type = schema.find("type"); type != nullptr) {
        constexpr std::array<std::string_view, 6> known{"object", "array", "string", "boolean", "null", "integer"};
        const auto                                name = type->asString();
        if (!name || std::ranges::find(known, *name) == known.end()) {
            problems.push_back(std::format("unsupported `type` '{}' (this subset knows object, array, string, boolean, null, integer)",
                                           name.value_or("<non-string>")));
        }
    }
    // `errorsInto` only enforces `additionalProperties: false`; a subschema value would be
    // silently ignored, so flag it here rather than letting undeclared properties through.
    if (const Value* additional = schema.find("additionalProperties");
        additional != nullptr && additional->kind() != Kind::Bool) {
        problems.emplace_back("only a boolean `additionalProperties` is supported");
    }
    if (const Value* ref = schema.find("$ref"); ref != nullptr) {
        const auto text = ref->asString();
        if (!text || resolveReference(root, *text) == nullptr) {
            problems.push_back(std::format("unresolved schema reference '{}'", text.value_or("<non-string>")));
        }
    }
    for (std::string_view collection : {std::string_view{"properties"}, std::string_view{"$defs"}}) {
        if (const Value* child = schema.find(collection); child != nullptr && child->kind() == Kind::Object) {
            for (const json::Member& entry : child->members()) {
                checkSchemaInto(entry.value, root, problems);
            }
        }
    }
    if (const Value* items = schema.find("items"); items != nullptr) {
        checkSchemaInto(*items, root, problems);
    }
    if (const Value* anyOf = schema.find("anyOf"); anyOf != nullptr && anyOf->kind() == Kind::Array) {
        for (const Value& child : anyOf->elements()) {
            checkSchemaInto(child, root, problems);
        }
    }
}

// --- document validation ---------------------------------------------------

[[nodiscard]] bool matchesType(const Value& value, std::string_view type) {
    if (type == "object") {
        return value.kind() == Kind::Object;
    }
    if (type == "array") {
        return value.kind() == Kind::Array;
    }
    if (type == "string") {
        return value.kind() == Kind::String;
    }
    if (type == "boolean") {
        return value.kind() == Kind::Bool;
    }
    if (type == "null") {
        return value.kind() == Kind::Null;
    }
    if (type == "integer") {
        if (value.kind() == Kind::Int || value.kind() == Kind::UInt) {
            return true;
        }
        if (value.kind() == Kind::Float32) {
            const float number = value.asFloat32().value_or(0.5F);
            return number == std::trunc(number);
        }
        return false;
    }
    return false;
}

void errorsInto(const Value&              value,
                const Value&              schema,
                const Value&              root,
                const std::string&        path,
                std::vector<std::string>& result) {
    if (const Value* ref = schema.find("$ref"); ref != nullptr) {
        if (const auto text = ref->asString()) {
            if (const Value* target = resolveReference(root, *text)) {
                errorsInto(value, *target, root, path, result);
            }
        }
    }

    if (const Value* anyOf = schema.find("anyOf"); anyOf != nullptr && anyOf->kind() == Kind::Array) {
        const bool anyMatch = std::ranges::any_of(anyOf->elements(), [&](const Value& child) {
            std::vector<std::string> ignored;
            errorsInto(value, child, root, path, ignored);
            return ignored.empty();
        });
        if (!anyMatch) {
            result.push_back(std::format("{}: no anyOf alternative matches", path));
        }
    }

    if (const Value* type = schema.find("type"); type != nullptr) {
        const std::string_view name = type->asString().value_or("");
        if (!matchesType(value, name)) {
            result.push_back(std::format("{}: expected {}", path, name));
            return;
        }
    }

    if (const Value* constant = schema.find("const"); constant != nullptr && !jsonEqual(value, *constant)) {
        result.push_back(std::format("{}: incorrect constant", path));
    }
    if (const Value* choices = schema.find("enum"); choices != nullptr && choices->kind() == Kind::Array) {
        if (std::ranges::none_of(choices->elements(), [&](const Value& item) { return jsonEqual(value, item); })) {
            result.push_back(std::format("{}: outside enum", path));
        }
    }

    if (value.kind() == Kind::Object) {
        const Value* properties = schema.find("properties");
        if (const Value* required = schema.find("required"); required != nullptr && required->kind() == Kind::Array) {
            for (const Value& key : required->elements()) {
                const auto name = key.asString();
                if (name && value.find(*name) == nullptr) {
                    result.push_back(std::format("{}: missing {}", path, *name));
                }
            }
        }
        if (const Value* additional = schema.find("additionalProperties");
            additional != nullptr && additional->kind() == Kind::Bool && !additional->asBool().value_or(true)) {
            for (const json::Member& entry : value.members()) {
                if (properties == nullptr || properties->find(entry.key) == nullptr) {
                    result.push_back(std::format("{}: unknown {}", path, entry.key));
                }
            }
        }
        if (properties != nullptr) {
            for (const json::Member& entry : value.members()) {
                if (const Value* childSchema = properties->find(entry.key)) {
                    errorsInto(entry.value, *childSchema, root, std::format("{}.{}", path, entry.key), result);
                }
            }
        }
    }

    if (value.kind() == Kind::Array) {
        const std::span<const Value> items = value.elements();
        if (const Value* minItems = schema.find("minItems")) {
            if (const auto bound = asIntegerNumber(*minItems); bound && std::cmp_less(items.size(), *bound)) {
                result.push_back(std::format("{}: too few items", path));
            }
        }
        if (const Value* maxItems = schema.find("maxItems")) {
            if (const auto bound = asIntegerNumber(*maxItems); bound && std::cmp_greater(items.size(), *bound)) {
                result.push_back(std::format("{}: too many items", path));
            }
        }
        if (const Value* unique = schema.find("uniqueItems"); unique != nullptr && unique->asBool().value_or(false)) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                for (std::size_t j = 0; j < i; ++j) {
                    if (jsonEqual(items[i], items[j])) {
                        result.push_back(std::format("{}: duplicate item", path));
                        i = items.size();  // one report is enough
                        break;
                    }
                }
            }
        }
        if (const Value* itemSchema = schema.find("items")) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                errorsInto(items[i], *itemSchema, root, std::format("{}[{}]", path, i), result);
            }
        }
    }

    if (value.kind() == Kind::String) {
        const std::string_view text = value.asString().value_or("");
        if (const Value* minLength = schema.find("minLength")) {
            if (const auto bound = asIntegerNumber(*minLength); bound && std::cmp_less(text.size(), *bound)) {
                result.push_back(std::format("{}: string too short", path));
            }
        }
        if (const Value* pattern = schema.find("pattern")) {
            if (const auto expression = pattern->asString()) {
                try {
                    const std::regex regex{std::string{*expression}, std::regex::ECMAScript};
                    if (!std::regex_search(std::string{text}, regex)) {
                        result.push_back(std::format("{}: pattern mismatch", path));
                    }
                } catch (const std::regex_error&) {
                    result.push_back(std::format("{}: schema pattern is not a usable regex", path));
                }
            }
        }
    }

    if (isNumberKind(value.kind())) {
        if (const Value* minimum = schema.find("minimum"); minimum != nullptr) {
            if (compareNumbers(value, *minimum) == std::partial_ordering::less) {
                result.push_back(std::format("{}: below minimum", path));
            }
        }
        if (const Value* maximum = schema.find("maximum"); maximum != nullptr) {
            if (compareNumbers(value, *maximum) == std::partial_ordering::greater) {
                result.push_back(std::format("{}: above maximum", path));
            }
        }
    }
}

}  // namespace

bool jsonEqual(const json::Value& left, const json::Value& right) {
    if (isNumberKind(left.kind()) && isNumberKind(right.kind())) {
        return numbersEqual(left, right);
    }
    if (left.kind() != right.kind()) {
        return false;
    }
    switch (left.kind()) {
        case Kind::Null:
            return true;
        case Kind::Bool:
            return left.asBool().value_or(false) == right.asBool().value_or(true);
        case Kind::String:
            return left.asString().value_or("") == right.asString().value_or("\x01");
        case Kind::Array: {
            if (left.elements().size() != right.elements().size()) {
                return false;
            }
            for (std::size_t i = 0; i < left.elements().size(); ++i) {
                if (!jsonEqual(left.elements()[i], right.elements()[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::Object: {
            if (left.members().size() != right.members().size()) {
                return false;
            }
            for (const json::Member& entry : left.members()) {
                const json::Value* other = right.find(entry.key);
                if (other == nullptr || !jsonEqual(entry.value, *other)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

std::vector<std::string> checkSchema(const json::Value& schema) {
    std::vector<std::string> problems;
    checkSchemaInto(schema, schema, problems);
    return problems;
}

std::vector<std::string> validate(const json::Value& value, const json::Value& schema) {
    std::vector<std::string> problems = checkSchema(schema);
    if (!problems.empty()) {
        for (std::string& problem : problems) {
            problem.insert(0, "schema: ");
        }
        return problems;
    }
    std::vector<std::string> result;
    errorsInto(value, schema, schema, "$", result);
    return result;
}

}  // namespace mdux::tools::schema
