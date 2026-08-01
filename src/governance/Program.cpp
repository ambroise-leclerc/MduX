/**
 * @file Program.cpp
 * @brief The lifecycle records - Requirement, Hazard, VerificationCase, ProblemReport,
 *        AuditEvent - and the ComplianceProgram that aggregates and cross-checks them.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Two layers of checking live here, and keeping them apart is the whole design:
 *
 * - each record's `validate()` checks only what is knowable from that record alone - an id's
 *   shape, a non-empty field, a well-formed citation key;
 * - `ComplianceProgram::validate()` checks everything that needs the other records - id
 *   uniqueness, requirement coverage, and whether a `controlledBy` or `requirementId` actually
 *   resolves.
 *
 * A per-record validate() that tried to check coverage would have to be handed the collection,
 * at which point it is the aggregate's method with extra steps. The split is also why a record
 * returns a single error while the program returns a list: one record has one first thing wrong
 * with it and the caller fixes that; a program has as many problems as it has, and a release
 * gate needs all of them at once.
 */
module;

module mdux.governance;

import std;
import mdux.core.result;
import mdux.evidence.json;

namespace mdux::governance {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = evidence::json;

// ---------------------------------------------------------------------------
// Requirement
// ---------------------------------------------------------------------------

ResultVoid<GovernanceError> Requirement::validate() const noexcept {
    if (id.empty()) {
        return err(GovernanceError::EmptyRequirementId);
    }
    if (!detail::isWellFormedId(id, "REQ-")) {
        return err(GovernanceError::MalformedRequirementId);
    }
    if (title.empty()) {
        return err(GovernanceError::EmptyRequirementTitle);
    }
    if (sourceClause.empty()) {
        return err(GovernanceError::EmptySourceClause);
    }
    if (!detail::citationKeyStandard(sourceClause).has_value()) {
        return err(GovernanceError::MalformedSourceClause);
    }
    if (verificationIntent.empty()) {
        return err(GovernanceError::EmptyVerificationIntent);
    }
    return {};
}

Result<json::Value, GovernanceError> Requirement::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    json::Value object = json::Value::emptyObject();
    const bool ok = object.set("id", json::Value::string(id)).has_value() &&
                    object.set("title", json::Value::string(title)).has_value() &&
                    object.set("source_clause", json::Value::string(sourceClause)).has_value() &&
                    object.set("verification_intent", json::Value::string(verificationIntent))
                        .has_value();
    if (!ok) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<Requirement, GovernanceError> Requirement::fromJson(const json::Value& object) noexcept {
    constexpr GovernanceError malformed = GovernanceError::MalformedComplianceProgram;
    if (!detail::hasExactly(object, 4)) {
        return err(malformed);
    }
    Requirement result;
    auto id = detail::requireString(object, "id", malformed);
    if (!id.has_value()) {
        return err(id.error());
    }
    result.id = std::move(*id);

    auto title = detail::requireString(object, "title", malformed);
    if (!title.has_value()) {
        return err(title.error());
    }
    result.title = std::move(*title);

    auto sourceClause = detail::requireString(object, "source_clause", malformed);
    if (!sourceClause.has_value()) {
        return err(sourceClause.error());
    }
    result.sourceClause = std::move(*sourceClause);

    auto intent = detail::requireString(object, "verification_intent", malformed);
    if (!intent.has_value()) {
        return err(intent.error());
    }
    result.verificationIntent = std::move(*intent);

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

// ---------------------------------------------------------------------------
// Hazard
// ---------------------------------------------------------------------------

ResultVoid<GovernanceError> Hazard::validate() const noexcept {
    if (id.empty()) {
        return err(GovernanceError::EmptyHazardId);
    }
    if (!detail::isWellFormedId(id, "HAZ-")) {
        return err(GovernanceError::MalformedHazardId);
    }
    if (description.empty()) {
        return err(GovernanceError::EmptyHazardDescription);
    }
    // Non-empty controlledBy is the one rule in this file that is a regulatory requirement rather
    // than a data-hygiene one; see the type's comment.
    if (const auto controls = detail::checkRefList(controlledBy, GovernanceError::EmptyControls,
                                                   GovernanceError::EmptyControlRef,
                                                   GovernanceError::DuplicateControlRef)) {
        return err(*controls);
    }
    for (const std::string& control : controlledBy) {
        if (!detail::isWellFormedId(control, "REQ-")) {
            return err(GovernanceError::MalformedRequirementId);
        }
    }
    return {};
}

Result<json::Value, GovernanceError> Hazard::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    auto controls = detail::stringArray(controlledBy, GovernanceError::MalformedComplianceProgram);
    if (!controls.has_value()) {
        return err(controls.error());
    }
    json::Value object = json::Value::emptyObject();
    const bool ok = object.set("id", json::Value::string(id)).has_value() &&
                    object.set("description", json::Value::string(description)).has_value() &&
                    object.set("controlled_by", std::move(*controls)).has_value();
    if (!ok) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<Hazard, GovernanceError> Hazard::fromJson(const json::Value& object) noexcept {
    constexpr GovernanceError malformed = GovernanceError::MalformedComplianceProgram;
    if (!detail::hasExactly(object, 3)) {
        return err(malformed);
    }
    Hazard result;
    auto id = detail::requireString(object, "id", malformed);
    if (!id.has_value()) {
        return err(id.error());
    }
    result.id = std::move(*id);

    auto description = detail::requireString(object, "description", malformed);
    if (!description.has_value()) {
        return err(description.error());
    }
    result.description = std::move(*description);

    auto controls = detail::requireStringArray(object, "controlled_by", malformed);
    if (!controls.has_value()) {
        return err(controls.error());
    }
    result.controlledBy = std::move(*controls);

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

// ---------------------------------------------------------------------------
// VerificationCase
// ---------------------------------------------------------------------------

ResultVoid<GovernanceError> VerificationCase::validate() const noexcept {
    if (id.empty()) {
        return err(GovernanceError::EmptyVerificationCaseId);
    }
    if (!detail::isWellFormedId(id, "VER-")) {
        return err(GovernanceError::MalformedVerificationCaseId);
    }
    if (requirementId.empty()) {
        return err(GovernanceError::EmptyVerifiedRequirementId);
    }
    if (!detail::isWellFormedId(requirementId, "REQ-")) {
        return err(GovernanceError::MalformedRequirementId);
    }
    if (const auto refs = detail::checkRefList(evidenceRefs, GovernanceError::EmptyEvidenceRefs,
                                               GovernanceError::EmptyEvidenceRef,
                                               GovernanceError::DuplicateEvidenceRef)) {
        return err(*refs);
    }
    return {};
}

Result<json::Value, GovernanceError> VerificationCase::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    auto refs = detail::stringArray(evidenceRefs, GovernanceError::MalformedComplianceProgram);
    if (!refs.has_value()) {
        return err(refs.error());
    }
    json::Value object = json::Value::emptyObject();
    const bool ok =
        object.set("id", json::Value::string(id)).has_value() &&
        object.set("requirement_id", json::Value::string(requirementId)).has_value() &&
        object.set("method", json::Value::string(std::string{toWireString(method)})).has_value() &&
        object.set("evidence_refs", std::move(*refs)).has_value() &&
        object.set("passed", json::Value::boolean(passed)).has_value();
    if (!ok) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<VerificationCase, GovernanceError> VerificationCase::fromJson(
    const json::Value& object) noexcept {
    constexpr GovernanceError malformed = GovernanceError::MalformedComplianceProgram;
    if (!detail::hasExactly(object, 5)) {
        return err(malformed);
    }
    VerificationCase result;
    auto id = detail::requireString(object, "id", malformed);
    if (!id.has_value()) {
        return err(id.error());
    }
    result.id = std::move(*id);

    auto requirementId = detail::requireString(object, "requirement_id", malformed);
    if (!requirementId.has_value()) {
        return err(requirementId.error());
    }
    result.requirementId = std::move(*requirementId);

    auto method = detail::requireString(object, "method", malformed);
    if (!method.has_value()) {
        return err(method.error());
    }
    const auto parsedMethod = verificationMethodFromWire(*method);
    if (!parsedMethod.has_value()) {
        return err(malformed);
    }
    result.method = *parsedMethod;

    auto refs = detail::requireStringArray(object, "evidence_refs", malformed);
    if (!refs.has_value()) {
        return err(refs.error());
    }
    result.evidenceRefs = std::move(*refs);

    auto passed = detail::requireBool(object, "passed", malformed);
    if (!passed.has_value()) {
        return err(passed.error());
    }
    result.passed = *passed;

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

// ---------------------------------------------------------------------------
// ProblemReport
// ---------------------------------------------------------------------------

ResultVoid<GovernanceError> ProblemReport::validate() const noexcept {
    if (id.empty()) {
        return err(GovernanceError::EmptyProblemReportId);
    }
    if (!detail::isWellFormedId(id, "PRB-")) {
        return err(GovernanceError::MalformedProblemReportId);
    }
    if (description.empty()) {
        return err(GovernanceError::EmptyProblemDescription);
    }
    return {};
}

Result<json::Value, GovernanceError> ProblemReport::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    json::Value object = json::Value::emptyObject();
    const bool ok = object.set("id", json::Value::string(id)).has_value() &&
                    object.set("description", json::Value::string(description)).has_value() &&
                    object.set("closed", json::Value::boolean(closed)).has_value() &&
                    object.set("affects_risk", json::Value::boolean(affectsRisk)).has_value();
    if (!ok) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<ProblemReport, GovernanceError> ProblemReport::fromJson(const json::Value& object) noexcept {
    constexpr GovernanceError malformed = GovernanceError::MalformedComplianceProgram;
    if (!detail::hasExactly(object, 4)) {
        return err(malformed);
    }
    ProblemReport result;
    auto id = detail::requireString(object, "id", malformed);
    if (!id.has_value()) {
        return err(id.error());
    }
    result.id = std::move(*id);

    auto description = detail::requireString(object, "description", malformed);
    if (!description.has_value()) {
        return err(description.error());
    }
    result.description = std::move(*description);

    auto closed = detail::requireBool(object, "closed", malformed);
    if (!closed.has_value()) {
        return err(closed.error());
    }
    result.closed = *closed;

    auto affectsRisk = detail::requireBool(object, "affects_risk", malformed);
    if (!affectsRisk.has_value()) {
        return err(affectsRisk.error());
    }
    result.affectsRisk = *affectsRisk;

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

// ---------------------------------------------------------------------------
// AuditEvent
// ---------------------------------------------------------------------------

ResultVoid<GovernanceError> AuditEvent::validate() const noexcept {
    if (timestamp.empty()) {
        return err(GovernanceError::EmptyAuditTimestamp);
    }
    if (!detail::isWellFormedUtcTimestamp(timestamp)) {
        return err(GovernanceError::MalformedAuditTimestamp);
    }
    if (subject.empty()) {
        return err(GovernanceError::EmptyAuditSubject);
    }
    return {};
}

Result<json::Value, GovernanceError> AuditEvent::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    json::Value object = json::Value::emptyObject();
    const bool ok =
        object.set("category", json::Value::string(std::string{toWireString(category)}))
            .has_value() &&
        object.set("timestamp", json::Value::string(timestamp)).has_value() &&
        object.set("subject", json::Value::string(subject)).has_value();
    if (!ok) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<AuditEvent, GovernanceError> AuditEvent::fromJson(const json::Value& object) noexcept {
    constexpr GovernanceError malformed = GovernanceError::MalformedComplianceProgram;
    if (!detail::hasExactly(object, 3)) {
        return err(malformed);
    }
    AuditEvent result;
    auto category = detail::requireString(object, "category", malformed);
    if (!category.has_value()) {
        return err(category.error());
    }
    const auto parsedCategory = auditCategoryFromWire(*category);
    if (!parsedCategory.has_value()) {
        return err(malformed);
    }
    result.category = *parsedCategory;

    auto timestamp = detail::requireString(object, "timestamp", malformed);
    if (!timestamp.has_value()) {
        return err(timestamp.error());
    }
    result.timestamp = std::move(*timestamp);

    auto subject = detail::requireString(object, "subject", malformed);
    if (!subject.has_value()) {
        return err(subject.error());
    }
    result.subject = std::move(*subject);

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

// ---------------------------------------------------------------------------
// ComplianceProgram
// ---------------------------------------------------------------------------

namespace {

/// Appends the record's own shape failure, if it has one, and reports whether the record was
/// sound enough for the cross-record rules below to say anything meaningful about it. A
/// requirement with a malformed id, for instance, would otherwise also be reported as
/// "unverified" - two failures for one mistake, the second of which disappears when the first
/// is fixed.
template <typename Record>
[[nodiscard]] bool collectShapeFailure(const Record& record, std::string subject,
                                       std::vector<ValidationFailure>& failures) {
    const auto valid = record.validate();
    if (valid.has_value()) {
        return true;
    }
    failures.push_back(ValidationFailure{valid.error(), std::move(subject), {}});
    return false;
}

/// Records a duplicate-id failure the *second* and later times an id is seen, so N copies of one
/// id produce N-1 failures rather than N.
[[nodiscard]] bool isFirstOccurrence(std::vector<std::string_view>& seen, std::string_view id) {
    if (std::ranges::find(seen, id) != seen.end()) {
        return false;
    }
    seen.push_back(id);
    return true;
}

}  // namespace

ResultVoid<std::vector<ValidationFailure>> ComplianceProgram::validate() const noexcept {
    std::vector<ValidationFailure> failures;

    // Pass 1: each record's own shape, and id uniqueness within its collection. Only records
    // that pass their own validate() are indexed for the cross-record rules - a record with a
    // malformed id has no usable id to join on.
    std::vector<std::string_view> requirementIds;
    std::vector<std::string_view> soundRequirementIds;
    for (const Requirement& requirement : requirements) {
        if (!collectShapeFailure(requirement, requirement.id, failures)) {
            continue;
        }
        if (!isFirstOccurrence(requirementIds, requirement.id)) {
            failures.push_back(
                ValidationFailure{GovernanceError::DuplicateRequirementId, requirement.id, {}});
            continue;
        }
        soundRequirementIds.push_back(requirement.id);
    }

    std::vector<std::string_view> hazardIds;
    for (const Hazard& hazard : hazards) {
        if (!collectShapeFailure(hazard, hazard.id, failures)) {
            continue;
        }
        if (!isFirstOccurrence(hazardIds, hazard.id)) {
            failures.push_back(ValidationFailure{GovernanceError::DuplicateHazardId, hazard.id, {}});
        }
    }

    std::vector<std::string_view> verificationCaseIds;
    std::vector<std::string_view> verifiedRequirementIds;
    for (const VerificationCase& verificationCase : verificationCases) {
        if (!collectShapeFailure(verificationCase, verificationCase.id, failures)) {
            continue;
        }
        if (!isFirstOccurrence(verificationCaseIds, verificationCase.id)) {
            failures.push_back(ValidationFailure{GovernanceError::DuplicateVerificationCaseId,
                                                 verificationCase.id, {}});
            continue;
        }
        verifiedRequirementIds.push_back(verificationCase.requirementId);
    }

    std::vector<std::string_view> problemReportIds;
    for (const ProblemReport& problemReport : problemReports) {
        if (!collectShapeFailure(problemReport, problemReport.id, failures)) {
            continue;
        }
        if (!isFirstOccurrence(problemReportIds, problemReport.id)) {
            failures.push_back(
                ValidationFailure{GovernanceError::DuplicateProblemReportId, problemReport.id, {}});
        }
    }

    for (std::size_t i = 0; i < auditEvents.size(); ++i) {
        // AuditEvent has no id, so it is named positionally. `auditEvents[3]` is enough to find
        // the record in the serialized program, which is what a failure message is for.
        // An AuditEvent takes part in no cross-record rule, so whether it was sound is of no
        // further interest here - only that its shape failure got recorded.
        static_cast<void>(
            collectShapeFailure(auditEvents[i], "auditEvents[" + std::to_string(i) + "]", failures));
    }

    std::vector<std::string_view> justificationIds;
    for (const Justification& justification : justifications) {
        if (!collectShapeFailure(justification, justification.justificationId, failures)) {
            continue;
        }
        if (!isFirstOccurrence(justificationIds, justification.justificationId)) {
            failures.push_back(ValidationFailure{GovernanceError::DuplicateJustificationId,
                                                 justification.justificationId, {}});
        }
    }

    // Pass 2: the cross-record rules - the ones no single record can check for itself.
    auto isKnownRequirement = [&soundRequirementIds](std::string_view id) {
        return std::ranges::find(soundRequirementIds, id) != soundRequirementIds.end();
    };

    // IEC 62304 §5.2.4, as something a build can fail on.
    for (const std::string_view requirementId : soundRequirementIds) {
        if (std::ranges::find(verifiedRequirementIds, requirementId) ==
            verifiedRequirementIds.end()) {
            failures.push_back(ValidationFailure{GovernanceError::UnverifiedRequirement,
                                                 std::string{requirementId}, {}});
        }
    }

    for (const VerificationCase& verificationCase : verificationCases) {
        if (!verificationCase.validate().has_value()) {
            continue;  // already reported in pass 1
        }
        if (!isKnownRequirement(verificationCase.requirementId)) {
            failures.push_back(ValidationFailure{GovernanceError::UnresolvedVerifiedRequirement,
                                                 verificationCase.id,
                                                 verificationCase.requirementId});
        }
    }

    // The ISO 14971 §7 / IEC 62304 §4.2 join: a control that names nothing real is not a control.
    for (const Hazard& hazard : hazards) {
        if (!hazard.validate().has_value()) {
            continue;  // already reported in pass 1
        }
        for (const std::string& control : hazard.controlledBy) {
            if (!isKnownRequirement(control)) {
                failures.push_back(ValidationFailure{GovernanceError::UnresolvedHazardControl,
                                                     hazard.id, control});
            }
        }
    }

    if (failures.empty()) {
        return {};
    }
    return err(std::move(failures));
}

Result<json::Value, GovernanceError> ComplianceProgram::toJson() const noexcept {
    json::Value object = json::Value::emptyObject();
    if (!object.set("safety_class", json::Value::string(std::string{toWireString(safetyClass)}))
             .has_value()) {
        return err(GovernanceError::MalformedComplianceProgram);
    }

    auto appendAll = [&object](std::string key, const auto& records) -> bool {
        json::Value array = json::Value::array({});
        for (const auto& record : records) {
            auto encoded = record.toJson();
            if (!encoded.has_value() || !array.push(std::move(*encoded)).has_value()) {
                return false;
            }
        }
        return object.set(std::move(key), std::move(array)).has_value();
    };

    if (!appendAll("requirements", requirements) || !appendAll("hazards", hazards) ||
        !appendAll("verification_cases", verificationCases) ||
        !appendAll("problem_reports", problemReports) || !appendAll("audit_events", auditEvents) ||
        !appendAll("justifications", justifications)) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return object;
}

Result<std::string, GovernanceError> ComplianceProgram::write() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        // The failure list is deliberately dropped here: a caller that needs to know *why* calls
        // validate() itself, and a release gate always does.
        return err(GovernanceError::MalformedComplianceProgram);
    }
    auto object = toJson();
    if (!object.has_value()) {
        return err(object.error());
    }
    auto text = json::write(*object);
    if (!text.has_value()) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return *text;
}

namespace {

/// Reads one array member into `out`, using `Record::fromJson` for each element.
template <typename Record>
[[nodiscard]] ResultVoid<GovernanceError> readRecords(const json::Value& document,
                                                      std::string_view key,
                                                      std::vector<Record>& out) noexcept {
    const auto member = document.require(key);
    if (!member.has_value() || (*member)->kind() != json::Value::Kind::Array) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    for (const json::Value& element : (*member)->elements()) {
        auto record = Record::fromJson(element);
        if (!record.has_value()) {
            return err(record.error());
        }
        out.push_back(std::move(*record));
    }
    return {};
}

}  // namespace

Result<ComplianceProgram, GovernanceError> ComplianceProgram::parse(std::string_view text) noexcept {
    const auto document = json::parse(text);
    if (!document.has_value()) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    // safety_class plus the six record collections. Every member is always written, so an
    // absent one is a malformed program rather than an empty collection.
    if (!detail::hasExactly(*document, 7)) {
        return err(GovernanceError::MalformedComplianceProgram);
    }

    ComplianceProgram result;

    auto safetyClass =
        detail::requireString(*document, "safety_class", GovernanceError::MalformedComplianceProgram);
    if (!safetyClass.has_value()) {
        return err(safetyClass.error());
    }
    const auto parsedSafetyClass = safetyClassFromWire(*safetyClass);
    if (!parsedSafetyClass.has_value()) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    result.safetyClass = *parsedSafetyClass;

    if (auto read = readRecords(*document, "requirements", result.requirements); !read.has_value()) {
        return err(read.error());
    }
    if (auto read = readRecords(*document, "hazards", result.hazards); !read.has_value()) {
        return err(read.error());
    }
    if (auto read = readRecords(*document, "verification_cases", result.verificationCases);
        !read.has_value()) {
        return err(read.error());
    }
    if (auto read = readRecords(*document, "problem_reports", result.problemReports);
        !read.has_value()) {
        return err(read.error());
    }
    if (auto read = readRecords(*document, "audit_events", result.auditEvents); !read.has_value()) {
        return err(read.error());
    }

    // Justification predates the other records and has no fromJson(Value) - it parses from text.
    // Re-serializing each element is the cost of not duplicating its reader here; a program is
    // parsed once at gate time, so the cost does not matter and one reader that both paths share
    // does.
    const auto justificationsMember = document->require("justifications");
    if (!justificationsMember.has_value() ||
        (*justificationsMember)->kind() != json::Value::Kind::Array) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    for (const json::Value& element : (*justificationsMember)->elements()) {
        const auto elementText = json::write(element);
        if (!elementText.has_value()) {
            return err(GovernanceError::MalformedComplianceProgram);
        }
        auto justification = Justification::parse(*elementText);
        if (!justification.has_value()) {
            return err(justification.error());
        }
        result.justifications.push_back(std::move(*justification));
    }

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(GovernanceError::MalformedComplianceProgram);
    }
    return result;
}

}  // namespace mdux::governance
