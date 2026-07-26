---
name: regulatory-citations
description: Use when writing or reviewing any document under docs/<standard>/, software_development_file/, or an ADR that claims alignment with IEC 62304, ISO 13485, ISO 14971, IEC 62366-1, or IEC 81001-5-1 — the citation-key format, the Justification object, and the prohibition on reproducing normative standard text.
---

# MduX regulatory citation protocol

Companion to § 1 ("Project purpose and maturity") of [`AGENTS.md`](../../../AGENTS.md). This skill
governs *how to cite a standard*; for the safety/compliance impact of a code change see
`mdux-regulated-change`, and for filling in a technical-file document see `sdf-documents`.

## Status: target convention, corpus rewrite in progress

This convention is adopted as of the MduX ↔ TrustSC parity programme
(GitHub issues [#7](https://github.com/ambroise-leclerc/MduX/issues/7)–[#9](https://github.com/ambroise-leclerc/MduX/issues/9)).
**As of this writing, `docs/iec62304/` and `docs/iso13485/` are still on an old flat-numbered
structure ("sections 1-16") that does not match the standards' real clause numbers, and
`docs/MduX-IEC-62304-AI-Reference.md` / `docs/MduX-ISO-13485-AI-Reference.md` contain reproduced
or closely-paraphrased normative text pending removal.** Do not treat the current corpus contents
as a model to imitate until issue #8 (clause-accurate rewrite) lands — treat this skill's rules as
binding for *new* material regardless.

## The rule that matters most

**Never reproduce or closely paraphrase a standard's normative text.** IEC and ISO standards are
copyrighted. Every citation is original prose written against the standard's real clause number
and title — explain the requirement in your own words, then point at the real MduX mechanism (a
type, an ADR, a CI step, a file path) that addresses it. Restating a clause abstractly, without a
concrete mechanism, is a sign the citation is decorative rather than load-bearing.

## Citation-key format

```
<Standard> §<clause> <Short clause title>
```

Example: `IEC 62304:2006 §5.2 Software development planning`.

Valid standard identifiers (closed set — do not invent others):

- `IEC 62304:2006`
- `ISO 13485:2016`
- `ISO 14971:2019`
- `IEC 62366-1:2015`
- `IEC 81001-5-1:2021`

Use the exact string verbatim as a module heading, an `AI-Reference.md` row, and any `clause_ref`
field — the format must match byte-for-byte so `mdux-docs-lint` (issue #25) can check it
mechanically.

MduX keeps IEC 62304 Class A in scope (unlike TrustSC, which models Class B/C only) — say so
explicitly wherever safety classification is discussed, rather than silently assuming one or the
other.

## The `Justification` object

Use this whenever a design decision needs a formal link to a clause, as a fenced `json` block:

```json
{
  "justification_id": "JUS-014",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control",
  "rationale": "Governed modules are compiled without Vulkan or platform include directories, making the trust-zone boundary a compile error rather than a review comment.",
  "requirement_id": "REQ-ARCH-004",
  "evidence_refs": ["cmake/MduXTrustZones.cmake", "docs/adr/ADR-005-trust-zones.md"]
}
```

- `justification_id` (`JUS-NNN`) is unique **across the whole corpus**, not per standard.
- `evidence_refs[]` must contain real repository paths that exist — `mdux-docs-lint` verifies this.
- Schema: `docs/iec62304/schemas/justification.schema.json` (issue #26).

## Where to find the clause you need

1. Open that standard's `docs/<standard>/README.md` and find the module covering the clause range.
2. Use `AI-Reference.md` for fast one-row-per-clause lookup (issue #32) — it is an index, not a
   transcription; if a row exists, its module has real prose behind the link.
3. Cite using the format above; prefer a real mechanism over an abstract restatement.

## Before you cite

- Confirm the clause number is real (check against the actual standard, not against MduX's old
  renumbered corpus).
- Confirm the mechanism you're pointing at actually exists — grep for the type/file/CI step named
  in `evidence_refs[]` rather than assuming.
- If you are unsure whether prose you're about to write is original or a close paraphrase of the
  standard's own wording, rewrite it from the requirement's *intent* rather than editing the
  standard's phrasing.
