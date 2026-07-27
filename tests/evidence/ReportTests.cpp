/**
 * @file ReportTests.cpp
 * @brief Tests for the governed-zone mdux.evidence.report module.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 */

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::evidence;

namespace {

[[nodiscard]] Digest digestOf(std::string_view text) noexcept {
    return sha256(std::as_bytes(std::span{text}));
}

[[nodiscard]] std::string hexOf(const Digest& digest) {
    const std::array<char, 64> chars = toHex(digest);
    return std::string{chars.data(), chars.size()};
}

/// A report that passes validate(), so each test can invalidate exactly one thing.
[[nodiscard]] BakeReport validReport() {
    json::Value options = json::Value::emptyObject();
    (void)options.set("atlasWidth", json::Value::unsignedInteger(512));
    (void)options.set("hinting", json::Value::boolean(false));

    return BakeReport{
        .schemaVersion = kSchemaVersion,
        .tool = "mdux-fontbake",
        .toolVersion = "0.2.0",
        .recipe = {.path = "recipes/font/roboto-ui.toml", .sha256 = digestOf("recipe")},
        .inputs = {{.path = "assets/fonts/Roboto-Regular.ttf", .sha256 = digestOf("font")}},
        .options = std::move(options),
        .outputs = {{.path = "package.json", .sha256 = digestOf("package")},
                    {.path = "atlas.bin", .sha256 = digestOf("atlas")}},
    };
}

void expectInvalid(const BakeReport& report, ReportError expected, std::string_view what) {
    const auto result = report.validate();
    if (result.has_value()) {
        CHECK_MESSAGE(false, std::string{what} + ": expected validate() to reject it");
        return;
    }
    CHECK_MESSAGE(result.error() == expected,
                  std::string{what} + ": expected '" + std::string{describe(expected)} +
                      "' but got '" + std::string{describe(result.error())} + "'");
}

}  // namespace

// ---------------------------------------------------------------------------
// Canonical serialization
// ---------------------------------------------------------------------------

TEST_CASE("A report serializes to the documented canonical shape", "evidence-unit") {
    BakeReport report = validReport();
    // Trim to one input and one output so the expected text stays readable.
    report.outputs.resize(1);
    report.options = json::Value::emptyObject();

    const auto text = report.write();
    REQUIRE(text.has_value());

    const std::string expected =
        "{\n"
        "  \"inputs\": [\n"
        "    {\n"
        "      \"path\": \"assets/fonts/Roboto-Regular.ttf\",\n"
        "      \"sha256\": \"" + hexOf(digestOf("font")) + "\"\n"
        "    }\n"
        "  ],\n"
        "  \"options\": {},\n"
        "  \"outputs\": [\n"
        "    {\n"
        "      \"path\": \"package.json\",\n"
        "      \"sha256\": \"" + hexOf(digestOf("package")) + "\"\n"
        "    }\n"
        "  ],\n"
        "  \"recipe\": {\n"
        "    \"path\": \"recipes/font/roboto-ui.toml\",\n"
        "    \"sha256\": \"" + hexOf(digestOf("recipe")) + "\"\n"
        "  },\n"
        "  \"schemaVersion\": 1,\n"
        "  \"tool\": \"mdux-fontbake\",\n"
        "  \"toolVersion\": \"0.2.0\"\n"
        "}\n";

    CHECK(*text == expected);
}

TEST_CASE("A report round-trips through write and parse", "evidence-unit") {
    const BakeReport original = validReport();
    const auto text = original.write();
    REQUIRE(text.has_value());

    const auto reparsed = BakeReport::parse(*text);
    REQUIRE(reparsed.has_value());

    CHECK(reparsed->schemaVersion == original.schemaVersion);
    CHECK(reparsed->tool == original.tool);
    CHECK(reparsed->toolVersion == original.toolVersion);
    CHECK(reparsed->recipe.path == original.recipe.path);
    CHECK(reparsed->recipe.sha256 == original.recipe.sha256);
    REQUIRE(reparsed->inputs.size() == 1);
    CHECK(reparsed->inputs[0].path == original.inputs[0].path);
    CHECK(reparsed->inputs[0].sha256 == original.inputs[0].sha256);
    REQUIRE(reparsed->outputs.size() == 2);
    // Array order is preserved, so an output list stays in the order the baker wrote it.
    CHECK(reparsed->outputs[0].path == "package.json");
    CHECK(reparsed->outputs[1].path == "atlas.bin");

    // The property CI actually depends on: re-serializing reproduces identical bytes.
    const auto rewritten = reparsed->write();
    REQUIRE(rewritten.has_value());
    CHECK(*rewritten == *text);
}

