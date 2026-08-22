/**
 * @file Semantic.cpp
 * @brief Component-dictionary, value-domain, theme-token, and locale-key semantic checks.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */
module;

module mdux.tools.medui.semantic;

import std;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;

namespace mdux::tools::medui {

namespace {

using enum FieldDomain;

constexpr FieldRule field(std::string_view name, bool required, FieldDomain domain) {
    return FieldRule{.name = name, .required = required, .domain = domain};
}

constexpr std::array rowFields{field("id", true, Identifier),
                               field("height", true, Size),
                               field("spacing", false, Size),
                               field("background", false, ColorToken)};
constexpr std::array criticalButtonFields{field("id", true, Identifier),
                                          field("requirement", true, String),
                                          field("width", true, Size),
                                          field("height", true, Size),
                                          field("label", true, TextKey),
                                          field("color", true, ColorToken),
                                          field("on_press", true, Identifier),
                                          field("position", false, Point)};
constexpr std::array buttonFields{field("id", true, Identifier),
                                  field("width", true, Size),
                                  field("height", true, Size),
                                  field("label", true, TextKey),
                                  field("color", true, ColorToken),
                                  field("source", true, String),
                                  field("position", false, Point),
                                  field("requirement", false, String)};
constexpr std::array viewportFields{field("id", true, Identifier),
                                    field("width", true, Size),
                                    field("height", true, Size),
                                    field("stream_source", true, String),
                                    field("position", false, Point)};
constexpr std::array signalTraceFields{field("id", true, Identifier),
                                       field("width", true, Size),
                                       field("height", true, Size),
                                       field("stream_source", true, String),
                                       field("color", true, ColorToken),
                                       field("position", false, Point)};
constexpr std::array numericDisplayFields{field("id", true, Identifier),
                                          field("width", true, Size),
                                          field("height", true, Size),
                                          field("requirement", true, String),
                                          field("template", true, String),
                                          field("source", true, String),
                                          field("color", true, ColorToken),
                                          field("position", false, Point)};
constexpr std::array statusIndicatorFields{field("id", true, Identifier),
                                           field("width", true, Size),
                                           field("height", true, Size),
                                           field("requirement", true, String),
                                           field("source", true, String),
                                           field("states", true, TextKeyList),
                                           field("position", false, Point),
                                           field("colors", false, ColorTokenList)};
constexpr std::array labelFields{field("id", true, Identifier),
                                 field("width", true, Size),
                                 field("height", true, Size),
                                 field("text", true, TextKey),
                                 field("color", true, ColorToken),
                                 field("position", false, Point)};
constexpr std::array clockFields{field("id", true, Identifier),
                                 field("width", true, Size),
                                 field("height", true, Size),
                                 field("format", true, Identifier),
                                 field("position", false, Point)};
constexpr std::array imageFields{field("id", true, Identifier),
                                 field("width", true, Size),
                                 field("height", true, Size),
                                 field("source", true, ImageRef),
                                 field("position", false, Point)};
constexpr std::array textInputFields{field("id", true, Identifier),
                                     field("width", true, Size),
                                     field("height", true, Size),
                                     field("source", true, String),
                                     field("max_length", true, Number),
                                     field("color", true, ColorToken),
                                     field("position", false, Point),
                                     field("charset", false, Identifier),
                                     field("requirement", false, String)};

constexpr std::array<ComponentRule, 11> componentRules{
    {
     {"Row", rowFields},
     {"CriticalButton", criticalButtonFields},
     {"Button", buttonFields},
     {"VulkanViewport", viewportFields},
     {"SignalTrace", signalTraceFields},
     {"NumericDisplay", numericDisplayFields},
     {"StatusIndicator", statusIndicatorFields},
     {"Label", labelFields},
     {"Clock", clockFields},
     {"Image", imageFields},
     {"TextInput", textInputFields},
     }
};

[[nodiscard]] const ComponentRule* ruleFor(std::string_view component) noexcept {
    const auto found = std::ranges::find(componentRules, component, &ComponentRule::name);
    return found == componentRules.end() ? nullptr : &*found;
}

[[nodiscard]] const FieldRule* ruleFor(const ComponentRule& component, std::string_view name) noexcept {
    const auto found = std::ranges::find(component.fields, name, &FieldRule::name);
    return found == component.fields.end() ? nullptr : &*found;
}

[[nodiscard]] const ast::Field* fieldFor(const ast::Node& node, std::string_view name) noexcept {
    const auto found = std::ranges::find(node.fields, name, &ast::Field::name);
    return found == node.fields.end() ? nullptr : &*found;
}

[[nodiscard]] bool listContains(const ast::Value& value, ast::ValueKind kind) noexcept {
    return value.kind == ast::ValueKind::List && !value.list.empty() && std::ranges::all_of(value.list, [kind](const std::shared_ptr<ast::Value>& element) {
               return element != nullptr && element->kind == kind;
           });
}

[[nodiscard]] bool matches(const ast::Value& value, FieldDomain domain) noexcept {
    switch (domain) {
        case Identifier:
            return value.kind == ast::ValueKind::Identifier;
        case Size:
            return value.kind == ast::ValueKind::Size;
        case Point:
            return value.kind == ast::ValueKind::Point;
        case String:
            return value.kind == ast::ValueKind::String;
        case TextKey:
            return value.kind == ast::ValueKind::TextKey;
        case TextKeyList:
            return listContains(value, ast::ValueKind::TextKey);
        case ColorToken:
            return value.kind == ast::ValueKind::ColorToken;
        case ColorTokenList:
            return listContains(value, ast::ValueKind::ColorToken);
        case ImageRef:
            return value.kind == ast::ValueKind::ImageRef;
        case Number:
            return value.kind == ast::ValueKind::Number && value.number > 0;
    }
    return false;
}

[[nodiscard]] std::string_view describe(FieldDomain domain) noexcept {
    switch (domain) {
        case Identifier:
            return "a named value";
        case Size:
            return "a pixel size or Fill";
        case Point:
            return "a pixel coordinate pair";
        case String:
            return "a quoted string";
        case TextKey:
            return "t(\"KEY\")";
        case TextKeyList:
            return "a non-empty list of t(\"KEY\") values";
        case ColorToken:
            return "Theme.Colors.<Token>";
        case ColorTokenList:
            return "a non-empty list of Theme.Colors.<Token> values";
        case ImageRef:
            return "img(\"ID\")";
        case Number:
            return "a positive integer";
    }
    return "the field's declared value domain";
}

class Analyzer {
public:
    Analyzer(std::string file, SemanticInputs inputs) : file_{std::move(file)}, inputs_{inputs} {}

