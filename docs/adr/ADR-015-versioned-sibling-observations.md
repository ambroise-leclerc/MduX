# ADR-015: Versioned sibling observations

## Status

**Proposed**, 2026-09-07, for #312. Maintainer review is pending. This changes no runtime behavior,
shared pin or accepted ADR. The upstream changes are proposals
[MedUI #15](https://github.com/Compliatory/MedUI/issues/15) and
[#16](https://github.com/Compliatory/MedUI/issues/16).

## Context

The [pinned behavior matrix](../parity/behavior-matrix.md) finds different parser declarations,
dynamic bindings, hit behavior and rendered predicates. In particular, `ColorHash` is a tint
predicate in MduX and a pixel digest in TrustSC. A shared name is insufficient evidence of the
same observation. The initial shared language contract defines compilation and golden selection;
it does not yet close the interaction or rendered-profile boundary.

## Medical Device Considerations

Impact is potentially safety-relevant design: a future weaker check or different activated
control could invalidate existing engineering evidence. [Prospective requirements](../parity/requirements.md)
record those concerns before implementation. No clinical requirement, device classification,
hazard severity or regulatory approval is inferred here. Existing caller ownership, trust zones,
bounded runtime storage and baked-evidence controls remain in force. Maintainer/domain review
must precede safety-relevant implementation; no new security or platform permission is granted.

## Decision

The following decisions are proposed for review, not claims of upstream agreement.

**D1 — Normalize declared observations.** Name the implementation SHA, exact MedUI SHA and claimed
phase/precision before comparing outputs. Stable compiler observables are acceptance/rejection
at the requested phase; code/severity and declared source position; ordered node IDs/kinds and
resolved rectangles; logical payload values, ordered state lists and source/requirement links;
and selected/deduplicated golden obligations. Paths are fixture-relative; messages, native
C++/Rust encodings and implementation-only envelopes are not equal-by-construction. Compare
resolved logical text/theme inputs and retain their provenance. Unknown or unclaimed behavior
is unsupported, not passing. Resolve existing shared diagnostic ambiguities through their issues.

**D2 — Name the predicate, not its legacy alias.** Introduce separately versioned observation
profiles for extent equality, ink containment, tint composition and RGBA8 SHA-256. Each defines
applicability, empty-content policy, ROI, coordinate/colour space, background resolution, sampling
and integer arithmetic/rounding. Current MduX one-step-per-composite and TrustSC threshold/margin
rules are documented observations, not a newly agreed common tolerance. Exact hashes allow zero
byte difference within a declared render profile. There is no global fuzzy tolerance. Adding a
common profile must preserve existing required checks unless a reviewed migration explicitly
replaces them with justified evidence. ADR-014 is not superseded by this proposal.

**D3 — Scope and identify every result.** The proposed normalized envelope contains contract SHA,
producer name/version/source SHA, observation profile ID/version, screen artifact digest, ordered
asset identities/digests, screen ID, and render configuration/backend identity where relevant.
Within that envelope the result key is `(node ID, scope, scenario digest or static, capture ID,
frame index, check ID, check version)`. Scope is a tagged locale-free value or an explicit locale,
never an ambiguous empty string. Static captures still name a capture and frame. Record surface
extent, pixel format/colour space, injected clock/data snapshot and baseline digest when used.
For byte-reproducible normalized reports, define canonical field ordering/encoding in the future
shared schema; this ADR does not invent a second canonical JSON implementation.

Outcomes distinguish pass, fail, unsupported/not-run and missing baseline. Only pass discharges a
required obligation. Derive the expected set independently from the pinned bundle/profile;
missing, duplicate, unknown or substituted required rows fail the report gate. Producer identity
is retained for audit but excluded when pairing sibling observations; pair only compatible
profiles and equal logical inputs/scopes. Different native artifact digests are allowed, with
each proven to resolve to those equal logical inputs. Never compare a bare `node::check` key
across locales, scenarios, frames or producers.

**D4 — Declare and migrate capabilities explicitly.** Today's manifests declare compiler phases
and position precision only. Their schemas reject unknown keys; provisional profile names must
not be added. MedUI #15/#16 must establish the profile schema and corpus before a consumer claims
support. Profiles/check versions are immutable. Follow MedUI's pre-1.0 policy: outcome-changing
changes require a minor revision, not a patch. A version label is informational beside the exact
SHA. Before a pin update, every consumer claiming an affected phase/profile runs its applicable
candidate corpus. Keep old and candidate observations during transition, preserve old evidence,
and rebake/commit changed artifacts through the existing pipeline. An unsupported profile never
falls back silently to a different predicate. Rollback restores a compatible pin/artifact/report
set, not only a version string. #314 will implement the shared gate; this ADR does not claim it
already exists.

**D5 — Separate interaction, data and appearance profiles.** MedUI #16 requests versioned logical
events/update order, source-to-node binding and presentation observations. The proposed local
requirements choose bounded storage, explicit cancellation/overflow, half-open reverse-paint
occlusion, traced action outputs, scalar-safe bounded editing and injected time for replay.
#315 must finalize event vocabulary, coordinate rounding and editing policy; #322 must finalize
viewport numeric/composition rules; #325 must finalize source-round-trip rules. Font/field/control
appearance and pressed/focused states need declared presentation profiles before exact-pixel
comparisons. Platform ownership, windowing and host action execution remain implementation/host
decisions. No interaction profile assigns a device's safety class or imports Rust's memory-safety
claims into C++.

## Alternatives Considered

- Copy TrustSC behavior under current names: rejected because it would silently change MduX's
  bounds, colour, occlusion and ownership guarantees.
- Declare parity from matching grammar/types or local green suites: rejected because those do
  not compare scope, failure semantics or rendered results.
- Require byte-identical native packages or all-platform pixels: rejected because language
  encodings and undeclared render configurations differ. Normalize logical observables and
  restrict exact pixels to an agreed profile.
- Resolve all future input and viewport details in #312: deferred to their design children;
  this record supplies prospective requirements and explicit decision gates first.

## Consequences

### Positive

- Differences are reproducible and separately named, preserving current evidence.
- Downstream work has [requirement IDs and decision gates](../parity/requirements.md), with
  upstream proposals linked rather than implied agreement.

### Negative

- Migration temporarily carries multiple profiles and evidence sets.
- Upstream agreement and new corpus cases can delay shared capability claims even when a local
  implementation is ready.

### Risks

- A provisional ID could be mistaken for an accepted wire contract: keep proposals out of
  manifests, artifacts and production reports until schema/corpus acceptance.
- A partial required-check set could appear green: derive obligations independently and test
  omission/substitution failures in #314/#321.
- Examples could be mistaken for clinical design inputs: require separate host/domain review
  and keep the prospective records explicitly unapproved until that review occurs.

## Implementation Notes

This change publishes the matrix, a pinned executable sibling probe and prospective records;
it does not implement #313–#327. The matrix distinguishes executed checks from source inspection.
Existing ADR-010/011/012/014 and runtime checks remain authoritative until an accepted amendment.

## References

- [Phase 2 roadmap](../roadmap.md)
- [Compatibility matrix and evidence](../parity/behavior-matrix.md)
- [Requirements, review disposition and downstream mapping](../parity/requirements.md)
- [Pinned MedUI decisions](https://github.com/Compliatory/MedUI/tree/265df1925a672bd556f69123e287215b45cfd210/decisions)

## Approval

- **Proposal date:** 2026-09-07
- **Approved by:** Not yet recorded; MduX maintainer review requested through the implementation PR.
- **Shared contract approval:** Not yet recorded; MedUI #15/#16 remain proposals.
