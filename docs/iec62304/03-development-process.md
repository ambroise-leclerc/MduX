# IEC 62304:2006 §5 — Software development process

The eight sub-clauses of §5 run in the order a development effort actually proceeds — plan, gather
requirements, design at increasing detail, implement and verify, integrate, test the whole system,
release — though IEC 62304 permits iterating rather than treating them as a strict waterfall.
Each sub-clause below states what the clause is asking for, in this project's own words, then says
what MduX currently has (or does not yet have) that addresses it.

## §5.1 Software development planning

A development plan is where a manufacturer commits, in writing, to which processes it will follow
and what its verification strategy is, before development produces something to verify. For MduX,
the closest equivalent artifacts are its Architecture Decision Records: each ADR states a decision,
the alternatives considered, and the consequences accepted, which is a real (if partial) planning
record for the areas it covers. There is no single document yet that plays the role of a complete
software development plan across the whole project — that gap is honest, not hidden.

## §5.2 Software requirements analysis

This clause asks that software requirements be documented, and that each one be traceable to
something upstream (a system requirement, a risk control measure) and something downstream
(a design element, a test). MduX does not yet have a requirements-traceability mechanism — the
`mdux.governance` module (issue #34) and its traceability matrix export (issue #35) are where this
lands, not before. The `requirement_id` field in the Justification schema
([`docs/governance/schemas/justification.schema.json`](../governance/schemas/justification.schema.json))
is the forward-looking half of this: an optional link to a requirement that, once #34/#35 exist,
will resolve to something real.

## §5.3 Software architectural design

Architectural design is where a system is decomposed into software items and the *interfaces and
dependencies between them* are fixed. MduX's clearest existing example is the governed/adapter/tools
split from [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md): the architecture states that a governed
item's link graph must never reach Vulkan or a windowing library, and `cmake/MduXTrustZones.cmake`
walks the actual build graph to enforce it — an architectural decision checked mechanically at
every configure, not asserted once in a diagram.

```json
{
  "justification_id": "JUS-003",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3 Software architectural design",
  "rationale": "The governed/adapter/tools split is MduX's architectural decomposition; MduXTrustZones.cmake verifies the resulting link-graph interfaces at every configure rather than only at design-review time.",
  "evidence_refs": ["docs/adr/ADR-004-trust-zones-in-cpp.md", "cmake/MduXTrustZones.cmake"]
}
```

## §5.4 Software detailed design

Detailed design refines an architectural item down to a level from which it can be implemented and
unit-tested directly. MduX has detailed design for the pieces it has actually built — the evidence
kernel's canonical-JSON encoding rules in [ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)
are as close to a detailed design record as this project currently has, since they specify exact
byte-level behaviour, not just an interface. Most of the product this corpus eventually needs to
cover (`.medui` compilation, the renderer, ML inference) has no detailed design yet, because it has
no implementation yet — issues #13–#18 are where both land together.

## §5.5 Software unit implementation and verification

This is the clause IEC 62304 uses to require that a software unit's implementation be verified
against its detailed design — for MduX, unit tests exercised through the in-repo
[MduXTest framework](../adr/ADR-005-error-handling-and-exceptions-policy.md) (`tests/framework/MduXTest.cppm`).
The evidence kernel is the fullest example available today: `mdux.evidence.digest`,
`mdux.evidence.json`, and `mdux.evidence.report` each carry a dedicated test executable checked
against independently-known values (published FIPS 180-4 vectors for the digest module, for
instance), not merely against the implementation's own output.

```json
{
  "justification_id": "JUS-004",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.5 Software unit implementation and verification",
  "rationale": "mdux.evidence.digest is verified against externally-known FIPS 180-4 test vectors and NIST-published values, not solely against its own output, which is what distinguishes verification from a self-consistency check.",
  "evidence_refs": ["include/mdux/evidence/Digest.cppm", "tests/evidence/DigestTests.cpp"]
}
```

## §5.6 Software integration and integration testing

Integration testing verifies that software items work together, not just individually. MduX's
`InstallTreeConsumer` test (issue #47) is a real integration test in this sense: it installs the
library to a scratch prefix and builds a separate consumer project against it via
`find_package(MduX)`, verifying the actual install-tree interface rather than just the in-tree
build. Broader integration testing — across the renderer, font pipeline, and `.medui` compiler once
they exist — is future work tracked under issues #13–#15.

## §5.7 Software system testing

System testing verifies the software as a whole against its requirements, in an environment
representative of its actual use. MduX has no software *system* yet in this sense — a system implies
an assembled product, and today's repository is foundations and an evidence kernel, not an
assembled UI. The rendered-truth verification epic (issue #16) is explicitly scoped as this
corpus's future system-testing story: rendering a compiled `.medui` screen offscreen and checking
that content appears where the compiled screen says it will.

## §5.8 Software release

A release requires the manufacturer to know precisely what is being released — every included
software item, its known anomalies, and the configuration it was built from. MduX's evidence
pipeline ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)) is the mechanism most directly
relevant here: a `report.json` records exactly which recipe, inputs, and resolved options produced
a given artifact, and CI re-derives the artifact from those to confirm the record is accurate before
anything is considered final. There is no product release yet for this to apply to; the mechanism
exists ahead of the need, which is the order issue #12 was deliberately built in.
