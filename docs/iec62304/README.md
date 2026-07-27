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
`IEC 62304:2006 §<clause> <clause title>`. A per-clause index (`AI-Reference.md`) and JSON Schemas
for the mechanisms cited here are tracked separately as issues #32 and #33 — this directory is the
prose those will index, not a replacement for it.

## Safety classification scope

MduX's own components are scoped as **Class A** (software that cannot contribute to a hazardous
situation) throughout this directory. Its sibling project TrustSC models Class B/C. Where a clause's
requirements scale with safety class, the text says so explicitly rather than assuming one class or
the other applies.
