# IEC 62304:2006 §1–§3 — Scope, normative references, terms and definitions

## §1 Scope

IEC 62304:2006 sets out a life cycle framework for the software in a medical device, or software
that is itself a medical device: a development process, a maintenance process, and the risk,
configuration, and problem-resolution processes that run alongside them. It applies whether the
software is newly written, a modification of existing software, or software of unknown provenance
being integrated into a device — MduX's own zero-SOUP direction (issue #18) is a response to that
last case, choosing not to carry dependencies whose provenance the project cannot itself vouch for.

The standard's own scope excludes software used only to *manufacture* a medical device (a factory
test rig, say) and software tools that never ship as part of the finished device. MduX is a UI SDK
that ships inside a device's software; nothing in this repository falls under those exclusions.

**MduX's own components are Class A** throughout this documentation set — software that cannot
contribute to a hazardous situation. Its sibling project TrustSC targets Class B/C. Where a
requirement's rigor scales with safety class, later clause files say so explicitly; nothing here
should be read as an implicit Class B/C claim.

## §2 Normative references

IEC 62304:2006 is written to be read alongside ISO 14971 (risk management) and, for a manufacturer's
quality system, ISO 13485. MduX's own regulatory corpus mirrors that structure:
[`docs/iso13485/`](../iso13485/) covers the quality-management side, and
[`docs/iso14971/`](../iso14971/) covers risk management directly. IEC 62366-1 (usability engineering) and
IEC 81001-5-1 (health-software security) round out the five standards this project's citation
convention names — see [`docs/governance/citation-convention.md`](../governance/citation-convention.md)
for the full closed set of approved identifiers.

## §3 Terms and definitions
<!-- pointer: Names the terms that recur across this corpus and points at where MduX gives each a checkable meaning, rather than restating a glossary that is itself normative text. -->

Rather than restate the standard's glossary — which is exactly the kind of clause where a
"definition" is normative text, easy to reproduce nearly word-for-word without meaning to — this
section names the handful of terms that recur across this corpus and points at where MduX gives
each one a concrete, checkable meaning instead of a prose definition:

| Term | Where MduX makes it concrete |
|---|---|
| Software safety classification | Declared per-component in this documentation set (see [`02-general-requirements.md`](02-general-requirements.md)), not computed — IEC 62304 treats classification as a judgment call the manufacturer records, not an algorithm. |
| Software item / software unit | The governed/adapter/tools split in [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md) is MduX's structural answer to "what counts as one unit for verification purposes": a governed target's link graph, mechanically checked by `cmake/MduXTrustZones.cmake`. |
| Verification | Where this corpus says a clause is "verified," it means a specific, named, re-runnable check — a `ctest` case, a `mdux-docs-lint` rule, a `git filter-repo` byte-comparison — not a review that happened once and left no artifact. |
| Risk control measure | Concrete link to ISO 14971's vocabulary lands with issue #29; until then, this corpus describes a mechanism's *effect* (e.g. "cannot compile if X") rather than asserting it satisfies a specific risk-control category from that standard. |

If a future clause file in this directory needs to explain a definition more precisely than this
table does, it should describe *how MduX uses the concept*, in original prose, rather than restate
the standard's definition — the same rule [`docs/governance/citation-convention.md`](../governance/citation-convention.md)
states for normative requirements applies equally to definitions.
