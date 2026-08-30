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

const mdux::spec::Register semanticFieldDomains{
    "Known fields reject syntactically valid values from the wrong semantic domain",
    "evidence-unit",
    [] {
        return speclab::Test("medui-semantic-field-domains")
            .Given("a Row and Label with three mismatched field value forms", [] {})
            .When("the field domains are analyzed", [] {})
            .Then("each mismatch is MEDUI-E033 at the value",
                  [] {
                      constexpr std::string_view                   source = R"(Screen Domains {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Row {
        id: content;
        height: 40px;
        spacing: Theme.Colors.Title;
        Label {
            id: t("STR-TITLE");
            width: "120px";
            height: 20px;
            text: t("STR-TITLE");
            color: Theme.Colors.Title;
        }
    }
})";
                      const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                      const std::array<mdux::text::TextPackage, 1> packages{package("en-US", {"STR-TITLE"})};
                      const md::SemanticResult                     result = analyze(source, themes, packages);
                      mdux::spec::Checks                           checks;
                      checks.expect(count(result, md::Code::FieldValueKind) == 3, "all three mismatches report E033");
                      checks.expect(result.diagnostics.size() == 3, "domain mismatches do not cascade into name diagnostics");
                      if (result.diagnostics.size() == 3) {
                          checks.expect(result.diagnostics[0].line == 6 && result.diagnostics[0].column == 18, "Row spacing points at Theme.Colors.Title");
                          checks.expect(result.diagnostics[1].line == 8 && result.diagnostics[1].column == 17, "Label id points at t(\"STR-TITLE\")");
                          checks.expect(result.diagnostics[2].line == 9 && result.diagnostics[2].column == 20, "Label width points at the string literal");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register malformedColorIsSemanticOnly{
    "A malformed colour path has one semantic-domain diagnostic and no syntax duplicate",
    "evidence-unit",
    [] {
        return speclab::Test("medui-semantic-malformed-color")
            .Given("a Label color written as Theme.Color.Title", [] {})
            .When("the source is parsed and analyzed", [] {})
            .Then("only MEDUI-E033 is emitted",
                  [] {
                      constexpr std::string_view                   source = R"(Screen ColorPath {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Label {
        id: title;
        width: 120px;
        height: 20px;
        text: t("STR-TITLE");
        color: Theme.Color.Title;
    }
})";
                      const std::array<std::string_view, 0>        themes{};
                      const std::array<mdux::text::TextPackage, 1> packages{package("en-US", {"STR-TITLE"})};
                      const md::SemanticResult                     result = analyze(source, themes, packages);
                      mdux::spec::Checks                           checks;
                      checks.expect(result.diagnostics.size() == 1, "the syntax and semantic phases do not duplicate the finding");
                      checks.expect(count(result, md::Code::FieldValueKind) == 1, "the malformed path reports E033");
                      if (result.diagnostics.size() == 1) {
                          checks.expect(result.diagnostics[0].line == 8 && result.diagnostics[0].column == 16, "E033 points at Theme.Color.Title");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register semanticSpecialValueForms{"Image references, positive integer limits, and dotted named values retain distinct forms",
                                                     "evidence-unit",
                                                     [] {
                                                         return speclab::Test("medui-semantic-special-value-forms")
                                                             .Given("valid Image, TextInput, Clock, and CriticalButton fields", [] {})
                                                             .When("their component-specific domains are analyzed", [] {})
                                                             .Then("the forms are accepted without weakening another field",
                                                                   [] {
                                                                       constexpr std::string_view                   source = R"(Screen Forms {
    layout: Vertical { spacing: 0px; padding: 0px; }
    Image { id: logo; width: 32px; height: 32px; source: img("LOGO"); }
    TextInput { id: input; width: 120px; height: 20px; source: "NAME"; max_length: 16; color: Theme.Colors.Title; charset: Charset.DigitsToA; }
    Clock { id: clock; width: 80px; height: 20px; format: TimeSeconds; }
    CriticalButton { id: stop; requirement: "REQ-1"; width: 80px; height: 24px; label: t("STR-STOP"); color: Theme.Colors.Title; on_press: TriggerHalt; }
})";
                                                                       const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                                                                       const std::array<mdux::text::TextPackage, 1> packages{package("en-US", {"STR-STOP"})};
                                                                       const md::SemanticResult                     result = analyze(source, themes, packages);
                                                                       mdux::spec::Checks                           checks;
                                                                       checks.expect(result.ok(), "all declared value forms are accepted");
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};

const mdux::spec::Register semanticHardcodedTextList{"A literal inside a text-key list is reported as hardcoded text at that element", "evidence-unit", [] {
                                                         return speclab::Test("medui-semantic-hardcoded-text-list")
                                                             .Given("a StatusIndicator states list mixing a literal and a governed key", [] {})
                                                             .When("the list is analyzed element by element", [] {})
                                                             .Then("the literal reports MEDUI-E017 without hiding valid keys",
                                                                   [] {
                                                                       constexpr std::string_view                   source = R"(Screen States {
    layout: Vertical { spacing: 0px; padding: 0px; }
    StatusIndicator {
        id: state;
        width: 100px;
        height: 20px;
        requirement: "REQ-STATE";
        source: "STATE";
        states: ["Ready", t("STR-STOP")];
    }
})";
                                                                       const std::array<std::string_view, 0>        themes{};
                                                                       const std::array<mdux::text::TextPackage, 1> packages{package("en-US", {"STR-STOP"})};
                                                                       const md::SemanticResult                     result = analyze(source, themes, packages);
                                                                       const cli::Diagnostic* literal = find(result, md::Code::HardcodedString);
                                                                       mdux::spec::Checks     checks;
                                                                       checks.expect(result.diagnostics.size() == 1, "only the literal is rejected");
                                                                       checks.expect(literal != nullptr && literal->line == 9 && literal->column == 18,
                                                                                     "E017 points at the literal list element");
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};

const mdux::spec::Register theLocalePolicyIsAskedForNotInferred{
    "Skipping the locale check is a mode a caller selects, not a meaning of an empty set",
    "evidence-unit",
    [] {
        return speclab::Test("medui-semantic-locale-policy")
            .Given("a screen with a text key and no approved locale supplied", [] {})
            .When("it is analyzed under each policy", [] {})
            .Then("the default reports the key and only the explicit mode skips it",
                  [] {
                      const std::string_view                       source = R"(Screen Policy {
    layout: Vertical { spacing: 0px; padding: 0px; }
    surface: 200px, 100px;

    Label {
        id: title;
        width: 100px;
        height: 20px;
        text: t("STR-TITLE");
        color: Theme.Colors.Title;
    }
})";
                      const std::array<std::string_view, 1>        themes{"Theme.Colors.Title"};
                      const std::array<mdux::text::TextPackage, 0> none{};

                      mdux::spec::Checks checks;

                      // The default has to be the one that fails closed: an empty package span means
                      // an approved set containing no locale, so every key is absent from all of
                      // them. A caller who wanted the check skipped and forgot to say so gets a
                      // finding rather than a clean result.
                      // Parsed once and checked, rather than dereferenced twice on faith: a fixture
                      // that stopped parsing would crash here instead of failing, and the helper at
                      // the top of this file already guards exactly this.
                      const md::ParseResult parsed = md::parse(source, "policy.medui");
                      if (!parsed.screen || !parsed.diagnostics.empty()) {
                          throw speclab::core::AssertionFailure("the locale-policy source did not parse", std::source_location::current());
                      }

                      const md::SemanticResult required = md::analyze(*parsed.screen,
                                                                      "policy.medui",
                                                                      md::SemanticInputs{.themeTokens = themes, .textPackages = none});
                      checks.expect(count(required, md::Code::UnknownTextKey) == 1, "the default policy reports a key with no locale to resolve in");

                      const md::SemanticResult skipped = md::analyze(
                          *parsed.screen,
                          "policy.medui",
                          md::SemanticInputs{.themeTokens = themes, .textPackages = none, .locales = md::LocalePolicy::Skipped});
                      checks.expect(skipped.ok(), "and the explicit mode skips it");
                      checks.expect(count(skipped, md::Code::UnknownTextKey) == 0, "reporting no key findings at all");
                      checks.raise();
                  })
            .Execute();
    }};
