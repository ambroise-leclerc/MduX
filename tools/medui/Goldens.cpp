/**
 * @file Goldens.cpp
 * @brief The `@safety_critical` annotation rules, and the golden set they select.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 */
module;

module mdux.tools.medui.goldens;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.layout;
import mdux.tools.medui.semantic;

namespace mdux::tools::medui {

namespace {

/// The one annotation that participates in the predicate. Others are carried by the AST and ignored
/// here; naming a rule for them would need a diagnostic code no accepted document has argued for.
constexpr std::string_view safetyCritical = "safety_critical";

/// The argument that lists the verifications a golden asks for.
constexpr std::string_view cvCheckArgument = "cv_check";

[[nodiscard]] const ast::Field* fieldFor(const ast::Node& node, std::string_view name) noexcept {
    const auto found = std::ranges::find(node.fields, name, &ast::Field::name);
    return found == node.fields.end() ? nullptr : &*found;
}

[[nodiscard]] const ast::Field* argumentFor(const ast::Annotation& annotation, std::string_view name) noexcept {
    const auto found = std::ranges::find(annotation.arguments, name, &ast::Field::name);
    return found == annotation.arguments.end() ? nullptr : &*found;
}

[[nodiscard]] bool isSafetyCritical(const ast::Annotation& annotation) noexcept {
    return annotation.name == safetyCritical;
}

/// Finds a component's field of one value domain, from the dictionary semantic analysis publishes.
///
/// The dictionary rather than a list of field names: `text` on a `Label` and `label` on a `Button`
/// are the same kind of thing, and a second list here would be one more place to forget a component.
[[nodiscard]] const ast::Field* fieldOfDomain(const ast::Node& node, FieldDomain domain) noexcept {
    const std::span<const ComponentRule> dictionary = componentDictionary();
    const auto                           component  = std::ranges::find(dictionary, node.component, &ComponentRule::name);
    if (component == dictionary.end()) {
        return nullptr;
    }
    for (const FieldRule& rule : component->fields) {
        if (rule.domain != domain) {
            continue;
        }
        if (const ast::Field* field = fieldFor(node, rule.name)) {
            return field;
        }
    }
    return nullptr;
}

/// The text a golden pins, or empty when the node draws none - or draws one that varies.
///
/// Only a single static key qualifies. A list-valued field such as `StatusIndicator`'s `states:` is
/// deliberately not read: the state on screen is the varying part, and a golden that pinned one
/// would fail whenever the device showed another.
[[nodiscard]] std::string textKeyOf(const ast::Node& node) {
    const ast::Field* field = fieldOfDomain(node, FieldDomain::TextKey);
    if (field == nullptr || field->value == nullptr || field->value->kind != ast::ValueKind::TextKey) {
        return {};
    }
    return field->value->text;
}

/// The colour token a golden pins, or empty when the node declares none - or declares several.
///
/// `StatusIndicator`'s `colors:` is a list, one per state, and which one is on screen is the
/// varying part. It is left unread for the same reason `states:` is: a golden pins the tint a
/// verifier can rely on, and a component that legitimately changes tint has none.
[[nodiscard]] std::string colorTokenOf(const ast::Node& node) {
    const ast::Field* field = fieldOfDomain(node, FieldDomain::ColorToken);
    if (field == nullptr || field->value == nullptr || field->value->kind != ast::ValueKind::ColorToken) {
        return {};
    }
    return field->value->text;
}

/// Adds `check` unless it is already present. The list is short - two values exist - so a scan
/// costs less than the set that would replace it, and it keeps insertion order out of the result.
void addCheck(std::vector<CvCheck>& checks, CvCheck check) {
    if (std::ranges::find(checks, check) == checks.end()) {
        checks.push_back(check);
    }
}

/// Collects the checks one annotation asks for, failing loudly if validation was bypassed.
///
/// Accepts both `cv_check: [Bounds, ColorHash]` and the single-value `cv_check: Bounds`. The shared
/// language writes the list form and so does every example here; the single form is accepted because
/// it means one unambiguous thing, and rejecting it would need a diagnostic for a shape no author
/// could misread.
void collectAnnotationChecks(const ast::Annotation& annotation, std::vector<CvCheck>& into) {
    const ast::Field* argument = argumentFor(annotation, cvCheckArgument);
    if (argument == nullptr || argument->value == nullptr) {
        return;
    }

    const auto take = [&into](const ast::Value& value) {
        if (value.kind != ast::ValueKind::Identifier) {
            throw std::logic_error(std::format("goldens received a cv_check value that is not a name at line {}, column {}; "
                                               "validateSafetyAnnotations() is a required gate",
                                               value.position.line,
                                               value.position.column));
        }
        const std::optional<CvCheck> check = parseCvCheck(value.text);
        if (!check.has_value()) {
            throw std::logic_error(std::format("goldens received unknown cv_check '{}' at line {}, column {}; "
                                               "validateSafetyAnnotations() is a required gate",
                                               value.text,
                                               value.position.line,
                                               value.position.column));
        }
        addCheck(into, *check);
    };

    if (argument->value->kind == ast::ValueKind::List) {
        for (const std::shared_ptr<ast::Value>& element : argument->value->list) {
            if (element == nullptr) {
                throw std::logic_error("goldens received an empty cv_check element; validateSafetyAnnotations() is a required gate");
            }
            take(*element);
        }
        return;
    }
    take(*argument->value);
}

/// One pass over a parsed screen, applying the annotation rules.
class Validator {
public:
    explicit Validator(std::string file) : file_{std::move(file)} {}

