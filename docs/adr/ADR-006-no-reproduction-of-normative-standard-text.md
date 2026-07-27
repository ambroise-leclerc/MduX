# ADR-006: No reproduction of normative standard text

## Status
Accepted (2026-07-26)

## Context
Epic #7 records six paths as previously identified standards or book material:

- `docs/MduX-IEC-62304-AI-Reference.md`;
- `docs/MduX-ISO-13485-AI-Reference.md`;
- `inputs/Documentation/IEC-DIS-62304-2.pdf`;
- `inputs/Documentation/IEC-62304-Complete.md`;
- `inputs/Documentation/ISO-13485-Complete.md`; and
- `inputs/Documentation/Medical Device Cybersecurity for Engineers and Manufacturers.md`.

None of those paths is present in this PR's `HEAD`; both `git ls-tree` locally and GitHub's tree API
return no match. Their absence from the current tree does not establish whether the corresponding
blobs remain reachable through older branches, tags, pull-request refs, or other repository
history. Issue #21 owns the authoritative inventory of current content and reachable history.

IEC and ISO state that reproducing their publications requires written permission. This repository
does not document such permission for the standards material reported in epic #7, nor does it
record a redistribution grant for the reported cybersecurity book. The project therefore treats
any such material found by the inventory as not approved for redistribution unless applicable
permission is documented. Attribution alone does not establish that permission, and
self-describing a file as a "markdown version of" a standard directly conflicts with this policy.

MduX's sibling Rust project, [TrustSC](https://github.com/ambroise-leclerc/TrustSC), already solved
this: its regulatory corpus is original explanatory prose written against each standard's real
clause numbers and titles, never a transcription. This ADR adopts the same rule for MduX.

## Medical Device Considerations

