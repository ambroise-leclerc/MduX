# ADR-017: Sibling conformance gate status and PAR-REQ dispositions

## Status

**Accepted**, 2026-09-08, for #312, by maintainer instruction. Acceptance ratifies the
per-capability status statement and the PAR-REQ-001/002/003 dispositions below — PAR-REQ-001
**Accepted**, PAR-REQ-002 **Accepted with amendment**, PAR-REQ-003 **Accepted with a recorded
subset limitation**. It changes no runtime behavior, no compiled artifact, no committed evidence,
no test and no shared pin, and supersedes no earlier ADR. PAR-REQ-004–010 keep their
`Proposed / unreviewed` status; the cross-implementation baseline stays with
[#335](https://github.com/ambroise-leclerc/MduX/issues/335), and ADR-016's own status is #313's to
record.

## Shared contract

No MedUI shared decision covers cross-implementation conformance — [ADR-014](ADR-014-rendered-truth-verification.md)
records that verification is not upstream-governed, and
[ADR-016](ADR-016-locally-versioned-observation-profiles.md) inherits it.
[#314](https://github.com/ambroise-leclerc/MduX/issues/314) owns the executable gate against one
pinned observation corpus, and its acceptance is explicit that a cross-implementation parity claim
cannot be discharged from one consumer's results. This record does not change that: it states what
MduX's side of the gate now demonstrates and records the ratified dispositions for the prospective
requirements it delivers, and it leaves the shared baseline to
[#335](https://github.com/ambroise-leclerc/MduX/issues/335).

## Context

[#314](https://github.com/ambroise-leclerc/MduX/issues/314) landed in four merged stages:

| Stage | PR | What it added to the gate |
|---|---|---|
| A | [#331](https://github.com/ambroise-leclerc/MduX/pull/331) | `medui-conformance.toml` re-pinned to MedUI `v0.3.0-rc.1` (`9a57f64`); the 27-case compiler corpus (5 syntax, 17 semantics, 3 layout, 2 safety) gated at `positions = "full"`; `MEDUI-E035`, `MEDUI-E054`; four compiler-defect fixes the newer corpus exposed |
| B | [#332](https://github.com/ambroise-leclerc/MduX/pull/332) | `profiles` claims `MEDUI-PROFILE-RENDERED`; a new `conformance_spec` suite runs the 33 `rendered-check` vectors (rules R01–R04) against `mdux.verify`'s own exported predicates, plus the 10 `consumer-manifest` contract cases |
| C | [#333](https://github.com/ambroise-leclerc/MduX/pull/333) | `profiles` also claims `MEDUI-PROFILE-EVIDENCE`; the 31 `aggregate-evidence` vectors (rules E01–E03) and 29 `evidence` contract documents are gated; `mdux.tools.schema` — a fail-closed JSON-Schema-subset engine — validates every contract document |
| C-emitter | [#334](https://github.com/ambroise-leclerc/MduX/pull/334) | `mdux-verify-ui --medui-evidence-out` derives MduX's own `MEDUI-PROFILE-RENDERED` E01 envelope from a real verify run — schema-valid, aggregating to `pass` on a GPU leg, derived and uncommitted |

The [pinned behavior matrix](../parity/behavior-matrix.md) was reviewed in
[PR #329](https://github.com/ambroise-leclerc/MduX/pull/329): the maintainer verified its substance
against the pinned sources and approved it, while explicitly leaving formal acceptance of the
individual PAR-REQ records pending. [ADR-015](ADR-015-versioned-sibling-observations.md)'s D1–D5
architectural direction was accepted separately on the same date; that acceptance assigned no
disposition to the individual requirements.

Three of the ten prospective requirements — PAR-REQ-001, PAR-REQ-002 and PAR-REQ-003 — are the ones
#314 was the named delivery vehicle for. Their engineering half now exists, gated in CI on all five
legs (`-L conformance`). This record fixes their dispositions, each tied to the tests that
demonstrate it, so the ratification is against running code rather than a plan. It was drafted as a
proposal for the `#314d` PR review; the dispositions were **ratified by maintainer instruction on
2026-09-08** under #312 and are recorded as Accepted below and in
[`docs/parity/requirements.md`](../parity/requirements.md). The other seven requirements belong to
interaction, streaming and authoring work (#315–#327) and keep their `Proposed / unreviewed`
status.

## Medical Device Considerations

Impact: **potentially safety-relevant planning and documentation**. This record changes no device
code, no compiled artifact, no committed evidence and no test. It describes the state of a
verification gate that already runs, and records the ratified review dispositions.

- **IEC 62304:2006 §5.7**: the software-system verification scope limit recorded in
  `docs/iec62304/03-development-process.md` is unchanged — MduX has no assembled software system,
  and nothing here is a verification, validation or release activity for one. The `conformance_spec`
  suite is a development-time conformance check against a pinned external corpus.
- **Risk management**: no hazard, risk control or software safety class in this repository names the
  failure mode that a weaker or misidentified conformance check would introduce. This record does
  not invent one. The prospective requirements' failure descriptions remain engineering concerns,
  not assigned clinical hazards.
- **Traceability**: PAR-REQ-001/002/003 gain a link from their disposition to the specific
  `conformance_spec` scenarios and ADR-016 fixtures that exercise them.
- **Cybersecurity**: unaffected.

No cross-implementation parity is claimed, and per #314's own acceptance it cannot be from MduX
results alone. This is not a certification, validation or production-readiness claim.

## Decision

### 1. Per-capability status is the roadmap results table

The authoritative record of what each consumer demonstrates on its own pin is the
[#314 per-capability conformance results](../roadmap.md#314-per-capability-conformance-results)
table in `docs/roadmap.md`: MduX at `183a6da` (MedUI `v0.3.0-rc.1` / `9a57f64`), TrustSC at
`4f114dd` (MedUI `0.1.0-candidate` / `c8cc45e`). It is not duplicated here or in the behavior
matrix. The two consumers are on different contract revisions, so the table is a per-consumer
statement, not a comparison of equivalent results. This is the historical `#314d` pairing;
the newer [#335 paired adoption results](../roadmap.md#335-paired-adoption-results) record TrustSC's
shared-pin syntax candidate without extending this record's rendered/evidence dispositions.

### 2. PAR-REQ-001 — **Accepted** (ratified 2026-09-08)

*Required observable:* a comparison names both implementation SHAs, the exact contract SHA,
phase/profile and position precision; an unknown or missing capability cannot imply support.

The gate enforces this. `medui-conformance.toml` carries the exact contract `commit`, the claimed
`capabilities`, `positions` and `profiles`; `conformance_spec` fails when the manifest claims a
profile with no adapter ("An unsupported profile claim in `medui-conformance.toml` fails the gate")
and when the manifest shape itself is malformed ("The manifest validator rejects a malformed
`medui-conformance.toml`"). Each claimed compiler phase is exercised by its complete set of pinned
positive cases through the shared harness, and `positions = "full"` is checked in both directions —
pinned columns matched exactly, and a diagnostic that started carrying an unexpected column would
fail. ADR-015 D1 (exact SHAs and contract identity) and D4 (declared precision) are satisfied.

### 3. PAR-REQ-002 — **Accepted with amendment** (ratified 2026-09-08)

*Required observable:* extent equality, containment, tint composition and raw RGBA hash have
distinct versioned identities with defined applicability, empty policy, ROI and arithmetic;
migration preserves old required checks.

Delivered in two halves. The **local** half is [ADR-016](ADR-016-locally-versioned-observation-profiles.md):
**five** `mdux.local/*` observation profiles — `extent-equality`, `tint-composition`,
`ink-containment`, `ink-coverage` and `raw-image-digest`, one per predicate. The four committed
checks record their profile per outcome in `verification.json`; `raw-image-digest`
(`rawImageDigest()`) is exercised only by an adversarial fixture set and has no committed baseline
and no production caller. The **shared-corpus** half is #314 Stage B: `MEDUI-PROFILE-RENDERED` is claimed
and the 33 `rendered-check` vectors run against `mdux.verify`'s own `rectContainedBy`, `inflate`
and `couldBeBlend` — one implementation of the arithmetic, shared by the local profiles and the
corpus — with the adapter-disagreement negative proving the gate bites.

The **amendment** records three bounded limitations, none of which weakens an existing required
check:

- `mdux.local/tint-composition` stays at version 1. The Stage B move of `couldBeBlend` into the
  module interface was a visibility change only; its body is unchanged and `verify_spec` proves the
  four required checks compute exactly what they computed before. A true outcome-changing revision
  (correcting alpha handling for mismatched-alpha callers) is deferred to a future version-2 change,
  per ADR-016 decision 4.
- **The canonical identifiers, the schemas and the corpus already exist** at the pinned revision
  `9a57f64`: `spec/profiles.md` defines `MEDUI-PROFILE-RENDERED/1` with `extent-equality/1`,
  `ink-containment/1`, `tint-composition/1` and `rgba8-sha256/1`; `schemas/` carries the
  `profile-case`, `contract-case` and `evidence` schemas; `conformance/profiles/` and
  `conformance/contracts/` carry the vectors. So the migration is **not** blocked on MEDUI-DEC-007
  delivering anything — that direction is Accepted and its candidate delivery is in the pinned
  0.3.0 line. What remains, and what ADR-016 decision 4 defers, is a distinct step with two
  identity domains:
  - **Derived shared evidence already uses the canonical ids.** The `--medui-evidence-out` envelope
    (Stage C-emitter) emits `profile.id = "MEDUI-PROFILE-RENDERED"` and `check.id` in
    {`extent-equality`, `tint-composition`, `ink-containment`}. That envelope is derived and
    uncommitted, so adopting canonical ids there carries no baseline risk and is done.
  - **Committed `verification.json` records the `mdux.local/` identity and, since #313, a candidate
    `candidateProfile` beside it.** Migrating to the canonical check ids is a change to a
    **byte-compared** artifact; per `spec/profiles.md` ("a consumer maps legacy obligations
    explicitly … runs old and candidate obligations together, retains both reports, and rebakes
    changed baselines") the [#313 amendment to ADR-016](ADR-016-locally-versioned-observation-profiles.md)
    added the candidate identity **beside** the retained local one — both identities now run together
    in the committed bundle — by maintainer instruction on 2026-09-09, ahead of the gate below.
    Still gated, and still residual: a final 0.3.0 minor release rather than `-rc.1`, the
    cross-implementation pass ([#335](https://github.com/ambroise-leclerc/MduX/issues/335)), and a
    reviewed re-bake against the final line, at which point `candidateProfile` stops being candidate.
  - **`mdux.local/ink-coverage`** (`LocalizedTextPresence`) has **no** `MEDUI-PROFILE-RENDERED`
    equivalent — the RENDERED profile has four checks and none is a localized-text-presence
    predicate — so it stays implementation-local after the migration, not mapped. This is the same
    boundary the derived envelope draws by excluding it.
- Rule R04 (`rgba8-sha256`) is exercised as arithmetic against the pinned vectors, but MduX commits
  no image baseline and R04 discharges no obligation — a driver-tuple-dependent digest cannot enter
  a byte-compared artifact (ADR-014 D4, ADR-007 D5), and TrustSC commits none either.

### 4. PAR-REQ-003 — **Accepted**, with a recorded subset limitation (ratified 2026-09-08)

*Required observable:* required observations are derived independently from the pinned
screen/profile; missing, duplicate, unknown, unsupported or not-run required rows prevent a
successful gate; keys include all scope and provenance fields.

Delivered in two halves. The **aggregate** half is #314 Stage C: the 31 `aggregate-evidence`
vectors check fieldwise identity match, one report row per derived obligation, and
`pass`/`fail`/`unsupported`/`not-run` aggregation; the negative vector proves a wrong expected
outcome fails; `mdux.tools.schema` validates every `evidence` contract document and fails closed on
any keyword it does not implement, so a constraint the contract adds cannot silently stop being
checked. Malformed identities are rejected before matching.

The **derived** half is #314 Stage C-emitter: `deriveRenderedEvidence()` synthesizes one obligation
per enumerated obligation from the screen and its checks — not read back from a report — and
**fails closed**: it refuses a run whose outcomes and obligations are not a complete one-to-one
pairing (missing, duplicate or substituted), refuses an unrecognised check name, and refuses a run
that never rendered. The envelope carries the full identity — contract, producer, profile,
artifact, ordered assets, screen, node, locale, scenario, capture, frame, check, backend and a
producer-scoped configuration token.

**Recorded limitation:** the derived envelope is the RENDERED **subset** of a run.
`LocalizedTextPresence` (`mdux.local/ink-coverage`) has no shared rendered-check id — it is an
implementation-local check — so its outcomes are excluded from the envelope and counted, not
mapped. An implementation-local check runs alongside the shared ones; it does not become one. The
cross-implementation half of PAR-REQ-003 (comparing MduX's and TrustSC's derived observations) is
out of scope until #335.

### 5. Cross-implementation parity is not claimed

At the `#314d` assessment, TrustSC's published head was `4f114dd`, pinned to MedUI `0.1.0-candidate`,
claiming `syntax` only at `line-only` positions and no profiles. A shared baseline requires TrustSC to
re-pin and claim the phases and profiles it implements, gated the same corpus-derived way. That is
[#335](https://github.com/ambroise-leclerc/MduX/issues/335), and until it closes the results table
is a per-consumer statement. MduX's coverage is not lowered to match the narrower claim.

**#335 paired adoption results, 8 September 2026:** TrustSC `b21c423` adopts MedUI `9a57f64` on its work
branch and passes all five syntax cases at line-only precision. The
[roadmap results table](../roadmap.md#335-paired-adoption-results) and
[behavior matrix](../parity/behavior-matrix.md#335-paired-adoption-results) record both heads
and the evidence. Semantics, layout, safety and RENDERED/EVIDENCE remain unclaimed by TrustSC;
joint sign-off is pending. This syntax result does not discharge the rendered cross-implementation
condition in decision 3 or establish the derived-observation comparison in decision 4.

## Alternatives Considered

- **Claim cross-implementation parity from MduX's green gate.** Rejected: #314's acceptance
  explicitly forbids it, and TrustSC is on a different contract revision. A single-side pass is not
  equivalence.
- **Wait for the TrustSC re-pin before recording any disposition.** Rejected: MduX's side of the
  three requirements is complete, gated in CI and reviewable now. Holding the dispositions hostage
  to sibling-side scheduling leaves accepted engineering work in an indefinite `unreviewed` state
  and loses the traceability link while the context is fresh.
- **Fold the dispositions into ADR-015/016 amendments.** Rejected: ADR-015 is the accepted
  architectural direction and ADR-016 is the local profile layer; the PAR-REQ dispositions are a
  distinct review artifact that maps each requirement to the specific tests that discharge it, and
  they deserve one record a reviewer can ratify or amend as a unit.
- **Assign the dispositions as Accepted in the drafting revision.** Rejected at drafting:
  dispositions are the maintainer's and domain reviewer's to assign, so the first revision proposed
  them and left ratification to review. That ratification has now happened — by maintainer
  instruction on 2026-09-08 under #312 — and is recorded here and linked back into
  `docs/parity/requirements.md`. The verifier-area dispositions (PAR-REQ-002/003) still carry the
  open domain-review note in the requirements table.

## Consequences

### Positive

- PAR-REQ-001/002/003 move from "no individual disposition" to a ratified disposition with a test
  map, each pinned to the `conformance_spec` scenarios that discharge it.
- The RENDERED-subset limitation of the derived envelope is stated once, in a place the reviewer
  and a downstream consumer will both find.
- The cross-implementation gap has a single owning issue (#335) and the results table has a defined
  completion condition.

### Negative

- A fourth parity record (ADR-015, ADR-016, the requirements table, this) is added; a reader must
  follow the chain to see the whole picture. Mitigation: each has a distinct role and this one
  links the others.
- The ratification is a maintainer instruction under #312 rather than the `#314d` PR review it was
  drafted for; the drafting-time reasoning is kept in Context and Alternatives so the trail is
  legible.

### Risks

- **The verifier-area dispositions are read as domain-reviewed.** Mitigation: PAR-REQ-002/003 keep
  the open "domain reviewer (verifier area)" note in `docs/parity/requirements.md`; ratification
  here is the maintainer's engineering acceptance, not a clinical or risk-control sign-off.
- **The per-consumer results table is read as a parity claim.** Mitigation: decision 1 and the
  table's own header state it is per-consumer; the two contract revisions are printed side by side;
  #335 is named as the completion condition.

## Implementation Notes

- No code, CMake, test or `medui-conformance.toml` change. The `conformance_spec` scenarios this
  record cites (`-L conformance`, tests for the RENDERED and EVIDENCE vector runs, the two
  adapter-disagreement negatives, the manifest and schema validators and their malformed-input
  negatives) already exist and are green on all five CI legs.
- `docs/parity/requirements.md` records the ratified dispositions in its PAR-REQ rows and its Review
  disposition table, each linking here; the decision-map row for "#314 common corpus gate" cites
  this ADR.
- `docs/parity/behavior-matrix.md` is re-assessed for `#314d` at the current pins and links the
  roadmap results table.
- `docs/adr/README.md` indexes this as ADR-017.

## References

- [ADR-014](ADR-014-rendered-truth-verification.md) — verification is not upstream-governed;
  decision 4 (no measured pixel in the artifact)
- [ADR-015](ADR-015-versioned-sibling-observations.md) — decisions D1–D5, accepted 2026-09-07
- [ADR-016](ADR-016-locally-versioned-observation-profiles.md) — the local observation-profile
  layer and the `rawImageDigest()` predicate
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md) — decision 5, environment-dependent data cannot
  live in a byte-compared artifact
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-001/002/003
- [Pinned behavior matrix](../parity/behavior-matrix.md) and the
  [roadmap results table](../roadmap.md#314-per-capability-conformance-results)
- Issues [#314](https://github.com/ambroise-leclerc/MduX/issues/314),
  [#335](https://github.com/ambroise-leclerc/MduX/issues/335); PR
  [#329](https://github.com/ambroise-leclerc/MduX/pull/329)
- `medui-conformance.toml`, `tests/conformance/`

## Approval

- **Proposal date**: 2026-09-08
- **Decision date**: 2026-09-08
- **Approved by**: Ambroise Leclerc, maintainer, by explicit instruction to ratify ADR-017 and its
  PAR-REQ-001/002/003 dispositions under #312.
- **Dispositions ratified**: PAR-REQ-001 **Accepted**; PAR-REQ-002 **Accepted with amendment**
  (three bounded limitations in decision 3, none weakening an existing required check);
  PAR-REQ-003 **Accepted** with the recorded RENDERED-subset limitation in decision 4.
- **Still open**: the verifier-area domain review for PAR-REQ-002/003 (recorded in
  `docs/parity/requirements.md`); the rendered-artifact migration in decision 3 is **partly
  delivered** — #313 added the candidate `candidateProfile` identity beside the retained local one
  in the committed bundle (ADR-016 #313 amendment), and the residual is a final 0.3.0 pin, the #335
  joint sign-off and a reviewed re-bake against the final line; PAR-REQ-004–010 stay
  `Proposed / unreviewed`. ADR-016 was recorded **Accepted (2026-09-09, #313)** for the engineering
  layer, verifier-area domain review still open.
- **Scope**: the per-capability status statement and the PAR-REQ-001/002/003 dispositions. The gate
  architecture stays in ADR-015/016; the cross-implementation baseline is #335. This is a
  maintainer engineering acceptance, not a certification, validation or production-readiness claim.
