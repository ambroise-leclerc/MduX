/**
 * @file JsonTests.cpp
 * @brief Tests for the governed-zone mdux.evidence.json module.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Two things are being established here, and they are not the same thing:
 *
 * 1. The writer emits exactly one byte sequence for a given value - asserted against literal
 *    expected strings, not against a re-serialization, because a test that compares the writer
 *    to itself cannot detect that the canonical form changed.
 * 2. The reader accepts that byte sequence and rejects everything a hand-edited or
 *    foreign-written artifact would contain.
 */

import std;
import mdux.evidence.json;
import mdux.core.result;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::evidence::json;

namespace {

/// Builds an object from members in the order given, so a test can vary insertion order.
[[nodiscard]] Value objectOf(std::vector<std::pair<std::string, Value>> members) {
    Value object = Value::emptyObject();
    for (auto& [key, value] : members) {
        // A duplicate here is a bug in the test, not in the code under test. Throwing surfaces
        // it through MduXTest's fatal-error path, which names the test case; an assert would
        // take the whole run down without saying which one.
        if (const auto inserted = object.set(key, std::move(value)); !inserted.has_value()) {
            throw std::runtime_error{"test bug: duplicate key '" + key + "' in objectOf"};
        }
    }
    return object;
}

[[nodiscard]] std::string written(const Value& value) {
    const auto result = write(value);
    if (!result.has_value()) {
        return "<write failed: " + std::string{describe(result.error().code)} + ">";
    }
    return *result;
}

/// Asserts that `text` is rejected, and rejected with the expected code - a strictness test that
/// only checked "it failed" would pass for the wrong reason after a refactor.
void expectRejected(std::string_view text, ErrorCode expected, std::string_view what) {
    const auto result = parse(text);
    if (result.has_value()) {
        CHECK_MESSAGE(false, std::string{what} + ": expected rejection, but it parsed");
        return;
    }
    CHECK_MESSAGE(result.error().code == expected,
                  std::string{what} + ": expected '" + std::string{describe(expected)} +
                      "' but got '" + std::string{describe(result.error().code)} + "'");
}

[[nodiscard]] std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

}  // namespace

// ---------------------------------------------------------------------------
// Canonical form, asserted literally
// ---------------------------------------------------------------------------

TEST_CASE("Canonical writer emits two-space indent, sorted keys and a trailing newline",
          "evidence-unit") {
    const Value value = objectOf({{"tool", Value::string("mdux-fontbake")},
                                  {"schemaVersion", Value::unsignedInteger(1)}});

    CHECK(written(value) ==
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"tool\": \"mdux-fontbake\"\n"
          "}\n");
}

TEST_CASE("Canonical writer nests arrays and objects at the right depth", "evidence-unit") {
    Value input = objectOf({{"path", Value::string("assets/fonts/Roboto-Regular.ttf")},
                            {"sha256", Value::string("abc123")}});
    const Value value = objectOf({{"inputs", Value::array({std::move(input)})}});

    CHECK(written(value) ==
          "{\n"
          "  \"inputs\": [\n"
          "    {\n"
          "      \"path\": \"assets/fonts/Roboto-Regular.ttf\",\n"
          "      \"sha256\": \"abc123\"\n"
          "    }\n"
          "  ]\n"
          "}\n");
}

TEST_CASE("Empty containers are written compactly", "evidence-unit") {
    const Value value = objectOf({{"outputs", Value::array({})},
                                  {"options", Value::emptyObject()}});

    CHECK(written(value) ==
          "{\n"
          "  \"options\": {},\n"
          "  \"outputs\": []\n"
          "}\n");
}

TEST_CASE("A float is written as its u32 bit pattern, never as decimal text", "evidence-unit") {
    const Value value = objectOf({{"advance", Value::float32(1.0F)}});

    CHECK(written(value) ==
          "{\n"
          "  \"advance\": {\n"
          "    \"bits\": 1065353216\n"
          "  }\n"
          "}\n");

    // The point of the rule, stated as a test: no decimal point reaches the output.
    CHECK(written(value).find('.') == std::string::npos);
}