### IEC 62304 / ISO 13485 / IEC 62366-1 / ISO 14971 / IEC 81001-5-1 Implications
- A compliance corpus with no documented redistribution permission cannot be safely handed to a
  manufacturer building on MduX, cited in a `software_development_file/` document that a
  manufacturer submits
  onward, or published as part of MduX's own documentation. Removing reproduced text is a
  precondition for the entire regulatory-documentation direction of this project (issues #8, #9),
  not a nice-to-have cleanup.
- Original prose that explains a clause's *intent* and points at a real MduX mechanism is more
  useful to an implementer than a transcription of the clause's legal text would be — the official
  publication is available from the standards body directly; what MduX can uniquely provide is
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
   Use this exact, unmodified string as a module heading, an `AI-Reference.md` row, or a
   `clause_ref` field, so it can be checked mechanically. The full convention document and machine-readable JSON Schema
   are tracked separately (issue #8, S1); the semantic fields are fixed here.

3. **Justification object**: every formal link from a design decision to a clause contains:
   - `justification_id`: `JUS-NNN`, unique across the entire corpus;
   - `standard`: one identifier from the closed set in item 2;
   - `clause_ref`: the complete citation key in item 2's format;
   - `rationale`: original prose explaining how the decision addresses the clause;
   - `requirement_id`: optional link to a project requirement; and
   - `evidence_refs`: repository-relative paths to the concrete evidence supporting the rationale.

   The documentation lint validates identifier uniqueness, citation consistency, and that each
   evidence path exists. The follow-on issue supplies serialization details and the JSON Schema
   without changing these fields.

4. **IEC 62304 safety-class scope**: unlike TrustSC's Class B/C-only model, MduX keeps Class A, B,
   and C software items in scope. Original prose and evidence depth may be proportionate to the
   applicable safety classification, but the citation and no-reproduction rules apply to all three.

5. **Remove confirmed violations from the working tree** (issue #7, S2-S3). Issue #21 first
   inventories current content and distinguishes reproduction, close paraphrase, and original
   explanatory prose. The six historical paths above are candidates for history inspection, not
   working-tree deletion while they remain absent from `HEAD`. Replacement with a clause-accurate,
   original-prose corpus is tracked separately (issue #8); confirmed current-tree violations do not
   need to wait for that rewrite before removal.

6. **Evaluate a coordinated history purge** (issue #7, S4). Deleting a file from `HEAD` does not
   remove it from existing history, clones, forks, pull-request references, or cached views. A
   `git filter-repo` rewrite plus force-push may remove reachable central-repository references, but
   it is not sufficient by itself: collaborators and fork owners must clean their copies, old
   branches must be rebased rather than merged, and remaining GitHub references or caches require
   consultation with GitHub Support about the applicable removal process. The rewrite changes
   commit identifiers and disrupts active branches, so it requires explicit maintainer sign-off
   separate from this ADR's adoption. This ADR records the need for a decision; it does not itself
   authorize the rewrite.

7. **Add mechanical checks going forward** (issue #7, S6): a `mdux-docs-lint` CI check validates
   citation-key format and flags files that self-describe as a transcription of a standard or
   contain long runs of numbered normative-style clauses. This is a heuristic guard, not proof that
   prose is original or licensed; review remains required for flagged content and uncertain cases.

## Alternatives Considered

### 1. Keep the material but add a copyright disclaimer (Rejected)
**Pros:** No content loss; minimal effort.
**Cons:** A disclaimer does not change whether reproduction is licensed. It also does not solve the
policy problem reported for the historical material — a transcription is not more useful than
original prose mapped to real mechanisms, and the latter is consistent with the publishers' stated
permission requirements and this project's policy.

### 2. Keep the material but stop distributing it (private submodule, gitignore going forward) (Rejected)
**Pros:** Avoids rewriting history immediately.
**Cons:** If the inventory confirms that material remains reachable in repository history, moving
future copies to a private submodule does not remove those existing objects, refs, forks, or clones.
It also adds an access-control problem and contradicts the project's practice of a single,
cloneable, self-contained repository.

### 3. Full rewrite before removal of any confirmed violation (sequence #8 before #7) (Rejected)
**Pros:** No new period where an existing corpus disappears before its replacement exists.
**Cons:** Once the inventory confirms a current-tree violation, its undocumented redistribution
risk is not reduced while a rewrite is drafted. Removing confirmed material is the higher-priority
policy action; any temporary reference gap is tracked in issue #7, S3 and is not a reason to retain
material without documented permission.

## Consequences

### Positive
- Reduces a documented redistribution and licensing risk rather than performing an aesthetic
  cleanup.
- Forces every future citation to be original prose pointing at a real mechanism, which is a
  stronger documentation discipline than transcription-and-annotate would ever produce.
- Aligns MduX's documentation practice with its Rust sibling, which is a stated goal of the parity
  programme independent of this specific issue.

### Negative
- The two historical `AI-Reference.md` paths are already absent from this PR's tree, leaving the
  planned replacement corpus in issue #8 responsible for restoring per-clause navigability.
- A git history purge, if and when authorized, changes commit identifiers, requires coordination
  across active branches, and cannot alter copies already present in other clones or forks.

### Risks and Mitigations
- **Someone re-adds reproduced text out of convenience** (e.g. pasting a clause to "get the wording
  exactly right"). *Mitigation*: the heuristic `mdux-docs-lint` CI check (issue #7, S6), mandatory
  review for flagged or uncertain content, and this ADR's explicit rule that
  intent-in-original-prose is wanted rather than copied normative wording.
- **The history purge never happens** because it's disruptive. *Mitigation*: issue #7, S4 requires
  an explicit decision either way — silently leaving it undecided is not an acceptable outcome, and
  if the rewrite is declined, the residual risk must be recorded rather than ignored.

## References
- [TrustSC docs/governance/citation-convention.md](https://github.com/ambroise-leclerc/TrustSC/blob/main/docs/governance/citation-convention.md)
- [ISO copyright and permissions](https://www.iso.org/copyright.html)
- [IEC copyright and permissions](https://webstore.iec.ch/en/copyright)
- [GitHub: Removing sensitive data from a repository](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository) (history-rewrite mechanics and limitations)
- ADR-004: Trust zones in C++ (this repository) — the parity programme this ADR is part of

## Approval
- **Decision Date**: 2026-07-26
- **Approved By**: Project maintainer
- **Review Date**: when issue #7 (S3-S4, removal and history purge) is resolved
