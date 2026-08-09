# ISO 13485:2016 quality management system — MduX's scope note

> Filled-in example for MduX itself. See
> `software_development_file/templates/ISO_13485/README.md` for the blank template, and
> `docs/iso13485/README.md` for the underlying clause-by-clause guidance.

MduX is a software library, not a manufacturer, and does not operate a quality management system of
its own that a notified body would audit. This document states, plainly, which of this project's
engineering artifacts a manufacturer's ISO 13485 QMS can draw on, and which parts of §4-§8 remain
entirely the manufacturer's own responsibility.

## A discrepancy this document does not repeat

Root `README.md`'s "Implementation Status" table marks "Risk Management System", "Quality
Management System", "Software Lifecycle Framework", "Design History File (DHF)", and "Risk
Management File (RMF)" as **Completed**, and its "Framework Integration and Usage" section shows a
`#include "mdux/compliance/MedicalDevice.cppm"` / `mdux::MedicalDeviceContext` code sample. **Neither
that header nor that type exists anywhere in this repository** — `AGENTS.md` § 2 already flags
README's claims as partly aspirational, and this document does not repeat them as fact. What
actually exists, verified in the tree as of this PR, is listed below.

## What MduX's engineering artifacts can feed into your QMS

> `ISO 13485:2016 §4.2 Documentation requirements`, `§7.3 Design and development`

- **Design and development traceability** — `mdux.governance.compliance::traceabilityMatrix()` /
  `releaseEvidenceSummary()` (`include/mdux/governance/Compliance.cppm`, issue #35) generate a
  requirement → verification → evidence matrix directly from typed data, once a `ComplianceProgram`
  is actually populated. As of this document, none is — the types and their tests exist; a real,
  filled program for MduX itself does not yet.
- **Document/record control for generated evidence** — `mdux.evidence.report`'s `BakeReport`/
  `PackageHeader` shape (issue #12, `docs/adr/ADR-007-evidence-pipeline-doctrine.md`) is designed
  for a committed `report.json` per baked asset with a re-verifiable SHA-256 digest. No baker exists
  yet (issues #13-18 are open) to produce one.
- **Design review record** — the ADR trail (`docs/adr/README.md`, 7 ADRs at time of writing) is a
  dated record of design decisions and their rationale, usable as supporting evidence for a design
  review under §7.3.4.
- **Purchasing/supplier information for SOUP** — `docs/governance/soup-register.toml` records
  supplier, license, and support-model information per third-party or build-tool dependency,
  relevant to §7.4's purchasing controls if a manufacturer treats MduX's SOUP as purchased/acquired
  product.

## What your QMS must still supply

- Management responsibility, quality policy, and management review (§5) — MduX has no concept of
  an organization's management structure.
- Human resources, infrastructure, and work-environment controls (§6).
- Customer-related processes and regulatory-submission-facing product realization steps (§7.2, most
  of §7.5-§7.6) beyond the design-and-development traceability noted above.
- Complaint handling, adverse-event/vigilance reporting, and CAPA (§8.2, §8.5) — `ProblemReport`
  (`mdux.governance`) can record that a problem exists and whether it's closed, but
  implements none of the regulatory reporting obligations §8.2.3 describes, and no instance of it
  has been populated for MduX itself.
- Everything about the manufacturer's own device beyond what's built with MduX's rendering/
  governance types.
- A correction to root `README.md`'s "Implementation Status" table and code sample — tracked
  separately, not resolved by this document.

## Justification records

```jsonc
{
  "justification_id": "JUS-020",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §7.3 Design and development",
  "rationale": "mdux.governance.compliance::traceabilityMatrix() is designed to generate a requirement-to-verification-to-evidence matrix from typed data rather than a hand-maintained spreadsheet, but as of this document no ComplianceProgram has actually been populated for MduX itself - the mechanism exists and is tested, its application to this project's own release does not yet.",
  "evidence_refs": [
    "include/mdux/governance/Compliance.cppm",
    "tests/governance/ComplianceTests.cpp"
  ]
}
```