TEST_CASE("Scalars and escapes are written canonically", "evidence-unit") {
    CHECK(written(Value::null()) == "null\n");
    CHECK(written(Value::boolean(true)) == "true\n");
    CHECK(written(Value::boolean(false)) == "false\n");
    CHECK(written(Value::unsignedInteger(0)) == "0\n");
    CHECK(written(Value::integer(-42)) == "-42\n");
    CHECK(written(Value::unsignedInteger(std::numeric_limits<std::uint64_t>::max())) ==
          "18446744073709551615\n");
    CHECK(written(Value::integer(std::numeric_limits<std::int64_t>::min())) ==
          "-9223372036854775808\n");

    // Shorthand escapes where one exists, \u00XX otherwise, and `/` deliberately left alone.
    CHECK(written(Value::string("a\"b\\c\nd\te/f")) == "\"a\\\"b\\\\c\\nd\\te/f\"\n");
    CHECK(written(Value::string(std::string{"x\x01y"})) == "\"x\\u0001y\"\n");
    // Non-ASCII stays raw UTF-8 rather than being escaped.
    CHECK(written(Value::string("mm\u00b2")) == "\"mm\u00b2\"\n");
}

TEST_CASE("Key ordering does not depend on insertion order", "evidence-unit") {
    const std::vector<std::string> keys{"zebra", "alpha", "Zulu", "beta", "ALPHA", "a"};

    // Insert the same keys in several different orders; every one must serialize identically.
    std::string reference;
    for (std::size_t rotation = 0; rotation < keys.size(); ++rotation) {
        std::vector<std::pair<std::string, Value>> members;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const std::string& key = keys[(i + rotation) % keys.size()];
            members.emplace_back(key, Value::unsignedInteger(i));
        }
        // The values differ between rotations, so compare only the key sequence.
        const Value object = objectOf(std::move(members));
        std::string order;
        for (const Member& member : object.members()) {
            order += member.key;
            order.push_back('|');
        }
        if (rotation == 0) {
            reference = order;
        }
        CHECK_MESSAGE(order == reference, "rotation " + std::to_string(rotation) +
                                              " produced key order " + order);
    }

    // Sorted by UTF-8 code unit, so uppercase sorts before lowercase - not a locale collation.
    CHECK(reference == "ALPHA|Zulu|a|alpha|beta|zebra|");
}

// ---------------------------------------------------------------------------
// Round-tripping
// ---------------------------------------------------------------------------

TEST_CASE("A report-shaped value round-trips through write and parse", "evidence-unit") {
    Value recipe = objectOf({{"path", Value::string("recipes/font/roboto-ui.toml")},
                             {"sha256", Value::string("11aa")}});
    Value input = objectOf({{"path", Value::string("assets/fonts/Roboto-Regular.ttf")},
                            {"sha256", Value::string("22bb")}});
    Value outputPackage = objectOf({{"path", Value::string("package.json")},
                                    {"sha256", Value::string("33cc")}});
    Value outputAtlas = objectOf({{"path", Value::string("atlas.bin")},
                                  {"sha256", Value::string("44dd")}});
    Value options = objectOf({{"atlasWidth", Value::unsignedInteger(512)},
                              {"hinting", Value::boolean(false)},
                              {"pixelSize", Value::float32(13.5F)}});

    const Value report =
        objectOf({{"schemaVersion", Value::unsignedInteger(1)},
                  {"tool", Value::string("mdux-fontbake")},
                  {"toolVersion", Value::string("0.2.0")},
                  {"toolGitSha", Value::string("a1b2c3d")},
                  {"recipe", std::move(recipe)},
                  {"inputs", Value::array({std::move(input)})},
                  {"options", std::move(options)},
                  {"outputs", Value::array({std::move(outputPackage), std::move(outputAtlas)})}});

    const std::string text = written(report);
    const auto reparsed = parse(text);
    REQUIRE(reparsed.has_value());

    // The real property: re-serializing the parsed value reproduces the identical bytes. That is
    // byte-identity in miniature, and it is what CI asserts at file scale.
    CHECK(written(*reparsed) == text);

    // Spot-check that the structure actually survived, not just the byte count.
    const auto tool = reparsed->require("tool");
    REQUIRE(tool.has_value());
    CHECK((*tool)->asString().value_or("") == "mdux-fontbake");

    const auto outputs = reparsed->require("outputs");
    REQUIRE(outputs.has_value());
    REQUIRE((*outputs)->elements().size() == 2);
    // Array order is preserved; only object keys are sorted.
    CHECK((*outputs)->elements()[0].find("path")->asString().value_or("") == "package.json");
    CHECK((*outputs)->elements()[1].find("path")->asString().value_or("") == "atlas.bin");

    const auto options2 = reparsed->require("options");
    REQUIRE(options2.has_value());
    const Value* pixelSize = (*options2)->find("pixelSize");
    REQUIRE(pixelSize != nullptr);
    const auto decoded = pixelSize->asFloat32();
    REQUIRE(decoded.has_value());
    CHECK(bitsOf(*decoded) == bitsOf(13.5F));
}

