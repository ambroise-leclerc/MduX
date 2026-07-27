---
name: sdf-documents
description: Use when filling in or reviewing a document under software_development_file/ (SAD, SDD, SOUP, Risk Management File, Usability Engineering File, Cybersecurity SAD) — the summarize-don't-duplicate rule and how to cite into the regulatory corpus.
---

# MduX Software Development File documents

Companion to `mdux-regulated-change`, which governs whether a change needs an SDF update at all.
This skill governs *how to write* an SDF document once you've determined one is needed; for the
citation format itself see `regulatory-citations`.

## Status: planned, not yet implemented

**`software_development_file/` does not exist in this repository yet.** It is tracked as
[issue #9](https://github.com/ambroise-leclerc/MduX/issues/9) (S4 templates, S5 filled-in-for-MduX)
and depends on the governance types in the same epic and the clause-accurate corpus in
[issue #8](https://github.com/ambroise-leclerc/MduX/issues/8). This skill describes the target
structure and rule so the first documents are written correctly rather than needing a rewrite.

## Structure (target)

```
software_development_file/
  README.md
  templates/       # blank, for a manufacturer building on MduX to fill in
    IEC_62304/{SAD.md, SDD.md, SOUP.md}
    IEC_62366/Usability_Engineering_File.md
    IEC_81001/Cybersecurity_SAD.md
    ISO_13485/README.md
    ISO_14971/Risk_Management_File.md
  regulatory/      # the same documents, filled in for MduX itself
    (mirrors templates/)
```

## The rule that matters most

**A filled document summarizes; it never duplicates a machine register.** If the information lives
in `docs/governance/soup-register.toml`, the SOUP document points at it — it does not restate the
rows. If it lives in a `Hazard` record, the risk file points at it — it does not copy the record.
Duplication is how a technical file goes stale: the register gets updated, the prose next to it
does not, and now there are two answers to the same question.

Concretely: write "see the SOUP register at `docs/governance/soup-register.toml` for the current
component list" rather than pasting a table of components into the SAD. If you need a summary
count or a highlighted subset, generate it and label it as a snapshot with the date/commit it was
taken at.

## Writing a section

1. Identify which clause the section discharges — use `regulatory-citations`' format and, where the
   decision needs formal justification, a `Justification` block with real `evidence_refs[]`.
2. Point at the real mechanism (a type, a CI check, a file) rather than restating the clause
   abstractly — the same rule as the corpus modules themselves.
3. If the mechanism doesn't exist yet, say so plainly rather than describing planned work as done.
   `templates/` documents may describe what a manufacturer is expected to add; `regulatory/`
   documents for MduX itself must describe only what is actually built.

## The constraint every SAD-equivalent document must carry

The C++ trust-zone split (see the trust-zones ADR once it lands) does **not** reproduce Rust's
`#![forbid(unsafe_code)]` audit property. Write the claim narrowly: *"governed modules are compiled
without access to platform, graphics, or OS headers, are checked by an enforced static-analysis
profile, and are covered by determinism tests"* — never *"cannot contain undefined behaviour."*
Any SDF document that blurs this distinction needs to be corrected before it is treated as final.
