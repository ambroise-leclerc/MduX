# Regulatory citation convention

This document exists because MduX used to track copyrighted normative text directly — see
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md) for what was removed and
why. This is the replacement: how to cite a standard's clause without reproducing its wording.

## Status

This is a minimal first version, landed alongside the removal it explains (issue #22) so the tree
is never in a state where the reproduced material is gone and nothing describes what replaces it.
The full convention — including the `Justification` object's JSON Schema and worked examples — is
tracked as issue #8, S1, and will extend this document rather than replace it.

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

Use the exact string verbatim wherever a clause is cited — as a module heading, an `AI-Reference.md`
row, or a `clause_ref` field — so it can eventually be checked mechanically (issue #7, S6).

MduX keeps IEC 62304 Class A in scope (its sibling project TrustSC models Class B/C only) — state
this explicitly wherever safety classification is discussed, rather than assuming one or the other.

## The `Justification` object (preview)

Use this shape whenever a design decision needs a formal link to a clause:

```json
{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control",
  "rationale": "One sentence explaining why the cited mechanism satisfies the clause's intent.",
  "requirement_id": "REQ-EXAMPLE-001",
  "evidence_refs": ["path/to/real/file.cppm"]
}
```

`justification_id` (`JUS-NNN`) must be unique across the whole corpus, not per standard.
`evidence_refs[]` must contain real repository paths. The formal JSON Schema for this object lands
with issue #8, S1 — this preview exists so early citations already use the right shape.

## Known gap this document does not yet close

Removing the two files that self-described as *"a comprehensive markdown version of"* IEC 62304 and
ISO 13485 (issue #22) does not mean the rest of `docs/iec62304/` and `docs/iso13485/` are already
clean. Spot-checking `docs/iec62304/01-scope-and-classification.md` during that removal found
sentences reading as close paraphrase of the standard's own §1.1 wording (e.g. *"This document
specifies life cycle processes for medical device software..."*). **The full clause-accurate
rewrite of both directories — which is needed regardless, since today's modules use a flat
"sections 1-16" numbering that doesn't match either standard's real clause structure — is tracked as
issue #8 and is where this gets fixed properly**, rather than patched piecemeal here. Treat
`docs/iec62304/` and `docs/iso13485/` as not yet compliant with this convention until issue #8
lands.