TEST_CASE("Every notable f32 bit pattern survives a round trip bit-exactly", "evidence-unit") {
    const std::vector<float> values{
        0.0F,
        -0.0F,
        1.0F,
        -1.0F,
        std::numeric_limits<float>::min(),             // smallest positive normal
        std::numeric_limits<float>::denorm_min(),      // smallest positive denormal
        -std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::epsilon(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        13.5F,
        3.14159265F,
    };

    for (const float value : values) {
        const Value wrapped = objectOf({{"v", Value::float32(value)}});
        const std::string text = written(wrapped);

        const auto reparsed = parse(text);
        REQUIRE(reparsed.has_value());
        const Value* member = reparsed->find("v");
        REQUIRE(member != nullptr);
        const auto decoded = member->asFloat32();
        REQUIRE(decoded.has_value());

        // Compared as bit patterns, not as floats: NaN != NaN, and -0.0 == 0.0, so a float
        // comparison would pass for two of these while the bytes differed.
        CHECK_MESSAGE(bitsOf(*decoded) == bitsOf(value),
                      "bit pattern " + std::to_string(bitsOf(value)) + " came back as " +
                          std::to_string(bitsOf(*decoded)));
        CHECK(written(*reparsed) == text);
    }
}

TEST_CASE("Infinity and NaN are representable as bit patterns even though the literals are not",
          "evidence-unit") {
    // Worth stating explicitly: rejecting the `NaN` *literal* does not mean a baker cannot record
    // a NaN. It records the bit pattern, which is exactly representable and portable.
    const Value value = objectOf({{"v", Value::float32(std::numeric_limits<float>::quiet_NaN())}});
    CHECK(written(value).find("NaN") == std::string::npos);

    const auto reparsed = parse(written(value));
    REQUIRE(reparsed.has_value());
    const auto decoded = reparsed->find("v")->asFloat32();
    REQUIRE(decoded.has_value());
    CHECK(std::isnan(*decoded));
}

TEST_CASE("Unicode escapes and surrogate pairs decode correctly", "evidence-unit") {
    const auto basic = parse("\"\\u00e9\"");
    REQUIRE(basic.has_value());
    CHECK(basic->asString().value_or("") == "\u00e9");

    // U+1F600, which requires a surrogate pair in the \u form.
    const auto astral = parse("\"\\ud83d\\ude00\"");
    REQUIRE(astral.has_value());
    CHECK(astral->asString().value_or("") == "\U0001F600");

    // Round-tripping re-emits it as raw UTF-8, since the writer escapes only what it must.
    CHECK(written(*astral) == "\"\U0001F600\"\n");
}

TEST_CASE("Whitespace between tokens is accepted, and a trailing newline is fine", "evidence-unit") {
    const auto spaced = parse("{\n  \"a\" :  1 ,\n  \"b\" : [ 1 , 2 ]\n}\n");
    REQUIRE(spaced.has_value());
    CHECK(written(*spaced) ==
          "{\n"
          "  \"a\": 1,\n"
          "  \"b\": [\n"
          "    1,\n"
          "    2\n"
          "  ]\n"
          "}\n");
}

// ---------------------------------------------------------------------------
// Strictness
// ---------------------------------------------------------------------------

TEST_CASE("The reader rejects what a hand-edited artifact would contain", "evidence-unit") {
    expectRejected("{\"a\": 1, \"a\": 2}", ErrorCode::DuplicateKey, "duplicate key");
    expectRejected("{\"a\": 1,}", ErrorCode::TrailingComma, "trailing comma in an object");
    expectRejected("[1, 2,]", ErrorCode::TrailingComma, "trailing comma in an array");
    expectRejected("{\"a\": 1} // note", ErrorCode::TrailingContent, "line comment after a value");
    expectRejected("{// note\n\"a\": 1}", ErrorCode::CommentRejected, "line comment in an object");
    expectRejected("/* note */ 1", ErrorCode::CommentRejected, "block comment before a value");
    expectRejected("NaN", ErrorCode::NonFiniteLiteralRejected, "NaN literal");
    expectRejected("nan", ErrorCode::NonFiniteLiteralRejected, "lowercase nan literal");
    expectRejected("Infinity", ErrorCode::NonFiniteLiteralRejected, "Infinity literal");
    expectRejected("-Infinity", ErrorCode::NonFiniteLiteralRejected, "-Infinity literal");
    expectRejected("\xef\xbb\xbf{}", ErrorCode::ByteOrderMarkRejected, "byte-order mark");
    expectRejected("{} {}", ErrorCode::TrailingContent, "a second top-level value");
    expectRejected("\"a\x01\"", ErrorCode::UnescapedControlCharacter, "raw control character");
    expectRejected("\"\\q\"", ErrorCode::InvalidEscape, "unrecognized escape");
    expectRejected("\"\\u00\"", ErrorCode::InvalidEscape, "truncated \\u escape");
    expectRejected("\"\\ud83d\"", ErrorCode::InvalidEscape, "unpaired high surrogate");
    expectRejected("\"\\udc00\"", ErrorCode::InvalidEscape, "unpaired low surrogate");
    expectRejected("\"\xc3\"", ErrorCode::InvalidUtf8, "truncated UTF-8 sequence");
    expectRejected("\"\xc0\xaf\"", ErrorCode::InvalidUtf8, "overlong UTF-8 encoding");
    expectRejected("\"\xed\xa0\x80\"", ErrorCode::InvalidUtf8, "UTF-8-encoded surrogate");
    expectRejected("{\"a\" 1}", ErrorCode::UnexpectedCharacter, "missing colon");
    expectRejected("{\"a\": 1", ErrorCode::UnexpectedEnd, "unterminated object");
    expectRejected("[1", ErrorCode::UnexpectedEnd, "unterminated array");
    expectRejected("\"abc", ErrorCode::UnexpectedEnd, "unterminated string");
    expectRejected("", ErrorCode::UnexpectedEnd, "empty input");
}

TEST_CASE("The reader rejects any number carrying a fraction or exponent", "evidence-unit") {
    // Canonical form encodes real numbers as {"bits": N}, so a decimal float in a generated/
    // file means it was not written by this writer - which is precisely what must not pass.
    expectRejected("1.5", ErrorCode::FractionalNumberRejected, "fraction");
    expectRejected("1.0", ErrorCode::FractionalNumberRejected, "fraction with a zero part");
    expectRejected("1e5", ErrorCode::FractionalNumberRejected, "lowercase exponent");
    expectRejected("1E5", ErrorCode::FractionalNumberRejected, "uppercase exponent");
    expectRejected("{\"pixelSize\": 13.5}", ErrorCode::FractionalNumberRejected,
                   "fraction inside an object");
}

TEST_CASE("The reader rejects non-canonical integer spellings", "evidence-unit") {
    expectRejected("01", ErrorCode::InvalidNumber, "leading zero");
    expectRejected("-0", ErrorCode::InvalidNumber, "negative zero");
    expectRejected("+1", ErrorCode::UnexpectedCharacter, "leading plus");
    expectRejected("-", ErrorCode::UnexpectedEnd, "bare minus");
    expectRejected("18446744073709551616", ErrorCode::NumberOutOfRange, "u64 overflow");
    expectRejected("-9223372036854775809", ErrorCode::NumberOutOfRange, "i64 underflow");
}

TEST_CASE("The reader rejects nesting past the depth limit", "evidence-unit") {
    const std::string tooDeep = std::string(kMaxDepth + 2, '[') + std::string(kMaxDepth + 2, ']');
    expectRejected(tooDeep, ErrorCode::DepthExceeded, "excessive nesting");

    // The limit is generous, not tight: a shape well past anything a baker emits still parses.
    const std::string acceptable = std::string(8, '[') + "1" + std::string(8, ']');
    CHECK(parse(acceptable).has_value());
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

TEST_CASE("Accessors report the wrong kind rather than guessing", "evidence-unit") {
    const Value text = Value::string("1");
    CHECK(!text.asUInt().has_value());
    CHECK(text.asUInt().error().code == ErrorCode::WrongKind);
    CHECK(!text.asBool().has_value());
    CHECK(!text.asFloat32().has_value());

    const Value number = Value::unsignedInteger(1);
    CHECK(!number.asString().has_value());
    CHECK(number.asString().error().code == ErrorCode::WrongKind);
}

TEST_CASE("Integer accessors enforce exact representability", "evidence-unit") {
    const Value negative = Value::integer(-1);
    CHECK(!negative.asUInt().has_value());
    CHECK(negative.asUInt().error().code == ErrorCode::NotExactlyRepresentable);
    CHECK(negative.asInt().value_or(0) == -1);

    const Value huge = Value::unsignedInteger(
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u);
    CHECK(!huge.asInt().has_value());
    CHECK(huge.asInt().error().code == ErrorCode::NotExactlyRepresentable);
    CHECK(huge.asUInt().has_value());
}

TEST_CASE("asFloat32 accepts only a single-member bits object", "evidence-unit") {
    const auto twoMembers = parse("{\"bits\": 1, \"extra\": 2}");
    REQUIRE(twoMembers.has_value());
    CHECK(!twoMembers->asFloat32().has_value());
    CHECK(twoMembers->asFloat32().error().code == ErrorCode::WrongKind);

    const auto wrongName = parse("{\"bytes\": 1}");
    REQUIRE(wrongName.has_value());
    CHECK(!wrongName->asFloat32().has_value());

    const auto tooLarge = parse("{\"bits\": 4294967296}");
    REQUIRE(tooLarge.has_value());
    CHECK(!tooLarge->asFloat32().has_value());
    CHECK(tooLarge->asFloat32().error().code == ErrorCode::NumberOutOfRange);

    const auto valid = parse("{\"bits\": 1065353216}");
    REQUIRE(valid.has_value());
    REQUIRE(valid->asFloat32().has_value());
    CHECK(bitsOf(valid->asFloat32().value()) == bitsOf(1.0F));
}

TEST_CASE("set() rejects a duplicate key instead of overwriting", "evidence-unit") {
    Value object = Value::emptyObject();
    REQUIRE(object.set("a", Value::unsignedInteger(1)).has_value());

    const auto again = object.set("a", Value::unsignedInteger(2));
    CHECK(!again.has_value());
    CHECK(again.error().code == ErrorCode::DuplicateKey);
    // The original value is intact - a rejected set() is not a partial one.
    CHECK(object.find("a")->asUInt().value_or(0) == 1);
}

TEST_CASE("set() and push() reject the wrong container kind", "evidence-unit") {
    Value array = Value::array({});
    CHECK(!array.set("a", Value::null()).has_value());
    CHECK(array.set("a", Value::null()).error().code == ErrorCode::WrongKind);
    CHECK(array.push(Value::unsignedInteger(1)).has_value());

    Value object = Value::emptyObject();
    CHECK(!object.push(Value::null()).has_value());
    CHECK(object.push(Value::null()).error().code == ErrorCode::WrongKind);
}

TEST_CASE("find() and require() agree, and require() names the missing member", "evidence-unit") {
    const Value object = objectOf({{"present", Value::unsignedInteger(1)}});

    CHECK(object.find("present") != nullptr);
    CHECK(object.find("absent") == nullptr);
    CHECK(object.require("present").has_value());

    const auto missing = object.require("absent");
    CHECK(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::MissingMember);
    CHECK(missing.error().detail.find("absent") != std::string::npos);
}

TEST_CASE("The writer rejects invalid UTF-8 rather than emitting it", "evidence-unit") {
    const Value badString = Value::string(std::string{"\xc3"});
    const auto result = write(badString);
    CHECK(!result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidUtf8);

    Value object = Value::emptyObject();
    REQUIRE(object.set(std::string{"\xff"}, Value::null()).has_value());
    const auto keyResult = write(object);
    CHECK(!keyResult.has_value());
    CHECK(keyResult.error().code == ErrorCode::InvalidUtf8);
}

TEST_CASE("describe() names every error code", "evidence-unit") {
    // Guards against a new ErrorCode being added without a diagnostic string, which would
    // otherwise surface as "unrecognized error" in a baker's output.
    constexpr std::array<ErrorCode, 18> all{
        ErrorCode::UnexpectedEnd,            ErrorCode::UnexpectedCharacter,
        ErrorCode::InvalidNumber,            ErrorCode::FractionalNumberRejected,
        ErrorCode::NumberOutOfRange,         ErrorCode::NonFiniteLiteralRejected,
        ErrorCode::DuplicateKey,             ErrorCode::TrailingComma,
        ErrorCode::CommentRejected,          ErrorCode::InvalidEscape,
        ErrorCode::UnescapedControlCharacter, ErrorCode::InvalidUtf8,
        ErrorCode::ByteOrderMarkRejected,    ErrorCode::TrailingContent,
        ErrorCode::DepthExceeded,            ErrorCode::WrongKind,
        ErrorCode::MissingMember,            ErrorCode::NotExactlyRepresentable};

    for (const ErrorCode code : all) {
        CHECK(!describe(code).empty());
        CHECK(describe(code) != "unrecognized error");
    }
}
