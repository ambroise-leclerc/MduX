# ISO 13485:2016 — clause-accurate reference

This directory replaces an earlier version built from fabricated per-module metadata (invented
counts of "AI decision matrices," "automation schemas," and similar) rather than the standard's
actual content, in the same style the IEC 62304 corpus had before its rewrite (issue #27) — see
[`../governance/citation-convention.md`](../governance/citation-convention.md)'s "Known gap" note
and [ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) for why neither could
stand.

## What this is, and is not

Each file below covers one clause of ISO 13485:2016, named to match the standard's real
top-level structure (its own numbering is used as the file order). The prose is original: it
explains what a clause asks of a quality management system, in this project's own words, then
either points at a concrete MduX mechanism or states plainly that none exists yet. MduX is a
software library, not a device manufacturer with a certified QMS — most of this standard describes
organizational and manufacturing processes MduX does not itself run. Where a clause has no
applicable MduX mechanism for that reason, this corpus says so rather than stretching a citation to
fit. It is not a substitute for reading the standard and not a compliance certification — see
issue #39 for the scope-limits document that will make that explicit project-wide.

## Structure

| File | Clause(s) | Covers |
|---|---|---|
| [`01-scope-and-terms.md`](01-scope-and-terms.md) | §1–§3 | Scope, normative references, terms and definitions |
| [`02-quality-management-system.md`](02-quality-management-system.md) | §4 | General QMS requirements, documentation requirements |
| [`03-management-responsibility.md`](03-management-responsibility.md) | §5 | Management commitment, quality policy, planning, responsibility and communication, management review |
| [`04-resource-management.md`](04-resource-management.md) | §6 | Provision of resources, human resources, infrastructure, work environment |
| [`05-product-realization.md`](05-product-realization.md) | §7 | Planning, customer-related processes, design and development, purchasing, production, monitoring/measuring equipment |
| [`06-measurement-analysis-improvement.md`](06-measurement-analysis-improvement.md) | §8 | Monitoring and measurement, nonconforming product, data analysis, improvement |

Every clause citation uses the key format from
[`docs/governance/citation-convention.md`](../governance/citation-convention.md):
`ISO 13485:2016 §<clause> <clause title>`. A per-clause index (`AI-Reference.md`) and JSON Schemas
are tracked separately as issues #32 and #33.
