# Inventory of reproduced or paraphrased standard text (issue #21)

This is the audit trail issue #21 asks for: every path this repository has ever tracked that
reproduced, transcribed, or paraphrased copyrighted normative text or a copyrighted book, found by
walking full git history rather than only the current tree. It is the scope document issue #23
(purge from git history) works from — nothing here should be purged without this list agreeing.

## Status of what it lists

**Resolved.** Every path below was already gone from the working tree on `develop` — removed by
issue #22 (working tree) and issue #24 (deleting `inputs/` and `libstd/` outright). Issue #23 has
since purged all 482 paths from history as well, via `git filter-repo`, across every branch that
still carried them (`master`, `develop`, `metautilsCecile`, and any open PR branch at the time).
Verified after the rewrite: zero remaining matches on every surviving ref, and the GitHub-reported
repository size dropped from 92 MB to 1.5 MB. See issue #23 for the branch-disposition decisions
and the execution record.

## What "reproduced or paraphrased" turned out to mean

The epic body for #7 described the problem as a 1.8 MB draft standard PDF and two long markdown
transcriptions. Walking full history (`git log --all --diff-filter=A --name-only`) found a larger
problem: **482 paths, 889 unique blob versions, ~105 MB of unique historical content**, not 3
files.

| Category | Count | What it is |
|---|---|---|
| Page-scan images | 303 `.jpg` | Every page of *Medical Device Cybersecurity for Engineers and Manufacturers*, exported as `..._page_0001.jpg` through `..._page_0303.jpg` — a complete page-by-page image reproduction of a copyrighted book, not excerpts. |
| Screenshot images | 87 `.png` | On-screen captures under `inputs/Documentation/ISO 13485/` (46) and `inputs/Documentation/ISO 14971 The Definitive Guide/` (41) — the standard and a second copyrighted book, captured rather than typed. An 88th `.png` (`inputs/Logo.png`) is unrelated cruft, listed below instead. |
| Full-text transcriptions | 6 `.md` | `docs/MduX-IEC-62304-AI-Reference.md` (2,950 lines), `docs/MduX-ISO-13485-AI-Reference.md` (2,604 lines), `inputs/Documentation/IEC-62304-Complete.md`, `inputs/Documentation/ISO-13485-Complete.md`, `inputs/Documentation/Medical Device Cybersecurity for Engineers and Manufacturers.md`, and `inputs/Documentation/ISO 14971 The Definitive Guide/ISO_14971_The_Definitive_Guide.md`. The first opens by describing itself as a markdown version of the standard, compiled from public source materials — the exact phrasing ADR-006 quotes and rejects. <!-- mdux-docs-lint:allow-reproduction-marker --> |
| Original-format documents | 6 `.pdf` + 1 `.epub` | The draft standard (`IEC-DIS-62304-2.pdf`, 1.8 MB) and `NF62304-2006.pdf`, plus four further copyrighted books in PDF or EPUB form, 3.9–11.4 MB apiece. |
| Vendored library headers | 74 files | `libstd/include/**` (plus a `readme.md`) — a full copied C++ standard-library header set of unclear provenance and licence. Not a *standard's* text, but the same "should never have been committed" class of problem, and issue #24 already scoped its removal alongside `inputs/`. |
| Unrelated cruft swept into the same tree | 5 files | `inputs/CMake`, `inputs/CompilerSettings.cmake`, `inputs/Linker.cmake`, `inputs/webfrontCmakelists.txt`, `inputs/Logo.png` — not a copyright problem, but tracked under the same directory issue #24 deleted, and included here so history matches what the tree removal actually covered.

## A second, larger problem this inventory surfaced — since resolved

Removing these paths from `develop`'s working tree and history is not the same as removing them
from the repository. A history purge only removes content that no reachable ref still carries. At
the time of this inventory, **32 other remote branches still had `inputs/` and `libstd/` present
at their current tip** — not merely in old history, but checked out at HEAD right now, on every
clone that fetched them:

- **18 were fully merged into `develop` already** (zero commits `develop` didn't have — fully
  superseded): `5-configure-an-agentsmd-for-ai-support`, `57bitset`, `8string_view`, `=r`,
  `BiggerPR`, `ambroise-leclerc-patch-1`, `ambroise-leclerc-patch-2`, all nine `foundations/*`
  branches, `regulatory/adr-no-reproduction-of-standard-text`, `uartEsp`.
- **14 carried commits `develop` didn't have** — from 1 to 15 apiece: `#17` (2), `35tuple` (2),
  `45uniqueptr` (1), `77bitset` (1), `8stringview` (3), `BigPR` (15), `LLVMStyle` (1), `_vTraits`
  (11), `gcc16` (1), `inline` (3), `metautilsCecile` (2), `ostream` (4), `pin16` (1), `wifi` (6).
  Thirteen were 2015–2018 work from an unrelated prior project (embedded-library experiments,
  referencing "ETL", Atmel Studio, ESP8266 code); `gcc16` was a stale MduX snapshot superseded by
  the CI work landed in issue #48.

**Resolution:** 31 of the 32 were deleted outright (the 18 merged plus 13 of the 14, all
maintainer-confirmed as abandoned or superseded). `metautilsCecile` was kept, at the maintainer's
request, for its own unrelated content — it went through the same `git filter-repo` pass as every
other branch, so it survives without the copyrighted material.

## What this inventory does not claim

A git history purge — however the branch question above is resolved — reduces future exposure
from this repository's own refs. It cannot retroactively guarantee removal from anyone who has
already cloned the old history, from GitHub's server-side caches of now-unreachable commits, or
from any fork. That limitation is inherent to how git and GitHub work, not a gap in issue #23's
execution; it belongs in this record so the purge is never described as more complete than it is.
