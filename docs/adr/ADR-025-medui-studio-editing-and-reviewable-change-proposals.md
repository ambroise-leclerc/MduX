# ADR-025: MedUI Studio editing and reviewable change proposals

## Status

**Proposed**, 2026-09-14, for [#327](https://github.com/ambroise-leclerc/MduX/issues/327), the last
child of epic [#311](https://github.com/ambroise-leclerc/MduX/issues/311). It builds on
[ADR-023](ADR-023-medui-host-editing-api-and-round-trip-source-contract.md) (the editing contract)
and [ADR-024](ADR-024-local-native-preview-service.md) (the preview service it extends). Maintainer
engineering review and domain review of the safety-metadata acknowledgement remain open.

## Shared contract

No MedUI shared decision governs an editor, a document view of the AST or a change-proposal workflow.
This record claims no shared profile and changes neither `medui-conformance.toml` nor any compiled
schema.

## Context

ADR-024 delivered previews through the production renderer and left "Studio assets, editing/saving and
proposals" to this issue, bound by ADR-023's two obligations on the first tool that writes source back:
negotiate schema versions fail-closed, and detect `//` comment loss and obtain acknowledgement before
incurring it.

TrustSC's `tools/trustsc-medui-studio` is the comparison target: a TypeScript frontend over DTOs that
mirror its authoring AST, and a `POST /api/proposals` route that builds a commit with git plumbing and
opens a pull request with `gh`. ADR-023 decision 1 already rejected porting its DTO layer. Its frontend
is written against those DTOs - every editor, inspector and palette module reads `NodeDefinitionDto`
fields such as `kind.text_key` and `safety_critical.cv_checks` - so reusing it would mean building the
rejected DTO family after all, plus a TypeScript toolchain and committed build output. What carries over
is the workflow shape: an in-memory document, a compile loop that keeps the last good frame, explicit
proposal submission, an optimistic source digest, and a comment-loss gate.

## Medical Device Considerations

Impact: **potentially safety-relevant authored-source integrity and review traceability**, per #327.
The affected prospective requirement is PAR-REQ-010 (editing must preserve logical fields, order,
safety annotations and trace ids). Hazards addressed:

- **An edit silently removes or alters authored safety metadata.** Control: the proposal route computes
  every node whose annotations or `requirement:` field were added, removed or changed
  (`safetyChanges()`), returns them on review, refuses submission without an explicit acknowledgement
  (`PRV011`), and writes them into the commit message and pull request body for the reviewer.
- **A document says one thing and compiles as another.** Control: names are checked against the
  lexer's identifier rule, and `sourceFromDocument()` refuses a document whose serialized text parses
  cleanly into a different screen.
- **An author proposes a screen that does not compile, or reviews a stale preview.** Control: the
  service compiles the proposed source before any git operation and refuses on failure; the Studio shows
  an invalid edit over a greyed, outlined copy of the last valid frame and disables proposing.
- **A proposal overwrites a concurrent change.** Control: the digest the author loaded must match both
  the served file and the file at the fetched base branch (`PRV009`), and the push can only create a
  branch.
- **Comment prose that explains a safety decision is lost.** Control: ADR-023 decision 3's gate,
  `PRV010`, now implemented.
- **Dynamic content is mistaken for device readings.** Unchanged from ADR-024: the Studio never
  invents fixture values; it lists required bindings with `null` placeholders the service refuses.

Nothing merges automatically. A proposal is a branch and, optionally, a draft pull request; the
repository's CI and maintainer review remain the gate. The Studio does not re-bake `generated/`, so a
source change that alters committed artifacts is expected to fail evidence checks until a developer
runs `mdux-bake-update`; the pull request body says so. No certification, validation or
production-readiness claim is made.

## Decision

### 1. Reuse the workflow, not TrustSC's frontend code

The Studio is a MduX frontend of four plain files (`tools/preview/studio/`) with no build step, no
package manager and no framework. It edits the AST document (decision 2) and draws selection overlays
from the compiler's IR (`screenIr()`), so every catalog it offers - components, fields, required flags,
theme tokens, named values - comes from `grammar()` at run time rather than from a copy. The files are
compiled into `mdux-preview` by `cmake/MduXEmbedStudio.cmake`; the service never serves a file from
disk.

### 2. A versioned JSON view of the unresolved AST

`mdux.tools.medui.document` (`MduXMeduiLib`, host tools zone) provides `screenDocument()`,
`readScreenDocument()` and `sourceFromDocument()` under `documentSchemaVersion = 1`, following
ADR-023 decision 2's versioning discipline. The document is the AST's own generic shape, so a field the
dictionary does not know survives an edit (ADR-023 decision 4). Reading refuses shape errors and
non-identifier names; it does not decide validity, which remains the compiler's. A document whose text
does not parse is compiled anyway so that its diagnostics reach the author.

The service offers the document only for a source that parses with no diagnostics, which is
ADR-023 decision 2's gate on recovered partial screens.

### 3. Fail closed on versions and unrecognised diagnostics

The Studio refuses to run against a catalog, IR or document schema version other than 1. A diagnostic
code absent from `grammar().diagnostics`, the retired list and the preview codes disables proposing
until the Studio is updated, while editing and previewing continue - the write path is where a
misinterpreted diagnostic would do harm.

### 4. Proposals are explicit, reviewed, acknowledged and isolated from the served checkout

`POST /api/proposals` takes the document, the loaded digest, an issue number, a branch slug, a base,
a title, a description, two acknowledgements and `dryRun`. A dry run validates everything except git and
returns the canonical source, comment loss and safety changes. Submission then requires:

- the service to have been started with `--proposal-git-dir` (otherwise `PRV008`);
- acknowledgements for comment loss (`PRV010`) and safety metadata changes (`PRV011`);
- the loaded digest to equal the served file and the file at the freshly fetched base (`PRV009`).

Branches follow AGENTS.md § 6: `<issue>-<slug>-<commit prefix>`, based on `develop` or an issue branch,
so CI attaches to the resulting pull request and a stacked base is expressible. All git commands name a
service-owned repository with `--git-dir`, which must lie outside the served root; the commit is built
in a temporary index with `read-tree`, `update-index`, `write-tree` and `commit-tree`; the push uses an
empty `--force-with-lease` expectation so it can create a branch and never move one. With
`--proposal-pull-requests`, `gh pr create --draft` opens a pull request; failing that is a warning,
because the branch already exists. No route merges, and no command writes the served checkout.

git and gh are external executables started without a shell by a bounded runner
(`tools/preview/Process.cpp`): arguments as a vector, no terminal prompts, three inherited streams, a
per-command timeout that kills the process tree, capped output, and credentials redacted from messages.
The runner is C++17 beside the transport, for the same header-isolation reason ADR-024 gives.

### 5. Browser access without weakening API authentication

The embedded files are served without a bearer token, because a browser cannot attach one to a page
load and the files are public build output. Every `/api/` route still requires the token, and every
route keeps ADR-024's Host and Origin checks. The page takes the token from a URL fragment, which is
never sent to the server, removes it from the address bar and keeps it in session storage. Pages carry
a restrictive Content-Security-Policy (`default-src 'none'`, self-hosted script and style only),
`X-Frame-Options: DENY` and `Referrer-Policy: no-referrer`.

## Alternatives Considered

- **Vendor TrustSC's TypeScript frontend and adapt it.** Rejected in decision 1: it would require the
  DTO family ADR-023 rejected, a TypeScript toolchain entry and committed build output, while every
  module that matters would still need rewriting against MduX's AST and IR.
- **Edit source text in the browser.** Rejected: canvas selection, move and resize need structure, and
  the browser would need a second `.medui` parser that could disagree with the compiler.
- **Build proposal commits in the served checkout** (TrustSC's approach writes objects and refs there).
  Rejected: #327 requires git isolation from the serving checkout, and a proposal must not change what
  the service is serving or lock its index.
- **Playwright or Selenium for browser tests.** Rejected for now: both are third-party packages with
  their own browser downloads. A standard-library DevTools Protocol client (`tests/preview/DevTools.py`)
  drives the installed Chrome, in keeping with the repository's stdlib-only host Python tools.
- **Treat safety metadata changes like any other edit.** Rejected: PAR-REQ-010 names them, and a
  reviewer reading a canonical-source diff can miss an annotation removed several screens away.

## Consequences

### Positive

- #327's workflow runs end to end through the production renderer and compiler, with every write
  explicit, reviewable and never merged by the tool.
- The document module gives any future host tool a tested, versioned AST view.

### Negative

- Only one backend request runs at a time, and a proposal holds the backend for its network operations;
  other requests receive `PRV007` until it finishes.
- A proposal changes source only. Artifacts under `generated/` need a developer's re-bake before CI is
  green.
- The browser test runs only where Chrome or Chromium exists: the macOS, Windows and Linux Clang legs,
  not the GCC container legs.

### Risks

- **A proposal is merged without re-baked evidence.** Mitigation: evidence byte-comparison fails CI, and
  the pull request body carries an unticked re-bake item.
- **An acknowledgement becomes a reflex.** Mitigation: the change list, with before and after metadata,
  is shown on review and recorded in the commit and pull request; maintainer review still applies.

## Implementation Notes

**#327** (2026-09-14):

- `tools/medui/Document.cppm`/`Document.cpp` and `tests/medui/DocumentTests.cpp` (`medui_tools_spec`):
  round trip over the accepted corpus, shape refusals including name injection, invalid edits reaching
  the compiler, comment detection matching the lexer, and safety change reporting.
- `tools/preview/Preview.cpp`: `document` and `proposals` routes, document overlays for `compile` and
  `frame`, catalog `documentSchemaVersion` and `proposals`, codes `PRV008`-`PRV012`.
- `tools/preview/Proposal.cpp`, `Process.cpp`, `Main.cpp` options `--proposal-git-dir`,
  `--proposal-remote` and `--proposal-pull-requests`; embedded Studio routes and headers.
- `tests/preview/ProposalTests.py` (`preview.proposals`): stale disk and base digests, invalid and
  injected documents, disabled writes, unreachable remotes, a rejecting pre-receive hook, both
  acknowledgements, a successful proposal's parent and single changed path, the non-GitHub pull request
  warning, and an unchanged served checkout (HEAD, refs, index, status and source).
- `tests/preview/StudioBrowserTests.py` (`preview.studio`): headless Chrome over the real service and
  GPU - fixture refusal, inspector edit, drag, resize, invalid-edit presentation, keyboard and button
  undo/redo, the required-field guard, palette insertion, and a proposal pushed to a bare repository.

## References

- [ADR-023](ADR-023-medui-host-editing-api-and-round-trip-source-contract.md)
- [ADR-024](ADR-024-local-native-preview-service.md)
- [Preview and Studio protocol](../tools/preview.md)
- [Prospective requirements](../parity/requirements.md) - PAR-REQ-010
- `tools/trustsc-medui-studio/README.md`, `src/proposals.rs`, `frontend/src/editor.ts` (TrustSC) - the
  workflow shape adapted here

## Approval

- **Proposal date**: 2026-09-14
- **Decision date**: pending
- **Approved by**: pending - maintainer engineering acceptance and domain review of the acknowledgement
  model, per #327's Impact section.
