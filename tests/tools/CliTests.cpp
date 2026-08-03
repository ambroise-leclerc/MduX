/**
 * @file CliTests.cpp
 * @brief BDD scenarios for the host-tools mdux.tools.cli argument parser and diagnostic envelope.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The envelope's field names and JSON shape are asserted literally. They are a published
 * contract that agents key off (issue #19, S3), so a reword must break a scenario rather than
 * silently break a consumer.
 *
 * Converted from the Wave 3 MduXTest suite (issue #141). Each scenario is a namespace-scope
 * `mdux::spec::Register` whose Given/When/Then steps share a local `std::shared_ptr` state,
 * created inside the factory and destroyed when `Execute()` returns.
 */

import std;
import speclab;
import mdux.tools.cli;

#include "../framework/SpecLabBridge.hpp"

using namespace mdux::tools::cli;

namespace {

constexpr std::string_view kTool = "mdux-fontbake";

/// Parses `arguments`, failing hard on an unexpected UsageError rather than recording it.
///
/// The unexpected-throw case is a precondition for everything the scenario then dereferences, so
/// it stays a thrown AssertionFailure (REQUIRE-equivalent) rather than a collected expectation.
[[nodiscard]] Invocation parsedOk(std::vector<std::string_view> arguments,
                                  std::source_location where = std::source_location::current()) {
    try {
        return parse(kTool, arguments);
    } catch (const UsageError& error) {
        // `where` defaults at the call site, so the failure names the step that called this.
        throw speclab::core::AssertionFailure(std::string{"unexpected UsageError: "} + error.what(),
                                              where);
    }
}

/// Parses `arguments` and returns the UsageError's message, failing hard if none is raised.
[[nodiscard]] std::string usageErrorOf(
    std::vector<std::string_view> arguments,
    std::source_location where = std::source_location::current()) {
    try {
        (void)parse(kTool, arguments);
    } catch (const UsageError& error) {
        return error.what();
    }
    throw speclab::core::AssertionFailure("expected a UsageError but none was raised", where);
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

const mdux::spec::Register bakeTakesRecipeAndOutputDir{
    "bake takes a recipe and an output directory", "evidence-unit", [] {
        struct State {
            Invocation invocation;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-bake-takes-recipe-and-output-dir")
            .Given("the bake subcommand with a recipe and an output directory",
                   [state] {
                       state->invocation =
                           parsedOk({"bake", "recipes/font/roboto-ui.toml", "build/mdux_bake"});
                   })
            .When("the invocation is inspected", [] {})
            .Then("it names the bake mode, text format, recipe and output directory",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->invocation.mode == Mode::Bake, "mode is Bake");
                      checks.expect(state->invocation.format == Format::Text, "format is Text");
                      checks.expect(state->invocation.bake.recipe == "recipes/font/roboto-ui.toml",
                                    "recipe");
                      checks.expect(state->invocation.bake.outputDir == "build/mdux_bake",
                                    "output directory");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register verifyTakesRecipeAndCommittedFiles{
    "verify takes a recipe and the two committed files", "evidence-unit", [] {
        struct State {
            Invocation invocation;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-verify-takes-recipe-and-committed-files")
            .Given("the verify subcommand with a recipe and both committed files",
                   [state] {
                       state->invocation = parsedOk({"verify", "recipes/font/roboto-ui.toml",
                                                     "generated/font/roboto-ui/package.json",
                                                     "generated/font/roboto-ui/report.json"});
                   })
            .When("the invocation is inspected", [] {})
            .Then("it names the verify mode, the recipe and both committed paths",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->invocation.mode == Mode::Verify, "mode is Verify");
                      checks.expect(state->invocation.verify.recipe ==
                                        "recipes/font/roboto-ui.toml",
                                    "recipe");
                      checks.expect(state->invocation.verify.packagePath ==
                                        "generated/font/roboto-ui/package.json",
                                    "package path");
                      checks.expect(state->invocation.verify.reportPath ==
                                        "generated/font/roboto-ui/report.json",
                                    "report path");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register formatAcceptedEverywhere{
    "--format is accepted before, between and after positionals", "evidence-unit", [] {
        struct State {
            std::array<Invocation, 3> jsonVariants;
            Invocation text;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-format-position")
            .Given("--format=json placed before, between and after the positionals",
                   [state] {
                       state->jsonVariants = {
                           parsedOk({"--format=json", "bake", "r.toml", "out"}),
                           parsedOk({"bake", "--format=json", "r.toml", "out"}),
                           parsedOk({"bake", "r.toml", "out", "--format=json"}),
                       };
                       state->text = parsedOk({"bake", "r.toml", "out", "--format=text"});
                   })
            .When("each spelling is parsed", [] {})
            .Then("all three spellings give a json bake of the same recipe",
                  [state] {
                      mdux::spec::Checks checks;
                      for (const Invocation& invocation : state->jsonVariants) {
                          checks.expect(invocation.format == Format::Json,
                                        "--format=json parses as Json");
                          checks.expect(invocation.bake.recipe == "r.toml", "recipe");
                          checks.expect(invocation.bake.outputDir == "out", "output directory");
                      }
                      checks.expect(state->text.format == Format::Text,
                                    "the --format=text spelling stays Text");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register argcArgvOverloadSkipsArgvZero{
    "The argc/argv overload skips argv[0]", "evidence-unit", [] {
        struct State {
            Invocation invocation;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-argc-argv-skips-argv0")
            .Given("an argv array whose first element is the program name",
                   [state] {
                       const std::array<const char*, 4> argv{"/usr/local/bin/mdux-fontbake",
                                                             "bake", "r.toml", "out"};
                       state->invocation = parse(kTool, static_cast<int>(argv.size()), argv.data());
                   })
            .When("it is decoded into an invocation", [] {})
            .Then("argv[0] is skipped and the trailing arguments are read",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->invocation.mode == Mode::Bake, "mode is Bake");
                      checks.expect(state->invocation.bake.recipe == "r.toml", "recipe");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Usage errors
// ---------------------------------------------------------------------------

const mdux::spec::Register usageErrorsNameWhatWasWrong{
    "Usage errors name what was wrong and print the usage text", "evidence-unit", [] {
        struct State {
            std::vector<std::string> messages;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-usage-errors-name-what-was-wrong")
            .Given("a set of malformed invocations", [state] {
                state->messages = {
                    usageErrorOf({}),
                    usageErrorOf({"build", "r.toml", "out"}),
                    usageErrorOf({"bake"}),
                    usageErrorOf({"bake", "r.toml"}),
                    usageErrorOf({"bake", "r.toml", "out", "extra"}),
                    usageErrorOf({"verify", "r.toml", "p.json"}),
                    usageErrorOf({"--wat", "bake", "r.toml", "out"}),
                    usageErrorOf({"bake", "r.toml", "out", "--format=xml"}),
                    usageErrorOf({"bake", "r.toml", "out", "--format", "json"}),
                };
            })
            .When("each is parsed", [] {})
            .Then("the error names the problem and carries the usage text",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->messages[0].find("expected a subcommand") !=
                                        std::string::npos,
                                    "no arguments");
                      checks.expect(state->messages[1].find("unrecognized subcommand 'build'") !=
                                        std::string::npos,
                                    "unrecognized subcommand");
                      checks.expect(state->messages[2].find("bake takes exactly 2 arguments") !=
                                        std::string::npos,
                                    "missing bake arguments");
                      checks.expect(state->messages[3].find("got 1") != std::string::npos,
                                    "one argument counted");
                      checks.expect(state->messages[4].find("got 3") != std::string::npos,
                                    "three arguments counted");
                      checks.expect(state->messages[5].find("verify takes exactly 3 arguments") !=
                                        std::string::npos,
                                    "verify arity");
                      checks.expect(state->messages[6].find("unrecognized option '--wat'") !=
                                        std::string::npos,
                                    "unrecognized option");
                      checks.expect(state->messages[7].find("unrecognized --format value 'xml'") !=
                                        std::string::npos,
                                    "unrecognized format");
                      // The space-separated spelling gets its own message rather than
                      // "unrecognized option".
                      checks.expect(state->messages[8].find("takes its value with '='") !=
                                        std::string::npos,
                                    "space-separated format spelling");
                      // Every usage error carries the usage text, so a caller has one place to
                      // print it.
                      checks.expect(state->messages[0].find(
                                        "mdux-fontbake bake   <recipe> <output-dir>") !=
                                        std::string::npos,
                                    "bake usage text is carried");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register helpIsReportedAndCarriesUsage{
    "--help is a UsageError carrying the usage text", "evidence-unit", [] {
        struct State {
            std::array<std::string, 2> raised;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-help-is-usage-error")
            .Given("--help and -h", [state] {
                state->raised = {usageErrorOf({"--help"}), usageErrorOf({"-h"})};
            })
            .When("each is parsed", [] {})
            .Then("the error message is exactly the usage text",
                  [state] {
                      mdux::spec::Checks checks;
                      for (const std::string& raised : state->raised) {
                          checks.expect(raised == usage(kTool), "message equals usage()");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register usageDocumentsBothSubcommandsAndOptions{
    "usage() documents both subcommands and both options", "evidence-unit", [] {
        struct State {
            std::string text;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-usage-documents")
            .Given("the usage text", [state] { state->text = usage(kTool); })
            .When("it is read", [] {})
            .Then("it documents both subcommands, both options and the bake/verify rule",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->text.find("mdux-fontbake bake   <recipe> <output-dir>") !=
                                        std::string::npos,
                                    "bake usage line");
                      checks.expect(state->text.find(
                                        "mdux-fontbake verify <recipe> <package.json> <report.json>") !=
                                        std::string::npos,
                                    "verify usage line");
                      checks.expect(state->text.find("--format=json|text") != std::string::npos,
                                    "--format option");
                      checks.expect(state->text.find("--help") != std::string::npos, "--help option");
                      // States the rule a baker author most needs to know.
                      checks.expect(state->text.find("verify") != std::string::npos,
                                    "the verify action");
                      checks.expect(state->text.find("writing nothing") != std::string::npos,
                                    "the writing-nothing rule");
                      checks.expect(state->text.find("ADR-007") != std::string::npos,
                                    "the evidence-pipeline reference");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The diagnostic envelope
// ---------------------------------------------------------------------------

const mdux::spec::Register jsonDiagnosticsEnvelopeShape{
    "JSON diagnostics use the published envelope shape", "evidence-unit", [] {
        struct State {
            std::string json;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-json-diagnostics-envelope-shape")
            .Given("an error finding with a fix hint",
                   [state] {
                       const std::vector<Diagnostic> diagnostics{
                           Diagnostic{.file = "recipes/font/roboto-ui.toml",
                                      .line = 7,
                                      .column = 22,
                                      .code = "FB001",
                                      .severity = Severity::Error,
                                      .message = "glyph budget exceeded",
                                      .fixHint = "raise atlasWidth or reduce the charset"},
                       };
                       state->json = render(diagnostics, Format::Json, kTool);
                   })
            .When("it is rendered as JSON", [] {})
            .Then("the envelope matches the published shape exactly",
                  [state] {
                      // Pinned literally, and deliberately so: this is the published contract of
                      // docs/governance/schemas/diagnostic.schema.json, which every later baker
                      // emits. A field added, renamed or reordered here changes what agents parse
                      // repository-wide, so it should cost a visible test edit rather than passing
                      // unnoticed.
                      mdux::spec::Checks checks;
                      checks.expect(
                          state->json ==
                              "{\n"
                              "  \"tool\": \"mdux-fontbake\",\n"
                              "  \"findings\": [\n"
                              "    {\n"
                              "      \"file\": \"recipes/font/roboto-ui.toml\",\n"
                              "      \"line\": 7,\n"
                              "      \"column\": 22,\n"
                              "      \"code\": \"FB001\",\n"
                              "      \"severity\": \"error\",\n"
                              "      \"message\": \"glyph budget exceeded\",\n"
                              "      \"fixHint\": \"raise atlasWidth or reduce the charset\"\n"
                              "    }\n"
                              "  ]\n"
                              "}\n",
                          "the envelope JSON bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unknownPositionIsZeroOnBothAxes{
    "An unknown position is carried as zero on both axes", "evidence-unit", [] {
        struct State {
            std::string json;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-unknown-position-is-zero-on-both-axes")
            .Given("a finding with no position at all",
                   [state] {
                       // A tool with no position at all must still emit both fields. Omitting them
                       // would make the envelope's shape depend on the finding, which is exactly
                       // what a strict consumer cannot tolerate - `column` is always present, and 0
                       // is how "no column" is spelled.
                        const std::vector<Diagnostic> diagnostics{
                            Diagnostic{.file = "recipes/font/roboto-ui.toml",
                                       .code = "FB003",
                                       .message = "no charset",
                                       .fixHint = ""},
                        };
                       state->json = render(diagnostics, Format::Json, kTool);
                   })
            .When("it is rendered as JSON", [] {})
            .Then("both position fields are present and zero",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->json.find("\"line\": 0,\n") != std::string::npos,
                                    "line is zero");
                      checks.expect(state->json.find("\"column\": 0,\n") != std::string::npos,
                                    "column is zero");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register commaSeparatesMultipleFindings{
    "JSON diagnostics separate multiple findings with a comma", "evidence-unit", [] {
        struct State {
            std::string json;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-json-separate-multiple-findings")
            .Given("two findings",
                   [state] {
                        const std::vector<Diagnostic> diagnostics{
                            Diagnostic{.file = "a.toml",
                                       .line = 1,
                                       .code = "A",
                                       .message = "first",
                                       .fixHint = ""},
                            Diagnostic{.file = "b.toml",
                                       .line = 2,
                                       .code = "B",
                                       .message = "second",
                                       .fixHint = ""},
                        };
                       state->json = render(diagnostics, Format::Json, kTool);
                   })
            .When("it is rendered as JSON", [] {})
            .Then("the two findings are separated with a comma",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->json.find("    },\n    {\n") != std::string::npos,
                                    "a comma separates the findings");
                      checks.expect(state->json.find("\"message\": \"first\"") !=
                                        std::string::npos,
                                    "the first message");
                      checks.expect(state->json.find("\"message\": \"second\"") !=
                                        std::string::npos,
                                    "the second message");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register emptyFindingListIsWellFormed{
    "An empty finding list still produces a well-formed envelope", "evidence-unit", [] {
        struct State {
            std::string json;
            std::string text;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-empty-finding-list-is-well-formed")
            .Given("no findings at all",
                   [state] {
                       state->json = render({}, Format::Json, kTool);
                       state->text = render({}, Format::Text, kTool);
                   })
            .When("both formats are rendered", [] {})
            .Then("the JSON envelope is well formed and the text is empty",
                  [state] {
                      // A consumer must not have to special-case success; it parses the same shape
                      // either way.
                      mdux::spec::Checks checks;
                      checks.expect(state->json ==
                                        "{\n"
                                        "  \"tool\": \"mdux-fontbake\",\n"
                                        "  \"findings\": []\n"
                                        "}\n",
                                    "empty JSON envelope");
                      checks.expect(state->text.empty(), "empty text output");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register jsonDiagnosticsEscape{
    "JSON diagnostics escape what would otherwise break the envelope", "evidence-unit", [] {
        struct State {
            std::string json;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-json-diagnostics-escape")
            .Given("a finding with a quote, a newline, a tab and a backslash in it",
                   [state] {
                       const std::vector<Diagnostic> diagnostics{
                           Diagnostic{.file = "a\"b.toml",
                                      .line = 1,
                                      .code = "X",
                                      .message = "line one\nline two\ttabbed",
                                      .fixHint = "use a backslash: \\"},
                       };
                       state->json = render(diagnostics, Format::Json, kTool);
                   })
            .When("it is rendered as JSON", [] {})
            .Then("every character that would break the envelope is escaped",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->json.find("\"file\": \"a\\\"b.toml\"") !=
                                        std::string::npos,
                                    "the quote in the file name is escaped");
                      checks.expect(state->json.find("line one\\nline two\\ttabbed") !=
                                        std::string::npos,
                                    "the newline and tab are escaped");
                      checks.expect(state->json.find("use a backslash: \\\\") !=
                                        std::string::npos,
                                    "the backslash is escaped");
                      // A raw newline inside a JSON string would make the envelope unparseable.
                      const std::size_t messageStart = state->json.find("\"message\":");
                      if (messageStart == std::string::npos) {
                          throw speclab::core::AssertionFailure(
                              "the diagnostic message was not rendered",
                              std::source_location::current());
                      }
                      checks.expect(state->json.find('\n', messageStart) >
                                        state->json.find("tabbed"),
                                    "no raw newline inside the JSON string");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register textDiagnosticsFollowTheConvention{
    "Text diagnostics follow the file:line:column: severity: [code] message convention",
    "evidence-unit", [] {
        struct State {
            std::string text;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-text-diagnostics-convention")
            .Given("findings with, without and without any position",
                   [state] {
                       const std::vector<Diagnostic> diagnostics{
                           Diagnostic{.file = "recipes/font/roboto-ui.toml",
                                      .line = 7,
                                      .column = 22,
                                      .code = "FB001",
                                      .severity = Severity::Error,
                                      .message = "glyph budget exceeded",
                                      .fixHint = "raise atlasWidth"},
                           // Knows the line but not the column: the position printed stops at the
                           // line rather than gaining a ":0" an editor would jump to.
                           Diagnostic{.file = "recipes/font/roboto-ui.toml",
                                      .line = 3,
                                      .code = "FB004",
                                      .severity = Severity::Note,
                                      .message = "charset resolved from the default",
                                      .fixHint = ""},
                           Diagnostic{.file = "recipes/font/roboto-ui.toml",
                                      .line = 0,
                                      .code = "FB002",
                                      .severity = Severity::Warning,
                                      .message = "charset has no digits",
                                      .fixHint = ""},
                       };
                       state->text = render(diagnostics, Format::Text, kTool);
                   })
            .When("it is rendered as text", [] {})
            .Then("every line follows the file:line:column: severity: [code] message convention",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(
                          state->text ==
                              "recipes/font/roboto-ui.toml:7:22: error: [FB001] glyph budget exceeded\n"
                              "    fix: raise atlasWidth\n"
                              "recipes/font/roboto-ui.toml:3: note: [FB004] charset resolved from "
                              "the default\n"
                              "recipes/font/roboto-ui.toml: warning: [FB002] charset has no "
                              "digits\n",
                          "the text diagnostics");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register severityNamesAreStable{
    "Severity names are stable", "evidence-unit", [] {
        struct State {
            std::array<std::string_view, 3> names;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-severity-names-are-stable")
            .Given("the severity enumerators",
                   [state] {
                       // These strings are part of the published envelope; renaming one breaks
                       // consumers.
                       state->names = {describe(Severity::Error), describe(Severity::Warning),
                                       describe(Severity::Note)};
                   })
            .When("each is described", [] {})
            .Then("the wire names stay the published ones",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->names[0] == "error", "Error");
                      checks.expect(state->names[1] == "warning", "Warning");
                      checks.expect(state->names[2] == "note", "Note");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register exitStatusFailsOnlyOnError{
    "exitStatus fails only on an error", "evidence-unit", [] {
        struct State {
            int none;
            int warningOnly;
            int withError;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("cli-exit-status-fails-only-on-error")
            .Given("a clean run, a warning-only run and a run with an error",
                   [state] {
                        const std::vector<Diagnostic> warnings{
                            Diagnostic{.file = "a",
                                       .code = "",
                                       .severity = Severity::Warning,
                                       .message = "w",
                                       .fixHint = ""},
                            Diagnostic{.file = "b",
                                       .code = "",
                                       .severity = Severity::Note,
                                       .message = "n",
                                       .fixHint = ""},
                        };
                        const std::vector<Diagnostic> error{
                            Diagnostic{.file = "a",
                                       .code = "",
                                       .severity = Severity::Warning,
                                       .message = "w",
                                       .fixHint = ""},
                            Diagnostic{.file = "b",
                                       .code = "",
                                       .severity = Severity::Error,
                                       .message = "e",
                                       .fixHint = ""},
                        };
                       state->none = exitStatus({});
                       state->warningOnly = exitStatus(warnings);
                       state->withError = exitStatus(error);
                   })
            .When("each exit status is computed", [] {})
            .Then("only an error fails the run",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->none == 0, "no findings");
                      // A warning alone must not fail a bake - only CI's byte-comparison decides
                      // that.
                      checks.expect(state->warningOnly == 0, "warnings do not fail");
                      checks.expect(state->withError == 1, "an error fails");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace