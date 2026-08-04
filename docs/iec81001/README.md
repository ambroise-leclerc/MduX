# IEC 81001-5-1:2021 — clause reference, verified against the published standard

New content, following the same convention as [`docs/iso14971/`](../iso14971/) and
[`docs/iec62366/`](../iec62366/): [`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s
citation-key format, original prose only, `Justification` objects where a real MduX mechanism
applies.

## What this directory cites, and what it does not

This corpus cites IEC 81001-5-1:2021 by clause throughout, and every clause number in it has been
checked against the published standard (see below). Section headings carry the clause they cover;
the per-module correspondence is tabulated in the clause map.

This directory once called itself a "clause-accurate reference" while stating that nothing below §4
had been checked. Those were incompatible claims and the title was the wrong one. The structure is
now verified, so the title says that instead — but "verified structure" is still not
"clause-accurate at requirement level", and the distinction is kept below.

The reason is the same as for [`docs/iec62366/`](../iec62366/), and stronger. IEC 81001-5-1:2021 is
newer and more specialized than the other four standards this project cites
([ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) keeps its text out of the
tree regardless). What this corpus describes with real confidence is the *category* of security
lifecycle activity the standard is known to require — security risk management, secure design, secure implementation,
security verification and validation, vulnerability and defect management, security update
management, security documentation and guidance — because those categories are extensively
documented in public security-lifecycle literature, and IEC 81001-5-1 is understood to align
closely with IEC 62443-4-1's practice structure adapted for health software.

Confidence extends through §4, since "general requirements" as clause 4 is a near-universal IEC
front-matter convention this project already relies on for two other standards. It does not extend
into the numbering of the security practices themselves. Asserting a `§5.2.3`-style citation for
those would claim a precision this project does not have, so no module does.

## Authorized-source verification

Verified against an official IEC preview of the published standard under issue #114.

| | |
|---|---|
| **Standard** | IEC 81001-5-1, Edition 1.0, 2021-12 |
| **Also consulted** | IEC 81001-5-1:2021/ISH1:2025 (interpretation sheet, 2025-11) |
| **Scope of review** | Every clause number and heading this directory asserts, against the standard's own bilingual table of contents |
| **Verified** | 2026-08-04 |
| **Source** | An official IEC preview PDF held by the maintainer. Not committed, not uploaded, not quoted — see [ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) |

**Confirmed as asserted:** §1 Scope, §2 Normative references, §3 Terms and definitions, and
§4 **General requirements** — the last of which this corpus had been carrying as an assumption from
IEC front-matter convention. The assumption was correct.

### Why a draft was not enough

A first attempt used IEC/DIS 81001-5-1:2020 (document 62D/1419/CDV), the Committee Draft for Vote.
It was rejected as a verification source because a draft is not the standard, and comparing the two
shows that caution was not academic — **the numbering moved substantially between them**:

| Subject | In the CDV draft | In the published standard |
|---|---|---|
| Security risk management | §7 | **§4.2** (with §7 the *security risk management process*) |
| Quality management | §10 | **§4.1** |

Anything numbered from the draft would have been wrong by a whole clause level.

### Verified clause map

This directory's practice modules are organised by topic rather than by clause, and their `02-`,
`03-`, `04-` prefixes are **file ordering, not clause numbers**. Now that the structure is
verified, this is where each module sits in the standard:

| Module | Published clauses it covers |
|---|---|
| [`01-scope-and-relationship-to-iec62304.md`](01-scope-and-relationship-to-iec62304.md) | §1, §2, §3 |
| [`02-security-risk-management.md`](02-security-risk-management.md) | §4.2; §7.1–§7.3, §7.4, §7.5 |
| [`03-secure-design-and-implementation.md`](03-secure-design-and-implementation.md) | §5.3–§5.4, §5.5 |
| [`04-security-verification-and-update-management.md`](04-security-verification-and-update-management.md) | §5.7, §5.8, §6, §9 |

The standard's full top-level structure is §1 scope, §2 normative references, §3 terms and
definitions, §4 general requirements, §5 software development process, §6 software maintenance
process, §7 security risk management process, §8 software configuration management process, §9
software problem resolution process.

### Depth of numbering, and where it stops

Every section heading in this directory now carries the clause it covers, taken from the verified
structure. Numbering stops at the level where MduX has something to say: §7.4 rather than §7.4.1,
§5.7 rather than §5.7.3.

That is a deliberate floor, not an omission. The generated index refuses a row pointing at a
heading with nothing behind it, and a corpus that split §5.7 into five sub-clause headings so it
could write "no MduX mechanism" five times would be padding rather than precision. Where a
sub-clause matters to a mechanism, the prose names it in words.

No normative text is reproduced or closely paraphrased here, so this corpus is not a substitute for
the standard and cannot be read as one.

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
| [`02-security-risk-management.md`](02-security-risk-management.md) | §4.2 and §7 — security risk management, and why it produces records in the ISO 14971 risk file rather than a second one |
| [`03-secure-design-and-implementation.md`](03-secure-design-and-implementation.md) | §5.3–§5.4 architectural and software design, §5.5 unit implementation and verification |
| [`04-security-verification-and-update-management.md`](04-security-verification-and-update-management.md) | §5.7 system testing, §5.8 release, §6 maintenance, §9 problem resolution |

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
directory's own headings. It makes no claim beyond what is already here: every row carries the
clause its heading carries.

Rows follow **module order, which is thematic** — scope, then risk management, then design and
implementation, then verification through problem resolution. Within a module the sections are in
clause order, but across modules they are not: §7 appears before §5 because security risk
management is discussed before secure design. That ordering was chosen when this corpus was
written and is kept, because the reading order is the useful one for a reader working out what
MduX does and does not provide. Sort by the clause column for the standard's order.

Two rows show `—` in the clause column, and both are deliberate. One indexes the
activity-by-activity mapping to IEC 62304's life cycle, which is a relationship between two
standards rather than a clause of this one; the other indexes a section recording what no MduX
mechanism covers. Neither is a gap in the index.

`AI-Reference.json` is the same rows in machine-readable form, written by the same pass so
the two cannot disagree, and validated against
[`../governance/schemas/clause-index.schema.json`](../governance/schemas/clause-index.schema.json).
