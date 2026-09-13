/**
 * @file SerializeTests.cpp
 * @brief BDD scenarios for the canonical `.medui` source serializer (issue #325, ADR-023).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-023 MedUI host editing API and round-trip source contract
 *
 * ADR-023 decision 4's claim is that a round trip through `serializeScreen()` is a fixed point:
 * serializing a second time after reparsing produces the same text as the first serialization, for
 * every construct the grammar admits. That claim is checked here directly, across the whole accepted
 * fixture corpus, rather than asserted for one hand-picked shape.
 *
 * Decision 3's claim is the opposite: the one thing this round trip does *not* preserve is a `//`
 * comment, because the AST never carried one. That is checked too, so the loss stays a documented
 * property of this module rather than something a reader has to take on faith from the ADR text.
 */

import std;
import speclab;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;
import mdux.tools.medui.serialize;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace ms  = mdux::medui;
namespace cli = mdux::tools::cli;

[[nodiscard]] std::string fixture(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
    std::ifstream               in{path, std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("fixture {} could not be opened at {}", name, path.generic_string()),
                                              std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] md::ast::Screen parseOrFail(std::string_view source, std::string_view label) {
    md::ParseResult parsed = md::parse(source, std::string{label});
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure(std::format("{} did not parse cleanly", label), std::source_location::current());
    }
    return std::move(*parsed.screen);
}

/// `mdux-medui-check`'s own partial mode (`Check.cpp`): the governed theme table, no approved
/// locale and no resource lists, requested rather than inferred - the standing precedent for
/// checking a screen's semantic shape with no recipe behind it.
[[nodiscard]] md::SemanticResult analyzeStandalone(const md::ast::Screen& screen, std::string file) {
    std::vector<std::string_view> themeTokens;
    themeTokens.reserve(ms::themeColors.size());
    for (const ms::ThemeColor& colour : ms::themeColors) {
        themeTokens.push_back(colour.token);
    }
    return md::analyze(screen,
                       std::move(file),
                       {.themeTokens          = themeTokens,
                        .textPackages         = {},
                        .numericTemplateNames = {},
                        .imageIds             = {},
                        .locales              = md::LocalePolicy::Skipped,
                        .resources            = md::ResourcePolicy::Skipped});
}

[[nodiscard]] const md::ast::Node* nodeById(const md::ast::Screen& screen, std::string_view wanted) {
    const md::ast::Node* found = nullptr;
    const auto           visit = [&](auto&& self, const md::ast::Node& node) -> void {
        for (const md::ast::Field& f : node.fields) {
            if (f.name == "id" && f.value != nullptr && f.value->text == wanted) {
                found = &node;
            }
        }
        for (const md::ast::Node& child : node.children) {
            self(self, child);
        }
    };
    for (const md::ast::Node& node : screen.nodes) {
        visit(visit, node);
    }
    return found;
}

