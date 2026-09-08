# Prospective sibling-contract requirements

These are **engineering requirements prepared by #312** on 7 September 2026. PAR-REQ-001/002/003
carry ratified dispositions (see [Review disposition](#review-disposition) and
[ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md), 8 September 2026); PAR-REQ-004–008
carry ratified dispositions from [ADR-018](../adr/ADR-018-bounded-input-and-update-order.md)
(8 September 2026, #315); PAR-REQ-009–010 remain **proposed, awaiting maintainer review**. None of
them describes implemented new behavior,
accepted MedUI decisions, approved product requirements or a device risk assessment. Their stable
local IDs identify this design work; they are not fabricated entries in a released
ComplianceProgram. A review disposition must be recorded before the affected implementation
starts. No clinical intent is inferred from an example screen, a `requirement` string, or the name
TriggerHalt.

Impact: **potentially safety-relevant**. This change defines future interaction and evidence
contracts but changes no runtime behavior. Affected designs are ADR-011/012 (compile/artifact
boundary), ADR-014 (rendered checks), ADR-010 (bounded text), and accepted
[ADR-015](../adr/ADR-015-versioned-sibling-observations.md). Relevant items are `mdux.medui.screen`,
`.field`, `.reading`, `.trace`, `mdux.verify`, the host compiler/verifier and the future application
adapter. Existing requirements and evidence remain in force.

## Requirement and verification records

Rows PAR-REQ-009 and PAR-REQ-010 have status **Proposed / unreviewed**. PAR-REQ-001/002/003 were
the delivery target of [#314](https://github.com/ambroise-leclerc/MduX/issues/314); their
engineering half is gated in CI, and [ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md)
records their dispositions — PAR-REQ-001 **Accepted**, PAR-REQ-002 **Accepted with amendment**,
PAR-REQ-003 **Accepted with a recorded subset limitation** — **ratified by maintainer instruction
on 8 September 2026** under #312. PAR-REQ-004 through PAR-REQ-008 are the input requirements;
[ADR-018](../adr/ADR-018-bounded-input-and-update-order.md) records their dispositions, **ratified
on 8 September 2026** under #315, each tied to an ADR-018 clause and to the `InputContractTests`
scenarios in `medui_spec` that exercise the pure pieces. The verifier-area domain review for
PAR-REQ-002/003, the critical-action host policy for PAR-REQ-006, and the acceptance tests listed
for PAR-REQ-009/010 all remain open.

| ID | Required observable if adopted | Engineering failure addressed | Verification and delivery |
|---|---|---|---|
| PAR-REQ-001 | A comparison names both implementation SHAs, exact contract SHA, phase/profile and position precision. Unknown/missing capability cannot imply support. | False parity from unequal input contracts. | #314: reject mismatched/unsupported claims; check positive cases for every declared phase. ADR-015 D1/D4. **ADR-017 disposition — Accepted (ratified 2026-09-08):** `conformance_spec` fails an unsupported profile claim and a malformed manifest, exercises every declared phase's pinned positive cases, and checks `positions = "full"` in both directions. |
| PAR-REQ-002 | Extent equality, containment, tint composition and raw RGBA hash have distinct versioned identities with defined applicability, empty policy, ROI and arithmetic. Migration preserves old required checks. | A weaker predicate silently replaces evidence. | #313: empty/inset/exact/overflow rectangles; absent/wrong tint, impossible shared coverage, composite-rounding boundaries; matching/missing/mismatched baselines. D2. **ADR-017 disposition — Accepted with amendment (ratified 2026-09-08):** local half [ADR-016](../adr/ADR-016-locally-versioned-observation-profiles.md) (five `mdux.local/*` profiles — `extent-equality`, `tint-composition`, `ink-containment`, `ink-coverage`, `raw-image-digest` — the last exercised only by fixtures); shared half #314 Stage B (`MEDUI-PROFILE-RENDERED` + 33 vectors against one shared arithmetic). Amendment: `mdux.local/tint-composition` stays v1 (visibility-only move); the canonical ids/schema/corpus exist at the pin and the derived envelope already emits them, so the remaining migration is re-baking the byte-compared `verification.json` onto the canonical ids — gated on a final 0.3.0 release, the #335 cross-implementation pass and a reviewed re-bake (`mdux.local/ink-coverage` has no RENDERED equivalent and stays local); R04/`rgba8-sha256` exercised as arithmetic only, no committed baseline. |
| PAR-REQ-003 | Required observations are derived independently from the pinned screen/profile; missing, duplicate, unknown, unsupported or not-run required rows prevent a successful gate. Keys include all scope/provenance fields in D3. | Omitted checks or locale/frame collisions produce a false pass. | #314/#321: remove/duplicate/substitute a row; swap locale, asset, scenario or capture identity; verify rejection. D3. **Stage C** gates the shared-corpus side: `medui-conformance.toml` claims `MEDUI-PROFILE-EVIDENCE`, and `conformance_spec` runs MedUI's 31 `aggregate-evidence` vectors (E01–E03 — identity match, one row per obligation, outcome aggregation; `not-run` on empty) and the 29 `evidence` contract documents, plus negative fixtures. **Stage C-emitter** closes the "derived independently" clause for MduX's side: `mdux-verify-ui --medui-evidence-out` derives the `MEDUI-PROFILE-RENDERED` E01 envelope from a real verify run (obligations synthesized from the screen/checks, not read back from a report), validated against `evidence.schema.json` and shown to aggregate to `pass` on a GPU leg. It is the RENDERED subset (`LocalizedTextPresence` is implementation-local) and is derived/uncommitted (ADR-014 D4). **ADR-017 disposition — Accepted (ratified 2026-09-08)** for the derivation and rejection clauses (Stage C's 31 vectors + fail-closed schema validation + malformed-identity rejection; Stage C-emitter's independently-derived, fail-closed envelope), with a **recorded subset limitation**: the derived envelope excludes and counts `LocalizedTextPresence`, and the cross-implementation half is #335. |
| PAR-REQ-004 | Normalize coordinates once with a specified rounding rule. Hit policy: half-open rectangles, reverse paint order, all-node occlusion; press arms a target and release activates only the same eligible target. Cancel on focus loss/removal/overflow. | Wrong or stale target activates. | #315/#316/#317: exact edges, overlap with a non-control, outside-release, changed target, cancellation. D5. **ADR-018 disposition — Accepted (ratified 2026-09-08):** clause 3 fixes the rule as **floor toward −∞** (`normalizeSurfacePoint`, tested at the sign boundary — a truncating cast is the wrong-target bug — and fail-closed on an oversized scale or a result outside `core::Px` rather than a wrapped coordinate); clause 4 is `PressLatch` (arm/activate-same-target/disarm-on-cancel, tested). Occlusion stays `resolvePress()`'s existing all-node reverse-order rule. |
| PAR-REQ-005 | Events have bounded caller-specified capacity and stable order. Overflow: drop newest, saturating drop count, cancel pending activation. An update consumes one accepted batch, updates state, binds one snapshot, renders then captures. | Lost release triggers an action; replay/capture observes inconsistent state. | #315/#316/#320: zero/full capacities, counter saturation, dropped release, multiple events, recorded order and identical replay snapshot. D5. **ADR-018 disposition — Accepted with amendment (ratified 2026-09-08):** clause 1 fixes the closed event vocabulary and clause 2 the overflow policy (drop newest, saturating `droppedCount`, cancel the pending arm) and `maxInputEvents`. Amendment: the `EventQueue` ring body and the batch-consuming update loop (clause 6) are a **documented contract** here, implemented and tested by #316/#320. |
| PAR-REQ-006 | Critical action resolution returns a closed action and requirement identity; ordinary button source is distinct. The host supplies execution/audit policy. Unknown/untraced critical actions fail explicitly. | Example action semantics become an unreviewed device control. | #315/#318: NoOp, TriggerHalt, invalid action, missing trace and ordinary button; host-policy review before any real-device effect. D5. **ADR-018 disposition — Accepted with a recorded limitation (ratified 2026-09-08):** clause 7 — MduX resolves and traces (`ActionTrace{nodeId, requirement, event, sequence}`), the closed `SystemEvent` set and the fail-closed `UnimplementedEvent` / `UntracedCriticalControl` refusals already exist and are tested; `NoOp` stays distinct from `TriggerHalt`. **Limitation: the host executes the action, and the execution / audit / orderly-stop policy stays open for domain review.** |
| PAR-REQ-007 | Editing respects bounded Unicode scalar positions, declared repertoire and length. Invalid encoding/glyphs/oversize edits fail without partial mutation; no on-device shaping or unbounded storage. | Corrupted/truncated text or an unbounded input path. | #316: multibyte caret/delete boundaries, disallowed glyph, full capacity, paste/repeat/focus policy. Retain ADR-010 constraints. D5. **ADR-018 disposition — Accepted (ratified 2026-09-08):** clause 5 — scalar-boundary caret/edits over `[0, length]`, the `EditOp` vocabulary, and `editWouldBeAccepted`, which applies the **same two charset bounds `mdux.medui.field` does** (the font package's `restrictedCharset` then the node's narrowed set, via the reused `admits()`) plus `max_length`, no mutation, tested. The `applyEdit` mutation over caller storage lands in #316 against this contract. |
| PAR-REQ-008 | Normalize binding by screen/node and declared source, with typed limits and a snapshot ID. Replay injects time. State order, number format, trace order and viewport row/bin semantics are explicit. | A capture shows data from a different frame or source. | #319/#320/#322: reordered/unknown binding, range/format boundaries, fixed time, ring wrap and overflow. D5. **ADR-018 disposition — Accepted with amendment (ratified 2026-09-08):** clause 6 fixes the input → update → bind → render → capture order (state bound is state after the batch, so a capture never shows unresolved input and a same-batch same-time replay is identical). Amendment: the loop and its replay are #316/#320; the viewport row/bin semantics half stays with [#322](https://github.com/ambroise-leclerc/MduX/issues/322). |
| PAR-REQ-009 | Exact pixel comparison requires a declared presentation/backend profile and theme/font/image digests. Logical equivalence alone makes no exact-pixel claim. | Different valid renderings are mistaken for equivalence or regressions. | #313/#321/#324/#326: mismatched profile/assets rejected; focused/pressed/neutral state and dynamic content captures under accepted profiles. D2/D3/D5. |
| PAR-REQ-010 | Editing/source round-trip preserves logical field values, order where observable, safety annotations and trace IDs. Preview uses the same accepted interaction and data profile as replay. | Authoring silently removes an obligation or previews different behavior. | #325/#326/#327: parse/edit/emit/reparse with unknown/invalid values and traced nodes; compare normalized IR, goldens and diagnostics. D1/D5. |

The failure descriptions above are prospective software concerns, **not assigned clinical
hazards, severity estimates or approved risk controls**. No device-level requirement/risk-control
entry exists for these new interaction behaviors in this change. The integrating manufacturer
and domain reviewer must supply that context before a real-device action policy is implemented.

## Decision gates by downstream issue

Local D1–D5 are defined in ADR-015. Accepted shared decisions remain
[MEDUI-DEC-001–006](https://github.com/Compliatory/MedUI/tree/265df1925a672bd556f69123e287215b45cfd210/decisions).
MedUI #15/#16 now have accepted architectural decisions MEDUI-DEC-007/008; this does not
allocate profile identifiers or establish consumer capabilities.

| Work | Decisions needed before dependent behavior lands |
|---|---|
| #313 verifier semantics | D2/D3/D4 (accepted); [MedUI #15](https://github.com/Compliatory/MedUI/issues/15), reviewed PAR-REQ-002/003/009. **Local half delivered** by [ADR-016](../adr/ADR-016-locally-versioned-observation-profiles.md): implementation-local observation-profile identities for the four rendered checks and the RGBA8 SHA-256 predicate, per-outcome in `verification.json`, no committed baseline. The shared schema/corpus half and the PAR-REQ dispositions remain open. |
| #314 common corpus gate | D1–D4 and #313; MedUI #15 plus diagnostic/safety cases from existing #2/#3/#4/#8. Only claim phases with complete applicable passing cases. **Stage A** re-pinned to `v0.3.0-rc.1` and adopted the 27-case compiler corpus. **Stage B** delivers the shared-corpus half of PAR-REQ-002: `medui-conformance.toml` claims `MEDUI-PROFILE-RENDERED`, and `conformance_spec` runs MedUI's 33 `rendered-check` vectors (R01–R04) against `mdux.verify`'s own exported arithmetic plus the 10 `consumer-manifest` contract cases. **Stage C** does the same for PAR-REQ-003: `MEDUI-PROFILE-EVIDENCE` claimed, 31 `aggregate-evidence` vectors (E01–E03) and 29 `evidence` contract documents gated, all contract documents now validated by the ported `mdux.tools.schema`. **Stage C-emitter** adds `mdux-verify-ui --medui-evidence-out`: MduX's own derived, uncommitted `MEDUI-PROFILE-RENDERED` E01 envelope from a real run, schema-valid and aggregating to `pass` on a GPU leg. **Stage D** ([ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md), Accepted 2026-09-08) records the [per-capability results table](../roadmap.md#314-per-capability-conformance-results) and the ratified PAR-REQ-001/002/003 dispositions. The TrustSC-side re-pin is [#335](https://github.com/ambroise-leclerc/MduX/issues/335). |
| #315 input design | **Delivered** by [ADR-018](../adr/ADR-018-bounded-input-and-update-order.md) (Accepted 2026-09-08) and the `mdux.medui.input` module: the closed event vocabulary, `maxInputEvents`, floor-toward-−∞ coordinate normalization, the `PressLatch` arm/activate model, the bounded `EditOp` / `editWouldBeAccepted` editing contract, the input→update→render order, and the host-executes-`TriggerHalt` boundary. PAR-REQ-004–008 dispositions ratified. Open: the critical-action host/audit policy (PAR-REQ-006, domain review). |
| #316 queue/editing; #317 presentation adapter | Accepted #315 requirements (ADR-018); D5; PAR-REQ-004–007. #316 implements the `EventQueue` ring and `applyEdit` against the ADR-018 types; #317 the platform adapter and coordinate-scale source. |
| #318 interactive monitor | #316/#317 plus reviewed host action policy; PAR-REQ-006/008/009. Example IDs do not approve clinical behavior. |
| #319 scenario compiler | D3/D5, MedUI #16 and #315 event/update contract; PAR-REQ-005/008. |
| #320 replay | #319/#316 and accepted update ordering; PAR-REQ-005/007/008. |
| #321 dynamic evidence | D2/D3, MedUI #15, #313/#318/#320; PAR-REQ-002/003/008/009. |
| #322 viewport contract | D5, MedUI #16; PAR-REQ-008/009. Resolve dimensions, value range/clamping, row/bin order, overflow and composition before #323. |
| #323 waterfall; #324 integrated pixels | Accepted #322, then #318 for integration; PAR-REQ-008/009 and D2/D3 capture profile. |
| #325 editing API | D1/D5 and MedUI #16; PAR-REQ-010. Resolve lossless versus canonical formatting explicitly without losing semantic fields. |
| #326 previews; #327 Studio | #325/#316/#323, then #326; PAR-REQ-009/010 and D3 capture identity. |

Local design exploration can proceed while upstream discussion is open, but must label any
extension as implementation-local. It cannot claim an accepted shared profile or silently adopt
changed verification semantics. Closing #312's documentation work does not accept either
upstream proposal or remove these downstream gates.

## Review disposition

On 7 September 2026, [AM-L approved the PR](https://github.com/ambroise-leclerc/MduX/pull/329#pullrequestreview-5130548324).
The [maintainer's detailed review](https://github.com/ambroise-leclerc/MduX/pull/329#pullrequestreview-5130673890)
verified the matrix against the pinned sources and approved its substance, while explicitly
leaving formal acceptance of the prospective requirements pending. These reviews do not assign
accepted/amended/deferred dispositions to individual requirements or accept the upstream profiles;
individual requirement dispositions remained pending at that review.

Subsequently on the same date, Ambroise Leclerc explicitly accepted ADR-015's D1–D5 architectural
direction. This acceptance does not assign dispositions to the individual PAR-REQ records.
The upstream decisions MEDUI-DEC-007/008 were also explicitly accepted by the maintainer;
their profile/schema/corpus delivery and consumer adoption gates remain in force.

On 8 September 2026, Ambroise Leclerc ratified [ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md)
and its individual dispositions by explicit instruction under
[#312](https://github.com/ambroise-leclerc/MduX/issues/312): **PAR-REQ-001 Accepted**,
**PAR-REQ-002 Accepted with amendment**, **PAR-REQ-003 Accepted with a recorded subset
limitation**, each tied to the `conformance_spec` scenarios that discharge it. This is the
maintainer's engineering acceptance; the verifier-area **domain review** for PAR-REQ-002/003, the
rendered-artifact migration in ADR-017 §3, and PAR-REQ-004–010 all remain open. It assigns no
clinical hazard, severity or risk control, and it does not accept the upstream profiles or close
the cross-implementation gap ([#335](https://github.com/ambroise-leclerc/MduX/issues/335)).

On 8 September 2026, Ambroise Leclerc accepted
[ADR-018](../adr/ADR-018-bounded-input-and-update-order.md) and the **PAR-REQ-004–008**
dispositions it supports, by explicit instruction under
[#315](https://github.com/ambroise-leclerc/MduX/issues/315): **PAR-REQ-004 Accepted**,
**PAR-REQ-005 Accepted with amendment**, **PAR-REQ-006 Accepted with a recorded limitation**,
**PAR-REQ-007 Accepted**, **PAR-REQ-008 Accepted with amendment** — each tied to an ADR-018
clause and to the `InputContractTests` scenarios in `medui_spec`. This is the maintainer's
engineering acceptance of the input contract; the **critical-action host execution / audit /
orderly-stop policy stays open for domain review** (PAR-REQ-006), the `EventQueue` and `applyEdit`
implementations are #316, the platform adapter is #317, and the update loop and its replay are
#316/#320. It assigns no clinical hazard, severity or risk control, and PAR-REQ-009/010 keep
their `Proposed / unreviewed` status.

| Record | Requested reviewer | State |
|---|---|---|
| ADR-015 D1–D5 | MduX maintainer | Accepted by Ambroise Leclerc, 2026-09-07 |
| ADR-016 (local observation profiles) | MduX maintainer; domain reviewer (safety-relevant verifier area) | Proposed 2026-09-07 (#313). The four required rendered checks (`Bounds`, `ColorHash`, `InkContainment`, `LocalizedTextPresence`) keep their exact semantics; each gains an implementation-local `ObservationProfile` id recorded per outcome. `mdux.verify::rawImageDigest()` is an exported, fixture-tested predicate with a defined `NoBaseline` outcome that never discharges an obligation — it is **not** a `CvCheck`, has **no production caller**, and is **outside driver obligation enumeration**. Production-driver behaviour is unchanged; the committed `verification.json` differs only by the additive `observationProfile` member. |
| [ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md) (parity status + PAR-REQ dispositions) | MduX maintainer; domain reviewer (verifier area, PAR-REQ-002/003) | **Accepted by Ambroise Leclerc, 2026-09-08** (ratified under #312; drafted for #314d). Records the [per-capability results table](../roadmap.md#314-per-capability-conformance-results) and the ratified dispositions: PAR-REQ-001 Accepted, PAR-REQ-002 Accepted with amendment, PAR-REQ-003 Accepted with a recorded subset limitation. The verifier-area domain review for PAR-REQ-002/003 and the rendered-artifact migration in §3 remain open. |
| [ADR-018](../adr/ADR-018-bounded-input-and-update-order.md) (bounded input + update order + action policy) | MduX maintainer; domain reviewer (critical-action host policy, PAR-REQ-006) | **Accepted by Ambroise Leclerc, 2026-09-08** (#315). Defines `mdux.medui.input` — the closed event vocabulary, `maxInputEvents`, floor coordinate normalization, `PressLatch`, the `EditOp` / `editWouldBeAccepted` editing contract, the update order, and `ActionTrace`. Ratifies PAR-REQ-004 Accepted, PAR-REQ-005 Accepted with amendment, PAR-REQ-006 Accepted with a recorded limitation, PAR-REQ-007 Accepted, PAR-REQ-008 Accepted with amendment. The critical-action host execution / audit policy stays for domain review. |
| PAR-REQ-001–010 | MduX maintainer; domain reviewer for any device action/risk-control interpretation | PAR-REQ-001/002/003: engineering half gated in CI by #314; **dispositions ratified 2026-09-08** via ADR-017. #313 delivered the **local** half of PAR-REQ-002; #314 Stage B the **shared-corpus** half of PAR-REQ-002, Stage C the shared-corpus half of PAR-REQ-003, Stage C-emitter MduX's own derived envelope. **PAR-REQ-004–008: dispositions ratified 2026-09-08** via ADR-018, tied to `mdux.medui.input` + `InputContractTests` (PAR-REQ-006's host policy stays for domain review; the `EventQueue`/`applyEdit`/loop are #316/#320). PAR-REQ-009–010 remain Proposed / unreviewed. The cross-implementation half of the verifier requirements is [#335](https://github.com/ambroise-leclerc/MduX/issues/335). |
| Shared rendered profiles | MedUI #15 participants/maintainer | MEDUI-DEC-007 accepted, 2026-09-07; profile delivery pending |
| Shared interaction/presentation profiles | MedUI #16 participants/maintainer | MEDUI-DEC-008 accepted, 2026-09-07; profile delivery pending |

PAR-REQ-001/002/003 dispositions are ratified above and in
[ADR-017](../adr/ADR-017-sibling-conformance-gate-status.md); PAR-REQ-004–008 in
[ADR-018](../adr/ADR-018-bounded-input-and-update-order.md). PAR-REQ-009–010 stay
`Proposed / unreviewed` until the streaming and authoring work (#322–#327) reviews them. Upstream
architectural acceptance is recorded in ADR-015; consumer adoption still requires the applicable
schema and corpus revision. [Executed evidence](behavior-matrix.md#executed-observations-and-reproduction)
supports the current-state comparison only; it does not discharge these future requirements.
