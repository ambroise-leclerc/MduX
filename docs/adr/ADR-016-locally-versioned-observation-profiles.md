# ADR-016: Locally versioned observation profiles for rendered checks

## Status
Proposed (2026-09-07)

## Shared contract

No MedUI shared decision covers verification — [ADR-014](ADR-014-rendered-truth-verification.md)
says so, and this record inherits that. One thing here is *directional* upstream and delivered
locally: [MEDUI-DEC-007](https://github.com/Compliatory/MedUI/blob/80961fd7274992d75c8f86016be04b177c581857/decisions/MEDUI-DEC-007-rendered-check-profiles.md)
accepts that rendered-check observations should be separately named and versioned, but delivers no
canonical identifiers, no schema and no conformance corpus — its own text says "profile identifiers,
schemas and the candidate conformance corpus remain delivery work; no consumer support is asserted".
So every profile id introduced here begins with `mdux.local/`, no `medui-conformance.toml` key or
pin claims a shared profile, and a later reviewed migration maps these onto the canonical set when
one exists. The cross-implementation gate is [#314](https://github.com/ambroise-leclerc/MduX/issues/314),
not this record.

## Context

The [pinned behavior matrix](../parity/behavior-matrix.md) found that `ColorHash` names two
different observations: in MduX it is a **tint-composition** predicate — every painted pixel is an
alpha blend of the node's ground and its resolved tint at one coverage, and at least one pixel is
exactly the tint — while in the Rust sibling TrustSC it is a **SHA-256 over the golden rectangle's
tightly packed RGBA8**, compared to a committed baseline. A shared spelling is not evidence of a
shared measurement.

[ADR-015](ADR-015-versioned-sibling-observations.md) decision 2 (Accepted 2026-09-07) requires MduX
to give each rendered-check predicate a **separately versioned observation-profile identity**, and
to introduce the RGBA8 SHA-256 predicate as its own profile, so the two implementations can be
compared honestly in #314 without silently reinterpreting existing MduX evidence. It also requires
that "adding a common profile must preserve existing required checks unless a reviewed migration
explicitly replaces them", and states that "ADR-014 is not superseded by this decision".

Three constraints shape the answer:

- **`CvCheck` is a closed, contract-owned set.** `Bounds` and `ColorHash` are the shared language's
  `safety` capability, pinned in `medui-conformance.toml`, and `MEDUI-E071` rejects a name outside
  it. Widening it is an upstream change and a re-pin — never an edit to the verifier.
- **ADR-014 decision 4 keeps measured pixel values out of the committed bundle.** Its alternative 6
  rejects recording measured samples in `verification.json`, "because it makes a byte-compared file
  depend on the driver that produced the frame". A committed RGBA8-SHA-256 *baseline* is exactly
  such a value.
- **TrustSC commits no hash baselines either.** Its verification guide records the absent committed
  hash baselines explicitly; the ability to compute a hash is not a committed image-regression gate.

## Medical Device Considerations

Impact: **potentially safety-relevant**. The change touches the governed `mdux.verify` zone and a
committed evidence artifact. It changes **no check's semantics and no runtime behaviour** — the four
existing predicates compute exactly what they computed before; only an identity field is added to
each outcome, and a fifth predicate is added with no production caller.

No hazard, risk control or software safety class in this repository names the failure mode a
weaker or misidentified verification check would introduce, and this ADR does not invent one. The
change is flagged for maintainer/domain review per the `mdux-regulated-change` skill. The
IEC 62304:2006 §5.7 scope limit recorded in `docs/iec62304/03-development-process.md` is unchanged:
MduX has no assembled software system, and none of this is a certification, validation or
production-readiness claim. No cross-implementation parity is claimed — #314 owns that, and it
cannot be discharged from MduX results alone.

## Decision

### 1. Each rendered-check predicate carries an implementation-local observation profile

`mdux.verify` gains an immutable `ObservationProfile` value type — an `id` naming what is observed
and a `version` that moves only on a reviewed change — and one `inline constexpr` constant per
predicate:

| Predicate | Profile id | Version | What it observes |
|---|---|---|---|
| `goldenBounds()` | `mdux.local/extent-equality` | 1 | the golden node's content occupies exactly its declared rectangle |
| `colorHash()` | `mdux.local/tint-composition` | 1 | painted pixels are a blend of ground and tint at one coverage, and one is the tint |
| `inkContainment()` | `mdux.local/ink-containment` | 1 | a text run's placed ink stays inside its node |
| `localizedTextPresence()` | `mdux.local/ink-coverage` | 1 | the approved locale's run is the shape on screen, per baked coverage |
| `rawImageDigest()` | `mdux.local/raw-image-digest` | 1 | SHA-256 over a rectangle's tightly packed row-major RGBA8 |

`profileOf(CvCheck)` and `profileOf(TextCheck)` are the mapping; every `CheckOutcome` carries its
profile, set in the one `opened()` helper so no check can report an outcome without one. This is a
pure **identity/reporting layer**: the four existing predicates are byte-for-byte the same
computation, ADR-015 decision 2's "preserve existing required checks" is satisfied by construction,
and version `1` is the current (`db198c6`-era) arithmetic of each check, including #261's
two-composite allowance for `colorHash()` and the ground-composite band for the text checks. Earlier
revisions of these checks are not retro-versioned.

### 2. The RGBA8 SHA-256 predicate is implemented and versioned, but is not a committed check

`mdux.verify::rawImageDigest()` performs TrustSC's `ColorHash` observation — SHA-256 over a region
of interest's pixels, row by row, four bytes per pixel, no stride padding. It is **not** a `CvCheck`,
**not** a `.medui` trigger, and **not** wired into the driver's obligation enumeration or the
committed `verification.json`. That is deliberate, and each half of the reason is load-bearing:

- **It cannot be a `CvCheck`.** That set is closed and contract-owned; a new enumerator is an
  upstream change and a re-pin, and ADR-014 decision 3 depends on the set being exactly `Bounds`
  and `ColorHash`.
- **It must not have a committed baseline.** A committed RGBA8-SHA-256 is a property of the driver
  tuple that produced the frame — lavapipe on Linux, MoltenVK on macOS, Mesa's lavapipe on Windows
  — not of the screen's declared inputs. ADR-014 decision 4 and its alternative 6 reject exactly
  this: it would fail an evidence leg on any rendering change while every check still held, and
  ADR-007 decision 5 makes the same structural argument about commit SHAs. TrustSC commits none
  either, so there is no sibling baseline to be compatible with.

So the predicate is **delivered** — implemented, versioned, and exercised by an adversarial fixture
set (`tests/verify/RawImageDigestTests.cpp`: match, single-channel mismatch, absent baseline,
region-outside-frame, stride independence, ROI scoping, profile identity, degenerate ROI) — while
all four required checks are **preserved** unchanged. That is what ADR-015 decision 2 asks for. A
future consumer that has declared a presentation/backend profile (PAR-REQ-009) can supply a baseline
through `RawImageExpectation::createWithBaseline()`; there is no such consumer today.

### 3. `NoBaseline` is distinct from a pass

`rawImageDigest()` reports `Finding::NoBaseline` for every call with no baseline. `held()` is false
for it, so it discharges nothing — the same behaviour as TrustSC's own `NoBaseline`, and consistent
with ADR-015 decision 3's "only pass discharges a required obligation". `DigestMismatch` is the
finding for a supplied baseline that does not match; `RegionOutsideFrame` for a region the frame
does not contain — a failure, not a skip.

### 4. Profile ids and versions are implementation-local and immutable

The `mdux.local/` prefix is the marker: a reader seeing it knows the id is MduX's own, not a
shared-contract identifier. A profile's `(id, version)` pair never changes meaning — an
outcome-changing revision takes a new version number, per ADR-015 decision 4's "outcome-changing
changes require a minor revision". When MEDUI-DEC-007 delivers canonical ids and a schema, a
reviewed migration maps `mdux.local/*` onto them, keeping the old and candidate identities during
the transition and preserving existing evidence.

### 5. `verification.json` records the profile per outcome; nothing else changes

Each outcome object gains one nested member:

```json
{
  "check": "Bounds",
  "finding": "Held",
  "nodeId": "emergency-halt",
  "observationProfile": { "id": "mdux.local/extent-equality", "version": 1 },
  "scope": "en-US"
}
```

`schemaVersion` stays `1`: `evidence::kSchemaVersion` is shared across every evidence artifact
(font, text, shader, image, ML, report), and an additive, outcome-preserving field in one artifact
is not a reason to move it. The file is byte-compared by `evidence.screen.<id>` on four toolchain
legs, not JSON-schema-validated, and `docs/recipes/screen.schema.json` describes `report.json`'s
resolved `options`, not `verification.json` — it is unaffected. The regenerated bundle is committed
through `mdux-bake-update` like any other artifact change.

`writeVerification()` validates the profile before it serialises one, the same way it already checks
that each outcome pairs with its obligation: an outcome whose profile is missing
(`{"", 0}`) or is not the one its check reports under is refused with
`ArtifactError::ObservationProfileInvalid`, not written. `mdux::verify::profileForCheckName()` is the
single resolver both the driver and the writer use, so the check cannot drift from the value the
driver set. This is ADR-014 decision 2's "derive, don't trust" applied to the identity: the writer
is the boundary that turns a `RunResult` into committed evidence, and an unidentified or
misidentified observation must not cross it.

## Alternatives Considered

- **Add `CvCheck::RawImageDigest`.** Rejected: the set is closed and contract-owned, and ADR-014
  decision 3 requires it to be exactly `Bounds` and `ColorHash`. A new member is an upstream change.
- **Commit an RGBA8 baseline for `endoscope-monitor`.** Rejected: ADR-014 decision 4 and
  alternative 6, and ADR-007 decision 5 — a measured, driver-tuple-dependent value in a
  byte-compared artifact fails an evidence leg on any rendering change while every check still
  holds. TrustSC commits none either.
- **One envelope-level profile id for the whole run.** Rejected: `Bounds` and `ColorHash` are
  different observations, and a text presence check is a third; a single id would say less than the
  per-outcome one and could not distinguish them.
- **Wait for MEDUI-DEC-007's canonical ids before doing anything.** Rejected: ADR-015 permits a
  labelled implementation-local extension while the upstream discussion is open, and #313's own
  decision gate (D2/D3/D4, accepted) is met. The migration to canonical ids is mechanical.
- **A `.medui` trigger for the digest.** Rejected: it is not an authoring concern, and it would
  widen the grammar to express a check with no committed consumer.

## Consequences

### Positive

- `ColorHash`'s divergence from TrustSC is named and versioned, so #314 compares like with like
  rather than conflating a tint predicate with a pixel hash.
- Existing evidence is preserved exactly — no check semantics change, and the regenerated
  `verification.json` differs only by the additive `observationProfile` member.
- The RGBA8-digest observation exists for a future declared-profile consumer (PAR-REQ-009) and for
  #321's dynamic evidence work, without becoming a gate.

### Negative

- `verification.json` grows by one nested object per outcome.
- A fifth predicate with no committed consumer is carried in the governed module.
- `mdux.local/*` ids need a future reviewed migration when canonical ids arrive.

### Risks

- **`mdux.local/*` mistaken for a shared-contract identifier.** Mitigation: the prefix, this ADR,
  the behavior-matrix note, and the PR gate note that no manifest or pin claims a shared profile.
- **The digest predicate's existence read as a committed image-regression gate.** Mitigation:
  decision 2, and `rawImageDigest()`'s own documentation — `NoBaseline` for every production call,
  and no committed baseline by design.

## Implementation Notes

- `mdux.verify` gains `import mdux.evidence.digest;` — both already in `MduXCore`, so no CMake
  change. `Sha256` is `noexcept`, fixed-buffer and allocation-free; `mdux-governed-lint` and
  `governed.noThrow.symbolScan` cover the new code with no second registration.
- New appended `Finding` enumerators `NoBaseline`, `DigestMismatch` and `VerifyError`
  `DigestRoiDegenerate`; `spell(Finding)` gets two cases (the committed serialisation), `describe()`
  is free to reword.
- `tools/verify/Driver.cppm`'s `Outcome` and `Artifact.cpp`'s `outcomeToJson()` carry the profile
  through; the obligation loop is unchanged because `rawImageDigest()` is not an obligation.
  `writeVerification()` gains one check beside its existing pairing check —
  `ArtifactError::ObservationProfileInvalid` for a missing or mismatched profile — and
  `mdux.verify` gains `parseTextCheck()` and `profileForCheckName()` so the writer resolves a
  check's expected profile through the same code the driver used to set it.
- PAR-REQ-002/003/009 dispositions remain pending; this record delivers the local half of
  PAR-REQ-002 (four named profiles + the digest predicate + adversarial fixtures) and does not
  discharge the shared-schema/corpus half.

### Amendment — #314 Stage B: the exported rendered-check leaves

The shared-corpus half of PAR-REQ-002 landed in #314 Stage B. MduX's manifest now claims
`MEDUI-PROFILE-RENDERED` and `conformance_spec` runs MedUI's 33 `rendered-check` vectors (rules
R01-R04) from the pinned checkout. To keep the local `mdux.local/*` profiles and the shared corpus
running **one** implementation of the arithmetic rather than two that agree until they matter
(ADR-008 decision 1, applied to the verifier), three pure predicates moved from the anonymous
namespace of `src/verify/Verify.cpp` into the `mdux.verify` module interface:

- `rectContainedBy(NodeRect inner, NodeRect outer)` — was `inside()`; `goldenBounds()` and the text
  checks already used it, and rule R02 is the same predicate over an inflated golden.
- `inflate(NodeRect, std::int32_t margin)` — new, for R02's nonnegative margin; saturating narrow
  from 64-bit intermediates.
- `couldBeBlend(ColorRgba8 pixel, ground, tint, std::int64_t allowance)` — `colorHash()`'s
  interval-intersection test; rule R03 is this predicate per sample with `allowance` set to the
  vector's composite count. Its body is unchanged. Its docstring now states the precondition both
  callers already meet — `ground` and `tint` share their alpha (opaque in every production and
  corpus case) — under which alpha drops out of the intersection. `couldBeBlend` treats alpha as an
  interpolated channel, which `blend()` does not; correcting that for mismatched-alpha callers would
  change what `colorHash()` can report and so is deferred to a change that moves
  `mdux.local/tint-composition` to version 2.

This is a visibility change only — the four required checks compute exactly what they computed
before, and `verify_spec` proves it. The `mdux.local/*` ids and versions are unchanged; the
`profiles` key in `medui-conformance.toml` names the canonical `MEDUI-PROFILE-RENDERED`, and the
migration mapping the two remains future work.

### Amendment — #314 Stage C: the `MEDUI-PROFILE-EVIDENCE` gate

`medui-conformance.toml` now also claims `MEDUI-PROFILE-EVIDENCE`. `conformance_spec` gates it as
pure list logic — the 31 `aggregate-evidence` vectors (rules E01–E03: identity match, one row per
obligation, outcome aggregation) and the 29 `conformance/contracts` `evidence`-schema documents.
No governed code and no runtime behaviour change: this is the aggregate-and-schema half of
MEDUI-PROFILE-EVIDENCE, not yet a *derived envelope* — MduX emitting its own E01 envelope from a
real `RunResult` is a separate, GPU-gated change (Stage C-emitter).

The evidence and consumer-manifest documents are validated by a new
[`mdux.tools.schema`](../../tools/common/Schema.cppm) — a JSON-Schema-subset engine in
`MduX::ToolsCommon`, ported from `Compliatory/MedUI`'s `tools/schema_check.py`, that **fails closed**
on any keyword it does not implement so a constraint the contract adds cannot silently stop being
checked. It replaces the hand-coded consumer-manifest reader, so there is now one validator for the
`medui-conformance.toml` shape and every `conformance/contracts` document.

### Amendment — #314 Stage C-emitter: the derived RENDERED evidence envelope

`mdux-verify-ui --medui-evidence-out=<dir>` writes `<screen>.medui-evidence.json`, MduX's own
`MEDUI-PROFILE-RENDERED` E01 envelope (`schemas/evidence.schema.json`) derived from a real verify
run. It is **the RENDERED subset**: `Bounds`, `ColorHash` and `InkContainment` map to the shared
rendered-check ids `extent-equality`, `tint-composition` and `ink-containment`;
`LocalizedTextPresence` (`mdux.local/ink-coverage`) has no shared id and its outcomes are excluded
and counted (an implementation-local check runs alongside the shared ones, it does not become one).

The envelope is **derived and uncommitted** — ADR-014 decision 4 and ADR-007 decision 5: it carries
host-dependent values (the Vulkan `deviceName` as `backend`, the current commit as
`producer.source`) that must never enter a byte-compared artifact. It is written to the build tree,
never committed, never byte-compared; `verification.json` keeps its own byte-compared local shape.

`producer.source` is `MDUX_BUILD_DIAGNOSTIC_SHA`, a configure-time `git rev-parse HEAD`
(`cmake/MduXBuildInfo.cmake`), which is only ever legal in output of exactly this kind — the file's
comment records why a commit SHA cannot enter `BakeReport` and why it is fine here. The
producer-scoped `configuration` token is a SHA-256 over `{backend, format, producerVersion,
surface}` — `spec/profiles.md` leaves the payload to the named producer, requiring only that it
bind the settings that determine what is rendered.

## References

- [ADR-014](ADR-014-rendered-truth-verification.md) — decision 4 (no measured pixel in the
  artifact), decision 3 (the closed `CvCheck` set), alternative 6
- [ADR-015](ADR-015-versioned-sibling-observations.md) — decisions D2, D3, D4
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md) — decision 5, why environment-dependent data
  cannot live in a byte-compared artifact
- [Pinned behavior matrix](../parity/behavior-matrix.md) — the `ColorHash` divergence
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-002/003/009
- [MEDUI-DEC-007](https://github.com/Compliatory/MedUI/blob/80961fd7274992d75c8f86016be04b177c581857/decisions/MEDUI-DEC-007-rendered-check-profiles.md)
- Issues [#313](https://github.com/ambroise-leclerc/MduX/issues/313),
  [#314](https://github.com/ambroise-leclerc/MduX/issues/314)
- `include/mdux/verify/Verify.cppm`, `src/verify/Verify.cpp`, `tests/verify/RawImageDigestTests.cpp`

## Approval

- **Proposal date:** 2026-09-07
- **Decision date:** pending
- **Approved by:** pending — maintainer/domain review requested (safety-relevant verifier area)
- **Scope:** the observation-profile identity layer, the `rawImageDigest()` predicate and its
  uncommitted status, and the `verification.json` per-outcome field. Individual PAR-REQ-002/003/009
  dispositions and the #314 shared gate are separate.
