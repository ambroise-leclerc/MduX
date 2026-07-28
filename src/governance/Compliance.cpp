/**
 * @file Compliance.cpp
 * @brief Requirement/verification/hazard graph validation and the two release-evidence exports.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * validate() and the two export functions deliberately read the same data and disagree about
 * what to do with a gap - see the module comment in Compliance.cppm before changing either.
 */
module;

module mdux.governance.compliance;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

namespace mdux::governance {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = evidence::json;

std::string_view describe(ComplianceError error) noexcept {
    switch (error) {
    case ComplianceError::EmptyRequirementId:
        return "requirementId is empty";
    case ComplianceError::DuplicateRequirementId:
        return "requirements contains a duplicate requirementId";
    case ComplianceError::EmptyVerificationCaseId:
        return "caseId is empty";
    case ComplianceError::DuplicateVerificationCaseId:
        return "verificationCases contains a duplicate caseId";
    case ComplianceError::DanglingVerificationCaseRequirement:
        return "a VerificationCase names a requirementId that does not exist";
    case ComplianceError::EmptyHazardId:
        return "hazardId is empty";
    case ComplianceError::DuplicateHazardId:
        return "hazards contains a duplicate hazardId";
    case ComplianceError::HazardMissingControl:
        return "a Hazard has an empty controlledBy list";
    case ComplianceError::DanglingHazardControl:
        return "a Hazard's controlledBy names a requirementId that does not exist";
    case ComplianceError::EmptyProblemReportId:
        return "reportId is empty";
    case ComplianceError::DuplicateProblemReportId:
        return "problemReports contains a duplicate reportId";
    case ComplianceError::UnverifiedRequirement:
        return "a Requirement has no discharging VerificationCase";
    case ComplianceError::MalformedCompliance:
        return "internal JSON assembly had an unexpected shape";
    }
    return "unrecognized compliance error";
}

namespace {

[[nodiscard]] bool requirementExists(const ComplianceProgram& program, std::string_view id) noexcept {
    return std::ranges::any_of(
        program.requirements, [&](const Requirement& r) { return r.requirementId == id; });
}

}  // namespace

ResultVoid<ComplianceError> ComplianceProgram::validate() const noexcept {
    for (std::size_t i = 0; i < requirements.size(); ++i) {
        if (requirements[i].requirementId.empty()) {
            return err(ComplianceError::EmptyRequirementId);
        }
        for (std::size_t k = i + 1; k < requirements.size(); ++k) {
            if (requirements[i].requirementId == requirements[k].requirementId) {
                return err(ComplianceError::DuplicateRequirementId);
            }
        }
    }

    for (std::size_t i = 0; i < verificationCases.size(); ++i) {
        const VerificationCase& current = verificationCases[i];
        if (current.caseId.empty()) {
            return err(ComplianceError::EmptyVerificationCaseId);
        }
        for (std::size_t k = i + 1; k < verificationCases.size(); ++k) {
            if (current.caseId == verificationCases[k].caseId) {
                return err(ComplianceError::DuplicateVerificationCaseId);
            }
        }
        if (!requirementExists(*this, current.requirementId)) {
            return err(ComplianceError::DanglingVerificationCaseRequirement);
        }
    }

    for (std::size_t i = 0; i < hazards.size(); ++i) {
        const Hazard& hazard = hazards[i];
        if (hazard.hazardId.empty()) {
            return err(ComplianceError::EmptyHazardId);
        }
        for (std::size_t k = i + 1; k < hazards.size(); ++k) {
            if (hazard.hazardId == hazards[k].hazardId) {
                return err(ComplianceError::DuplicateHazardId);
            }
        }
        // Checked before dangling references: an empty list has nothing to dangle, and the two
        // failure modes should never be conflated into one message.
        if (hazard.controlledBy.empty()) {
            return err(ComplianceError::HazardMissingControl);
        }
        for (const std::string& requirementId : hazard.controlledBy) {
            if (!requirementExists(*this, requirementId)) {
                return err(ComplianceError::DanglingHazardControl);
            }
        }
    }

    for (std::size_t i = 0; i < problemReports.size(); ++i) {
        if (problemReports[i].reportId.empty()) {
            return err(ComplianceError::EmptyProblemReportId);
        }
        for (std::size_t k = i + 1; k < problemReports.size(); ++k) {
            if (problemReports[i].reportId == problemReports[k].reportId) {
                return err(ComplianceError::DuplicateProblemReportId);
            }
        }
    }

    for (const Requirement& requirement : requirements) {
        const bool discharged = std::ranges::any_of(verificationCases, [&](const VerificationCase& vc) {
            return vc.requirementId == requirement.requirementId;
        });
        if (!discharged) {
            return err(ComplianceError::UnverifiedRequirement);
        }
    }

    return {};
}

Result<json::Value, ComplianceError> traceabilityMatrix(const ComplianceProgram& program) noexcept {
    std::vector<const Requirement*> sortedRequirements;
    sortedRequirements.reserve(program.requirements.size());
    for (const Requirement& requirement : program.requirements) {
        sortedRequirements.push_back(&requirement);
    }
    std::ranges::sort(sortedRequirements, {}, [](const Requirement* requirement) {
        return std::string_view{requirement->requirementId};
    });

    json::Value rows = json::Value::array({});
    for (const Requirement* requirement : sortedRequirements) {
        std::vector<const VerificationCase*> cases;
        for (const VerificationCase& verificationCase : program.verificationCases) {
            if (verificationCase.requirementId == requirement->requirementId) {
                cases.push_back(&verificationCase);
            }
        }
        std::ranges::sort(cases, {}, [](const VerificationCase* verificationCase) {
            return std::string_view{verificationCase->caseId};
        });

        json::Value row = json::Value::emptyObject();
        auto setRowMember = [&row](std::string key, json::Value value) -> bool {
            return row.set(std::move(key), std::move(value)).has_value();
        };
        if (!setRowMember("requirement_id", json::Value::string(requirement->requirementId)) ||
            !setRowMember("description", json::Value::string(requirement->description))) {
            return err(ComplianceError::MalformedCompliance);
        }

        // A Requirement with no cases here still gets its row pushed below, with this array
        // empty - see the "gaps are rows, not omissions" module comment.
        json::Value caseArray = json::Value::array({});
        for (const VerificationCase* verificationCase : cases) {
            json::Value caseObject = json::Value::emptyObject();
            auto setCaseMember = [&caseObject](std::string key, json::Value value) -> bool {
                return caseObject.set(std::move(key), std::move(value)).has_value();
            };
            if (!setCaseMember("case_id", json::Value::string(verificationCase->caseId)) ||
                !setCaseMember("passed", json::Value::boolean(verificationCase->passed))) {
                return err(ComplianceError::MalformedCompliance);
            }
            json::Value refs = json::Value::array({});
            for (const std::string& ref : verificationCase->evidenceRefs) {
                if (auto pushed = refs.push(json::Value::string(ref)); !pushed.has_value()) {
                    return err(ComplianceError::MalformedCompliance);
                }
            }
            if (!setCaseMember("evidence_refs", std::move(refs))) {
                return err(ComplianceError::MalformedCompliance);
            }
            if (auto pushed = caseArray.push(std::move(caseObject)); !pushed.has_value()) {
                return err(ComplianceError::MalformedCompliance);
            }
        }
        if (!setRowMember("verification_cases", std::move(caseArray))) {
            return err(ComplianceError::MalformedCompliance);
        }

        if (auto pushed = rows.push(std::move(row)); !pushed.has_value()) {
            return err(ComplianceError::MalformedCompliance);
        }
    }
    return rows;
}

Result<json::Value, ComplianceError> releaseEvidenceSummary(const ComplianceProgram& program) noexcept {
    const std::size_t requirementsTotal = program.requirements.size();
    std::size_t requirementsVerified = 0;
    bool allDischargingCasesPassed = true;
    for (const Requirement& requirement : program.requirements) {
        bool discharged = false;
        for (const VerificationCase& verificationCase : program.verificationCases) {
            if (verificationCase.requirementId == requirement.requirementId) {
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
    // program's complete invariant check so a dangling reference, uncontrolled hazard, duplicate
    // ID, or malformed problem-report set cannot be reported as a passing release just because
    // every Requirement happens to have a VerificationCase.
    const bool programValid = program.validate().has_value();
    const bool validationPassed =
        programValid && allDischargingCasesPassed && requirementsVerified == requirementsTotal;

    json::Value summary = json::Value::emptyObject();
    auto setMember = [&summary](std::string key, json::Value value) -> bool {
        return summary.set(std::move(key), std::move(value)).has_value();
    };
    if (!setMember("validation_passed", json::Value::boolean(validationPassed)) ||
        !setMember("requirements_total", json::Value::unsignedInteger(requirementsTotal)) ||
        !setMember("requirements_verified", json::Value::unsignedInteger(requirementsVerified))) {
        return err(ComplianceError::MalformedCompliance);
    }

    std::vector<const ProblemReport*> openReports;
    for (const ProblemReport& report : program.problemReports) {
        if (report.open) {
            openReports.push_back(&report);
        }
    }
    std::ranges::sort(openReports, {}, [](const ProblemReport* report) {
        return std::string_view{report->reportId};
    });
    json::Value openArray = json::Value::array({});
    for (const ProblemReport* report : openReports) {
        json::Value object = json::Value::emptyObject();
        if (!object.set("report_id", json::Value::string(report->reportId)).has_value() ||
            !object.set("description", json::Value::string(report->description)).has_value()) {
            return err(ComplianceError::MalformedCompliance);
        }
        if (auto pushed = openArray.push(std::move(object)); !pushed.has_value()) {
            return err(ComplianceError::MalformedCompliance);
        }
    }
    if (!setMember("open_problem_reports", std::move(openArray))) {
        return err(ComplianceError::MalformedCompliance);
    }

    std::vector<const evidence::FileRecord*> artifacts;
    artifacts.reserve(program.generatedArtifacts.size());
    for (const evidence::FileRecord& record : program.generatedArtifacts) {
        artifacts.push_back(&record);
    }
    std::ranges::sort(artifacts, {}, [](const evidence::FileRecord* record) {
        return std::string_view{record->path};
    });
    json::Value artifactArray = json::Value::array({});
    for (const evidence::FileRecord* record : artifacts) {
        json::Value object = json::Value::emptyObject();
        const std::array<char, 64> hex = evidence::toHex(record->sha256);
        if (!object.set("path", json::Value::string(record->path)).has_value() ||
            !object.set("sha256", json::Value::string(std::string{hex.data(), hex.size()}))
                 .has_value()) {
            return err(ComplianceError::MalformedCompliance);
        }
        if (auto pushed = artifactArray.push(std::move(object)); !pushed.has_value()) {
            return err(ComplianceError::MalformedCompliance);
        }
    }
    if (!setMember("generated_artifacts", std::move(artifactArray))) {
        return err(ComplianceError::MalformedCompliance);
    }

    return summary;
}

}  // namespace mdux::governance
