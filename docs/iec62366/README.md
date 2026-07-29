# IEC 62366-1:2015 — clause reference, top-level clauses only

New content, following the same convention as [`docs/iso14971/`](../iso14971/):
[`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s citation-key
format, original prose only, `Justification` objects where a real MduX mechanism applies.

## What this directory cites, and what it does not

This corpus cites IEC 62366-1:2015 at the **top level only** — `§1` through `§5`. Those clause
numbers are asserted; nothing below them is.

An earlier version of [`03-usability-engineering-process.md`](03-usability-engineering-process.md)
numbered the process steps `§5.1` through `§5.9`. Those numbers came from general professional
familiarity with the usability engineering process rather than from the standard text, which this
project does not hold a copy of (see
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md)). The citation convention
requires a clause number to be confirmed before it is cited, so they have been **removed rather
than caveated**: a warning label does not stop a number being copied into a design history file,
and a reader who copies it has cited something nobody checked. The named process steps — application
specification, UI characteristics related to safety, UI specification, evaluation planning, design
and implementation, formative evaluation, iteration, summative evaluation — are what this corpus
asserts, and they are stated as headings rather than citations.

Restoring the sub-clause numbering needs one thing this repository cannot supply: a maintainer with
access to IEC 62366-1:2015, checking each heading against the standard's §5. That is tracked on
issue #30.

## What this is, and is not

IEC 62366-1 specifies a usability engineering process for identifying and mitigating use-related
risks for a medical device's user interface. MduX renders UI content but does not, itself, run a
usability engineering process against a device's actual users and use environment — that remains a
device integrator's responsibility. Where MduX's architecture bears directly on a use-related risk
category regardless of the integrating device (a UI budget the compiler enforces at build time, for
instance, once issue #15 exists), this corpus will say so; today, most of the process this standard
describes has no MduX mechanism yet, because the `.medui` compiler and renderer it would apply to
have not been built.

## Structure

| File | Clause(s) | Covers |
|---|---|---|
| [`01-scope-and-terms.md`](01-scope-and-terms.md) | §1–§3 | Scope, normative references, terms and definitions |
| [`02-general-requirements.md`](02-general-requirements.md) | §4 | General requirements for applying usability engineering |
| [`03-usability-engineering-process.md`](03-usability-engineering-process.md) | §5 | Application specification through summative evaluation |

A per-clause index (`AI-Reference.md`) is tracked separately as issue #32.

## Schemas

[`schemas/usability-engineering-record.schema.json`](schemas/usability-engineering-record.schema.json)
is one use-related risk, the UI characteristic it arises from, and the control that addresses it.

Two things about it are deliberate. Its `process_step` is a set of **named** steps rather than
clause numbers, for the reason above — putting unverified sub-clause numbers into machine-readable
form would be worse than leaving them out of prose. And `control_status` is required with no
default: a usability engineering file citing a control that is not implemented is the precise
failure this standard's evaluation steps exist to catch, so a record cannot be written without
saying whether its control exists today, and a `planned` one must name the issue that would build
it.

`requirement_id` and `hazard_id` use the same shapes as `mdux::governance::Requirement::id` and
`Hazard::id`, so a usability control joins to the risk management file rather than sitting beside
it — use-related risk is risk.