TEST_CASE("Two reports built from identical inputs are byte-identical, regardless of when each "
          "was built",
          "evidence-unit") {
    // The regression this guards: an earlier design embedded a configure-time git commit SHA in
    // the report (toolGitSha), which is self-referential for a committed, byte-compared artifact
    // - baking at commit H0 and then committing the report creates a different commit H1, so a
    // report re-baked at H1 could never match one committed at H0, on every single report,
    // permanently (see ADR-007, decision 5). BakeReport now has no field whose value could differ
    // between "just baked" and "re-baked after being committed" for identical recipe/inputs/
    // options - this test is what would catch a future field reintroducing that problem.
    const BakeReport bakedBeforeCommit = validReport();
    const BakeReport rebakedAfterCommit = validReport();  // simulates CI re-baking at a later commit

    const auto first = bakedBeforeCommit.write();
    const auto second = rebakedAfterCommit.write();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
}

TEST_CASE("Resolved options survive the round trip intact", "evidence-unit") {
    BakeReport report = validReport();
    json::Value options = json::Value::emptyObject();
    REQUIRE(options.set("atlasWidth", json::Value::unsignedInteger(512)).has_value());
    REQUIRE(options.set("hinting", json::Value::boolean(false)).has_value());
    REQUIRE(options.set("pixelSize", json::Value::float32(13.5F)).has_value());
    REQUIRE(options.set("name", json::Value::string("roboto-ui")).has_value());
    report.options = std::move(options);

    const auto text = report.write();
    REQUIRE(text.has_value());
    // Floats inside options obey the same rule as everywhere else.
    CHECK(text->find("\"bits\": 1096286208") != std::string::npos);

    const auto reparsed = BakeReport::parse(*text);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->options.find("atlasWidth")->asUInt().value_or(0) == 512);
    CHECK(reparsed->options.find("hinting")->asBool().value_or(true) == false);
    CHECK(reparsed->options.find("name")->asString().value_or("") == "roboto-ui");

    const json::Value* pixelSize = reparsed->options.find("pixelSize");
    REQUIRE(pixelSize != nullptr);
    const auto decoded = pixelSize->asFloat32();
    REQUIRE(decoded.has_value());
    CHECK(std::bit_cast<std::uint32_t>(*decoded) == std::bit_cast<std::uint32_t>(13.5F));
}

TEST_CASE("An unpopulated options member is written as an empty object, not null", "evidence-unit") {
    BakeReport report = validReport();
    report.options = json::Value::null();

    const auto text = report.write();
    REQUIRE(text.has_value());
    CHECK(text->find("\"options\": {}") != std::string::npos);

    // A reader therefore never has to special-case a missing or null options member.
    const auto reparsed = BakeReport::parse(*text);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->options.kind() == json::Value::Kind::Object);
    CHECK(reparsed->options.members().empty());
}

TEST_CASE("A report contains no decimal float text anywhere", "evidence-unit") {
    // Every string in this report is deliberately dot-free - no file extensions, no dotted
    // version - so that "the output contains no '.' at all" is a meaningful assertion about
    // number formatting rather than one that trips over "package.json" and "0.2.0".
    BakeReport report{
        .schemaVersion = kSchemaVersion,
        .tool = "mdux-fontbake",
        .toolVersion = "020",
        .recipe = {.path = "recipes/font/roboto-ui", .sha256 = digestOf("recipe")},
        .inputs = {},
        .options = json::Value::emptyObject(),
        .outputs = {{.path = "package", .sha256 = digestOf("package")}},
    };

    json::Value options = json::Value::emptyObject();
    // 0.1F is the classic value whose decimal rendering differs between implementations - and
    // the reason ADR-007 encodes bit patterns rather than trusting any formatter.
    REQUIRE(options.set("scale", json::Value::float32(0.1F)).has_value());
    REQUIRE(options.set("epsilon", json::Value::float32(std::numeric_limits<float>::epsilon()))
                .has_value());
    report.options = std::move(options);

    const auto text = report.write();
    REQUIRE(text.has_value());
    CHECK(text->find("\"bits\": 1036831949") != std::string::npos);  // 0.1F
    CHECK(text->find('.') == std::string::npos);
    // Nor any of the letters a decimal float rendering would need for an exponent form.
    CHECK(text->find('e') != std::string::npos);  // present in words like "recipe", so...
    CHECK(text->find("e+") == std::string::npos);
    CHECK(text->find("e-") == std::string::npos);
}

