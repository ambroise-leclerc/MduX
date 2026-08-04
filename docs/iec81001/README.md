# IEC 81001-5-1:2021 — practice reference, clause structure verified

New content, following the same convention as [`docs/iso14971/`](../iso14971/) and
[`docs/iec62366/`](../iec62366/): [`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s
citation-key format, original prose only, `Justification` objects where a real MduX mechanism
applies.

## What this directory cites, and what it does not

This corpus cites IEC 81001-5-1:2021 at the **top level** — `§1` through `§4` — and its clause
structure has been checked against the published standard (see below). The practice modules still
use named headings rather than clause numbers; the verified correspondence between the two is
tabulated in the clause map.

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
| [`02-security-risk-management.md`](02-security-risk-management.md) | §4.2 security risk management; §7 security risk management process (§7.1–§7.5) |
| [`03-secure-design-and-implementation.md`](03-secure-design-and-implementation.md) | §5.3 software architectural design, §5.4 software design, §5.5 software unit implementation and verification |
| [`04-security-verification-and-update-management.md`](04-security-verification-and-update-management.md) | §5.7 software system testing, §5.8 software release, §6 software maintenance process, §9 software problem resolution process |

The standard's full top-level structure is §1 scope, §2 normative references, §3 terms and
definitions, §4 general requirements, §5 software development process, §6 software maintenance
process, §7 security risk management process, §8 software configuration management process, §9
software problem resolution process.

### What is still not asserted

Requirement-level numbering *inside* the practice modules — a heading in
`03-secure-design-and-implementation.md` still reads "Secure design" rather than "§5.4.2 Secure
design". The clause map above is the verified correspondence; carrying the numbers down into every
heading is issue #31, and it is now unblocked rather than waiting on access.

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

`AI-Reference.json` is the same rows in machine-readable form, written by the same pass so
the two cannot disagree, and validated against
[`../governance/schemas/clause-index.schema.json`](../governance/schemas/clause-index.schema.json).
