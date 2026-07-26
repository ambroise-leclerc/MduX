# ADR-006: No reproduction of normative standard text

## Status
Accepted (2026-07-26)

## Context
This repository currently tracks copyrighted normative material verbatim or near-verbatim:

- `docs/MduX-IEC-62304-AI-Reference.md` (2,950 lines) opens by describing itself as *"a
  comprehensive markdown version of IEC 62304:2006, compiled from public source materials"*.
- `docs/MduX-ISO-13485-AI-Reference.md` (2,604 lines) follows the same pattern for ISO 13485.
- `inputs/Documentation/IEC-DIS-62304-2.pdf` (1.8 MB, the draft standard itself),
  `inputs/Documentation/IEC-62304-Complete.md`, `inputs/Documentation/ISO-13485-Complete.md`, and
  `inputs/Documentation/Medical Device Cybersecurity for Engineers and Manufacturers.md` (a
  copyrighted book) are all **tracked in git** — confirmed via `git ls-files inputs/`. A later
  `.gitignore` rule for `inputs/Documentation/*.pdf` postdates these files and never untracked them.

IEC and ISO standards, and the cybersecurity book, are copyrighted works. Reproducing or closely
paraphrasing their normative text — even for an AI-optimization purpose, even with attribution —
is not something this project has a license to do, and self-describing a file as a "markdown
version of" a standard is itself an admission of the problem.

MduX's sibling Rust project, [TrustSC](https://github.com/ambroise-leclerc/TrustSC), already solved
this: its regulatory corpus is original explanatory prose written against each standard's real
clause numbers and titles, never a transcription. This ADR adopts the same rule for MduX.

## Medical Device Considerations

### IEC 62304 / ISO 13485 / IEC 62366-1 / ISO 14971 / IEC 81001-5-1 Implications
- A compliance corpus that cannot be redistributed cannot be safely handed to a manufacturer
  building on MduX, cited in a `software_development_file/` document that a manufacturer submits
  onward, or published as part of MduX's own documentation. Removing reproduced text is a
  precondition for the entire regulatory-documentation direction of this project (issues #8, #9),
  not a nice-to-have cleanup.
- Original prose that explains a clause's *intent* and points at a real MduX mechanism is more
  useful to an implementer than a transcription of the clause's legal text would be — the
  transcription is available from the standards body directly; what MduX can uniquely provide is
  the mapping from clause to mechanism.

## Decision

1. **Never reproduce or closely paraphrase a standard's normative text**, anywhere in this
   repository — documentation, code comments, commit messages, or generated artifacts. Explain a
   requirement in original prose against its real clause number and title, then point at a concrete
   MduX mechanism (a type, an ADR, a CI step, a file path) that addresses it. A citation with no
   mechanism behind it is a sign the citation is decorative rather than load-bearing, and should be
   rewritten or removed.

2. **Citation-key format**: `<Standard> §<clause> <Short clause title>` — e.g.
   `IEC 62304:2006 §5.2 Software development planning`. Valid standard identifiers (closed set):
   `IEC 62304:2006`, `ISO 13485:2016`, `ISO 14971:2019`, `IEC 62366-1:2015`, `IEC 81001-5-1:2021`.
   Use this string verbatim as a module heading, an `AI-Reference.md` row, or a `clause_ref` field,
   so it can be checked mechanically. The full convention document and the shared `Justification`
   object schema are tracked separately (issue #8, S1) — this ADR fixes the format and the rule;
   that follow-on issue delivers the detailed spec and JSON Schema.

3. **Remove existing violations from the working tree** (issue #7, S2-S3): the two
   `MduX-*-AI-Reference.md` files and the four files under `inputs/Documentation/`. Their
   replacement — a clause-accurate, original-prose corpus — is tracked separately (issue #8) and
   is not blocked on waiting for this ADR; removal and rewrite may land in either order, but removal
   should not wait indefinitely for the rewrite to be ready.

4. **Purge from git history where feasible** (issue #7, S4). Deleting a file from `HEAD` does not
   remove a 1.8 MB copyrighted PDF from every existing clone or from GitHub's blob storage. A
   history rewrite (`git filter-repo` + force-push) is the correct fix, but it is destructive to
   every existing clone and open branch and requires explicit maintainer sign-off separate from this
   ADR's adoption — this ADR records the obligation; it does not itself authorize the rewrite.

5. **Enforce mechanically going forward** (issue #7, S6): a `mdux-docs-lint` CI check validating
   citation-key format and flagging files that self-describe as a transcription of a standard or
   that contain long runs of numbered normative-style clauses.

## Alternatives Considered

### 1. Keep the material but add a copyright disclaimer (Rejected)
**Pros:** No content loss; minimal effort.
**Cons:** A disclaimer does not change whether reproduction is licensed. It also does not solve the
actual problem this project has with the material — the two AI-Reference files are not more useful
for being a transcription; original prose mapped to real mechanisms is strictly more useful *and*
lawful.

### 2. Keep the material but stop distributing it (private submodule, gitignore going forward) (Rejected)
**Pros:** Avoids rewriting history immediately.
**Cons:** Does not address that the material is already in every existing clone and in GitHub's
history. A private submodule adds an access-control problem without removing the underlying
copyright problem, and it contradicts the project's practice of a single, cloneable, self-contained
repository.

### 3. Full rewrite before any removal (sequence #8 before #7) (Rejected)
**Pros:** No period where the corpus is "gone" before its replacement exists.
**Cons:** The copyright exposure exists today and does not become more acceptable while a rewrite
is drafted. Removing the violating material is the higher-priority, independently correct action;
the AI-Reference gap it leaves is a documented, temporary regression (see issue #7, S3), not a
reason to leave copyrighted material in place.

## Consequences

### Positive
- Closes a real legal-exposure gap rather than an aesthetic one.
- Forces every future citation to be original prose pointing at a real mechanism, which is a
  stronger documentation discipline than transcription-and-annotate would ever produce.
- Aligns MduX's documentation practice with its Rust sibling, which is a stated goal of the parity
  programme independent of this specific issue.

### Negative
- Deleting the two `AI-Reference.md` files removes a working (if legally unsound) per-clause index
  until issue #8's replacement lands — a temporary regression in documentation navigability.
- A git history purge, if and when authorized, invalidates every existing clone and requires
  coordinating force-pushes across active branches.

### Risks and Mitigations
- **Someone re-adds reproduced text out of convenience** (e.g. pasting a clause to "get the wording
  exactly right"). *Mitigation*: the `mdux-docs-lint` CI check (issue #7, S6) and this ADR's explicit
  rule that intent-in-original-prose is what's wanted, not legal precision of wording.
- **The history purge never happens** because it's disruptive. *Mitigation*: issue #7, S4 requires
  an explicit decision either way — silently leaving it undecided is not an acceptable outcome, and
  if the rewrite is declined, the residual risk must be recorded rather than ignored.

## References
- [TrustSC docs/governance/citation-convention.md](https://github.com/ambroise-leclerc/TrustSC/blob/main/docs/governance/citation-convention.md)
- ADR-004: Trust zones in C++ (this repository) — the parity programme this ADR is part of

## Approval
- **Decision Date**: 2026-07-26
- **Approved By**: Project maintainer
- **Review Date**: when issue #7 (S3-S4, removal and history purge) is resolved
