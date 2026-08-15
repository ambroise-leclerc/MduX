/**
 * @file Semantic.cpp
 * @brief Component-dictionary, theme-token, and locale-key semantic checks.
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

struct ComponentRule {
    std::string_view name;
    std::string_view required;
    std::string_view allowed;
};

constexpr std::array<ComponentRule, 11> componentRules{
    {
     {"Row", "id height", "id height spacing background"},
     {"CriticalButton", "id requirement width height label color on_press", "id requirement width height label color on_press position"},
     {"Button", "id width height label color source", "id width height label color source position requirement"},
     {"VulkanViewport", "id width height stream_source", "id width height stream_source position"},
     {"SignalTrace", "id width height stream_source color", "id width height stream_source color position"},
     {"NumericDisplay", "id width height requirement template source color", "id width height requirement template source color position"},
     {"StatusIndicator", "id width height requirement source states", "id width height requirement source states position colors"},
     {"Label", "id width height text color", "id width height text color position"},
     {"Clock", "id width height format", "id width height format position"},
     {"Image", "id width height source", "id width height source position"},
     {"TextInput", "id width height source max_length color", "id width height source max_length color position charset requirement"},
     }
};

[[nodiscard]] bool containsWord(std::string_view words, std::string_view wanted) noexcept {
    while (!words.empty()) {
        const std::size_t      separator = words.find(' ');
        const std::string_view word      = words.substr(0, separator);
        if (word == wanted) {
            return true;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        words.remove_prefix(separator + 1);
    }
    return false;
}

[[nodiscard]] const ComponentRule* ruleFor(std::string_view component) noexcept {
    const auto found = std::ranges::find(componentRules, component, &ComponentRule::name);
    return found == componentRules.end() ? nullptr : &*found;
}

[[nodiscard]] const ast::Field* fieldFor(const ast::Node& node, std::string_view name) noexcept {
    const auto found = std::ranges::find(node.fields, name, &ast::Field::name);
    return found == node.fields.end() ? nullptr : &*found;
}

[[nodiscard]] bool isTextField(std::string_view component, std::string_view field) noexcept {
    return (component == "Label" && field == "text") || ((component == "CriticalButton" || component == "Button") && field == "label")
           || (component == "StatusIndicator" && field == "states");
}

[[nodiscard]] bool isColorField(std::string_view field) noexcept {
    return field == "color" || field == "background" || field == "colors";
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

    void analyzeTextValue(const ast::Value& value) {
        if (value.kind == ast::ValueKind::List) {
            for (const std::shared_ptr<ast::Value>& element : value.list) {
                if (element != nullptr) {
                    analyzeTextValue(*element);
                }
            }
            return;
        }
        if (value.kind == ast::ValueKind::String) {
            report(Code::HardcodedString, value.position, "literal text cannot be checked against every approved locale");
            return;
        }
        if (value.kind != ast::ValueKind::TextKey) {
            report(Code::UnexpectedToken, value.position, "this text field requires t(\"KEY\") or a list of text keys");
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

    void analyzeColorValue(const ast::Value& value) {
        if (value.kind == ast::ValueKind::List) {
            for (const std::shared_ptr<ast::Value>& element : value.list) {
                if (element != nullptr) {
                    analyzeColorValue(*element);
                }
            }
            return;
        }
        if (value.kind != ast::ValueKind::ColorToken) {
            report(Code::UnexpectedToken, value.position, "this colour field requires Theme.Colors.<Token>");
            return;
        }
        if (std::ranges::find(inputs_.themeTokens, value.text) == inputs_.themeTokens.end()) {
            report(Code::UnknownColorToken, value.position, std::format("theme token '{}' is not in the governed table", value.text));
        }
    }

    void analyzeNode(const ast::Node& node) {
        const ComponentRule* rule = ruleFor(node.component);
        if (rule == nullptr) {
            report(Code::UnknownComponent, node.position, std::format("component '{}' is not in the component dictionary", node.component));
        } else {
            std::string_view required = rule->required;
            while (!required.empty()) {
                const std::size_t      separator = required.find(' ');
                const std::string_view name      = required.substr(0, separator);
                if (fieldFor(node, name) == nullptr) {
                    report(Code::MissingRequiredField, node.position, std::format("component '{}' requires field '{}'", node.component, name));
                }
                if (separator == std::string_view::npos) {
                    break;
                }
                required.remove_prefix(separator + 1);
            }

            for (const ast::Field& field : node.fields) {
                if (!containsWord(rule->allowed, field.name)) {
                    report(Code::UnknownField, field.namePosition, std::format("field '{}' is not defined for component '{}'", field.name, node.component));
                    continue;
                }
                if (field.value == nullptr) {
                    continue;
                }
                if (isTextField(node.component, field.name)) {
                    analyzeTextValue(*field.value);
                } else if (isColorField(field.name)) {
                    analyzeColorValue(*field.value);
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

SemanticResult analyze(const ast::Screen& screen, std::string file, SemanticInputs inputs) {
    return Analyzer{std::move(file), inputs}.run(screen);
}

}  // namespace mdux::tools::medui
