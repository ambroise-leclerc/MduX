/**
 * @file Package.cpp
 * @brief Implementation of the compiled-screen document and its canonical JSON form.
 */

module;

module mdux.tools.medui.package;

import std;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;

namespace mdux::tools::medui {

namespace {

namespace ms   = mdux::medui;
namespace json = mdux::evidence::json;

/// Stable diagnostic codes for a malformed committed artifact. Local to this tool, as
/// `mdux-shaderemit`'s `SHE0NN` are; see the interface comment for why these are not `MEDUI-E0NN`.
constexpr std::string_view bytesUnparsed = "SCP001";
constexpr std::string_view memberWrong   = "SCP002";
constexpr std::string_view memberUnknown = "SCP003";
constexpr std::string_view kindUnknown   = "SCP004";
constexpr std::string_view schemaRefused = "SCP005";

// ---------------------------------------------------------------------------
// Reading the AST a resolved node carries
// ---------------------------------------------------------------------------

[[nodiscard]] const ast::Field* fieldFor(const ast::Node& node, std::string_view name) noexcept {
    const auto found = std::ranges::find(node.fields, name, &ast::Field::name);
    return found == node.fields.end() ? nullptr : &*found;
}

/// The text a field carries, or empty when the component did not declare it.
///
/// Every name-valued domain - `String`, `TextKey`, `ImageRef`, `ColorToken`, `Identifier` - stores
/// its characters in the same AST member, so one accessor serves all five. A field holding some
/// other domain means semantic analysis did not run, which is a bypassed gate rather than a
/// malformed screen.
[[nodiscard]] std::string_view textOf(const ast::Node& node, std::string_view name) {
    const ast::Field* field = fieldFor(node, name);
    if (field == nullptr || field->value == nullptr) {
        return {};
    }
    switch (field->value->kind) {
        case ast::ValueKind::String:
        case ast::ValueKind::TextKey:
        case ast::ValueKind::ImageRef:
        case ast::ValueKind::ColorToken:
        case ast::ValueKind::Identifier:
            return field->value->text;
        default:
            throw std::logic_error(std::format("field '{}' on {} does not hold a name: semantic analysis is a required gate", name, node.component));
    }
}

[[nodiscard]] std::int64_t numberOf(const ast::Node& node, std::string_view name) {
    const ast::Field* field = fieldFor(node, name);
    if (field == nullptr || field->value == nullptr) {
        return 0;
    }
    if (field->value->kind != ast::ValueKind::Number) {
        throw std::logic_error(std::format("field '{}' on {} does not hold a number: semantic analysis is a required gate", name, node.component));
    }
    return field->value->number;
}

/// The names in a list-valued field, in source order. Empty when the component declared none.
[[nodiscard]] std::vector<std::string> listOf(const ast::Node& node, std::string_view name) {
    const ast::Field* field = fieldFor(node, name);
    if (field == nullptr || field->value == nullptr) {
        return {};
    }
    if (field->value->kind != ast::ValueKind::List) {
        throw std::logic_error(std::format("field '{}' on {} does not hold a list: semantic analysis is a required gate", name, node.component));
    }
    std::vector<std::string> names;
    names.reserve(field->value->list.size());
    for (const std::shared_ptr<ast::Value>& element : field->value->list) {
        if (element == nullptr) {
            throw std::logic_error(std::format("field '{}' on {} holds an empty list element", name, node.component));
        }
        // Checked for the same reason `textOf()` checks: an element of some other domain carries no
        // text, so taking it silently would put an empty name in a state key and surface as the
        // schema refusing the package - a confusing report of a bypassed gate rather than a clear one.
        switch (element->kind) {
            case ast::ValueKind::String:
            case ast::ValueKind::TextKey:
            case ast::ValueKind::ImageRef:
            case ast::ValueKind::ColorToken:
            case ast::ValueKind::Identifier:
                names.push_back(element->text);
                break;
            default:
                throw std::logic_error(
                    std::format("field '{}' on {} lists a value that is not a name: semantic analysis is a required gate", name, node.component));
        }
    }
    return names;
}

/**
 * @brief The typed payload for one resolved node.
 *
 * The mapping from a dictionary field to a spec member is the whole content of this function, and
 * it is spelled once here rather than being derivable: `text` on a `Label` and `label` on a
 * `Button` are both a `textKey`, while `source` means a data stream on one component and an image
 * package on another. A table keyed by field name could not express that.
 */
[[nodiscard]] ms::NodePayload payloadFor(const ResolvedNode& resolved, ScreenDocument& document) {
    const ast::Node& source = resolved.source;
    const auto       name   = [&](std::string_view field) {
        return document.intern(textOf(source, field));
    };

    // The only synthetic node the solver produces, and the only way a Panel is ever compiled: an
    // author cannot write one, because `Panel` is not in the component dictionary.
    if (resolved.component == "Panel") {
        return ms::PanelSpec{.colorToken = name("background")};
    }
    if (resolved.component == "Label") {
        return ms::LabelSpec{.textKey = name("text"), .colorToken = name("color")};
    }
    if (resolved.component == "Clock") {
        return ms::ClockSpec{.format = name("format")};
    }
    if (resolved.component == "Image") {
        return ms::ImageSpec{.source = name("source")};
    }
    if (resolved.component == "VulkanViewport") {
        return ms::VulkanViewportSpec{.streamSource = name("stream_source")};
    }
    if (resolved.component == "SignalTrace") {
        return ms::SignalTraceSpec{.streamSource = name("stream_source"), .colorToken = name("color")};
    }
    if (resolved.component == "Button") {
        return ms::ButtonSpec{.labelKey = name("label"), .colorToken = name("color"), .source = name("source"), .requirement = name("requirement")};
    }
    if (resolved.component == "CriticalButton") {
        return ms::CriticalButtonSpec{.requirement = name("requirement"), .labelKey = name("label"), .colorToken = name("color"), .onPress = name("on_press")};
    }
    if (resolved.component == "NumericDisplay") {
        return ms::NumericDisplaySpec{.requirement = name("requirement"),
                                      .templateId  = name("template"),
                                      .source      = name("source"),
                                      .colorToken  = name("color")};
    }
    if (resolved.component == "StatusIndicator") {
        return ms::StatusIndicatorSpec{.requirement = name("requirement"),
                                       .source      = name("source"),
                                       .stateKeys   = document.internList(listOf(source, "states")),
                                       .colorTokens = document.internList(listOf(source, "colors"))};
    }
    if (resolved.component == "TextInput") {
        return ms::TextInputSpec{.source      = name("source"),
                                 .colorToken  = name("color"),
                                 .maxLength   = numberOf(source, "max_length"),
                                 .charset     = name("charset"),
                                 .requirement = name("requirement")};
    }
    // `Row` reaches here only if the solver stopped flattening: a Row contributes its children and
    // at most one synthetic background, never itself. Anything else is a component semantic
    // analysis would have refused.
    throw std::logic_error(std::format("no compiled payload for component '{}': semantic analysis is a required gate", resolved.component));
}

/// Narrows a solved coordinate to what the compiled schema carries.
///
/// The layout solver already refuses a surface wider than `std::int32_t` and every rectangle it
/// resolves lies inside that surface, so a value out of range here means layout was bypassed.
[[nodiscard]] std::int32_t narrow(std::int64_t value, std::string_view what) {
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::logic_error(std::format("{} is {}, which a compiled screen cannot carry: layout is a required gate", what, value));
    }
    return static_cast<std::int32_t>(value);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

/// Inserts a member, turning the writer's `Result` into the exception a host tool may throw.
void put(json::Value& object, std::string key, json::Value value) {
    if (const auto inserted = object.set(std::move(key), std::move(value)); !inserted.has_value()) {
        throw std::logic_error(std::format("canonical JSON refused a member: {}", json::describe(inserted.error().code)));
    }
}

void putName(json::Value& object, std::string_view key, std::string_view value) {
    // An optional name the component did not declare is omitted rather than written empty, as
    // `goldens.json` omits an absent `textKey`. A required name is never empty in a validated
    // package, so this cannot drop one.
    if (value.empty()) {
        return;
    }
    put(object, std::string{key}, json::Value::string(std::string{value}));
}

/// One rectangle, from the compiled schema's `NodeRect` or from the solver's `LayoutRect`.
///
/// A template rather than two overloads: the two types spell their members identically and differ
/// only in integer width, so two bodies would be two places for the member names of a byte-compared
/// artifact to drift apart.
template <typename Rect>
[[nodiscard]] json::Value writeRect(const Rect& bounds) {
    json::Value object = json::Value::emptyObject();
    put(object, "x", json::Value::integer(bounds.x));
    put(object, "y", json::Value::integer(bounds.y));
    put(object, "width", json::Value::integer(bounds.width));
    put(object, "height", json::Value::integer(bounds.height));
    return object;
}

[[nodiscard]] json::Value writeNames(std::span<const std::string_view> names) {
    std::vector<json::Value> elements;
    elements.reserve(names.size());
    for (const std::string_view name : names) {
        elements.push_back(json::Value::string(std::string{name}));
    }
    return json::Value::array(std::move(elements));
}

[[nodiscard]] std::string hexOf(const mdux::evidence::Digest& digest) {
    const std::array<char, 64> hex = mdux::evidence::toHex(digest);
    return {hex.data(), hex.size()};
}

[[nodiscard]] json::Value writeApprovals(std::span<const ms::TextPackageApproval> approvals) {
    std::vector<json::Value> entries;
    entries.reserve(approvals.size());
    for (const ms::TextPackageApproval& approval : approvals) {
        json::Value entry = json::Value::emptyObject();
        put(entry, "locale", json::Value::string(std::string{approval.locale}));
        put(entry, "packageId", json::Value::string(std::string{approval.packageId}));
        put(entry, "packageSha256", json::Value::string(hexOf(approval.packageSha256)));
        entries.push_back(std::move(entry));
    }
    return json::Value::array(std::move(entries));
}

/// The `spec` object for one payload: the component's own fields, and nothing shared.
[[nodiscard]] json::Value writeSpec(const ms::NodePayload& payload) {
    json::Value spec = json::Value::emptyObject();

    if (const auto* panel = std::get_if<ms::PanelSpec>(&payload)) {
        putName(spec, "colorToken", panel->colorToken);
    } else if (const auto* label = std::get_if<ms::LabelSpec>(&payload)) {
        putName(spec, "colorToken", label->colorToken);
        putName(spec, "textKey", label->textKey);
    } else if (const auto* clock = std::get_if<ms::ClockSpec>(&payload)) {
        putName(spec, "format", clock->format);
    } else if (const auto* image = std::get_if<ms::ImageSpec>(&payload)) {
        putName(spec, "source", image->source);
    } else if (const auto* viewport = std::get_if<ms::VulkanViewportSpec>(&payload)) {
        putName(spec, "streamSource", viewport->streamSource);
    } else if (const auto* trace = std::get_if<ms::SignalTraceSpec>(&payload)) {
        putName(spec, "colorToken", trace->colorToken);
        putName(spec, "streamSource", trace->streamSource);
    } else if (const auto* button = std::get_if<ms::ButtonSpec>(&payload)) {
        putName(spec, "colorToken", button->colorToken);
        putName(spec, "labelKey", button->labelKey);
        putName(spec, "requirement", button->requirement);
        putName(spec, "source", button->source);
    } else if (const auto* critical = std::get_if<ms::CriticalButtonSpec>(&payload)) {
        putName(spec, "colorToken", critical->colorToken);
        putName(spec, "labelKey", critical->labelKey);
        putName(spec, "onPress", critical->onPress);
        putName(spec, "requirement", critical->requirement);
    } else if (const auto* numeric = std::get_if<ms::NumericDisplaySpec>(&payload)) {
        putName(spec, "colorToken", numeric->colorToken);
        putName(spec, "requirement", numeric->requirement);
        putName(spec, "source", numeric->source);
        putName(spec, "templateId", numeric->templateId);
    } else if (const auto* status = std::get_if<ms::StatusIndicatorSpec>(&payload)) {
        // Written even when empty, unlike a name: an empty list is the component declaring no
        // per-state tint, which is a different statement from a name it does not have.
        put(spec, "colorTokens", writeNames(status->colorTokens));
        putName(spec, "requirement", status->requirement);
        putName(spec, "source", status->source);
        put(spec, "stateKeys", writeNames(status->stateKeys));
    } else if (const auto* input = std::get_if<ms::TextInputSpec>(&payload)) {
        putName(spec, "charset", input->charset);
        putName(spec, "colorToken", input->colorToken);
        put(spec, "maxLength", json::Value::integer(input->maxLength));
        putName(spec, "requirement", input->requirement);
        putName(spec, "source", input->source);
    } else {
        // Unreachable for a validated package: `validate()` refuses an unknown payload, and
        // `writePackage()` validates before rendering.
        throw std::logic_error("a node carries a payload this writer does not know");
    }
    return spec;
}

}  // namespace

// ---------------------------------------------------------------------------
// ScreenDocument
// ---------------------------------------------------------------------------

std::string_view ScreenDocument::intern(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    return text_.emplace_back(text);
}

std::span<const std::string_view> ScreenDocument::internList(std::span<const std::string> items) {
    std::vector<std::string_view>& views = lists_.emplace_back();
    views.reserve(items.size());
    for (const std::string& item : items) {
        views.push_back(intern(item));
    }
    return views;
}

void ScreenDocument::setHeader(std::string_view                         id,
                               std::int32_t                             surfaceWidth,
                               std::int32_t                             surfaceHeight,
                               mdux::draw::DrawBudget                   budget,
                               std::span<const ms::TextPackageApproval> approvedTextPackages) {
    id_            = intern(id);
    surfaceWidth_  = surfaceWidth;
    surfaceHeight_ = surfaceHeight;
    budget_        = budget;
    approvedTextPackages_.reserve(approvedTextPackages.size());
    for (const ms::TextPackageApproval& approval : approvedTextPackages) {
        approvedTextPackages_.push_back(
            ms::TextPackageApproval{.locale = intern(approval.locale), .packageId = intern(approval.packageId), .packageSha256 = approval.packageSha256});
    }
}

void ScreenDocument::addNode(ms::CompiledNode node) {
    nodes_.push_back(std::move(node));
}

ms::ScreenPackage ScreenDocument::package() const noexcept {
    return ms::ScreenPackage{.id                   = id_,
                             .schemaVersion        = mdux::evidence::kSchemaVersion,
                             .surfaceWidth         = surfaceWidth_,
                             .surfaceHeight        = surfaceHeight_,
                             .approvedTextPackages = approvedTextPackages_,
                             .nodes                = nodes_,
                             .budget               = budget_};
}

// ---------------------------------------------------------------------------
// Building from a resolved layout
// ---------------------------------------------------------------------------

ScreenDocument buildPackage(const LayoutResult& layout, PackageInputs inputs) {
    if (!layout.ok()) {
        throw std::logic_error("buildPackage was given a layout that did not resolve: layout is a required gate");
    }

    ScreenDocument document;
    document.setHeader(inputs.id,
                       narrow(layout.surfaceWidth, "surface width"),
                       narrow(layout.surfaceHeight, "surface height"),
                       inputs.budget,
                       inputs.approvedTextPackages);

    for (const ResolvedNode& resolved : layout.nodes) {
        document.addNode(ms::CompiledNode{
            .id      = document.intern(resolved.id),
            .bounds  = ms::NodeRect{.x      = narrow(resolved.bounds.x, "node x"),
                                    .y      = narrow(resolved.bounds.y, "node y"),
                                    .width  = narrow(resolved.bounds.width, "node width"),
                                    .height = narrow(resolved.bounds.height, "node height")},
            .payload = payloadFor(resolved, document)
        });
    }

    if (const auto valid = document.package().validate(); !valid.has_value()) {
        throw std::logic_error(std::format("the compiled screen does not satisfy its own schema: {}", ms::describe(valid.error())));
    }
    return document;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

std::string writePackage(const ms::ScreenPackage& package) {
    if (const auto valid = package.validate(); !valid.has_value()) {
        throw std::logic_error(std::format("refusing to write a screen the schema rejects: {}", ms::describe(valid.error())));
    }

    std::vector<json::Value> nodes;
    nodes.reserve(package.nodes.size());
    for (const ms::CompiledNode& node : package.nodes) {
        json::Value entry = json::Value::emptyObject();
        put(entry, "bounds", writeRect(node.bounds));
        put(entry, "id", json::Value::string(std::string{node.id}));
        put(entry, "kind", json::Value::string(std::string{ms::kindName(node)}));
        put(entry, "spec", writeSpec(node.payload));
        nodes.push_back(std::move(entry));
    }

    json::Value budget = json::Value::emptyObject();
    put(budget, "maxCommands", json::Value::unsignedInteger(package.budget.maxCommands));
    put(budget, "maxIndices", json::Value::unsignedInteger(package.budget.maxIndices));
    put(budget, "maxVertices", json::Value::unsignedInteger(package.budget.maxVertices));

    json::Value root = json::Value::emptyObject();
    put(root, "approvedTextPackages", writeApprovals(package.approvedTextPackages));
    put(root, "budget", std::move(budget));
    put(root, "id", json::Value::string(std::string{package.id}));
    put(root, "kind", json::Value::string(std::string{ms::packageKind}));
    put(root, "nodes", json::Value::array(std::move(nodes)));
    put(root, "schemaVersion", json::Value::unsignedInteger(package.schemaVersion));
    put(root, "surfaceHeight", json::Value::integer(package.surfaceHeight));
    put(root, "surfaceWidth", json::Value::integer(package.surfaceWidth));

    auto written = json::write(root);
    if (!written.has_value()) {
        throw std::logic_error(std::format("canonical JSON refused the screen: {}", json::describe(written.error().code)));
    }
    return std::move(*written);
}

std::string writeGoldens(std::span<const GoldenReference> goldens) {
    std::vector<json::Value> entries;
    entries.reserve(goldens.size());
    for (const GoldenReference& golden : goldens) {
        std::vector<json::Value> checks;
        checks.reserve(golden.cvChecks.size());
        for (const CvCheck check : golden.cvChecks) {
            checks.push_back(json::Value::string(std::string{spell(check)}));
        }

        json::Value entry = json::Value::emptyObject();
        put(entry, "bounds", writeRect(golden.bounds));
        putName(entry, "colorToken", golden.colorToken);
        put(entry, "cvChecks", json::Value::array(std::move(checks)));
        put(entry, "nodeId", json::Value::string(golden.nodeId));
        putName(entry, "textKey", golden.textKey);
        entries.push_back(std::move(entry));
    }

    // An empty array rather than no file: ADR-012 makes all three outputs unconditional, because
    // `mdux_bake_artifact()` declares each as a build output and a skipped one breaks the
    // byte-comparison test rather than meaning "this screen pins nothing".
    auto written = json::write(json::Value::array(std::move(entries)));
    if (!written.has_value()) {
        throw std::logic_error(std::format("canonical JSON refused the golden references: {}", json::describe(written.error().code)));
    }
    return std::move(*written);
}


// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

/// Where a read failure goes, and the file it names.
///
/// Every helper returns false or nullopt after reporting, so a caller stops at the first failure.
/// Deliberately not an accumulating pass like the authoring stages: those report on a source a
/// human is editing, where a list of problems saves round trips. This reads a generated, reviewed,
/// byte-compared artifact - if it is malformed at all, the first place it stops being canonical is
/// the actionable fact.
class Sink {
public:
    Sink(std::string file, std::vector<mdux::tools::cli::Diagnostic>& diagnostics) : file_{std::move(file)}, diagnostics_{&diagnostics} {}

    bool fail(std::string_view code, std::string message, std::string fixHint = {}) const {
        diagnostics_->push_back(mdux::tools::cli::Diagnostic{.file     = file_,
                                                             .code     = std::string{code},
                                                             .severity = mdux::tools::cli::Severity::Error,
                                                             .message  = std::move(message),
                                                             .fixHint  = std::move(fixHint)});
        return false;
    }

private:
    std::string                                file_;
    std::vector<mdux::tools::cli::Diagnostic>* diagnostics_;
};

/// Checks that `value` is an object and carries no member outside `allowed`.
///
/// Rejecting an unknown member is the fail-closed direction for a reviewed artifact: a member this
/// reader ignored would be one a reviewer believed was doing something.
[[nodiscard]] bool expectObject(const json::Value& value, std::string_view what, std::span<const std::string_view> allowed, const Sink& sink) {
    if (value.kind() != json::Value::Kind::Object) {
        return sink.fail(memberWrong, std::format("{} is not an object", what));
    }
    for (const json::Member& member : value.members()) {
        if (std::ranges::find(allowed, member.key) == allowed.end()) {
            return sink.fail(memberUnknown,
                             std::format("{} carries a member '{}' the compiled-screen format does not define", what, member.key),
                             "the format is fixed by ADR-012; a member that is not in it means the file was hand-edited or written by "
                             "another tool");
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> readName(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return std::string{};
    }
    const auto text = member->asString();
    if (!text.has_value()) {
        sink.fail(memberWrong, std::format("{} member '{}' is not a string", what, key));
        return std::nullopt;
    }
    return std::string{*text};
}

[[nodiscard]] std::optional<mdux::evidence::Digest> readDigest(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        sink.fail(memberWrong, std::format("{} is missing the member '{}'", what, key));
        return std::nullopt;
    }
    const auto text = member->asString();
    if (!text.has_value()) {
        sink.fail(memberWrong, std::format("{} member '{}' is not a lowercase SHA-256 string", what, key));
        return std::nullopt;
    }
    auto digest = mdux::evidence::digestFromHex(*text);
    if (!digest.has_value()) {
        sink.fail(memberWrong, std::format("{} member '{}' is not 64 lowercase hexadecimal digits", what, key));
        return std::nullopt;
    }
    return *digest;
}

[[nodiscard]] std::optional<std::int64_t> readInteger(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        sink.fail(memberWrong, std::format("{} is missing the member '{}'", what, key));
        return std::nullopt;
    }
    const auto number = member->asInt();
    if (!number.has_value()) {
        sink.fail(memberWrong, std::format("{} member '{}' is not an integer", what, key));
        return std::nullopt;
    }
    return *number;
}

[[nodiscard]] std::optional<std::int32_t> readInt32(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const auto number = readInteger(object, key, what, sink);
    if (!number.has_value()) {
        return std::nullopt;
    }
    if (*number < std::numeric_limits<std::int32_t>::min() || *number > std::numeric_limits<std::int32_t>::max()) {
        sink.fail(memberWrong, std::format("{} member '{}' is {}, which a compiled screen cannot carry", what, key, *number));
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*number);
}

[[nodiscard]] std::optional<std::uint32_t> readUInt32(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const auto number = readInteger(object, key, what, sink);
    if (!number.has_value()) {
        return std::nullopt;
    }
    if (*number < 0 || *number > std::numeric_limits<std::uint32_t>::max()) {
        sink.fail(memberWrong, std::format("{} member '{}' is {}, which is outside a draw budget's range", what, key, *number));
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*number);
}

/// A list-valued spec member, absent meaning empty.
[[nodiscard]] std::optional<std::vector<std::string>> readNames(const json::Value& object, std::string_view key, std::string_view what, const Sink& sink) {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return std::vector<std::string>{};
    }
    if (member->kind() != json::Value::Kind::Array) {
        sink.fail(memberWrong, std::format("{} member '{}' is not an array", what, key));
        return std::nullopt;
    }
    std::vector<std::string> names;
    names.reserve(member->elements().size());
    for (const json::Value& element : member->elements()) {
        const auto text = element.asString();
        if (!text.has_value()) {
            sink.fail(memberWrong, std::format("{} member '{}' holds a value that is not a string", what, key));
            return std::nullopt;
        }
        names.emplace_back(*text);
    }
    return names;
}

[[nodiscard]] std::optional<ms::NodeRect> readRect(const json::Value& node, std::string_view what, const Sink& sink) {
    static constexpr std::array<std::string_view, 4> allowed{"height", "width", "x", "y"};
    const json::Value*                               member = node.find("bounds");
    if (member == nullptr) {
        sink.fail(memberWrong, std::format("{} is missing the member 'bounds'", what));
        return std::nullopt;
    }
    if (!expectObject(*member, std::format("{} bounds", what), allowed, sink)) {
        return std::nullopt;
    }
    const auto x      = readInt32(*member, "x", what, sink);
    const auto y      = readInt32(*member, "y", what, sink);
    const auto width  = readInt32(*member, "width", what, sink);
    const auto height = readInt32(*member, "height", what, sink);
    if (!x.has_value() || !y.has_value() || !width.has_value() || !height.has_value()) {
        return std::nullopt;
    }
    return ms::NodeRect{.x = *x, .y = *y, .width = *width, .height = *height};
}

/**
 * @brief Reads one node's `spec` into the payload its `kind` names.
 *
 * Every name is read as optional here, and that is not laxity: which names a component *must*
 * declare is fixed by the dictionary and enforced by `validatePayload()`, which runs over the whole
 * package a few lines later. Listing the required ones again in this reader would be a second
 * authority on the same question, free to drift from the first.
 */
[[nodiscard]] bool
readSpec(const json::Value& node, std::string_view kind, std::string_view what, ScreenDocument& document, ms::NodePayload& payload, const Sink& sink) {
    const json::Value* spec = node.find("spec");
    if (spec == nullptr) {
        return sink.fail(memberWrong, std::format("{} is missing the member 'spec'", what));
    }
    const std::string where = std::format("{} spec", what);

    // One list per kind, in the order `writeSpec()` writes them. The round-trip scenario in
    // PackageTests executes both directions over one screen, which is what keeps these in step -
    // an assertion on either half alone could not.
    static constexpr std::array<std::string_view, 1> panelMembers{"colorToken"};
    static constexpr std::array<std::string_view, 2> labelMembers{"colorToken", "textKey"};
    static constexpr std::array<std::string_view, 1> clockMembers{"format"};
    static constexpr std::array<std::string_view, 1> imageMembers{"source"};
    static constexpr std::array<std::string_view, 1> viewportMembers{"streamSource"};
    static constexpr std::array<std::string_view, 2> traceMembers{"colorToken", "streamSource"};
    static constexpr std::array<std::string_view, 4> buttonMembers{"colorToken", "labelKey", "requirement", "source"};
    static constexpr std::array<std::string_view, 4> criticalMembers{"colorToken", "labelKey", "onPress", "requirement"};
    static constexpr std::array<std::string_view, 4> numericMembers{"colorToken", "requirement", "source", "templateId"};
    static constexpr std::array<std::string_view, 4> statusMembers{"colorTokens", "requirement", "source", "stateKeys"};
    static constexpr std::array<std::string_view, 5> inputMembers{"charset", "colorToken", "maxLength", "requirement", "source"};

    // Interns one spec name. The member set has already been checked by `expectObject()` on the
    // branch that calls this, so what remains is reading the names the kind admits.
    const auto name = [&](std::string_view key) -> std::optional<std::string_view> {
        const auto text = readName(*spec, key, where, sink);
        if (!text.has_value()) {
            return std::nullopt;
        }
        return document.intern(*text);
    };

    if (kind == "Panel") {
        if (!expectObject(*spec, where, panelMembers, sink)) {
            return false;
        }
        const auto colorToken = name("colorToken");
        if (!colorToken.has_value()) {
            return false;
        }
        payload = ms::PanelSpec{.colorToken = *colorToken};
        return true;
    }
    if (kind == "Label") {
        if (!expectObject(*spec, where, labelMembers, sink)) {
            return false;
        }
        const auto textKey    = name("textKey");
        const auto colorToken = name("colorToken");
        if (!textKey.has_value() || !colorToken.has_value()) {
            return false;
        }
        payload = ms::LabelSpec{.textKey = *textKey, .colorToken = *colorToken};
        return true;
    }
    if (kind == "Clock") {
        if (!expectObject(*spec, where, clockMembers, sink)) {
            return false;
        }
        const auto format = name("format");
        if (!format.has_value()) {
            return false;
        }
        payload = ms::ClockSpec{.format = *format};
        return true;
    }
    if (kind == "Image") {
        if (!expectObject(*spec, where, imageMembers, sink)) {
            return false;
        }
        const auto source = name("source");
        if (!source.has_value()) {
            return false;
        }
        payload = ms::ImageSpec{.source = *source};
        return true;
    }
    if (kind == "VulkanViewport") {
        if (!expectObject(*spec, where, viewportMembers, sink)) {
            return false;
        }
        const auto streamSource = name("streamSource");
        if (!streamSource.has_value()) {
            return false;
        }
        payload = ms::VulkanViewportSpec{.streamSource = *streamSource};
        return true;
    }
    if (kind == "SignalTrace") {
        if (!expectObject(*spec, where, traceMembers, sink)) {
            return false;
        }
        const auto streamSource = name("streamSource");
        const auto colorToken   = name("colorToken");
        if (!streamSource.has_value() || !colorToken.has_value()) {
            return false;
        }
        payload = ms::SignalTraceSpec{.streamSource = *streamSource, .colorToken = *colorToken};
        return true;
    }
    if (kind == "Button") {
        if (!expectObject(*spec, where, buttonMembers, sink)) {
            return false;
        }
        const auto labelKey    = name("labelKey");
        const auto colorToken  = name("colorToken");
        const auto source      = name("source");
        const auto requirement = name("requirement");
        if (!labelKey.has_value() || !colorToken.has_value() || !source.has_value() || !requirement.has_value()) {
            return false;
        }
        payload = ms::ButtonSpec{.labelKey = *labelKey, .colorToken = *colorToken, .source = *source, .requirement = *requirement};
        return true;
    }
    if (kind == "CriticalButton") {
        if (!expectObject(*spec, where, criticalMembers, sink)) {
            return false;
        }
        const auto requirement = name("requirement");
        const auto labelKey    = name("labelKey");
        const auto colorToken  = name("colorToken");
        const auto onPress     = name("onPress");
        if (!requirement.has_value() || !labelKey.has_value() || !colorToken.has_value() || !onPress.has_value()) {
            return false;
        }
        payload = ms::CriticalButtonSpec{.requirement = *requirement, .labelKey = *labelKey, .colorToken = *colorToken, .onPress = *onPress};
        return true;
    }
    if (kind == "NumericDisplay") {
        if (!expectObject(*spec, where, numericMembers, sink)) {
            return false;
        }
        const auto requirement = name("requirement");
        const auto templateId  = name("templateId");
        const auto source      = name("source");
        const auto colorToken  = name("colorToken");
        if (!requirement.has_value() || !templateId.has_value() || !source.has_value() || !colorToken.has_value()) {
            return false;
        }
        payload = ms::NumericDisplaySpec{.requirement = *requirement, .templateId = *templateId, .source = *source, .colorToken = *colorToken};
        return true;
    }
    if (kind == "StatusIndicator") {
        if (!expectObject(*spec, where, statusMembers, sink)) {
            return false;
        }
        const auto requirement = name("requirement");
        const auto source      = name("source");
        const auto stateKeys   = readNames(*spec, "stateKeys", where, sink);
        const auto colorTokens = readNames(*spec, "colorTokens", where, sink);
        if (!requirement.has_value() || !source.has_value() || !stateKeys.has_value() || !colorTokens.has_value()) {
            return false;
        }
        payload = ms::StatusIndicatorSpec{.requirement = *requirement,
                                          .source      = *source,
                                          .stateKeys   = document.internList(*stateKeys),
                                          .colorTokens = document.internList(*colorTokens)};
        return true;
    }
    if (kind == "TextInput") {
        if (!expectObject(*spec, where, inputMembers, sink)) {
            return false;
        }
        const auto source      = name("source");
        const auto colorToken  = name("colorToken");
        const auto maxLength   = readInteger(*spec, "maxLength", where, sink);
        const auto charset     = name("charset");
        const auto requirement = name("requirement");
        if (!source.has_value() || !colorToken.has_value() || !maxLength.has_value() || !charset.has_value() || !requirement.has_value()) {
            return false;
        }
        payload = ms::TextInputSpec{.source = *source, .colorToken = *colorToken, .maxLength = *maxLength, .charset = *charset, .requirement = *requirement};
        return true;
    }
    return sink.fail(kindUnknown,
                     std::format("{} names the component '{}', which is not in the dictionary", what, kind),
                     "a compiled node's kind is one of the eleven ADR-011 fixes");
}

}  // namespace

