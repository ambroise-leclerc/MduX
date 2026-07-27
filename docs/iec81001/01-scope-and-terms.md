# IEC 81001-5-1:2021 §1–§3 — Scope, normative references, terms and definitions

## §1 Scope

IEC 81001-5-1 specifies life cycle requirements for the security of health software and health IT
systems — establishing a secure product development life cycle a manufacturer follows from initial
requirements through post-market maintenance. It sits alongside IEC 62304 (safety life cycle) as a
security-focused counterpart, addressing a device's security posture rather than its functional
safety directly, though the two are related: a security failure can become a safety hazard.

MduX contributes to a device's security posture without running the standard's process itself — it
is a dependency a manufacturer's secure development life cycle would need to account for, not the
life cycle. See the confidence note in [`README.md`](README.md) for why this corpus stops naming
practice categories rather than citing specific clause numbers below this point.

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
