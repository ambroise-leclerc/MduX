# Software development file

This tree is the `software_development_file/` scaffold named in issue #9's epic. It has two
subtrees, mirroring each other standard-by-standard:

```
software_development_file/
├── templates/    # blank, fillable by any manufacturer building a device on MduX
└── regulatory/   # the same tree, filled in for MduX itself (issue #38, a later PR in this stack)
```

Both subtrees now exist: `templates/` (issue #37) and `regulatory/` (issue #38, this PR).

| Standard | Documents |
|---|---|
| IEC 62304 | `SAD.md` (architecture), `SDD.md` (detailed design), `SOUP.md` (SOUP list/justification) |
| IEC 62366-1 | `Usability_Engineering_File.md` |
| IEC 81001-5-1 | `Cybersecurity_SAD.md` |
| ISO 13485 | `README.md` (QMS scope note) |
| ISO 14971 | `Risk_Management_File.md` |

## `templates/`

Blank documents with section headers matching each standard's clauses, a citation blockquote per
section pointing at the relevant `docs/<standard>/` module, and `[ ... ]` placeholders. A
manufacturer building a device on MduX copies these into their own document set and fills them
in — they contain no MduX-specific content.

## `regulatory/`

The same documents, filled in for MduX itself: real architecture description, real SOUP entries
(derived from `docs/governance/soup-register.toml`), real
citations into `docs/<standard>/` and the ADR trail. These describe MduX as a software development
kit — they are not, and do not claim to be, a finished medical device's regulatory file. See
`docs/regulatory-compliance.md` (issue #39, not yet written) for the scope disclaimer covering what
this project does and does not provide.

**These filled documents describe what exists and what does not, including where that is a gap.**
As of this PR, no `ComplianceProgram` (issue #35) has actually been populated for MduX itself — the
`Requirement`/`VerificationCase`/`Hazard`/`ProblemReport` types exist and are unit-tested (31 tests
in `evidence_tests`), but zero real instances of any of them have been recorded for MduX's own
release. `regulatory/ISO_14971/Risk_Management_File.md` and `regulatory/IEC_62304/SDD.md` say this
plainly rather than fabricating a worked example the way a finished device's SDF would contain one.
Root `README.md`'s "Implementation Status" table separately claims several of these items
"Completed" and references a `mdux::MedicalDeviceContext` / `mdux/compliance/MedicalDevice.cppm` API
that does not exist anywhere in this repository (`AGENTS.md` § 2 already flags README's claims as
partly aspirational) — `regulatory/ISO_13485/README.md` names this discrepancy directly rather than
repeating it.

Some cross-references to `include/mdux/governance/Compliance.cppm` and
`docs/governance/soup-register.toml` in `regulatory/`'s `Justification` blocks are fenced as
`jsonc` rather than `json`, even though they are real, not illustrative — those files exist on the
still-open `governance/traceability-matrix` (#96) and `governance/soup-register` (#97) branches,
not yet on `develop`, so `mdux-docs-lint`'s `evidence_refs` existence check would fail here until
those branches merge. Re-fence them as `json` once they do.

## The rule that keeps these documents honest

**A filled document summarizes, and never duplicates, a machine register.** A SOUP document points
at `docs/governance/soup-register.toml`; it does not
restate its rows. A risk file points at the `Requirement`/`VerificationCase`/`Hazard`/`ProblemReport`
records a `mdux::governance::ComplianceProgram` (issue #34) assembles; it does not copy
them. The moment a number in one of these documents can drift out of sync with the register it
describes, the document is already wrong — pointing at the register instead of copying from it is
what keeps that from happening silently.

## How these connect to the rest of the corpus

Every document here cites into `docs/<standard>/` (the clause-by-clause explanatory corpus) using
the citation-key format defined in
[`docs/governance/citation-convention.md`](../docs/governance/citation-convention.md), and may embed
inline `Justification` objects
(`docs/governance/schemas/justification.schema.json`)
tying a specific statement to a specific clause and its supporting evidence.

## `risk-assessment-templates.md` (repository root)

That 935-line file predates this tree and this project's citation/`Justification`/
`ComplianceProgram` machinery. It describes a `mdux::risk_assessment` C++ API
(`ComponentRiskProfile`, `RiskScreener`) that was never implemented, alongside generic CI/CMake
scaffolding unrelated to any real MduX build target. None of it cites a real clause, references a
real file, or reflects code that exists in this repository — it is exactly the kind of claim this
epic's "Note on scope honesty" warns against. It is deleted in this PR rather than folded in: there
is no real content in it worth preserving, and `templates/ISO_14971/Risk_Management_File.md` plus
the real `Hazard`/`Requirement` types (issue #35) are what a MduX-based risk file should actually
be built on instead.
