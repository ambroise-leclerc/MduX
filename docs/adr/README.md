# Architecture Decision Records (ADRs)

This directory contains Architecture Decision Records (ADRs) for the MduX project. ADRs document important architectural decisions made during the development of this medical device UI library.

## ADR Format

We follow the standard ADR format with the following sections:

- **Title**: A short descriptive title
- **Status**: Proposed, Accepted, Deprecated, or Superseded
- **Context**: The situation that led to this decision
- **Decision**: The decision that was made
- **Consequences**: The positive and negative consequences of this decision

## Medical Device Compliance

All ADRs in this project must consider:
- IEC 62304 (Software Lifecycle Processes)
- IEC 62366 (Usability Engineering)
- Cybersecurity requirements
- Risk management implications
- Traceability requirements

## ADR Index

| ADR | Title | Status | Date |
|-----|-------|--------|------|
| [ADR-001](ADR-001-multiplatform-graphics-framework.md) | Multiplatform Low-Level Graphics Framework Selection | Accepted | 2025-07-15 |
| [ADR-002](ADR-002-testing-framework-selection.md) | Testing Framework Selection for Medical Device Compliance | Proposed (never implemented) | — |
| [ADR-003](ADR-003-compiler-modernization.md) | Compiler Modernization for C++23 Modules Support | Accepted | 2025-07-26 |
| [ADR-004](ADR-004-trust-zones-in-cpp.md) | Trust zones in C++ | Accepted | 2026-07-26 |
| [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) | Error handling and exceptions policy | Accepted | 2026-07-26 |
| [ADR-006](ADR-006-no-reproduction-of-normative-standard-text.md) | No reproduction of normative standard text | Accepted | 2026-07-26 |
| [ADR-007](ADR-007-evidence-pipeline-doctrine.md) | Evidence pipeline doctrine | Accepted | 2026-07-27 |
| [ADR-008](ADR-008-zero-soup-ml-inference.md) | Zero-SOUP ML inference | Accepted | 2026-08-03 |

Note: `ADR-002-implementation-plan.md` shares its number with `ADR-002-testing-framework-selection.md`
(a pre-existing duplication in this directory) and is not an independently numbered ADR — it is the
implementation plan for the testing-framework decision. Re-baselining this directory's numbering is
tracked separately (issue #10, S1); ADR-004 and ADR-005 were assigned the next free numbers rather
than waiting on that rebaseline. ADR-002's Catch2 selection was never implemented and is expected to
be superseded by an in-repo test framework decision (issue #11, S5) once that ADR is written — until
then it remains "Proposed" in the sense that the decision it recorded was never carried out.

## Creating New ADRs

1. Copy the template from `template.md`
2. Number the ADR sequentially (ADR-XXX)
3. Use descriptive filenames: `ADR-XXX-short-description.md`
4. Update this README index
5. Consider regulatory and safety implications
6. Get review from the architecture team