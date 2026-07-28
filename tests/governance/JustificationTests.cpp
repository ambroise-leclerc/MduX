/**
 * @file JustificationTests.cpp
 * @brief Tests for the governed-zone mdux.governance module.
 *
 * @compliance ADR-007 (indirectly, via mdux.evidence.json)
 *
 * Every constraint here has a sibling test in tools/docs-lint/test_mdux_docs_lint.py, checking
 * the same rule against the JSON Schema and against the fenced-```json-block heuristic
 * respectively. All three should agree on what a valid Justification looks like.
 */

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.governance;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::governance;

namespace {

/// A Justification that passes validate(), so each test can invalidate exactly one thing.
[[nodiscard]] Justification validJustification() {
    return Justification{
        .justificationId = "JUS-001",
        .standard = "IEC 62304:2006",
        .clauseRef = "IEC 62304:2006 §5.3 Software architectural design",
        .rationale = "MduXTrustZones.cmake mechanically enforces the architectural decision "
                     "recorded in ADR-004.",
        .requirementId = std::nullopt,
        .evidenceRefs = {"cmake/MduXTrustZones.cmake", "docs/adr/ADR-004-trust-zones-in-cpp.md"},
    };
}

void expectInvalid(const Justification& justification, GovernanceError expected,
                   std::string_view what) {
    const auto result = justification.validate();
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
// isApprovedStandard()
// ---------------------------------------------------------------------------

TEST_CASE("isApprovedStandard accepts exactly the five approved standards", "evidence-unit") {
    for (const std::string_view standard :
         {"IEC 62304:2006", "ISO 13485:2016", "ISO 14971:2019", "IEC 62366-1:2015",
          "IEC 81001-5-1:2021"}) {
        CHECK_MESSAGE(isApprovedStandard(standard), std::string{standard} + " should be approved");
    }
    for (const std::string_view standard :
         {"IEC 60601-1:2005", "iec 62304:2006", "IEC 62304:2006 ", "", "IEC 62304"}) {
        CHECK_MESSAGE(!isApprovedStandard(standard),
                      std::string{"'"} + std::string{standard} + "' should not be approved");
    }
}

// ---------------------------------------------------------------------------
// validate()
// ---------------------------------------------------------------------------

TEST_CASE("A well-formed Justification validates", "evidence-unit") {
    CHECK(validJustification().validate().has_value());
}

TEST_CASE("validate() requires a well-formed justificationId", "evidence-unit") {
    Justification empty = validJustification();
    empty.justificationId.clear();
    expectInvalid(empty, GovernanceError::EmptyJustificationId, "empty justificationId");

    for (const std::string_view bad : {"JUS-1", "JUS-12", "JUS-abc", "001", "jus-001", "JUS001"}) {
        Justification malformed = validJustification();
        malformed.justificationId = std::string{bad};
        expectInvalid(malformed, GovernanceError::MalformedJustificationId,
                      std::string{"malformed justificationId '"} + std::string{bad} + "'");
    }

    // Boundary: exactly three digits is the minimum: JUS-NNN.
    Justification threeDigits = validJustification();
    threeDigits.justificationId = "JUS-999";
    CHECK(threeDigits.validate().has_value());

    // More than three digits is fine too.
    Justification fourDigits = validJustification();
    fourDigits.justificationId = "JUS-1000";
    CHECK(fourDigits.validate().has_value());
}

TEST_CASE("validate() rejects an unapproved standard", "evidence-unit") {
    Justification bad = validJustification();
    bad.standard = "IEC 60601-1:2005";
    bad.clauseRef = "IEC 60601-1:2005 §1 Scope";
    expectInvalid(bad, GovernanceError::UnapprovedStandard, "unapproved standard");
}

TEST_CASE("validate() requires a complete, well-formed clauseRef", "evidence-unit") {
    Justification empty = validJustification();
    empty.clauseRef.clear();
    expectInvalid(empty, GovernanceError::EmptyClauseRef, "empty clauseRef");

    const std::vector<std::string> malformed{
        "IEC 62304:2006 5.3 no marker",              // missing §
        "§5.3 no standard prefix",                    // missing standard
        "IEC 60601-1:2005 §5.3 unapproved standard",  // standard not in approved set
        "IEC 62304:2006 § no clause number",           // no digits after §
        "IEC 62304:2006 §5.3",                         // no title after the clause number
        "IEC 62304:2006 §.3 leading dot",              // no digit before the first dot
        "IEC 62304:2006 §5. trailing dot",             // dot with no digits following it
        "IEC 62304:2006 §5..3 double dot",             // empty group between two dots
    };
    for (const std::string& bad : malformed) {
        Justification j = validJustification();
        j.clauseRef = bad;
        expectInvalid(j, GovernanceError::MalformedClauseRef,
                      std::string{"malformed clauseRef '"} + bad + "'");
    }
}

TEST_CASE("validate() rejects a clauseRef naming a different standard than 'standard'",
          "evidence-unit") {
    // This is the exact bug an earlier, regex-only version of the JSON Schema (docs/governance/
    // schemas/justification.schema.json, issue #26) let through before a per-standard if/then
    // pair was added - the C++ type must not repeat that mistake.
    Justification mismatch = validJustification();
    mismatch.clauseRef = "ISO 13485:2016 §5.3 Design and development";
    expectInvalid(mismatch, GovernanceError::ClauseRefStandardMismatch,
                 "clauseRef/standard mismatch");
}

TEST_CASE("validate() accepts every real clause shape used in the regulatory corpus",
          "evidence-unit") {
    // Single clause, single-digit and multi-level.
    for (const std::string_view clauseRef :
         {"IEC 62304:2006 §5.3 Software architectural design",
          "ISO 14971:2019 §6 Risk evaluation",
          "IEC 81001-5-1:2021 §4 General requirements",
          "ISO 13485:2016 §7.3 Design and development"}) {
        Justification j = validJustification();
        j.standard = std::string{clauseRef.substr(0, clauseRef.find(" §"))};
        j.clauseRef = std::string{clauseRef};
        CHECK_MESSAGE(j.validate().has_value(),
                      std::string{"should accept: "} + std::string{clauseRef});
    }
}

TEST_CASE("validate() requires a non-empty rationale", "evidence-unit") {
    Justification empty = validJustification();
    empty.rationale.clear();
    expectInvalid(empty, GovernanceError::EmptyRationale, "empty rationale");
}

TEST_CASE("validate() requires evidenceRefs to be non-empty and free of duplicates",
          "evidence-unit") {
    Justification empty = validJustification();
    empty.evidenceRefs.clear();
    expectInvalid(empty, GovernanceError::EmptyEvidenceRefs, "empty evidenceRefs");

    Justification duplicate = validJustification();
    duplicate.evidenceRefs = {"a.md", "b.md", "a.md"};
    expectInvalid(duplicate, GovernanceError::DuplicateEvidenceRef, "duplicate evidenceRefs entry");

    // A single entry, or several distinct ones, is fine.
    Justification single = validJustification();
    single.evidenceRefs = {"only-one.md"};
    CHECK(single.validate().has_value());
}

TEST_CASE("requirementId is optional, but must be well-formed when present", "evidence-unit") {
    Justification withoutOne = validJustification();
    withoutOne.requirementId = std::nullopt;
    CHECK(withoutOne.validate().has_value());

    Justification withOne = validJustification();
    withOne.requirementId = "REQ-TRUSTZONE-001";
    CHECK(withOne.validate().has_value());

    Justification malformed = validJustification();
    malformed.requirementId = "not-a-requirement-id";
    CHECK(!malformed.validate().has_value());
}

// ---------------------------------------------------------------------------
// toJson() / write() / parse() round-trip
// ---------------------------------------------------------------------------

TEST_CASE("A Justification serializes to the documented canonical shape", "evidence-unit") {
    const auto text = validJustification().write();
    REQUIRE(text.has_value());
    CHECK(*text ==
          "{\n"
          "  \"clause_ref\": \"IEC 62304:2006 §5.3 Software architectural design\",\n"
          "  \"evidence_refs\": [\n"
          "    \"cmake/MduXTrustZones.cmake\",\n"
          "    \"docs/adr/ADR-004-trust-zones-in-cpp.md\"\n"
          "  ],\n"
          "  \"justification_id\": \"JUS-001\",\n"
          "  \"rationale\": \"MduXTrustZones.cmake mechanically enforces the architectural "
          "decision recorded in ADR-004.\",\n"
          "  \"standard\": \"IEC 62304:2006\"\n"
          "}\n");
}

TEST_CASE("requirement_id is present in the serialized form only when set", "evidence-unit") {
    Justification withOne = validJustification();
    withOne.requirementId = "REQ-EXAMPLE-001";
    const auto withText = withOne.write();
    REQUIRE(withText.has_value());
    CHECK(withText->find("\"requirement_id\": \"REQ-EXAMPLE-001\"") != std::string::npos);

    const auto withoutText = validJustification().write();
    REQUIRE(withoutText.has_value());
    CHECK(withoutText->find("requirement_id") == std::string::npos);
}

TEST_CASE("A Justification round-trips through write and parse", "evidence-unit") {
    Justification original = validJustification();
    original.requirementId = "REQ-EXAMPLE-001";
    const auto text = original.write();
    REQUIRE(text.has_value());

    const auto reparsed = Justification::parse(*text);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->justificationId == original.justificationId);
    CHECK(reparsed->standard == original.standard);
    CHECK(reparsed->clauseRef == original.clauseRef);
    CHECK(reparsed->rationale == original.rationale);
    CHECK(reparsed->requirementId == original.requirementId);
    CHECK(reparsed->evidenceRefs == original.evidenceRefs);

    // The property CI would actually depend on: re-serializing reproduces identical bytes.
    const auto rewritten = reparsed->write();
    REQUIRE(rewritten.has_value());
    CHECK(*rewritten == *text);
}

TEST_CASE("parse() rejects a malformed or incomplete Justification", "evidence-unit") {
    const auto text = validJustification().write();
    REQUIRE(text.has_value());

    for (const std::string_view member :
         {"\"justification_id\"", "\"standard\"", "\"clause_ref\"", "\"rationale\"",
          "\"evidence_refs\""}) {
        const std::size_t position = text->find(member);
        REQUIRE(position != std::string::npos);
        std::string mutated = *text;
        mutated.replace(position + 1, 3, "zzz");
        CHECK_MESSAGE(!Justification::parse(mutated).has_value(),
                      std::string{"a Justification missing "} + std::string{member} +
                          " should not parse");
    }

    CHECK(!Justification::parse("").has_value());
    CHECK(!Justification::parse("[]").has_value());
    CHECK(!Justification::parse("null").has_value());
    CHECK(!Justification::parse("{}").has_value());
    CHECK(!Justification::parse("not json at all").has_value());
}

TEST_CASE("parse() validates what it parsed, not just the JSON shape", "evidence-unit") {
    // Syntactically fine JSON whose content violates an invariant must still be rejected.
    const auto text = validJustification().write();
    REQUIRE(text.has_value());

    std::string mutated = *text;
    const std::size_t position = mutated.find("\"evidence_refs\": [");
    REQUIRE(position != std::string::npos);
    // Collapse evidence_refs to an empty array.
    const std::size_t closeBracket = mutated.find(']', position);
    mutated.erase(position + std::string_view{"\"evidence_refs\": ["}.size(),
                  closeBracket - position - std::string_view{"\"evidence_refs\": ["}.size());

    const auto parsed = Justification::parse(mutated);
    CHECK(!parsed.has_value());
    CHECK(parsed.error() == GovernanceError::EmptyEvidenceRefs);
}

TEST_CASE("describe() names every governance error", "evidence-unit") {
    constexpr std::array<GovernanceError, 10> all{
        GovernanceError::EmptyJustificationId,      GovernanceError::MalformedJustificationId,
        GovernanceError::UnapprovedStandard,        GovernanceError::EmptyClauseRef,
        GovernanceError::MalformedClauseRef,        GovernanceError::ClauseRefStandardMismatch,
        GovernanceError::EmptyRationale,            GovernanceError::EmptyEvidenceRefs,
        GovernanceError::DuplicateEvidenceRef,      GovernanceError::MalformedJustification};

    for (const GovernanceError error : all) {
        CHECK(!describe(error).empty());
        CHECK(describe(error) != "unrecognized governance error");
    }
}