// ---------------------------------------------------------------------------
// validate()
// ---------------------------------------------------------------------------

TEST_CASE("A well-formed report validates", "evidence-unit") {
    CHECK(validReport().validate().has_value());
}

TEST_CASE("validate() requires tool identity fields", "evidence-unit") {
    BakeReport missingTool = validReport();
    missingTool.tool.clear();
    expectInvalid(missingTool, ReportError::EmptyToolName, "empty tool name");

    BakeReport missingVersion = validReport();
    missingVersion.toolVersion.clear();
    expectInvalid(missingVersion, ReportError::EmptyToolVersion, "empty tool version");
}

TEST_CASE("validate() rejects paths that would break byte-identity", "evidence-unit") {
    // Each of these makes the committed artifact depend on the machine or OS that produced it,
    // which is precisely what a byte-identity guarantee cannot tolerate.
    struct Case {
        std::string path;
        ReportError expected;
        std::string_view what;
    };
    const std::vector<Case> cases{
        {"", ReportError::EmptyPath, "empty path"},
        {"/abs/package.json", ReportError::AbsolutePath, "POSIX absolute path"},
        {"C:/build/package.json", ReportError::AbsolutePath, "Windows drive-letter path"},
        {"generated\\font\\package.json", ReportError::BackslashInPath, "backslash separator"},
        {"../outside/package.json", ReportError::ParentDirectoryInPath, "leading '..'"},
        {"generated/../package.json", ReportError::ParentDirectoryInPath, "interior '..'"},
    };

    for (const Case& testCase : cases) {
        BakeReport badOutput = validReport();
        badOutput.outputs[0].path = testCase.path;
        expectInvalid(badOutput, testCase.expected, std::string{testCase.what} + " in an output");

        BakeReport badInput = validReport();
        badInput.inputs[0].path = testCase.path;
        expectInvalid(badInput, testCase.expected, std::string{testCase.what} + " in an input");

        BakeReport badRecipe = validReport();
        badRecipe.recipe.path = testCase.path;
        expectInvalid(badRecipe, testCase.expected, std::string{testCase.what} + " in the recipe");
    }
}

TEST_CASE("validate() accepts a path that merely contains dots", "evidence-unit") {
    // Guards against the '..' check being written as a substring search, which would reject
    // ordinary filenames like these.
    for (const std::string_view path : {"generated/font/atlas..bin", "a/b..c/d.bin",
                                        "generated/font/roboto-ui.package.json"}) {
        BakeReport report = validReport();
        report.outputs[0].path = std::string{path};
        CHECK_MESSAGE(report.validate().has_value(),
                      std::string{"path '"} + std::string{path} + "' should be accepted");
    }
}

TEST_CASE("validate() requires at least one output", "evidence-unit") {
    BakeReport report = validReport();
    report.outputs.clear();
    expectInvalid(report, ReportError::NoOutputs, "no outputs");
}

TEST_CASE("validate() rejects a duplicated output path", "evidence-unit") {
    BakeReport report = validReport();
    report.outputs[1].path = report.outputs[0].path;
    expectInvalid(report, ReportError::DuplicateOutputPath, "duplicate output path");
}

TEST_CASE("validate() rejects an unsupported schema version", "evidence-unit") {
    BakeReport report = validReport();
    report.schemaVersion = kSchemaVersion + 1;
    expectInvalid(report, ReportError::UnsupportedSchemaVersion, "future schema version");
}

