# Regulatory citation convention

This document exists because MduX used to track copyrighted normative text directly — see
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) for what was removed and
why. This is the replacement: how to cite a standard's clause without reproducing its wording.

## Status

Landed in two parts. The first (issue #22) shipped alongside the reproduced-text removal so the
tree was never in a state where the material was gone and nothing described what replaces it. This
version (issue #8, S1) adds the formal `Justification` schema and worked examples, completing it.

`docs/iec62304/` and `docs/iso13485/` do not follow this convention yet — see "Known gap" below.
Issues #27/#28 are the clause-accurate rewrite that fixes this; this document defines the target
shape those rewrites write against.

## The rule

**Never reproduce or closely paraphrase a standard's normative text.** IEC and ISO standards are
copyrighted. Explain a requirement's *intent* in original prose, against the standard's real clause
number and title, then point at a concrete MduX mechanism — a type, an ADR, a CI step, a file path
— that addresses it. A citation with no mechanism behind it is decorative, not load-bearing, and
should be rewritten or removed.

## Citation-key format

```
<Standard> §<clause> <Short clause title>
```

Example: `IEC 62304:2006 §5.2 Software development planning`.

Valid standard identifiers (closed set):

- `IEC 62304:2006`
- `ISO 13485:2016`
- `ISO 14971:2019`
- `IEC 62366-1:2015`
- `IEC 81001-5-1:2021`

Use the exact string wherever a clause is cited — as a module heading, an `AI-Reference.md` row, or
a `clause_ref` field — so it can eventually be checked mechanically (issue #7, S6).

MduX keeps IEC 62304 Class A in scope (its sibling project TrustSC models Class B/C only) — state
this explicitly wherever safety classification is discussed, rather than assuming one or the other.

## The `Justification` object

Use this shape whenever a design decision needs a formal link to a clause. The schema is
[`docs/governance/schemas/justification.schema.json`](schemas/justification.schema.json) —
Draft 2020-12, `additionalProperties: false`, and it cross-checks that `clause_ref` actually starts
with the same standard named in `standard` (a plain regex can't do that alone; the schema uses a
per-standard `if`/`then` pair for it).

`justification_id` (`JUS-NNN`) must be unique across the whole corpus, not per standard.
`evidence_refs[]` must be non-empty and contain real repository paths — the schema rejects an empty
array, because a Justification with no evidence is exactly the decorative citation this convention
exists to prevent.

Two worked examples, both validated against the schema and pointing at mechanisms that exist in
this repository today:

```json
{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3 Software architectural design",
  "rationale": "MduXTrustZones.cmake mechanically walks a governed target's link graph and fails the configure step if it reaches Vulkan or a windowing library, which is how the trust-zone architecture (ADR-004) keeps risk-relevant segregation a build-time guarantee rather than a code-review convention.",
  "evidence_refs": ["cmake/MduXTrustZones.cmake", "docs/adr/ADR-004-trust-zones-in-cpp.md"]
}
```

```json
{
  "justification_id": "JUS-002",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §8 Software configuration management process",
  "rationale": "Every baked artifact is identified by a report naming its recipe digest, input digests and resolved options, and CI re-derives the artifact from those inputs and asserts byte-identity - so an artifact's configuration is verified by re-derivation, not by inspecting a binary diff.",
  "evidence_refs": [
    "docs/adr/ADR-007-evidence-pipeline-doctrine.md",
    "include/mdux/evidence/Report.cppm",
    "cmake/MduXBake.cmake"
  ]
}
```

Precise sub-clause numbering (the digits after the first dot, e.g. exactly which `§5.x` a
requirement falls under) should be checked against the actual standard by whoever writes a new
Justification. The clause-accurate corpus landing with issues #27-#31 is the place that numbering
gets fixed once, centrally, rather than re-verified ad hoc at every citation site.

## Known gap this document does not yet close

Removing the two files that self-described in the phrasing ADR-006 quotes in full (issue #22) does
not mean the rest of `docs/iec62304/` and `docs/iso13485/` are already clean. Spot-checking
`docs/iec62304/01-scope-and-classification.md` during that removal found sentences reading as close
paraphrase of the standard's own §1.1 wording (e.g. *"This document
specifies life cycle processes for medical device software..."*). **The full clause-accurate
rewrite of both directories — which is needed regardless, since today's modules use a flat
"sections 1-16" numbering that doesn't match either standard's real clause structure — is tracked as
issue #8 and is where this gets fixed properly**, rather than patched piecemeal here. Treat
`docs/iec62304/` and `docs/iso13485/` as not yet compliant with this convention until issue #8
lands.
