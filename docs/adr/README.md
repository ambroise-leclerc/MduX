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

## Creating New ADRs

1. Copy the template from `template.md`
2. Number the ADR sequentially (ADR-XXX)
3. Use descriptive filenames: `ADR-XXX-short-description.md`
4. Update this README index
5. Consider regulatory and safety implications
6. Get review from the architecture team