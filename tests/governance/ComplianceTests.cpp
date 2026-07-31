/**
 * @file ComplianceTests.cpp
 * @brief Tests for the two release-evidence exports in mdux.governance.compliance.
 *
 * @compliance ADR-007 (indirectly, via mdux.evidence.json)
 *
 * The central property under test is issue #35's own acceptance criterion: a program built with a
 * deliberate gap - an unverified Requirement, a Hazard with an empty controlledBy - must fail
 * `validate()` *and* `traceabilityMatrix()` must still name the specific gap rather than omit it.
 *
 * The invariants of `ComplianceProgram::validate()` itself are covered in
 * ComplianceProgramTests.cpp, which owns the model; this file tests only the exports over it.
 * That split is deliberate: when these two files each had their own `ComplianceProgram`, the two
 * definitions drifted apart in exactly the way one shared model prevents.
 */

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.governance;
import mdux.governance.compliance;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::governance;
namespace json = mdux::evidence::json;
namespace evidence = mdux::evidence;

namespace {

[[nodiscard]] Requirement requirement(std::string id, std::string title) {
    return Requirement{
        .id = std::move(id),
        .title = std::move(title),
        .sourceClause = "IEC 62304:2006 §5.2 Software requirements analysis",
        .verificationIntent = "Covered by a unit test asserting the documented behaviour.",
    };
}

[[nodiscard]] VerificationCase verificationCase(std::string id, std::string requirementId,
                                                std::vector<std::string> refs, bool passed) {
    return VerificationCase{
        .id = std::move(id),
        .requirementId = std::move(requirementId),
        .method = VerificationMethod::Test,
        .evidenceRefs = std::move(refs),
        .passed = passed,
    };
}

/// A program that passes validate(), so each test can break exactly one thing.
[[nodiscard]] ComplianceProgram validProgram() {
    return ComplianceProgram{
        .safetyClass = SafetyClass::B,
        .requirements =
            {
                requirement("REQ-001", "The device shall do X."),
                requirement("REQ-002", "The device shall do Y."),
            },
        .hazards =
            {
                Hazard{.id = "HAZ-001",
                       .description = "Z could happen.",
                       .controlledBy = {"REQ-001"}},
            },
        .verificationCases =
            {
                verificationCase("VER-001", "REQ-001", {"tests/x_test.cpp"}, true),
                verificationCase("VER-002", "REQ-002", {"tests/y_test.cpp"}, true),
            },
        .problemReports =
            {
                ProblemReport{.id = "PRB-001",
                              .description = "Minor cosmetic issue.",
                              .closed = true,
                              .affectsRisk = false},
            },
    };
}

[[nodiscard]] const json::Value* member(const json::Value& object, std::string_view key) {
    return object.find(key);
}

[[nodiscard]] std::string stringAt(const json::Value& object, std::string_view key) {
    const json::Value* found = object.find(key);
    if (found == nullptr) {
        return {};
    }
    const auto text = found->asString();
    return text.has_value() ? std::string{*text} : std::string{};
}

}  // namespace

// ---------------------------------------------------------------------------
// traceabilityMatrix()
// ---------------------------------------------------------------------------

TEST_CASE("traceabilityMatrix() lists every requirement with its cases and evidence",
          "evidence-unit") {
    const auto matrix = traceabilityMatrix(validProgram());
    REQUIRE(matrix.has_value());
    REQUIRE(matrix->kind() == json::Value::Kind::Array);

    const auto rows = matrix->elements();
    REQUIRE(rows.size() == 2);
    CHECK(stringAt(rows[0], "requirement_id") == "REQ-001");
    CHECK(stringAt(rows[0], "title") == "The device shall do X.");

    const json::Value* cases = member(rows[0], "verification_cases");
    REQUIRE(cases != nullptr);
    REQUIRE(cases->elements().size() == 1);
    CHECK(stringAt(cases->elements()[0], "id") == "VER-001");
    CHECK(stringAt(cases->elements()[0], "method") == "test");

    const json::Value* refs = member(cases->elements()[0], "evidence_refs");
    REQUIRE(refs != nullptr);
    REQUIRE(refs->elements().size() == 1);
    CHECK(refs->elements()[0].asString().value_or("") == "tests/x_test.cpp");
}

TEST_CASE("Every matrix row names the clause its requirement came from", "evidence-unit") {
    // The property that makes this a regulatory traceability matrix rather than a coverage
    // report, and the one the pre-reconciliation export could not provide at all: its local
    // Requirement type had no clause field.
    const auto matrix = traceabilityMatrix(validProgram());
    REQUIRE(matrix.has_value());
    for (const json::Value& row : matrix->elements()) {
        CHECK(stringAt(row, "source_clause") ==
              "IEC 62304:2006 §5.2 Software requirements analysis");
        CHECK(!stringAt(row, "verification_intent").empty());
    }
}

