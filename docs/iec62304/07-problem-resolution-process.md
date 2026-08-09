# IEC 62304:2006 §9 — Software problem resolution process

§9 asks for a single, consistent route from "a problem was reported" to "it was recorded,
evaluated, and resolved" — spanning every software item and every phase, not just problems found
during development.

## Problem resolution

MduX's problem-resolution route today is GitHub Issues and pull requests: an issue records the
problem, a linked PR records the analysis and the fix, and the merge commit records when and by
whom it was resolved. This is a real, working mechanism — every issue referenced throughout this
documentation set (`#20` through `#39` and beyond) is an instance of it — but it is general-purpose
project tracking, not a mechanism purpose-built to IEC 62304's problem-resolution requirements
(e.g. evaluating whether a given problem could itself be a symptom of a broader risk-control
failure, which requires an explicit link back to §7). That link is one `mdux.governance` (issue #34)
and its traceability export (issue #35) are positioned to close; this file states the gap rather
than asserting the general-purpose mechanism already closes it.

A CI failure is this project's fastest problem-resolution loop in practice: `mdux_verify_trust_zones()`,
the evidence-kernel byte-identity checks, and `mdux-docs-lint` all turn a specific class of problem
into a failed pull request before it merges, which is a faster and more consistent resolution path
than any of them would be as a manually-followed procedure.
