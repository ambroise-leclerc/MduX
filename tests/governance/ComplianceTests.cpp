/**
 * @file ComplianceTests.cpp
 * @brief Tests for the governed-zone mdux.governance.compliance module.
 *
 * @compliance ADR-007 (indirectly, via mdux.evidence.json)
 *
 * The central property under test is issue #35's own acceptance criterion: a program built with
 * a deliberate gap (an unverified Requirement, a Hazard with an empty controlledBy) must fail
 * validate() *and* traceabilityMatrix() must still name the specific gap rather than omit it.
 */

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.governance.compliance;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::governance;
namespace json = mdux::evidence::json;

namespace {

/// A program that passes validate(), so each test can invalidate exactly one thing.
[[nodiscard]] ComplianceProgram validProgram() {
    return ComplianceProgram{
        .requirements =
            {
                Requirement{.requirementId = "REQ-001", .description = "The device shall do X."},
                Requirement{.requirementId = "REQ-002", .description = "The device shall do Y."},
            },
        .verificationCases =
            {
                VerificationCase{.caseId = "VER-001",
                                  .requirementId = "REQ-001",
                                  .evidenceRefs = {"tests/x_test.cpp"},
                                  .passed = true},
                VerificationCase{.caseId = "VER-002",
                                  .requirementId = "REQ-002",
                                  .evidenceRefs = {"tests/y_test.cpp"},
                                  .passed = true},
            },
        .hazards =
            {
                Hazard{.hazardId = "HAZ-001",
                       .description = "Z could happen.",
                       .controlledBy = {"REQ-001"}},
            },
        .problemReports =
            {
                ProblemReport{
                    .reportId = "PR-001", .description = "Minor cosmetic issue.", .open = false},
            },
        .generatedArtifacts = {},
    };
}

void expectInvalid(const ComplianceProgram& program, ComplianceError expected,
                   std::string_view what) {
    const auto result = program.validate();
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
// validate()
// ---------------------------------------------------------------------------

TEST_CASE("A well-formed ComplianceProgram validates", "evidence-unit") {
    CHECK(validProgram().validate().has_value());
}

TEST_CASE("validate() requires non-empty, unique requirementIds", "evidence-unit") {
    ComplianceProgram empty = validProgram();
    empty.requirements[0].requirementId.clear();
    expectInvalid(empty, ComplianceError::EmptyRequirementId, "empty requirementId");

    ComplianceProgram duplicate = validProgram();
    duplicate.requirements[1].requirementId = "REQ-001";
    // REQ-002's VerificationCase now points at a requirementId that still exists (REQ-001
    // appears twice), so this program is caught by the duplicate check, not a dangling one.
    expectInvalid(duplicate, ComplianceError::DuplicateRequirementId, "duplicate requirementId");
}

TEST_CASE("validate() requires non-empty, unique caseIds that reference a real requirement",
          "evidence-unit") {
    ComplianceProgram emptyId = validProgram();
    emptyId.verificationCases[0].caseId.clear();
    expectInvalid(emptyId, ComplianceError::EmptyVerificationCaseId, "empty caseId");

    ComplianceProgram duplicateId = validProgram();
    duplicateId.verificationCases[1].caseId = "VER-001";
    expectInvalid(duplicateId, ComplianceError::DuplicateVerificationCaseId, "duplicate caseId");

    ComplianceProgram dangling = validProgram();
    dangling.verificationCases[0].requirementId = "REQ-999";
    expectInvalid(dangling, ComplianceError::DanglingVerificationCaseRequirement,
                  "VerificationCase referencing a nonexistent requirement");
}

TEST_CASE("validate() requires every Hazard to name at least one real controlling requirement",
          "evidence-unit") {
    // This is issue #35's own example of a deliberate gap: a Hazard with an empty controlledBy.
    ComplianceProgram missingControl = validProgram();
    missingControl.hazards[0].controlledBy.clear();
    expectInvalid(missingControl, ComplianceError::HazardMissingControl,
                  "Hazard with empty controlledBy");

    ComplianceProgram dangling = validProgram();
    dangling.hazards[0].controlledBy = {"REQ-999"};
    expectInvalid(dangling, ComplianceError::DanglingHazardControl,
                  "Hazard controlledBy referencing a nonexistent requirement");

    ComplianceProgram emptyId = validProgram();
    emptyId.hazards[0].hazardId.clear();
    expectInvalid(emptyId, ComplianceError::EmptyHazardId, "empty hazardId");

    ComplianceProgram duplicateId = validProgram();
    duplicateId.hazards.push_back(duplicateId.hazards[0]);
    expectInvalid(duplicateId, ComplianceError::DuplicateHazardId, "duplicate hazardId");
}

TEST_CASE("validate() requires non-empty, unique problem report ids", "evidence-unit") {
    ComplianceProgram emptyId = validProgram();
    emptyId.problemReports[0].reportId.clear();
    expectInvalid(emptyId, ComplianceError::EmptyProblemReportId, "empty reportId");

    ComplianceProgram duplicateId = validProgram();
    duplicateId.problemReports.push_back(duplicateId.problemReports[0]);
    expectInvalid(duplicateId, ComplianceError::DuplicateProblemReportId, "duplicate reportId");
}

TEST_CASE("validate() requires every Requirement to be discharged by a VerificationCase",
          "evidence-unit") {
    // Issue #35's other example of a deliberate gap: a requirement with no verification case.
    ComplianceProgram unverified = validProgram();
    unverified.requirements.push_back(
        Requirement{.requirementId = "REQ-003", .description = "Nothing verifies this."});
    expectInvalid(unverified, ComplianceError::UnverifiedRequirement,
                  "Requirement with no discharging VerificationCase");
}

// ---------------------------------------------------------------------------
// traceabilityMatrix()
// ---------------------------------------------------------------------------

TEST_CASE("traceabilityMatrix() lists every requirement with its cases and evidence",
          "evidence-unit") {
    const auto matrix = traceabilityMatrix(validProgram());
    REQUIRE(matrix.has_value());
    REQUIRE(matrix->kind() == json::Value::Kind::Array);
    REQUIRE(matrix->elements().size() == 2);

    const json::Value& first = matrix->elements()[0];
    CHECK(first.find("requirement_id")->asString().value() == "REQ-001");
    const json::Value* cases = first.find("verification_cases");
    REQUIRE(cases != nullptr);
    REQUIRE(cases->kind() == json::Value::Kind::Array);
    REQUIRE(cases->elements().size() == 1);
    CHECK(cases->elements()[0].find("case_id")->asString().value() == "VER-001");
    CHECK(cases->elements()[0].find("passed")->asBool().value() == true);
    const json::Value* refs = cases->elements()[0].find("evidence_refs");
    REQUIRE(refs != nullptr);
    REQUIRE(refs->elements().size() == 1);
    CHECK(refs->elements()[0].asString().value() == "tests/x_test.cpp");
}

TEST_CASE("traceabilityMatrix() names a coverage gap as an empty row, not an omission",
          "evidence-unit") {
    ComplianceProgram gapped = validProgram();
    gapped.requirements.push_back(
        Requirement{.requirementId = "REQ-003", .description = "Nothing verifies this."});

    // The program itself is invalid...
    CHECK(!gapped.validate().has_value());

    // ...but the matrix still names the gap rather than gating on validate() or dropping the row.
    const auto matrix = traceabilityMatrix(gapped);
    REQUIRE(matrix.has_value());
    REQUIRE(matrix->elements().size() == 3);

    const json::Value& gapRow = matrix->elements()[2];
    CHECK(gapRow.find("requirement_id")->asString().value() == "REQ-003");
    const json::Value* cases = gapRow.find("verification_cases");
    REQUIRE(cases != nullptr);
    CHECK(cases->elements().empty());
}

TEST_CASE("traceabilityMatrix() sorts rows and cases for a byte-stable export", "evidence-unit") {
    ComplianceProgram reordered = validProgram();
    std::ranges::reverse(reordered.requirements);
    std::ranges::reverse(reordered.verificationCases);

    const auto matrix = traceabilityMatrix(reordered);
    REQUIRE(matrix.has_value());
    CHECK(matrix->elements()[0].find("requirement_id")->asString().value() == "REQ-001");
    CHECK(matrix->elements()[1].find("requirement_id")->asString().value() == "REQ-002");
}

// ---------------------------------------------------------------------------
// releaseEvidenceSummary()
// ---------------------------------------------------------------------------

TEST_CASE("releaseEvidenceSummary() reports full coverage and no open problem reports as passing",
          "evidence-unit") {
    const auto summary = releaseEvidenceSummary(validProgram());
    REQUIRE(summary.has_value());
    CHECK(summary->find("validation_passed")->asBool().value() == true);
    CHECK(summary->find("requirements_total")->asUInt().value() == 2);
    CHECK(summary->find("requirements_verified")->asUInt().value() == 2);
    CHECK(summary->find("open_problem_reports")->elements().empty());
}

TEST_CASE("releaseEvidenceSummary() fails validation on an unverified requirement or a failed case",
          "evidence-unit") {
    ComplianceProgram gapped = validProgram();
    gapped.requirements.push_back(
        Requirement{.requirementId = "REQ-003", .description = "Nothing verifies this."});
    const auto gappedSummary = releaseEvidenceSummary(gapped);
    REQUIRE(gappedSummary.has_value());
    CHECK(gappedSummary->find("validation_passed")->asBool().value() == false);
    CHECK(gappedSummary->find("requirements_total")->asUInt().value() == 3);
    CHECK(gappedSummary->find("requirements_verified")->asUInt().value() == 2);

    ComplianceProgram failedCase = validProgram();
    failedCase.verificationCases[0].passed = false;
    const auto failedSummary = releaseEvidenceSummary(failedCase);
    REQUIRE(failedSummary.has_value());
    CHECK(failedSummary->find("validation_passed")->asBool().value() == false);
}

TEST_CASE("releaseEvidenceSummary() uses all ComplianceProgram release invariants",
          "evidence-unit") {
    ComplianceProgram uncontrolledHazard = validProgram();
    uncontrolledHazard.hazards[0].controlledBy.clear();

    // Requirement coverage and case results still look complete, but an uncontrolled hazard is
    // a release blocker and must not be hidden behind those narrower counts.
    const auto summary = releaseEvidenceSummary(uncontrolledHazard);
    REQUIRE(summary.has_value());
    CHECK(summary->find("requirements_total")->asUInt().value() == 2);
    CHECK(summary->find("requirements_verified")->asUInt().value() == 2);
    CHECK(summary->find("validation_passed")->asBool().value() == false);
}

TEST_CASE("releaseEvidenceSummary() lists only open problem reports, sorted by id",
          "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.problemReports = {
        ProblemReport{.reportId = "PR-002", .description = "Still open, filed second.", .open = true},
        ProblemReport{.reportId = "PR-001", .description = "Closed already.", .open = false},
        ProblemReport{.reportId = "PR-003", .description = "Still open, filed third.", .open = true},
    };

    const auto summary = releaseEvidenceSummary(program);
    REQUIRE(summary.has_value());
    const json::Value* open = summary->find("open_problem_reports");
    REQUIRE(open != nullptr);
    REQUIRE(open->elements().size() == 2);
    CHECK(open->elements()[0].find("report_id")->asString().value() == "PR-002");
    CHECK(open->elements()[1].find("report_id")->asString().value() == "PR-003");
}

TEST_CASE("releaseEvidenceSummary() lists generated artifact digests, sorted by path",
          "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.generatedArtifacts = {
        mdux::evidence::FileRecord{
            .path = "generated/font/roboto-ui/package.json",
            .sha256 = mdux::evidence::sha256(
                std::as_bytes(std::span{std::string_view{"package bytes"}}))},
        mdux::evidence::FileRecord{
            .path = "generated/font/roboto-ui/atlas.bin",
            .sha256 = mdux::evidence::sha256(
                std::as_bytes(std::span{std::string_view{"atlas bytes"}}))},
    };