TEST_CASE("traceabilityMatrix() names a coverage gap as an empty row, not an omission",
          "evidence-unit") {
    // Issue #35's acceptance criterion, first half.
    ComplianceProgram program = validProgram();
    program.verificationCases.erase(program.verificationCases.begin() + 1);  // REQ-002 uncovered

    const auto matrix = traceabilityMatrix(program);
    REQUIRE(matrix.has_value());
    const auto rows = matrix->elements();
    REQUIRE_MESSAGE(rows.size() == 2, "the uncovered requirement must still have a row");

    const json::Value* cases = member(rows[1], "verification_cases");
    REQUIRE(cases != nullptr);
    CHECK(stringAt(rows[1], "requirement_id") == "REQ-002");
    CHECK_MESSAGE(cases->elements().empty(),
                  "an uncovered requirement's case list must be present and empty");
}

TEST_CASE("A gap fails validate() while the matrix still reports it", "evidence-unit") {
    // Issue #35's acceptance criterion in full: the gate and the export read the same data and
    // deliberately disagree about what to do with a gap.
    ComplianceProgram program = validProgram();
    program.verificationCases.clear();        // both requirements uncovered
    program.hazards[0].controlledBy.clear();  // and an uncontrolled hazard

    const auto validation = program.validate();
    REQUIRE_MESSAGE(!validation.has_value(), "the gate must fail");
    bool sawUnverified = false;
    bool sawUncontrolled = false;
    for (const ValidationFailure& failure : validation.error()) {
        sawUnverified = sawUnverified || failure.code == GovernanceError::UnverifiedRequirement;
        sawUncontrolled = sawUncontrolled || failure.code == GovernanceError::EmptyControls;
    }
    CHECK(sawUnverified);
    CHECK(sawUncontrolled);

    const auto matrix = traceabilityMatrix(program);
    REQUIRE_MESSAGE(matrix.has_value(), "the export must not gate on validate()");
    const auto rows = matrix->elements();
    REQUIRE(rows.size() == 2);
    for (const json::Value& row : rows) {
        const json::Value* cases = member(row, "verification_cases");
        REQUIRE(cases != nullptr);
        CHECK(cases->elements().empty());
    }
}

TEST_CASE("traceabilityMatrix() sorts rows and cases for a byte-stable export", "evidence-unit") {
    ComplianceProgram program = validProgram();
    std::ranges::reverse(program.requirements);
    program.verificationCases.push_back(
        verificationCase("VER-000", "REQ-001", {"tests/earlier_test.cpp"}, true));
    std::ranges::reverse(program.verificationCases);

    const auto matrix = traceabilityMatrix(program);
    REQUIRE(matrix.has_value());
    const auto rows = matrix->elements();
    REQUIRE(rows.size() == 2);
    CHECK(stringAt(rows[0], "requirement_id") == "REQ-001");
    CHECK(stringAt(rows[1], "requirement_id") == "REQ-002");

    const json::Value* cases = member(rows[0], "verification_cases");
    REQUIRE(cases != nullptr);
    REQUIRE(cases->elements().size() == 2);
    CHECK(stringAt(cases->elements()[0], "id") == "VER-000");
    CHECK(stringAt(cases->elements()[1], "id") == "VER-001");
}

TEST_CASE("Assembly order cannot change the exported bytes", "evidence-unit") {
    // The property CI would depend on if this export were committed as evidence.
    ComplianceProgram shuffled = validProgram();
    std::ranges::reverse(shuffled.requirements);
    std::ranges::reverse(shuffled.verificationCases);

    const auto a = traceabilityMatrix(validProgram());
    const auto b = traceabilityMatrix(shuffled);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const auto textA = json::write(*a);
    const auto textB = json::write(*b);
    REQUIRE(textA.has_value());
    REQUIRE(textB.has_value());
    CHECK(*textA == *textB);
}

TEST_CASE("Matrix member names match the schemas the records are validated against",
          "evidence-unit") {
    // docs/iec62304/schemas/verification-case.schema.json declares `id`, not `case_id`. The
    // pre-reconciliation export emitted the latter, so a row and the record it came from could
    // not be read side by side.
    const auto matrix = traceabilityMatrix(validProgram());
    REQUIRE(matrix.has_value());
    const json::Value* cases = member(matrix->elements()[0], "verification_cases");
    REQUIRE(cases != nullptr);
    const json::Value& firstCase = cases->elements()[0];
    CHECK(member(firstCase, "id") != nullptr);
    CHECK(member(firstCase, "case_id") == nullptr);
    CHECK(member(firstCase, "passed") != nullptr);
    // requirement_id is the row's key; repeating it inside each case would let the two disagree.
    CHECK(member(firstCase, "requirement_id") == nullptr);
}

// ---------------------------------------------------------------------------
// releaseEvidenceSummary()
// ---------------------------------------------------------------------------

