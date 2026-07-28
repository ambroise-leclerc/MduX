# IEC 81001-5-1:2021 — practice reference, top-level clauses only

New content, following the same convention as [`docs/iso14971/`](../iso14971/) and
[`docs/iec62366/`](../iec62366/): [`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s
citation-key format, original prose only, `Justification` objects where a real MduX mechanism
applies.

## What this directory cites, and what it does not

This corpus cites IEC 81001-5-1:2021 at the **top level only** — `§1` through `§4`. Nothing below
that is cited, and the practice modules use named headings rather than clause numbers.

This directory previously called itself a "clause-accurate reference" while stating that nothing
below §4 had been checked against the standard. Those are two incompatible claims, and the first
was the wrong one: a reference is not clause-accurate at a depth it does not cite. The title now
says what is true.

The reason is the same as for [`docs/iec62366/`](../iec62366/), and stronger. IEC 81001-5-1:2021 is
newer and more specialized than the other four standards this project cites, and this project holds
no copy of its text ([ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md)). What
this corpus describes with real confidence is the *category* of security lifecycle activity the
standard is known to require — security risk management, secure design, secure implementation,
security verification and validation, vulnerability and defect management, security update
management, security documentation and guidance — because those categories are extensively
documented in public security-lifecycle literature, and IEC 81001-5-1 is understood to align
closely with IEC 62443-4-1's practice structure adapted for health software.

Confidence extends through §4, since "general requirements" as clause 4 is a near-universal IEC
front-matter convention this project already relies on for two other standards. It does not extend
into the numbering of the security practices themselves. Asserting a `§5.2.3`-style citation for
those would claim a precision this project does not have, so no module does.

**Before this corpus is used for anything compliance-facing**, someone with access to the actual
standard should check the practice modules against its real clause structure and add numbering
below §4. That is tracked on issue #31, and it is bounded work: the module boundaries and the
practice categories are the part least likely to need revisiting.

## What this is, and is not

IEC 81001-5-1 asks a manufacturer to run a secure product development life cycle for health
software — building security in from requirements through maintenance, not bolting it on after
release. MduX's zero-SOUP direction (issue #18), trust-zone architecture, and evidence pipeline
were each designed with a security-adjacent property in mind (provenance, dependency isolation,
tamper-evidence), even though none was built *against* this standard. This corpus is honest about
that: the mechanisms it names address the general concern a practice category names, not a specific
numbered requirement anyone has verified against the standard's text.

Each module is also explicit about what its mechanisms do **not** cover. Fuzzing of parsing
surfaces, coordinated disclosure, security triage, and downstream notification are all absent, and
they are recorded as gaps rather than left out, so that silence does not read as coverage.

## Structure

| File | Covers |
|---|---|
| [`01-scope-and-relationship-to-iec62304.md`](01-scope-and-relationship-to-iec62304.md) | §1–§3 scope, normative references and terms, plus the activity-by-activity relationship to IEC 62304's life cycle |
| [`02-security-risk-management.md`](02-security-risk-management.md) | Security risk management, and why it produces records in the ISO 14971 risk file rather than a second one |
| [`03-secure-design-and-implementation.md`](03-secure-design-and-implementation.md) | Secure design, secure implementation |
| [`04-security-verification-and-update-management.md`](04-security-verification-and-update-management.md) | Security verification and validation, vulnerability and defect management, security update management, security documentation and guidance |

## Schemas

[`schemas/security-risk-record.schema.json`](schemas/security-risk-record.schema.json) is one
security risk: the threat, the asset and weakness it acts on, and the requirements that control it.

It is an **extension of** [`../iso14971/schemas/risk-record.schema.json`](../iso14971/schemas/risk-record.schema.json),
not a replacement, and that is the substantive decision in it. A security failure that can lead to
harm is a hazard; this standard adds the analysis that finds it, not a second file to record it in.
Every member the risk record defines keeps its name and meaning, so a security record is readable
by anything that reads risk records and cannot escape the review a safety hazard gets. `id`,
`description` and `controlled_by` stay field-aligned with `mdux::governance::Hazard`, checked by
`tools/docs-lint/check_schema_type_drift.py`.

[`AI-Reference.md`](AI-Reference.md) is the per-clause index, generated mechanically from this
directory's own headings. It makes no claim beyond what is already here: rows for §1–§4 carry
clause numbers, and every practice row shows `—`, because that is what this corpus asserts. It
is not waiting on the numbering verification above — it will gain numbers when the modules do.