PackageReadResult readPackage(std::string_view text, std::string file) {
    PackageReadResult result;
    const Sink        sink{std::move(file), result.diagnostics};

    auto parsed = json::parse(text);
    if (!parsed.has_value()) {
        sink.fail(bytesUnparsed,
                  std::format("the file is not canonical JSON: {} at byte {}", json::describe(parsed.error().code), parsed.error().offset),
                  "canonical form is what mdux.evidence.json writes; re-bake rather than editing the artifact");
        return result;
    }

    static constexpr std::array<std::string_view, 8>
        rootMembers{"approvedTextPackages", "budget", "id", "kind", "nodes", "schemaVersion", "surfaceHeight", "surfaceWidth"};
    static constexpr std::array<std::string_view, 3> budgetMembers{"maxCommands", "maxIndices", "maxVertices"};
    static constexpr std::array<std::string_view, 4> nodeMembers{"bounds", "id", "kind", "spec"};
    static constexpr std::array<std::string_view, 3> approvalMembers{"locale", "packageId", "packageSha256"};

    const json::Value& root = *parsed;
    if (!expectObject(root, "the package", rootMembers, sink)) {
        return result;
    }

    const auto id            = readName(root, "id", "the package", sink);
    const auto kind          = readName(root, "kind", "the package", sink);
    const auto schemaVersion = readInteger(root, "schemaVersion", "the package", sink);
    const auto surfaceWidth  = readInt32(root, "surfaceWidth", "the package", sink);
    const auto surfaceHeight = readInt32(root, "surfaceHeight", "the package", sink);
    if (!id.has_value() || !kind.has_value() || !schemaVersion.has_value() || !surfaceWidth.has_value() || !surfaceHeight.has_value()) {
        return result;
    }
    if (*kind != ms::packageKind) {
        sink.fail(memberWrong, std::format("the package names the kind '{}' rather than '{}'", *kind, ms::packageKind));
        return result;
    }
    if (static_cast<std::uint64_t>(*schemaVersion) != mdux::evidence::kSchemaVersion) {
        sink.fail(memberWrong,
                  std::format("the package declares schema version {}, and this build reads version {}", *schemaVersion, mdux::evidence::kSchemaVersion));
        return result;
    }

    const json::Value* budgetValue = root.find("budget");
    if (budgetValue == nullptr) {
        sink.fail(memberWrong, "the package is missing the member 'budget'");
        return result;
    }
    if (!expectObject(*budgetValue, "the budget", budgetMembers, sink)) {
        return result;
    }
    const auto maxVertices = readUInt32(*budgetValue, "maxVertices", "the budget", sink);
    const auto maxIndices  = readUInt32(*budgetValue, "maxIndices", "the budget", sink);
    const auto maxCommands = readUInt32(*budgetValue, "maxCommands", "the budget", sink);
    if (!maxVertices.has_value() || !maxIndices.has_value() || !maxCommands.has_value()) {
        return result;
    }

    const json::Value* approvalsValue = root.find("approvedTextPackages");
    if (approvalsValue == nullptr || approvalsValue->kind() != json::Value::Kind::Array) {
        sink.fail(memberWrong, "the package member 'approvedTextPackages' is missing or is not an array");
        return result;
    }
    std::vector<ms::TextPackageApproval> approvals;
    std::vector<std::string>             approvalLocales;
    std::vector<std::string>             approvalPackageIds;
    approvals.reserve(approvalsValue->elements().size());
    approvalLocales.reserve(approvalsValue->elements().size());
    approvalPackageIds.reserve(approvalsValue->elements().size());
    for (std::size_t index = 0; index < approvalsValue->elements().size(); ++index) {
        const json::Value& entry = approvalsValue->elements()[index];
        const std::string  what  = std::format("approved text package {}", index);
        if (!expectObject(entry, what, approvalMembers, sink)) {
            return result;
        }
        const auto locale        = readName(entry, "locale", what, sink);
        const auto packageId     = readName(entry, "packageId", what, sink);
        const auto packageSha256 = readDigest(entry, "packageSha256", what, sink);
        if (!locale.has_value() || !packageId.has_value() || !packageSha256.has_value()) {
            return result;
        }
        approvalLocales.push_back(*locale);
        approvalPackageIds.push_back(*packageId);
        approvals.push_back(ms::TextPackageApproval{.locale = approvalLocales.back(), .packageId = approvalPackageIds.back(), .packageSha256 = *packageSha256});
    }

    ScreenDocument document;
    document.setHeader(*id,
                       *surfaceWidth,
                       *surfaceHeight,
                       mdux::draw::DrawBudget{.maxVertices = *maxVertices, .maxIndices = *maxIndices, .maxCommands = *maxCommands},
                       approvals);

    const json::Value* nodes = root.find("nodes");
    if (nodes == nullptr || nodes->kind() != json::Value::Kind::Array) {
        sink.fail(memberWrong, "the package member 'nodes' is missing or is not an array");
        return result;
    }

    for (std::size_t index = 0; index < nodes->elements().size(); ++index) {
        const json::Value& entry = nodes->elements()[index];
        const std::string  what  = std::format("node {}", index);
        if (!expectObject(entry, what, nodeMembers, sink)) {
            return result;
        }
        const auto nodeId   = readName(entry, "id", what, sink);
        const auto nodeKind = readName(entry, "kind", what, sink);
        const auto bounds   = readRect(entry, what, sink);
        if (!nodeId.has_value() || !nodeKind.has_value() || !bounds.has_value()) {
            return result;
        }
        ms::NodePayload payload;
        if (!readSpec(entry, *nodeKind, what, document, payload, sink)) {
            return result;
        }
        document.addNode(ms::CompiledNode{.id = document.intern(*nodeId), .bounds = *bounds, .payload = payload});
    }

    // The same check a device runs, and the reason this reader does not repeat the dictionary's
    // required-name rules itself: one authority decides what a compiled screen must contain.
    if (const auto valid = document.package().validate(); !valid.has_value()) {
        sink.fail(schemaRefused,
                  std::format("the screen the file describes is not valid: {}", ms::describe(valid.error())),
                  "re-bake the screen from its source rather than editing the artifact");
        return result;
    }

    result.document = std::move(document);
    return result;
}

}  // namespace mdux::tools::medui