TEST_CASE("releaseEvidenceSummary() reports a clean program as passing", "evidence-unit") {
    const auto summary = releaseEvidenceSummary(validProgram(), {});
    REQUIRE(summary.has_value());
    CHECK(member(*summary, "validation_passed")->asBool().value_or(false));
    CHECK(member(*summary, "requirements_total")->asUInt().value_or(0) == 2);
    CHECK(member(*summary, "requirements_verified")->asUInt().value_or(0) == 2);
    CHECK(stringAt(*summary, "safety_class") == "B");
    CHECK(member(*summary, "validation_failures")->elements().empty());
    CHECK(member(*summary, "open_problem_reports")->elements().empty());
    CHECK(member(*summary, "generated_artifacts")->elements().empty());
}

TEST_CASE("releaseEvidenceSummary() fails on an unverified requirement or a failed case",
          "evidence-unit") {
    ComplianceProgram uncovered = validProgram();
    uncovered.verificationCases.pop_back();
    const auto uncoveredSummary = releaseEvidenceSummary(uncovered, {});
    REQUIRE(uncoveredSummary.has_value());
    CHECK(!member(*uncoveredSummary, "validation_passed")->asBool().value_or(true));
    CHECK(member(*uncoveredSummary, "requirements_verified")->asUInt().value_or(0) == 1);

    // A case that exists but did not pass is a different failure, and must not read as coverage.
    ComplianceProgram failed = validProgram();
    failed.verificationCases[0].passed = false;
    const auto failedSummary = releaseEvidenceSummary(failed, {});
    REQUIRE(failedSummary.has_value());
    CHECK(!member(*failedSummary, "validation_passed")->asBool().value_or(true));
    CHECK_MESSAGE(member(*failedSummary, "requirements_verified")->asUInt().value_or(0) == 2,
                  "coverage is still complete - it is the outcome that failed");
}

TEST_CASE("The summary carries every reason the gate failed, not just that it did",
          "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.hazards[0].controlledBy = {"REQ-DOES-NOT-EXIST"};

    const auto summary = releaseEvidenceSummary(program, {});
    REQUIRE(summary.has_value());
    CHECK(!member(*summary, "validation_passed")->asBool().value_or(true));

    const json::Value* failures = member(*summary, "validation_failures");
    REQUIRE(failures != nullptr);
    REQUIRE(failures->elements().size() == 1);
    CHECK(stringAt(failures->elements()[0], "subject") == "HAZ-001");
    CHECK(stringAt(failures->elements()[0], "detail") == "REQ-DOES-NOT-EXIST");
    CHECK(!stringAt(failures->elements()[0], "code").empty());
}

TEST_CASE("releaseEvidenceSummary() lists only open problem reports, sorted by id",
          "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.problemReports.push_back(ProblemReport{
        .id = "PRB-003", .description = "Still open.", .closed = false, .affectsRisk = true});
    program.problemReports.push_back(ProblemReport{
        .id = "PRB-002", .description = "Also open.", .closed = false, .affectsRisk = false});

    const auto summary = releaseEvidenceSummary(program, {});
    REQUIRE(summary.has_value());
    const json::Value* open = member(*summary, "open_problem_reports");
    REQUIRE(open != nullptr);
    REQUIRE(open->elements().size() == 2);
    CHECK(stringAt(open->elements()[0], "id") == "PRB-002");
    CHECK(stringAt(open->elements()[1], "id") == "PRB-003");
    // §9.4: an open problem that could affect safety is a different release blocker from one
    // that could not, so the flag travels with the record.
    CHECK(!open->elements()[0].find("affects_risk")->asBool().value_or(true));
    CHECK(open->elements()[1].find("affects_risk")->asBool().value_or(false));
}

TEST_CASE("releaseEvidenceSummary() lists supplied artifact digests, sorted by path",
          "evidence-unit") {
    const std::array<char, 1> a{'a'};
    const std::array<char, 1> b{'b'};
    const std::array<evidence::FileRecord, 2> artifacts{
        evidence::FileRecord{.path = "generated/shader/ui.spv",
                             .sha256 = evidence::sha256(std::as_bytes(std::span{b}))},
        evidence::FileRecord{.path = "generated/font/roboto/atlas.bin",
                             .sha256 = evidence::sha256(std::as_bytes(std::span{a}))},
    };

    const auto summary = releaseEvidenceSummary(validProgram(), artifacts);
    REQUIRE(summary.has_value());
    const json::Value* listed = member(*summary, "generated_artifacts");
    REQUIRE(listed != nullptr);
    REQUIRE(listed->elements().size() == 2);
    CHECK(stringAt(listed->elements()[0], "path") == "generated/font/roboto/atlas.bin");
    CHECK(stringAt(listed->elements()[1], "path") == "generated/shader/ui.spv");
    CHECK(stringAt(listed->elements()[0], "sha256").size() == 64);
}

TEST_CASE("Artifacts are a parameter, so they never enter the program's round-trip",
          "evidence-unit") {
    // A digest of a built file is evidence about a build, not an authored governance record.
    // Keeping it out of ComplianceProgram preserves parse()'s strict member set.
    const ComplianceProgram program = validProgram();
    const auto text = program.write();
    REQUIRE(text.has_value());
    CHECK(text->find("generated_artifacts") == std::string::npos);
    CHECK(ComplianceProgram::parse(*text).has_value());
}
