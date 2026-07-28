# Risk Management File — MduX

> Filled-in example for MduX itself. See
> `software_development_file/templates/ISO_14971/Risk_Management_File.md` for the blank template,
> `docs/iso14971/README.md` for the underlying clause-by-clause guidance, and
> `docs/iec62304/05-risk-management-process.md` for the software-specific slice of this process.

## Document control

- **Product / software item:** MduX
- **Scope note:** this file documents how MduX's `mdux.governance.compliance` types and design
  mechanisms are *intended* to support a manufacturer's own ISO 14971 risk management file for a
  device built on MduX — it is not a risk management file for a specific finished device, since
  MduX has no clinical intended use of its own, **and, as stated throughout, no actual risk data has
  been recorded for MduX using these types yet.** Treat every section below as a description of a
  mechanism, not as evidence the mechanism has been exercised.

## 1. Risk management plan summary

> `ISO 14971:2019 §4.4 Risk management plan`

An application sets `mdux::ComplianceMetadata.deviceClass` to a free-form string
(`include/mdux/mdux.cppm`) intended to record a safety classification (Class A/B/C — MduX keeps
Class A in scope, unlike TrustSC's Class B/C-only model). **This is not a checked enum**: nothing in
MduX validates that the value is one of `"A"`/`"B"`/`"C"`, and nothing ties it to how much of
`ComplianceProgram::validate()`'s enforcement applies, the way TrustSC's `SafetyClass` does. A
manufacturer should not assume setting `deviceClass` triggers any classification-dependent behavior.

## 2. Risk analysis

> `ISO 14971:2019 §5.4 Identification of hazards and hazardous situations`

`mdux::governance::Hazard { hazardId, description, controlledBy }` (`mdux.governance.compliance`,
issue #35) is the type meant to record the outcome of a manufacturer's hazard analysis —
`ComplianceProgram::validate()` rejects a `Hazard` with an empty `controlledBy` list, so a hazard
cannot be recorded without at least one `Requirement` addressing it, and rejects a `controlledBy`
entry naming a `Requirement` that doesn't exist (`DanglingHazardControl`). **No worked example
exists.** Unlike TrustSC's `examples/class_c_monitor`, MduX has no example application demonstrating
a real hazard, a real requirement, or a real verification case — `tests/governance/ComplianceTests.cpp`'s
fixtures (`HAZ-001`, `REQ-001`/`REQ-002`) are synthetic test data, not a real risk analysis for this
project.

## 3. Risk evaluation

> `ISO 14971:2019 §6 Risk evaluation`

Not automated by `mdux.governance.compliance` — deciding whether an estimated risk is acceptable
as-is is the manufacturer's clinical/regulatory judgment. The types record the *outcome* of that
judgment (which hazards have controls) but do not perform the evaluation itself.

## 4. Risk control

> `ISO 14971:2019 §7.3 Implementation of risk control measures`

Every risk control measure is required to exist as an actual `Requirement`
(`include/mdux/governance/Compliance.cppm`), not a free-floating note — `Hazard.controlledBy` is a
list of requirement IDs. Each such `Requirement` must in turn have at least one `VerificationCase`
discharging it (`ComplianceProgram::validate()`'s `UnverifiedRequirement` check), so a risk control
measure cannot pass structural validation without a defined verification activity. That check does
not prove the activity passed: `VerificationCase.passed` is evaluated separately by
`releaseEvidenceSummary()`, and the caller must also supply the actual `evidenceRefs`.

## 5. Overall residual risk evaluation

> `ISO 14971:2019 §8 Evaluation of overall residual risk`

Not automated — `releaseEvidenceSummary()` gives a release-time snapshot (`requirements_total`,
`requirements_verified`, `validation_passed`) useful as an input to this judgment, but the judgment
itself — whether overall residual risk is acceptable — remains the manufacturer's.

## 6. Risk management review

> `ISO 14971:2019 §9 Risk management review`

No audit-trail/event-sequencing type exists in `mdux.governance.compliance` today (unlike, for
example, TrustSC's `AuditEvent` trail) — this is a real gap relative to what a full risk management
review would want, not an oversight in this document. A manufacturer relying on a chronological
record of when each hazard, requirement, and verification was added must build that themselves,
e.g. from git history of the file(s) that construct their `ComplianceProgram`.

## 7. Production and post-production activities

> `ISO 14971:2019 §10.2 Collection of information`

`ProblemReport { reportId, description, open }` (`mdux.governance.compliance`) is where field
information (via a manufacturer's own complaint/incident intake) would be recorded once triaged —
this project provides the record type, not the intake process, and not a populated instance for
MduX itself.

## Justification records

```jsonc
{
  "justification_id": "JUS-021",
  "standard": "ISO 14971:2019",
  "clause_ref": "ISO 14971:2019 §7.3 Implementation of risk control measures",
  "rationale": "Hazard.controlledBy requires at least one Requirement that must exist (DanglingHazardControl), and that Requirement in turn requires at least one VerificationCase (ComplianceProgram::validate()'s UnverifiedRequirement check). This mechanically guarantees a defined verification activity; releaseEvidenceSummary() separately reports whether its passed flag is true.",
  "evidence_refs": [
    "include/mdux/governance/Compliance.cppm",
    "src/governance/Compliance.cpp",
    "tests/governance/ComplianceTests.cpp"
  ]
}
```
