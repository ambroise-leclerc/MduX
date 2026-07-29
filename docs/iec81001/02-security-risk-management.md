# IEC 81001-5-1:2021 — Security risk management

Identifying security risks, evaluating them, controlling them, and feeding what remains into the
device's overall risk management process.

Practice categories are named rather than numbered throughout this directory; see
[`README.md`](README.md) for what this corpus asserts and what it does not.

## One risk file, not two

The most important thing to get right about this practice is what it is *not*: it is not a separate
risk management system running beside ISO 14971's. A security failure that can lead to harm is a
hazard, and it belongs in the same risk management file as every other hazard, controlled by the
same requirements and verified the same way. IEC 81001-5-1 adds the security-specific *analysis*
that finds those hazards — threat modelling, attack surface enumeration, supply-chain exposure —
not a second place to record them.

This is why [`../iso14971/schemas/risk-record.schema.json`](../iso14971/schemas/risk-record.schema.json)
is the record shape for a security risk too, and why
[`schemas/security-risk-record.schema.json`](schemas/security-risk-record.schema.json) extends it
with the threat-side fields rather than replacing it. A security record that could not be read as
a risk record would let a security hazard escape the review that every safety hazard gets.

## What MduX does not do

MduX performs no device-level security risk assessment, for the same reason it performs no
device-level safety risk assessment: it has no device. Threat modelling needs an intended use, a
deployment environment, a network position, and an adversary — none of which a library has, and
all of which change the answer.

Nothing in this repository should be read as a security risk assessment for a device built on
MduX.

## What MduX does supply

Three inputs a manufacturer's security risk assessment can consume. Each is a fact about this
library, not a conclusion about a device.

### Dependency surface, mechanically bounded

The trust-zone split constrains what a governed module can link against, checked at configure time
rather than asserted in review. For a security risk assessment, this bounds the question "what
code executes in the governed zone" to something answerable from the build system.

```json
{
  "justification_id": "JUS-013",
  "standard": "IEC 81001-5-1:2021",
  "clause_ref": "IEC 81001-5-1:2021 §4 General requirements",
  "rationale": "The governed/adapter/tools trust-zone split constrains a governed target's dependency surface at the architecture level - mechanically enforced by MduXTrustZones.cmake at configure time - rather than relying on a runtime security control layered on afterward. Cited at the general-requirements level rather than at a security-practice sub-clause, per this corpus's stated citation limits.",
  "evidence_refs": ["docs/adr/ADR-004-trust-zones-in-cpp.md", "cmake/MduXTrustZones.cmake"]
}
```

### Provenance of what is depended on

The zero-SOUP direction (issue #18) and the SOUP register (issue #36) between them answer the
supply-chain question a security risk assessment asks first: what third-party code is in here, and
where did it come from. A dependency that is not present cannot be a supply-chain risk; one that is
present is enumerated rather than discovered.

The register is a statement of what MduX depends on. Whether each dependency is acceptable for a
given device — its update posture, its vendor, its exposure — is the manufacturer's judgement, and
the register exists so that judgement has an input rather than a search.

### Integrity of what is shipped

The evidence pipeline's byte-identity discipline ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md))
makes a committed artifact that no longer matches its recipe a CI-failing condition. That is
narrower than tamper-evidence in a deployed running system — it covers the build, not the field —
but it is a real integrity property over the artifacts it covers, and it is the mechanism the
supply-chain rows of a security risk assessment can point at.

## The gap this corpus does not paper over

None of the three mechanisms above was designed against IEC 81001-5-1. They were designed for
determinism, auditability and dependency discipline, and they happen to answer questions this
standard asks. That is worth having and worth stating plainly; it is not the same as having run
the standard's process, and a manufacturer should treat these as inputs to its own assessment
rather than as evidence of a completed one.