    [[nodiscard]] SafetyResult run(const ast::Screen& screen) {
        for (const ast::Node& node : screen.nodes) {
            validateNode(node);
        }
        return SafetyResult{.diagnostics = std::move(diagnostics_)};
    }

private:
    void report(Code code, ast::Position position, std::string message) {
        diagnostics_.push_back(diagnose(code, file_, position.line, position.column, std::move(message)));
    }

    void validateNode(const ast::Node& node) {
        for (const ast::Annotation& annotation : node.annotations) {
            if (isSafetyCritical(annotation)) {
                validateAnnotation(node, annotation);
            }
        }
        for (const ast::Node& child : node.children) {
            validateNode(child);
        }
    }

    void validateAnnotation(const ast::Node& node, const ast::Annotation& annotation) {
        // Reported at the annotation rather than at the node: the annotation is the claim that
        // needs a requirement behind it, and the shared case pins that position.
        if (fieldFor(node, "requirement") == nullptr) {
            report(Code::SafetyCriticalWithoutRequirement,
                   annotation.position,
                   std::format("component '{}' is annotated @safety_critical but declares no requirement", node.component));
        }

        const ast::Field* argument = argumentFor(annotation, cvCheckArgument);
        if (argument == nullptr || argument->value == nullptr) {
            return;
        }
        if (argument->value->kind == ast::ValueKind::List) {
            for (const std::shared_ptr<ast::Value>& element : argument->value->list) {
                if (element != nullptr) {
                    validateCheckValue(*element);
                }
            }
            return;
        }
        validateCheckValue(*argument->value);
    }

    void validateCheckValue(const ast::Value& value) {
        if (value.kind != ast::ValueKind::Identifier) {
            // MEDUI-E033 rather than a new code: an annotation argument is an `ast::Field`, and
            // "a field value has the wrong semantic kind" is exactly what a quoted or numeric
            // cv_check is. A code invented for the annotation case would mean the same thing twice.
            report(Code::FieldValueKind, value.position, "cv_check takes a name, or a list of names, from the closed set");
            return;
        }
        if (!parseCvCheck(value.text).has_value()) {
            report(Code::UnknownCvCheck,
                   value.position,
                   std::format("'{}' is not a verification this compiler emits; the set is {} and {}",
                               value.text,
                               spell(CvCheck::Bounds),
                               spell(CvCheck::ColorHash)));
        }
    }

    std::string                  file_;
    std::vector<cli::Diagnostic> diagnostics_;
};

}  // namespace

std::string_view spell(CvCheck check) noexcept {
    switch (check) {
        case CvCheck::Bounds:
            return "Bounds";
        case CvCheck::ColorHash:
            return "ColorHash";
    }
    return "";
}

std::optional<CvCheck> parseCvCheck(std::string_view name) noexcept {
    if (name == spell(CvCheck::Bounds)) {
        return CvCheck::Bounds;
    }
    if (name == spell(CvCheck::ColorHash)) {
        return CvCheck::ColorHash;
    }
    return std::nullopt;
}

SafetyResult validateSafetyAnnotations(const ast::Screen& screen, std::string file) {
    return Validator{std::move(file)}.run(screen);
}

std::vector<GoldenReference> collectGoldens(const LayoutResult& layout) {
    std::vector<GoldenReference> references;

    for (const ResolvedNode& node : layout.nodes) {
        std::vector<CvCheck> checks;

        const bool annotated = std::ranges::any_of(node.source.annotations, isSafetyCritical);
        for (const ast::Annotation& annotation : node.source.annotations) {
            if (isSafetyCritical(annotation)) {
                collectAnnotationChecks(annotation, checks);
            }
        }

        // ADR-011: a declared position is a safety-relevant claim by itself, so it adds `Bounds`
        // whether or not the node is annotated. A node matching both rules is here exactly once,
        // because this loop walks resolved nodes rather than the two rules in turn - which is what
        // makes "one merged entry, never two" a property of the shape rather than of a later dedupe.
        if (node.positioned) {
            addCheck(checks, CvCheck::Bounds);
        }

        if (!annotated && !node.positioned) {
            continue;
        }

        // An annotation naming no check still selects the node, and pins its bounds. That is read
        // from ADR-011's positioned rule rather than invented: if a declared position alone is
        // enough to make a rectangle worth verifying, an explicit safety-critical claim cannot be
        // worth less. The alternative - an entry with no checks - would be a golden a verifier
        // reads and does nothing with.
        if (checks.empty()) {
            addCheck(checks, CvCheck::Bounds);
        }
        std::ranges::sort(checks);

        references.push_back(GoldenReference{.nodeId     = node.id,
                                             .bounds     = node.bounds,
                                             .textKey    = textKeyOf(node.source),
                                             .colorToken = colorTokenOf(node.source),
                                             .cvChecks   = std::move(checks)});
    }

    return references;
}

}  // namespace mdux::tools::medui
