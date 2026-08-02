/**
 * @file CliTests.cpp
 * @brief Tests for the host-tools mdux.tools.cli argument parser and diagnostic envelope.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The envelope's field names and JSON shape are asserted literally. They are a published
 * contract that agents key off (issue #19, S3), so a reword must break a test rather than
 * silently break a consumer.
 */

import std;
import mdux.tools.cli;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::tools::cli;

namespace {

constexpr std::string_view kTool = "mdux-fontbake";

[[nodiscard]] std::optional<Invocation> parsedOk(std::vector<std::string_view> arguments) {
    try {
        return parse(kTool, arguments);
    } catch (const UsageError& error) {
        CHECK_MESSAGE(false, std::string{"unexpected UsageError: "} + error.what());
        return std::nullopt;
    }
}

void expectUsageError(std::vector<std::string_view> arguments, std::string_view expectedMention) {
    try {
        (void)parse(kTool, arguments);
        CHECK_MESSAGE(false, "expected a UsageError");
    } catch (const UsageError& error) {
        const std::string message = error.what();
        CHECK_MESSAGE(message.find(expectedMention) != std::string::npos,
                      "message should mention '" + std::string{expectedMention} + "', got: " +
                          message);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

TEST_CASE("bake takes a recipe and an output directory", "evidence-unit") {
    const auto invocation = parsedOk({"bake", "recipes/font/roboto-ui.toml", "build/mdux_bake"});
    REQUIRE(invocation.has_value());
    CHECK(invocation->mode == Mode::Bake);
    CHECK(invocation->format == Format::Text);
    CHECK(invocation->bake.recipe == "recipes/font/roboto-ui.toml");
    CHECK(invocation->bake.outputDir == "build/mdux_bake");
}

TEST_CASE("verify takes a recipe and the two committed files", "evidence-unit") {
    const auto invocation = parsedOk({"verify", "recipes/font/roboto-ui.toml",
                                      "generated/font/roboto-ui/package.json",
                                      "generated/font/roboto-ui/report.json"});
    REQUIRE(invocation.has_value());
    CHECK(invocation->mode == Mode::Verify);
    CHECK(invocation->verify.recipe == "recipes/font/roboto-ui.toml");
    CHECK(invocation->verify.packagePath == "generated/font/roboto-ui/package.json");
    CHECK(invocation->verify.reportPath == "generated/font/roboto-ui/report.json");
}

TEST_CASE("--format is accepted before, between and after positionals", "evidence-unit") {
    for (const std::vector<std::string_view> arguments :
         {std::vector<std::string_view>{"--format=json", "bake", "r.toml", "out"},
          std::vector<std::string_view>{"bake", "--format=json", "r.toml", "out"},
          std::vector<std::string_view>{"bake", "r.toml", "out", "--format=json"}}) {
        const auto invocation = parsedOk(arguments);
        REQUIRE(invocation.has_value());
        CHECK(invocation->format == Format::Json);
        CHECK(invocation->bake.recipe == "r.toml");
        CHECK(invocation->bake.outputDir == "out");
    }

    const auto text = parsedOk({"bake", "r.toml", "out", "--format=text"});
    REQUIRE(text.has_value());
    CHECK(text->format == Format::Text);
}

TEST_CASE("The argc/argv overload skips argv[0]", "evidence-unit") {
    const std::array<const char*, 4> argv{"/usr/local/bin/mdux-fontbake", "bake", "r.toml", "out"};
    const auto invocation = parse(kTool, static_cast<int>(argv.size()), argv.data());
    CHECK(invocation.mode == Mode::Bake);
    CHECK(invocation.bake.recipe == "r.toml");
}

// ---------------------------------------------------------------------------
// Usage errors
// ---------------------------------------------------------------------------

TEST_CASE("Usage errors name what was wrong and print the usage text", "evidence-unit") {
    expectUsageError({}, "expected a subcommand");
    expectUsageError({"build", "r.toml", "out"}, "unrecognized subcommand 'build'");
    expectUsageError({"bake"}, "bake takes exactly 2 arguments");
    expectUsageError({"bake", "r.toml"}, "got 1");
    expectUsageError({"bake", "r.toml", "out", "extra"}, "got 3");
    expectUsageError({"verify", "r.toml", "p.json"}, "verify takes exactly 3 arguments");
    expectUsageError({"--wat", "bake", "r.toml", "out"}, "unrecognized option '--wat'");
    expectUsageError({"bake", "r.toml", "out", "--format=xml"},
                     "unrecognized --format value 'xml'");
    // The space-separated spelling gets its own message rather than "unrecognized option".
    expectUsageError({"bake", "r.toml", "out", "--format", "json"}, "takes its value with '='");

    // Every usage error carries the usage text, so a caller has one place to print it.
    expectUsageError({}, "mdux-fontbake bake   <recipe> <output-dir>");
}

TEST_CASE("--help is a UsageError carrying the usage text", "evidence-unit") {
    for (const std::string_view flag : {"--help", "-h"}) {
        try {
            (void)parse(kTool, std::vector<std::string_view>{flag});
            CHECK_MESSAGE(false, "expected --help to throw");
        } catch (const UsageError& error) {
            CHECK(std::string{error.what()} == usage(kTool));
        }
    }
}

TEST_CASE("usage() documents both subcommands and both options", "evidence-unit") {
    const std::string text = usage(kTool);
    CHECK(text.find("mdux-fontbake bake   <recipe> <output-dir>") != std::string::npos);
    CHECK(text.find("mdux-fontbake verify <recipe> <package.json> <report.json>") !=
          std::string::npos);
    CHECK(text.find("--format=json|text") != std::string::npos);
    CHECK(text.find("--help") != std::string::npos);
    // States the rule a baker author most needs to know.
    CHECK(text.find("verify") != std::string::npos);
    CHECK(text.find("writing nothing") != std::string::npos);
    CHECK(text.find("ADR-007") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The diagnostic envelope
// ---------------------------------------------------------------------------

TEST_CASE("JSON diagnostics use the published envelope shape", "evidence-unit") {
    const std::vector<Diagnostic> diagnostics{
        Diagnostic{.file = "recipes/font/roboto-ui.toml",
                   .line = 7,
                   .column = 22,
                   .code = "FB001",
                   .severity = Severity::Error,
                   .message = "glyph budget exceeded",
                   .fixHint = "raise atlasWidth or reduce the charset"},
    };

    // Pinned literally, and deliberately so: this is the published contract of
    // docs/governance/schemas/diagnostic.schema.json, which every later baker emits. A field
    // added, renamed or reordered here changes what agents parse repository-wide, so it should
    // cost a visible test edit rather than passing unnoticed.
    CHECK(render(diagnostics, Format::Json, kTool) ==
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
          "}\n");
}

TEST_CASE("An unknown position is carried as zero on both axes", "evidence-unit") {
    // A tool with no position at all must still emit both fields. Omitting them would make the
    // envelope's shape depend on the finding, which is exactly what a strict consumer cannot
    // tolerate - `column` is always present, and 0 is how "no column" is spelled.
    const std::vector<Diagnostic> diagnostics{
        Diagnostic{.file = "recipes/font/roboto-ui.toml", .code = "FB003", .message = "no charset"},
    };
    const std::string json = render(diagnostics, Format::Json, kTool);
    CHECK(json.find("\"line\": 0,\n") != std::string::npos);
    CHECK(json.find("\"column\": 0,\n") != std::string::npos);
}

TEST_CASE("JSON diagnostics separate multiple findings with a comma", "evidence-unit") {
    const std::vector<Diagnostic> diagnostics{
        Diagnostic{.file = "a.toml", .line = 1, .code = "A", .message = "first"},
        Diagnostic{.file = "b.toml", .line = 2, .code = "B", .message = "second"},
    };
    const std::string json = render(diagnostics, Format::Json, kTool);
    CHECK(json.find("    },\n    {\n") != std::string::npos);
    CHECK(json.find("\"message\": \"first\"") != std::string::npos);
    CHECK(json.find("\"message\": \"second\"") != std::string::npos);
}

TEST_CASE("An empty finding list still produces a well-formed envelope", "evidence-unit") {
    // A consumer must not have to special-case success; it parses the same shape either way.
    CHECK(render({}, Format::Json, kTool) ==
          "{\n"
          "  \"tool\": \"mdux-fontbake\",\n"
          "  \"findings\": []\n"
          "}\n");
    CHECK(render({}, Format::Text, kTool).empty());
}

TEST_CASE("JSON diagnostics escape what would otherwise break the envelope", "evidence-unit") {
    const std::vector<Diagnostic> diagnostics{
        Diagnostic{.file = "a\"b.toml",
                   .line = 1,
                   .code = "X",
                   .message = "line one\nline two\ttabbed",
                   .fixHint = "use a backslash: \\"},
    };
    const std::string json = render(diagnostics, Format::Json, kTool);
    CHECK(json.find("\"file\": \"a\\\"b.toml\"") != std::string::npos);
    CHECK(json.find("line one\\nline two\\ttabbed") != std::string::npos);
    CHECK(json.find("use a backslash: \\\\") != std::string::npos);
    // A raw newline inside a JSON string would make the envelope unparseable.
    const std::size_t messageStart = json.find("\"message\":");
    REQUIRE(messageStart != std::string::npos);
    CHECK(json.find('\n', messageStart) > json.find("tabbed"));
}

TEST_CASE("Text diagnostics follow the file:line:column: severity: [code] message convention",
          "evidence-unit") {
    const std::vector<Diagnostic> diagnostics{
        Diagnostic{.file = "recipes/font/roboto-ui.toml",
                   .line = 7,
                   .column = 22,
                   .code = "FB001",
                   .severity = Severity::Error,
                   .message = "glyph budget exceeded",
                   .fixHint = "raise atlasWidth"},
        // Knows the line but not the column: the position printed stops at the line rather than
        // gaining a ":0" an editor would jump to.
        Diagnostic{.file = "recipes/font/roboto-ui.toml",
                   .line = 3,
                   .code = "FB004",
                   .severity = Severity::Note,
                   .message = "charset resolved from the default"},
        Diagnostic{.file = "recipes/font/roboto-ui.toml",
                   .line = 0,
                   .code = "FB002",
                   .severity = Severity::Warning,
                   .message = "charset has no digits"},
    };

    CHECK(render(diagnostics, Format::Text, kTool) ==
          "recipes/font/roboto-ui.toml:7:22: error: [FB001] glyph budget exceeded\n"
          "    fix: raise atlasWidth\n"
          "recipes/font/roboto-ui.toml:3: note: [FB004] charset resolved from the default\n"
          "recipes/font/roboto-ui.toml: warning: [FB002] charset has no digits\n");
}

TEST_CASE("Severity names are stable", "evidence-unit") {
    // These strings are part of the published envelope; renaming one breaks consumers.
    CHECK(describe(Severity::Error) == "error");
    CHECK(describe(Severity::Warning) == "warning");
    CHECK(describe(Severity::Note) == "note");
}

TEST_CASE("exitStatus fails only on an error", "evidence-unit") {
    CHECK(exitStatus({}) == 0);

    const std::vector<Diagnostic> warningOnly{
        Diagnostic{.file = "a", .severity = Severity::Warning, .message = "w"},
        Diagnostic{.file = "b", .severity = Severity::Note, .message = "n"},
    };
    // A warning alone must not fail a bake - only CI's byte-comparison decides that.
    CHECK(exitStatus(warningOnly) == 0);

    const std::vector<Diagnostic> withError{
        Diagnostic{.file = "a", .severity = Severity::Warning, .message = "w"},
        Diagnostic{.file = "b", .severity = Severity::Error, .message = "e"},
    };
    CHECK(exitStatus(withError) == 1);
}
