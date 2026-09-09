# MduX ↔ TrustSC parity roadmap

> Assessed 6 September 2026 against MduX `develop` at
> [`e68e04b`](https://github.com/ambroise-leclerc/MduX/commit/e68e04b9f41277775dd4a13b692790aa8c13ad86)
> and TrustSC `main` at
> [`4f114dd`](https://github.com/ambroise-leclerc/TrustSC/commit/4f114dd30c64f61d11edb5941e95f422e189f305).
> All thirteen original parity epics (#7–#19) are closed. PR #306 closed #304, and #19
> was closed on 6 September. All post-merge MduX workflows reported success at this head,
> including the four platform/compiler build legs and sanitizers.
> Phase 2 below is newly planned work: five open epics and sixteen open child issues.
>
> **Update, 8 September 2026:** epic #307's child #314 is merged on the MduX side (four stages,
> through `183a6da`). The [#314 per-capability conformance results](#314-per-capability-conformance-results)
> table records what each consumer's own pin demonstrates; the cross-implementation baseline is
> [#335](https://github.com/ambroise-leclerc/MduX/issues/335). TrustSC's published head is unchanged at
> `4f114dd`, so the Phase-2 comparison below still stands as assessed.
>
> **#335 paired adoption results, 8 September 2026:** TrustSC `b21c423` now pins the same MedUI
> revision as MduX. The [paired adoption results](#335-paired-adoption-results) below record a
> shared syntax baseline at line precision. The earlier comparison remains historical;
> unclaimed phases/profiles and joint sign-off remain explicit gaps.

The original six waves delivered the foundations: trust zones, governance records, baked evidence,
a real Vulkan renderer, deterministic ML inference, fonts and text, a host-side MedUI compiler,
bounded widget rendering and rendered-screen verification. Those features are implemented. The
next programme connects them into an interactive application and an authoring workflow, and makes
the two siblings' shared behavior precise enough to compare.

This is a comparison of code and recorded CI evidence, not a new build of both repositories or a
claim of product certification. MduX remains an experimental C++23 project; TrustSC's Rust
implementation is a comparison target, not proof of clinical readiness. MduX's Class A/B/C
metadata scope and TrustSC's B/C scope remain an intentional difference.

| Scope at this assessment | State |
|---|---|
| Original parity epics #7–#19 | 13 closed; no remaining children |
| Original release waves | Six shipped, v0.2.0 through v0.8.0 |
| Phase 2 epics | 5 open: #307–#311 |
| Phase 2 child issues | 16 open: #312–#327 |
| Additional platform epic #222 | Closed; outside the original thirteen-epic count |
| First actionable issue | [#312](https://github.com/ambroise-leclerc/MduX/issues/312), behavior matrix and versioned decisions |

## Current comparison

The table is re-assessed at the two commits above. The older August comparison and its test totals
are superseded; closing the original backlog does not imply complete application-level parity.

| Area | MduX at the assessed head | TrustSC at the assessed head | Remaining work |
|---|---|---|---|
| Architecture and governance | Governed core, separate Vulkan adapter and host tools; requirement/hazard/verification records, traceability and evidence exports exist. | Governed crates, presentation adapter and host tools; corresponding governance records and application facade exist. | Preserve boundaries; add application integration rather than rebuild governance. |
| MedUI compilation | Published grammar, stable diagnostics, resolved IR, C++ emitters and committed screen bundles. Claims syntax, semantics, layout and safety with full diagnostic positions. #314 re-pinned to `v0.3.0-rc.1` and gates all 27 compiler cases from the pinned corpus. | Build-time compilation and Rust output; current shared manifest claims syntax with line-only positions against an older pin. | #307: TrustSC re-pin and joint sign-off ([#335](https://github.com/ambroise-leclerc/MduX/issues/335)); the MduX corpus-derived gate is delivered. |
| Content and dynamic bindings | Label, image, numeric/clock, trace, status, text/caret and control faces are implemented. `resolvePress()` resolves a traced action. | Corresponding components are connected to application state and platform input. | #308: bounded interaction, editing and a presented medical monitor. |
| Presentation and streaming | `UiRenderer` consumes host Vulkan resources; medical examples build draw lists, while the windowed example presents a triangle. A `VulkanViewport` reserves geometry but its stream content is deferred. | The Vulkan/winit adapter presents medical screens and renders a bounded streaming waterfall. | #308 for presentation; #310 for viewport content. “Every component draws” does not mean the MduX viewport already has a stream renderer. |
| Verification | Committed bundle verification checks approved locale text and golden obligations. The current driver binds text/images, not live reading, trace, status or field state. `ColorHash` is a tint predicate, versioned `mdux.local/tint-composition` v1; the raw RGBA8 digest is a separate `mdux.local/raw-image-digest` with no committed baseline. #314 gates `MEDUI-PROFILE-RENDERED`/`-EVIDENCE` against the pinned corpus and emits a derived RENDERED envelope. | Scenario replay supports events, frame advances, state expectations, pinned time and captures. `ColorHash` compares raw-pixel digests; no baseline files are committed at this head. | #307 predicate meaning is resolved and gated on the MduX side; #309 verifies dynamic application behavior without weakening the static gate. |
| Authoring tools | Machine-readable grammar, diagnostics, IR, recipe schemas and tool manifest. | MedUI Studio has real-renderer previews, editing, palette/inspector, undo/redo and change proposals; a VS Code extension supplies syntax highlighting. | #311: host editing/preview interfaces and Studio integration, with a reuse decision before a fork. |
| Evidence, ML and text | Recipe-driven committed artifacts, cross-toolchain byte checks, shared host/device inference kernels, fail-closed model creation and bounded runtime bindings. | Baked font/image/shader/model artifacts, deterministic inference and bounded draw paths; the screen/text join still allocates at startup. | Retain existing MduX guarantees. TrustSC #47 is a sibling-side tightening proposal, not missing MduX work. |
| Platform evidence | MSVC/Windows, GCC/Linux, Clang/libc++ on Linux and Apple Silicon macOS, with pixel/evidence gates and sanitizers. | Current CI builds/tests the Rust workspace on Linux with lavapipe, baker verification, monitor smoke and Studio preview checks. | Do not trade MduX's wider platform coverage for API similarity. |

### Sources and limits

The comparison uses these implemented mechanisms, rather than feature names alone:

- [MduX conformance declaration](../medui-conformance.toml) and
  [TrustSC conformance declaration](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/medui-conformance.toml).
  Declared coverage differs; unclaimed phases are not evidence that TrustSC lacks all local behavior.
- [MduX screen runtime](../include/mdux/medui/Screen.cppm),
  [example targets](../examples/CMakeLists.txt), and
  [TrustSC input model](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/crates/trustsc/src/input.rs).
- [MduX verification driver](../tools/verify/Driver.cpp),
  [governed predicates](../include/mdux/verify/Verify.cppm), and
  [TrustSC scenario replay](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/crates/trustsc/src/verify_scenario.rs).
  Tint checking and pixel hashing must not be reported as interchangeable evidence.
- [TrustSC verification guide](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/docs/verification/ui-verification.md)
  explicitly records the absent committed hash baselines. Its property checks run, but that does
  not establish a committed exact-image regression gate.
- [TrustSC Studio](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/tools/trustsc-medui-studio/README.md)
  and [stream renderer](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/adapters/trustsc-vulkan-winit/src/renderer.rs)
  supply concrete application/authoring comparison targets.
- [MduX post-merge GCC run](https://github.com/ambroise-leclerc/MduX/actions/runs/34060338888),
  [Windows run](https://github.com/ambroise-leclerc/MduX/actions/runs/34060339257),
  [macOS run](https://github.com/ambroise-leclerc/MduX/actions/runs/34060338909),
  [Linux Clang run](https://github.com/ambroise-leclerc/MduX/actions/runs/34060338848), and
  [TrustSC CI](https://github.com/ambroise-leclerc/TrustSC/actions/runs/33518269343)
  are the observed integration evidence. No test-total comparison is used as a measure of parity.

Manual generation is still [TrustSC #6](https://github.com/ambroise-leclerc/TrustSC/issues/6), not a
delivered sibling feature to copy. An allocation-free startup text join is
[TrustSC #47](https://github.com/ambroise-leclerc/TrustSC/issues/47). Full Unicode shaping, a native
Vulkan SC deployment and clinical qualification are not established by this comparison and are
not silently added to Phase 2. The [MduX text baker](../tools/text/TextBake.cppm) documents and
enforces its limited LTR repertoire; the TrustSC font baker's code-point walk is not proof of
general shaping support either.

## Phase 2 — application and authoring parity

These issues are open planning records, including those blocked by design decisions. Creating a
record does not authorize its implementation to bypass a dependency. This replaces the old
practice of waiting for an entire predecessor epic to close before recording its successor's
children: child-level dependencies now make the next executable step explicit.

The first step was #312. Its [pinned behavior matrix](parity/behavior-matrix.md),
[accepted ADR-015](adr/ADR-015-versioned-sibling-observations.md) and
[requirements and decision gates](parity/requirements.md) define the review
surface. MedUI decisions for [#15](https://github.com/Compliatory/MedUI/issues/15)
and [#16](https://github.com/Compliatory/MedUI/issues/16) are Accepted as MEDUI-DEC-007/008; profile/schema/corpus delivery remains follow-up work.
ADR-015's local architectural direction is accepted, and
[ADR-017](adr/ADR-017-sibling-conformance-gate-status.md) ratifies the PAR-REQ-001/002/003
dispositions (8 September 2026); PAR-REQ-004–010 review and shared
profile adoption gates remain explicit in the decision map. Subject to those gates, input design (#315), viewport
design (#322) and editor API design (#325) can proceed alongside verifier alignment (#313).
Canonical interfaces land before their consumers. No new version number or release date is assigned
until a deliverable and its evidence are agreed.

### [#307](https://github.com/ambroise-leclerc/MduX/issues/307) — Shared MedUI behavior and verification contract · planned

Publish an agreed, versioned compatibility boundary, adopt its verification semantics in MduX, and enforce normalized cross-implementation observations. Preserve intentional differences in language, supported safety classes and resource ownership.

| Child | Deliverable | Prerequisites |
|---|---|---|
| [#312](https://github.com/ambroise-leclerc/MduX/issues/312) | Define the sibling behavior matrix and versioned MedUI decisions | Actionable now |
| [#313](https://github.com/ambroise-leclerc/MduX/issues/313) | Implement agreed verification semantics without weakening existing evidence | [#312](https://github.com/ambroise-leclerc/MduX/issues/312) |
| [#314](https://github.com/ambroise-leclerc/MduX/issues/314) | Gate sibling conformance against one pinned observation corpus | [#312](https://github.com/ambroise-leclerc/MduX/issues/312), [#313](https://github.com/ambroise-leclerc/MduX/issues/313) |

**#313 landed its verifier semantics.** The **local** half is
[ADR-016](adr/ADR-016-locally-versioned-observation-profiles.md) (Accepted 2026-09-09) — five
`mdux.local/` observation profiles recorded per outcome in `verification.json`, plus the fixture-only
`rawImageDigest()` predicate — and the **shared** half is #314 Stage B. The **#313 amendment**
(2026-09-09) completed the ADR-017 §3 committed-artifact migration: a `Bounds` or `ColorHash` outcome
now also records a candidate `candidateProfile` naming the shared `MEDUI-PROFILE-RENDERED` `{profile,
check}` identity (`extent-equality/1`, `tint-composition/1`) beside the retained local one, via one
pure `canonicalRenderedCheckFor()` in `mdux.verify`, because `goldenBounds()` and `colorHash()`
compute exactly R01 and R03. `InkContainment` (a compound predicate stronger than shared R02) and
`LocalizedTextPresence` (no shared equivalent) stay local and carry no `candidateProfile`.
`canonicalRenderedCheckFor()` also names R04's `rgba8-sha256/1` for `raw-image-digest`, but that
predicate is fixture-only with no committed baseline, so no R04 `candidateProfile` reaches
`verification.json`. No check semantics, finding or runtime behaviour changed. Residual, recorded on
ADR-016: a final 0.3.0 minor pin (currently `-rc.1`), the #335 joint rendered/evidence sign-off, a
reviewed re-bake against the final line, and the verifier-area domain review for PAR-REQ-002/003.

The #314 gate landed in four merged stages. **Stage A** ([#331](https://github.com/ambroise-leclerc/MduX/pull/331)):
`medui-conformance.toml` re-pinned to MedUI `v0.3.0-rc.1` (`9a57f64`), the shared-conformance test
green over all 27 compiler cases at `positions = "full"`, adding `MEDUI-E035` and `MEDUI-E054`.
**Stage B** ([#332](https://github.com/ambroise-leclerc/MduX/pull/332)): the manifest claims
`MEDUI-PROFILE-RENDERED`, and the new `conformance_spec` suite gates MedUI's 33 `rendered-check`
vectors (R01–R04) against `mdux.verify`'s own arithmetic. **Stage C**
([#333](https://github.com/ambroise-leclerc/MduX/pull/333)): `MEDUI-PROFILE-EVIDENCE` claimed, its
31 `aggregate-evidence` vectors (E01–E03) and 29 `evidence` contract documents gated, and every
contract document validated by `mdux.tools.schema` (a JSON-Schema-subset engine). **Stage
C-emitter** ([#334](https://github.com/ambroise-leclerc/MduX/pull/334)): `mdux-verify-ui
--medui-evidence-out` derives MduX's own `MEDUI-PROFILE-RENDERED` E01 envelope from a real verify
run — schema-valid, aggregating to `pass` on a GPU leg, derived and uncommitted. **Stage D**
(`#314d`, this record): the per-capability results table below, ADR-017's PAR-REQ-001/002/003
dispositions — ratified by maintainer instruction on 8 September 2026 under #312 (PAR-REQ-001
Accepted, PAR-REQ-002 Accepted with amendment, PAR-REQ-003 Accepted with a recorded subset
limitation) — and the TrustSC-side re-pin tracked as a follow-up issue.

#### #314 per-capability conformance results

**Historical #314 assessment; the [#335 paired adoption results](#335-paired-adoption-results) below are newer.**
At these assessed heads the two consumers pin **different** shared-contract revisions, so the table
records what each demonstrates on its own pin — it is not a cross-implementation parity claim.

| Consumer | Implementation SHA | Pinned MedUI contract | Manifest claims |
|---|---|---|---|
| MduX | [`183a6da`](https://github.com/ambroise-leclerc/MduX/commit/183a6da3190e673c41edbe9050586d4dc0fa4fa4) on `develop` | `v0.3.0-rc.1` / [`9a57f64`](https://github.com/Compliatory/MedUI/commit/9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb) | `syntax, semantics, layout, safety`; `positions = "full"`; `profiles = [MEDUI-PROFILE-RENDERED, MEDUI-PROFILE-EVIDENCE]` |
| TrustSC | [`4f114dd`](https://github.com/ambroise-leclerc/TrustSC/commit/4f114dd30c64f61d11edb5941e95f422e189f305) on `main` | `0.1.0-candidate` / [`c8cc45e`](https://github.com/Compliatory/MedUI/commit/c8cc45ecec2f2dfd84940b9efc17c613e691cc0d) | `syntax`; `positions = "line-only"`; no profiles |

| Capability / profile | MduX at `183a6da` — what the gate demonstrates | TrustSC at `4f114dd` | Cross-implementation status |
|---|---|---|---|
| `syntax` | Claimed. All 5 pinned syntax cases pass through the shared harness (`medui_tools_spec`). | Claimed, against the older pin. | Both claim; different corpus revision — not the same baseline. |
| `semantics` | Claimed. All 17 pinned semantics cases pass. | Not claimed. Local resolution exists; unverified against the shared corpus. | Single-side. |
| `layout` | Claimed. All 3 pinned layout cases pass. | Not claimed. Local bounded layout exists; unverified against the shared corpus. | Single-side. |
| `safety` | Claimed. Both pinned safety cases pass; the compiler re-derives obligations. | Not claimed. Golden references are embedded; shared safety phase unclaimed. | Single-side. |
| Diagnostic positions | `full` — every pinned 1-based UTF-8 byte column is matched exactly (`positions = "full"` checked both directions). | `line-only` — the parser carries no column; pinned columns are required absent. | Intentional, recorded difference. |
| `MEDUI-PROFILE-RENDERED` (R01–R04) | Claimed. 33 `rendered-check` vectors run against `mdux.verify`'s own `rectContainedBy` / `inflate` / `couldBeBlend` and `mdux.evidence`'s `sha256` (`conformance_spec` tests for "holds against mdux.verify's own arithmetic" and the adapter-disagreement negative). R04 (`rgba8-sha256`) is exercised as arithmetic only — **no committed baseline** (ADR-016, ADR-014 D4). | Not claimed. `ColorHash` computes a raw RGBA8 digest but commits no baseline. | Single-side. |
| `MEDUI-PROFILE-EVIDENCE` (E01–E03) | Claimed. 31 `aggregate-evidence` vectors (identity match, one row per obligation, `pass`/`fail`/`unsupported`/`not-run` aggregation) plus 29 `evidence` contract documents, every document schema-validated by `mdux.tools.schema` (fail-closed). Missing, duplicate, unknown, unsupported and not-run rows are all rejected. | Not claimed. | Single-side. |
| Derived RENDERED E01 envelope | `mdux-verify-ui --medui-evidence-out` emits MduX's own envelope from a real GPU verify run — obligations synthesized from the screen and its checks, `evidence.schema.json`-valid, aggregating to `pass` on a GPU leg. It is the RENDERED **subset**: `LocalizedTextPresence` is implementation-local, excluded and counted. Derived and uncommitted (ADR-014 D4, ADR-007 D5). | n/a — TrustSC emits its own native report shape. | Single-side. |

MduX's coverage is not lowered to match TrustSC's narrower claim: the four compiler phases and both
profiles stay claimed and gated. A shared baseline requires TrustSC to re-pin to `v0.3.0-rc.1` and
claim the phases and profiles it implements — tracked in
[#335](https://github.com/ambroise-leclerc/MduX/issues/335). The
[pinned behavior matrix](parity/behavior-matrix.md) links here rather than repeating this table.

#### #335 paired adoption results

Assessed 8 September 2026. Both consumers now select the exact contract commit
[`9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb`](https://github.com/Compliatory/MedUI/commit/9a57f6462b6f8dbdf1f0b8b4519674f1c6235dbb)
(`v0.3.0-rc.1`). TrustSC's adoption is [PR #52](https://github.com/ambroise-leclerc/TrustSC/pull/52),
on a work branch, not yet integrated into `main`.

| Consumer | Assessed implementation head | Manifest claims |
|---|---|---|
| MduX | [`a722784`](https://github.com/ambroise-leclerc/MduX/commit/a722784de5c958cec9ede7903bdf577eb10fe09f) on the #335 issue branch; documentation only since `183a6da` | syntax, semantics, layout, safety; full positions; RENDERED and EVIDENCE |
| TrustSC | [`b21c423`](https://github.com/ambroise-leclerc/TrustSC/commit/b21c423f590d37c98625e3101ef41787ee4fb13c) on `335-shared-conformance-baseline` | syntax; line-only positions; no profiles |

| Capability / profile | MduX | TrustSC | Paired conclusion |
|---|---|---|---|
| Syntax | All 5 pinned cases pass with full positions. | All 5 pinned cases pass through the production byte parser. | Same corpus and acceptance/rejection codes; lines agree. Columns are asserted only by MduX. |
| Semantics | All 17 pinned cases pass. | Unclaimed; no complete shared adapter. | MduX-only evidence. |
| Layout | All 3 pinned cases pass. | Unclaimed; no complete shared adapter. | MduX-only evidence. |
| Safety | Both pinned cases pass. | Unclaimed; no complete shared adapter. | MduX-only evidence. |
| Diagnostic positions | Full, including UTF-8 byte columns. | Line-only, with every column required absent. | Precision differs; no full-position equivalence claim. |
| MEDUI-PROFILE-RENDERED | All 33 vectors pass against MduX's arithmetic. | Unclaimed; native checks are not a complete shared-profile adapter. | MduX-only evidence; no paired pixel/capture result. |
| MEDUI-PROFILE-EVIDENCE | All 31 aggregate vectors and 29 evidence contract cases pass. | Unclaimed; native reports do not establish the shared aggregate contract. | MduX-only evidence. |
| Derived RENDERED E01 envelope | Existing GPU CI evidence from #314; not re-run in this local comparison. | No shared envelope claim. | No paired envelope comparison. |

Local verification: MduX's GCC selection of `verify_spec`, `medui_tools_spec`, `medui_spec` and
`conformance_spec` passed **313 tests** with the pinned checkout supplied. TrustSC's parser/checker
selection passed **70 tests** (61 library, 6 CLI, 3 conformance-harness tests), including the five
corpus-derived syntax cases, an inverted-expectation negative and refusal of an unsupported phase
claim. The Studio selection passed **36 tests**, including invalid-encoding versus I/O failures
on detail, frame and proposal endpoints. Its full local workspace build and **319 tests with no failures or ignored tests** also
passed, using Rust/Cargo 1.96.0. The [TrustSC adoption CI run](https://github.com/ambroise-leclerc/TrustSC/actions/runs/34229297866)
completed successfully for `b21c423`, including the conformance gate, workspace build/tests,
documentation lint, artifact verification, headless smoke tests, Studio preview, lavapipe UI
verification and evidence upload.
MduX `a722784` has successful published
[GCC](https://github.com/ambroise-leclerc/MduX/actions/runs/34222463527),
[MSVC](https://github.com/ambroise-leclerc/MduX/actions/runs/34222463504),
[Linux Clang](https://github.com/ambroise-leclerc/MduX/actions/runs/34222463514),
[macOS](https://github.com/ambroise-leclerc/MduX/actions/runs/34222463456) and
[sanitizer](https://github.com/ambroise-leclerc/MduX/actions/runs/34222463431) runs.
Reproduction and the behavioral reassessment are in the
[behavior matrix](parity/behavior-matrix.md#335-paired-adoption-results).

**Joint sign-off remains pending.** This removes the pin mismatch for the adoption branch and
supports the common syntax subset only. TrustSC's semantics/layout/safety adapters and shared
observation profiles remain unimplemented in the gate; unclaimed coverage is neither a pass nor
evidence of equivalent behavior. Neither side's coverage was lowered. #335 must not be closed as
full cross-implementation conformance on this evidence, and ADR-017's rendered-artifact migration
condition is not discharged by this syntax result.

### [#308](https://github.com/ambroise-leclerc/MduX/issues/308) — Interactive medical monitor and bounded input handling · planned

Deliver one interactive monitor whose platform events flow through a bounded governed model into the existing screen bindings and real Vulkan presentation, with a deterministic headless path using the same application logic.

**#315 and #316 landed the input contract and its model.** [ADR-018](adr/ADR-018-bounded-input-and-update-order.md)
(Accepted 2026-09-08) and the header-only `mdux.medui.input` module define the closed
pointer/key/text/focus event vocabulary, `maxInputEvents`, the fail-closed floor-toward-−∞
coordinate rule, the `PressLatch` arm/activate model over `resolvePress()`, the bounded
editing contract, the input→update→render order, and the resolved-and-traced-by-MduX /
executed-by-the-host critical-action boundary. #315 delivered the pure pieces; **#316** added
`EventQueue` (the caller-owned bounded ring, drop-newest on overflow) and `FieldEditor` (the
scalar-indexed editing state — insert/backspace/delete/caret-moves, both charset bounds and
`max_length`, no partial mutation, `handleKey`/`handleText` routing). Covered by
`InputContractTests` in `medui_spec` and a no-allocation proof in `input_noheap_spec`.
PAR-REQ-004–008 dispositions are ratified in ADR-018. #317 (the platform adapter) and #318 (the
assembled monitor) are now unblocked.

| Child | Deliverable | Prerequisites |
|---|---|---|
| [#315](https://github.com/ambroise-leclerc/MduX/issues/315) | Define bounded input events, application update order and action policy | [#312](https://github.com/ambroise-leclerc/MduX/issues/312) · **done** (ADR-018, `mdux.medui.input`) |
| [#316](https://github.com/ambroise-leclerc/MduX/issues/316) | Implement the bounded event queue and controlled text-editing model | [#315](https://github.com/ambroise-leclerc/MduX/issues/315) · **done** (`EventQueue`, `FieldEditor`) |
| [#317](https://github.com/ambroise-leclerc/MduX/issues/317) | Add an optional medical-screen presentation and input adapter | [#315](https://github.com/ambroise-leclerc/MduX/issues/315) |
| [#318](https://github.com/ambroise-leclerc/MduX/issues/318) | Deliver the interactive monitor with two approved locales | [#316](https://github.com/ambroise-leclerc/MduX/issues/316), [#317](https://github.com/ambroise-leclerc/MduX/issues/317) |

### [#309](https://github.com/ambroise-leclerc/MduX/issues/309) — Deterministic interaction scenarios and dynamic UI evidence · planned

Replay the actual application's bounded event/update path from compiled scenarios, then verify the resulting dynamic frames and emit complete, traceable evidence.

| Child | Deliverable | Prerequisites |
|---|---|---|
| [#319](https://github.com/ambroise-leclerc/MduX/issues/319) | Compile bounded interaction scenarios on the host | [#312](https://github.com/ambroise-leclerc/MduX/issues/312), [#315](https://github.com/ambroise-leclerc/MduX/issues/315) |
| [#320](https://github.com/ambroise-leclerc/MduX/issues/320) | Replay scenarios through the application's real input and update path | [#319](https://github.com/ambroise-leclerc/MduX/issues/319), [#316](https://github.com/ambroise-leclerc/MduX/issues/316) |
| [#321](https://github.com/ambroise-leclerc/MduX/issues/321) | Verify dynamic scenario captures and gate complete evidence in CI | [#320](https://github.com/ambroise-leclerc/MduX/issues/320), [#318](https://github.com/ambroise-leclerc/MduX/issues/318), [#313](https://github.com/ambroise-leclerc/MduX/issues/313) |

### [#310](https://github.com/ambroise-leclerc/MduX/issues/310) — Bounded streaming VulkanViewport rendering · planned

Add an optional Vulkan adapter path that consumes a caller-owned bounded stream and composes a concrete waterfall visualization inside the compiled viewport rectangle.

| Child | Deliverable | Prerequisites |
|---|---|---|
| [#322](https://github.com/ambroise-leclerc/MduX/issues/322) | Specify the streaming viewport data and composition contract | [#312](https://github.com/ambroise-leclerc/MduX/issues/312) |
| [#323](https://github.com/ambroise-leclerc/MduX/issues/323) | Render a bounded waterfall inside the compiled VulkanViewport | [#322](https://github.com/ambroise-leclerc/MduX/issues/322) |
| [#324](https://github.com/ambroise-leclerc/MduX/issues/324) | Exercise streaming viewport updates in the monitor and pixel tests | [#323](https://github.com/ambroise-leclerc/MduX/issues/323), [#318](https://github.com/ambroise-leclerc/MduX/issues/318) |

### [#311](https://github.com/ambroise-leclerc/MduX/issues/311) — MedUI authoring tools and Studio integration · planned

Expose a stable host-only editing/preview interface and reuse the TrustSC Studio frontend where practical through a MduX backend, ending in a tested proposal workflow.

| Child | Deliverable | Prerequisites |
|---|---|---|
| [#325](https://github.com/ambroise-leclerc/MduX/issues/325) | Define the MduX host editing API and round-trip source contract | [#312](https://github.com/ambroise-leclerc/MduX/issues/312) |
| [#326](https://github.com/ambroise-leclerc/MduX/issues/326) | Serve real MduX previews with explicit locale and dynamic fixture data | [#325](https://github.com/ambroise-leclerc/MduX/issues/325), [#316](https://github.com/ambroise-leclerc/MduX/issues/316), [#323](https://github.com/ambroise-leclerc/MduX/issues/323) |
| [#327](https://github.com/ambroise-leclerc/MduX/issues/327) | Integrate Studio editing and reviewable change proposals | [#326](https://github.com/ambroise-leclerc/MduX/issues/326) |

### Delivery and evidence rules

The issue bodies carry acceptance criteria, source references, impact classifications and precise
dependencies. Shared-contract changes also need the relevant Compliatory/MedUI decision and a
linked TrustSC adoption; only MduX issues were created by this planning update. Do not mark a
cross-implementation claim complete from MduX results alone.

Input, rendering and verification changes need prospective requirement/design decisions and
maintainer or domain-expert review. Preserve allocation bounds, explicit refusals, package
authentication and the existing static verification gate. New scenarios must enumerate all
required captures and locales, so a missing result cannot look like a successful run. The studio
is host-only and must use the compiler and real renderer; it does not reintroduce runtime HTML/CSS.

Each PR targets `develop` or names its predecessor. After that predecessor merges, rebase onto
current `develop`, review the final diff and wait for its post-merge integration checks before
merging dependent work. The detailed policy remains in [CONTRIBUTING.md](../CONTRIBUTING.md).

This roadmap change is **potentially safety-relevant planning/documentation**: it changes which
behavior and evidence will be developed, but changes no device code, compiled artifact or existing
verification result. The design children identify affected records before implementation rather
than inventing retrospective clinical requirements.

## Original programme — delivered

The release and epic records below retain the history of #7–#19. Their historical test counts and
release-time observations are not the current comparison above. All original epics are closed;
Phase 2 is a separate backlog, not a reopening of their completed acceptance criteria.

### Six delivered waves

The original programme delivered its dependencies through six waves. All six have shipped
(v0.2.0 through v0.8.0), with dependencies delivered before their consumers.
Wave 5 was #15, the largest epic of the programme, and it closed at 12/12. Wave 6 shipped in two
halves: #16 closed at 5/5 in v0.7.0, and #17 closed at 6/6 plus its #297 follow-on in v0.8.0. #19
spanned waves by design: S4–S6 shipped alongside #17, and PR #306 delivered its S7 guidance
follow-up before the epic closed.

```text
Wave 1 · shipped v0.2.0     #7 (done)   #11 (done)  #19 (S4–S6 done)
Wave 2 · shipped v0.3.0     #8 (done)   #9 (done)   #12 (done)
Wave 3 · shipped v0.4.0     #10 (done)  #13 (done)  #18 (done)
Wave 4 · shipped v0.5.0     #14 (done)
Wave 5 · shipped v0.6.0     #15 (done)
Wave 6 · shipped v0.7.0/v0.8.0  #16 (done, v0.7.0)  #17 (done, v0.8.0)
```

#### When v0.6.0 gets cut

**Cut, on 23 August 2026.** The convention above is one version per wave, Wave 5 was #15 alone, and
#201 closed it at 12/12: an authored `.medui` file reaches compared pixels through every stage. What
follows is the reasoning as it stood before the tag, kept because the next release faces the same
question.

This recommendation was written at 10/12 and said the two content gaps — no baked text package, a
runtime that draws one component kind — would close in #201. **They did not, and that expectation was
mine rather than the issue's.** #201's acceptance asked for an end-to-end screen exercising a
safety-critical node, and that is what it delivered; nothing in it promised the text half. So the
gaps outlive the wave and belong to its successors: the text package to whoever bakes one, the
components' own geometry to #17.

That does not argue for delaying the tag. What v0.6.0 ships is a complete *path* — authored file,
byte-compared artifact, `constexpr` C++, governed runtime, compared pixel — and the honest release
note says exactly that: the chain is built and the content is one rectangle. A version held back
until the content filled in would be waiting on #17, which is a wave of its own.

Two things worth settling before the tag rather than during it:

- **There is now a [CHANGELOG](../CHANGELOG.md), and still no release workflow.** The 0.6.0 entry is
  written from the merges it contains; the four earlier ones are summarised from this document's own
  wave record, and the file says so, because two of the four tags are not ancestors of `develop` and
  a reader checking them with `git log` would find that out the hard way.
- **The release ritual is inferable but not written down.** Checked rather than assumed, and an
  earlier revision of this bullet got both facts wrong: `v0.5.0` peels to exactly `origin/master`'s
  tip, so **tags land on `master`**, and `origin/master...origin/develop` is `1 32` — master carries
  one commit develop does not, develop carries 32. (The "668" this bullet used to claim came from a
  stale *local* `master`, 462 commits divergent from the real one, and was a total-history count
  mislabelled as an ahead count.)

  The procedure that was missing is now [`release-process.md`](release-process.md): a release
  branch off `develop`, the version moved in `CMakeLists.txt`, **every committed artifact re-baked**
  because each `report.json` records the `toolVersion` it was baked by, the artifact diff reviewed
  for anything that is not that field, then a merge to `master` and an annotated tag there. One
  non-linear boundary exists — `v0.2.0` is not an ancestor of `v0.3.0` — and it has a documented
  cause: #23 purged normative text from git history.

#### What v0.7.0 ships

Twenty commits separate this tag from `v0.6.0`'s back-merge, and unlike the previous inter-tag
window, **one of them closed an epic**. Two are not product changes and have no row below: the
back-merge itself, and #238, which recorded what cutting v0.6.0 taught the procedure.

| Merged | PR | Issue | What it changed |
|---|---|---|---|
| 23 Aug | #239, #240 | #235 | A text package is baked and committed, and the compiled screen carries a `t("STR-KEY")` measured against it. |
| 24 Aug | #241 | #222 | macOS Apple Silicon becomes a continuously verified target: Clang 21, libc++, MoltenVK, on every push. |
| 25 Aug | #243 | #242 | The governed runtime draws a `Label` — compiled screen plus text package reaches pixels. |
| 25 Aug | #245 | #244 | The screen records the digests of the text packages it was compiled against, and the runtime refuses any other. |
| 29 Aug | #247 | #246 | The Linux Clang leg runs on every pull request, and caught two defects three green legs had missed. |
| 29 Aug | #250 | #249 | A lint that fails a document naming a CI mechanism no workflow runs. |
| 1 Sep | #270 | #219 | `ClockFormat` and `SystemEvent` become closed sets, so a `Clock` can be measured rather than looked up. |
| 1 Sep | #271 | #18 | `mdux.ml` emits `constexpr` model packages, the treatment shaders and screens already had. |
| 2 Sep | #272 | #251 | ADR-014 fixes what rendered-truth verification checks, and what it cannot. |
| 2 Sep | #273 | #252 | `mdux.verify` — four governed checks over a CPU framebuffer. |
| 2 Sep | #277 | #253 | `mdux-verify-ui`, across every approved locale, refusing a narrower set. |
| 2 Sep | #278 | #254 | `verification.json` as the screen bundle's fourth byte-compared file. |
| 3 Sep | #279 | #255 | The CI gate on three render legs, the failure diff image, and ADR-014 decision 5. |

The first four are #17's content arriving ahead of its epic, and they are what made #16 buildable:
without a baked text package and a drawn `Label`, the two mandatory text obligations would have had
no ink to check. The last five are #16 itself, in order, each blocked on its predecessor.

> **#255 had to settle a question #16 left to #17.** The committed screen's two golden nodes were
> deferred by the runtime, so the gate the epic exists to add would have been red on the day it
> landed. ADR-014 decision 5 is the answer — a `NumericDisplay` and a `SignalTrace` paint the field
> they reserve, read off the golden sidecar rather than invented — and the three cheaper answers
> (delete the goldens, weaken their checks, verify a different screen) are the three the issue
> forbids by name. Recorded here because the same shape will recur: an epic's last child is where
> its unstated assumptions become someone's problem.

> **#244 merged with review findings addressed rather than deferred.** All eight raised against the
> first revision landed as fixes in the squashed commit, two of them solved better than proposed: the
> device path dropped JSON re-serialization entirely instead of hashing fields, and noncanonical
> package bytes became a *compile* error instead of a test for the divergence they caused.

## Original epic delivery record

The thirteen original parity epics are all closed. Their completed children and release context
are retained below; use the Phase 2 table for actionable work and blockers.

### Track A · Documentation, governance, regulatory

#### #7 — Copyright remediation · **Done v0.2.0**

The corpus rebuild has no point while the tree still ships reproduced normative text —
including a tracked 1.8 MB draft standard and a copyrighted book.

- #20 ADR: no reproduction of normative text
- #21 Inventory reproduced text
- #22 Remove from the working tree
- #23 Purge from git history
- #24 Delete `inputs/` and `libstd/`
- #25 `mdux-docs-lint` CI check

_Blocks #8, #9_

#### #8 — Clause-accurate corpus · **Done v0.3.0**

Five standards on real clause structure, each with a per-clause index and JSON Schemas.
Adds ISO 14971, IEC 62366-1 and IEC 81001-5-1 — the first of which is the standard most
relevant to a UI SDK.

- #26 Citation convention + `Justification` schema
- #27 Rewrite `docs/iec62304/`
- #28 Rewrite `docs/iso13485/`
- #29 New `docs/iso14971/`
- #30 New `docs/iec62366/`
- #31 New `docs/iec81001/`
- #32 Per-clause index per standard
- #33 JSON Schemas per standard

_Blocks #10_

#### #9 — Governance & the Software Development File · **Done v0.3.0**

Makes "traceable" a property of the repository rather than a claim in a README:
governance types that validate, a real SOUP register, and both SDF trees.

- #34 `mdux.governance` module
- #35 Traceability matrix export
- #36 `soup-register.toml`
- #37 SDF templates
- #38 SDF filled in for MduX itself
- #39 Scope-limits document

_Runs alongside #8_

#### #10 — Documentation architecture & README honesty · **Done v0.4.0**

The Implementation Status table marks five compliance frameworks "Completed" and shows
includes for four `.cppm` files that did not exist when this epic was opened. Two ADRs
were both numbered 002. The epic closes the track by re-baselining the ADR series,
authoring `docs/architecture.md` and `docs/getting-started.md` from the real build graph,
rewriting the README from built reality, aligning `AGENTS.md`, and retiring the
point-in-time documents. All nine children merged to `develop` in PR #154.

- #108 Re-baseline ADR numbering and supersede Catch2
- #109 `docs/architecture.md` from the current build graph
- #110 `docs/getting-started.md` using supported surfaces
- #111 Rewrite README implementation status from built reality
- #65 Land and align `AGENTS.md`
- #112 Retire superseded point-in-time documentation
- #113 Verify IEC 62366-1 corpus mappings against an authorized source
- #114 Verify IEC 81001-5-1 corpus mappings against an authorized source
- #115 Internal-link and retired-path CI lint

_All nine closed in PR #154. `#65` also closes the S1 child of #19_

---

### Track B · Load-bearing foundations

#### #11 — Foundations & trust-zone skeleton · **Done**

A new `MduXCore` that never receives Vulkan's include directories — so
`#include <vulkan/vulkan.h>` in governed code is rejected on every platform. Plus
the prerequisites that produce no demo and block everything.

The foundations shipped in Wave 1: `MduXCore` split, link-graph verification, the test
framework, presets, install/export, and `#48` (GCC 16 CI green; the Clang CI leg stays
disabled, an honesty scope pinned in ADR-007). The two enforcement gaps that postdate
v0.2.0 closed before Wave 5 opened, which closes the epic.

`#116` found the gap it was written for to be live rather than theoretical. ADR-005
asserted in the present tense a governed-source lint that had never been written, and
`src/text/Raster.cpp` — governed, shipped in v0.5.0 — contained `try` and three `catch`
clauses. The intended fix, `-fno-exceptions` on `MduXCore`, turned out to be unavailable:
GCC records the language dialect in every module BMI and CMake synthesises one shared
`std` target, so `import std` and `-fno-exceptions` are mutually exclusive. Enforcement
landed instead as `mdux-governed-lint` over the source and `governed.noThrow.symbolScan`
over the emitted objects; the rasteriser moved to the host-tools zone; and ADR-004 and
ADR-005 were rewritten to describe only mechanisms CI runs, including what they still
cannot claim.

- #40 ADR: trust zones in C++
- #41 ADR: error handling and exceptions
- #42 Delete dead code from `examples/`
- #43 `.gitignore` negations
- #44 A real test framework
- #45 Linux and Clang presets
- #46 Split out `MduXCore`
- #47 Restore install/export
- #48 Unblock the compilers
- #116 S10 — Enforce governed-zone source and exception policy
- #117 S11 — Stack-safe PR integration and post-merge policy

_All eleven closed. `#117`'s PR template and merge-ordering policy land ahead of Wave 5,
which is the most stacked epic of the programme_

> **Closure depended on two independent merges**, and this line stays so that a reader can check
> rather than assume: `#116` closed with PR #189 (the last of a three-PR stack behind #188 and
> #187), and `#117` closed with PR #186, which targeted `develop` on its own. All four merged on
> 11 August 2026, so the "Done" above is behind the tree rather than ahead of it — which is the
> only direction this status line is allowed to be wrong in.

#### #12 — Evidence kernel · **Done v0.3.0**

Host-only bakers produce committed artifacts; CI re-derives them and asserts byte-identity.
Canonical JSON encodes floats as bit patterns, because decimal float text is not
byte-identical across MSVC, glibc and libc++ — and this pipeline crosses all three.

- #49 ADR: evidence pipeline doctrine
- #50 SHA-256
- #51 Canonical JSON writer and strict reader
- #52 Bake-report types
- #53 `cmake/MduXBake.cmake`
- #54 Shared TOML and CLI
- #55 CI verify on both toolchain legs

_Blocks #13, #14, #18_

---

### Track C · `.medui`

#### #13 — Shader pipeline & renderer slice · **Done v0.4.0**

Where MduX draws its first pixel from library code. One 24-byte vertex, one triangle-list
pipeline, four modes; buffers sized once from a compiler-computed budget and never grown.

- S1 Shader schema and SPIR-V baker
- S2 Author and bake the UI shaders
- S3 The C++ emitter, with a header fallback
- S4 Migrate the triangle example
- S5 `DrawList`, `DrawCommand`, `DrawBudget`
- S6 Fixed-budget solid-rect recording
- S7 Offscreen target and readback
- S8 The project's first pixel test
- S9 Retire the HTML path

_Blocks #14, #16_

#### #14 — Font & text pipeline · **Done v0.5.0**

Static text bakes to positioned glyph runs per locale. Dynamic text gets a restricted
charset table — and the compiler rejects any format that could escape it, which turns
"no shaping on device" from a slogan into a compile error.

- S1 Text schema and baker — `mdux.text.schema`, `mdux-textbake`
- S2 Hand-parsed TrueType (`glyf` only) — `mdux.tools.truetype`, host-only
- S3 Rasteriser with coverage AA — `mdux.text.raster`, integer-only
- S4 Atlas packer and font baker — the first committed font package
- S5 Metrics and the tabular-figure assertion — `mdux.font.schema`
- S6 Coverage draw path and text pixel tests — `mdux.text.draw`, rendered under lavapipe

_Unblocks #15_

#### #15 — `.medui` compiler & build integration · **Done v0.6.0**

The schema module is imported by both the device runtime and the host compiler — one
definition, shared. The runtime never sees the parser, which lives in a host-only tool.
Rust shares types across the crate boundary; C++ can do better.

The front end is in the tree and conformance-tested against the shared MedUI spec
(`medui-conformance.toml`, capabilities `syntax`, `semantics`, `layout`, `safety` — the last claimed
with #196, which is what made the pinned `MEDUI-E070` case executable rather than skipped). ADR-011 and
ADR-012 were amended by #203 before any code depended on them: the compiled screen is
locale-free, so the per-locale text stays in the text package — which is why S6 measures every
approved locale and reserves the worst of them, rather than sizing a box to the locale its author
happened to read.

- #190 S1 ADRs: DSL boundary and generated artifacts · _closed_
- #191 S2 Diagnostics and the MDX-E code registry · _closed_
- #192 S3 Lexer, parser, AST and the fixture corpus · _closed_
- #193 S4 Theme tokens and locale-checked strings · _closed_
- #194 S5 Bounded layout and `Row` flattening · _closed (PR #212)_
- #195 S6 Text-budget validation against every approved locale · _closed_
- #196 S7 Golden references for safety-critical nodes · _closed_
- #197 S8 Canonical package and C++ emitters · _closed (PRs #220, #221)_
- #198 S9 CMake integration and the `mdux-meduic` host tool · _closed (PRs #225, #231)_
- #199 S10 Allocation-free screen runtime · _closed (PR #232)_
- #200 S11 `mdux-medui-check` · _closed (PR #233)_
- #201 S12 First end-to-end screen · _closed (PR #234)_

**The follow-up is delivered.** #219 closed `ClockFormat` and `SystemEvent` across the compiler
and schema before #258 and #261 consumed them. #218 had attempted a schema-only closure and was
withdrawn because that would have made the canonical type unable to represent accepted compiler
input. The repair landed the shared definitions before consumers. `charset` remains an authored
name, while #297 carries its resolved ranges to the device and enforces them there.

_Historical successors: #16 and #17, both now closed._

#### #16 — Rendered-truth verification · **Done v0.7.0**

Render offscreen, then check that critical content appears where the compiled screen says
it will, in the declared tint, in every approved locale — and emit that as evidence. Bounds
and colour checks are exercisable before a single glyph exists.

- #251 S1 ADR: automated UI verification · _closed_
- #252 S2 Bounds, ink containment, colour hash · _closed_
- #253 S3 The verify driver · _closed_
- #254 S4 Evidence report emission · _closed_
- #255 S5 CI across all locales · _closed_

**#255 closed the epic at 5/5, and it ships in v0.7.0.** It also had to settle something #16 left to #17: the committed
screen's two golden nodes were deferred by the runtime, so the gate this child exists to add would
have been red on the day it was added. ADR-014 decision 5 is the answer - a `NumericDisplay` and a
`SignalTrace` paint the field they reserve, in the token their own golden entry names, while the
reading inside it waited on #257 and #258 — both shipped since, in v0.8.0. The three cheaper answers (delete the goldens, weaken
their checks, verify a different screen) are the three #255 forbids by name.

Sequential: each child is blocked by its predecessor. Two things landed after the epic was written
that make it cheaper than it reads — #242 draws a `Label`, so ink containment has real ink to check
rather than waiting on #17, and #244 makes "every approved locale" a property of the screen's own
manifest rather than a caller-supplied list. **#252's bounds and colour-hash checks wait on no
further content** — the epic's own note calls them fully exercisable at the solid-rect slice, which
shipped in v0.6.0 — but they still follow #251, which fixes the derive-don't-trust rule they
implement. Content is not the constraint; the governing decision is.

#### #17 — Content components · **Done v0.8.0**

The rest of the component dictionary. Two deliberate scope cuts: QOI rather than PNG in v1,
and no IME — input-method editing is a platform concern that does not belong inside a
governed renderer. Both cuts held: #256 baked QOI on the host only, and #260 gave a `TextInput`
display and caret and nothing else.

- #256 S1 Image baker and the `Image` component — **shipped**; a host-only QOI decoder, RGBA8 the device never parses
- #257 S2 `SignalTrace` — **shipped**; a waveform expanded on device from a caller-owned ring into the pre-sized vertex budget
- #258 S3 `NumericDisplay` and `Clock` — **shipped**; live values through a pattern whose slot positions are compile-time constants
- #259 S4 `StatusIndicator` — **shipped**; the ECG demonstrator binds its classifier's class to one
- #260 S5 `TextInput` (display and caret only) — **shipped**; a fixed-pitch grid, measured at compile time
  - #297 — **shipped**; the follow-on that made `charset:` a bound on what the *device* displays and
    not only a claim about the source, by carrying the resolved code-point ranges in the compiled node
- #261 S6 Buttons with requirement binding — **shipped**; a face, a closed action, and the requirement it is traced to

Largely independent of one another, unlike #16's, which is why they landed in that order rather than
in a forced one.

**#219 ordered two of them, and was a prerequisite rather than a nicety.** It closed `ClockFormat` and
`SystemEvent`. An open format name cannot be *measured*, only looked up, so #258's `Clock` would be
built against the product-supplied table #195 needs today and then have it removed; and a screen
that can name any system event can name one nothing implements, which is worst discovered on the
press of the critical button #261 builds. Both held: #258's clock box is measured rather than looked
up, and #261's `resolvePress()` refuses an action outside the closed set instead of reporting a
no-op nothing performs.

**#261 closed the epic at 6/6, and #297 — the one gap its last child exposed — shipped alongside it
in v0.8.0.** Every component the dictionary names now draws: `Panel`, `Label`, `Clock`, `Image`,
`VulkanViewport`, `SignalTrace`, `Button`, `CriticalButton`, `NumericDisplay`, `StatusIndicator` and
`TextInput` — eleven of eleven. #16's rendered-truth verification, which shipped a wave earlier
checking two components that painted only the field they reserved, now has a real reading to check
in both of them.

#### What v0.8.0 ships

Nineteen commits separate this tag from `v0.7.0`'s back-merge, four of them a governance mechanism
(#284) with no product row below, alongside the version bump and its re-baked artifacts.

| Merged | PR | Issue | What it changed |
|---|---|---|---|
| 4 Sep | #289 | #280 | `ccache` is BMI-aware for Clang modules, closing a local-build corruption path. |
| 5 Sep | #290 | #281 | An approved locale tag gets a grammar — `en/US` and worse no longer pass. |
| 5 Sep | #291 | #282 | The rendered-truth gate is asserted as a named step on the Windows leg too. |
| 5 Sep | #292 | #257 | `SignalTrace` draws its waveform, expanded on device from a caller-owned ring. |
| 5 Sep | #293 | #256 | `Image`, from a host-only QOI decoder — the first new component since #16 shipped. |
| 5 Sep | #294 | #258 | `NumericDisplay` and `Clock` draw live values through a compile-time-measured pattern. |
| 5 Sep | #295 | #259 | `StatusIndicator` draws its state; a bound indicator with no per-state tint is refused. |
| 5 Sep | #296 | #260 | `TextInput` draws its value and caret on a fixed-pitch grid. |
| 6 Sep | #298 | #261 | `Button` and `CriticalButton` draw a face and resolve a press — #17's last child. |
| 6 Sep | #300 | #263 | A machine-readable `.medui` grammar, emitted from the compiler's own tables. |
| 6 Sep | #301 | #264 | A committed JSON Schema for every recipe kind, checked against every report. |
| 6 Sep | #302 | #265 | `--dump-ir`, and a generated host-tool manifest — #19's last two children. |
| 6 Sep | #303 | #297 | A `TextInput`'s `charset:` bounds the device, not only the compiler. |

The first nine are #17 in dependency order — mostly independent, with #219 ordering #258 and #261 as
noted above. The last four are #19's remaining children plus #297, landing alongside rather than
blocking on #17: neither epic depends on the other.

---

### Track D · "LM" — embedded ML and agent tooling

#### #18 — Zero-SOUP ML inference · **Done v0.4.0**

Embed learned models without linking any foreign inference stack. Weights are data:
swapping demonstrator weights for clinically-qualified ones is a re-bake, with zero
application source change. Runs in parallel with all of Track C.

- #56 ADR: zero-SOUP ML pipeline
- #57 `mdux.ml.schema`
- #58 `mdux.ml.kernels`
- #59 Determinism enforcement
- #60 Safetensors import and validation
- #61 Golden generation and the model baker
- #62 `Classifier1D`, fail-closed
- #63 No heap in `predict`, verified three ways
- #64 ECG demonstrator and weight-swap test
- #153 Follow-up: `constexpr` package emitter · **closed v0.7.0**

_The nine children closed · 360/360 on `develop`. #153 was a follow-up rather than a tenth child: the
ML package used to be the one committed artifact a device build still parsed at startup, and #271
closed it — `mdux.ml` emits `constexpr` model packages, the same treatment shaders and screens
already had. It did not reopen #18 and did not block a wave._

#### #19 — Agent & LLM tooling parity · **Closed 6 September 2026**

A diagnostic envelope of file, line, code, severity and fix hint is what lets an agent
fix a `.medui` error without parsing prose. With a published grammar, it is the difference
between guessing at the DSL and being handed its contract.

AGENTS.md is aligned with the v0.4.0+ architecture, the repository skills are present, the stable
JSON diagnostic envelope is landed across the tools, the `.medui` contract is published as
machine-readable JSON the compiler emits from its own tables, every recipe kind has a committed JSON
Schema checked against its own reports, and the compiler's resolved IR is dumpable alongside a
generated host-tool manifest. PR #306 delivered the S7 guidance follow-up and closed #304;
#19 itself is now closed. The post-merge build and lint workflows passed at `e68e04b`.

- #65 Land and align `AGENTS.md` · _closed_
- #66 Repository skills · _closed_
- #118 Stable JSON diagnostic envelope across all tools · _closed_
- #263 S4 Machine-readable `.medui` grammar and `--explain` — **shipped**; `docs/medui/grammar.json`, emitted from the compiler's own tables
- #264 S5 JSON Schemas for every recipe kind — **shipped**; `docs/recipes/*.schema.json`, checked against every committed report
- #265 S6 `--dump-ir` JSON and a generated tool manifest — **shipped**; the resolved IR, and a manifest generated from the build's own registration
- #304 S7 Refresh agent instructions and reject empty documented test selections — **shipped**;
  a follow-up to #65/#66 rather than a seventh grammar/schema/IR feature. An assessment of
  `develop` found agent-facing instructions that disagreed with the implementation - a
  `ctest -R <name>` example naming a suite registration `mdux_discover_tests()` had already
  replaced with `<target>::<case>` entries, a "five artifacts" count where eight now exist, and two
  references to issues since closed. Fixed, and closed mechanically rather than left to the next
  audit to rediscover by hand: `tools/docs-lint/check_documented_test_selectors.py` asks a built
  CTest configuration whether every documented `-R` example still matches something, wired into
  the GCC 16 build leg since the question needs a build to ask.

_S1–S7 closed; #19 closed._

---

## Read before starting

### Three things worth flagging

#### The copyright problem is in history — resolved

`#23` is closed. The reproduced normative text — a tracked 1.8 MB draft standard, both
transcriptions, and a copyrighted book — was purged from the working tree *and* from git
history, and `mdux-docs-lint` now runs in CI to keep it out. This removed the residual
clone-time exposure that a HEAD-only deletion leaves behind.

#### Two claims C++ makes better

Running the evidence tests on MSVC, GCC 16 and Clang 21 with libc++ — the last on both Apple
Silicon (#222) and Linux (#246) — in the same pull request proves byte-identity across three
independent toolchains, standard libraries and floating-point code generators, on three operating
systems. TrustSC's assessed CI exercises one Rust toolchain on Linux; it does not establish
the same cross-toolchain/platform byte-identity evidence.

The third leg arrived by a route this document did not predict, and finding that out corrected a
false claim. ADR-007 decision 6 read "and Clang, now that issue #48 re-enabled that leg" — #48 did
not: `clang-build.yml` has never carried a `push` or `pull_request` trigger. The claim was true in
substance and wrong about its own evidence, which is the defect class #116 found in ADR-005.

#246 then ran the Linux leg, and it is now a fourth lane on every push. `import std` was never the
blocker there — libc++-21 ships its manifest and resolves fine. Two real defects were, and neither
was reachable on any other leg: `ShaderPackage::toJson()` exceeded the 4096-byte
`-Wframe-larger-than` guard #63 set so that "no heap" could not quietly become "enormous stack"
(4120 bytes, on x86-64, where the macOS lane is arm64 and Clang reuses sibling-scope slots less than
GCC), and `InstallTreeConsumer` never forwarded compiler flags to its nested configure, so the
consumer built against the compiler's default standard library rather than the one MduX was built
with — invisible on macOS, where libc++ *is* that default. The function was split into per-section
helpers and the forwarding completed; the guard was left where it was.

The fourth lane is not a fourth toolchain, and ADR-007 decision 6 now says what it does buy: the
same compiler and standard library as the macOS lane, on the same OS as the GCC lane. That separates
"a different toolchain produced identical bytes" from "a different *platform* produced identical
bytes" — two claims the doctrine had been making as one.

And "the host baker uses the same ML kernels as the device runtime" stops being a
discipline: it is one governed module imported by both. That removes duplicated kernel
implementations; golden vectors still check the actual compiled behavior on each supported
configuration.

#### One claim it cannot make

`#![forbid(unsafe_code)]`'s audit property is not reproducible in C++. The substitute —
denying governed targets the platform headers, an enforced static-analysis profile, a grep
lint — is real, but it is narrower. The wording is fixed in #40 and #38:

> Governed modules are compiled without access to platform, graphics, or OS headers, are
> checked by an enforced static-analysis profile, and are covered by determinism tests —
> not that they cannot contain undefined behaviour.

---

_Original programme: 13 closed epics and six shipped waves. Phase 2: five planned epics and
sixteen child issues, linked above. Status assessed 6 September 2026._
