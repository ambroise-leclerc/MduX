# Pinned sibling behavior matrix

Assessed **7 September 2026** for [#312](https://github.com/ambroise-leclerc/MduX/issues/312).
This is an implementation comparison and a proposed compatibility boundary, not a declaration
that the siblings conform to the same rendered or interactive contract. Prospective requirements,
review gates and migration rules are in [ADR-015](../adr/ADR-015-versioned-sibling-observations.md)
and the [design records](requirements.md). No runtime behavior or conformance pin changes here.
ADR-015 was subsequently accepted on the assessment date; this matrix retains its pinned
implementation observations. The ADR records the separately accepted upstream architectural decisions.

## Immutable comparison inputs

| Input | MduX | TrustSC |
|---|---|---|
| Implementation SHA | `db198c6cc84ab8e7c816549481e8349a0137767b` | `4f114dd30c64f61d11edb5941e95f422e189f305` |
| MedUI SHA | `265df1925a672bd556f69123e287215b45cfd210` | `c8cc45ecec2f2dfd84940b9efc17c613e691cc0d` |
| Manifest version label | `0.1.0` | `0.1.0-candidate` |
| Claimed phases | syntax, semantics, layout, safety | syntax |
| Diagnostic positions | full | line-only |

Sources: [MduX manifest][m-manifest], [TrustSC manifest][t-manifest]. The exact SHA is authoritative;
the label alone does not identify a contract. TrustSC's unclaimed phases still have local
implementations. This assessment does not turn those implementations into conformance claims.

## Compiler and artifact observations

| Surface | MduX at its pin | TrustSC at its pin | Compatibility consequence |
|---|---|---|---|
| Parse/phase boundary | Lexer/parser, semantic resolution, bounded layout and safety selection; host checker exposes the four phases. | Line-oriented parser and local compiler; shared checker claims syntax only. | Compare the requested phase and declared precision, not just process exit status. |
| Diagnostics | Stable codes and 1-based UTF-8 byte columns; nested Row gives E015 at 6:9. | Same nested-Row fixture gives E015 at line 6, absent column (wire position precision is line-only). | Absent precision is not column 1 or a full-position pass. Messages and fix prose are not identity. |
| Closed values | Newer pin includes MEDUI-DEC-006, clock/action domain/member cases E033/E034. | Older shared pin predates that decision, even where local enums agree. | A matching enum is not proof of matching rejection/precedence behavior. |
| Missing fields and references | Local checks exceed the small shared corpus. | Some reference failures remain unregistered; optional safety promotion is disputed. | Resolve [MedUI #2](https://github.com/Compliatory/MedUI/issues/2), [#3](https://github.com/Compliatory/MedUI/issues/3) and [#8](https://github.com/Compliatory/MedUI/issues/8); do not invent a common error code. |
| Layout | Bounded integer rectangles and flat paint-order nodes; positioned layout restrictions. | Corresponding fixed/row layout and flattened nodes. | Compare ordered node IDs/kinds/rectangles and failure cases, not generated C++ versus Rust. Layout issue #2 remains open. |
| Safety/goldens | Compiler rederives obligations; committed golden sidecar; verifier checks complete applicable obligations. | Golden references embedded in compiled package; shared safety phase unclaimed. | Compare selection/deduplication independently of rendered check meaning; annotation/required-field precedence needs #8. |
| Artifact envelope | Screen ID/version, surface extent, text/locale/digest and image approvals, nodes and draw budget; separate golden sidecar. | Screen ID, layout kind/spacing/padding, nodes and golden references. | Normalize logical fields; retain native envelope and provenance. Native bytes/layout are intentionally different. |
| Binding identity | Readings, status and field values bind by node ID; trace binds caller-owned source data. | Frame inputs generally route numbers, text, states and streams by source name. | Normalize screen/node plus declared source mapping. Never assume source and node identifiers are interchangeable. |

The shared corpus this matrix was written against had 18 cases (2 syntax, 14 semantics, 1 layout,
1 safety). MduX #314 Stage A re-pinned to `v0.3.0-rc.1` (`9a57f64`), whose compiler corpus is 27
cases (5 syntax, 17 semantics, 3 layout, 2 safety); the full matrix refresh is #314d. This is not
exhaustive coverage of the compiler. [MedUI #4](https://github.com/Compliatory/MedUI/issues/4)
tracks missing cases; its original count predates this pin. MEDUI-DEC-003/004/005 define compiled
observables, golden selection and diagnostic declarations, respectively; none establishes pixel
equivalence. See the pinned [MedUI decisions][decisions], [MduX schema][m-schema],
[MduX compiler][m-compiler], [TrustSC schema][t-schema] and [TrustSC compiler][t-compiler].

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

**Update, 7 September 2026 — [#313](https://github.com/ambroise-leclerc/MduX/issues/313) delivered
the local half of the "Proposed treatment" column above.** MduX's four rendered checks each carry an
implementation-local `ObservationProfile` (id + version), recorded per outcome in `verification.json`,
and `mdux.verify::rawImageDigest()` implements the RGBA8 SHA-256 observation TrustSC's `ColorHash`
performs — as its own profile, so the two `ColorHash` results are never conflated. Per
[ADR-016](../adr/ADR-016-locally-versioned-observation-profiles.md) and ADR-014 decision 4 it has
**no committed baseline** (a driver-tuple-dependent digest cannot live in a byte-compared artifact),
still returns `NoBaseline` on every production call, and is fixture-tested only. The profile ids are
`mdux.local/*` until MEDUI-DEC-007 delivers canonical identifiers; the shared schema/corpus and the
cross-implementation gate remain [#314](https://github.com/ambroise-leclerc/MduX/issues/314). This is
not a parity claim.

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

On 7 September 2026, the existing configured GCC build ran the following selection: **284 passed**.
This was not a fresh configure/build, a GPU capture comparison or a shared-corpus parity gate.

```sh
ctest --test-dir build-gcc -R '^(verify_spec|medui_tools_spec|medui_spec)::' --no-tests=error --output-on-failure
```

Notable fixtures include `A golden rectangle moved by one pixel fails, and an empty one fails
differently`, wrong/absent tint, impossible cross-channel blend and the reduced-coverage field
under a full-tint stroke. The compiler suite exercises the MduX shared-contract harness.

TrustSC's pinned verifier was built fresh with Rust/Cargo 1.96.0: **30 tests passed**, no doc tests.
The combined authoring/checker test attempt could not unpack cached arrayvec into the read-only
Cargo home. It is not reported as passing. Building the authoring library alone succeeded.
The [observation probe](trustsc-observation-probe.rs) calls the actual parser and verifier;
it confirmed E015 at line 6 with absent column, and isolated empty GoldenBounds=Pass alongside
ColorHash=NoBaseline. The node-free synthetic package isolates those golden predicates; it does
not claim an empty real screen would pass every applicable check.

To reproduce, extract the exact TrustSC SHA above into `/tmp/trustsc-parity-20260906` and the
MduX-pinned MedUI SHA into `/tmp/medui-312` (GitHub commit archives suffice), then run from MduX.
The nested-Row `source.medui` fixture is byte-identical at both MedUI pins (Git blob
`3ee73d7e4287b30bbb50d2554a95b414dc71a4b1`), so using this copy with TrustSC introduces no
fixture-version difference:

```sh
cargo test --offline --locked --manifest-path /tmp/trustsc-parity-20260906/Cargo.toml --target-dir /tmp/mdux-312-trustsc-target -p trustsc-ui-verify
cargo build --offline --locked --manifest-path /tmp/trustsc-parity-20260906/Cargo.toml --target-dir /tmp/mdux-312-trustsc-target -p trustsc-ui -p trustsc-ui-verify -p trustsc-ui-dsl-authoring
rustc --edition=2024 docs/parity/trustsc-observation-probe.rs \
  --extern trustsc_ui=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui.rlib \
  --extern trustsc_ui_verify=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui_verify.rlib \
  --extern trustsc_ui_dsl_authoring=/tmp/mdux-312-trustsc-target/debug/libtrustsc_ui_dsl_authoring.rlib \
  -L dependency=/tmp/mdux-312-trustsc-target/debug/deps -o /tmp/312-observation-probe
/tmp/312-observation-probe /tmp/medui-312/conformance/syntax/rejected-nested-row/source.medui
```

Offline mode needs the lockfile's dependencies in the local cache. This probe is supporting
evidence, not the future #314 CI gate. Widget/presentation/dynamic comparisons above are source
inspection; no cross-sibling pixel or event-replay equivalence was executed or established.

[m-manifest]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/medui-conformance.toml
[t-manifest]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/medui-conformance.toml
[decisions]: https://github.com/Compliatory/MedUI/tree/265df1925a672bd556f69123e287215b45cfd210/decisions
[m-schema]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/include/mdux/medui/Schema.cppm
[m-compiler]: https://github.com/ambroise-leclerc/MduX/tree/db198c6cc84ab8e7c816549481e8349a0137767b/tools/medui
[m-screen]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/include/mdux/medui/Screen.cppm
[m-hit]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/src/medui/Screen.cpp
[m-verify]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/include/mdux/verify/Verify.cppm
[m-arithmetic]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/src/verify/Verify.cpp
[m-tests]: https://github.com/ambroise-leclerc/MduX/blob/db198c6cc84ab8e7c816549481e8349a0137767b/tests/verify/GoldenCheckTests.cpp
[t-schema]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui/src/lib.rs
[t-compiler]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-dsl-authoring/src/lib.rs
[t-renderer]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/adapters/trustsc-vulkan-winit/src/renderer.rs
[t-adapter]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/adapters/trustsc-vulkan-winit/src/lib.rs
[t-input]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc/src/input.rs
[t-checks]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/checks.rs
[t-verify]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/lib.rs
[t-tests]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-ui-verify/src/tests.rs
[t-core]: https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd30c64f61d11edb5941e95f422e189f305/crates/trustsc-core/src/lib.rs
