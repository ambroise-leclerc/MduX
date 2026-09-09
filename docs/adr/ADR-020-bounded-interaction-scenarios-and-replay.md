# ADR-020: Bounded interaction scenarios and their replay

## Status

**Accepted**, 2026-09-09, for [#319](https://github.com/ambroise-leclerc/MduX/issues/319) and
[#320](https://github.com/ambroise-leclerc/MduX/issues/320), by maintainer instruction. Acceptance
establishes MduX's local contract for a **reviewed interaction-scenario format**, its host
compiler, the committed artifact it produces, and the **replay contract** that drives a compiled
scenario through the application's real event/update path. It changes no existing runtime
behaviour, no compiled screen, and no shared pin, and supersedes no earlier ADR. It builds on
[ADR-018](ADR-018-bounded-input-and-update-order.md) clause 6 (the assembled update order) and
[ADR-011](ADR-011-deterministic-medui-compile-boundary.md) / [ADR-012](ADR-012-compiled-screen-artifacts.md)
(a source compiles to a committed, byte-verified artifact and then to `constexpr` C++).

The scenario **format and compiler** (#319) and the governed data types were delivered first; the
**replay runner, its host trace and the monitor wiring** (#320) followed in a stacked PR and this
record now reflects both.

## Shared contract

No MedUI shared decision governs interaction scenarios. MedUI
[#16](https://github.com/Compliatory/MedUI/issues/16) / MEDUI-DEC-008 name interaction *profiles*
(`MEDUI-PROFILE-INTERACTION`, `-BINDING`, `-PRESENTATION`) but deliver no scenario schema and no
conformance corpus, and the pinned `v0.3.0-rc.1` checkout carries vectors no implementation claims
yet. So every type this record introduces begins in the `mdux.medui.scenario` module as
**implementation-local** vocabulary, no `medui-conformance.toml` key claims a shared scenario
profile, and a later reviewed migration maps these onto the canonical set if and when it is
delivered — the same stance [ADR-018](ADR-018-bounded-input-and-update-order.md) took for the event
vocabulary and [ADR-016](ADR-016-locally-versioned-observation-profiles.md) for the rendered
checks.

## Context

[ADR-018](ADR-018-bounded-input-and-update-order.md) fixed the bounded event vocabulary, the
overflow policy and the input → update → bind → render → capture order; #316 delivered `EventQueue`
and `FieldEditor`; #317 (ADR-019) the platform adapter; #318 assembled the loop as
`examples/support/MonitorApp.hpp`'s `updateMonitor()`, driven identically by an interactive window
and a deterministic `--headless-smoke`.

What does not exist is a **reviewed way to state an interaction and check its outcome**. A scenario
today would be a hand-written C++ test. Two failure modes follow, and the
[pinned behavior matrix](../parity/behavior-matrix.md) records that TrustSC already avoids both:

- **A parallel test state machine can pass while the real event ordering is wrong.** A test that
  re-implements "press then release" in its own loop verifies its own loop, not `updateMonitor()`.
- **Test input parsed on the device.** #17 cut on-device parsing for exactly this reason; a
  scenario script belongs on the host, compiled once, committed and byte-verified — like a
  `.medui` screen (ADR-011).

The forcing question: #320 (replay) and #321 (dynamic evidence) both consume "a scenario". The
format they share has to exist in one place, decided once, before either builds a consumer — the
stacked-delivery rule CONTRIBUTING § "Ordering within a chain" states.

## Medical Device Considerations

Impact: **potentially safety-relevant test-input compilation and behaviour verification**. This
record is a design decision plus a host compiler and a small module of pure, bounded types. It
performs no input, executes no action and renders no frame.

- **IEC 62304:2006 §5.7**: the software-system verification scope limit in
  `docs/iec62304/03-development-process.md` is unchanged — MduX has no assembled software system,
  and a replay of a bounded example is development-time evidence, not a verification, validation or
  release activity for one.
- **IEC 62366:2006 (usability)**: a scenario states an operator interaction — a press armed on one
  control and released on another, a refused character, a queue overflow — and checks that the
  application resolved it the way a usability review would expect. This record states the format so
  a review has something concrete to assess; it does not itself constitute that review.
- **Risk management**: no hazard, risk control or software safety class in this repository names
  the failure mode of a wrong-target activation, a swallowed critical press or a mis-ordered
  batch. This record does not invent one. The `requirement` references a scenario cites are
  engineering trace ids, not assigned hazards. **The host executes a critical action** (ADR-018
  clause 7); a replayed scenario records an `ActionTrace` and executes nothing.
- **Traceability**: a scenario carries `requirement` ids and, per obligation, a typed
  expected/observed pair, so a replay run leaves an ordered, traceable record. The
  cross-implementation half stays [#335](https://github.com/ambroise-leclerc/MduX/issues/335).
- **Cybersecurity**: unaffected. The host compiler parses untrusted `.scenario` text and may
  throw; the governed `mdux.medui.scenario` module imports `std` and the other governed medui
  modules only, allocates nothing and holds caller-owned spans.

No cross-implementation parity is claimed. This is not a certification, validation or
production-readiness claim.

## Decision

### 1. The `.scenario` format is a closed, line-oriented DSL

One directive per line (`#` to end of line is a comment, blank lines ignored), parsed by a small
hand-written parser like the `.medui` component-body parser — TrustSC's scenario shape, and the
"one property per line" convention that keeps a fixture portable. The directive set is closed:

| Directive | Meaning |
|---|---|
| `scenario <id>` / `screen <id>` / `version <N>` / `clock <YYYY-MM-DD> <HH:MM:SS>` | header, once each, before any step |
| `sample <beatPeriod> [<warmupFrames>]` | optional: how the replay seeds the demonstration ECG generator |
| `requirement <REQ-ID> ...` | optional, repeatable: engineering trace ids |
| `pointer <down\|up\|move\|cancel> [<node>]` | queue a `PointerEvent`; `<node>` is resolved to its rectangle's centre pixel by the compiler |
| `key [<down\|up>] <KeyCode>` | queue a `KeyEvent` (ADR-018's `KeyCode` wire spellings) |
| `text <scalar>` / `text "<run>"` | queue one `TextEvent` per scalar |
| `focus <enter\|leave> <node>` | queue a `FocusEvent` |
| `advance [<N>]` | consume the queued batch as **one accepted batch**; run N application updates |
| `expect <kind> <args>` | check one typed fact about the state the preceding `advance` settled |
| `capture <name>` | the replay must hand this frame to its capture callback |

**Batching and settling (ADR-018 clause 6).** Events queued between two `advance` lines are one
batch. `advance N` runs N `updateMonitor()` calls (the first consumes the batch, the rest run
empty). An `expect` or `capture` must directly follow an `advance` (or another `expect` / `capture`)
and checks the state **that advance settled** — state bound after the batch, so a check never sees
unresolved input. Refused (`SCN008`, and structurally by `CompiledScenario::validate()`): events
after the last `advance`; an `expect` or `capture` before the first `advance`; an `expect` or
`capture` wedged between queued events and their `advance` (it would check state a pending batch
will still change); a scenario with no `advance` at all.

**The typed expectation vocabulary.** `clock`, `field "<value>" [caret N]`, `refused N`,
`action <node> <SystemEvent> <REQ-ID>`, `button <node> <source>`, `reading <node> <int>`,
`state <node> <index>`, `latch <node|->`, `frame <stat> <N>`, `overflow <true|false>`. Each is a
governed fact (`FieldEditor` value, `PressLatch` state, `ActionTrace`, `FrameStats`) or a generic
named numeric/state slot the replay fills. `button <node> <source>` checks **both** the control and
its `source`, so a press on a different control does not satisfy it; `button <node> ""` (empty
`source`) asserts that no ordinary `Button` press resolved this batch.

**Bounds.** `maxScenarioSteps = 256`, `maxScenarioExpectations = 128`, `maxScenarioCaptures = 16`,
`maxScenarioRequirements = 16` — all `inline constexpr` in `mdux.medui.scenario`, `maxInputEvents`'s
reasoning applied to the step list.

**Identity.** `scenario <id>` is a lowercase slug (`[a-z0-9-]`, no leading or trailing `-`),
matching the recipe `id` and the screen id convention. The `constexpr` emitter maps every
non-alphanumeric character to `_` to form a C++ identifier, so constraining the source id at the
door (`SCN002`) keeps that map injective and no two scenarios can render the same module name.
`version <N>` must be the one the build implements — an unsupported version is refused both by the
parser (`SCN004`) and, defensively, when the emitter reads `scenario.json` back.

### 2. A scenario compiles to a committed, byte-verified artifact and then to `constexpr` C++

`mdux-scenariobake` speaks the shared `bake` / `verify` grammar, so `mdux_bake_artifact(KIND
scenario …)` registers it exactly as it registers a `.medui` screen. It reads the committed
**screen** `package.json` the recipe's `screen` key names — passed as a SOURCE — to resolve every
`pointer <node>` directive and to reject a directive naming a node the screen does not carry.
Output: `generated/scenario/<id>/{scenario.json, report.json}`, committed and byte-compared by
`ctest -L evidence` (`evidence.scenario.<id>`). `scenario.json` is canonical — no timestamps, no
absolute paths, scalars as code-point integers never as text (ADR-007).

`mdux-scenarioemit` renders `scenario.json` into `mdux.medui.generated.scenario_<id>` — a module
and a header, build-tree only, never committed, regenerated every build (ADR-012 decision 3). The
generated source carries `static_assert(scenario.validate().has_value())`, so a scenario that does
not satisfy `mdux.medui.scenario` is a **build failure in whatever links it**.

Re-laying-out a screen re-bakes every scenario scripted against it: `report.json` records the
screen package's digest as an input, and the fixpoint discipline the text → screen chain already
uses applies.

### 3. Replay is governed and bounded; the trace is host-side

`ScenarioRunner` (in `mdux.medui.scenario`, `constexpr`/`noexcept`/allocation-free, caller-owned
storage) drives a compiled scenario through the exact interfaces `updateMonitor()` uses:

1. `loadNextBatch(EventQueue&)` fills the caller's queue with the current batch's events. A batch
   larger than the caller's queue is a **failure** (`queueTooSmall` → `QueueTooSmall`), not a
   silent truncation.
2. The caller runs its own `updateMonitor()` `framesThisAdvance()` times.
3. `observe(ScenarioObservation)` checks every `Expect` step for the settled frame against a plain
   struct the caller filled — clock, field value/caret, refused count, `ActionTrace`, latch node,
   `FrameStats`, and generic `{node, value}` reading/state spans. All caller-owned; no allocation.
4. `capturesThisFrame()` names the `Capture` markers due; the caller invokes its capture callback
   and calls `markCaptured(name)` **only once the callback has run**. A marker the caller cannot
   honour (no callback) is left outstanding, and a declared capture the replay never marks —
   whether unreached or unhonoured — is a **failure** (`CaptureNotInvoked`).

`report()` returns a per-step held/failed record. **Queue overflow, a rejected event, an unknown
target and a missing capture each fail the run.** The allocating step-by-step expected/observed
**trace** is built host-side by `mdux.tools.scenario.trace` from that report — separate from the
governed runner, per #320's acceptance. The trace is derived and uncommitted (ADR-014 D4 stance);
#321 gates it and the dynamic-frame captures.

Determinism: the same batches and the same `pinnedClock` produce the same frames (PAR-REQ-005's
"identical replay snapshot"); replay injects time (PAR-REQ-008). The replay runs **without a GPU** —
`updateMonitor()`, the binding assembly and `render()` into a `DrawList` need no device; the
capture callback is the caller's, and can be an offscreen render or a counter.

### 4. Trust zones

`mdux.tools.scenario*` (parser, baker, emitter) are host-tools-zone: they parse untrusted input,
may throw, and are never linked into `MduXCore` or `MduX` — `mdux_verify_trust_zones()` is
unaffected, and the `scenario_spec` link line (`MduX::Core` only) is the standing evidence, like
`medui_spec`. `mdux.medui.scenario` is governed: `std` and the other governed medui modules only,
all `constexpr`, all caller-owned spans. It does **not** import `mdux.medui.screen` — node ids are
bare `string_view` — so the module graph stays acyclic.

## Alternatives Considered

- **A TOML recipe carrying the step stream as parallel arrays.** Rejected: `mdux.tools.toml` has
  no array-of-tables, so an ordered heterogeneous script (event, advance, expect, capture) would
  be several positional column-arrays a reviewer cannot follow. The line-oriented DSL is one more
  small parser and reads as a script.
- **Keep scenarios as hand-written C++ tests.** Rejected: that is the parallel-state-machine
  failure this record exists to prevent, and it is not a reviewed, byte-verified artifact.
- **Parse the `.scenario` on the device.** Rejected outright — #17 and ADR-010's doctrine. The
  device holds `constexpr` data reached by name and parses nothing.
- **One ADR for #319 and a second for #320.** Rejected: the format and the replay contract
  constrain each other (the settling semantics, the expectation vocabulary, the capture model),
  and ADR-018 already set the precedent of covering the input vocabulary and the update order in
  one record.
- **Let the governed runner build the trace.** Rejected: the trace allocates (a growing
  expected/observed document) and is host-only evidence. #320's acceptance separates them
  explicitly; the governed runner records a bounded per-step outcome and nothing else.
- **A `std::variant` `ScenarioStep`.** Rejected: a variant-of-variant (the event alternative is
  itself `mdux.medui.input`'s `InputEvent` variant) is what an emitter renders as deeply nested
  initialisers. A flat tagged aggregate is `mdux.evidence.json::Value`'s choice, for its reason.

## Consequences

### Positive

- One decided scenario format, in one governed module, before two consumers (#320, #321) need it.
- A scenario is a committed, byte-verified, reviewable artifact — a `.scenario` diff a reviewer
  reads, not a C++ test whose loop they have to trust.
- The replay drives the real `EventQueue` / `PressLatch` / `FieldEditor` / `render()` path, so a
  mis-ordered batch or a swallowed release fails a scenario rather than passing a parallel loop.
- The governed runner allocates nothing (`scenario_noheap_spec`), and the trace that allocates is
  outside it.

### Negative

- A reader of the interaction subsystem now follows four records: ADR-018 (the contract), ADR-019
  (the adapter), ADR-018 clause 6 as delivered by #318 (`MonitorApp.hpp`), and this. Mitigation:
  this record builds on rather than restates them, and cites the specific clauses.
- The `.scenario` DSL is a new surface with its own `SCN0NN` diagnostic codes. Mitigation: the
  directive set is closed and small, every rejection has a stable code and a test, and the format
  is documented in `ScenarioScript.cppm` and here.
- `ScenarioObservation` carries a few monitor-shaped slots (`readings`, `states`) as generic
  `{node, value}` spans. Mitigation: they are generic already; a second application fills them
  from its own state without the runner changing.

### Risks

- **The format is read as an accepted shared profile.** Mitigation: the Shared-contract section
  and this record's status both say implementation-local; no `medui-conformance.toml` key claims a
  scenario profile.
- **A scenario drifts from the screen it names.** Mitigation: the screen package digest is a
  recorded input, so a screen re-layout fails `evidence.scenario.<id>` until the scenario is
  re-baked — the same guard the text → screen chain has.
- **The dispositions are read as a completed safety review.** Mitigation: PAR-REQ-006's
  host-execution / audit policy stays open for domain review, and this record's Medical Device
  Considerations say the replay executes nothing.

## Implementation Notes

- `include/mdux/medui/Scenario.cppm` (`mdux.medui.scenario`, in `MduXCore`, header-only, no `src/`
  file): `ExpectKind` / `FrameStatField` / `StepKind` + wire spellings, `Expectation`,
  `ScenarioStep`, `ScenarioSampleSeed`, `CompiledScenario` + `validate()`, the bounds, the
  `ScenarioError` enum, and (added by #320) `ScenarioRunner` / `ScenarioObservation` /
  `FrameCounts` / `StepOutcome` / `ReplayReport` — the bounded, allocation-free replay driver.
- `tools/scenario/`: `ScenarioScript.{cppm,cpp}` (the line parser, `SCN001`–`SCN008`),
  `Scenario.{cppm,cpp}` (recipe + `run` / `write` / `verify` + `readScreenNodes` + `readScenarioDoc`,
  `SCN020`+), `ScenarioEmit.{cppm,cpp}` (`SCE0NN`), the two thin mains, and (added by #320)
  `ScenarioTrace.{cppm,cpp}` (`mdux.tools.scenario.trace`: `renderTraceText(CompiledScenario,
  ReplayReport)` → the deterministic step-by-step expected/observed trace). `MduXScenarioLib` links
  `MduX::ToolsCommon`.
- `examples/support/ScenarioReplay.hpp` (#320): `replayMonitorScenario()` — the `updateMonitor()`
  glue that fills the `EventQueue` from a `ScenarioRunner`, builds a `ScenarioObservation` from the
  settled `DemoState` / `PressLatch` / `MonitorUpdateOutcome`, and invokes the caller's capture
  callback per marker. `MedicalScreenMonitorExample --replay=<id>` renders one offscreen frame per
  capture; registered as `example.monitor.replay`.
- `cmake/MduXScenarioEmit.cmake` (`mdux_emit_scenario_package()`, mirrors `MduXScreenEmit.cmake`);
  the `mdux_bake_artifact(KIND scenario …)` + `mdux_emit_scenario_package(…)` calls in the root
  `CMakeLists.txt`; `docs/recipes/scenario.schema.json` + the `RECIPE_SCHEMAS` entry in
  `tools/docs-lint/check_schema_type_drift.py`.
- `recipes/scenario/endoscope-monitor-basics.toml` + `…/basics.scenario` →
  `generated/scenario/endoscope-monitor-basics/`. Negative `.scenario` fixtures live in the tests.
- `tests/scenario/`: `ScenarioScriptTests.cpp` → `scenario_tools_spec` (every `SCN0NN`, two-bake
  byte equality, emit round-trip); `GeneratedScenarioTests.cpp` → `scenario_spec` (`MduX::Core` +
  the generated module); `ScenarioNoHeapTests.cpp` → `scenario_noheap_spec`. #320 adds
  `ReplayTests.cpp`.
- `docs/architecture.md`, `docs/roadmap.md` (#309), `docs/parity/requirements.md` (PAR-REQ-005/008
  columns, the #319/#320 gate rows), `docs/adr/README.md` (index ADR-020; next free is ADR-021).

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) — the governed zone
- [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) — Result-returning, `noexcept`
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md) — canonical form, byte-identity, bake reports
- [ADR-010](ADR-010-no-on-device-text-shaping.md) — no on-device parsing doctrine
- [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) / [ADR-012](ADR-012-compiled-screen-artifacts.md)
  — a source compiles to a committed artifact and then to `constexpr` C++
- [ADR-016](ADR-016-locally-versioned-observation-profiles.md) / [ADR-018](ADR-018-bounded-input-and-update-order.md)
  — the implementation-local stance this mirrors
- [ADR-019](ADR-019-windowed-presentation-and-input-adapter.md) — the adapter the monitor replay reuses
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-005, PAR-REQ-008
- [Pinned behavior matrix](../parity/behavior-matrix.md) — the scenario-replay comparison
- Issues [#309](https://github.com/ambroise-leclerc/MduX/issues/309),
  [#319](https://github.com/ambroise-leclerc/MduX/issues/319),
  [#320](https://github.com/ambroise-leclerc/MduX/issues/320)
- `include/mdux/medui/Scenario.cppm`, `tools/scenario/`,
  `examples/support/MonitorApp.hpp` (the `updateMonitor()` the replay drives)

## Approval

- **Proposal date**: 2026-09-09
- **Decision date**: 2026-09-09
- **Approved by**: Ambroise Leclerc, maintainer, by explicit instruction to accept ADR-020 and the
  scenario format, artifact ownership and replay contract it fixes, under #319 and #320.
- **Dispositions**: **PAR-REQ-005 Accepted** (recorded order and identical replay snapshot: the
  same batches and the same pinned clock produce the same frames — the scenario carries the batches
  and `pinnedClock`, and #320's `ScenarioRunner` is deterministic; queue overflow fails the run).
  **PAR-REQ-008 Accepted** (replay injects time; state order, number format — the `NumericDisplay`
  `template:` — and trace order — the ring's `oldest`/`count` — are explicit). The
  cross-implementation half stays [#335](https://github.com/ambroise-leclerc/MduX/issues/335); the
  viewport row/bin half stays [#322](https://github.com/ambroise-leclerc/MduX/issues/322).
- **Still open**: the critical-action host execution / audit policy (PAR-REQ-006, domain review);
  the dynamic-frame evidence gate with committed capture digests (#321). PAR-REQ-009/010 keep
  their own status. The replay runner, its host trace and the monitor `--replay` wiring were
  delivered by #320.
- **Scope**: the `.scenario` format, its diagnostics, the committed artifact and its ownership, the
  `constexpr` emit, the governed bounded data types, and the replay contract (batching/settling,
  deterministic advancement, expected/observed traces, capture invocation, no-alloc governed path).
  Windowing, native capture and host action execution stay implementation/host decisions. No
  scenario profile assigns a device's safety class.
