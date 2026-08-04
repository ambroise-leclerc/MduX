# IEC 81001-5-1:2021 — per-clause index

One row per clause section in this corpus: the clause, a one-sentence pointer to what
MduX does or does not provide for it, and a deep link to the heading that covers it.
Generated from the headings, `Justification` objects and pointer comments already
present in the modules — not hand-transcribed, so it cannot drift from the prose it
indexes without the source changing too.

Regenerate after editing any file in this directory:

```
python3 tools/docs-lint/generate_ai_reference.py docs/iec81001
```

A clause shown as `—` is one this corpus deliberately does not number — see
[`README.md`](README.md). It is not a gap in the index.

| Clause | Covers | Pointer | Justification(s) |
|---|---|---|---|
| §1 | [Scope](01-scope-and-relationship-to-iec62304.md#1-scope) | MduX contributes to a device's security posture without running the standard's process itself — it is a dependency a manufacturer's secure development life cycle would need to account for, not the life cycle. | — |
| — | [Relationship to IEC 62304](01-scope-and-relationship-to-iec62304.md#relationship-to-iec-62304) | Maps each IEC 62304 lifecycle activity to its security counterpart, so a manufacturer using MduX runs one process rather than two. | — |
| §2 | [Normative references](01-scope-and-relationship-to-iec62304.md#2-normative-references) | Read alongside docs/iso14971/, which covers the risk-management framework a security risk assessment sits inside. | — |
| §3 | [Terms and definitions](01-scope-and-relationship-to-iec62304.md#3-terms-and-definitions) | Names the security concepts that recur here and where MduX makes each concrete: trust zones, provenance, defect tracking, and build tamper-evidence. | — |
| §4.2 | [Security risk management](02-security-risk-management.md#42-security-risk-management) | MduX's security records use the ISO 14971 risk-record shape, so a security hazard cannot escape the review every safety hazard gets. | — |
| §7.1–§7.3 | [Risk management context, identification and estimation](02-security-risk-management.md#7173-risk-management-context-identification-and-estimation) | MduX performs no device-level security risk assessment, for the same reason it performs no device-level safety risk assessment: it has no device. | — |
| §7.4 | [Controlling security risks](02-security-risk-management.md#74-controlling-security-risks) | The governed/adapter/tools trust-zone split constrains a governed target's dependency surface at the architecture level - mechanically enforced by MduXTrustZones.cmake at configure time - rather than relying on a runtime security control layered on afterward. | JUS-013 |
| §7.5 | [Monitoring the effectiveness of risk controls](02-security-risk-management.md#75-monitoring-the-effectiveness-of-risk-controls) | None of MduX's three mechanisms was designed against this standard; they are inputs to a manufacturer's assessment, not evidence of a completed one. | — |
| §5.3–§5.4 | [Software architectural design and software design](03-secure-design-and-implementation.md#5354-software-architectural-design-and-software-design) | MduX's trust-zone architecture is the clearest instance of a design-time security property in this repository. | — |
| §5.5 | [Software unit implementation and verification](03-secure-design-and-implementation.md#55-software-unit-implementation-and-verification) | Where MduX ships a derived artifact rather than only source, the consumer verifies it before use. | — |
| — | [What no MduX mechanism covers](03-secure-design-and-implementation.md#what-no-mdux-mechanism-covers) | MduX runs `clang-tidy` and compiles warnings-as-errors, which is general code hygiene rather than a security programme, and has no secrets to handle. | — |
| §5.7 | [Software system testing](04-security-verification-and-update-management.md#57-software-system-testing) | MduX verifies two properties this practice cares about - the trust-zone dependency boundary and cross-toolchain byte identity - and fuzzes none of its parsing surfaces, which is a gap rather than a scope exclusion. | — |
| §9 | [Software problem resolution process](04-security-verification-and-update-management.md#9-software-problem-resolution-process) | MduX tracks defects in GitHub Issues, with the general-purpose-tracking limits already set out in [`../iec62304/07-problem-resolution-process.md`](../iec62304/07-problem-resolution-process.md). | — |
| §6 | [Software maintenance process](04-security-verification-and-update-management.md#6-software-maintenance-process) | MduX has no deployed product, so it has no update mechanism to describe — the same honest gap already recorded for maintenance in [`../iec62304/04-maintenance-process.md`](../iec62304/04-maintenance-process.md). | — |
| §5.8 | [Software release](04-security-verification-and-update-management.md#58-software-release) | The caveat stated throughout this directory applies with full force here: none of them was written against IEC 81001-5-1's documentation requirements, so they should be read as material a manufacturer can use, not as a deliverable this standard asks for and MduX has produced. | — |
