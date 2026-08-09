/**
 * @file Compliance.cpp
 * @brief The two release-evidence exports, over the governance model in Governance.cppm.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * These functions and `ComplianceProgram::validate()` deliberately read the same data and
 * disagree about what to do with a gap - see the module comment in Compliance.cppm before
 * changing either.
 *
 * Every member name emitted here matches the field it comes from in the schemas under
 * `docs/iec62304/schemas/`, so a row of the matrix and the record it was built from can be read
 * side by side. `tools/docs-lint/check_schema_type_drift.py` keeps those schemas and the C++
 * types aligned; this file is what makes the export agree with both.
 */
module;

module mdux.governance.compliance;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.governance;

namespace mdux::governance {

using mdux::core::err;
using mdux::core::Result;
namespace json = evidence::json;

namespace {

constexpr GovernanceError kMalformed = GovernanceError::MalformedComplianceProgram;

/// Sets a member, reporting the allocation-shaped failure `set()` can return. Every caller
/// collapses that to `MalformedComplianceProgram`: an export that cannot assemble its own object
/// has no partial result worth returning.
[[nodiscard]] bool setMember(json::Value& object, std::string key, json::Value value) noexcept {
    return object.set(std::move(key), std::move(value)).has_value();
}

[[nodiscard]] Result<json::Value, GovernanceError> stringArray(
    std::span<const std::string> values) noexcept {
    json::Value array = json::Value::array({});
    for (const std::string& value : values) {
        if (auto pushed = array.push(json::Value::string(value)); !pushed.has_value()) {
            return err(kMalformed);
        }
    }
    return array;
}

/// Pointers into `values`, ordered by `key`. Sorting pointers rather than copies keeps the export
/// byte-stable regardless of the order the caller assembled the program in, without duplicating
/// the records to do it.
template <typename Record, typename KeyFn>
[[nodiscard]] std::vector<const Record*> sortedBy(std::span<const Record> values, KeyFn key) {
    std::vector<const Record*> pointers;
    pointers.reserve(values.size());
    for (const Record& value : values) {
        pointers.push_back(&value);
    }
    std::ranges::sort(pointers, {}, [&key](const Record* record) { return key(*record); });
    return pointers;
}

}  // namespace

Result<json::Value, GovernanceError> traceabilityMatrix(const ComplianceProgram& program) noexcept {
    const auto requirements = sortedBy<Requirement>(
        program.requirements, [](const Requirement& r) { return std::string_view{r.id}; });

    json::Value rows = json::Value::array({});
    for (const Requirement* requirement : requirements) {
        auto cases = sortedBy<VerificationCase>(
            program.verificationCases,
            [](const VerificationCase& c) { return std::string_view{c.id}; });
        std::erase_if(cases, [requirement](const VerificationCase* c) {
            return c->requirementId != requirement->id;
        });

        json::Value row = json::Value::emptyObject();
        // source_clause is the member that makes this a regulatory traceability matrix rather
        // than a coverage report: it answers "which clause is this requirement here for" without
        // a second lookup.
        if (!setMember(row, "requirement_id", json::Value::string(requirement->id)) ||
            !setMember(row, "title", json::Value::string(requirement->title)) ||
            !setMember(row, "source_clause", json::Value::string(requirement->sourceClause)) ||
            !setMember(row, "verification_intent",
                       json::Value::string(requirement->verificationIntent))) {
            return err(kMalformed);
        }

        // A Requirement with no cases still gets its row pushed below, with this array empty -
        // see the "gaps are rows, not omissions" module comment.
        json::Value caseArray = json::Value::array({});
        for (const VerificationCase* verificationCase : cases) {
            auto refs = stringArray(verificationCase->evidenceRefs);
            if (!refs.has_value()) {
                return err(refs.error());
            }
            json::Value caseObject = json::Value::emptyObject();
            // No requirement_id here: it is the row's key, and repeating it inside every nested
            // case would let the two disagree.
            if (!setMember(caseObject, "id", json::Value::string(verificationCase->id)) ||
                !setMember(
                    caseObject, "method",
                    json::Value::string(std::string{toWireString(verificationCase->method)})) ||
                !setMember(caseObject, "evidence_refs", std::move(*refs)) ||
                !setMember(caseObject, "passed", json::Value::boolean(verificationCase->passed))) {
                return err(kMalformed);
            }
            if (auto pushed = caseArray.push(std::move(caseObject)); !pushed.has_value()) {
                return err(kMalformed);
            }
        }
        if (!setMember(row, "verification_cases", std::move(caseArray))) {
            return err(kMalformed);
        }
        if (auto pushed = rows.push(std::move(row)); !pushed.has_value()) {
            return err(kMalformed);
        }
    }
    return rows;
}

Result<json::Value, GovernanceError> releaseEvidenceSummary(
    const ComplianceProgram& program, std::span<const evidence::FileRecord> artifacts) noexcept {
    const std::size_t requirementsTotal = program.requirements.size();
    std::size_t requirementsVerified = 0;
    bool allDischargingCasesPassed = true;
    for (const Requirement& requirement : program.requirements) {
        bool discharged = false;
        for (const VerificationCase& verificationCase : program.verificationCases) {
            if (verificationCase.requirementId == requirement.id) {
                discharged = true;
                if (!verificationCase.passed) {
                    allDischargingCasesPassed = false;
                }
            }
        }
        if (discharged) {
            ++requirementsVerified;
        }
    }

    // `validation_passed` is the release gate, not merely a coverage percentage. Reuse the
    // program's complete invariant check so a dangling reference, an uncontrolled hazard, a
    // duplicate id or a malformed record cannot be reported as a passing release just because
    // every Requirement happens to have a VerificationCase.
    const auto validation = program.validate();
    const bool validationPassed =
        validation.has_value() && allDischargingCasesPassed &&
        requirementsVerified == requirementsTotal;

    json::Value summary = json::Value::emptyObject();
    if (!setMember(summary, "validation_passed", json::Value::boolean(validationPassed)) ||
        !setMember(summary, "safety_class",
                   json::Value::string(std::string{toWireString(program.safetyClass)})) ||
        !setMember(summary, "requirements_total",
                   json::Value::unsignedInteger(requirementsTotal)) ||
        !setMember(summary, "requirements_verified",
                   json::Value::unsignedInteger(requirementsVerified))) {
        return err(kMalformed);
    }

    // Every reason the gate failed, not merely the fact that it did. A release review told only
    // "validation_passed: false" has to re-run validate() to learn anything; carrying the failure
    // list is what makes this summary usable on its own.
    json::Value failures = json::Value::array({});
    if (!validation.has_value()) {
        for (const ValidationFailure& failure : validation.error()) {
            json::Value object = json::Value::emptyObject();
            if (!setMember(object, "code",
                           json::Value::string(std::string{describe(failure.code)})) ||
                !setMember(object, "subject", json::Value::string(failure.subject)) ||
                !setMember(object, "detail", json::Value::string(failure.detail))) {
                return err(kMalformed);
            }
            if (auto pushed = failures.push(std::move(object)); !pushed.has_value()) {
                return err(kMalformed);
            }
        }
    }
    if (!setMember(summary, "validation_failures", std::move(failures))) {
        return err(kMalformed);
    }

    // A failed case is intentionally not a ComplianceProgram structural-validation failure, but
    // it still makes validation_passed false. Name those cases explicitly so the summary carries
    // every reason its own release gate failed.
    auto failedCases = sortedBy<VerificationCase>(
        program.verificationCases,
        [](const VerificationCase& verificationCase) {
            return std::string_view{verificationCase.id};
        });
    std::erase_if(failedCases,
                  [](const VerificationCase* verificationCase) {
                      return verificationCase->passed;
                  });
    json::Value failedCaseIds = json::Value::array({});
    for (const VerificationCase* verificationCase : failedCases) {
        if (auto pushed = failedCaseIds.push(json::Value::string(verificationCase->id));
            !pushed.has_value()) {
            return err(kMalformed);
        }
    }
    if (!setMember(summary, "failed_verification_cases", std::move(failedCaseIds))) {
        return err(kMalformed);
    }

    auto openReports = sortedBy<ProblemReport>(
        program.problemReports, [](const ProblemReport& r) { return std::string_view{r.id}; });
    std::erase_if(openReports, [](const ProblemReport* report) { return report->closed; });

    json::Value openArray = json::Value::array({});
    for (const ProblemReport* report : openReports) {
        json::Value object = json::Value::emptyObject();
        // affects_risk is carried because IEC 62304 §9.4 makes an open problem that could affect
        // safety a different kind of release blocker from one that could not.
        if (!setMember(object, "id", json::Value::string(report->id)) ||
            !setMember(object, "description", json::Value::string(report->description)) ||
            !setMember(object, "affects_risk", json::Value::boolean(report->affectsRisk))) {
            return err(kMalformed);
        }
        if (auto pushed = openArray.push(std::move(object)); !pushed.has_value()) {
            return err(kMalformed);
        }
    }
    if (!setMember(summary, "open_problem_reports", std::move(openArray))) {
        return err(kMalformed);
    }

    const auto sortedArtifacts = sortedBy<evidence::FileRecord>(
        artifacts, [](const evidence::FileRecord& record) { return std::string_view{record.path}; });
    json::Value artifactArray = json::Value::array({});
    for (const evidence::FileRecord* record : sortedArtifacts) {
        const std::array<char, 64> hex = evidence::toHex(record->sha256);
        json::Value object = json::Value::emptyObject();
        if (!setMember(object, "path", json::Value::string(record->path)) ||
            !setMember(object, "sha256",
                       json::Value::string(std::string{hex.data(), hex.size()}))) {
            return err(kMalformed);
        }
        if (auto pushed = artifactArray.push(std::move(object)); !pushed.has_value()) {
            return err(kMalformed);
        }
    }
    if (!setMember(summary, "generated_artifacts", std::move(artifactArray))) {
        return err(kMalformed);
    }

    return summary;
}

}  // namespace mdux::governance
