/**
 * @file Compliance.cppm
 * @brief Governed-zone requirement/verification/hazard graph, and the two pure exports IEC 62304
 *        §5.8's release review reads.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON, no I/O in governed code)
 *
 * Part of MduXCore. A `ComplianceProgram` is the in-memory shape of one release's requirement
 * set, the verification cases that discharge each requirement, the hazards those requirements
 * control, the problem reports still open against it, and the digests of every committed
 * `generated/` artifact. `traceabilityMatrix()` and `releaseEvidenceSummary()` are pure functions
 * over that shape - assembling the program (reading `docs/`, walking `generated/`, hashing files)
 * is the caller's job, the same split ADR-007 already draws between a baker and
 * `mdux.evidence.report`.
 *
 * ## Gaps are rows, not omissions
 *
 * `traceabilityMatrix()` does not gate on `validate()` and never skips a Requirement with no
 * VerificationCase - it emits that requirement with an empty `verification_cases` array, because
 * the question an auditor asks of this document is "what is *not* covered", and an omitted row
 * cannot answer that. `ComplianceProgram::validate()` is the thing that turns that same gap into
 * a build failure; the export and the gate look at the same data and deliberately disagree about
 * what to do with a gap.
 */
module;

export module mdux.governance.compliance;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

export namespace mdux::governance {

enum class ComplianceError : std::uint8_t {
    EmptyRequirementId,
    DuplicateRequirementId,
    EmptyVerificationCaseId,
    DuplicateVerificationCaseId,
    DanglingVerificationCaseRequirement,  ///< requirementId names no Requirement in the program
    EmptyHazardId,
    DuplicateHazardId,
    HazardMissingControl,                 ///< controlledBy is empty
    DanglingHazardControl,                ///< a controlledBy entry names no Requirement
    EmptyProblemReportId,
    DuplicateProblemReportId,
    UnverifiedRequirement,                ///< no VerificationCase discharges this Requirement
    MalformedCompliance,                  ///< internal JSON assembly had an unexpected shape
};

[[nodiscard]] std::string_view describe(ComplianceError error) noexcept;

/// One requirement in the program. `requirementId` is the same `REQ-*` identifier
/// `Justification::requirementId` cites.
struct Requirement {
    std::string requirementId;
    std::string description;
};

/// A single verification activity discharging one Requirement.
struct VerificationCase {
    std::string caseId;
    std::string requirementId;  ///< the Requirement this case discharges
    std::vector<std::string> evidenceRefs;
    bool passed{false};
};

/**
 * @brief A hazard controlled by one or more Requirements.
 *
 * Mirrors the ISO 14971 control-measure link without duplicating the risk file itself - this
 * only records which requirement IDs close a hazard, not the hazard's own risk analysis.
 */
struct Hazard {
    std::string hazardId;
    std::string description;
    std::vector<std::string> controlledBy;  ///< Requirement IDs
};

/// An open or closed problem report, IEC 62304 §9's anomaly record.
struct ProblemReport {
    std::string reportId;
    std::string description;
    bool open{true};
};

/**
 * @brief The whole requirement/verification/hazard/problem-report graph for one release, plus
 *        the digests of every committed `generated/` artifact.
 *
 * Assembled entirely by the caller - this type does no I/O and reads nothing from disk itself,
 * per ADR-007's split between assembling evidence and exporting it.
 */
struct ComplianceProgram {
    std::vector<Requirement> requirements;
    std::vector<VerificationCase> verificationCases;
    std::vector<Hazard> hazards;
    std::vector<ProblemReport> problemReports;
    std::vector<evidence::FileRecord> generatedArtifacts;

    /**
     * @brief Checks the invariants a release must satisfy: every Requirement is discharged by at
     *        least one VerificationCase, every Hazard names at least one controlling Requirement,
     *        every cross-reference resolves to a Requirement that exists, and no collection
     *        holds a duplicate or empty ID.
     */
    [[nodiscard]] mdux::core::ResultVoid<ComplianceError> validate() const noexcept;
};

/**
 * @brief Requirement -> VerificationCase -> evidence_refs[], one row per Requirement, sorted by
 *        requirement ID for a byte-stable export regardless of assembly order.
 *
 * Does not gate on validate() and does not skip a Requirement with no VerificationCase - see the
 * module comment. Fails only on an internal assembly problem, never on a coverage gap.
 */
[[nodiscard]] mdux::core::Result<evidence::json::Value, ComplianceError> traceabilityMatrix(
    const ComplianceProgram& program) noexcept;

/**
 * @brief The IEC 62304 §5.8 release view: whether every Requirement is verified and every
 *        VerificationCase discharging it passed, the still-open ProblemReports, and the digest
 *        of every committed `generated/` artifact the caller supplied.
 *
 * Also does not gate on validate() - a release with gaps still gets a summary naming them, rather
 * than no summary at all.
 */
[[nodiscard]] mdux::core::Result<evidence::json::Value, ComplianceError> releaseEvidenceSummary(
    const ComplianceProgram& program) noexcept;

}  // namespace mdux::governance
