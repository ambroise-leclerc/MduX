# ISO 13485:2016 §7 — Product realization

§7 is where a QMS meets an actual product, and it is the clause with the most genuine overlap with
what this repository does — design and development controls in particular (§7.3) apply to MduX's
own architecture in a way §5 and §6 could not.

## §7.1 Planning of product realization

Planning what needs verifying, and when, before building it. MduX's ADRs play this role for the
decisions they cover: each one states the decision, the alternatives considered, and what
verification the decision implies, ahead of the implementation that follows it.

## §7.2 Customer-related processes

No MduX-specific mechanism. Determining and reviewing a customer's requirements is a
manufacturer-facing process MduX, as a dependency rather than a product with its own customers,
does not run.

## §7.3 Design and development

This is the sub-clause with MduX's strongest real mechanisms. Design and development *planning*,
*inputs*, *outputs*, *review*, *verification*, *validation*, and *change control* are all named
separately by the standard; MduX's evidence pipeline and trust-zone architecture between them cover
several at once:

```json
{
  "justification_id": "JUS-008",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §7.3 Design and development",
  "rationale": "MduXTrustZones.cmake is a design output verified mechanically at every build (a governed target's link graph cannot reach Vulkan or a windowing library), and ADR-004 is the design record explaining why - together they give a design-and-development control that runs on every change, not only at a scheduled review.",
  "evidence_refs": ["docs/adr/ADR-004-trust-zones-in-cpp.md", "cmake/MduXTrustZones.cmake"]
}
```

```json
{
  "justification_id": "JUS-009",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §7.3 Design and development",
  "rationale": "The evidence-kernel test suite verifies each module's implementation against independently-known values (published FIPS 180-4 vectors for the SHA-256 digest, for instance) rather than only against its own output, which is what design verification requires: confirmation the output meets the input requirement, not merely that the code is internally consistent.",
  "evidence_refs": ["tests/evidence/DigestTests.cpp", "include/mdux/evidence/Digest.cppm"]
}
```

Design *validation* — confirming the finished product meets the user's actual needs, as opposed to
verifying it meets its specified design — has no MduX mechanism yet, because there is no finished
product yet to validate against real usage. Design *transfer* (into production) is the concern
[ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)'s baking discipline addresses for MduX's
generated artifacts specifically: an artifact's committed form is re-derived and byte-compared from
its recipe and inputs, so the transfer from "design" (the recipe) to "product" (the artifact) is
verified, not assumed.

## §7.4 Purchasing

No MduX-specific mechanism in the standard's sense (evaluating and controlling suppliers). MduX's
own zero-SOUP direction (issue #18) is the closest adjacent idea — not purchasing a third-party
component at all, for exactly the provenance reasons this clause exists to manage for components a
manufacturer does purchase.

## §7.5 Production and service provision

For MduX, "production" is the baking process that turns a recipe and its inputs into a committed
artifact. [ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)'s byte-identity requirement is a
direct production control: CI re-runs the same process the artifact was originally produced by and
rejects a mismatch, which is production-process verification in a stronger form than most
manufacturing processes get, since it is exact rather than statistical.

## §7.6 Control of monitoring and measuring equipment

No MduX-specific mechanism. This sub-clause concerns calibration of physical measuring equipment,
which has no analog in a software repository.