TEST_CASE("An empty inputs list is permitted", "evidence-unit") {
    // A baker whose only input is its recipe is legitimate - a generated palette, say - so this
    // must not be conflated with the empty-outputs case, which is not.
    BakeReport report = validReport();
    report.inputs.clear();
    CHECK(report.validate().has_value());

    const auto text = report.write();
    REQUIRE(text.has_value());
    CHECK(text->find("\"inputs\": []") != std::string::npos);
    CHECK(BakeReport::parse(*text).has_value());
}

// ---------------------------------------------------------------------------
// parse() strictness
// ---------------------------------------------------------------------------

TEST_CASE("parse() rejects a malformed or incomplete report", "evidence-unit") {
    const auto text = validReport().write();
    REQUIRE(text.has_value());

    // Removing any required member must fail rather than yielding a partly-populated report.
    for (const std::string_view member :
         {"\"tool\"", "\"toolVersion\"", "\"recipe\"", "\"inputs\"",
          "\"options\"", "\"outputs\"", "\"schemaVersion\""}) {
        const std::size_t position = text->find(member);
        REQUIRE(position != std::string::npos);
        // Rename the key rather than excising it, which keeps the JSON well-formed so the
        // failure comes from the report schema and not from the JSON parser.
        std::string mutated = *text;
        mutated.replace(position + 1, 3, "zzz");
        CHECK_MESSAGE(!BakeReport::parse(mutated).has_value(),
                      std::string{"a report missing "} + std::string{member} +
                          " should not parse");
    }

    CHECK(!BakeReport::parse("").has_value());
    CHECK(!BakeReport::parse("[]").has_value());
    CHECK(!BakeReport::parse("null").has_value());
    CHECK(!BakeReport::parse("{}").has_value());
    // Not valid JSON at all - a fraction, which the canonical reader rejects outright.
    CHECK(!BakeReport::parse("{\"schemaVersion\": 1.0}").has_value());
}

TEST_CASE("parse() reports an unsupported schema version distinctly", "evidence-unit") {
    BakeReport report = validReport();
    report.schemaVersion = kSchemaVersion;
    const auto text = report.write();
    REQUIRE(text.has_value());

    std::string mutated = *text;
    const std::size_t position = mutated.find("\"schemaVersion\": 1");
    REQUIRE(position != std::string::npos);
    mutated.replace(position, std::string_view{"\"schemaVersion\": 1"}.size(),
                    "\"schemaVersion\": 2");

    const auto parsed = BakeReport::parse(mutated);
    CHECK(!parsed.has_value());
    CHECK(parsed.error() == ReportError::UnsupportedSchemaVersion);
}

TEST_CASE("parse() validates what it parsed", "evidence-unit") {
    // A syntactically fine report whose content violates an invariant must still be rejected -
    // otherwise a hand-edited generated/ file could smuggle an absolute path past the reader.
    const auto text = validReport().write();
    REQUIRE(text.has_value());

    std::string mutated = *text;
    const std::size_t position = mutated.find("\"package.json\"");
    REQUIRE(position != std::string::npos);
    mutated.replace(position, std::string_view{"\"package.json\""}.size(), "\"/tmp/package.json\"");

    const auto parsed = BakeReport::parse(mutated);
    CHECK(!parsed.has_value());
    CHECK(parsed.error() == ReportError::AbsolutePath);
}

// ---------------------------------------------------------------------------
// digestFromHex()
// ---------------------------------------------------------------------------

TEST_CASE("digestFromHex round-trips with toHex", "evidence-unit") {
    Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    const auto decoded = digestFromHex(hexOf(digest));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == digest);

    const auto empty = digestFromHex(hexOf(Digest{}));
    REQUIRE(empty.has_value());
    CHECK(*empty == Digest{});
}

