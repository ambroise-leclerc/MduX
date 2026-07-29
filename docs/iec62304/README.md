# IEC 62304:2006 — clause-accurate reference

This directory replaces an earlier version that grouped content under a flat "sections 1-16"
numbering invented for this repository, which does not exist in IEC 62304:2006 and could not be
cited by real clause number. It also contained sentences that were a close paraphrase of the
standard's own wording — removed under [ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md).
See `docs/governance/reproduced-text-inventory.md` (issue #21) for the full accounting.

## What this is, and is not

Each file below covers one clause of IEC 62304:2006, named and numbered to match the standard's
actual structure. The prose is original: it explains what a clause requires *of the development
process*, in this project's own words, then either points at a concrete MduX mechanism that
addresses it or says plainly that none exists yet. It is not a substitute for reading the standard,
and it is not a compliance certification — a scope-limits document making that explicit for every
regulatory document in this repository is tracked as issue #39.

## Structure

| File | Clause(s) | Covers |
|---|---|---|
| [`01-scope-and-terms.md`](01-scope-and-terms.md) | §1–§3 | Scope, normative references, terms and definitions |
| [`02-general-requirements.md`](02-general-requirements.md) | §4 | Quality management system context, risk management context, software safety classification |
| [`03-development-process.md`](03-development-process.md) | §5.1–§5.8 | Planning through release |
| [`04-maintenance-process.md`](04-maintenance-process.md) | §6 | Maintenance planning, problem/modification analysis, modification implementation |
| [`05-risk-management-process.md`](05-risk-management-process.md) | §7 | Hazard analysis, risk control measures, verification, change impact |
| [`06-configuration-management-process.md`](06-configuration-management-process.md) | §8 | Configuration identification, change control, status accounting |
| [`07-problem-resolution-process.md`](07-problem-resolution-process.md) | §9 | Problem resolution process |

Every clause citation in this directory uses the key format from
[`docs/governance/citation-convention.md`](../governance/citation-convention.md):
`IEC 62304:2006 §<clause> <clause title>`. [`AI-Reference.md`](AI-Reference.md) is the per-clause
index, generated from this directory's own headings and `Justification` objects.
`AI-Reference.json` is the same rows in machine-readable form, written by the same pass so the two
cannot disagree, and validated against
[`../governance/schemas/clause-index.schema.json`](../governance/schemas/clause-index.schema.json).

## Schemas

[`schemas/`](schemas/) holds the four IEC 62304 record types, each field-aligned with the
corresponding type in `include/mdux/governance/Governance.cppm`:

| Schema | Type it mirrors |
|---|---|
| [`requirement.schema.json`](schemas/requirement.schema.json) | `mdux::governance::Requirement` |
| [`hazard.schema.json`](schemas/hazard.schema.json) | `Hazard` — `controlled_by` non-empty, the §4.2 join |
| [`verification-case.schema.json`](schemas/verification-case.schema.json) | `VerificationCase` and the `VerificationMethod` vocabulary |
| [`safety-classification.schema.json`](schemas/safety-classification.schema.json) | the `SafetyClass` vocabulary |

The alignment is the deliverable, not the files. `tools/docs-lint/check_schema_type_drift.py` fails
the build when a schema and the type it documents stop agreeing — a schema that silently diverges
from its type is worse than no schema, because it still looks authoritative.

The shared `Justification` schema lives at
[`../governance/schemas/justification.schema.json`](../governance/schemas/justification.schema.json),
since all five corpora use it.

## Safety classification scope

**MduX does not classify itself, and no document in this directory should be read as doing so.**
IEC 62304 §4.3 makes safety classification a decision taken from a device-level risk analysis:
what harm the software can contribute to, in *this* device, for *this* intended use. The same
rendering code is Class A in one product and Class C in another, and a library has neither a device
nor an intended use to reason from.

What this directory does instead is cover the clause set as it applies at each class, and say
explicitly where a requirement scales with classification. Where the corpus discusses Class A
obligations at length, that reflects the classification a manufacturer is most likely to assign to
a component like an evidence digest — not a conclusion this project has reached on a manufacturer's
behalf.

[`schemas/safety-classification.schema.json`](schemas/safety-classification.schema.json) enforces
the same discipline in machine-readable form: a classification record cannot be written without an
`assigned_by` naming who decided. A previous version of this corpus declared all MduX components
Class A while simultaneously stating that no device-level risk analysis existed; that contradiction
is what the required field exists to prevent recurring.
