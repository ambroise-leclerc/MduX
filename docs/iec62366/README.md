# IEC 62366-1:2015 — clause-accurate reference

New content, following the same convention as [`docs/iso14971/`](../iso14971/):
[`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s citation-key
format, original prose only, `Justification` objects where a real MduX mechanism applies.

## Confidence note — read before citing a sub-clause from this directory

This corpus's citations below the top level (`§5.1` through `§5.9`) are written from general
professional familiarity with IEC 62366-1's usability engineering process, not from the standard
text itself, which this project does not reproduce or hold a copy of (see
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md)). Confidence in the exact
sub-clause numbering here is lower than for [`docs/iec62304/`](../iec62304/),
[`docs/iso13485/`](../iso13485/), and [`docs/iso14971/`](../iso14971/), which this project's authors
are more confident is accurate at the same depth. **Verify any `§5.x` citation from this directory
against the actual standard before relying on it** — the named process steps (application
specification, UI-related hazard identification, UI specification, evaluation planning, design and
implementation, formative evaluation, summative evaluation) are the part of this corpus with higher
confidence; their exact numeric position within §5 is the part that most needs checking.

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

A per-clause index (`AI-Reference.md`) and JSON Schemas are tracked separately as issues #32
and #33.
