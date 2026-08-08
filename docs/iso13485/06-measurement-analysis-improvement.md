# ISO 13485:2016 §8 — Measurement, analysis and improvement

## §8.1 General

Planning the monitoring, measurement, analysis, and improvement processes a QMS needs to
demonstrate conformity and maintain effectiveness. MduX's CI matrix (three toolchains, the evidence
byte-identity checks, `mdux-docs-lint`, `mdux-evidence-lint`) is the concrete form this takes here:
a fixed, versioned set of checks that run on every change, not a process reconstructed ad hoc per
review.

## §8.2 Monitoring and measurement

```json
{
  "justification_id": "JUS-010",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §8.2 Monitoring and measurement",
  "rationale": "The evidence-kernel and host-tools test suites (91 cases as of the epic that introduced them) run on every push to main or develop and on every pull request whose base branch matches main, develop, the issue-derived work-branch scheme [0-9]+-*, or the legacy feat/**, across the two toolchains automatic CI exercises - MSVC and GCC 16; a Clang workflow exists but is manual-dispatch only, so it contributes no automatic coverage - giving continuous, reproducible monitoring of whether the codebase still meets its own specified behaviour, not a periodic audit sample.",
  "evidence_refs": ["tests/evidence/", "tests/tools/", ".github/workflows/windows-build.yml", ".github/workflows/linux-gcc16-build.yml"]
}
```

Customer feedback and complaint handling, the other half of this sub-clause, has no MduX mechanism:
there is no released product yet with users to collect feedback from.

## §8.3 Control of nonconforming product

A failed CI check is MduX's nonconforming-product control: a pull request whose evidence
byte-comparison, trust-zone check, or lint fails cannot merge, which is nonconformity contained at
the point of detection rather than shipped and corrected later. There is no equivalent for a
released artifact yet, because there is no release yet.

## §8.4 Analysis of data

No dedicated MduX mechanism. CI results are visible per pull request but not yet aggregated or
trended over time; that would be a real gap once a released product exists to trend data about.

## §8.5 Improvement
<!-- pointer: GitHub issues and pull requests are MduX's CAPA analog, with no effectiveness verification; mdux.governance (issue #34) is where a purpose-built mechanism would land. -->

Corrective and preventive action, in this repository, is the GitHub issue and pull request process
— an issue records a problem, a PR records the analysis and fix. As noted in
[`../iec62304/07-problem-resolution-process.md`](../iec62304/07-problem-resolution-process.md),
this is general-purpose project tracking, not a mechanism purpose-built to this standard's
requirements for CAPA (in particular, verifying an action's *effectiveness* after the fact, which
GitHub's own tooling does not do automatically). `mdux.governance` (issue #34) is where a more
direct mechanism would need to land, not before.
