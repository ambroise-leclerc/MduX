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
    "A Row inside a Row is MEDUI-E015, at the inner Row",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-nested-row")
            .Given("a screen with a nested Row", [] {})
            .When("it is parsed", [] {})
            .Then("MEDUI-E015 names the inner Row's position", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-nested-row.medui"),
                                                    "rejected-nested-row.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::NestedRow);
                checks.expect(d != nullptr,
                              std::format("MEDUI-E015 reported, got {}", codesOf(r.diagnostics)));
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
    "A control-flow keyword is MEDUI-E016",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-forbidden")
            .Given("a screen containing 'if'", [] {})
            .When("it is parsed", [] {})
            .Then("MEDUI-E016 names it at its position", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-forbidden-construct.medui"),
                                                    "rejected-forbidden-construct.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::ForbiddenConstruct);
                checks.expect(d != nullptr,
                              std::format("MEDUI-E016 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 3, std::format("at line 3, got {}", d->line));
                    checks.expect(d->column == 5, std::format("at column 5, got {}", d->column));
                    checks.expect(d->message.find("if") != std::string::npos,
                                  "the message names the offending word");
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register duplicateIdRejected{
    "A repeated node id is MEDUI-E014, pointing at the second one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-duplicate-id")
            .Given("two nodes sharing an id", [] {})
            .When("the screen is parsed", [] {})
            .Then("MEDUI-E014 names the second and cites the first", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-duplicate-id.medui"),
                                                    "rejected-duplicate-id.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::DuplicateNodeId);
                checks.expect(d != nullptr,
                              std::format("MEDUI-E014 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 8, std::format("at the second id, line 8, got {}",
                                                            d->line));
                    checks.expect(d->column == 9, std::format("at column 9, got {}", d->column));
                    // Naming only the duplicate would leave an author hunting for the original.
                    checks.expect(d->message.find("line 4") != std::string::npos,
                                  std::format("the message cites the first, got '{}'", d->message));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register unknownUnitRejected{
    "A length in a unit other than px is MEDUI-E010",
    "evidence-unit",
    [] {
        return speclab::Test("medui-reject-bad-unit")
            .Given("a width written as 10rem", [] {})
            .When("the screen is parsed", [] {})
            .Then("MEDUI-E010 says what units exist", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse(fixture("rejected-bad-unit.medui"),
                                                    "rejected-bad-unit.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::UnexpectedToken);
                checks.expect(d != nullptr,
                              std::format("MEDUI-E010 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 5, std::format("at line 5, got {}", d->line));
                    checks.expect(d->column == 18, std::format("at column 18, got {}", d->column));
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
    "A source that is not valid UTF-8 is rejected whole, with MEDUI-E004",
    "evidence-unit",
    [] {
        // Whole rather than at the byte: past an invalid sequence there are no defined character
        // boundaries, so every column after it would be invented.
        return speclab::Test("medui-lex-bad-utf8")
            .Given("a source containing a lone continuation byte", [] {})
            .When("it is lexed", [] {})
            .Then("MEDUI-E004 is reported and no tokens are produced", [] {
                mdux::spec::Checks checks;
                std::string source = "Screen A { }\n";
                source += '\x80';
                const md::LexResult r = md::lex(source, "bad.medui");
                checks.expect(has(r.diagnostics, md::Code::SourceNotUtf8),
                              std::format("MEDUI-E004 reported, got {}", codesOf(r.diagnostics)));
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
                    r.diagnostics, [](const cli::Diagnostic& d) { return d.code == "MEDUI-E010"; });
                checks.expect(count >= 2,
                              std::format("at least two findings, got {}",
                                          codesOf(r.diagnostics)));
                checks.raise();
            })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Regressions. Each of these parsed clean before review on #209.
// ---------------------------------------------------------------------------

const mdux::spec::Register columnsAreUtf8Bytes{
    "A column counts UTF-8 bytes, not code points",
    "evidence-unit",
    [] {
        // Raised by @coderabbitai: every other position scenario uses ASCII, so a lexer counting
        // code points would pass all of them. The comment before "e" is 3 ASCII + one 2-byte
        // character (U+00E9) + 1 ASCII = 6 bytes, so `Screen` starts at byte column 7 and at code
        // point column 6. Asserting 7 is what pins the convention shared with mdux-docs-lint.
        return speclab::Test("medui-lex-utf8-columns")
            .Given("a line with a multi-byte character before a token", [] {})
            .When("it is lexed", [] {})
            .Then("the token's column is its byte offset", [] {
                mdux::spec::Checks checks;
                const md::LexResult r = md::lex("/*\u00e9*/Screen A { }\n", "u.medui");
                // `/*` is not a comment in this grammar, so those are unexpected-character
                // diagnostics; the token positions are what this scenario is about.
                const auto screen = std::ranges::find_if(
                    r.tokens, [](const md::Token& t) { return t.text == "Screen"; });
                checks.expect(screen != r.tokens.end(), "the Screen token was produced");
                if (screen != r.tokens.end()) {
                    checks.expect(screen->column == 7,
                                  std::format("byte column 7 (code-point column would be 6), got {}",
                                              screen->column));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register trailingContentRejected{
    "Content after the screen's closing brace is rejected",
    "evidence-unit",
    [] {
        // Before review this parsed clean: the closing brace was consumed and nothing looked at
        // what followed, so a second Screen or plain garbage produced ok() == true.
        return speclab::Test("medui-reject-trailing")
            .Given("two screens in one source, and a source with trailing junk", [] {})
            .When("each is parsed", [] {})
            .Then("both are rejected", [] {
                mdux::spec::Checks checks;
                const md::ParseResult two = md::parse("Screen A { }\nScreen B { }\n", "t.medui");
                const cli::Diagnostic* d2 = find(two.diagnostics, md::Code::UnexpectedToken);
                checks.expect(d2 != nullptr,
                              std::format("MEDUI-E010 for a second Screen, got {}",
                                          codesOf(two.diagnostics)));
                if (d2 != nullptr) {
                    checks.expect(d2->line == 2 && d2->column == 1,
                                  std::format("at 2:1, the second Screen keyword, got {}:{}",
                                              d2->line, d2->column));
                }

                const md::ParseResult junk = md::parse("Screen A { } garbage\n", "t.medui");
                const cli::Diagnostic* dj = find(junk.diagnostics, md::Code::UnexpectedToken);
                checks.expect(dj != nullptr,
                              std::format("MEDUI-E010 for trailing junk, got {}",
                                          codesOf(junk.diagnostics)));
                if (dj != nullptr) {
                    checks.expect(dj->line == 1 && dj->column == 14,
                                  std::format("at 1:14, the junk token, got {}:{}",
                                              dj->line, dj->column));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register annotatedChildOutsideRowRejected{
    "An annotated child is rejected outside a Row, and a Row inside one is still MEDUI-E015",
    "evidence-unit",
    [] {
        // Reported independently by three reviewers on #209. The unannotated path enforced
        // "only a Row has children"; the annotated path did not, so `Card { @x Label { } }`
        // was accepted - and `Card { @x Row { } }` put a Row at depth two with no MEDUI-E015,
        // because the annotated path also passed insideRow = false.
        return speclab::Test("medui-reject-annotated-child")
            .Given("an annotated child under a non-Row parent", [] {})
            .When("it is parsed", [] {})
            .Then("it is rejected, as the unannotated form already was", [] {
                mdux::spec::Checks checks;
                const md::ParseResult child =
                    md::parse("Screen A {\n Card {\n  @x Label { }\n }\n}\n", "a.medui");
                const cli::Diagnostic* dc = find(child.diagnostics, md::Code::UnexpectedToken);
                checks.expect(dc != nullptr,
                              std::format("MEDUI-E010 for an annotated child, got {}",
                                          codesOf(child.diagnostics)));
                if (dc != nullptr) {
                    checks.expect(dc->line == 3 && dc->column == 3,
                                  std::format("at 3:3, the '@', got {}:{}", dc->line, dc->column));
                    checks.expect(dc->message.find("Card") != std::string::npos,
                                  "the message names the offending parent");
                }

                // A Row under a non-Row is rejected as a child, *not* as a nested Row: the parent
                // is a Card, so MEDUI-E015 would be the wrong code. The nested-Row rule is asserted
                // separately below, where the parent really is a Row.
                const md::ParseResult row =
                    md::parse("Screen A {\n Card {\n  @x Row { }\n }\n}\n", "a.medui");
                checks.expect(find(row.diagnostics, md::Code::UnexpectedToken) != nullptr,
                              std::format("MEDUI-E010 for an annotated Row under a Card, got {}",
                                          codesOf(row.diagnostics)));

                // An *annotated* Row inside a Row is the case the gate previously let through
                // with no diagnostic at all. It must be MEDUI-E015, exactly as the unannotated form.
                const md::ParseResult nested =
                    md::parse("Screen A {\n Row {\n  @x Row { }\n }\n}\n", "a.medui");
                const cli::Diagnostic* dn = find(nested.diagnostics, md::Code::NestedRow);
                checks.expect(dn != nullptr,
                              std::format("MEDUI-E015 for an annotated nested Row, got {}",
                                          codesOf(nested.diagnostics)));
                if (dn != nullptr) {
                    checks.expect(dn->line == 3 && dn->column == 6,
                                  std::format("at 3:6, the inner Row, got {}:{}",
                                              dn->line, dn->column));
                }

                // The unannotated form was always rejected; asserted so the two paths cannot
                // drift apart again without a test noticing.
                const md::ParseResult plain =
                    md::parse("Screen A {\n Card {\n  Label { }\n }\n}\n", "a.medui");
                checks.expect(find(plain.diagnostics, md::Code::UnexpectedToken) != nullptr,
                              "the unannotated form stays rejected with the same code");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register forbiddenWordInLayoutRejected{
    "A control-flow keyword as a layout kind is MEDUI-E016",
    "evidence-unit",
    [] {
        // "Wherever an identifier may appear" did not include the layout kind, so `layout: if { }`
        // parsed clean.
        return speclab::Test("medui-reject-forbidden-layout")
            .Given("layout: if { }", [] {})
            .When("it is parsed", [] {})
            .Then("MEDUI-E016 is reported", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse("Screen A {\n layout: if { }\n}\n", "l.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::ForbiddenConstruct);
                checks.expect(d != nullptr,
                              std::format("MEDUI-E016 reported, got {}", codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 2 && d->column == 10,
                                  std::format("at 2:10, the 'if', got {}:{}", d->line, d->column));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register onlyThemeColorsIsAColourToken{
    "A dotted path is a colour token only when it is Theme.Colors.<Token>",
    "evidence-unit",
    [] {
        // Every dotted path was classified ColorToken, so `Foo.Bar` would have reached #193's
        // theme lookup and produced MEDUI-E030 "not in the governed table" for a value that never
        // claimed to name a colour.
        return speclab::Test("medui-value-colour-classification")
            .Given("three dotted paths", [] {})
            .When("they are parsed as values", [] {})
            .Then("only the qualified one is a ColorToken", [] {
                mdux::spec::Checks checks;
                const auto kindOf = [&](std::string_view value) {
                    const md::ParseResult r = md::parse(
                        std::format("Screen A {{\n L {{ c: {}; }}\n}}\n", value), "v.medui");
                    if (!r.screen || r.screen->nodes.empty() ||
                        r.screen->nodes[0].fields.empty() ||
                        r.screen->nodes[0].fields[0].value == nullptr) {
                        return md::ast::ValueKind::Number;  // a sentinel none of the cases expect
                    }
                    return r.screen->nodes[0].fields[0].value->kind;
                };
                checks.expect(kindOf("Theme.Colors.ScoreDigits") == md::ast::ValueKind::ColorToken,
                              "Theme.Colors.ScoreDigits is a ColorToken");
                checks.expect(kindOf("Foo.Bar") == md::ast::ValueKind::Identifier,
                              "Foo.Bar is not a ColorToken");
                checks.expect(kindOf("Theme.Colors") == md::ast::ValueKind::Identifier,
                              "a truncated Theme.Colors is not a ColorToken");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register backslashAtLineEndDoesNotEatTheNewline{
    "A backslash at end of line does not let a string span lines",
    "evidence-unit",
    [] {
        // The escape branch consumed the next byte unconditionally, so a trailing backslash ate
        // the newline and the string ran on - skewing every position after it, and reporting the
        // unterminated string somewhere the author never wrote one.
        return speclab::Test("medui-lex-backslash-eol")
            .Given("a string with a trailing backslash", [] {})
            .When("it is lexed", [] {})
            .Then("it is unterminated, reported at its opening quote, and lines still line up", [] {
                mdux::spec::Checks checks;
                const md::LexResult r = md::lex("Screen A {\n r: \"ab\\\n cd\";\n}\n", "b.medui");
                const cli::Diagnostic* d = find(r.diagnostics, md::Code::UnexpectedToken);
                checks.expect(d != nullptr,
                              std::format("a diagnostic was reported, got {}",
                                          codesOf(r.diagnostics)));
                if (d != nullptr) {
                    checks.expect(d->line == 2 && d->column == 5,
                                  std::format("at the opening quote, 2:5, got {}:{}",
                                              d->line, d->column));
                }
                // The token after the broken string must still be on line 3, which it cannot be if
                // the newline was swallowed.
                const auto brace = std::ranges::find_if(
                    r.tokens, [](const md::Token& t) { return t.kind == md::TokenKind::RBrace; });
                checks.expect(brace != r.tokens.end() && brace->line >= 3,
                              "positions after the broken string are not skewed");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register screenPositionIsTheKeyword{
    "A screen's position is the Screen keyword, not its name",
    "evidence-unit",
    [] {
        // Ast.cppm says a node carries the position of the token that introduced it, and
        // parseLayout/parseSurface both capture before advancing. This one captured after.
        return speclab::Test("medui-screen-position")
            .Given("a screen declared at a known column", [] {})
            .When("it is parsed", [] {})
            .Then("the position is the keyword's", [] {
                mdux::spec::Checks checks;
                const md::ParseResult r = md::parse("Screen A { }\n", "s.medui");
                checks.expect(r.screen.has_value(), "a screen was produced");
                if (r.screen) {
                    checks.expect(r.screen->position.column == 1,
                                  std::format("column 1, the 'Screen' keyword, got {}",
                                              r.screen->position.column));
                }
                checks.raise();
            })
            .Execute();
    }};
