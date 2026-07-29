# IEC 62304:2006 §8 — Software configuration management process

Configuration management is how a manufacturer knows, precisely, what a piece of software consists
of: which items, at which versions, built from which inputs — and controls how that composition
changes.

## §8.1 Configuration identification

This sub-clause asks that a software item be uniquely identifiable, such that its exact composition
can be recovered later. MduX's evidence pipeline ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md))
is a direct, mechanical answer for every artifact it covers: `mdux.evidence.report` records a
recipe's digest, every input's digest, the fully resolved options a baker ran with (defaults
expanded, not just what the recipe literally said), and each output's digest — a `report.json` is
a configuration identification record for exactly one artifact, machine-checkable rather than
narrative.

```json
{
  "justification_id": "JUS-006",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §8.1 Configuration identification",
  "rationale": "BakeReport records the recipe digest, every input digest, the fully resolved options, and every output digest for a baked artifact - a machine-checkable configuration identification record, not a narrative one.",
  "evidence_refs": ["include/mdux/evidence/Report.cppm", "docs/adr/ADR-007-evidence-pipeline-doctrine.md"]
}
```

Source-level configuration identification is git itself: every file in this repository is
identified by the commit that introduced or last changed it, which is why issue #23 could purge
specific historical content by path without disturbing anything else's identity.

## §8.2 Change control

Change control governs how a configuration item's composition is allowed to change. For MduX's
baked artifacts, this is the source-tree rule from ADR-007: a normal build never writes into
`generated/`; `cmake --build <dir> --target mdux-bake-update` is the only path that does, and it
produces a diff a reviewer reads and a commit records — a build cannot silently redefine an
artifact's configuration. For source code generally, this repository's branch protection
(no direct pushes to `master`, pull request required) is the change-control mechanism: `master`'s
configuration can only change through a reviewed, recorded pull request.

## §8.3 Configuration status accounting
<!-- pointer: git log and git blame answer this for every tracked file, and the evidence pipeline extends the same answer to baked artifacts; ADR-007 decision 5 explains why no separate status-accounting document exists. -->

Status accounting is being able to answer "what is the current configuration, and how did it get
here" without reconstructing it from memory. `git log` and `git blame` already answer this for
every tracked file; the evidence pipeline extends the same answer to baked artifacts specifically,
since a baked binary's provenance is not otherwise visible from the git history of the files that
produced it. There is deliberately no separate status-accounting *document* — see
[ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md), decision 5, for why duplicating what git
already provides was rejected even before it turned out to be self-referentially broken.