TEST_CASE("digestFromHex rejects anything toHex would not have produced", "evidence-unit") {
    const std::string valid = hexOf(digestOf("x"));

    CHECK(!digestFromHex("").has_value());
    CHECK(!digestFromHex(valid.substr(0, 63)).has_value());   // too short
    CHECK(!digestFromHex(valid + "0").has_value());           // too long
    CHECK(!digestFromHex(std::string(64, 'g')).has_value());  // not a hex digit
    CHECK(!digestFromHex(std::string(64, ' ')).has_value());

    // Uppercase is rejected rather than accepted: toHex() emits lowercase, so uppercase means
    // the file came from somewhere else, and accepting it would give two spellings of one digest.
    std::string uppercase = valid;
    std::ranges::transform(uppercase, uppercase.begin(),
                            [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    CHECK(!digestFromHex(uppercase).has_value());
}

// ---------------------------------------------------------------------------
// PackageHeader
// ---------------------------------------------------------------------------

TEST_CASE("PackageHeader writes its members at the top level of a package object", "evidence-unit") {
    const PackageHeader header{
        .schemaVersion = kSchemaVersion, .id = "roboto-ui", .kind = "font"};
    REQUIRE(header.validate().has_value());

    json::Value package = json::Value::emptyObject();
    REQUIRE(header.writeInto(package).has_value());
    // A baker adds its own members alongside the header's rather than nesting them.
    REQUIRE(package.set("glyphCount", json::Value::unsignedInteger(96)).has_value());

    const auto text = json::write(package);
    REQUIRE(text.has_value());
    CHECK(*text ==
          "{\n"
          "  \"glyphCount\": 96,\n"
          "  \"id\": \"roboto-ui\",\n"
          "  \"kind\": \"font\",\n"
          "  \"schemaVersion\": 1\n"
          "}\n");

    const auto reread = PackageHeader::readFrom(package);
    REQUIRE(reread.has_value());
    CHECK(reread->id == "roboto-ui");
    CHECK(reread->kind == "font");
    CHECK(reread->schemaVersion == kSchemaVersion);
}

TEST_CASE("PackageHeader validate() requires an id and a kind", "evidence-unit") {
    PackageHeader noId{.schemaVersion = kSchemaVersion, .id = "", .kind = "font"};
    CHECK(!noId.validate().has_value());
    CHECK(noId.validate().error() == ReportError::EmptyId);

    PackageHeader noKind{.schemaVersion = kSchemaVersion, .id = "roboto-ui", .kind = ""};
    CHECK(!noKind.validate().has_value());
    CHECK(noKind.validate().error() == ReportError::EmptyKind);

    PackageHeader wrongVersion{
        .schemaVersion = kSchemaVersion + 1, .id = "roboto-ui", .kind = "font"};
    CHECK(!wrongVersion.validate().has_value());
    CHECK(wrongVersion.validate().error() == ReportError::UnsupportedSchemaVersion);
}

TEST_CASE("PackageHeader readFrom rejects a malformed object", "evidence-unit") {
    const auto missingKind = json::parse("{\"schemaVersion\": 1, \"id\": \"x\"}");
    REQUIRE(missingKind.has_value());
    CHECK(!PackageHeader::readFrom(*missingKind).has_value());

    const auto futureVersion =
        json::parse("{\"schemaVersion\": 99, \"id\": \"x\", \"kind\": \"font\"}");
    REQUIRE(futureVersion.has_value());
    const auto result = PackageHeader::readFrom(*futureVersion);
    CHECK(!result.has_value());
    CHECK(result.error() == ReportError::UnsupportedSchemaVersion);

    const auto wrongType =
        json::parse("{\"schemaVersion\": 1, \"id\": 5, \"kind\": \"font\"}");
    REQUIRE(wrongType.has_value());
    CHECK(!PackageHeader::readFrom(*wrongType).has_value());
}

TEST_CASE("describe() names every report error", "evidence-unit") {
    constexpr std::array<ReportError, 12> all{
        ReportError::EmptyToolName,         ReportError::EmptyToolVersion,
        ReportError::EmptyPath,             ReportError::AbsolutePath,
        ReportError::BackslashInPath,       ReportError::ParentDirectoryInPath,
        ReportError::EmptyId,               ReportError::EmptyKind,
        ReportError::NoOutputs,             ReportError::DuplicateOutputPath,
        ReportError::UnsupportedSchemaVersion, ReportError::MalformedReport};

    for (const ReportError error : all) {
        CHECK(!describe(error).empty());
        CHECK(describe(error) != "unrecognized report error");
    }
}
