/**
 * @file SchemaSubsetTests.cpp
 * @brief BDD scenarios for `mdux.tools.schema`, the JSON Schema subset validator (#314).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON)
 *
 * A port of `Compliatory/MedUI`'s `tools/schema_check.py`. These scenarios pin the two properties
 * that port must keep: it enforces the keywords the contract schemas use (`required`,
 * `additionalProperties`, `pattern`, `enum`, `const`, `$ref`/`$defs`, `anyOf`, array bounds), and
 * it **fails closed** on a keyword it does not implement rather than passing a document that
 * keyword would have rejected. The pinned `consumer-manifest`/`evidence` schema documents are run
 * against it by `conformance_spec`; this file exercises the engine directly.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.tools.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace json   = mdux::evidence::json;
namespace schema = mdux::tools::schema;

[[nodiscard]] json::Value parse(std::string_view text) {
    auto value = json::parse(text);
    if (!value) {
        throw speclab::core::AssertionFailure(std::format("test JSON did not parse: {}", text), std::source_location::current());
    }
    return std::move(*value);
}

const mdux::spec::Register requiredAndAdditionalProperties{
    "required keys and additionalProperties:false are both enforced",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-object-shape")
            .Given("a schema requiring `a` and forbidding extra keys", [] {})
            .When("documents that miss `a` or carry `c` are validated", [] {})
            .Then("each is reported, and the exact document passes", [] {
                mdux::spec::Checks checks;
                const json::Value shape = parse(R"({"type":"object","additionalProperties":false,
                    "required":["a"],"properties":{"a":{"type":"string"},"b":{"type":"integer"}}})");

                checks.expect(schema::validate(parse(R"({"a":"x","b":1})"), shape).empty(), "the exact document is valid");
                checks.expect(!schema::validate(parse(R"({"b":1})"), shape).empty(), "a missing required key is a violation");
                checks.expect(!schema::validate(parse(R"({"a":"x","c":true})"), shape).empty(), "an undeclared key is a violation");
                checks.expect(!schema::validate(parse(R"({"a":1})"), shape).empty(), "a wrong-typed property is a violation");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register patternEnumConst{
    "pattern, enum and const compare as the contract expects",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-scalars")
            .Given("a schema pinning a 40-hex string, an enum and a const 1", [] {})
            .When("values on and off each constraint are validated", [] {})
            .Then("the anchored pattern, the enum membership and the integer const all hold", [] {
                mdux::spec::Checks checks;
                const json::Value hex   = parse(R"({"type":"string","pattern":"^[0-9a-f]{40}$"})");
                const json::Value pick  = parse(R"({"enum":["a","b"]})");
                const json::Value fixed = parse(R"({"const":1})");

                checks.expect(schema::validate(parse("\"" + std::string(40, 'a') + "\""), hex).empty(), "40 lowercase hex passes");
                checks.expect(!schema::validate(parse("\"" + std::string(40, 'A') + "\""), hex).empty(), "uppercase fails the anchored class");
                checks.expect(!schema::validate(parse("\"" + std::string(41, 'a') + "\""), hex).empty(), "41 chars fails the anchored length");
                checks.expect(schema::validate(parse(R"("b")"), pick).empty(), "an enum member passes");
                checks.expect(!schema::validate(parse(R"("z")"), pick).empty(), "a non-member fails");
                checks.expect(schema::validate(parse("1"), fixed).empty(), "the const integer passes");
                checks.expect(!schema::validate(parse("2"), fixed).empty(), "a different integer fails");
                checks.expect(!schema::validate(parse("true"), fixed).empty(), "a boolean is not the integer 1");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register refsAndAnyOf{
    "$ref resolves local pointers and anyOf accepts either branch",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-refs")
            .Given("a schema whose items $ref a $defs entry, and a nullable-string anyOf", [] {})
            .When("arrays of the referenced shape and null/string values are validated", [] {})
            .Then("the reference is followed and either anyOf branch is accepted", [] {
                mdux::spec::Checks checks;
                const json::Value shape = parse(R"({
                    "type":"object","additionalProperties":false,"required":["ids","locale"],
                    "properties":{
                        "ids":{"type":"array","items":{"$ref":"#/$defs/id"}},
                        "locale":{"anyOf":[{"type":"string","minLength":1},{"type":"null"}]}},
                    "$defs":{"id":{"type":"string","pattern":"^[a-z]+$"}}})");

                checks.expect(schema::validate(parse(R"({"ids":["ab","cd"],"locale":null})"), shape).empty(), "valid, null locale");
                checks.expect(schema::validate(parse(R"({"ids":[],"locale":"fr-fr"})"), shape).empty(), "valid, string locale");
                checks.expect(!schema::validate(parse(R"({"ids":["AB"],"locale":null})"), shape).empty(), "the $ref'd pattern is enforced");
                checks.expect(!schema::validate(parse(R"({"ids":[],"locale":5})"), shape).empty(), "an integer matches neither anyOf branch");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register failsClosedOnAnUnknownKeyword{
    "A schema keyword this subset does not implement fails closed",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-fail-closed")
            .Given("a schema using `multipleOf`, which this port does not implement", [] {})
            .When("checkSchema and validate see it", [] {})
            .Then("checkSchema names it and validate refuses the document rather than passing it", [] {
                mdux::spec::Checks checks;
                const json::Value shape = parse(R"({"type":"integer","multipleOf":3})");

                const std::vector<std::string> schemaProblems = schema::checkSchema(shape);
                checks.expect(!schemaProblems.empty(), "checkSchema reports the unsupported keyword");

                const std::vector<std::string> docProblems = schema::validate(parse("9"), shape);
                checks.expect(!docProblems.empty() && docProblems.front().starts_with("schema: "),
                              "validate returns the schema problem rather than silently accepting 9");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register numbersCompareByValue{
    "const and enum compare numbers by value, and a boolean is not a number",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-numbers")
            .Given("const 1, an integer enum, and a large unsigned value", [] {})
            .When("integer and unsigned forms are validated (MduX JSON has no float literal)", [] {})
            .Then("equal values match, a boolean never does, and the whole uint64 range is exact", [] {
                mdux::spec::Checks checks;
                const json::Value one   = parse(R"({"const":1})");
                const json::Value picks = parse(R"({"enum":[0,5,42]})");

                checks.expect(schema::validate(parse("1"), one).empty(), "the integer 1 matches const 1");
                checks.expect(!schema::validate(parse("true"), one).empty(), "the boolean true does not match const 1");
                checks.expect(!schema::validate(parse("2"), one).empty(), "2 does not match const 1");
                checks.expect(schema::validate(parse("5"), picks).empty(), "5 is in the integer enum");
                checks.expect(!schema::validate(parse("6"), picks).empty(), "6 is not");

                // The whole std::uint64_t range compares exactly, past INT64_MAX.
                const json::Value huge = parse(R"({"const":18446744073709551615})");
                checks.expect(schema::validate(parse("18446744073709551615"), huge).empty(), "UINT64_MAX matches itself");
                checks.expect(!schema::validate(parse("18446744073709551614"), huge).empty(), "a neighbour does not");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register unsupportedTypeAndAdditionalProperties{
    "An unknown `type` string or a non-boolean additionalProperties fails closed",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-type-guard")
            .Given("schemas using `type: number` and a subschema `additionalProperties`", [] {})
            .When("checkSchema sees them", [] {})
            .Then("both are reported rather than silently rejecting or admitting documents", [] {
                mdux::spec::Checks checks;
                checks.expect(!schema::checkSchema(parse(R"({"type":"number"})")).empty(),
                              "`type: number` is not in this subset");
                checks.expect(!schema::checkSchema(parse(R"({"type":"object","additionalProperties":{"type":"string"}})")).empty(),
                              "a subschema additionalProperties is not supported");
                checks.expect(schema::checkSchema(parse(R"({"type":"integer"})")).empty(), "`type: integer` is fine");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register arrayBoundsAndUniqueness{
    "minItems, maxItems and uniqueItems are enforced",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-subset-arrays")
            .Given("a schema requiring 1 to 3 unique items", [] {})
            .When("empty, oversized and duplicate arrays are validated", [] {})
            .Then("each bound is a violation and a valid array passes", [] {
                mdux::spec::Checks checks;
                const json::Value shape = parse(R"({"type":"array","minItems":1,"maxItems":3,"uniqueItems":true,"items":{"type":"integer"}})");

                checks.expect(schema::validate(parse("[1,2,3]"), shape).empty(), "three distinct items pass");
                checks.expect(!schema::validate(parse("[]"), shape).empty(), "empty fails minItems");
                checks.expect(!schema::validate(parse("[1,2,3,4]"), shape).empty(), "four fails maxItems");
                checks.expect(!schema::validate(parse("[1,1]"), shape).empty(), "a duplicate fails uniqueItems");
                checks.raise();
            })
            .Execute();
    }};

}  // namespace
