/**
 * @file ComplianceProgramTests.cpp
 * @brief Tests for the lifecycle records and the ComplianceProgram aggregate.
 *
 * @compliance ADR-007 (indirectly, via mdux.evidence.json)
 *
 * The tests that matter most here are the cross-record ones - coverage, and whether a hazard's
 * control resolves. Those are the rules a release gate exists to enforce, and they are also the
 * only ones no per-record validate() could ever catch, so a regression in them would otherwise
 * be silent.
 */

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.governance;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::governance;

namespace {

[[nodiscard]] Requirement validRequirement() {
    return Requirement{
        .id = "REQ-TRUSTZONE-001",
        .title = "Governed modules must not reach Vulkan",
        .sourceClause = "IEC 62304:2006 §5.3 Software architectural design",
        .verificationIntent = "mdux_verify_trust_zones() fails the configure step on any governed "
                              "target whose link closure contains Vulkan::Vulkan.",
    };
}

[[nodiscard]] Hazard validHazard() {
    return Hazard{
        .id = "HAZ-001",
        .description = "A governed component gains a dependency on a driver-provided library, so "
                       "its behaviour is no longer determined by the audited source alone.",
        .controlledBy = {"REQ-TRUSTZONE-001"},
    };
}

[[nodiscard]] VerificationCase validVerificationCase() {
    return VerificationCase{
        .id = "VER-001",
        .requirementId = "REQ-TRUSTZONE-001",
        .method = VerificationMethod::Test,
        .evidenceRefs = {"cmake/MduXTrustZones.cmake", "tests/compliance/TrustZoneTests.cpp"},
    };
}

[[nodiscard]] ProblemReport validProblemReport() {
    return ProblemReport{
        .id = "PRB-001",
        .description = "The trust-zone check did not traverse INTERFACE link libraries.",
        .closed = true,
        .affectsRisk = true,
    };
}

[[nodiscard]] AuditEvent validAuditEvent() {
    return AuditEvent{
        .category = AuditCategory::Verification,
        .timestamp = "2026-07-28T09:15:00Z",
        .subject = "REQ-TRUSTZONE-001",
    };
}

[[nodiscard]] Justification validJustificationRecord() {
    return Justification{
        .justificationId = "JUS-001",
        .standard = "IEC 62304:2006",
        .clauseRef = "IEC 62304:2006 §5.3 Software architectural design",
        .rationale = "MduXTrustZones.cmake mechanically enforces the architectural decision "
                     "recorded in ADR-004.",
        .requirementId = "REQ-TRUSTZONE-001",
        .evidenceRefs = {"cmake/MduXTrustZones.cmake"},
    };
}

/// A program every rule accepts, so each test can break exactly one thing.
[[nodiscard]] ComplianceProgram validProgram() {
    return ComplianceProgram{
        .safetyClass = SafetyClass::B,
        .requirements = {validRequirement()},
        .hazards = {validHazard()},
        .verificationCases = {validVerificationCase()},
        .problemReports = {validProblemReport()},
        .auditEvents = {validAuditEvent()},
        .justifications = {validJustificationRecord()},
    };
}

/// Reports whether `failures` contains exactly one entry with this code, and returns it.
[[nodiscard]] const ValidationFailure* findOnly(const std::vector<ValidationFailure>& failures,
                                                GovernanceError code) {
    const ValidationFailure* found = nullptr;
    std::size_t count = 0;
    for (const ValidationFailure& failure : failures) {
        if (failure.code == code) {
            found = &failure;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

template <typename Record>
void expectInvalid(const Record& record, GovernanceError expected, std::string_view what) {
    const auto result = record.validate();
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
// Closed vocabularies
// ---------------------------------------------------------------------------

TEST_CASE("Every closed vocabulary round-trips through its wire spelling", "evidence-unit") {
    for (const SafetyClass value : {SafetyClass::A, SafetyClass::B, SafetyClass::C}) {
        const auto parsed = safetyClassFromWire(toWireString(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    for (const VerificationMethod value :
         {VerificationMethod::Test, VerificationMethod::Analysis, VerificationMethod::Inspection,
          VerificationMethod::Review}) {
        const auto parsed = verificationMethodFromWire(toWireString(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    for (const AuditCategory value :
         {AuditCategory::Lifecycle, AuditCategory::Verification, AuditCategory::Change}) {
        const auto parsed = auditCategoryFromWire(toWireString(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
}

TEST_CASE("An unrecognized wire spelling is an error, never a default", "evidence-unit") {
    CHECK(!safetyClassFromWire("D").has_value());
    CHECK(!safetyClassFromWire("a").has_value());  // case-sensitive
    CHECK(!safetyClassFromWire("").has_value());
    CHECK(!verificationMethodFromWire("Test").has_value());  // wire spelling is lowercase
    CHECK(!verificationMethodFromWire("demonstration").has_value());
    CHECK(!auditCategoryFromWire("release").has_value());
}

TEST_CASE("The exported wire-value arrays list exactly what the parsers accept", "evidence-unit") {
    // The schema-drift check reads these arrays; if they and the parsers disagreed, the schema
    // would document a set the code does not implement.
    for (const std::string_view value : kSafetyClassWireValues) {
        CHECK(safetyClassFromWire(value).has_value());
    }
    for (const std::string_view value : kVerificationMethodWireValues) {
        CHECK(verificationMethodFromWire(value).has_value());
    }
    for (const std::string_view value : kAuditCategoryWireValues) {
        CHECK(auditCategoryFromWire(value).has_value());
    }
}

// ---------------------------------------------------------------------------
// Per-record shape rules
// ---------------------------------------------------------------------------

TEST_CASE("Requirement rejects each malformed field in turn", "evidence-unit") {
    CHECK(validRequirement().validate().has_value());

    Requirement noId = validRequirement();
    noId.id.clear();
    expectInvalid(noId, GovernanceError::EmptyRequirementId, "empty id");

    Requirement badId = validRequirement();
    badId.id = "REQUIREMENT-1";
    expectInvalid(badId, GovernanceError::MalformedRequirementId, "wrong prefix");

    Requirement lowercaseId = validRequirement();
    lowercaseId.id = "REQ-trustzone";
    expectInvalid(lowercaseId, GovernanceError::MalformedRequirementId, "lowercase id body");

    Requirement noTitle = validRequirement();
    noTitle.title.clear();
    expectInvalid(noTitle, GovernanceError::EmptyRequirementTitle, "empty title");

    Requirement noClause = validRequirement();
    noClause.sourceClause.clear();
    expectInvalid(noClause, GovernanceError::EmptySourceClause, "empty sourceClause");

    Requirement unapprovedStandard = validRequirement();
    unapprovedStandard.sourceClause = "ISO 9001:2015 §8.3 Design and development";
    expectInvalid(unapprovedStandard, GovernanceError::MalformedSourceClause,
                  "sourceClause naming an unapproved standard");

    Requirement noClauseNumber = validRequirement();
    noClauseNumber.sourceClause = "IEC 62304:2006 §Software architectural design";
    expectInvalid(noClauseNumber, GovernanceError::MalformedSourceClause,
                  "sourceClause with no clause number");

    Requirement noIntent = validRequirement();
    noIntent.verificationIntent.clear();
    expectInvalid(noIntent, GovernanceError::EmptyVerificationIntent, "empty verificationIntent");
}

TEST_CASE("A Hazard with no recorded control is rejected", "evidence-unit") {
    CHECK(validHazard().validate().has_value());

    Hazard uncontrolled = validHazard();
    uncontrolled.controlledBy.clear();
    expectInvalid(uncontrolled, GovernanceError::EmptyControls,
                  "the IEC 62304 §4.2 / ISO 14971 §7 join must not be empty");

    Hazard emptyControl = validHazard();
    emptyControl.controlledBy = {""};
    expectInvalid(emptyControl, GovernanceError::EmptyControlRef, "empty control entry");

    Hazard duplicateControl = validHazard();
    duplicateControl.controlledBy = {"REQ-A", "REQ-A"};
    expectInvalid(duplicateControl, GovernanceError::DuplicateControlRef, "duplicate control");

    Hazard nonRequirementControl = validHazard();
    nonRequirementControl.controlledBy = {"HAZ-002"};
    expectInvalid(nonRequirementControl, GovernanceError::MalformedRequirementId,
                  "a control must name a requirement");

    Hazard badId = validHazard();
    badId.id = "H-1";
    expectInvalid(badId, GovernanceError::MalformedHazardId, "wrong hazard id prefix");

    Hazard noDescription = validHazard();
    noDescription.description.clear();
    expectInvalid(noDescription, GovernanceError::EmptyHazardDescription, "empty description");
}

TEST_CASE("VerificationCase rejects each malformed field in turn", "evidence-unit") {
    CHECK(validVerificationCase().validate().has_value());

    VerificationCase badId = validVerificationCase();
    badId.id = "VERIFY-1";
    expectInvalid(badId, GovernanceError::MalformedVerificationCaseId, "wrong prefix");

    VerificationCase noRequirement = validVerificationCase();
    noRequirement.requirementId.clear();
    expectInvalid(noRequirement, GovernanceError::EmptyVerifiedRequirementId, "empty requirementId");

    VerificationCase badRequirement = validVerificationCase();
    badRequirement.requirementId = "HAZ-001";
    expectInvalid(badRequirement, GovernanceError::MalformedRequirementId,
                  "requirementId must name a requirement");

    VerificationCase noEvidence = validVerificationCase();
    noEvidence.evidenceRefs.clear();
    expectInvalid(noEvidence, GovernanceError::EmptyEvidenceRefs, "empty evidenceRefs");

    VerificationCase duplicateEvidence = validVerificationCase();
    duplicateEvidence.evidenceRefs = {"a.cpp", "a.cpp"};
    expectInvalid(duplicateEvidence, GovernanceError::DuplicateEvidenceRef, "duplicate evidence");
}

TEST_CASE("ProblemReport rejects each malformed field in turn", "evidence-unit") {
    CHECK(validProblemReport().validate().has_value());

    ProblemReport badId = validProblemReport();
    badId.id = "PR-001";
    expectInvalid(badId, GovernanceError::MalformedProblemReportId, "wrong prefix");

    ProblemReport noDescription = validProblemReport();
    noDescription.description.clear();
    expectInvalid(noDescription, GovernanceError::EmptyProblemDescription, "empty description");

    // An open problem that affects risk is a perfectly valid record - it is a fact about the
    // project, not an error. Nothing here should reject it.
    ProblemReport openRiskProblem = validProblemReport();
    openRiskProblem.closed = false;
    openRiskProblem.affectsRisk = true;
    CHECK(openRiskProblem.validate().has_value());
}

TEST_CASE("AuditEvent accepts exactly one timestamp spelling", "evidence-unit") {
    CHECK(validAuditEvent().validate().has_value());

    for (const std::string_view rejected : {
             "2026-07-28T09:15:00",       // no zone marker
             "2026-07-28T09:15:00+00:00", // offset spelling instead of Z
             "2026-07-28 09:15:00Z",      // space instead of T
             "2026-07-28T09:15:00.000Z",  // fractional seconds
             "26-07-28T09:15:00Z",        // two-digit year
             "2026-13-28T09:15:00Z",      // month out of range
             "2026-07-32T09:15:00Z",      // day out of range
             "2026-07-28T24:15:00Z",      // hour out of range
             "2026-07-28T09:60:00Z",      // minute out of range
         }) {
        AuditEvent event = validAuditEvent();
        event.timestamp = std::string{rejected};
        expectInvalid(event, GovernanceError::MalformedAuditTimestamp, rejected);
    }

    AuditEvent noTimestamp = validAuditEvent();
    noTimestamp.timestamp.clear();
    expectInvalid(noTimestamp, GovernanceError::EmptyAuditTimestamp, "empty timestamp");

    AuditEvent noSubject = validAuditEvent();
    noSubject.subject.clear();
    expectInvalid(noSubject, GovernanceError::EmptyAuditSubject, "empty subject");
}

// ---------------------------------------------------------------------------
// ComplianceProgram - the cross-record rules
// ---------------------------------------------------------------------------

TEST_CASE("A well-formed program validates, and an empty one does too", "evidence-unit") {
    CHECK(validProgram().validate().has_value());
    // A program with no records violates no rule. It claims nothing, so there is nothing to
    // contradict; a gate that wants records to exist is asking a different question.
    CHECK(ComplianceProgram{}.validate().has_value());
}

TEST_CASE("Every requirement must be covered by at least one verification case", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.verificationCases.clear();

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    const ValidationFailure* failure =
        findOnly(result.error(), GovernanceError::UnverifiedRequirement);
    REQUIRE(failure != nullptr);
    CHECK(failure->subject == "REQ-TRUSTZONE-001");
}

TEST_CASE("A verification case naming an unknown requirement is reported", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.verificationCases[0].requirementId = "REQ-DOES-NOT-EXIST";

    const auto result = program.validate();
    REQUIRE(!result.has_value());

    const ValidationFailure* dangling =
        findOnly(result.error(), GovernanceError::UnresolvedVerifiedRequirement);
    REQUIRE(dangling != nullptr);
    CHECK(dangling->subject == "VER-001");
    CHECK(dangling->detail == "REQ-DOES-NOT-EXIST");

    // The real requirement is now uncovered as well, and both facts are reported: a gate that
    // only said "dangling reference" would leave the coverage gap to be discovered next run.
    const ValidationFailure* uncovered =
        findOnly(result.error(), GovernanceError::UnverifiedRequirement);
    REQUIRE(uncovered != nullptr);
    CHECK(uncovered->subject == "REQ-TRUSTZONE-001");
}

TEST_CASE("A hazard control that resolves to nothing is reported", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.hazards[0].controlledBy = {"REQ-NO-SUCH-CONTROL"};

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    const ValidationFailure* failure =
        findOnly(result.error(), GovernanceError::UnresolvedHazardControl);
    REQUIRE(failure != nullptr);
    CHECK(failure->subject == "HAZ-001");
    CHECK(failure->detail == "REQ-NO-SUCH-CONTROL");
}

TEST_CASE("Duplicate ids are reported once per extra copy, per collection", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.requirements.push_back(validRequirement());
    program.hazards.push_back(validHazard());
    program.verificationCases.push_back(validVerificationCase());
    program.problemReports.push_back(validProblemReport());
    program.justifications.push_back(validJustificationRecord());

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    CHECK(findOnly(result.error(), GovernanceError::DuplicateRequirementId) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::DuplicateHazardId) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::DuplicateVerificationCaseId) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::DuplicateProblemReportId) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::DuplicateJustificationId) != nullptr);
}

TEST_CASE("validate() reports every failure, not just the first", "evidence-unit") {
    ComplianceProgram program;
    program.requirements = {validRequirement()};                    // uncovered
    program.hazards = {validHazard()};                              // control resolves
    program.hazards[0].controlledBy = {"REQ-MISSING"};              // ...no longer
    program.auditEvents = {validAuditEvent()};
    program.auditEvents[0].timestamp = "yesterday";                 // malformed
    program.problemReports = {validProblemReport()};
    program.problemReports[0].description.clear();                  // malformed

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    CHECK_MESSAGE(result.error().size() == 4,
                  "expected 4 failures, got " + std::to_string(result.error().size()));
    CHECK(findOnly(result.error(), GovernanceError::UnverifiedRequirement) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::UnresolvedHazardControl) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::MalformedAuditTimestamp) != nullptr);
    CHECK(findOnly(result.error(), GovernanceError::EmptyProblemDescription) != nullptr);
}

TEST_CASE("A record with a malformed id is not also reported as uncovered", "evidence-unit") {
    // One mistake, one failure. Reporting the coverage gap too would send a reader chasing a
    // second problem that disappears the moment the first is fixed.
    ComplianceProgram program;
    program.requirements = {validRequirement()};
    program.requirements[0].id = "REQUIREMENT-1";

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    CHECK(result.error().size() == 1);
    CHECK(result.error()[0].code == GovernanceError::MalformedRequirementId);
}

TEST_CASE("An AuditEvent failure is named by its position", "evidence-unit") {
    ComplianceProgram program;
    program.auditEvents = {validAuditEvent(), validAuditEvent()};
    program.auditEvents[1].subject.clear();

    const auto result = program.validate();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().size() == 1);
    CHECK(result.error()[0].subject == "auditEvents[1]");
}

TEST_CASE("The failure list is deterministic across runs", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.verificationCases.clear();
    program.hazards[0].controlledBy = {"REQ-MISSING"};

    const auto first = program.validate();
    const auto second = program.validate();
    REQUIRE(!first.has_value());
    REQUIRE(!second.has_value());
    CHECK(first.error() == second.error());
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("A ComplianceProgram round-trips through write and parse", "evidence-unit") {
    const ComplianceProgram original = validProgram();
    const auto text = original.write();
    REQUIRE(text.has_value());

    const auto reparsed = ComplianceProgram::parse(*text);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->safetyClass == original.safetyClass);
    REQUIRE(reparsed->requirements.size() == 1);
    CHECK(reparsed->requirements[0].id == original.requirements[0].id);
    CHECK(reparsed->requirements[0].sourceClause == original.requirements[0].sourceClause);
    REQUIRE(reparsed->hazards.size() == 1);
    CHECK(reparsed->hazards[0].controlledBy == original.hazards[0].controlledBy);
    REQUIRE(reparsed->verificationCases.size() == 1);
    CHECK(reparsed->verificationCases[0].method == original.verificationCases[0].method);
    REQUIRE(reparsed->problemReports.size() == 1);
    CHECK(reparsed->problemReports[0].closed == original.problemReports[0].closed);
    CHECK(reparsed->problemReports[0].affectsRisk == original.problemReports[0].affectsRisk);
    REQUIRE(reparsed->auditEvents.size() == 1);
    CHECK(reparsed->auditEvents[0].category == original.auditEvents[0].category);
    REQUIRE(reparsed->justifications.size() == 1);
    CHECK(reparsed->justifications[0].justificationId == original.justifications[0].justificationId);

    // The property CI would actually depend on: re-serializing reproduces identical bytes.
    const auto rewritten = reparsed->write();
    REQUIRE(rewritten.has_value());
    CHECK(*rewritten == *text);
}

TEST_CASE("write() refuses to serialize a program that does not validate", "evidence-unit") {
    ComplianceProgram program = validProgram();
    program.verificationCases.clear();
    CHECK(!program.write().has_value());
}

TEST_CASE("parse() rejects an unknown member, a missing member, and a bad enum", "evidence-unit") {
    const auto text = validProgram().write();
    REQUIRE(text.has_value());

    std::string extraMember = *text;
    const std::size_t closingBrace = extraMember.rfind('}');
    REQUIRE(closingBrace != std::string::npos);
    extraMember.insert(closingBrace, ",\n  \"unexpected\": \"value\"\n");
    CHECK_MESSAGE(!ComplianceProgram::parse(extraMember).has_value(),
                  "additionalProperties: false must reject unknown members");

    for (const std::string_view member :
         {"\"safety_class\"", "\"requirements\"", "\"hazards\"", "\"verification_cases\"",
          "\"problem_reports\"", "\"audit_events\"", "\"justifications\""}) {
        std::string mutated = *text;
        const std::size_t position = mutated.find(member);
        REQUIRE(position != std::string::npos);
        mutated.replace(position + 1, 3, "zzz");
        CHECK_MESSAGE(!ComplianceProgram::parse(mutated).has_value(),
                      std::string{"a program missing "} + std::string{member} +
                          " should not parse");
    }

    std::string badEnum = *text;
    const std::size_t position = badEnum.find("\"safety_class\": \"B\"");
    REQUIRE(position != std::string::npos);
    badEnum.replace(position, std::string_view{"\"safety_class\": \"B\""}.size(),
                    "\"safety_class\": \"D\"");
    CHECK(!ComplianceProgram::parse(badEnum).has_value());

    CHECK(!ComplianceProgram::parse("").has_value());
    CHECK(!ComplianceProgram::parse("[]").has_value());
    CHECK(!ComplianceProgram::parse("{}").has_value());
    CHECK(!ComplianceProgram::parse("not json at all").has_value());
}

TEST_CASE("parse() validates what it parsed, not just the JSON shape", "evidence-unit") {
    // A program whose JSON is impeccable but whose hazard control dangles must not load.
    ComplianceProgram program = validProgram();
    const auto text = program.write();
    REQUIRE(text.has_value());

    std::string mutated = *text;
    // Specifically the hazard's control, not the first "REQ-TRUSTZONE-001" in the document -
    // canonical JSON sorts members alphabetically, so that one is an audit event's subject,
    // which takes part in no cross-record rule and would leave the program valid.
    const std::size_t controls = mutated.find("\"controlled_by\"");
    REQUIRE(controls != std::string::npos);
    const std::size_t position = mutated.find("\"REQ-TRUSTZONE-001\"", controls);
    REQUIRE(position != std::string::npos);
    mutated.replace(position, std::string_view{"\"REQ-TRUSTZONE-001\""}.size(),
                    "\"REQ-SOMETHING-ELSE\"");
    CHECK(!ComplianceProgram::parse(mutated).has_value());
}

TEST_CASE("describe() names every governance error", "evidence-unit") {
    // Every enumerator, not a hand-copied subset: a new code with no description would otherwise
    // reach a release gate's output as "unrecognized governance error".
    constexpr auto last = static_cast<std::uint8_t>(GovernanceError::MalformedComplianceProgram);
    for (std::uint8_t value = 0; value <= last; ++value) {
        const auto error = static_cast<GovernanceError>(value);
        CHECK_MESSAGE(!describe(error).empty(),
                      "GovernanceError " + std::to_string(value) + " has no description");
        CHECK_MESSAGE(describe(error) != "unrecognized governance error",
                      "GovernanceError " + std::to_string(value) + " is undescribed");
    }
}
