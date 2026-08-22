/**
 * @file EmitTests.cpp
 * @brief BDD scenarios for the screen emitter (issue #197).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * The emitted C++ is compiled and compared in `GeneratedScreenTests.cpp`, which is where #197's
 * acceptance criterion lives. What these scenarios cover is everything that has to hold before a
 * compiler ever sees the output: that the fixture the emission reads is still what the compiler
 * would produce, that the CMake and C++ halves of the filename rule agree, and that a malformed
 * package is refused rather than rendered.
 */

import std;
import speclab;
import mdux.draw;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.emit;
import mdux.tools.medui.layout;
import mdux.tools.medui.package;
import mdux.tools.medui.parser;

#include "../framework/SpecLabBridge.hpp"
#include "../framework/TemporaryDirectory.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace cli = mdux::tools::cli;

/// The budget the fixture package declares, and therefore the one a rebuild must reproduce.
constexpr mdux::draw::DrawBudget fixtureBudget{.maxVertices = 4096, .maxIndices = 6144, .maxCommands = 256};

[[nodiscard]] std::filesystem::path fixturePath(std::string_view name) {
    return std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
}

[[nodiscard]] std::string fixture(std::string_view name) {
    std::ifstream in{fixturePath(name), std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("fixture {} could not be opened", name), std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}


[[nodiscard]] std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

const mdux::spec::Register theEmittedFixtureIsWhatTheCompilerProduces{
    "The committed fixture package is what compiling its source produces",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-fixture-is-current")
            .Given("the screen source the fixture package was compiled from", [] {})
            .When("it is compiled again", [] {})
            .Then("the bytes match the committed package exactly",
                  [] {
                      mdux::spec::Checks checks;

                      // The emission reads a committed package, so that package has to stay the
                      // compiler's own answer. Without this scenario it would be a hand-maintained
                      // file that drifts the first time the layout solver changes a rectangle - and
                      // the drift would show up as generated C++ describing a screen nobody wrote.
                      md::ParseResult parsed = md::parse(fixture("accepted-every-component.medui"), "every-component.medui");
                      if (!parsed.screen || !parsed.diagnostics.empty()) {
                          checks.expect(false, "the fixture source parses");
                          checks.raise();
                          return;
                      }
                      const md::LayoutResult layout = md::resolveLayout(*parsed.screen, "every-component.medui", {.surfaceWidth = 800, .surfaceHeight = 700});
                      if (!layout.ok()) {
                          checks.expect(false, std::format("the fixture source resolves: {}", layout.diagnostics.front().message));
                          checks.raise();
                          return;
                      }

                      const std::string produced = md::writePackage(md::buildPackage(layout, {.id = "every-component", .budget = fixtureBudget}).package());
                      checks.expect(produced == fixture("every-component-package.json"), std::format("the committed package is current, got:\n{}", produced));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bothHalvesOfTheFilenameRuleAgree{
    "The CMake and C++ halves of the identifier rule agree",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-identifier-parity")
            .Given("the identifiers mdux_screen_identifier() produced at configure time", [] {})
            .When("identifierForScreen() is run over the same ids", [] {})
            .Then("both halves answer the same for every id",
                  [] {
                      mdux::spec::Checks checks;

                      // Both halves are executed and compared, rather than one being asserted and
                      // called consistent with the other: a C++ assertion cannot observe a CMake
                      // regex, which is how the shader pair drifted on the leading-digit rule.
                      std::ifstream parity{MDUX_SCREEN_IDENTIFIER_PARITY_FILE, std::ios::binary};
                      checks.expect(parity.is_open(), "the build wrote its identifier answers");

                      std::size_t compared = 0;
                      std::string line;
                      while (std::getline(parity, line)) {
                          if (line.empty()) {
                              continue;
                          }
                          const std::size_t tab = line.find('\t');
                          if (tab == std::string::npos) {
                              checks.expect(false, std::format("parity line '{}' has two fields", line));
                              continue;
                          }
                          const std::string id       = line.substr(0, tab);
                          std::string       fromMake = line.substr(tab + 1);
                          // CMake's file(WRITE) writes CRLF on Windows, and getline strips only the
                          // newline. Without this the parity check fails on the MSVC leg alone, on a
                          // carriage return rather than on a disagreement about the rule.
                          if (!fromMake.empty() && fromMake.back() == '\r') {
                              fromMake.pop_back();
                          }
                          const std::string fromCpp = md::identifierForScreen(id);
                          checks.expect(fromCpp == fromMake, std::format("id '{}': CMake says '{}', C++ says '{}'", id, fromMake, fromCpp));
                          ++compared;
                      }
                      checks.expect(compared > 0, "the parity file listed at least one id");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bothFormsRenderFromOneScreen{
    "The module and header forms render the same screen",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-two-forms")
            .Given("a committed screen package", [] {})
            .When("it is rendered", [] {})
            .Then("both outputs carry one body, one static_assert, and every payload the screen holds",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      const auto outputs = md::renderScreen(fixturePath("every-component-package.json"), diagnostics);
                      checks.expect(outputs.has_value(), "a valid package renders");
                      if (!outputs.has_value()) {
                          checks.raise();
                          return;
                      }

                      checks.expect(outputs->stem == "screen_every_component", std::format("the stem, got '{}'", outputs->stem));
                      checks.expect(outputs->moduleName == "mdux.medui.generated.screen_every_component",
                                    std::format("the module name, got '{}'", outputs->moduleName));
                      checks.expect(outputs->moduleSource.contains("export module mdux.medui.generated.screen_every_component;"),
                                    "the module form declares its module");
                      checks.expect(outputs->headerSource.contains("#pragma once"), "the header form is include-guarded");
                      checks.expect(!outputs->headerSource.contains("export module"), "the header form declares no module");

                      // The two outputs share one rendering of the screen, so the body has to appear
                      // in both. Compared as a whole rather than by sampling members: a shared body
                      // is the property that makes them impossible to disagree.
                      const std::size_t bodyStart = outputs->moduleSource.find("namespace mdux::medui::generated::");
                      checks.expect(bodyStart != std::string::npos, "the module form carries the generated namespace");
                      if (bodyStart != std::string::npos) {
                          const std::string body = outputs->moduleSource.substr(bodyStart);
                          checks.expect(outputs->headerSource.contains(body), "both forms carry the identical body");
                      }

                      checks.expect(outputs->moduleSource.contains("static_assert(screen.validate().has_value()"),
                                    "the generated screen checks itself where it is defined");

                      // All eleven payloads: what makes this an emitter rather than a renderer of
                      // the components somebody happened to test with.
                      for (const std::string_view spec : {"PanelSpec",
                                                          "LabelSpec",
                                                          "ClockSpec",
                                                          "ImageSpec",
                                                          "VulkanViewportSpec",
                                                          "SignalTraceSpec",
                                                          "ButtonSpec",
                                                          "CriticalButtonSpec",
                                                          "NumericDisplaySpec",
                                                          "StatusIndicatorSpec",
                                                          "TextInputSpec"}) {
                          checks.expect(outputs->moduleSource.contains(spec), std::format("the rendering carries a {}", spec));
                      }
                      checks.expect(diagnostics.empty(), "a valid package renders without diagnostics");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aMalformedPackageIsNotRendered{
    "A package the schema refuses is not rendered",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-refuses-malformed")
            .Given("a package whose surface no longer contains its nodes", [] {})
            .When("the emitter is pointed at it", [] {})
            .Then("nothing is rendered and the reader's own diagnostic is reported",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-screenemit-malformed"};

                      std::string       edited = fixture("every-component-package.json");
                      const std::size_t at     = edited.find("\"surfaceWidth\": 800");
                      checks.expect(at != std::string::npos, "the fixture declares its surface width");
                      if (at != std::string::npos) {
                          edited.replace(at, std::string_view{"\"surfaceWidth\": 800"}.size(), "\"surfaceWidth\": 100");
                      }
                      const std::filesystem::path path = scratch.path() / "package.json";
                      std::ofstream               out{path, std::ios::binary | std::ios::trunc};
                      out.write(edited.data(), static_cast<std::streamsize>(edited.size()));
                      out.close();

                      std::vector<cli::Diagnostic> diagnostics;
                      const auto                   outputs = md::renderScreen(path, diagnostics);
                      checks.expect(!outputs.has_value(), "a screen the schema refuses is not rendered");
                      checks.expect(
                          !diagnostics.empty() && diagnostics.front().code == "SCP005",
                          std::format("the reader's verdict is reported, got '{}'", diagnostics.empty() ? std::string{"<none>"} : diagnostics.front().code));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnreadablePackageIsReported{
    "A package that cannot be read is reported, not assumed empty",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-unreadable")
            .Given("a path where no package exists", [] {})
            .When("the emitter is pointed at it", [] {})
            .Then("it reports the file it could not read",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;
                      const auto                   outputs = md::renderScreen(fixturePath("no-such-screen-package.json"), diagnostics);
                      checks.expect(!outputs.has_value(), "a missing package renders nothing");
                      checks.expect(!diagnostics.empty() && diagnostics.front().code == "SCE001",
                                    std::format("reported as SCE001, got '{}'", diagnostics.empty() ? std::string{"<none>"} : diagnostics.front().code));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writingIsIdempotent{
    "Writing the same screen twice leaves the same two files",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-write")
            .Given("a rendered screen", [] {})
            .When("it is written into an empty directory, then written again", [] {})
            .Then("both files hold the rendering, unchanged by the second write",
                  [] {
                      mdux::spec::Checks             checks;
                      std::vector<cli::Diagnostic>   diagnostics;
                      mdux::test::TemporaryDirectory scratch{"mdux-screenemit-write"};

                      const auto outputs = md::renderScreen(fixturePath("every-component-package.json"), diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, "the fixture package renders");
                          checks.raise();
                          return;
                      }

                      checks.expect(md::writeScreen(*outputs, scratch.path(), diagnostics), "the first write succeeds");
                      const std::filesystem::path module = scratch.path() / "screen_every_component.cppm";
                      const std::filesystem::path header = scratch.path() / "screen_every_component.hpp";
                      checks.expect(contentsOf(module) == outputs->moduleSource, "the module file holds the rendering");
                      checks.expect(contentsOf(header) == outputs->headerSource, "the header file holds the rendering");

                      // A rebuild that changes nothing must not restamp these: every consumer of a
                      // module interface recompiles when it changes.
                      checks.expect(md::writeScreen(*outputs, scratch.path(), diagnostics), "the second write succeeds");
                      checks.expect(contentsOf(module) == outputs->moduleSource, "the second write left the module file alone");
                      checks.expect(diagnostics.empty(), "writing twice reports nothing");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aDecodedControlCharacterCannotEndTheLiteral{
    "A name carrying a decoded control character is escaped, not embedded",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-escapes")
            .Given("a screen whose requirement contains a source-legal escaped newline", [] {})
            .When("it is compiled and rendered", [] {})
            .Then("the generated literal carries an escape rather than a line break",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-screenemit-escapes"};

                      // The lexer decodes `\n` inside a source string and the dictionary accepts an
                      // unrestricted String for `requirement`, so this screen is legal, its package
                      // is valid, and the decoded newline reaches the emitter. Rendered raw it would
                      // end the C++ literal and let what follows be read as tokens.
                      const std::string source = "Screen Escapes {\n"
                                                 "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                                 "    surface: 200px, 100px;\n"
                                                 "\n"
                                                 "    NumericDisplay {\n"
                                                 "        id: score;\n"
                                                 "        width: 200px;\n"
                                                 "        height: 40px;\n"
                                                 "        requirement: \"REQ\\nHALT\";\n"
                                                 "        template: \"TPL\\tSCORE\";\n"
                                                 "        source: \"SCORE\";\n"
                                                 "        color: Theme.Colors.ScoreDigits;\n"
                                                 "    }\n"
                                                 "}\n";

                      md::ParseResult parsed = md::parse(source, "escapes.medui");
                      if (!parsed.screen || !parsed.diagnostics.empty()) {
                          checks.expect(false, "the escaped source parses");
                          checks.raise();
                          return;
                      }
                      const md::LayoutResult layout = md::resolveLayout(*parsed.screen, "escapes.medui", {.surfaceWidth = 200, .surfaceHeight = 100});
                      if (!layout.ok()) {
                          checks.expect(false, "the escaped source resolves");
                          checks.raise();
                          return;
                      }

                      const std::string           json = md::writePackage(md::buildPackage(layout, {.id = "escapes", .budget = fixtureBudget}).package());
                      const std::filesystem::path path = scratch.path() / "package.json";
                      std::ofstream               out{path, std::ios::binary | std::ios::trunc};
                      out.write(json.data(), static_cast<std::streamsize>(json.size()));
                      out.close();

                      std::vector<cli::Diagnostic> diagnostics;
                      const auto                   outputs = md::renderScreen(path, diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, "a screen with an escaped name renders");
                          checks.raise();
                          return;
                      }

                      checks.expect(outputs->moduleSource.contains("REQ\\nHALT"), "the newline is emitted as an escape");
                      checks.expect(outputs->moduleSource.contains("TPL\\tSCORE"), "and so is the tab");
                      checks.expect(!outputs->moduleSource.contains("REQ\nHALT"), "no decoded newline reaches the generated source");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aScreenWithNoNodesRenders{
    "A screen that resolves to no nodes renders as an empty span",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-empty")
            .Given("the empty screen the schema permits", [] {})
            .When("it is rendered", [] {})
            .Then("its node table is an empty span rather than an empty array",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diagnostics;

                      const auto outputs = md::renderScreen(fixturePath("empty-screen-package.json"), diagnostics);
                      checks.expect(outputs.has_value(), "the empty screen renders");
                      if (!outputs.has_value()) {
                          checks.raise();
                          return;
                      }
                      // `CompiledNode nodes[] = {}` cannot deduce a zero bound and is not valid C++,
                      // so a screen validate() accepts could not be consumed in either form.
                      // GeneratedEmptyScreenConsumer.cpp is where the compiler says so.
                      checks.expect(outputs->moduleSource.contains("std::span<const mdux::medui::CompiledNode> nodes{}"), "the empty node table is a span");
                      checks.expect(outputs->moduleSource.contains("static_assert(screen.validate().has_value()"), "the empty screen still checks itself");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aReservedIdentifierIsRefused{
    "An id that maps to a reserved identifier is refused",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-reserved-identifier")
            .Given("a package whose id carries two adjacent separators", [] {})
            .When("it is rendered", [] {})
            .Then("the emitter refuses rather than generating a reserved name",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-screenemit-reserved"};

                      // `a--b` maps to `screen_a__b`, and `__` is reserved to the implementation
                      // everywhere. Collapsing the run would fix the name and break injectivity, so
                      // two screens could claim one filename; refusing keeps both properties.
                      checks.expect(md::identifierForScreen("a--b") == "screen_a__b", "the mapping is one underscore per separator");
                      checks.expect(md::identifierForScreen("class") == "screen_class", "a keyword id maps to a usable identifier");

                      std::string       edited = fixture("empty-screen-package.json");
                      const std::size_t at     = edited.find("\"empty-screen\"");
                      checks.expect(at != std::string::npos, "the fixture carries its id");
                      if (at != std::string::npos) {
                          edited.replace(at, std::string_view{"\"empty-screen\""}.size(), "\"a--b\"");
                      }
                      const std::filesystem::path path = scratch.path() / "package.json";
                      std::ofstream               out{path, std::ios::binary | std::ios::trunc};
                      out.write(edited.data(), static_cast<std::streamsize>(edited.size()));
                      out.close();

                      std::vector<cli::Diagnostic> diagnostics;
                      const auto                   outputs = md::renderScreen(path, diagnostics);
                      checks.expect(!outputs.has_value(), "a reserved identifier is refused");
                      checks.expect(!diagnostics.empty() && diagnostics.front().code == "SCE003",
                                    std::format("reported as SCE003, got '{}'", diagnostics.empty() ? std::string{"<none>"} : diagnostics.front().code));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aProvenanceCommentCannotBeEnded{
    "An id carrying a newline cannot end the provenance comment",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-emit-comment-escapes")
            .Given("a package whose id contains a control character", [] {})
            .When("it is rendered", [] {})
            .Then("the id appears escaped inside the comment, on one line",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-screenemit-comment"};

                      // The schema asks only that an id be non-empty, and canonical JSON carries a
                      // control character as an escape, so this package is valid on both counts. A
                      // `//` comment ends at a newline, so an unescaped id would have closed the
                      // provenance comment and left `namespace` as generated source.
                      std::string       edited = fixture("empty-screen-package.json");
                      const std::size_t at     = edited.find("\"empty-screen\"");
                      checks.expect(at != std::string::npos, "the fixture carries its id");
                      if (at != std::string::npos) {
                          edited.replace(at, std::string_view{"\"empty-screen\""}.size(), "\"a\\nnamespace\"");
                      }
                      const std::filesystem::path path = scratch.path() / "package.json";
                      std::ofstream               out{path, std::ios::binary | std::ios::trunc};
                      out.write(edited.data(), static_cast<std::streamsize>(edited.size()));
                      out.close();

                      std::vector<cli::Diagnostic> diagnostics;
                      const auto                   outputs = md::renderScreen(path, diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, "a package with a control character in its id renders");
                          checks.raise();
                          return;
                      }

                      const std::size_t marker = outputs->moduleSource.find("// Screen: ");
                      checks.expect(marker != std::string::npos, "the provenance comment names the screen");
                      if (marker != std::string::npos) {
                          const std::size_t lineEnd = outputs->moduleSource.find('\n', marker);
                          const std::string line    = outputs->moduleSource.substr(marker, lineEnd - marker);
                          checks.expect(line == "// Screen: a\\nnamespace", std::format("the whole id stays on the comment line, got '{}'", line));
                      }
                      checks.raise();
                  })
            .Execute();
    }};
