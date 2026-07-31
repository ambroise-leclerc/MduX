# Cybersecurity Software Architecture Design

> Template — IEC 81001-5-1:2021. Fill in every `[ ... ]` placeholder. See
> `docs/iec81001/README.md` for the underlying guidance — read
> that document's confidence note first. **MduX's own corpus for this standard cites only through
> §4 (`IEC 81001-5-1:2021 §4 General requirements`); the practice-level sections below are named by
> category, not by a numbered sub-clause, because this project does not hold a copy of the standard's
> text and does not have verified confidence in its clause numbering past §4 (ADR-006).** Do not
> invent a `§5.x`-style citation for a security practice unless you have checked it against your own
> licensed copy of the standard. See
> `software_development_file/regulatory/IEC_81001/Cybersecurity_SAD.md` (issue #38, a later PR in
> this stack) for MduX's own filled-in example.

## Document control

- **Product / software item:** [ ... ]
- **Version:** [ ... ]
- **Author(s):** [ ... ]
- **Date:** [ YYYY-MM-DD ]
- **Approval:** [ ... ]

## 1. Scope and relationship to the IEC 62304 lifecycle

> `IEC 81001-5-1:2021 §4 General requirements`

[ How does security risk management integrate with this product's IEC 62304 process? Reference the
product's SAD (`software_development_file/.../IEC_62304/SAD.md`). ]

## 2. Security risk management

> IEC 81001-5-1:2021, security risk management practice category — see
> `docs/iec81001/02-security-lifecycle-practices.md`.

[ Threats and vulnerabilities identified, their assessed risk, and the security controls applied.
Cross-reference safety hazards from the ISO 14971 risk management file where a security issue could
also cause safety harm. ]

## 3. Secure by design and secure implementation

> IEC 81001-5-1:2021, secure-by-design and secure-implementation practice categories.

[ Secure-design principles applied — trust boundaries, privilege segregation, memory-safety
guarantees, dependency review policy. If built on MduX, reference the trust-zone split (ADR-004)
and the zero-SOUP scoping decisions recorded in
`docs/governance/soup-register.toml`. ]

## 4. Security verification and validation

> IEC 81001-5-1:2021, security verification and validation practice category.

[ How security controls were verified — testing, static analysis, dependency scanning. ]

## 5. Vulnerability and defect management

> IEC 81001-5-1:2021, vulnerability and defect management practice category.

[ How a discovered vulnerability is triaged, tracked, and closed. If built on MduX, reference how
a `ProblemReport` (`mdux.governance.compliance`, issue #35) records it. ]

## 6. Security update management

> IEC 81001-5-1:2021, security update management practice category.

[ How security updates reach a fielded device. If the product has no network connectivity or
update mechanism, state that explicitly rather than leaving this section silently blank. ]

## 7. Security documentation and guidance

> IEC 81001-5-1:2021, security documentation and guidance practice category.

[ What security-relevant information/guidance is provided to the device's operators/IT
administrators? ]

## Justification records

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "IEC 81001-5-1:2021",
  "clause_ref": "IEC 81001-5-1:2021 §4 General requirements",
  "rationale": "[ ... ]",
  "evidence_refs": ["[ ... ]"]
}
```
