# Superseded documents and their disposition

Every point-in-time document this project has retired, why, and what replaced it. A reader who
finds a reference to one of these paths in an old commit message, issue, or branch should find the
answer here rather than concluding the content was lost.

This is a *disposition* record, not the copyright purge. The files listed here were MduX's own
prose; nothing here reproduced normative standard text. That inventory is
issue #21's `reproduced-text-inventory.md`, and the two lists are disjoint on
purpose — a document can be wrong without being a copyright problem, and confusing the two would
make both records harder to trust.

## Why these were deleted rather than archived in-tree

Git history is the archive. A superseded document kept in the tree is a document that still gets
found by search, still gets cited, and still gets quoted back at the project — which for these
files means an unsupportable compliance claim re-entering circulation through a path nobody
intended. `git log --follow --diff-filter=D -- <path>` recovers any of them in full.

Epic #10's S6 ("Archive the point-in-time docs") may later define a published archive location for
documents worth preserving as historical artifacts. Nothing below qualifies: each was retired
because its content was *wrong*, not merely because it was old.

## Disposition

| Path | Lines | Retired by | Replaced by | Why |
|---|---|---|---|---|
| `docs/MduX_ISO-13485-Quality-Management-Framework.md` | 1,423 | issue #28 | [`docs/iso13485/`](../iso13485/) | Described MduX as fulfilling "the role of Medical Device Software Manufacturer" and as implementing an ISO 13485 quality management system. MduX is a software library: it runs no management review, no CAPA, no supplier qualification, and has no customers of its own. The clause-by-clause corpus that replaces it states for each clause either the concrete MduX mechanism or that none exists. |
| `docs/iso13485/ai-automation-schemas/*.json` | 3 files | issue #28 | [`docs/iso13485/schemas/quality-management-system.schema.json`](../iso13485/schemas/quality-management-system.schema.json) | Modelled an organisation profile, regulatory jurisdictions, and a device portfolio — a manufacturer's QMS, not anything this repository holds or can populate. The replacement records only the interface a library actually has with a manufacturer's QMS. |
| `docs/iso13485/code-examples/**/*.py` | 3 files | issue #28 | — | Python illustrations of the automation the schemas above described. Nothing in MduX runs Python; the examples demonstrated processes that do not exist, in a language the project does not use. |
| `docs/MduX_ISO-14971-Risk-Management-Framework.md` | 713 | issue #29 | [`docs/iso14971/`](../iso14971/) | Superseded by the clause-by-clause corpus, which states for each clause whether MduX has a genuine mechanism or whether the clause belongs to the integrating manufacturer. |
| `risk-assessment-templates.md` | 935 | issue #29 | [`docs/iso14971/schemas/risk-record.schema.json`](../iso14971/schemas/risk-record.schema.json) | Documented three C++ namespaces that do not exist in the tree — `mdux::risk_assessment`, `mdux::static_analysis`, `mdux::traceability` — and prescribed severity and probability scales, which ISO 14971 §4.4 makes the manufacturer's to define. Issue #29 offered folding it into `software_development_file/templates/` as an alternative to deletion; folding it in would have carried a fictional API into the templates a manufacturer is meant to use. The reusable part of it was the record *shape*, which the risk-record schema now states in machine-checkable form. |
| `docs/adr/ADR-002-implementation-plan.md` | 267 | issue #108 | [`docs/adr/ADR-009-in-repository-test-framework.md`](../adr/ADR-009-in-repository-test-framework.md) | A week-by-week plan for integrating Catch2 v3 — a framework this project never adopted. It shared ADR-002's number without being an independent decision, which is what made the ADR index non-contiguous and left two files presenting as ADR-002. It is retired rather than renumbered: renumbering it into a new decision would give an implementation schedule for abandoned work the standing of an architectural decision. ADR-009 records the framework actually in use. |
| `docs/MduX_IEC-62304-Software-Lifecycle-Framework.md` | 3,205 | issue #112 | [`docs/iec62304/`](../iec62304/) plus the `software_development_file/` records | Documented an entire API that does not exist: `mdux::iec62304::SoftwareLifecycleManager` with `isCompliantWithIEC62304()` and `integratWithQMS()`, alongside ten further fictional namespaces (`mdux_hazards`, `mdux_risk_controls`, `mdux_release`, `mdux_problems` and others). None of it is in the tree; the only real `iec62304` strings in the source are citation keys in `mdux.governance`. A reader could not tell the difference, and a document that presents `bool isCompliantWithIEC62304() const` as available API is worse than no document — it invites a compliance claim the code cannot support. The clause corpus that replaces it states, per clause, either the concrete MduX mechanism or that none exists. |

## What retirement does not mean

Deleting a document that overclaimed does not by itself make the remaining documentation correct.
The corpus that replaced these files describes clauses and points at mechanisms; it does not
certify compliance, and no document in this repository does. See `docs/regulatory-compliance.md`
for the scope limits this project claims once issue #39 lands it, and
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) for the rule that keeps
normative text out of the tree in the first place.
