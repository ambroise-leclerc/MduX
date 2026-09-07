# Prospective sibling-contract requirements

These are **proposed engineering requirements awaiting maintainer review**, prepared by #312 on
7 September 2026. They do not describe implemented new behavior, accepted MedUI decisions,
approved product requirements or a device risk assessment. Their stable local IDs identify this
design work; they are not fabricated entries in a released ComplianceProgram. Review disposition
must be recorded before the affected implementation starts. No clinical intent is inferred from
an example screen, a `requirement` string, or the name TriggerHalt.

Impact: **potentially safety-relevant**. This change defines future interaction and evidence
contracts but changes no runtime behavior. Affected designs are ADR-011/012 (compile/artifact
boundary), ADR-014 (rendered checks), ADR-010 (bounded text), and accepted
[ADR-015](../adr/ADR-015-versioned-sibling-observations.md). Relevant items are `mdux.medui.screen`,
`.field`, `.reading`, `.trace`, `mdux.verify`, the host compiler/verifier and the future application
adapter. Existing requirements and evidence remain in force.

## Requirement and verification records

Every row below has status **Proposed / unreviewed**. Acceptance tests listed here are future
verification obligations, not tests this documentation change claims to have passed.

| ID | Required observable if adopted | Engineering failure addressed | Verification and delivery |
|---|---|---|---|
| PAR-REQ-001 | A comparison names both implementation SHAs, exact contract SHA, phase/profile and position precision. Unknown/missing capability cannot imply support. | False parity from unequal input contracts. | #314: reject mismatched/unsupported claims; check positive cases for every declared phase. ADR-015 D1/D4. |
| PAR-REQ-002 | Extent equality, containment, tint composition and raw RGBA hash have distinct versioned identities with defined applicability, empty policy, ROI and arithmetic. Migration preserves old required checks. | A weaker predicate silently replaces evidence. | #313: empty/inset/exact/overflow rectangles; absent/wrong tint, impossible shared coverage, composite-rounding boundaries; matching/missing/mismatched baselines. D2. |
| PAR-REQ-003 | Required observations are derived independently from the pinned screen/profile; missing, duplicate, unknown, unsupported or not-run required rows prevent a successful gate. Keys include all scope/provenance fields in D3. | Omitted checks or locale/frame collisions produce a false pass. | #314/#321: remove/duplicate/substitute a row; swap locale, asset, scenario or capture identity; verify rejection. D3. **Stage C** gates the shared-corpus side: `medui-conformance.toml` claims `MEDUI-PROFILE-EVIDENCE`, and `conformance_spec` runs MedUI's 31 `aggregate-evidence` vectors (E01–E03 — identity match, one row per obligation, outcome aggregation; `not-run` on empty) and the 29 `evidence` contract documents, plus negative fixtures. **Stage C-emitter** closes the "derived independently" clause for MduX's side: `mdux-verify-ui --medui-evidence-out` derives the `MEDUI-PROFILE-RENDERED` E01 envelope from a real verify run (obligations synthesized from the screen/checks, not read back from a report), validated against `evidence.schema.json` and shown to aggregate to `pass` on a GPU leg. It is the RENDERED subset (`LocalizedTextPresence` is implementation-local) and is derived/uncommitted (ADR-014 D4). |
| PAR-REQ-004 | Normalize coordinates once with a specified rounding rule. Proposed hit policy: half-open rectangles, reverse paint order, all-node occlusion; press arms a target and release activates only the same eligible target. Cancel on focus loss/removal/overflow. | Wrong or stale target activates. | #315/#316/#317: exact edges, overlap with a non-control, outside-release, changed target, cancellation. D5; final coordinate rounding still requires #315 review. |
| PAR-REQ-005 | Events have bounded caller-specified capacity and stable order. Proposed overflow: drop newest, saturating drop count, cancel pending activation. An update consumes one accepted batch, updates state, binds one snapshot, renders then captures. | Lost release triggers an action; replay/capture observes inconsistent state. | #315/#316/#320: zero/full capacities, counter saturation, dropped release, multiple events, recorded order and identical replay snapshot. D5. |
| PAR-REQ-006 | Critical action resolution returns a closed action and requirement identity; ordinary button source is distinct. The host supplies execution/audit policy. Unknown/untraced critical actions fail explicitly. | Example action semantics become an unreviewed device control. | #315/#318: NoOp, TriggerHalt, invalid action, missing trace and ordinary button; host-policy review before any real-device effect. D5. |
| PAR-REQ-007 | Editing respects bounded Unicode scalar positions, declared repertoire and length. Invalid encoding/glyphs/oversize edits fail without partial mutation; no on-device shaping or unbounded storage. | Corrupted/truncated text or an unbounded input path. | #316: multibyte caret/delete boundaries, disallowed glyph, full capacity, paste/repeat/focus policy. Review final policies in #315; retain ADR-010 constraints. D5. |
| PAR-REQ-008 | Normalize binding by screen/node and declared source, with typed limits and a snapshot ID. Replay injects time. State order, number format, trace order and viewport row/bin semantics are explicit. | A capture shows data from a different frame or source. | #319/#320/#322: reordered/unknown binding, range/format boundaries, fixed time, ring wrap and overflow. D5. |
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
| #314 common corpus gate | D1–D4 and #313; MedUI #15 plus diagnostic/safety cases from existing #2/#3/#4/#8. Only claim phases with complete applicable passing cases. **Stage A** re-pinned to `v0.3.0-rc.1` and adopted the 27-case compiler corpus. **Stage B** delivers the shared-corpus half of PAR-REQ-002: `medui-conformance.toml` claims `MEDUI-PROFILE-RENDERED`, and `conformance_spec` runs MedUI's 33 `rendered-check` vectors (R01–R04) against `mdux.verify`'s own exported arithmetic plus the 10 `consumer-manifest` contract cases. **Stage C** does the same for PAR-REQ-003: `MEDUI-PROFILE-EVIDENCE` claimed, 31 `aggregate-evidence` vectors (E01–E03) and 29 `evidence` contract documents gated, all contract documents now validated by the ported `mdux.tools.schema`. **Stage C-emitter** adds `mdux-verify-ui --medui-evidence-out`: MduX's own derived, uncommitted `MEDUI-PROFILE-RENDERED` E01 envelope from a real run, schema-valid and aggregating to `pass` on a GPU leg. The individual PAR-REQ dispositions and the TrustSC-side re-pin remain open. |
| #315 input design | D5; [MedUI #16](https://github.com/Compliatory/MedUI/issues/16), PAR-REQ-004–008. Resolve event vocabulary, coordinate rounding, paste/repeat/focus and host action policy here. |
| #316 queue/editing; #317 presentation adapter | Accepted #315 requirements; D5; PAR-REQ-004–007. |
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

