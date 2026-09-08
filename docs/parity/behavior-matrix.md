# Pinned sibling behavior matrix

**Latest: [#335 paired adoption results](#335-paired-adoption-results), 8 September 2026.**
The `#314d` assessment below is preserved at its original immutable inputs; the adoption section
records the newer pair and the limited observations they now share.

Assessed **7 September 2026** for [#312](https://github.com/ambroise-leclerc/MduX/issues/312);
**re-assessed 8 September 2026 for `#314d`** against MduX
[`183a6da`](https://github.com/ambroise-leclerc/MduX/commit/183a6da3190e673c41edbe9050586d4dc0fa4fa4)
(after #314 Stages A–C-emitter merged) and TrustSC
[`4f114dd`](https://github.com/ambroise-leclerc/TrustSC/commit/4f114dd30c64f61d11edb5941e95f422e189f305)
(unchanged since the first assessment). This is an implementation comparison and a proposed
compatibility boundary, not a declaration that the siblings conform to the same rendered or
interactive contract. Prospective requirements, review gates and migration rules are in
[ADR-015](../adr/ADR-015-versioned-sibling-observations.md),
[ADR-016](../adr/ADR-016-locally-versioned-observation-profiles.md),
[ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md) and the
[design records](requirements.md). No runtime behavior changes here; the MduX conformance pin
moved to MedUI `v0.3.0-rc.1` in #314 Stage A. The
[#314 per-capability conformance results](../roadmap.md#314-per-capability-conformance-results)
table in the roadmap is the authoritative per-consumer statement; this matrix is the behavioral
narrative behind it.

## Immutable comparison inputs

| Input | MduX | TrustSC |
|---|---|---|
| Implementation SHA | `183a6da3190e673c41edbe9050586d4dc0fa4fa4` | `4f114dd30c64f61d11edb5941e95f422e189f305` |
| MedUI SHA | `9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb` (`v0.3.0-rc.1`) | `c8cc45ecec2f2dfd84940b9efc17c613e691cc0d` |
| Manifest version label | `0.3.0-rc.1` | `0.1.0-candidate` |
| Claimed phases | syntax, semantics, layout, safety | syntax |
| Claimed observation profiles | `MEDUI-PROFILE-RENDERED`, `MEDUI-PROFILE-EVIDENCE` | none |
| Diagnostic positions | full | line-only |

Sources: [MduX manifest][m-manifest], [TrustSC manifest][t-manifest]. The exact SHA is authoritative;
the label alone does not identify a contract. The two consumers pin **different** contract
revisions, so a matching row is a per-consumer statement, not a shared result. TrustSC's unclaimed
phases and profiles still have local implementations; this assessment does not turn them into
conformance claims. The TrustSC re-pin is [#335](https://github.com/ambroise-leclerc/MduX/issues/335).

## Compiler and artifact observations

| Surface | MduX at its pin | TrustSC at its pin | Compatibility consequence |
|---|---|---|---|
| Parse/phase boundary | Lexer/parser, semantic resolution, bounded layout and safety selection; host checker exposes the four phases. | Line-oriented parser and local compiler; shared checker claims syntax only. | Compare the requested phase and declared precision, not just process exit status. |
| Diagnostics | Stable codes and 1-based UTF-8 byte columns; nested Row gives E015 at 6:9. Re-confirmed at `183a6da` — the fixture blob is unchanged (`3ee73d7e`). | Same nested-Row fixture gives E015 at line 6, absent column (wire position precision is line-only). Re-confirmed at `4f114dd` by the observation probe: `MEDUI-E015 line=6 column=absent`. | Absent precision is not column 1 or a full-position pass. Messages and fix prose are not identity. |
| Closed values | Newer pin includes MEDUI-DEC-006, clock/action domain/member cases E033/E034. | Older shared pin predates that decision, even where local enums agree. | A matching enum is not proof of matching rejection/precedence behavior. |
| Missing fields and references | Local checks exceed the small shared corpus. | Some reference failures remain unregistered; optional safety promotion is disputed. | Resolve [MedUI #2](https://github.com/Compliatory/MedUI/issues/2), [#3](https://github.com/Compliatory/MedUI/issues/3) and [#8](https://github.com/Compliatory/MedUI/issues/8); do not invent a common error code. |
| Layout | Bounded integer rectangles and flat paint-order nodes; positioned layout restrictions. | Corresponding fixed/row layout and flattened nodes. | Compare ordered node IDs/kinds/rectangles and failure cases, not generated C++ versus Rust. Layout issue #2 remains open. |
| Safety/goldens | Compiler rederives obligations; committed golden sidecar; verifier checks complete applicable obligations. | Golden references embedded in compiled package; shared safety phase unclaimed. | Compare selection/deduplication independently of rendered check meaning; annotation/required-field precedence needs #8. |
| Artifact envelope | Screen ID/version, surface extent, text/locale/digest and image approvals, nodes and draw budget; separate golden sidecar. | Screen ID, layout kind/spacing/padding, nodes and golden references. | Normalize logical fields; retain native envelope and provenance. Native bytes/layout are intentionally different. |
| Binding identity | Readings, status and field values bind by node ID; trace binds caller-owned source data. | Frame inputs generally route numbers, text, states and streams by source name. | Normalize screen/node plus declared source mapping. Never assume source and node identifiers are interchangeable. |

The first assessment was written against an 18-case shared corpus (2 syntax, 14 semantics, 1
layout, 1 safety). **MduX now pins `v0.3.0-rc.1` (`9a57f64`)**, whose compiler corpus is 27 cases
(5 syntax, 17 semantics, 3 layout, 2 safety); MduX's shared-conformance harness runs all 27 at
`positions = "full"`, and #314 Stage A fixed four pre-existing MduX compiler defects the larger
corpus exposed. **TrustSC still pins the 18-case `0.1.0-candidate` (`c8cc45e`)**, so the two
consumers are not compiling the same corpus — the [roadmap results
table](../roadmap.md#314-per-capability-conformance-results) records each side's claim on its own
pin, and closing that gap is [#335](https://github.com/ambroise-leclerc/MduX/issues/335). This is
not exhaustive coverage of either compiler. [MedUI #4](https://github.com/Compliatory/MedUI/issues/4)
tracks missing cases. MEDUI-DEC-003/004/005 define compiled observables, golden selection and
diagnostic declarations, respectively; none establishes pixel equivalence. See the pinned
[MedUI decisions][decisions], [MduX schema][m-schema], [MduX compiler][m-compiler],
[TrustSC schema][t-schema] and [TrustSC compiler][t-compiler].

## Component fields, presentation and dynamic data

All nodes have an ID, kind and resolved rectangle. This table lists the payload correspondence;
`requirement` means a source traceability identifier, not a clinical requirement inferred here.
Names are normalized concepts, not new wire keys. The dictionary has eleven authored kinds;
Panel is an additional synthesized Row-background node, not authored component syntax.

| Kind | Logical payload | Observed runtime/presentation difference |
|---|---|---|
| Row → Panel | Row height, spacing, optional background; Panel color | Both flatten the layout and paint an underlay; retain the resulting paint order for occlusion. |
| Label | text key, color | MduX uses an approved baked text run; TrustSC joins baked text packages at startup. Font/placement differences preclude an undeclared pixel claim. |
| Button | label key, color, source, optional requirement | Neither payload is a CriticalButton action. MduX has dimmed face plus label and coordinate resolution; TrustSC adds press/release behavior and pressed appearance. |
| CriticalButton | requirement, label key, color, closed action | Both have NoOp/TriggerHalt; MduX's internal Unspecified sentinel is invalid and not a wire action. MduX returns traced action data; TrustSC's adapter dispatches the halt event and exits. Shutdown policy belongs to the host. |
| NumericDisplay | requirement, template, source, color | MduX accepts caller-bound fixed-point reading/template data; TrustSC updates a source-keyed number. Formatting, scaling and overflow must be specified before replay equivalence. |
| Clock | TimeSeconds or DateTimeSeconds | MduX receives caller-supplied CivilTime and tint; TrustSC adapter normally supplies wall time and scenarios pin it. Compare injected time and explicit tint, not live wall clocks. |
| StatusIndicator | requirement, source, ordered states, optional source colors | TrustSC fills omitted colors with Neutral. MduX compiled color list may remain empty; its live StatusBinding refuses that list. Both need closed, valid state selection; absent-color behavior is a concrete unresolved default. |
| TextInput | source, maximum length, color, optional charset and requirement | MduX stores resolved scalar ranges with the charset name and draws caller-supplied value/caret on a fixed-pitch grid. TrustSC stores a glyph-set ID and u16 length, adds focused/unfocused field chrome and a character-indexed editing model. Compare accepted repertoire and length units, not native integer widths. |
| SignalTrace | stream source, color | MduX expands a caller-owned sample ring within a declared vertex budget; TrustSC streams samples through FrameInputs. Sample order, range and snapshot identity need normalization. |
| VulkanViewport | stream source | MduX reserves geometry but defers stream content; TrustSC paints a bounded waterfall. Dimensions, row/bin order, numeric range and overflow remain #322 design inputs. |
| Image | approved image reference | Both consume baked image data. Preserve approval/digest and resolved rectangle; a matching image ID alone does not establish matching pixels. |

Sources: [MduX payloads][m-schema], [screen renderer/bindings][m-screen],
[TrustSC payloads][t-schema], [compiler defaults][t-compiler], [renderer][t-renderer] and
[input model][t-input]. Dynamic draw bindings existing in MduX do not mean the current static
verification driver supplies those values; dynamic driver/capture work remains #319–#321.

## Interaction boundary

MduX [resolvePress()][m-hit] walks nodes in reverse paint order, uses half-open rectangles and
lets every covering node occlude lower nodes. The first covering non-control returns no action.
An ordinary Button resolves its source and optional requirement; a CriticalButton resolves a
closed, traced action. This function neither queues platform events nor executes the action.

The [TrustSC adapter][t-adapter] searches separate button target lists, arms a press and activates
on release within the armed target. It separately manages text focus. This is not MduX's
all-node reverse-order occlusion. Physical-to-authored coordinate scaling also belongs to the
adapter. TrustSC's bounded queue drops the newest event on overflow and saturates a drop counter;
its text model reserves storage at construction. MduX does not yet have that application path.

[MedUI #16](https://github.com/Compliatory/MedUI/issues/16) proposes separate logical interaction
and presentation profiles. The local prospective requirements specify edge/occlusion cases,
cancelled activation, observable overflow, bounded editing and update/capture order. They are
not an instruction to execute a clinical action or to copy TrustSC's host shutdown behavior.

**#315 and #316 fixed the MduX side of this comparison.**
[ADR-018](../adr/ADR-018-bounded-input-and-update-order.md) (Accepted 2026-09-08) and the
`mdux.medui.input` module decide the local event vocabulary, the fail-closed **floor-toward-−∞**
coordinate-normalization rule (a truncating cast would be a latent wrong-target activation once an
out-of-flow `position:` sits at a negative authored coordinate), the drop-newest / saturating /
cancel-the-arm overflow policy, the press-arms / release-activates-same-target model over the
existing `resolvePress()` occlusion, the bounded scalar-indexed editing contract, and the
input→update→render order. #316 delivered `EventQueue` (the caller-owned bounded ring) and
`FieldEditor` (the editing state — both charset bounds, `max_length`, no partial mutation), both
allocation-free. The critical action stays **resolved and traced by MduX, executed by the host**.
TrustSC's observations above are unchanged; the remaining MduX-side work is the platform adapter
(#317) and the assembled monitor (#318), and no `medui-conformance.toml` key claims
`MEDUI-PROFILE-INTERACTION`. PAR-REQ-004–008 dispositions are ratified in ADR-018.

The pinned `v0.3.0-rc.1` checkout carries observation vectors for `MEDUI-PROFILE-INTERACTION`,
`-BINDING`, `-PRESENTATION` and `-PIXELS` as well, but **#314 stops at `MEDUI-PROFILE-RENDERED`
and `MEDUI-PROFILE-EVIDENCE`** — MduX claims only those two, and `conformance_spec` rejects a
manifest that claims a profile with no adapter. The interaction and presentation profiles are
picked up by the input, streaming and authoring work (#315–#327), not here. This section stays a
design-level comparison; #314 changed neither side's interaction code.

## Rendered checks and tolerances

| Check | MduX observation | TrustSC observation | Proposed treatment |
|---|---|---|---|
| Bounds / GoldenBounds | Measured content extent must equal golden rectangle; empty fails. Detection is local to the check region and is not proof of no distant overflow. Named `mdux.local/extent-equality` v1 since #313. | Ink bbox in a clamped region expanded by 8 pixels must be contained in the expected bounds; no ink passes this check. | Separate extent-equality and containment identities, with explicit empty-content and search policies. |
| ColorHash | No hash: common-coverage tint/background composition; fully resolved tint must occur. Missing tint fails. Named `mdux.local/tint-composition` v1 since #313; the raw-pixel-digest observation is `mdux.local/raw-image-digest` v1, implemented but with no committed baseline. | SHA-256 over tightly packed row-major RGBA8 of the golden rectangle. Missing baseline returns NoBaseline, never pass. | Separate tint-composition and RGBA8-digest identities. Never reuse a name to reinterpret old evidence. |
| Color tolerance | One UNORM step per modeled composite; existing two-layer fields need two. Channels share a feasible coverage; arbitrary independent channel slack is not permitted. | Exact hash has zero tolerance; ChromeColor uses ±1 per channel in its sampled edge bands. | Profile/check-specific arithmetic and sampling; no global fuzzy tolerance. |
| TextPresence | Approved atlas coverage and placed glyph checks; ink containment has explicit composite-ground limits. Named `mdux.local/ink-containment` and `mdux.local/ink-coverage`, both v1, since #313. | Glyph-count coverage heuristic (5–70% of estimated glyph area), not a glyph-shape comparison. | Distinct shape/coverage predicates, asset identity and applicability. |
| Ink/background | Resolved ground/tint and baked placements; bounds and ink checks have documented local limits. | Ink delta threshold 8; containment/search margins 8; local panel/chrome background handling. | Record thresholds, ground resolution and ROI in the profile; do not transfer one sibling's constants into the other silently. |
| Report scope | Artifact provenance and node/check with explicit locale-free or locale scope; driver enforces complete obligations. Each outcome in `verification.json` now records an `observationProfile` {id, version} (#313). | Check IDs such as node::golden_bounds; locale/scenario/clock also carried by the enclosing report. | A common observation key must include scope and provenance, not just the local check ID. |

Sources: [MduX predicates][m-verify], [implementation arithmetic][m-arithmetic],
[MduX fixtures][m-tests], [TrustSC checks][t-checks], [report types][t-verify] and
[TrustSC fixtures][t-tests]. No committed TrustSC hash baselines exist at this assessed head;
the ability to compute/compare a hash is not evidence of a committed image regression gate.
[MedUI #15](https://github.com/Compliatory/MedUI/issues/15) requests the versioned resolution.

### `#314d` — the MduX side of the "Proposed treatment" column is delivered and gated

The four merged stages of [#314](https://github.com/ambroise-leclerc/MduX/issues/314) turned the
proposal above into running code on the MduX side. TrustSC's observations in the table are
unchanged (`4f114dd`); the delivery is one-sided until [#335](https://github.com/ambroise-leclerc/MduX/issues/335).

- **Separate versioned identities** ([#313](https://github.com/ambroise-leclerc/MduX/issues/313),
  [ADR-016](../adr/ADR-016-locally-versioned-observation-profiles.md)). Each of MduX's four
  rendered checks carries an implementation-local `ObservationProfile` (id + version), recorded per
  outcome in `verification.json`. `mdux.verify::rawImageDigest()` implements the RGBA8 SHA-256
  observation TrustSC's `ColorHash` performs, as its own `mdux.local/raw-image-digest` profile, so
  the two `ColorHash` results are never conflated — with **no committed baseline** (a
  driver-tuple-dependent digest cannot live in a byte-compared artifact, ADR-014 D4), `NoBaseline`
  on every production call, fixture-tested only. The **committed** `verification.json` keeps the
  `mdux.local/*` ids; the canonical `MEDUI-PROFILE-RENDERED` ids (`extent-equality`,
  `ink-containment`, `tint-composition`, `rgba8-sha256`) exist at the pin, and the **derived**
  envelope below already emits them. Migrating the byte-compared artifact to them is gated on a
  final 0.3.0 release, the cross-implementation pass (#335) and a reviewed re-bake — see
  [ADR-017 §3](../adr/ADR-017-sibling-conformance-gate-status.md). `mdux.local/ink-coverage` has no
  RENDERED equivalent and stays local.
- **The shared RENDERED corpus** (Stage B). `medui-conformance.toml` claims
  `MEDUI-PROFILE-RENDERED`, and `conformance_spec` runs `v0.3.0-rc.1`'s 33 `rendered-check` vectors
  (rules R01–R04) against `mdux.verify`'s own exported predicates — `rectContainedBy`, `inflate`,
  `couldBeBlend` — with the ROI digest from `mdux.evidence`'s `sha256`, plus the 10
  `consumer-manifest` contract cases and committed negative fixtures, on all five CI legs. One
  arithmetic implementation serves both the local profiles and the corpus.
- **The shared EVIDENCE corpus** (Stage C). `medui-conformance.toml` also claims
  `MEDUI-PROFILE-EVIDENCE`; `conformance_spec` runs the 31 `aggregate-evidence` vectors (E01–E03:
  fieldwise identity match, one row per derived obligation, `pass`/`fail`/`unsupported`/`not-run`
  aggregation) and the 29 `evidence` contract documents, every document validated by
  `mdux.tools.schema` — a JSON-Schema-subset engine ported from MedUI's `tools/schema_check.py`
  that fails closed on an unimplemented keyword.
- **MduX's own derived envelope** (Stage C-emitter). `mdux-verify-ui --medui-evidence-out=<dir>`
  writes `<screen>.medui-evidence.json`, the `MEDUI-PROFILE-RENDERED` E01 envelope derived from a
  real verify run — obligations synthesized from the screen and its checks,
  `Bounds`/`ColorHash`/`InkContainment` mapped to
  `extent-equality`/`tint-composition`/`ink-containment`, `LocalizedTextPresence` excluded and
  counted as implementation-local. `backend` is the Vulkan `deviceName` and `producer.source` the
  build commit, so the envelope is **derived and uncommitted** (ADR-014 D4, ADR-007 D5). A GPU leg
  checks it is `evidence.schema.json`-valid and aggregates to `pass`. `deriveRenderedEvidence()`
  **fails closed**: one obligation per enumerated obligation (not per report row), refusing an
  incomplete one-to-one pairing (`OutcomeMismatch`), an unrecognised check name (`UnknownCheck` —
  only `LocalizedTextPresence` may be excluded) and a run that never rendered (`NotRun`).

[ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md) (Accepted 2026-09-08) records the
ratified PAR-REQ-001/002/003 dispositions this delivery supports — PAR-REQ-001 Accepted,
PAR-REQ-002 Accepted with amendment, PAR-REQ-003 Accepted with a recorded subset limitation. None
of it is a cross-implementation parity claim: the
[roadmap results table](../roadmap.md#314-per-capability-conformance-results) is per-consumer, and
TrustSC has not re-pinned.

## Intentional differences

- MduX keeps its supported Windows/Linux/Apple Silicon tuples and cross-toolchain evidence.
  TrustSC's assessed CI is Linux. A shared language profile does not set platform policy.
- MduX keeps Class A in its A/B/C governance scope; [TrustSC's enum][t-core] models B/C only.
  These record declared classifications. B/C facade metadata wording does not
  confer Rust's unsafe-code prohibition or establish a device classification. C++ trust-zone,
  no-throw and bounded-resource checks remain the actual engineering guarantees. No common
  conformance profile assigns a software safety class or establishes certification.
- The host owns MduX's Vulkan device, presentation and binding storage. Rust-specific lifetime
  types, startup allocation and window ownership are not API requirements for C++. Baked native
  artifacts may differ while declared normalized observables agree.

See [architecture](../architecture.md), [governance types](../../include/mdux/governance/Governance.cppm),
[ADR-004](../adr/ADR-004-trust-zones-in-cpp.md) and
[ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md).

## Executed observations and reproduction

**Re-assessed 8 September 2026 for `#314d`.** The configured GCC build ran the selection below
against MduX `183a6da` with `MEDUI_CONFORMANCE_DIR` set: **313 passed, 0 failed** (adds
`conformance_spec` to the earlier selection). This is not a GPU capture comparison; the derived
RENDERED envelope and its `pass` aggregation are exercised on the GPU legs in CI, not here.

```sh
export MEDUI_CONFORMANCE_DIR="$PWD/.medui-conformance"
ctest --test-dir build-gcc -R '^(verify_spec|medui_tools_spec|medui_spec|conformance_spec)::' --no-tests=error --output-on-failure
```

`conformance_spec` reports **33 `rendered-check` vector(s)** and **31 `aggregate-evidence`
vector(s)** at contract `9a57f64`, each run against `mdux.verify`'s own arithmetic or as pure
aggregate list logic, plus the two adapter/aggregate-disagreement negatives and the manifest and
schema validators with their malformed-input negatives. Other notable fixtures: `A golden
rectangle moved by one pixel fails, and an empty one fails differently`, wrong/absent tint,
impossible cross-channel blend, the reduced-coverage field under a full-tint stroke.

TrustSC is unchanged at `4f114dd`. Its verifier crate builds with Rust/Cargo 1.96.0 (**30 tests
passed**, no doc tests); the combined authoring/checker test run still cannot unpack cached
`arrayvec` into the read-only Cargo home and is not reported as passing. The
[observation probe](trustsc-observation-probe.rs) was **re-run on 8 September 2026** against the
`4f114dd` archive and re-confirmed:

```text
nested-row: MEDUI-E015 line=6 column=absent
empty golden: GoldenBounds=pass ColorHash=no_baseline
```

The node-free synthetic package isolates those golden predicates; it does not claim an empty real
screen would pass every applicable check.

To reproduce, extract the exact TrustSC SHA above into `/tmp/trustsc-parity-20260906` (a GitHub
commit archive suffices) and run from MduX. The nested-Row `source.medui` fixture is byte-identical
at every MedUI pin used here (Git blob `3ee73d7e4287b30bbb50d2554a95b414dc71a4b1`), so the pinned
checkout at `.medui-conformance/` can feed the probe directly:

```sh
cargo build --offline --locked --manifest-path /tmp/trustsc-parity-20260906/Cargo.toml \
  --target-dir /tmp/mdux-312-trustsc-target -p trustsc-ui -p trustsc-ui-verify -p trustsc-ui-dsl-authoring
rustc --edition=2024 docs/parity/trustsc-observation-probe.rs \
  --extern trustsc_ui=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui.rlib \
  --extern trustsc_ui_verify=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui_verify.rlib \
  --extern trustsc_ui_dsl_authoring=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui_dsl_authoring.rlib \
  -L dependency=/tmp/mdux-312-trustsc-target/debug/deps -o /tmp/314d-observation-probe
/tmp/314d-observation-probe "$PWD/.medui-conformance/conformance/syntax/rejected-nested-row/source.medui"
```

Offline mode needs the lockfile's dependencies in the local cache. This probe is supporting
evidence; MduX's own `conformance_spec` is the CI gate. Widget/presentation/dynamic comparisons
above are source inspection; no cross-sibling pixel or event-replay equivalence was executed or
established.

## #335 paired adoption results

Re-assessed **8 September 2026** against MduX
[`a722784`](https://github.com/ambroise-leclerc/MduX/commit/a722784de5c958cec9ede7903bdf577eb10fe09f)
and TrustSC
[`b21c423`](https://github.com/ambroise-leclerc/TrustSC/commit/b21c423f590d37c98625e3101ef41787ee4fb13c).
Both manifests pin MedUI `9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb` (`v0.3.0-rc.1`).
The [roadmap's paired results](../roadmap.md#335-paired-adoption-results) are the authoritative
capability table; this section records the behavioral consequences and reproduction.

MduX's implementation is unchanged from `183a6da`: the intervening changes update documentation.
TrustSC's adoption changes the parser/checker, Studio file loading and conformance gate; inspection
of its diff against
`4f114dd` found no runtime, renderer, binding, layout or golden-predicate changes. The component,
interaction, rendering and intentional-difference rows above therefore retain their earlier
assessment. They have not become cross-implementation tests merely because the pins now agree.

The existing [observation probe](trustsc-observation-probe.rs) was also compiled and re-run against
TrustSC `b21c423`: `nested-row: MEDUI-E015 line=6 column=absent` and
`empty golden: GoldenBounds=pass ColorHash=no_baseline`. These remain isolated predicate results,
not whole-screen conformance, and confirm that the earlier rendering limitations still apply.

| Changed observation | Reassessment |
|---|---|
| Contract revision | The adoption branch now reads the same 27 compiler cases as MduX. TrustSC executes the 5 syntax cases and names the remaining 22 as unclaimed; it does not report 27 passes. |
| Duplicate authored IDs | TrustSC refuses them during parsing across root nodes, Rows and children, using final IDs and the later effective declaration's line; overwritten values reserve no names. Its compiled-node check remains for synthetic panel collisions and edited ASTs. The pinned duplicate-ID source gives E014 at line 11 in both siblings. |
| Source encoding | TrustSC's production byte parser, file compiler, checker and Studio file endpoints distinguish invalid UTF-8 (E004) from unreadable source (E003). The pinned invalid-UTF-8 source gives E004 at line 1 in both siblings. |
| Diagnostic precision | The nested-Row source remains E015 at line 6. TrustSC reports no columns for any of the five cases; MduX checks every pinned column. Full-position equivalence remains unestablished. |
| Closed values and later compiler phases | The new pin includes E033/E034/E035 and the newer layout/safety cases. TrustSC does not execute those phases; acquiring their fixtures supplies no semantic, layout or safety conformance evidence. |
| Known safety divergence | An annotation does not promote an optional Button/TextInput requirement to mandatory: E070 is not implemented. This is a behavioral gap in the unclaimed safety phase, not merely a missing diagnostic constant. |
| Shared observation profiles | TrustSC still has no complete RENDERED/EVIDENCE adapters. Native `ColorHash` and native report shapes retain the differences recorded above. No GPU/capture or derived-envelope comparison was executed. |

The paired local selections passed: MduX **313 tests**, TrustSC **70 tests**. The syntax harness
reads all expected results from the contract; its negative test loads the source and runs a positive
control before inverting validity, then checks the exact assertion message. I/O failures cannot
satisfy that negative. Studio's **36 tests** also passed, including file endpoint encoding and I/O
regressions. Another negative proves that adding a phase
without an adapter is refused. The TrustSC gate runs in a named CI step. CI integration and joint
review status are recorded in the roadmap rather than inferred from the local totals.

To reproduce, use checkouts at the two implementation SHAs above and the exact contract SHA:

```sh
# From MduX, with its configured GCC build:
export MEDUI_CONFORMANCE_DIR=/path/to/MedUI-at-9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb
ctest --test-dir build-gcc -R '^(verify_spec|medui_tools_spec|medui_spec|conformance_spec)::' --no-tests=error --output-on-failure

# From TrustSC, retaining the same MEDUI_CONFORMANCE_DIR:
# Copy the already populated registry cache into a fresh writable Cargo home.
mdux335_cargo_source="${CARGO_HOME:-$HOME/.cargo}"
mdux335_cargo_copy="$(mktemp -d /tmp/mdux-335-cargo.XXXXXX)"
cp -R "$mdux335_cargo_source/registry" "$mdux335_cargo_copy/"
chmod -R u+w "$mdux335_cargo_copy"
export CARGO_HOME="$mdux335_cargo_copy"
CI=1 cargo test --offline --locked -p trustsc-ui-dsl-authoring --test shared_conformance -- --nocapture
cargo test --offline --locked -p trustsc-ui-dsl-authoring -p trustsc-medui-check
cargo build --offline --locked --workspace
cargo test --offline --locked --quiet
```

The local TrustSC run used Rust/Cargo 1.96.0, `--offline`, and a writable copy of the Cargo cache
under `/tmp`, as shown above. The source cache must already contain the registry packages required
by the pinned `Cargo.lock` for the selected host; `--offline` does not download missing dependencies. Copying it to a
writable directory allows Cargo to unpack cached packages despite the original read-only Cargo
home, resolving that specific limitation of the historical `#314d` run. These results support common
syntax acceptance and diagnostic codes/lines. They do not change
PAR-REQ-002/003's unverified cross-implementation observations or assign clinical risk controls.
Impact is **potentially safety-relevant conformance documentation**: only the recorded evidence
changes in MduX; no runtime code, baked artifact, safety class or certification claim changes.

[m-manifest]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/medui-conformance.toml
[t-manifest]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/medui-conformance.toml
[decisions]: https://github.com/Compliatory/MedUI/tree/9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb/decisions
[m-schema]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/include/mdux/medui/Schema.cppm
[m-compiler]: https://github.com/ambroise-leclerc/MduX/tree/183a6da3190e673c41edbe9050586d4dc0fa4fa4/tools/medui
[m-screen]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/include/mdux/medui/Screen.cppm
[m-hit]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/src/medui/Screen.cpp
[m-verify]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/include/mdux/verify/Verify.cppm
[m-arithmetic]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/src/verify/Verify.cpp
[m-tests]: https://github.com/ambroise-leclerc/MduX/blob/183a6da3190e673c41edbe9050586d4dc0fa4fa4/tests/verify/GoldenCheckTests.cpp
[t-schema]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui/src/lib.rs
[t-compiler]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-dsl-authoring/src/lib.rs
[t-renderer]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/adapters/trustsc-vulkan-winit/src/renderer.rs
[t-adapter]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/adapters/trustsc-vulkan-winit/src/lib.rs
[t-input]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc/src/input.rs
[t-checks]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/checks.rs
[t-verify]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/lib.rs
[t-tests]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/tests.rs
[t-core]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-core/src/lib.rs
