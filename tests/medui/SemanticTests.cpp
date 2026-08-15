/**
 * @file SemanticTests.cpp
 * @brief BDD scenarios for `.medui` semantic analysis (issue #193).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */

import std;
import speclab;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace cli = mdux::tools::cli;

[[nodiscard]] mdux::text::TextPackage package(std::string locale, std::initializer_list<std::string_view> keys) {
    mdux::text::TextPackage result;
    result.header.id   = std::format("semantic-fixture-{}", locale);
    result.atlasId     = "semantic-fixture-atlas";
    result.locale      = std::move(locale);
    result.sidecarPath = "runs.bin";
    for (std::string_view key : keys) {
        result.runs.push_back(mdux::text::TextRun{.id = std::string{key}, .byteOffset = 0, .byteLength = 0, .sha256 = {}});
    }
    return result;
}

[[nodiscard]] md::SemanticResult analyze(std::string_view source, std::span<const std::string_view> themes, std::span<const mdux::text::TextPackage> packages) {
    md::ParseResult parsed = md::parse(source, "semantic-test.medui");
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure("semantic test source did not parse", std::source_location::current());
    }
    return md::analyze(*parsed.screen, "semantic-test.medui", md::SemanticInputs{.themeTokens = themes, .textPackages = packages});
}

[[nodiscard]] const cli::Diagnostic* find(const md::SemanticResult& result, md::Code code) {
    const std::string_view wanted = md::id(code);
    const auto             found  = std::ranges::find_if(result.diagnostics, [wanted](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == wanted;
    });
    return found == result.diagnostics.end() ? nullptr : &*found;
}

[[nodiscard]] std::size_t count(const md::SemanticResult& result, md::Code code) {
    const std::string_view wanted = md::id(code);
    return static_cast<std::size_t>(std::ranges::count_if(result.diagnostics, [wanted](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == wanted;
    }));
}

constexpr std::string_view validNested = R"(Screen Valid {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Row {
        id: header;
        height: 32px;
        background: Theme.Colors.Header;
        Label {
            id: title;
            width: Fill;
            height: 32px;
            text: t("STR-TITLE");
            color: Theme.Colors.Title;
        }
    }
})";

}  // namespace

const mdux::spec::Register semanticNamesResolve{"Semantic analysis accepts known names recursively without substituting the AST", "evidence-unit", [] {
                                                    return speclab::Test("medui-semantic-known-names")
                                                        .Given("a Row child whose theme tokens and text key exist in two locales", [] {})
                                                        .When("the unresolved parsed screen is analyzed", [] {})
                                                        .Then("the recursive semantic pass accepts it",
                                                              [] {
                                                                  const std::array<std::string_view, 2> themes{"Theme.Colors.Header", "Theme.Colors.Title"};
                                                                  const std::array<mdux::text::TextPackage, 2> packages{package("en-US", {"STR-TITLE"}),
                                                                                                                        package("fr-FR", {"STR-TITLE"})};
                                                                  const md::SemanticResult                     result = analyze(validNested, themes, packages);
                                                                  mdux::spec::Checks                           checks;
                                                                  checks.expect(result.ok(), "known recursive names produce no diagnostics");
                                                                  checks.raise();
                                                              })
                                                        .Execute();
                                                }};

