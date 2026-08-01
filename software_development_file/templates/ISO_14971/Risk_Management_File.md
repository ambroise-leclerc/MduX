# Risk Management File

> Template — ISO 14971:2019. Fill in every `[ ... ]` placeholder. See
> `docs/iso14971/README.md` for the underlying clause-by-clause
> guidance, and
> `docs/iec62304/05-risk-management-process.md`
> for the software-specific slice of this process. See
> `software_development_file/regulatory/ISO_14971/Risk_Management_File.md` (issue #38, a later PR
> in this stack) for MduX's own filled-in example.

## Document control

- **Product / software item:** [ ... ]
- **Version:** [ ... ]
- **Author(s):** [ ... ]
- **Date:** [ YYYY-MM-DD ]
- **Approval:** [ ... ]

## 1. Risk management plan summary

> `ISO 14971:2019 §4.4 Risk management plan`

[ Scope, criteria for risk acceptability, who's responsible. ]

## 2. Risk analysis

> `ISO 14971:2019 §5.4 Identification of hazards and hazardous situations`

**Point at your hazard records; do not restate them here.** [ Intended use/misuse, identified
hazards and hazardous situations, estimated risk per hazard — reference the `Hazard` records a
`mdux::governance::ComplianceProgram` (issue #34) tracks, rather than copying them into
prose that can drift out of sync. ]

## 3. Risk evaluation

> `ISO 14971:2019 §6 Risk evaluation`

[ Which estimated risks are acceptable as-is, and which require control. ]

## 4. Risk control

> `ISO 14971:2019 §7.3 Implementation of risk control measures`

[ Risk control measures selected, implemented, and verified. Where a control is a software measure,
cross-reference the `Requirement`(s) it corresponds to and confirm `Hazard.controlledBy` names
them — `ComplianceProgram::validate()` (issue #35) rejects a `Hazard` with no controlling
`Requirement`, so this cross-reference is checked mechanically, not only by review. ]

## 5. Overall residual risk evaluation

> `ISO 14971:2019 §8 Evaluation of overall residual risk`

[ Is overall residual risk acceptable, considering all controls together? ]

## 6. Risk management review

> `ISO 14971:2019 §9 Risk management review`

[ Confirms the plan was executed, residual risks are acceptable, and monitoring for production/
post-production information is in place. Reference `releaseEvidenceSummary()`
(`mdux.governance`, issue #34) as the mechanical check that every `Requirement` is
verified and no `VerificationCase` failed, rather than re-deriving that conclusion by hand. ]

## 7. Production and post-production activities

> `ISO 14971:2019 §10.2 Collection of information`

[ How field information (complaints, incidents, near-misses) feeds back into this file — e.g. as
new `ProblemReport` entries (`mdux.governance`, issue #34). ]

## Justification records

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "ISO 14971:2019",
  "clause_ref": "ISO 14971:2019 §7.3 Implementation of risk control measures",
  "rationale": "[ ... ]",
  "requirement_id": "[ optional mdux.governance Requirement id, e.g. REQ-EXAMPLE-001 ]",
  "evidence_refs": ["[ ... ]"]
}
```