[[nodiscard]] const md::ast::Field* fieldNamed(const md::ast::Node& node, std::string_view name) {
    for (const md::ast::Field& field : node.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

/// Mutable counterpart, for the tests that corrupt an AST on purpose to prove `serializeScreen()`
/// refuses it rather than dereferencing a null value it never should have received.
[[nodiscard]] md::ast::Field* mutableFieldNamed(md::ast::Node& node, std::string_view name) {
    for (md::ast::Field& field : node.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

constexpr std::array<std::string_view, 6> acceptedFixtures{
    "accepted-every-component.medui",
    "accepted-goldens.medui",
    "accepted-layout.medui",
    "accepted-neurosense.medui",
    "accepted-numeric-only.medui",
    "accepted-textless.medui",
};

}  // namespace

// ---------------------------------------------------------------------------
// The whole accepted corpus reaches a fixed point.
// ---------------------------------------------------------------------------

const mdux::spec::Register wholeCorpusReachesAFixedPoint{
    "Every accepted fixture serializes to text that reparses and re-serializes identically",
    "evidence-unit",
    [] {
        return speclab::Test("medui-serialize-fixed-point")
            .Given("every fixture the parser accepts with no diagnostics", [] {})
            .When("each is parsed, serialized, reparsed and serialized again", [] {})
            .Then("the second serialization matches the first, and both stay semantically clean",
                  [] {
                      mdux::spec::Checks checks;
                      for (std::string_view name : acceptedFixtures) {
                          const md::ast::Screen original = parseOrFail(fixture(name), name);
                          const std::string     first    = md::serializeScreen(original);

                          md::ParseResult reparsed = md::parse(first, std::string{name});
                          checks.expect(reparsed.diagnostics.empty() && reparsed.screen.has_value(),
                                        std::format("{}: the serialized text reparses cleanly", name));
                          if (!reparsed.screen) {
                              continue;
                          }
                          const std::string second = md::serializeScreen(*reparsed.screen);
                          checks.expect(first == second, std::format("{}: serialize(parse(serialize(x))) == serialize(x)", name));

                          const md::SemanticResult before = analyzeStandalone(original, std::string{name});
                          const md::SemanticResult after  = analyzeStandalone(*reparsed.screen, std::string{name});
                          checks.expect(before.ok(), std::format("{}: the original screen is semantically clean", name));
                          checks.expect(before.ok() == after.ok(), std::format("{}: the round trip changes no semantic outcome", name));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Authored safety metadata survives exactly.
// ---------------------------------------------------------------------------

const mdux::spec::Register safetyMetadataSurvives{
    "Safety-critical annotations and requirement fields survive a round trip exactly",
    "evidence-unit",
    [] {
        return speclab::Test("medui-serialize-safety-metadata")
            .Given("accepted-goldens.medui, which carries @safety_critical and requirement fields", [] {})
            .When("it is serialized and reparsed", [] {})
            .Then("every annotation and every requirement string is unchanged",
                  [] {
                      mdux::spec::Checks    checks;
                      const md::ast::Screen original = parseOrFail(fixture("accepted-goldens.medui"), "goldens");
                      md::ParseResult       reparsed = md::parse(md::serializeScreen(original), "goldens-roundtrip");
                      checks.expect(reparsed.screen.has_value(), "the round-tripped screen reparses");
                      if (!reparsed.screen) {
                          checks.raise();
                          return;
                      }
                      const md::ast::Screen& copy = *reparsed.screen;

                      for (std::string_view id : {"action", "score"}) {
                          const md::ast::Node* before = nodeById(original, id);
                          const md::ast::Node* after  = nodeById(copy, id);
                          checks.expect(before != nullptr && after != nullptr, std::format("node '{}' exists on both sides", id));
                          if (before == nullptr || after == nullptr) {
                              continue;
                          }
                          checks.expect(before->annotations.size() == after->annotations.size(), std::format("node '{}' keeps its annotation count", id));
                          for (std::size_t i = 0; i < before->annotations.size() && i < after->annotations.size(); ++i) {
                              checks.expect(before->annotations[i].name == after->annotations[i].name,
                                            std::format("node '{}' annotation {} keeps its name", id, i));
                              checks.expect(before->annotations[i].arguments.size() == after->annotations[i].arguments.size(),
                                            std::format("node '{}' annotation {} keeps its argument count", id, i));
                          }

                          const md::ast::Field* requirementBefore = fieldNamed(*before, "requirement");
                          const md::ast::Field* requirementAfter  = fieldNamed(*after, "requirement");
                          checks.expect(requirementBefore != nullptr && requirementAfter != nullptr, std::format("node '{}' keeps its requirement field", id));
                          if (requirementBefore != nullptr && requirementAfter != nullptr) {
                              checks.expect(requirementBefore->value->text == requirementAfter->value->text,
                                            std::format("node '{}' requirement text is unchanged", id));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// A field this build's dictionary does not recognise still round-trips.
// ---------------------------------------------------------------------------

const mdux::spec::Register unknownFieldSurvives{
    "A field name outside the current component dictionary still round-trips",
    "evidence-unit",
    [] {
        return speclab::Test("medui-serialize-unknown-field")
            .Given("a parsed screen with a field no component rule declares", [] {})
            .When("it is serialized and reparsed", [] {})
            .Then("the unrecognised field survives unchanged, because the AST never consulted the dictionary",
                  [] {
                      mdux::spec::Checks checks;
                      md::ast::Screen    screen = parseOrFail(fixture("accepted-layout.medui"), "layout");
                      checks.expect(!screen.nodes.empty(), "the fixture has at least one node");
                      if (screen.nodes.empty()) {
                          checks.raise();
                          return;
                      }

                      auto value  = std::make_shared<md::ast::Value>();
                      value->kind = md::ast::ValueKind::Identifier;
                      value->text = "vendor-value";
                      screen.nodes.front().fields.push_back(md::ast::Field{.name = "x-vendor-extension", .namePosition = {}, .value = value});

                      md::ParseResult reparsed = md::parse(md::serializeScreen(screen), "layout-with-extension");
                      checks.expect(reparsed.diagnostics.empty() && reparsed.screen.has_value(),
                                    "the serialized text (with the unknown field) still parses cleanly");
                      if (!reparsed.screen || reparsed.screen->nodes.empty()) {
                          checks.raise();
                          return;
                      }
                      const md::ast::Field* survived = fieldNamed(reparsed.screen->nodes.front(), "x-vendor-extension");
                      checks.expect(survived != nullptr, "the unknown field is present after the round trip");
                      if (survived != nullptr) {
                          checks.expect(survived->value != nullptr && survived->value->text == "vendor-value", "the unknown field kept its value");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The one accepted loss: comments.
// ---------------------------------------------------------------------------

const mdux::spec::Register commentsAreTheOneAcceptedLoss{
    "Serializing drops header comments, and drops nothing else",
    "evidence-unit",
    [] {
        return speclab::Test("medui-serialize-comment-loss")
            .Given("accepted-every-component.medui, which opens with // comment lines", [] {})
            .When("it is parsed and serialized", [] {})
            .Then("the comment text is gone but every component and field survived",
                  [] {
                      mdux::spec::Checks checks;
                      const std::string  source = fixture("accepted-every-component.medui");
                      checks.expect(source.find("// One screen carrying") != std::string::npos, "the fixture actually carries a header comment to lose");

                      const md::ast::Screen original   = parseOrFail(source, "every-component");
                      const std::string     serialized = md::serializeScreen(original);
                      checks.expect(serialized.find("One screen carrying") == std::string::npos, "the comment text does not appear in the serialized output");

                      md::ParseResult reparsed = md::parse(serialized, "every-component-roundtrip");
                      checks.expect(reparsed.diagnostics.empty() && reparsed.screen.has_value(), "the comment-free serialization still parses cleanly");
                      if (reparsed.screen) {
                          checks.expect(reparsed.screen->nodes.size() == original.nodes.size(),
                                        "every component survived even though its neighbouring comments did not");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// A caller must gate on diagnostics, not on screen presence, before serializing.
// ---------------------------------------------------------------------------

const mdux::spec::Register malformedInputGatesOnDiagnostics{
    "A source the parser rejects still yields a partial screen, so callers must gate on diagnostics",
    "evidence-unit",
    [] {
        // `Parser.cpp`'s error recovery keeps parsing a screen after a bad field
        // (`recoverAfterMissingFieldTerminator()`/`recover()`), so `rejected-bad-unit.medui` still
        // produces a `screen` - just one missing the field that failed and carrying a diagnostic
        // that says so. This is exactly why `Compile.cppm`'s driver stops a compile on any reported
        // diagnostic rather than on an absent screen (its own "a stage that reports anything stops
        // the compile" rule): `screen.has_value()` alone is not the gate a caller can trust.
        return speclab::Test("medui-serialize-boundary")
            .Given("a fixture the parser rejects with a recoverable field error", [] {})
            .When("it is parsed and (against the documented contract) serialized anyway", [] {})
            .Then("diagnostics were reported, and the malformed field is silently absent rather than flagged by the serializer",
                  [] {
                      mdux::spec::Checks checks;
                      md::ParseResult    rejected = md::parse(fixture("rejected-bad-unit.medui"), "rejected");
                      checks.expect(!rejected.diagnostics.empty(), "the parser reports at least one diagnostic");
                      checks.expect(rejected.screen.has_value(), "error recovery still produces a partial screen - diagnostics are the real gate");
                      if (!rejected.screen) {
                          checks.raise();
                          return;
                      }
                      const md::ast::Node* label = nodeById(*rejected.screen, "label");
                      checks.expect(label != nullptr, "the Label survived despite its bad field");
                      if (label != nullptr) {
                          checks.expect(fieldNamed(*label, "width") == nullptr, "the unparseable width field is simply absent, not an error marker");
                          const std::string serialized = md::serializeScreen(*rejected.screen);
                          checks.expect(serialized.find("width") == std::string::npos,
                                        "serializeScreen() reproduces that absence silently - it never sees the rejection");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// A null value anywhere the parser never leaves one fails loudly, not with UB.
// ---------------------------------------------------------------------------

const mdux::spec::Register nullValueFailsLoudly{
    "A hand-built AST carrying a null field, argument or list-element value is refused, not dereferenced",
    "evidence-unit",
    [] {
        // `Parser.cpp` never leaves `ast::Field::value`, an annotation argument's value, or a list
        // element null - but `ast::Value::list` and every `Field`/`Annotation` member are public, so
        // an editor building or mutating an `ast::Screen` by hand (this record's whole reason to
        // exist) could. There is no `.medui` syntax a null value could stand for, so `require()`
        // throws rather than letting `appendValue()` dereference a null `shared_ptr`.
        return speclab::Test("medui-serialize-null-value")
            .Given("three otherwise-valid screens, each with one value replaced by a null shared_ptr", [] {})
            .When("each is serialized", [] {})
            .Then("serializeScreen() throws std::logic_error instead of dereferencing the null value",
                  [] {
                      mdux::spec::Checks checks;

                      md::ast::Screen nullField = parseOrFail(fixture("accepted-layout.medui"), "layout");
                      checks.expect(!nullField.nodes.empty() && !nullField.nodes.front().fields.empty(), "the fixture has a node with at least one field");
                      if (!nullField.nodes.empty() && !nullField.nodes.front().fields.empty()) {
                          nullField.nodes.front().fields.front().value = nullptr;
                          bool threw                                   = false;
                          try {
                              static_cast<void>(md::serializeScreen(nullField));
                          } catch (const std::logic_error&) {
                              threw = true;
                          }
                          checks.expect(threw, "a null field value throws rather than crashing");
                      }

                      md::ast::Screen nullAnnotationArgument = parseOrFail(fixture("accepted-goldens.medui"), "goldens");
                      md::ast::Node*  action                 = nullptr;
                      for (md::ast::Node& node : nullAnnotationArgument.nodes) {
                          if (fieldNamed(node, "id") != nullptr && fieldNamed(node, "id")->value->text == "action") {
                              action = &node;
                          }
                      }
                      checks.expect(action != nullptr && !action->annotations.empty() && !action->annotations.front().arguments.empty(),
                                    "the fixture has an annotated node with at least one argument");
                      if (action != nullptr && !action->annotations.empty() && !action->annotations.front().arguments.empty()) {
                          action->annotations.front().arguments.front().value = nullptr;
                          bool threw                                          = false;
                          try {
                              static_cast<void>(md::serializeScreen(nullAnnotationArgument));
                          } catch (const std::logic_error&) {
                              threw = true;
                          }
                          checks.expect(threw, "a null annotation-argument value throws rather than crashing");
                      }

                      md::ast::Screen nullListElement = parseOrFail(fixture("accepted-goldens.medui"), "goldens");
                      md::ast::Field* states          = nullptr;
                      for (md::ast::Node& node : nullListElement.nodes) {
                          if (md::ast::Field* candidate = mutableFieldNamed(node, "states")) {
                              states = candidate;
                          }
                      }
                      checks.expect(states != nullptr && states->value != nullptr && !states->value->list.empty(),
                                    "the fixture has a StatusIndicator with a non-empty states list");
                      if (states != nullptr && states->value != nullptr && !states->value->list.empty()) {
                          states->value->list.front() = nullptr;
                          bool threw                  = false;
                          try {
                              static_cast<void>(md::serializeScreen(nullListElement));
                          } catch (const std::logic_error&) {
                              threw = true;
                          }
                          checks.expect(threw, "a null list element throws rather than crashing");
                      }

                      checks.raise();
                  })
            .Execute();
    }};
