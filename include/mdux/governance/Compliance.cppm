/**
 * @file Compliance.cppm
 * @brief The two pure exports IEC 62304 §5.8's release review reads, over the governance model.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON, no I/O in governed code)
 *
 * Part of MduXCore. `traceabilityMatrix()` and `releaseEvidenceSummary()` are pure functions over
 * a `mdux::governance::ComplianceProgram` - the one defined in
 * [Governance.cppm](Governance.cppm) - as issue #35 specifies. Assembling that program (reading
 * `docs/`, walking `generated/`, hashing files) is the caller's job, the same split ADR-007
 * already draws between a baker and `mdux.evidence.report`.
 *
 * ## Why this module defines no types of its own
 *
 * It used to. An earlier version declared its own `Requirement`, `Hazard`, `VerificationCase`,
 * `ProblemReport` and `ComplianceProgram` in this same namespace, because it was written against a
 * version of the governance module that had only `Justification`. When the fuller model landed,
 * the two sets collided: same namespace, same names, different fields (`requirementId` against
 * `id`, `caseId` against `id`, `open` against `closed`). They coexisted only because no
 * translation unit imported both, and the exports emitted member names that contradicted the
 * schemas under `docs/iec62304/schemas/`.
 *
 * More seriously, the local `Requirement` carried only an id and a description - so the
 * traceability matrix could not name the clause a requirement came from, which is the one thing a
 * regulatory traceability matrix exists to show. This module now imports the model rather than
 * restating it, and the matrix carries `source_clause`.
 *
 * ## Gaps are rows, not omissions
 *
 * `traceabilityMatrix()` does not gate on `validate()` and never skips a `Requirement` with no
 * `VerificationCase` - it emits that requirement with an empty `verification_cases` array,
 * because the question an auditor asks of this document is "what is *not* covered", and an
 * omitted row cannot answer that. `ComplianceProgram::validate()` is the thing that turns the
 * same gap into a build failure; the export and the gate look at the same data and deliberately
 * disagree about what to do with a gap.
 */
module;

export module mdux.governance.compliance;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.governance;

export namespace mdux::governance {

/**
 * @brief Requirement → VerificationCase → evidence_refs[], one row per Requirement, sorted by
 *        requirement id for a byte-stable export regardless of assembly order.
 *
 * Each row carries the requirement's `source_clause`, so the matrix answers "which clause is this
 * requirement here for" without a second lookup - the property that makes it a *regulatory*
 * traceability matrix rather than a coverage report.
 *
 * Does not gate on validate() and does not skip a Requirement with no VerificationCase - see the
 * module comment. Fails only on an internal assembly problem, never on a coverage gap.
 */
[[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> traceabilityMatrix(
    const ComplianceProgram& program) noexcept;

/**
 * @brief The IEC 62304 §5.8 release view: whether the program validates and every discharging
 *        VerificationCase passed, the validation failures and failed case ids, the still-open
 *        ProblemReports, and the digest of every committed `generated/` artifact the caller
 *        supplied.
 *
 * `artifacts` is a parameter rather than a field on `ComplianceProgram` on purpose. A digest of a
 * built file is evidence *about a build*, not a governance record: it is not authored, not
 * reviewed, and not round-tripped through the program's JSON form. Keeping it out preserves
 * `ComplianceProgram::parse()`'s strict member set, and matches ADR-007's rule that assembling
 * evidence is the caller's job.
 *
 * Also does not gate on validate() - a release with gaps still gets a summary naming them, rather
 * than no summary at all.
 */
[[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> releaseEvidenceSummary(
    const ComplianceProgram& program,
    std::span<const evidence::FileRecord> artifacts) noexcept;

}  // namespace mdux::governance
