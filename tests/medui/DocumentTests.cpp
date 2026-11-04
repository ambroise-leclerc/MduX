/**
 * @file DocumentTests.cpp
 * @brief BDD scenarios for the `.medui` AST JSON document view (issue #327, ADR-025).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-023 MedUI host editing API and round-trip source contract
 * @compliance ADR-025 MedUI Studio editing and reviewable change proposals
 *
 * The Studio edits a JSON document and the service compiles the source that document serializes to.
 * The claims worth checking are therefore the ones an author relies on without seeing them: the
 * document describes the same screen the source does, a document cannot smuggle a second field in
 * through a name, the comment detector agrees with the lexer about what a comment is, and a change to
 * safety metadata is always reported.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.tools.medui.ast;
import mdux.tools.medui.document;
import mdux.tools.medui.parser;
import mdux.tools.medui.serialize;

#include "../framework/SpecLabBridge.hpp"

namespace {

using speclab::core::Assertions;

namespace md   = mdux::tools::medui;
namespace json = mdux::evidence::json;

[[nodiscard]] std::string fixture(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
    std::ifstream               in{path, std::ios::binary};
    Assertions::require(static_cast<bool>(in), "fixture {} could not be opened at {}", name, path.generic_string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] md::ast::Screen parseOrFail(std::string_view source, std::string_view label) {
    md::ParseResult parsed = md::parse(source, std::string{label});
    Assertions::require(parsed.ok(), "{} did not parse cleanly", label);
    return std::move(*parsed.screen);
}

[[nodiscard]] json::Value reparse(std::string_view text) {
    auto value = json::parse(text);
    Assertions::require(static_cast<bool>(value), "document JSON did not parse");
    return std::move(*value);
}

[[nodiscard]] std::string written(const json::Value& value) {
    auto text = json::write(value);
    Assertions::require(static_cast<bool>(text), "document JSON could not be written");
    return *text;
}

constexpr std::array<std::string_view, 6> acceptedFixtures{
    "accepted-every-component.medui",
    "accepted-goldens.medui",
    "accepted-layout.medui",
    "accepted-neurosense.medui",
    "accepted-numeric-only.medui",
    "accepted-textless.medui",
};

[[nodiscard]] md::ast::Node* nodeById(md::ast::Screen& screen, std::string_view wanted) {
    md::ast::Node* found = nullptr;
    const auto     visit = [&](auto&& self, md::ast::Node& node) -> void {
        for (const md::ast::Field& f : node.fields) {
            if (f.name == "id" && f.value != nullptr && f.value->text == wanted) {
                found = &node;
            }
        }
        for (md::ast::Node& child : node.children) {
            self(self, child);
        }
    };
    for (md::ast::Node& node : screen.nodes) {
        visit(visit, node);
    }
    return found;
}

}  // namespace

const mdux::spec::Register documentsDescribeTheirSource{
    "Every accepted fixture survives source -> document -> JSON text -> document -> source unchanged",
    "evidence-unit",
    [] {
        return speclab::Test("medui-document-round-trip")
            .Given("every fixture the parser accepts with no diagnostics", [] {})
            .When("each is turned into a document, written as JSON text, read back and serialized", [] {})
            .Then("the source equals the canonical serialization of the original, and the screens are structurally equal",
                  [] {
                      mdux::spec::Checks checks;
                      for (std::string_view name : acceptedFixtures) {
                          const md::ast::Screen original = parseOrFail(fixture(name), name);
                          const json::Value     document = reparse(written(md::screenDocument(original)));
                          const auto            screen   = md::readScreenDocument(document);
                          checks.expect(screen.has_value(), std::format("{}: the written document reads back", name));
                          if (!screen) {
                              continue;
                          }
                          checks.expect(md::sameScreen(original, *screen), std::format("{}: the document describes the same screen", name));
                          const auto source = md::sourceFromDocument(document);
                          checks.expect(source.has_value() && *source == md::serializeScreen(original),
                                        std::format("{}: the document serializes to the original's canonical text", name));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register documentsRefuseMalformedShapes{
    "A malformed document is refused with the path of the offending member",
    "evidence-unit",
    [] {
        return speclab::Test("medui-document-refusals")
            .Given("the document of accepted-goldens.medui, corrupted one member at a time", [] {})
            .When("each corruption is read", [] {})
            .Then("every one is refused, and none reaches the serializer",
                  [] {
                      mdux::spec::Checks checks;
                      const std::string  base = written(md::screenDocument(parseOrFail(fixture("accepted-goldens.medui"), "goldens")));
                      const auto         find = [&](std::string_view needle) {
                          const auto at = base.find(needle);
                          Assertions::require(at != std::string::npos, "the goldens document has no {}", needle);
                          return at;
                      };
                      const auto replaced = [&](std::string_view needle, std::string_view with) {
                          auto copy = base;
                          copy.replace(find(needle), needle.size(), with);
                          return copy;
                      };
                      // The first pixel count may be 0, and "-0" is not canonical JSON: replace the number.
                      const auto negated = [&] {
                          auto             copy  = base;
                          std::string_view key   = "\"pixels\": ";
                          const auto       start = find(key) + key.size();
                          copy.replace(start, copy.find_first_not_of("0123456789", start) - start, "-5");
                          return copy;
                      };
                      const std::vector<std::pair<std::string, std::string>> corruptions{
                          {"an unsupported schema version", replaced("\"schemaVersion\": 1", "\"schemaVersion\": 2")},
                          {"an unknown member", replaced("\"schemaVersion\": 1", "\"schemaVersion\": 1, \"extra\": true")},
                          {"a field name that would inject a second field", replaced("\"name\": \"id\"", "\"name\": \"id: x; requirement\"")},
                          {"a component name that is not an identifier", replaced("\"component\": \"", "\"component\": \"1")},
                          {"an unknown value kind", replaced("\"kind\": \"Identifier\"", "\"kind\": \"Expression\"")},
                          {"a negative pixel count", negated()},
                      };
                      for (const auto& [what, text] : corruptions) {
                          auto document = json::parse(text);
                          checks.expect(document.has_value(), std::format("{}: the corrupted text is still JSON", what));
                          if (!document) {
                              continue;
                          }
                          const auto read = md::sourceFromDocument(*document);
                          checks.expect(!read.has_value(), std::format("{}: refused", what));
                          if (!read) {
                              checks.expect(read.error().starts_with("document"), std::format("{}: the refusal names a member path ({})", what, read.error()));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register invalidEditsReachTheCompiler{
    "A well-shaped document the grammar rejects is serialized rather than refused, so its diagnostics reach the author",
    "evidence-unit",
    [] {
        return speclab::Test("medui-document-invalid-edit")
            .Given("accepted-layout.medui with a Row nested inside its first node", [] {})
            .When("the document is serialized", [] {})
            .Then("source is returned and the parser reports the grammar violation against it",
                  [] {
                      mdux::spec::Checks checks;
                      md::ast::Screen    screen = parseOrFail(fixture("accepted-layout.medui"), "layout");
                      md::ast::Node      row;
                      row.component = "Row";
                      md::ast::Node outer;
                      outer.component = "Row";
                      outer.children.push_back(row);
                      screen.nodes.push_back(outer);
                      const auto source = md::sourceFromDocument(md::screenDocument(screen));
                      checks.expect(source.has_value(), "the document is not refused for a grammar violation");
                      if (source) {
                          checks.expect(!md::parse(*source, "nested").diagnostics.empty(), "the serialized text carries the violation to the parser");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register commentDetectionMatchesTheLexer{
    "Comment detection treats // inside a string literal as text, as the lexer does",
    "evidence-unit",
    [] {
        return speclab::Test("medui-document-comment-detection")
            .Given("sources with and without comments, including // inside strings", [] {})
            .When("each is checked", [] {})
            .Then("only a // outside a string counts, and every commented fixture is detected",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(md::containsComments("Screen A {\n    // note\n}\n"), "a full-line comment");
                      checks.expect(md::containsComments("Screen A { } // trailing\n"), "a trailing comment");
                      checks.expect(!md::containsComments("Screen A {\n    X { source: \"http://x\"; }\n}\n"), "// inside a string");
                      checks.expect(!md::containsComments("Screen A {\n    X { source: \"a\\\"//b\"; }\n}\n"), "// after an escaped quote inside a string");
                      checks.expect(md::containsComments("Screen A {\n    X { source: \"a\"; } // c\n}\n"), "a comment after a closed string");
                      checks.expect(md::containsComments(fixture("accepted-every-component.medui")), "the commented fixture");
                      for (std::string_view name : acceptedFixtures) {
                          const auto canonical = md::serializeScreen(parseOrFail(fixture(name), name));
                          checks.expect(!md::containsComments(canonical), std::format("{}: canonical serialization carries no comment", name));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register safetyChangesAreReported{
    "Adding, removing or changing a safety annotation or requirement is reported per node",
    "evidence-unit",
    [] {
        return speclab::Test("medui-document-safety-changes")
            .Given("accepted-goldens.medui and edited copies of it", [] {})
            .When("the edited copies are compared with the original", [] {})
            .Then("an unchanged screen reports nothing, and each kind of edit is reported against its node",
                  [] {
                      mdux::spec::Checks    checks;
                      const md::ast::Screen original = parseOrFail(fixture("accepted-goldens.medui"), "goldens");
                      checks.expect(md::safetyChanges(original, original).empty(), "an unchanged screen reports nothing");

                      md::ast::Screen edited = original;
                      md::ast::Node*  action = nodeById(edited, "action");
                      md::ast::Node*  score  = nodeById(edited, "score");
                      checks.expect(action != nullptr && score != nullptr, "the fixture carries both traced nodes");
                      if (action == nullptr || score == nullptr) {
                          checks.raise();
                          return;
                      }
                      action->annotations.clear();
                      for (md::ast::Field& field : score->fields) {
                          if (field.name == "requirement") {
                              auto value  = std::make_shared<md::ast::Value>(*field.value);
                              value->text = "REQ-EDITED";
                              field.value = value;
                          }
                      }
                      const auto changes = md::safetyChanges(original, edited);
                      const auto has     = [&](std::string_view id, std::string_view change) {
                          return std::ranges::any_of(changes, [&](const md::SafetyChange& c) {
                              return c.nodeId == id && c.change == change;
                          });
                      };
                      checks.expect(has("action", "changed"), "removing an annotation from a traced node is reported");
                      checks.expect(has("score", "changed"), "changing a requirement string is reported");

                      md::ast::Screen removed = original;
                      std::erase_if(removed.nodes, [](const md::ast::Node& n) {
                          return std::ranges::any_of(n.fields, [](const md::ast::Field& f) {
                              return f.name == "id" && f.value != nullptr && f.value->text == "action";
                          });
                      });
                      checks.expect(removed.nodes.size() + 1 == original.nodes.size(), "the traced node 'action' is a top-level node and was deleted");
                      const auto reported = [](const std::vector<md::SafetyChange>& list, std::string_view change) {
                          return list.size() == 1 && list.front().nodeId == "action" && list.front().change == change
                                 && !list.front().before.empty() != (change == "added");
                      };
                      checks.expect(reported(md::safetyChanges(original, removed), "removed"), "deleting a traced node is reported as exactly one removal");
                      checks.expect(reported(md::safetyChanges(removed, original), "added"), "adding a traced node is reported as exactly one addition");
                      checks.raise();
                  })
            .Execute();
    }};
