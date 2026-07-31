# Software development file

This tree is the `software_development_file/` scaffold named in issue #9's epic. It has two
subtrees, mirroring each other standard-by-standard:

```
software_development_file/
├── templates/    # blank, fillable by any manufacturer building a device on MduX
└── regulatory/   # the same tree, filled in for MduX itself (issue #38, a later PR in this stack)
```

Only `templates/` exists as of this PR — `regulatory/` is added in the PR immediately after it
(issue #38).

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

*(Added in the PR immediately after this one, issue #38 — not present yet as of this PR.)*

The same documents, filled in for MduX itself: real architecture description, real SOUP entries
(derived from `docs/governance/soup-register.toml`), real
citations into `docs/<standard>/` and the ADR trail. These describe MduX as a software development
kit — they are not, and do not claim to be, a finished medical device's regulatory file. See
`docs/regulatory-compliance.md` (issue #39, not yet written) for the scope disclaimer covering what
this project does and does not provide.

## The rule that keeps these documents honest

**A filled document summarizes, and never duplicates, a machine register.** A SOUP document points
at `docs/governance/soup-register.toml`; it does not
restate its rows. A risk file points at the `Requirement`/`VerificationCase`/`Hazard`/`ProblemReport`
records a `mdux.governance.compliance::ComplianceProgram` (issue #35) assembles; it does not copy
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
