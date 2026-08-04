# Architecture Decision Records (ADRs)

This directory is the authoritative decision trail for MduX. One file per decision, one row per file
in the index below, numbers contiguous from ADR-001.

## ADR Index

| ADR | Title | Status | Date |
|-----|-------|--------|------|
| [ADR-001](ADR-001-multiplatform-graphics-framework.md) | Multiplatform low-level graphics framework selection | Accepted | 2025-07-15 |
| [ADR-002](ADR-002-testing-framework-selection.md) | Testing framework selection (Catch2) | **Superseded by ADR-009** | 2026-08-03 |
| [ADR-003](ADR-003-compiler-modernization.md) | Compiler modernization for C++23 modules | Accepted | 2025-07-26 |
| [ADR-004](ADR-004-trust-zones-in-cpp.md) | Trust zones in C++ | Accepted | 2026-07-26 |
| [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) | Error handling and exceptions policy | Accepted | 2026-07-26 |
| [ADR-006](ADR-006-no-reproduction-of-normative-standard-text.md) | No reproduction of normative standard text | Accepted | 2026-07-26 |
| [ADR-007](ADR-007-evidence-pipeline-doctrine.md) | Evidence pipeline doctrine | Accepted | 2026-07-27 |
| [ADR-008](ADR-008-zero-soup-ml-inference.md) | Zero-SOUP ML inference | Accepted | 2026-08-03 |
| [ADR-009](ADR-009-in-repository-test-framework.md) | In-repository test framework, and SpecLab for BDD | Accepted | 2026-08-03 |

Every number from 001 to 009 appears exactly once. A superseded decision keeps its number and its
file — the trail is only useful if the abandoned turns are still visible.

## What is not here

`ADR-002-implementation-plan.md` was retired by issue #108. It was an integration schedule for
Catch2, not an independent decision, and it shared ADR-002's number — which is what made this index
non-contiguous. Its disposition is recorded in
[`docs/governance/superseded-documents.md`](../governance/superseded-documents.md); `git log
--follow --diff-filter=D` recovers it in full.

It was not renumbered into a new decision. Doing so would have given an implementation schedule for
abandoned work the standing of an architectural decision.

## Format

Each ADR follows the same shape, from [`template.md`](template.md):

- **Title** and a status of `Proposed`, `Accepted`, `Deprecated`, or `Superseded by ADR-NNN`
- **Context** — the situation that forced a decision
- **Medical Device Considerations** — IEC 62304 lifecycle implications, risk-management
  consequences, and traceability requirements
- **Decision** — what was chosen, in enough detail to be argued with
- **Alternatives Considered** — what was rejected and why, so a rejected option is not silently
  re-proposed
- **Consequences** — positive, negative-and-accepted, and risks introduced with their mitigations
- **Implementation Notes**, **References**, **Approval**

The *Alternatives Considered* and *negative consequences* sections are the load-bearing ones. A
decision record that lists only benefits documents an advertisement rather than a decision.

## Writing a new ADR

1. Copy [`template.md`](template.md).
2. Take the next free number from the index above — currently **ADR-010**.
3. Name the file `ADR-NNN-short-description.md`, lowercase and hyphenated.
4. Add a row to the index in this file. An ADR that is not indexed does not exist.
5. If it supersedes an earlier decision, set that ADR's status to `Superseded by ADR-NNN`, link
   forward from it, and link back from the new one. Do not delete the superseded file.

## Scope limits

No ADR in this repository establishes certification, validation, production readiness, or
regulatory compliance. These records document engineering decisions and the reasoning behind them;
see [`docs/regulatory-compliance.md`](../regulatory-compliance.md) for what this project does and
does not claim, and [ADR-006](ADR-006-no-reproduction-of-normative-standard-text.md) for the rule
that keeps normative standard text out of the tree.