    [[nodiscard]] SemanticResult run(const ast::Screen& screen) {
        for (const ast::Node& node : screen.nodes) {
            analyzeNode(node);
        }
        return SemanticResult{.diagnostics = std::move(diagnostics_)};
    }

private:
    void report(Code code, ast::Position position, std::string message) {
        diagnostics_.push_back(diagnose(code, file_, position.line, position.column, std::move(message)));
    }

    void analyzeTextKey(const ast::Value& value) {
        // No approved locale to check against means there is nothing to check, and saying "absent
        // from every approved locale" when there are none is a vacuous truth dressed as a finding.
        // The caller that must not tolerate the gap is the compiler driver, which refuses a recipe
        // whose screen draws text and declares no locales (#198); `mdux-medui-check` runs without a
        // recipe on purpose and reports the gap as a note (#200).
        if (inputs_.textPackages.empty()) {
            return;
        }

        std::vector<std::string_view> missingLocales;
        bool                          foundAnywhere = false;
        for (const mdux::text::TextPackage& package : inputs_.textPackages) {
            if (package.find(value.text) != nullptr) {
                foundAnywhere = true;
            } else {
                missingLocales.push_back(package.locale);
            }
        }
        if (!foundAnywhere) {
            report(Code::UnknownTextKey, value.position, std::format("text key '{}' is absent from every approved locale", value.text));
            return;
        }
        for (std::string_view locale : missingLocales) {
            report(Code::TextKeyMissingForLocale, value.position, std::format("text key '{}' is missing for approved locale '{}'", value.text, locale));
        }
    }

