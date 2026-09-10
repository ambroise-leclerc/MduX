# ADR-021: Dynamic scenario-capture evidence and its CI gate

## Status

**Proposed**, 2026-09-10, for [#321](https://github.com/ambroise-leclerc/MduX/issues/321), the
epic-[#309](https://github.com/ambroise-leclerc/MduX/issues/309) critical path. It records the
design of the **dynamic rendered-evidence gate** — a verifier that replays a committed
`.scenario` through the application's real update path, renders the frames its `capture` markers
name with the production offscreen adapter, and checks and commits a complete, traceable evidence
set — and a **prospective PAR-REQ-009 disposition** for maintainer/domain review.

It changes no existing runtime behaviour, no compiled screen, no compiled scenario and no shared
pin. It builds on [ADR-014](ADR-014-rendered-truth-verification.md) (what a rendered check may and
may not claim), [ADR-016](ADR-016-locally-versioned-observation-profiles.md) (observation-profile
identity, and why a measured pixel is not committed), [ADR-018](ADR-018-bounded-input-and-update-order.md)
clause 6 (input → update → bind → render → capture) and [ADR-020](ADR-020-bounded-interaction-scenarios-and-replay.md)
(the `.scenario` format, the committed artifact, and the bounded replay contract). ADR-020's
Approval section left "the dynamic-frame evidence gate with committed capture digests (#321)" open;
this record closes that item, and deliberately does **not** commit capture digests — see Decision 3.

## Shared contract

No MedUI shared decision governs dynamic-capture evidence. MedUI
[#16](https://github.com/Compliatory/MedUI/issues/16) / MEDUI-DEC-008 name
`MEDUI-PROFILE-INTERACTION` / `-BINDING` / `-PRESENTATION` but deliver no schema and no corpus, and
ADR-020 already records that every scenario type is implementation-local. This record adds no
`medui-conformance.toml` key and claims no shared profile. The rendered checks the scenario gate
runs are the same `mdux.verify` predicates the screen gate runs, under the same `mdux.local/`
observation profiles ADR-016 fixed; nothing new is added to that set.

## Context

After #320, a committed `.scenario` replays through `updateMonitor()` — the one assembled update
loop — and `ScenarioRunner` checks a typed expected/observed pair per `Expect` step, GPU-free. The
`endoscope-monitor-basics` scenario names two `capture` markers, `freeze-pressed` and
`halt-activated`, and `MedicalScreenMonitorExample --replay` renders one offscreen frame per marker
— but nothing verifies those frames, and nothing byte-verified records that the verification
happened. The static verifier `mdux-verify-ui` renders only the committed screen's **static**
bindings: the default reading, the neutral classifier state, an empty `patient-id`. The
freeze-pressed frame in `fr-FR` — the pressure reading at its settled value, the French halt label,
the field showing `A7` — is checked by no gate on any leg.

Two failure modes follow, and PAR-REQ-002/003/009 name both:

- **A dynamic frame regresses while every static check still passes.** An interaction that corrupts
  the layout, drops the localized label, or renders a stale value produces a frame `mdux-verify-ui`
  never sees.
- **An incomplete evidence set reads as complete.** A capture the replay never reached, a locale
  silently dropped, a substituted package, a duplicated outcome — any of these can make a partial
  run look like a full one (PAR-REQ-003, D3).

The replayed application whose settled state a capture shows is `examples/support/MonitorApp.hpp`'s
`updateMonitor()` — examples-zone, endoscope-monitor-specific demonstration logic. Committed
evidence artifacts are produced by host tools registered through `mdux_bake_artifact()`. So the
gate has to cross that boundary deliberately, or not exist.

## Medical Device Considerations

Impact: **safety-relevant dynamic rendered evidence**. This record is a design decision plus a host
build tool and one committed evidence artifact per scenario. The tool performs no input, executes
no action (it records an `ActionTrace`, exactly as the replay does) and renders only offscreen.

- **IEC 62304:2006 §5.7**: the software-system verification scope limit in
  `docs/iec62304/03-development-process.md` is unchanged — MduX has no assembled software system,
  and a replay of a bounded example verified against its own compiled inputs is development-time
  evidence, not a verification, validation or release activity for a device.
- **IEC 62366:2006 (usability)**: a scenario states an operator interaction and its settled visual
  result; committing per-obligation findings gives a usability review an ordered, traceable record
  to assess. This record does not itself constitute that review.
- **Risk management**: no hazard, risk control or software safety class in this repository names
  the failure mode of a stale rendered value, a dropped localized label or a mis-attributed
  capture. This record does not invent one. The `requirement` ids a scenario cites (`REQ-EM-002`,
  `REQ-EM-003`) are engineering trace ids; the evidence links each obligation to them and to the
  input digests, and nothing more is claimed.
- **Traceability**: `scenario-verification.json` records, per obligation, the scenario id, the
  cited requirement ids, the render scope, the capture marker, the check, the finding, and every
  input digest — the scenario, the screen, its goldens, the `shader/mdux-ui` package, each approved locale's
  text and font packages, and the brand-mark image, each read from `generated/` and used directly
  for the render it names. A run leaves a complete scenario × locale × capture × check matrix or it
  fails.
- **Cybersecurity**: unaffected. The tool reads only committed artifacts under `generated/`,
  digests each, and refuses a non-canonical or digest-mismatched input. It is a build tool, never
  installed, never linked into `MduXCore` or `MduX`.

No cross-implementation parity is claimed; that stays [#335](https://github.com/ambroise-leclerc/MduX/issues/335).
This is not a certification, validation or production-readiness claim.

## Decision

### 1. `mdux-verify-scenario` is a host-tools-zone verifier that links the examples-zone replay glue

A new tool pair under `tools/verify-scenario/`, mirroring the `mdux-verify-ui` / `mdux-verify-bake`
split ADR-014 established:

- **`mdux-verify-scenario`** — the verdict driver. `--scenario=<generated/scenario/id> --locales=all`
  over the **committed** bundle. Exit 0 pass, 1 an obligation failed, 2 usage, 3 the run could not
  be made (no Vulkan 1.3 device is `NoRenderDevice`, exit 3, never a skip — the same reading
  `mdux-verify-ui` takes). `--locales` cannot narrow the scenario's screen manifest.
- **`mdux-verify-scenario-bake`** — the `THEN_TOOLS` stage. `bake <recipe> <output-dir>` renders
  the just-baked bundle, writes `scenario-verification.json`, extends `report.json`, and publishes
  both atomically (`mdux::tools::verify::publishBundle`). A failed obligation is recorded and the
  bundle written; a run that could not be made writes nothing and exits non-zero — ADR-014
  decision 3, unchanged.

The library `MduXVerifyScenarioLib` links `MduX::VerifyUiLib` (for the atomic publish, the report
extension and the shared artifact loaders), `MduX::MduX` (`mdux.render.offscreen`, `mdux.draw`),
`MduX::ScenarioLib` (the `scenario.json` reader), `mdux.verify`, and `#include`s
`examples/support/MonitorApp.hpp`, `ScenarioReplay.hpp` and the lean new
`examples/support/MonitorFrame.hpp`. It embeds **nothing** screen- or scenario-specific: it holds no
generated `constexpr` scenario or screen module and no `mdux_embed_blob` package. Every artifact it
replays and renders against is read from `generated/` on disk at run time (Decision 2), so the digest
it records is of the bytes it actually used, and a scenario or screen edit never needs the tool
rebuilt.

**Why this boundary crossing is admitted rather than worked around.** The settled dynamic state a
capture shows — the classifier position, the insufflation pressure, the field value — is produced
by application logic that lives in the examples zone and nowhere else. A generic screen-runtime
replay (Alternative below) has no such state to bind and would verify a static frame under a
dynamic name. The examples-zone code the tool links is the update loop and the frame recorder
(`updateMonitor()`, `recordMonitorFrame()`); the compiled scenario, the compiled screen and the
font / text / image / shader packages it drives them with are read from `generated/`, not linked,
so what is reviewed as an example is the interaction logic and nothing else. `mdux_verify_trust_zones()` constrains only *governed* targets (that they reach no
Vulkan or windowing dependency); a host tool linking an examples-zone header is outside its scope
and always has been. The tool executes nothing, and the replayed logic is a reviewed, committed
example, so the risk the trust-zone split exists to contain — untrusted or unreviewed code in the
evidence path — is not present.

### 2. The complete obligation set, and how a missing outcome fails closed

For a compiled scenario S with approved locales L₁…Lₙ and declared capture markers C₁…Cₘ, a run
enumerates:

- **Binding obligations** — one per `Expect` step per locale (`|Expect(S)| · n`). The obligation is
  discharged by `ScenarioRunner`'s own `StepOutcome` for that step in that locale's replay: the
  settled clock / field / reading / status / latch / refused-count / overflow equals the pinned
  value. A non-`Held` outcome, a batch larger than the queue (`QueueTooSmall`), an unreached step
  (`OutcomeStorageFull`) or a malformed scenario each fail the run.
- **Rendered obligations** — for each `(Cⱼ, Lᵢ)` pair, the scenario gate runs the golden and
  mandatory-text obligation set `mdux-verify-ui` enumerates for the screen against the frame that
  capture settled, in that locale: `Bounds` / `ColorHash` on each golden-bearing node,
  `InkContainment` / `LocalizedTextPresence` on each `textKey` node — **minus golden `ColorHash` on
  any node whose content the scenario drives** (`contentIsSceneDriven()`: the pressure reading, the
  classifier state, the `patient-id` field, the ECG trace, the wall clock), **plus a `RegionPainted`
  check on each of those scene-driven nodes**. The static baseline verified a NumericDisplay's
  default face; one showing "12.0 mmHg" paints digit glyphs whose edges are a legitimate third
  colour a ground-and-tint blend cannot be, so the tint check on those nodes is filtered before the
  set is counted or evaluated — golden `Bounds` on them still holds, and `RegionPainted`
  (`mdux.verify::regionPainted()`, governed, `mdux.local/region-painted`) adds the one honest claim a
  rendered check can make about a value it cannot predict: the node drew *something* over the
  driver's resolved ground, so it did not go blank. *Which* value it drew is the binding obligation's
  to assert, not a rendered check's (Decision 4). The predicates, the expectations (derived from the
  committed artifacts, never from the caller) and the observation profiles are `mdux.verify`'s: the
  scenario gate renders a different *frame*, and adds one governed predicate for the scene-driven
  case, but runs no check the screen gate could not.
- **Capture-completeness obligations** — one per declared `captureNames` entry: the replay handed
  that marker's frame to the callback (`markCaptured`), and the callback rendered a readback of the
  screen's authored extent. A declared marker no `Capture` step produces, or one the replay never
  reaches, is `CaptureNotInvoked` and fails the run.

`scenario-verification.json` records one entry per obligation with its identities and finding.
`run()` reconciles the outcomes against the enumerated obligations *before* writing: each outcome is
paired with its obligation by the whole identity tuple
`(kind, scope, capture, stepIndex, expectKind, nodeId, check)`, a missing pairing appends a failed
outcome (VSC010), a duplicated one is flagged (VSC011), and an outcome that discharges no obligation
fails the verdict (VSC014). `mdux-verify-scenario-bake` then refuses to write the artifact at all
unless the outcome count equals the obligation count and each pair agrees — the same fail-closed
check `writeVerification()` makes for the screen bundle. Missing, duplicate, unknown, unsupported and
not-run rows are all rejected.

**Two gates, two claims — the verifier attests the run it made; the bake comparison attests
source-to-artifact identity.**

- `mdux-verify-scenario` (`verify.scenario.<id>`) rebuilds the `CompiledScenario` from
  `scenario.json` with the shared `mdux.tools.scenario::readScenarioDoc()` reader and reads the
  screen, goldens, shader, per-locale text/font and image packages from `generated/` — digesting
  each, checking it for canonical form, and using it *directly* for the bindings, the replay and the
  render (the point of Decision 1's disk model). It therefore attests exactly one thing: that the
  bytes it recorded a digest of are the bytes it replayed and rendered, and that every obligation
  those inputs generate held. It does **not** compare `scenario.json` against a reviewed source: a
  canonical, same-id edit whose altered inputs and altered expectations stay self-consistent is
  replayed as written and can pass. An edit that makes the scenario *internally* inconsistent —
  a pinned value the interaction no longer settles, a frame count that lands the clock a second
  short — fails a binding obligation (`VSC101`), and a non-canonical or digest-mismatched screen /
  text / font / image / shader package stops the run before a frame is drawn (`VSC004`); a malformed
  or wrong-id scenario stops it before the screen is resolved (`VSC001` / `VSC002` / `VSC003`).
- `evidence.scenario.<id>` is what catches a `scenario.json` (or any bundle file) that drifted from
  the reviewed recipe: it byte-compares the freshly baked bundle against the committed one on every
  toolchain leg, exactly as `evidence.screen.<id>` does. A same-id edit that never re-baked is a
  byte difference there, whether or not the verifier's self-consistent replay would have passed.

The two are complementary and neither subsumes the other: the bake comparison proves the committed
bytes are the recipe's output, and the verifier proves those committed bytes replay and render to
the findings recorded beside them.

### 3. Committed byte-verified evidence versus diagnostic attachments

**Committed, byte-verified** (`scenario-verification.json`, compared by `evidence.scenario.<id>` on
every toolchain leg): the per-obligation *findings* — `Held` or a named `Finding` — plus the input
digests, the scenario id, the cited requirement ids, the render scopes and the capture markers. No
measured pixel value appears in it, for ADR-014 decision 4's reason: a committed measurement is a
property of the driver tuple (lavapipe, MoltenVK, Mesa-on-Windows) that produced the frame, not of
the screen's declared inputs, so a rendering change would fail an evidence comparison while every
check still held. The findings are outcome-preserving identities the way the screen bundle's are.

**Diagnostic attachments** (under the build tree, never `generated/`, uploaded by CI only on
failure): one PNG per capture × locale (the readback, undimmed), and a `captures.sha256` manifest
of the per-backend `rgba8-sha256` of each. The manifest **is** the backend-specific baseline
PAR-REQ-009 asks for — it is kept out of the byte-compared bundle exactly as ADR-016 keeps
`rawImageDigest()`'s digest out, and it is portable across neither backends nor driver versions.

### 4. Prospective PAR-REQ-009 disposition (for maintainer/domain review)

PAR-REQ-009 — "exact pixel comparison requires a declared presentation/backend profile and
theme/font/image digests; logical equivalence alone makes no exact-pixel claim" — is disposed as
**Accepted with a recorded limitation**, proposed here and **pending the verifier-area domain
review** that also gates ADR-016/ADR-017's residuals:

- The committed scenario gate makes **no exact-pixel claim**. It asserts portable structural
  obligations: settled binding values equal the scenario's pinned expectations, and each capture
  frame discharges the screen's own golden/text rendered-truth obligations (extent equality, tint
  composition within the device-rounding allowance, localized-run coverage) plus `RegionPainted` on
  the scene-driven nodes — all under the `mdux.local/` profiles, none of which commits a pixel value.
- **A scene-driven node's exact rendered value is verified as settled state, not as pixels.** A
  `NumericDisplay` showing the insufflation pressure, or the `patient-id` field showing what was
  typed, renders glyphs whose shapes a rendered-truth check cannot predict without running the
  device's own text shaper over the live value — which [ADR-010](ADR-010-no-on-device-text-shaping.md)
  forbids in a host tool, and which no committed golden captures. The gate therefore splits the
  claim: the **binding obligation** asserts the replay settled the pinned value (the pressure is
  `120`, the field is `A7`), exactly and portably, and **`RegionPainted`** asserts the node then
  drew that value rather than going blank. A frame that rendered the wrong number from the right
  state is a rendering defect the golden `Bounds` extent and, on a device that has declared a
  presentation profile, the `captures.sha256` baseline would catch; the committed gate does not
  claim to.
- An **exact-pixel** claim over a capture stays a **backend-specific baseline** (the
  `captures.sha256` diagnostic manifest), available to a future consumer that has declared a
  presentation/backend profile, and is never a committed gate — the same stance ADR-016 records
  for `rawImageDigest()`.
- The cross-implementation half stays #335; the streaming-viewport capture half stays
  [#322](https://github.com/ambroise-leclerc/MduX/issues/322) → #324.

### 5. The gate retains the screen gate and adds a distinct scenario gate

`evidence.screen.<id>`, `verify.screen.<id>` and `verify_ui_spec` are unchanged. The scenario
recipe's `mdux_bake_artifact(KIND scenario …)` registration gains `THEN_TOOLS
mdux-verify-scenario-bake` and one output (`scenario-verification.json`), so
`evidence.scenario.<id>` now byte-compares it too. A new `verify.scenario.<id>` ctest (label
`verify`) runs `mdux-verify-scenario` against the committed bundle on the GPU legs, parallel to
`verify.screen.<id>` and never a skip. `mdux-verify-ui`'s driver gains one exported helper,
`evaluateFrame()`, factored out of its existing per-scope check loop with no behaviour change so
both gates run the identical rendered-truth checks; the shared headless-device bring-up moves to a
private `tools/verify/HeadlessDevice.hpp` included by both drivers.

## Alternatives Considered

- **Promote `MedicalScreenMonitorExample --replay` into the evidence producer.** Rejected: it puts
  committed-evidence production in the examples zone, and an example's build (`mdux_embed_blob`,
  GLFW, generated modules) is not an evidence-tool build. The tool reads committed artifacts from
  disk like every other baker.
- **A generic screen-runtime replay tool with no application logic.** Rejected: `ScenarioRunner`
  checks expectations but touches no application, and the screen runtime's `render()` binds
  caller-supplied state. Without `updateMonitor()` there is no classifier, pressure or field logic,
  so every "dynamic" capture would be the static frame under a new name — the false-confidence
  failure this record exists to prevent.
- **Commit an `rgba8-sha256` per capture × locale as a baseline in the byte-compared bundle.**
  Rejected: it reverses ADR-014 decision 4 and ADR-016, makes `evidence.scenario.<id>` fail on any
  driver or Mesa update while every check still holds, and TrustSC commits none either. The digest
  lives in the diagnostic manifest instead.
- **A "region is painted" check implemented in the host tool.** Rejected on where it lives, not on
  whether it is needed: a rendered check belongs in governed `mdux.verify` (ADR-014 decision 1), so
  `regionPainted()` was added there, under `mdux.local/region-painted`, with its own unit coverage —
  the tool only enumerates it. It *is* needed: dropping golden `ColorHash` on a scene-driven
  NumericDisplay (a live value legitimately paints a third colour) left the frozen-pressure and
  typed-field regions with no rendered check that they drew anything at all, so a frame that
  rendered those nodes blank while the settled state was correct would have held every remaining
  obligation. `RegionPainted` closes that; the pinned *value* stays the binding obligation's to
  assert (Decision 4).
- **A rendered check that verifies the scene-driven node shows the pinned value's glyphs.** Rejected:
  it needs the device's text shaper run over the live value at verify time, which ADR-010 forbids in
  a host tool, and no committed golden carries that run. The binding obligation already asserts the
  value portably and exactly; `RegionPainted` asserts it was drawn.
- **A second ADR for the tool and a third for the PAR-REQ-009 disposition.** Rejected: ADR-020 set
  the precedent of one record for a format and its consumers' contract, and the disposition is a
  direct consequence of Decision 3.
- **Extend `mdux-verify-ui` with a `--scenario` mode.** Rejected: one is a static screen driver
  that needs no application, the other replays an examples-zone loop. Sharing `evaluateFrame()` and
  the publish plumbing is the right amount of reuse; a mode flag would drag the examples-zone
  include into the screen driver.

## Consequences

### Positive

- The freeze-pressed and halt-activated frames are verified in both approved locales on every GPU
  leg, against the screen's own committed expectations — the first dynamic rendered evidence in the
  repository.
- One committed artifact per scenario, byte-compared on every toolchain, links each obligation to
  its scenario, requirements and input digests. An incomplete run fails closed.
- The rendered checks are governed `mdux.verify` predicates: the scenario gate cannot claim more
  about a frame than a governed check allows, and a predicate improvement reaches every consumer.
  The one predicate this work adds, `regionPainted()`, lands in `mdux.verify` with its own tests, not
  in the tool.
- The screen gate is untouched in behaviour and guarded by its existing tests.

### Negative

- A reader of the verification subsystem now follows ADR-014 (what a check claims), ADR-016 (profile
  identity), and this (the dynamic frame and the committed/diagnostic split). Mitigation: this
  record cites the specific decisions rather than restating them, and the scenario gate adds no
  check and no profile.
- A host tool now `#include`s an examples-zone header. Mitigation: Decision 1 states the boundary
  crossing, its justification and why the mechanical trust-zone check is unaffected; the replayed
  logic is a committed, reviewed example.
- The tool matches the bundle it is pointed at by directory name and its `scenario.json`'s own id;
  it is not wired to a second scenario yet. Mitigation: because it reads the bundle from disk rather
  than linking a generated module, a second scenario adds a `mdux_bake_artifact` registration and a
  `verify.scenario.<id>` ctest and nothing in the tool. The obligation model is generic.

### Risks

- **The disposition is read as a completed safety review.** Mitigation: the status is **Proposed**,
  Decision 4 says "pending the verifier-area domain review", and PAR-REQ-009 keeps its
  `Proposed / unreviewed` status in `docs/parity/requirements.md` until that review.
- **The diagnostic digest manifest is mistaken for a gate.** Mitigation: it is written under the
  build tree, never staged into `generated/`, the "no source-tree writes" CI gate would catch a
  regression, and `scenario-verification.json` — the committed file — contains no pixel value.
- **A capture frame's dynamic content drifts from the scenario's pinned state without a finding.**
  Mitigation: the binding obligations pin the settled state exactly and portably, and the rendered
  obligations pin the frame — golden `Bounds` on every scene-driven node, `RegionPainted` that it
  drew at all, and the full golden/text set on every static node. What no committed check covers is a
  scene-driven node that drew the *wrong* value from the *right* state (Decision 4): that needs
  on-device shaping ADR-010 forbids here, and the `captures.sha256` diagnostic baseline is where a
  profile-declaring consumer would catch it.

## Implementation Notes

- `tools/verify-scenario/`: `ScenarioDriver.{hpp,cpp}` (reads `scenario.json` and rebuilds the
  `CompiledScenario` with `mdux.tools.scenario::readScenarioDoc()`, loads the screen / goldens /
  shader / per-locale text+font / image packages from `generated/` with the shared
  `mdux.tools.verify.artifacts` loaders, replays each approved locale through
  `replayMonitorScenario()`, renders each capture with `recordMonitorFrame()` + a headless
  `OffscreenTarget` built from the disk-loaded shader, enumerates and discharges the obligation set),
  `ScenarioArtifact.{hpp,cpp}` (`writeScenarioVerification()`, `extendScenarioReport()`, reusing
  `publishBundle` / `BundleFile`), `VerifyScenarioMain.cpp`, `VerifyScenarioBakeMain.cpp`. These are
  **not C++20 modules** — they link the global-module examples-support glue (`updateMonitor()`,
  `replayMonitorScenario()`, `recordMonitorFrame()`), and a module interface would attach a
  declaration of one to the module, which Clang rejects at link; they are plain headers with an
  include-order contract, like `ScenarioReplay.hpp`. `MduXVerifyScenarioLib` PUBLIC-links
  `MduX::VerifyUiLib` only (consumers `import mdux.tools.verify.driver` / `mdux.tools.verify.artifact`
  themselves); `MduX::MduX`, `MduX::ScenarioLib` and `Vulkan::Vulkan` are PRIVATE — a PUBLIC MduX
  would make a consumer inherit `MduX_options`' version macros twice (GCC `-Werror` redefinition).
- `tools/verify/Artifacts.{cppm,cpp}` (`module mdux.tools.verify.artifacts`): `readBytes` /
  `readText` / `hexDigest` and `loadShader` / `loadLocale` / `loadImage`, moved unchanged out of
  `Driver.cpp`'s anonymous namespace (`VUI005` / `VUI006` diagnostics unchanged) so the screen gate
  and the scenario gate load and authenticate the committed shader, font, text and image packages
  through **one** implementation and cannot disagree about what the committed `shader/mdux-ui`
  package is.
- `tools/verify/Driver.{cppm,cpp}`: export `evaluateFrame()` and `readGoldens()` / `OwnedGolden`,
  factored from the per-scope check loop / anonymous namespace with no behaviour change.
  `tools/verify/HeadlessDevice.hpp`: the Vulkan 1.3 headless bring-up, moved out of `Driver.cpp`'s
  anonymous namespace, `#include`d by both drivers as a module-private implementation detail (no
  module interface exposes a Vulkan type).
- `include/mdux/verify/Verify.cppm`, `src/verify/Verify.cpp`: `regionPainted(frame, rect, ground,
  nodeId, scope)` and `regionPaintedProfile` (`mdux.local/region-painted`, v1) — the governed
  weakest-honest-claim check for a scene-driven node, `Finding::NothingPainted` when the rectangle is
  entirely the ground and `Finding::RegionOutsideFrame` when it leaves the frame; unit coverage in
  `tests/verify/GoldenCheckTests.cpp`. `profileForCheckName("RegionPainted")` maps it.
- `examples/support/MonitorFrame.hpp`: the lean shared frame path — `FrameStorage` and
  `recordMonitorFrame(screen, textBinding, imageBinding, storage, state, clock)`, parameterised on
  already-loaded packages, naming no `mdux_embed_blob` and no generated module. The blob- and
  generated-module-dependent `BoundScreen` / `makeRenderer` moved to the new
  `examples/support/MonitorScreen.hpp`, which only `MedicalScreenMonitorExample.cpp` includes; the
  tool builds its bindings and renderer from the disk-loaded packages instead.
- `CMakeLists.txt`: `THEN_TOOLS mdux-verify-scenario-bake` + `OUTPUTS scenario-verification.json` on
  the `endoscope-monitor-basics` `mdux_bake_artifact()` call, and a `verify.scenario.<id>` ctest
  registered beside it (label `verify`, no `SKIP_RETURN_CODE`).
- `tests/verify-scenario/`: `verify_scenario_spec` (`MduX::VerifyScenarioLib`, `MDUX_REPO_ROOT`
  defined) — the positive run in both locales, and the fail-closed set the issue names: a binding
  outcome the replay marked failed (wrong pinned value), a missing outcome (dropped input / omitted
  capture), a duplicated outcome, a surplus outcome, a substituted screen package, a scenario id
  mismatch, and an altered `scenario.json` that is *replayed as written* — the shortened final
  advance no longer settles the pinned clock, a binding obligation fails (`VSC101`), and the digest
  recorded in `inputs` is the altered file's, proving the verifier attests the run it made rather
  than a stale copy. The real replays carry label `pixel`; the pure enumeration / reconciliation /
  rejection cases run everywhere.
- CI: the four build workflows already run `ctest -L verify` and `-L evidence`; add a
  `verify-scenario-frames/` upload-artifact-on-failure step to the Clang (lavapipe) and macOS
  (MoltenVK) legs.
- `docs/architecture.md`, `docs/roadmap.md` (#309 / #321), `docs/parity/requirements.md`
  (PAR-REQ-009 row, the #321 decision-map row), `docs/adr/README.md` (index ADR-021; next free is
  ADR-022), this ADR cited from ADR-020's Approval "Still open" bullet.

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) — the governed / adapter / host-tools split
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md) — canonical form, byte-identity, bake reports
- [ADR-010](ADR-010-no-on-device-text-shaping.md) — why a host tool cannot shape the live value's
  glyphs at verify time, so a scene-driven node's exact rendered content is not a committed claim
- [ADR-014](ADR-014-rendered-truth-verification.md) — what a rendered check may claim; decisions
  2 (derive, don't trust), 3 (a skipped check is a failure), 4 (no measured pixel is committed)
- [ADR-016](ADR-016-locally-versioned-observation-profiles.md) — observation-profile identity; why
  `rawImageDigest()` has no committed baseline
- [ADR-018](ADR-018-bounded-input-and-update-order.md) clause 6 — input → update → bind → render →
  capture
- [ADR-020](ADR-020-bounded-interaction-scenarios-and-replay.md) — the `.scenario` format, the
  committed artifact, the bounded replay contract; its Approval leaves the #321 gate open
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-002, PAR-REQ-003, PAR-REQ-009
- Issues [#309](https://github.com/ambroise-leclerc/MduX/issues/309),
  [#320](https://github.com/ambroise-leclerc/MduX/issues/320),
  [#321](https://github.com/ambroise-leclerc/MduX/issues/321)
- `tools/verify/Driver.cppm`, `tools/verify/Artifacts.cppm`, `tools/verify/Artifact.cppm`,
  `include/mdux/verify/Verify.cppm`, `include/mdux/medui/Scenario.cppm`,
  `tools/scenario/Scenario.cppm` (`readScenarioDoc`), `examples/support/ScenarioReplay.hpp`,
  `examples/support/MonitorFrame.hpp`, `examples/support/MonitorScreen.hpp`

## Approval

- **Proposal date**: 2026-09-10
- **Decision date**: pending
- **Approved by**: pending — maintainer engineering acceptance and the verifier-area domain review
  (behaviour verification, dynamic rendered evidence) for the PAR-REQ-009 disposition in Decision 4.
- **Scope**: the `mdux-verify-scenario` / `-bake` tool pair, the `scenario-verification.json`
  committed artifact and its obligation set, the committed-versus-diagnostic split, the
  `verify.scenario.<id>` gate, and the prospective PAR-REQ-009 disposition. Windowing, native
  capture and host action execution stay implementation/host decisions (ADR-018 clause 7,
  unchanged). No scenario or capture assigns a device's safety class.
- **Review date**: with the ADR-016/ADR-017 verifier-area domain review.
