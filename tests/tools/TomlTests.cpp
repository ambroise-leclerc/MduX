/**
 * @file TomlTests.cpp
 * @brief Tests for the host-tools mdux.tools.toml recipe reader.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The rejection tests carry as much weight as the acceptance tests. A parser that quietly
 * accepted a feature outside the subset would let a recipe be written that only some future
 * version of the tool understands, which is exactly the drift the narrow scope prevents.
 */

import std;
import mdux.tools.toml;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::tools::toml;

namespace {

/// Parses `text`, or fails the test with the recipe error rather than propagating it.
[[nodiscard]] std::optional<Document> parsed(std::string_view text) {
    try {
        return parse(text);
    } catch (const TomlError& error) {
        CHECK_MESSAGE(false, "unexpected TomlError at line " + std::to_string(error.line()) + ": " +
                                 error.what());
        return std::nullopt;
    }
}

/// Asserts `text` is rejected, and that the message mentions `expectedMention` - so a test
/// pinning down "outside this subset" cannot pass on an unrelated syntax error.
void expectRejected(std::string_view text, std::string_view expectedMention,
                    std::size_t expectedLine = 0) {
    try {
        (void)parse(text);
        CHECK_MESSAGE(false, "expected rejection for: " + std::string{text});
    } catch (const TomlError& error) {
        const std::string message = error.what();
        CHECK_MESSAGE(message.find(expectedMention) != std::string::npos,
                      "message '" + message + "' should mention '" +
                          std::string{expectedMention} + "'");
        if (expectedLine != 0) {
            CHECK_MESSAGE(error.line() == expectedLine,
                          "expected line " + std::to_string(expectedLine) + ", got " +
                              std::to_string(error.line()));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The supported subset
// ---------------------------------------------------------------------------

TEST_CASE("Root-level key/value pairs of every supported type parse", "evidence-unit") {
    const auto document = parsed(R"(
name = "roboto-ui"
atlasWidth = 512
offset = -17
hinting = false
antialias = true
locales = ["en", "fr", "de"]
sizes = [11, 13, 17]
)");
    REQUIRE(document.has_value());
    const Table& root = document->root();

    CHECK(root.require("name").asString() == "roboto-ui");
    CHECK(root.require("atlasWidth").asInteger() == 512);
    CHECK(root.require("offset").asInteger() == -17);
    CHECK(root.require("hinting").asBoolean() == false);
    CHECK(root.require("antialias").asBoolean() == true);
    // Hoisted into locals: a braced initializer inside CHECK() would split on its commas and
    // be read as extra macro arguments.
    const std::vector<std::string> expectedLocales{"en", "fr", "de"};
    const std::vector<std::int64_t> expectedSizes{11, 13, 17};
    CHECK(root.require("locales").asStringArray() == expectedLocales);
    CHECK(root.require("sizes").asIntegerArray() == expectedSizes);
}

TEST_CASE("Tables are flat and keep their own keys", "evidence-unit") {
    const auto document = parsed(R"(
kind = "font"

[atlas]
width = 512
height = 512

[metrics]
tabularFigures = true
)");
    REQUIRE(document.has_value());

    CHECK(document->root().require("kind").asString() == "font");
    REQUIRE(document->tables().size() == 2);

    const Table& atlas = document->requireTable("atlas");
    CHECK(atlas.require("width").asInteger() == 512);
    CHECK(atlas.require("height").asInteger() == 512);
    // A key in one table is not visible from another, nor from the root.
    CHECK(atlas.find("tabularFigures") == nullptr);
    CHECK(document->root().find("width") == nullptr);

    CHECK(document->requireTable("metrics").require("tabularFigures").asBoolean());
    CHECK(document->table("absent") == nullptr);
}

TEST_CASE("Comments, blank lines and surrounding whitespace are ignored", "evidence-unit") {
    const auto document = parsed(R"(
# a leading comment

  name = "x"   # trailing comment

	[atlas]	# comment after a header
  width  =  512
)");
    REQUIRE(document.has_value());
    CHECK(document->root().require("name").asString() == "x");
    CHECK(document->requireTable("atlas").require("width").asInteger() == 512);
}

TEST_CASE("A '#' inside a string is not a comment", "evidence-unit") {
    const auto document = parsed(R"(
label = "shade #3"
escaped = "quote \" then # hash"
)");
    REQUIRE(document.has_value());
    CHECK(document->root().require("label").asString() == "shade #3");
    CHECK(document->root().require("escaped").asString() == "quote \" then # hash");
}

TEST_CASE("String escapes are decoded", "evidence-unit") {
    const auto document = parsed(R"(
escapes = "a\tb\nc\\d\"e"
unicode = "é\U0001F600"
)");
    REQUIRE(document.has_value());
    CHECK(document->root().require("escapes").asString() == "a\tb\nc\\d\"e");
    CHECK(document->root().require("unicode").asString() == "é\U0001F600");
}

TEST_CASE("Arrays may span lines, nest, be empty, and carry a trailing comma", "evidence-unit") {
    const auto document = parsed(R"(
empty = []
multiline = [
  "en",
  "fr",   # a comment inside the array
  "de",
]
nested = [[1, 2], [3]]
)");
    REQUIRE(document.has_value());

    const std::vector<std::string> expectedLocales{"en", "fr", "de"};
    const std::vector<std::int64_t> expectedFirst{1, 2};
    const std::vector<std::int64_t> expectedSecond{3};

    CHECK(document->root().require("empty").asArray().empty());
    CHECK(document->root().require("multiline").asStringArray() == expectedLocales);

    const std::span<const Value> nested = document->root().require("nested").asArray();
    REQUIRE(nested.size() == 2);
    CHECK(nested[0].asIntegerArray() == expectedFirst);
    CHECK(nested[1].asIntegerArray() == expectedSecond);
}

TEST_CASE("Integer bounds are handled exactly", "evidence-unit") {
    const auto document = parsed(R"(
max = 9223372036854775807
min = -9223372036854775808
zero = 0
positive = +5
)");
    REQUIRE(document.has_value());
    CHECK(document->root().require("max").asInteger() ==
          std::numeric_limits<std::int64_t>::max());
    CHECK(document->root().require("min").asInteger() ==
          std::numeric_limits<std::int64_t>::min());
    CHECK(document->root().require("zero").asInteger() == 0);
    CHECK(document->root().require("positive").asInteger() == 5);
}

TEST_CASE("An empty document is valid", "evidence-unit") {
    const auto document = parsed("");
    REQUIRE(document.has_value());
    CHECK(document->root().entries().empty());
    CHECK(document->tables().empty());

    const auto commentsOnly = parsed("# nothing but a comment\n\n# and another\n");
    REQUIRE(commentsOnly.has_value());
    CHECK(commentsOnly->root().entries().empty());
}

// ---------------------------------------------------------------------------
// Outside the subset
// ---------------------------------------------------------------------------

TEST_CASE("Floats are rejected with a message pointing at the bit-pattern rule", "evidence-unit") {
    // The ADR-007 rule surfaced where a recipe author will actually meet it.
    expectRejected("pixelSize = 13.5\n", "floats are rejected", 1);
    expectRejected("scale = 1e5\n", "floats are rejected", 1);
    expectRejected("scale = 1E5\n", "floats are rejected", 1);
    // The message must say what to do instead, not merely that it failed.
    try {
        (void)parse("pixelSize = 13.5\n");
    } catch (const TomlError& error) {
        const std::string message = error.what();
        CHECK(message.find("bit pattern") != std::string::npos);
        CHECK(message.find("ADR-007") != std::string::npos);
    }
}

TEST_CASE("Features outside the subset are named in the diagnostic", "evidence-unit") {
    expectRejected("a.b = 1\n", "dotted key", 1);
    expectRejected("[font.atlas]\n", "dotted table name", 1);
    expectRejected("[[products]]\n", "arrays of tables", 1);
    expectRejected("point = { x = 1 }\n", "inline tables", 1);
    expectRejected("name = 'literal'\n", "literal", 1);
    expectRejected("\"quoted\" = 1\n", "quoted key", 1);
    expectRejected("mask = 0xff\n", "hexadecimal", 1);
    expectRejected("mask = 0o17\n", "hexadecimal", 1);
    expectRejected("mask = 0b1010\n", "hexadecimal", 1);
    expectRejected("count = 1_000\n", "digit separators", 1);
    expectRejected("when = 1979-05-27\n", "datetimes", 1);
}

TEST_CASE("Malformed syntax is rejected on the offending line", "evidence-unit") {
    expectRejected("name = \"x\"\nbroken\n", "expected 'key = value'", 2);
    expectRejected("[atlas\n", "closing ']'", 1);
    expectRejected("name = \"unterminated\n", "not terminated", 1);
    expectRejected("name = \n", "expected a value", 1);
    expectRejected("= 1\n", "key is empty", 1);
    expectRejected("name = \"x\" extra\n", "unexpected text after the value", 1);
    expectRejected("list = [1, 2\n", "not terminated", 1);
    expectRejected("list = [1 2]\n", "expected ',' or ']'", 1);
    expectRejected("name = maybe\n", "unrecognized value", 1);
    expectRejected("key! = 1\n", "outside [A-Za-z0-9_-]", 1);
    expectRejected("name = \"bad\\qescape\"\n", "unrecognized escape", 1);
}

TEST_CASE("Integers outside 64-bit range are rejected", "evidence-unit") {
    expectRejected("n = 9223372036854775808\n", "does not fit", 1);
    expectRejected("n = -9223372036854775809\n", "does not fit", 1);
    expectRejected("n = 99999999999999999999999\n", "does not fit", 1);
}

TEST_CASE("Duplicate keys and tables are rejected rather than overwritten", "evidence-unit") {
    // Taking the last value silently is how an author's intent gets lost, and in a pipeline
    // whose whole point is reproducibility, an ambiguous recipe is not acceptable input.
    expectRejected("a = 1\na = 2\n", "duplicate key", 2);
    expectRejected("[t]\na = 1\na = 2\n", "duplicate key", 3);
    expectRejected("[t]\nx = 1\n[t]\ny = 2\n", "duplicate table", 3);

    // The same key in two different tables is fine.
    const auto document = parsed("[a]\nwidth = 1\n\n[b]\nwidth = 2\n");
    REQUIRE(document.has_value());
    CHECK(document->requireTable("a").require("width").asInteger() == 1);
    CHECK(document->requireTable("b").require("width").asInteger() == 2);
}

TEST_CASE("Line numbers survive multi-line arrays", "evidence-unit") {
    // An off-by-one here would send a recipe author to the wrong line, which is the main thing
    // a diagnostic must not do.
    expectRejected("a = [\n 1,\n 2,\n]\nbroken\n", "expected 'key = value'", 5);
}

// ---------------------------------------------------------------------------
// Typed accessors
// ---------------------------------------------------------------------------

TEST_CASE("Accessors report the expected and actual type", "evidence-unit") {
    const auto document = parsed("text = \"x\"\nnumber = 1\nflag = true\nlist = [1]\n");
    REQUIRE(document.has_value());
    const Table& root = document->root();

    const auto expectTypeError = [](auto&& call, std::string_view expected) {
        try {
            call();
            CHECK_MESSAGE(false, "expected a type error");
        } catch (const TomlError& error) {
            const std::string message = error.what();
            CHECK_MESSAGE(message.find(expected) != std::string::npos,
                          "message '" + message + "' should mention '" + std::string{expected} +
                              "'");
        }
    };

    expectTypeError([&] { return root.require("text").asInteger(); }, "expected an integer");
    expectTypeError([&] { return root.require("number").asString(); }, "expected a string");
    expectTypeError([&] { return root.require("flag").asArray(); }, "expected an array");
    expectTypeError([&] { return root.require("list").asBoolean(); }, "expected a boolean");
    // And the actual type is named too, not just the expected one.
    expectTypeError([&] { return root.require("text").asInteger(); }, "found a string");
}

TEST_CASE("Array accessors name the offending element index", "evidence-unit") {
    const auto document = parsed("mixed = [\"a\", 2, \"c\"]\n");
    REQUIRE(document.has_value());
    try {
        (void)document->root().require("mixed").asStringArray();
        CHECK_MESSAGE(false, "expected a type error");
    } catch (const TomlError& error) {
        const std::string message = error.what();
        CHECK(message.find("element 1") != std::string::npos);
        CHECK(message.find("expected a string") != std::string::npos);
    }
}

TEST_CASE("require() names the missing key or table", "evidence-unit") {
    const auto document = parsed("[atlas]\nwidth = 1\n");
    REQUIRE(document.has_value());

    try {
        (void)document->requireTable("atlas").require("height");
        CHECK_MESSAGE(false, "expected a missing-key error");
    } catch (const TomlError& error) {
        CHECK(std::string{error.what()}.find("height") != std::string::npos);
        // The line reported is the table's header, which is where an author adds the key.
        CHECK(error.line() == 1);
    }

    try {
        (void)document->requireTable("metrics");
        CHECK_MESSAGE(false, "expected a missing-table error");
    } catch (const TomlError& error) {
        CHECK(std::string{error.what()}.find("[metrics]") != std::string::npos);
    }
}

TEST_CASE("Values carry the line they were written on", "evidence-unit") {
    const auto document = parsed("a = 1\n\nb = 2\n\n[t]\nc = 3\n");
    REQUIRE(document.has_value());
    CHECK(document->root().require("a").line() == 1);
    CHECK(document->root().require("b").line() == 3);
    CHECK(document->requireTable("t").line() == 5);
    CHECK(document->requireTable("t").require("c").line() == 6);
}
