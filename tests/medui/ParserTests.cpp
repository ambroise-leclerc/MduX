/**
 * @file ParserTests.cpp
 * @brief BDD scenarios for the `.medui` lexer and parser (issue #192).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * The rejection scenarios assert on the **code**, not on the message, because #191 published the
 * codes as the compiler's contract and messages are explicitly rewordable. They also assert the
 * **position**, which #192 requires: a diagnostic that names the right problem at the wrong place
 * costs an author as much time as one that names the wrong problem, and a lexer that dropped
 * columns would make that impossible to notice from the code alone.
 *
 * Fixtures live in `fixtures/` as real `.medui` files rather than as string literals in this file.
 * They are what an author would write, they can be opened in an editor, and #200's
 * `mdux-medui-check` will be pointed at the same corpus - a fixture that only exists inside a test
 * cannot be reused by the tool that exists to check files.
 */

import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.lexer;
import mdux.tools.medui.parser;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md = mdux::tools::medui;
namespace cli = mdux::tools::cli;

[[nodiscard]] std::string fixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(
            std::format("fixture {} could not be opened at {}", name, path.generic_string()),
            std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// True when `code` appears among `diagnostics`.
[[nodiscard]] bool has(const std::vector<cli::Diagnostic>& diagnostics, md::Code code) {
    const std::string_view wanted = md::id(code);
    return std::ranges::any_of(diagnostics,
                               [wanted](const cli::Diagnostic& d) { return d.code == wanted; });
}

[[nodiscard]] const cli::Diagnostic* find(const std::vector<cli::Diagnostic>& diagnostics,
                                          md::Code code) {
    const std::string_view wanted = md::id(code);
    const auto it = std::ranges::find_if(
        diagnostics, [wanted](const cli::Diagnostic& d) { return d.code == wanted; });
    return it == diagnostics.end() ? nullptr : &*it;
}

[[nodiscard]] std::string codesOf(const std::vector<cli::Diagnostic>& diagnostics) {
    std::string out;
    for (const cli::Diagnostic& d : diagnostics) {
        if (!out.empty()) { out += ", "; }
        out += std::format("{}@{}:{}", d.code, d.line, d.column);
    }
    return out.empty() ? "(none)" : out;
}

/// A node anywhere in the screen with `id: wanted`.
[[nodiscard]] const md::ast::Node* nodeById(const md::ast::Screen& screen, std::string_view wanted) {
    const md::ast::Node* found = nullptr;
    const auto visit = [&](auto&& self, const md::ast::Node& node) -> void {
        for (const md::ast::Field& f : node.fields) {
            if (f.name == "id" && f.value != nullptr && f.value->text == wanted) {
                found = &node;
            }
        }
        for (const md::ast::Node& child : node.children) { self(self, child); }
    };
    for (const md::ast::Node& node : screen.nodes) { visit(visit, node); }
    return found;
}

}  // namespace

// ---------------------------------------------------------------------------
// The corpus parses.
// ---------------------------------------------------------------------------