const mdux::spec::Register semanticDictionaryAccumulates{
    "The closed component dictionary reports unknown components, fields, and missing fields",
    "evidence-unit",
    [] {
        return speclab::Test("medui-semantic-component-dictionary")
            .Given("one unknown component and one incomplete Label with a misspelled field", [] {})
            .When("both nodes are analyzed in one pass", [] {})
            .Then("all dictionary findings carry their source positions",
                  [] {
                      constexpr std::string_view                   source = R"(Screen Dictionary {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Mystery {
        id: unknown;
    }
    Label {
        id: title;
        width: 80px;
        height: 20px;
        text: t("STR-TITLE");
        colour: Theme.Colors.Title;
    }
})";
                      const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                      const std::array<mdux::text::TextPackage, 1> packages{package("en-US", {"STR-TITLE"})};
                      const md::SemanticResult                     result           = analyze(source, themes, packages);
                      const cli::Diagnostic*                       unknownComponent = find(result, md::Code::UnknownComponent);
                      const cli::Diagnostic*                       missingField     = find(result, md::Code::MissingRequiredField);
                      const cli::Diagnostic*                       unknownField     = find(result, md::Code::UnknownField);

                      mdux::spec::Checks checks;
                      checks.expect(result.diagnostics.size() == 3, "three independent findings accumulate");
                      checks.expect(unknownComponent != nullptr && unknownComponent->line == 3 && unknownComponent->column == 5,
                                    "the unknown component points at its name");
                      checks.expect(missingField != nullptr && missingField->line == 6 && missingField->column == 5,
                                    "the missing color points at its component");
                      checks.expect(unknownField != nullptr && unknownField->line == 11 && unknownField->column == 9, "the unknown field points at its name");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register semanticResolutionDistinguishesFailures{
    "Theme and text resolution distinguish global absence from a locale-specific gap",
    "evidence-unit",
    [] {
        return speclab::Test("medui-semantic-resolution-diagnostics")
            .Given("unknown theme/text names and a second key missing in two approved locales", [] {})
            .When("the screen is analyzed against three locale packages", [] {})
            .Then("E030, E031, and one E032 per missing locale are reported",
                  [] {
                      constexpr std::string_view                   source = R"(Screen Resolution {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Row {
        id: content;
        height: 40px;
        Label {
            id: unknown;
            width: 100px;
            height: 20px;
            text: t("STR-UNKNOWN");
            color: Theme.Colors.Unknown;
        }
        Label {
            id: partial;
            width: 100px;
            height: 20px;
            text: t("STR-PARTIAL");
            color: Theme.Colors.Title;
        }
    }
})";
                      const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                      const std::array<mdux::text::TextPackage, 3> packages{package("en-US", {"STR-PARTIAL"}), package("fr-FR", {}), package("de-DE", {})};
                      const md::SemanticResult                     result       = analyze(source, themes, packages);
                      const cli::Diagnostic*                       unknownKey   = find(result, md::Code::UnknownTextKey);
                      const cli::Diagnostic*                       unknownColor = find(result, md::Code::UnknownColorToken);

                      mdux::spec::Checks checks;
                      checks.expect(count(result, md::Code::UnknownTextKey) == 1, "a key absent everywhere produces one E031");
                      checks.expect(count(result, md::Code::TextKeyMissingForLocale) == 2, "a partially present key produces one E032 per missing locale");
                      checks.expect(count(result, md::Code::UnknownColorToken) == 1, "an absent theme token produces E030");
                      checks.expect(unknownKey != nullptr && unknownKey->line == 10 && unknownKey->column == 19, "E031 points at t(KEY)");
                      checks.expect(unknownColor != nullptr && unknownColor->line == 11 && unknownColor->column == 20, "E030 points at Theme.Colors.Token");
                      checks.expect(std::ranges::any_of(result.diagnostics,
                                                        [](const cli::Diagnostic& d) {
                                                            return d.code == md::id(md::Code::TextKeyMissingForLocale) && d.message.contains("fr-FR");
                                                        }),
                                    "a missing-locale message names fr-FR");
                      checks.expect(std::ranges::any_of(result.diagnostics,
                                                        [](const cli::Diagnostic& d) {
                                                            return d.code == md::id(md::Code::TextKeyMissingForLocale) && d.message.contains("de-DE");
                                                        }),
                                    "a missing-locale message names de-DE");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register semanticHardcodedTextPrecedence{"A literal in a text-bearing field is rejected as hardcoded text at its value", "evidence-unit", [] {
                                                               return speclab::Test("medui-semantic-hardcoded-text")
                                                                   .Given("a Label whose text is a literal string", [] {})
                                                                   .When("the text field is analyzed", [] {})
                                                                   .Then("E017 takes precedence over a generic value-shape finding",
                                                                         [] {
                                                                             constexpr std::string_view                   source = R"(Screen Literal {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Label {
        id: title;
        width: 100px;
        height: 20px;
        text: "Product Name";
        color: Theme.Colors.Title;
    }
})";
                                                                             const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                                                                             const std::array<mdux::text::TextPackage, 0> packages{};
                                                                             const md::SemanticResult result  = analyze(source, themes, packages);
                                                                             const cli::Diagnostic*   literal = find(result, md::Code::HardcodedString);
                                                                             mdux::spec::Checks       checks;
                                                                             checks.expect(result.diagnostics.size() == 1,
                                                                                           "only the specific hardcoded-text finding is emitted");
                                                                             checks.expect(literal != nullptr && literal->line == 7 && literal->column == 15,
                                                                                           "E017 points at the literal value");
                                                                             checks.raise();
                                                                         })
                                                                   .Execute();
                                                           }};
