# ISO 13485:2016 — per-clause index

One row per clause section in this corpus: the clause, a one-sentence pointer to what
MduX does or does not provide for it, and a deep link to the heading that covers it.
Generated from the headings, `Justification` objects and pointer comments already
present in the modules — not hand-transcribed, so it cannot drift from the prose it
indexes without the source changing too.

Regenerate after editing any file in this directory:

```
python3 tools/docs-lint/generate_ai_reference.py docs/iso13485
```

A clause shown as `—` is one this corpus deliberately does not number — see
[`README.md`](README.md). It is not a gap in the index.

| Clause | Covers | Pointer | Justification(s) |
|---|---|---|---|
| §1 | [Scope](01-scope-and-terms.md#1-scope) | MduX is a software library, not the organization operating a device's QMS. | — |
| §2 | [Normative references](01-scope-and-terms.md#2-normative-references) | MduX's own regulatory corpus covers IEC 62304 ([`../iec62304/`](../iec62304/)) directly; [`../iso14971/`](../iso14971/) is where risk management gets the same clause-accurate treatment. | — |
| §3 | [Terms and definitions](01-scope-and-terms.md#3-terms-and-definitions) | The terms below recur across this directory; each points at where MduX makes the concept concrete rather than defining it in prose. | — |
| §4.1 | [General requirements](02-quality-management-system.md#41-general-requirements) | A manufacturer integrating MduX does not control MduX's internal build process directly, but ADR-007's evidence pipeline gives them a re-derivable, byte-verified record of exactly what produced any given MduX artifact - the concrete control an outsourced-process requirement asks for. | JUS-007 |
| §4.2 | [Documentation requirements](02-quality-management-system.md#42-documentation-requirements) | MduX's documented-information control is git itself: every file's history, the branch-protection rule that changes to `master` go through a reviewed pull request (not a direct push), and the ADR set's own "Status" field (Proposed / Accepted / Superseded) marking which decisions are current. | — |
| §5.1–§5.3 | [Management commitment, customer focus, quality policy](03-management-responsibility.md#5153-management-commitment-customer-focus-quality-policy) | No MduX-specific mechanism. | — |
| §5.4 | [Planning](03-management-responsibility.md#54-planning) | No MduX mechanism: the GitHub epic and issue structure is project planning, not the quality-objective planning this clause asks a certified organization for. | — |
| §5.5 | [Responsibility, authority and communication](03-management-responsibility.md#55-responsibility-authority-and-communication) | MduX's closest concrete instance is the branch protection requiring pull requests before `master` changes — an authority boundary enforced by GitHub, not only stated in a policy document. | — |
| §5.6 | [Management review](03-management-responsibility.md#56-management-review) | No MduX-specific mechanism. | — |
| §6.1–§6.2 | [Provision of resources, human resources](04-resource-management.md#6162-provision-of-resources-human-resources) | No MduX-specific mechanism. | — |
| §6.3 | [Infrastructure](04-resource-management.md#63-infrastructure) | MduX version-controls and reviews its build infrastructure as source, which is infrastructure control of the build only, not of the facilities and equipment this clause covers. | — |
| §6.4 | [Work environment and contamination control](04-resource-management.md#64-work-environment-and-contamination-control) | No MduX-specific mechanism. | — |
| §7.1 | [Planning of product realization](05-product-realization.md#71-planning-of-product-realization) | MduX's ADRs play this role for the decisions they cover: each one states the decision, the alternatives considered, and what verification the decision implies, ahead of the implementation that follows it. | — |
| §7.2 | [Customer-related processes](05-product-realization.md#72-customer-related-processes) | No MduX-specific mechanism. | — |
| §7.3 | [Design and development](05-product-realization.md#73-design-and-development) | MduXTrustZones.cmake is a design output verified mechanically at every build (a governed target's link graph cannot reach Vulkan or a windowing library), and ADR-004 is the design record explaining why - together they give a design-and-development control that runs on every change, not only at a scheduled review. | JUS-008, JUS-009 |
| §7.4 | [Purchasing](05-product-realization.md#74-purchasing) | No MduX-specific mechanism in the standard's sense (evaluating and controlling suppliers). | — |
| §7.5 | [Production and service provision](05-product-realization.md#75-production-and-service-provision) | For MduX, "production" is the baking process that turns a recipe and its inputs into a committed artifact. | — |
| §7.6 | [Control of monitoring and measuring equipment](05-product-realization.md#76-control-of-monitoring-and-measuring-equipment) | No MduX-specific mechanism. | — |
| §8.1 | [General](06-measurement-analysis-improvement.md#81-general) | MduX's CI matrix (three toolchains, the evidence byte-identity checks, `mdux-docs-lint`, `mdux-evidence-lint`) is the concrete form this takes here: a fixed, versioned set of checks that run on every change, not a process reconstructed ad hoc per review. | — |
| §8.2 | [Monitoring and measurement](06-measurement-analysis-improvement.md#82-monitoring-and-measurement) | The evidence-kernel and host-tools test suites (91 cases as of the epic that introduced them) run on every pull request and on every push to main or develop, across the two toolchains CI currently exercises - MSVC and GCC 16; a Clang leg exists but is disabled - giving continuous, reproducible monitoring of whether the codebase still meets its own specified behaviour, not a periodic audit sample. | JUS-010 |
| §8.3 | [Control of nonconforming product](06-measurement-analysis-improvement.md#83-control-of-nonconforming-product) | A failed CI check is MduX's nonconforming-product control: a pull request whose evidence byte-comparison, trust-zone check, or lint fails cannot merge, which is nonconformity contained at the point of detection rather than shipped and corrected later. | — |
| §8.4 | [Analysis of data](06-measurement-analysis-improvement.md#84-analysis-of-data) | No dedicated MduX mechanism. | — |
| §8.5 | [Improvement](06-measurement-analysis-improvement.md#85-improvement) | GitHub issues and pull requests are MduX's CAPA analog, with no effectiveness verification; mdux.governance (issue #34) is where a purpose-built mechanism would land. | — |