    const auto summary = releaseEvidenceSummary(program);
    REQUIRE(summary.has_value());
    const json::Value* artifacts = summary->find("generated_artifacts");
    REQUIRE(artifacts != nullptr);
    REQUIRE(artifacts->elements().size() == 2);
    CHECK(artifacts->elements()[0].find("path")->asString().value() ==
          "generated/font/roboto-ui/atlas.bin");
    CHECK(artifacts->elements()[1].find("path")->asString().value() ==
          "generated/font/roboto-ui/package.json");
    CHECK(artifacts->elements()[0].find("sha256")->asString().value().size() == 64);
}

TEST_CASE("describe() names every compliance error", "evidence-unit") {
    constexpr std::array<ComplianceError, 13> all{
        ComplianceError::EmptyRequirementId,
        ComplianceError::DuplicateRequirementId,
        ComplianceError::EmptyVerificationCaseId,
        ComplianceError::DuplicateVerificationCaseId,
        ComplianceError::DanglingVerificationCaseRequirement,
        ComplianceError::EmptyHazardId,
        ComplianceError::DuplicateHazardId,
        ComplianceError::HazardMissingControl,
        ComplianceError::DanglingHazardControl,
        ComplianceError::EmptyProblemReportId,
        ComplianceError::DuplicateProblemReportId,
        ComplianceError::UnverifiedRequirement,
        ComplianceError::MalformedCompliance};

    for (const ComplianceError error : all) {
        CHECK(!describe(error).empty());
        CHECK(describe(error) != "unrecognized compliance error");
    }
}