| Record | Requested reviewer | State |
|---|---|---|
| ADR-015 D1–D5 | MduX maintainer | Accepted by Ambroise Leclerc, 2026-09-07 |
| ADR-016 (local observation profiles) | MduX maintainer; domain reviewer (safety-relevant verifier area) | Proposed 2026-09-07 (#313). The four required rendered checks (`Bounds`, `ColorHash`, `InkContainment`, `LocalizedTextPresence`) keep their exact semantics; each gains an implementation-local `ObservationProfile` id recorded per outcome. `mdux.verify::rawImageDigest()` is an exported, fixture-tested predicate with a defined `NoBaseline` outcome that never discharges an obligation — it is **not** a `CvCheck`, has **no production caller**, and is **outside driver obligation enumeration**. Production-driver behaviour is unchanged; the committed `verification.json` differs only by the additive `observationProfile` member. |
| PAR-REQ-001–010 | MduX maintainer; domain reviewer for any device action/risk-control interpretation | Individual dispositions pending. #313 delivered the **local** half of PAR-REQ-002; #314 Stage B delivers the **shared-corpus** half of PAR-REQ-002 (`MEDUI-PROFILE-RENDERED` + 33 vectors + 10 manifest cases) and Stage C the shared-corpus half of PAR-REQ-003 (`MEDUI-PROFILE-EVIDENCE` + 31 `aggregate-evidence` vectors + 29 `evidence` contract documents + `mdux.tools.schema`); Stage C-emitter adds MduX's own derived, uncommitted `MEDUI-PROFILE-RENDERED` E01 envelope (`mdux-verify-ui --medui-evidence-out`). The PAR-REQ dispositions themselves and the cross-implementation half remain open. |
| Shared rendered profiles | MedUI #15 participants/maintainer | MEDUI-DEC-007 accepted, 2026-09-07; profile delivery pending |
| Shared interaction/presentation profiles | MedUI #16 participants/maintainer | MEDUI-DEC-008 accepted, 2026-09-07; profile delivery pending |

The PR review should record which local requirements are accepted, amended or deferred and link
the reviewer/date here or in the accepting follow-up. Upstream architectural acceptance is recorded in ADR-015; consumer adoption still requires
the applicable schema and corpus revision. [Executed evidence](behavior-matrix.md#executed-observations-and-reproduction)
supports the current-state comparison only; it does not discharge these future requirements.
