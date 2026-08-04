# IEC 62366-1:2015 — clause reference, top-level clauses only

New content, following the same convention as [`docs/iso14971/`](../iso14971/):
[`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s citation-key
format, original prose only, `Justification` objects where a real MduX mechanism applies.

## Authorized-source verification

This corpus's clause numbers and headings were checked against an authorized copy of the standard,
clause by clause, under issue #113.

| | |
|---|---|
| **Standard** | IEC 62366-1, Edition 1.0, 2015-02 (en-fr), ISBN 978-2-8322-2281-2 |
| **Scope of review** | Every clause number, heading and sub-clause heading asserted by this directory, against the standard's own table of contents and clause headings |
| **Verified** | 2026-08-04 |
| **Source** | A licensed copy held by the maintainer. Not committed, not uploaded, and not quoted — see [ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) |

Sub-clause numbering is therefore now **asserted** rather than withheld. It was withheld before
because the numbers then in the file came from professional familiarity rather than from the
standard, and the check found that caution was justified — it corrected three substantive errors:

1. **§4 was titled "General requirements".** The standard's §4 is **Principles**; "General
   requirements" is §4.1. The module is renamed [`02-principles.md`](02-principles.md).
2. **§5.1 was "application specification".** That is the superseded IEC 62366:2007 term; the 2015
   edition calls it **use specification**.
3. **§5.3, §5.4, §5.5 and §5.10 were absent.** The three hazard-identification and
   scenario-selection steps are how this process connects to ISO 14971, and §5.10 covers a
   *user interface of unknown provenance* — the clause an integrator is most likely to apply to
   MduX itself.

A step that was listed and is not a sub-clause — "iterate the design as evaluation results
require" — is folded into §5.8, which is where the standard puts iteration.

### What is still not asserted

Requirement-level text. This corpus states clause numbers, headings and what MduX does or does not
provide against them. It reproduces no normative text and paraphrases none closely, so it is not a
substitute for the standard and cannot be read as one.

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
| [`02-principles.md`](02-principles.md) | §4 | Principles — general requirements, usability engineering file, tailoring |
| [`03-usability-engineering-process.md`](03-usability-engineering-process.md) | §5 | §5.1 prepare use specification through §5.10 user interface of unknown provenance |

[`AI-Reference.md`](AI-Reference.md) is the per-clause index, generated mechanically from this
directory's own headings. It makes no claim beyond what is already here, so it carries the same
citation limits stated above rather than new ones — its process steps are indexed with the
clause numbers verified above, exactly as the modules state them.

`AI-Reference.json` is the same rows in machine-readable form, written by the same pass so
the two cannot disagree, and validated against
[`../governance/schemas/clause-index.schema.json`](../governance/schemas/clause-index.schema.json).

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
