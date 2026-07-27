# IEC 81001-5-1:2021 — clause-accurate reference

New content, following the same convention as [`docs/iso14971/`](../iso14971/) and
[`docs/iec62366/`](../iec62366/): [`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s
citation-key format, original prose only, `Justification` objects where a real MduX mechanism
applies.

## Confidence note — read before citing anything from this directory

**This is the lowest-confidence corpus in this project's regulatory documentation, and should be
treated accordingly.** IEC 81001-5-1:2021 is newer and more specialized than the other four
standards this project cites, and this project holds no copy of its text (ADR-006). What this
corpus describes with real confidence is the *category* of security lifecycle activity the standard
is known to require — security risk management, secure design, secure implementation, security
verification and validation, vulnerability and defect management, security update management, and
security-related documentation and guidance — because these categories are extensively documented
in public security-lifecycle literature (IEC 81001-5-1 is understood to align closely with
IEC 62443-4-1's practice structure, adapted for health software). Confidence extends through §4
("General requirements" as clause 4 is a near-universal IEC front-matter convention this project
already relies on for two other standards) but **not into the specific numbering of the security
practices themselves** — asserting a numbered `§5.2.3`-style citation for those would carry a
confidence this project does not actually have. Every practice-level citation below names the
category in prose instead of a sub-clause number. Before this corpus is used for anything
compliance-facing, someone with access to the actual standard should verify it against the real
clause structure and add numbering below §4.

## What this is, and is not

IEC 81001-5-1 asks a manufacturer to run a secure product development life cycle for health
software — building security in from requirements through maintenance, not bolting it on after
release. MduX's own zero-SOUP direction (issue #18), trust-zone architecture, and evidence pipeline
were each designed with a security-adjacent property in mind (provenance, dependency isolation,
tamper-evidence), even though none were built *against* this specific standard. This corpus is
honest about that: the mechanisms below address the general concern a practice category names,
not a specific numbered requirement this project has verified against the standard's text.

## Structure

| File | Practice categories covered |
|---|---|
| [`01-scope-and-terms.md`](01-scope-and-terms.md) | Scope, normative references, terms and definitions (§1–§3, the one part of this corpus cited with normal confidence — this structure is common to all IEC standards) |
| [`02-security-lifecycle-practices.md`](02-security-lifecycle-practices.md) | Security risk management, secure design, secure implementation, security verification and validation, vulnerability and defect management, security update management, security documentation and guidance |

A per-clause index (`AI-Reference.md`) and JSON Schemas are tracked separately as issues #32
and #33 — for this standard specifically, both should wait until the confidence gap above is
closed.
