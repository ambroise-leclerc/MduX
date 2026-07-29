# ISO 13485:2016 §4 — Quality management system

## §4.1 General requirements

This clause asks an organization to identify the processes its QMS needs, define how they interact,
and keep them effective — including processes it outsources, which remain the organization's
responsibility to control. For a device manufacturer integrating MduX, the relevant "outsourced
process" framing is exactly right: MduX is a dependency the manufacturer does not control the
internals of, which is why its own evidence pipeline matters to *their* QMS. A manufacturer can
point at a specific `report.json` and know precisely which recipe, inputs, and options produced the
MduX artifact they integrated — see [ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md).

```json
{
  "justification_id": "JUS-007",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §4.1 General requirements",
  "rationale": "A manufacturer integrating MduX does not control MduX's internal build process directly, but ADR-007's evidence pipeline gives them a re-derivable, byte-verified record of exactly what produced any given MduX artifact - the concrete control an outsourced-process requirement asks for.",
  "evidence_refs": ["docs/adr/ADR-007-evidence-pipeline-doctrine.md", "cmake/MduXBake.cmake"]
}
```

## §4.2 Documentation requirements

This clause asks for a quality manual, documented procedures, and records — kept under control so
that the current, approved version is what people actually use, and superseded versions do not
circulate as if current. MduX's documented-information control is git itself: every file's history,
the branch-protection rule that changes to `master` go through a reviewed pull request (not a direct
push), and the ADR set's own "Status" field (Proposed / Accepted / Superseded) marking which
decisions are current. Nothing here is a *quality manual* in the standard's sense — MduX is a
dependency, not a manufacturer — but the underlying document-control discipline the clause asks for
is genuinely present and checkable.
