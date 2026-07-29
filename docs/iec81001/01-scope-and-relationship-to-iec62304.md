# IEC 81001-5-1:2021 §1–§3 — Scope, relationship to IEC 62304, terms and definitions

## §1 Scope

IEC 81001-5-1 specifies life cycle requirements for the security of health software and health IT
systems — establishing a secure product development life cycle a manufacturer follows from initial
requirements through post-market maintenance. It sits alongside IEC 62304 (safety life cycle) as a
security-focused counterpart, addressing a device's security posture rather than its functional
safety directly, though the two are related: a security failure can become a safety hazard.

MduX contributes to a device's security posture without running the standard's process itself — it
is a dependency a manufacturer's secure development life cycle would need to account for, not the
life cycle. See [`README.md`](README.md) for what this corpus does and does not cite.

## Relationship to IEC 62304

The two standards describe the same life cycle from two directions, and the practical consequence
is that a manufacturer runs one process, not two.

| IEC 62304 activity | The security counterpart | Where they meet |
|---|---|---|
| §5.1 software development planning | Security planning as part of the same plan | One development plan with security activities in it, not a separate security plan running in parallel |
| §5.2 software requirements analysis | Security requirements, derived from the security risk assessment | A security requirement is a requirement: same `Requirement` record, same verification obligation ([`../iec62304/02-planning-and-requirements.md`](../iec62304/02-planning-and-requirements.md)) |
| §5.3 software architectural design | Secure design — attack surface as an architectural property | [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust zones constrain both at once |
| §7 software risk management | Security risk management | Both feed the device's single ISO 14971 risk management file; see [`02-security-risk-management.md`](02-security-risk-management.md) |
| §8 software configuration management | SOUP and provenance, the supply-chain half of security | The SOUP register (issue #36) and the evidence pipeline's input digests are the same records read for a different question |
| §9 software problem resolution | Vulnerability and defect management | One tracker, with security triage layered on ([`04-security-verification-and-update-management.md`](04-security-verification-and-update-management.md)) |

The row that matters most for reading this corpus is the risk one. A security failure that can
lead to harm *is* a safety hazard, and IEC 81001-5-1 does not create a second, parallel risk file
for it. This corpus therefore points at [`../iso14971/`](../iso14971/) for the risk machinery and
adds only what is security-specific on top.

## §2 Normative references

IEC 81001-5-1 is understood to be written in coordination with IEC 62443-4-1 (secure product
development life cycle requirements, from the industrial-control-systems security family) and to
reference ISO 14971 for the risk-management framework a security risk assessment sits inside. See
[`../iso14971/`](../iso14971/) for this project's treatment of the latter.

## §3 Terms and definitions

As elsewhere in this corpus, definitions are not restated — doubly so here, given the lower
confidence noted above. The concepts below recur across this directory as category names, not as
quoted definitions.

| Concept | Where MduX makes it concrete |
|---|---|
| Secure by design | [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust-zone architecture keeps a governed module's dependency surface minimal and mechanically checked, which is a design-time security property, not a bolt-on mitigation. |
| Software Bill of Materials / provenance | MduX's zero-SOUP direction (issue #18) and the evidence pipeline's recipe/input digests ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)) together mean an artifact's inputs are known and verifiable, which is the provenance question an SBOM exists to answer. |
| Vulnerability and defect management | GitHub Issues is MduX's mechanism today, with the same limits already noted in [`../iec62304/07-problem-resolution-process.md`](../iec62304/07-problem-resolution-process.md) — general-purpose tracking, not a security-specific triage process. |
| Tamper-evidence | The evidence pipeline's byte-identity verification ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)) means a committed artifact that no longer matches its recipe is a detectable, CI-failing condition — a narrower property than tamper-evidence in a deployed, running system, but a real one for the build artifacts it covers. |