    void analyzeText(const ast::Value& value, FieldDomain domain) {
        if (domain == TextKeyList && value.kind == ast::ValueKind::List) {
            if (value.list.empty()) {
                report(Code::FieldValueKind, value.position, std::format("text field requires {}", describe(domain)));
                return;
            }
            for (const std::shared_ptr<ast::Value>& element : value.list) {
                if (element == nullptr) {
                    report(Code::FieldValueKind, value.position, std::format("text field requires {}", describe(domain)));
                } else if (element->kind == ast::ValueKind::String) {
                    report(Code::HardcodedString, element->position, "literal text cannot be checked against every approved locale");
                } else if (element->kind != ast::ValueKind::TextKey) {
                    report(Code::FieldValueKind, element->position, std::format("text field requires {}", describe(domain)));
                } else {
                    analyzeTextKey(*element);
                }
            }
            return;
        }
        if (value.kind == ast::ValueKind::String) {
            report(Code::HardcodedString, value.position, "literal text cannot be checked against every approved locale");
            return;
        }
        if (!matches(value, domain)) {
            report(Code::FieldValueKind, value.position, std::format("text field requires {}", describe(domain)));
            return;
        }
        analyzeTextKey(value);
    }

    void analyzeColor(const ast::Value& value, FieldDomain domain) {
        if (!matches(value, domain)) {
            report(Code::FieldValueKind, value.position, std::format("colour field requires {}", describe(domain)));
            return;
        }
        const auto check = [&](const ast::Value& token) {
            if (std::ranges::find(inputs_.themeTokens, token.text) == inputs_.themeTokens.end()) {
                report(Code::UnknownColorToken, token.position, std::format("theme token '{}' is not in the governed table", token.text));
            }
        };
        if (domain == ColorToken) {
            check(value);
        } else {
            for (const std::shared_ptr<ast::Value>& element : value.list) {
                check(*element);
            }
        }
    }

    void analyzeNode(const ast::Node& node) {
        const ComponentRule* component = ruleFor(node.component);
        if (component == nullptr) {
            report(Code::UnknownComponent, node.position, std::format("component '{}' is not in the component dictionary", node.component));
        } else {
            for (const FieldRule& fieldRule : component->fields) {
                if (fieldRule.required && fieldFor(node, fieldRule.name) == nullptr) {
                    report(Code::MissingRequiredField, node.position, std::format("component '{}' requires field '{}'", node.component, fieldRule.name));
                }
            }

            for (const ast::Field& fieldValue : node.fields) {
                const FieldRule* fieldRule = ruleFor(*component, fieldValue.name);
                if (fieldRule == nullptr) {
                    report(Code::UnknownField,
                           fieldValue.namePosition,
                           std::format("field '{}' is not defined for component '{}'", fieldValue.name, node.component));
                    continue;
                }
                if (fieldValue.value == nullptr) {
                    continue;
                }
                if (fieldRule->domain == TextKey || fieldRule->domain == TextKeyList) {
                    analyzeText(*fieldValue.value, fieldRule->domain);
                } else if (fieldRule->domain == ColorToken || fieldRule->domain == ColorTokenList) {
                    analyzeColor(*fieldValue.value, fieldRule->domain);
                } else if (!matches(*fieldValue.value, fieldRule->domain)) {
                    report(Code::FieldValueKind,
                           fieldValue.value->position,
                           std::format("field '{}' requires {}", fieldValue.name, describe(fieldRule->domain)));
                }
            }
        }

        for (const ast::Node& child : node.children) {
            analyzeNode(child);
        }
    }

    std::string                  file_;
    SemanticInputs               inputs_;
    std::vector<cli::Diagnostic> diagnostics_;
};

}  // namespace

std::span<const ComponentRule> componentDictionary() noexcept {
    return componentRules;
}

SemanticResult analyze(const ast::Screen& screen, std::string file, SemanticInputs inputs) {
    return Analyzer{std::move(file), inputs}.run(screen);
}

}  // namespace mdux::tools::medui