const mdux::spec::Register acceptedScreenParses{
    "The worked example from the authoring skill parses with no diagnostics",
    "evidence-unit",
    [] {
        return speclab::Test("medui-parse-accepted")
            .Given("the published grammar example", [] {})
            .When("it is parsed", [] {})
            .Then("it yields a screen and nothing to report", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("accepted-neurosense.medui"),
                                                    "accepted-neurosense.medui");
                checks.expect(r.diagnostics.empty(),
                              std::format("no diagnostics, got {}", codesOf(r.diagnostics)));
                checks.expect(r.screen.has_value(), "a screen was produced");
                if (r.screen) {
                    checks.expect(r.screen->name == "NeuroSense500",
                                  std::format("screen name, got '{}'", r.screen->name));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register acceptedScreenShape{
    "The parsed AST carries layout, surface, annotations and a one-level Row",
    "evidence-unit",
    [] {
        return speclab::Test("medui-parse-shape")
            .Given("the parsed example", [] {})
            .When("its structure is inspected", [] {})
            .Then("every construct survived with its position", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("accepted-neurosense.medui"),
                                                    "accepted-neurosense.medui");
                if (!r.screen) {
                    checks.expect(false, "a screen was produced");
                    checks.raise();
                    return;
                }
                const md::ast::Screen& s = *r.screen;

                checks.expect(s.layoutKind == "Vertical",
                              std::format("layout kind, got '{}'", s.layoutKind));
                checks.expect(s.layout.size() == 2, "layout carries spacing and padding");
                checks.expect(s.surface.has_value() && s.surface->x == 1920 && s.surface->y == 1080,
                              "surface is 1920x1080");
                checks.expect(s.nodes.size() == 2, "two top-level nodes");

                const md::ast::Node* display = nodeById(s, "sedation-index");
                checks.expect(display != nullptr, "the NumericDisplay is present");
                if (display != nullptr) {
                    checks.expect(display->annotations.size() == 1, "it carries one annotation");
                    if (!display->annotations.empty()) {
                        checks.expect(display->annotations[0].name == "safety_critical",
                                      "the annotation is @safety_critical");
                    }
                    // Position is asserted, not just presence: it is what a diagnostic three
                    // stages later will quote back to the author.
                    checks.expect(display->position.line == 11,
                                  std::format("declared on line 11, got {}",
                                              display->position.line));
                }

                const md::ast::Node* title = nodeById(s, "title");
                checks.expect(title != nullptr, "the Row's child survived flattening-free parsing");

                const md::ast::Node* row = nodeById(s, "topbar");
                checks.expect(row != nullptr && row->children.size() == 1,
                              "the Row holds exactly its one child");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register valuesKeepTheirKinds{
    "Sizes, points, text keys and colour tokens parse to distinct value kinds",
    "evidence-unit",
    [] {
        // The kinds are what #193 dispatches on. If `Fill` and `512px` collapsed to one kind, or a
        // colour token arrived as a bare identifier, the next stage would have to re-lex strings.
        return speclab::Test("medui-parse-values")
            .Given("the parsed example", [] {})
            .When("the NumericDisplay's fields are read", [] {})
            .Then("each value kind is preserved and unresolved", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("accepted-neurosense.medui"),
                                                    "accepted-neurosense.medui");
                if (!r.screen) { checks.expect(false, "a screen was produced"); checks.raise(); return; }

                const md::ast::Node* display = nodeById(*r.screen, "sedation-index");
                const md::ast::Node* title = nodeById(*r.screen, "title");
                if (display == nullptr || title == nullptr) {
                    checks.expect(false, "both nodes present"); checks.raise(); return;
                }
                const auto fieldOf = [](const md::ast::Node& n, std::string_view name)
                    -> const md::ast::Value* {
                    for (const md::ast::Field& f : n.fields) {
                        if (f.name == name) { return f.value.get(); }
                    }
                    return nullptr;
                };

                const md::ast::Value* width = fieldOf(*display, "width");
                checks.expect(width != nullptr && width->kind == md::ast::ValueKind::Size &&
                                  !width->size.fill && width->size.pixels == 512,
                              "width is a 512px size");

                const md::ast::Value* fill = fieldOf(*title, "width");
                checks.expect(fill != nullptr && fill->kind == md::ast::ValueKind::Size &&
                                  fill->size.fill,
                              "Fill is a size, flagged fill");

                const md::ast::Value* pos = fieldOf(*display, "position");
                checks.expect(pos != nullptr && pos->kind == md::ast::ValueKind::Point &&
                                  pos->point.x == 1392 && pos->point.y == 80,
                              "position is a point");

                const md::ast::Value* colour = fieldOf(*display, "color");
                checks.expect(colour != nullptr && colour->kind == md::ast::ValueKind::ColorToken &&
                                  colour->text == "Theme.Colors.ScoreDigits",
                              "the colour token is carried whole and unresolved");

                const md::ast::Value* text = fieldOf(*title, "text");
                checks.expect(text != nullptr && text->kind == md::ast::ValueKind::TextKey &&
                                  text->text == "STR-TITLE",
                              "t(\"...\") is a text key, not a string");

                const md::ast::Value* requirement = fieldOf(*display, "requirement");
                checks.expect(requirement != nullptr &&
                                  requirement->kind == md::ast::ValueKind::String &&
                                  requirement->text == "REQ-NS-001",
                              "a quoted literal stays a String, for #193 to accept or reject");
                checks.raise();
            })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Rejections. Each asserts the code and the position.
// ---------------------------------------------------------------------------

const mdux::spec::Register nestedRowRejected{
    "A Row inside a Row is MDX-E015, at the inner Row",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-nested-row")
            .Given("a screen with a nested Row", [] {})
            .When("it is parsed", [] {})
            .Then("MDX-E015 names the inner Row's position", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-nested-row.medui"),
                                                    "rejected-nested-row.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::NestedRow);
                checks.expect(d != nullptr,
                              std::format("MDX-E015 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 6, std::format("at line 6, got {}", d->line));
                    checks.expect(d->column == 9, std::format("at column 9, got {}", d->column));
                    checks.expect(!d->fixHint.empty(), "carries the registry's fix hint");
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register forbiddenConstructRejected{
    "A control-flow keyword is MDX-E016",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-forbidden")
            .Given("a screen containing 'if'", [] {})
            .When("it is parsed", [] {})
            .Then("MDX-E016 names it at its position", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-forbidden-construct.medui"),
                                                    "rejected-forbidden-construct.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::ForbiddenConstruct);
                checks.expect(d != nullptr,
                              std::format("MDX-E016 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 3, std::format("at line 3, got {}", d->line));
                    checks.expect(d->message.find("if") != std::string::npos,
                                  "the message names the offending word");
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register duplicateIdRejected{
    "A repeated node id is MDX-E014, pointing at the second one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-duplicate-id")
            .Given("two nodes sharing an id", [] {})
            .When("the screen is parsed", [] {})
            .Then("MDX-E014 names the second and cites the first", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-duplicate-id.medui"),
                                                    "rejected-duplicate-id.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::DuplicateNodeId);
                checks.expect(d != nullptr,
                              std::format("MDX-E014 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 8, std::format("at the second id, line 8, got {}",
                                                            d->line));
                    // Naming only the duplicate would leave an author hunting for the original.
                    checks.expect(d->message.find("line 4") != std::string::npos,
                                  std::format("the message cites the first, got '{}'", d->message));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register unknownUnitRejected{
    "A length in a unit other than px is MDX-E010",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-bad-unit")
            .Given("a width written as 10rem", [] {})
            .When("the screen is parsed", [] {})
            .Then("MDX-E010 says what units exist", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-bad-unit.medui"),
                                                    "rejected-bad-unit.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::UnexpectedToken);
                checks.expect(d != nullptr,
                              std::format("MDX-E010 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 5, std::format("at line 5, got {}", d->line));
                    checks.expect(d->fixHint.find("Npx") != std::string::npos,
                                  "the hint names the accepted form");
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register unterminatedStringRejected{
    "An unterminated string is reported at its opening quote",
    "evidence-unit",
    [] {
        // Reported where the string *started*, not where the line ended: the opening quote is
        // where the author's mistake is, and a diagnostic at the newline points at the symptom.
        return speclab::Test("medui-reject-unterminated-string")
            .Given("a string with no closing quote", [] {})
            .When("the source is lexed", [] {})
            .Then("the diagnostic points at the opening quote", [] {
                mdux::spec::Checks checks;
                const md::LexResult r = md::lex(fixture("rejected-unterminated-string.medui"),
                                                "rejected-unterminated-string.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::UnexpectedToken);
                checks.expect(d != nullptr,
                              std::format("a diagnostic was reported, got {}",
                                          codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 5, std::format("at line 5, got {}", d->line));
                    checks.expect(d->column == 22,
                                  std::format("at the opening quote, column 22, got {}",
                                              d->column));
                }
                checks.raise();
            })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Lexer properties the parser depends on.
// ---------------------------------------------------------------------------

const mdux::spec::Register commentsAreSkipped{
    "A // comment does not become a token",
    "evidence-unit",
    [] {
        return speclab::Test("medui-lex-comments")
            .Given("a source with a trailing comment", [] {})
            .When("it is lexed", [] {})
            .Then("only the code tokens survive, with positions after the comment intact", [] {
                mdux::spec::Checks checks;
                const md::LexResult r = md::lex("// leading\nScreen A { } // trailing\n", "c.medui");
                checks.expect(r.ok(), "no diagnostics");
                checks.expect(r.tokens.size() == 5,
                              std::format("Screen, A, {{, }}, EOF - got {}", r.tokens.size()));
                if (r.tokens.size() >= 2) {
                    checks.expect(r.tokens[0].line == 2,
                                  std::format("the comment did not eat the newline, got line {}",
                                              r.tokens[0].line));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register invalidUtf8Rejected{
    "A source that is not valid UTF-8 is rejected whole, with MDX-E004",
    "evidence-unit",
    [] {
        // Whole rather than at the byte: past an invalid sequence there are no defined character
        // boundaries, so every column after it would be invented.
        return speclab::Test("medui-lex-bad-utf8")
            .Given("a source containing a lone continuation byte", [] {})
            .When("it is lexed", [] {})
            .Then("MDX-E004 is reported and no tokens are produced", [] {
                mdux::spec::Checks checks;
                std::string source = "Screen A { }\n";
                source += '\x80';
                const md::LexResult r = md::lex(source, "bad.medui");
                checks.expect(has(r.diagnostics, md::Code::SourceNotUtf8),
                              std::format("MDX-E004 reported, got {}", codesOf(r.diagnostics)));
                checks.expect(r.tokens.empty(), "no tokens, so no invented positions");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register severalProblemsInOneRun{
    "One run reports more than the first problem",
    "evidence-unit",
    [] {
        // A compiler that stops at the first error makes fixing a screen an iterative guessing
        // game, and #200 exists to hand an author everything at once.
        return speclab::Test("medui-parse-recovery")
            .Given("a screen with two independent bad fields", [] {})
            .When("it is parsed", [] {})
            .Then("both are reported", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(
                    "Screen A {\n"
                    "  surface: 800px, 600px;\n"
                    "  Label {\n"
                    "    width: 10rem;\n"
                    "    height: 20rem;\n"
                    "  }\n"
                    "}\n",
                    "two.medui");
                const auto count = std::ranges::count_if(
                    r.diagnostics, [](const cli::Diagnostic& d) { return d.code == "MDX-E010"; });
                checks.expect(count >= 2,
                              std::format("at least two findings, got {}",
                                          codesOf(r.diagnostics)));
                checks.raise();
            })
            .Execute();
    }};
