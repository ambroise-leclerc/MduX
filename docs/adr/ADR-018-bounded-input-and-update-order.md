# ADR-018: Bounded input, application update order and action policy

## Status

**Accepted**, 2026-09-08, for #315, by maintainer instruction. Acceptance establishes MduX's
local contract for input events, the input → application update → render order, bounded keyboard
editing and the policy around a critical action such as `SystemEvent::TriggerHalt`. It changes no
existing runtime behavior, no compiled artifact, no committed evidence and no shared pin, and
supersedes no earlier ADR. The bounded event queue body and the text-editing mutation were
specified here and delivered by [#316](https://github.com/ambroise-leclerc/MduX/issues/316) as
`EventQueue` and `FieldEditor`; the concrete platform adapter is
[#317](https://github.com/ambroise-leclerc/MduX/issues/317). The host-side execution and audit
policy for a critical action stays open for domain review, and is recorded as such in
[`docs/parity/requirements.md`](../parity/requirements.md).

## Shared contract

No MedUI shared decision governs interaction. MedUI
[#16](https://github.com/Compliatory/MedUI/issues/16) and its accepted direction
[MEDUI-DEC-008](https://github.com/Compliatory/MedUI/blob/80961fd7274992d75c8f86016be04b177c581857/decisions/MEDUI-DEC-008-interaction-profiles.md)
agree that logical events, update order and source-to-node binding should be separately named and
versioned observation profiles, but deliver no identifiers, no schema and no conformance corpus —
the pinned `v0.3.0-rc.1` checkout carries `MEDUI-PROFILE-INTERACTION`, `-BINDING` and
`-PRESENTATION` vectors that no implementation claims yet. So every type this record introduces
begins in the `mdux.medui.input` module as an **implementation-local** vocabulary, no
`medui-conformance.toml` key claims a shared interaction profile, and a later reviewed migration
maps these onto the canonical set if and when it is delivered — the same stance
[ADR-016](ADR-016-locally-versioned-observation-profiles.md) takes for the rendered checks.
[ADR-015](ADR-015-versioned-sibling-observations.md)'s decision **D5** is the accepted local
direction this record fills in; #315 is the issue D5 names as the one that "must finalize event
vocabulary, coordinate rounding and editing policy".

## Context

`mdux::medui::resolvePress()` (`src/medui/Screen.cpp`, since #261) resolves a surface coordinate
to the control under it — reverse paint order, half-open rectangles, every node opaque to a press
— and returns a `PressAction` carrying the node id, the traced `requirement`, and either a
`Button`'s open `source` string or a `CriticalButton`'s closed `SystemEvent`. That is the whole
of the interaction surface today. It neither queues a platform event, nor tracks which control a
press armed, nor knows about release or cancellation, nor touches text. `TextInputBinding` (#260)
carries a value and a caret **to display** — the host does the editing and hands MduX the result.

The [pinned behavior matrix](../parity/behavior-matrix.md) records that the Rust sibling TrustSC
already has the pieces MduX lacks: a bounded event queue that drops the newest event on overflow
and saturates a drop counter, a press/arm/activate-on-release model over per-target button lists,
separately managed text focus, and a text model that reserves its storage at construction.
MduX's occlusion rule is different (all-node reverse-order, not per-list), its storage ownership
is caller-side by doctrine (ADR-004), and copying TrustSC's model wholesale would silently change
both. The parity work therefore recorded ten prospective requirements; PAR-REQ-004 through
PAR-REQ-008 are the input ones, and they were left `Proposed / unreviewed` pending exactly the
design this record makes.

The forcing question: before #316 implements a queue, #317 an adapter, and #319/#320 a replay
harness, the vocabulary those three share has to exist in one place, decided once. Two branches
each defining "the same" event type is the failure CONTRIBUTING § "Stacked delivery" exists to
prevent.

## Medical Device Considerations

Impact: **potentially safety-relevant interaction and action policy**. This record is a design
decision and a small module of pure, testable types; it performs no input, executes no action and
renders no frame.

- **IEC 62304:2006 §5.7**: the software-system verification scope limit in
  `docs/iec62304/03-development-process.md` is unchanged — MduX has no assembled software system,
  and nothing here is a verification, validation or release activity for one. The
  `InputContractTests` suite is development-time evidence for the pure contract pieces.
- **IEC 62366:2006 (usability)**: press-arms / release-activates-same-target, and cancellation on
  focus loss or a lost event, are the interaction rules that keep an operator from activating a
  control they did not mean to. This record states them so a usability review has something
  concrete to assess; it does not itself constitute that review.
- **Risk management**: no hazard, risk control or software safety class in this repository names
  the failure mode of a wrong-target activation or a swallowed critical press. This record does
  not invent one. The failure descriptions in the requirements table remain engineering concerns.
  The **host executes** a critical action, so the device-level risk control for an orderly stop
  is the integrating manufacturer's to place — this record draws the boundary so that placement
  is possible, and deliberately does not hard-wire a clinical behavior from the name `TriggerHalt`.
- **Traceability**: PAR-REQ-004–008 gain a link from their disposition to this record and to the
  specific `InputContractTests` scenarios that exercise the pure clauses.
- **Cybersecurity**: unaffected. All storage is caller-owned and bounded; the governed module
  imports `std` only.

No cross-implementation parity is claimed. This is not a certification, validation or
production-readiness claim.

## Decision

The `mdux.medui.input` module (`include/mdux/medui/Input.cppm`, in `MduXCore`, `std`-only) is the
one home for the vocabulary below. The clauses are numbered because the requirement dispositions
cite them.

### 1. The event vocabulary is closed

Four event kinds cross the boundary from a platform adapter into governed code:

| Type | Members | Meaning |
|---|---|---|
| `PointerEvent` | `kind` ∈ {`Down`, `Up`, `Move`, `Cancel`}, `x`, `y` (`core::Px`) | one pointer transition or motion, already in authored surface coordinates (clause 3) |
| `KeyEvent` | `kind` ∈ {`Down`, `Up`}, `key` (`KeyCode`) | one physical key transition |
| `TextEvent` | `scalar` (`char32_t`) | one committed Unicode scalar the platform's keymap/IME already resolved |
| `FocusEvent` | `kind` ∈ {`Enter`, `Leave`}, `nodeId` (`std::string_view`) | the focused control changed |

Each `enum class` has an `Unspecified` member as its zero value, never valid in a delivered
event, and each has `toWire` / `fromWire` contract spellings under the same rule
`toWire(SystemEvent)` already follows: an out-of-set spelling — `""` included — yields no member,
so a missing field is refused rather than read as the default and caught later.

`TextEvent` carries a **scalar the platform already produced**. MduX performs no keymap lookup, no
dead-key composition and no shaping (ADR-010). A `KeyEvent` is for non-text keys — caret motion,
delete, focus traversal — and a `TextEvent` for the character itself; a platform that delivers
both for one keystroke is expected and each is handled on its own terms.

**Explicitly deferred**, each to be added as a new member or type under this module's own
versioning when a consumer needs it: IME composition strings and candidate lists, scroll and
wheel, multi-touch and gestures, hover / pointer-enter styling, and host key-repeat synthesis
(a repeat is whatever the platform chooses to deliver; MduX does not manufacture one).

### 2. The event queue is caller-owned, bounded, and drops the newest on overflow

The queue is a ring over storage the caller allocated once — `maxInputEvents` is the named
default capacity, and a caller may size its own. On overflow `push()` returns `DroppedNewest`:
the queue **drops the newest event**, increments a **saturating** `droppedCount`, and the caller
**cancels any pending armed activation** (clause 4) on that signal — a dropped event means the
stream is no longer a complete record of what the operator did, and a press waiting for its
release must not be allowed to activate against an incomplete stream. One application update
`pop()`s the queue to empty — **one** accepted batch (clause 6). The queue is allocation-free and
`noexcept`.

**#316 delivered this as `EventQueue`** — a `constexpr` class over a `std::span<InputEvent>`, with
`push()` / `pop()` / `clear()` and `size` / `capacity` / `empty` / `full` / `droppedCount`
accessors.

Dropping the newest rather than the oldest is deliberate and matches TrustSC: the oldest events
are the ones whose consequences (an arm, a caret move) the application has the best chance of
having already acted on, and discarding them would desynchronise application state from what the
operator last saw. Discarding the newest loses the least-integrated input and is observable
through `droppedCount`.

### 3. Coordinates are normalized once, by the adapter, flooring toward −∞

A platform delivers pointer coordinates in physical device pixels. The **adapter** maps them to
authored surface pixels exactly once, before any governed code sees them, using
`normalizeSurfacePoint(physX, physY, scaleNum, scaleDen, originX, originY)` — a pure `constexpr`
function in this module. The rule is **floor toward −∞** (`⌊(phys − origin) · num / den⌋`), not a
truncating cast.

Floor is the rule that agrees with the half-open `[x, x + w)` rectangle `core::Rect::contains`
and `resolvePress()` already use: every physical point falls in exactly one authored cell, the
cell boundary belongs to the cell on its `+` side, and there is no double-wide cell straddling
zero. A truncating cast (`static_cast<Px>`) rounds toward zero, so physical rows −0.5 and +0.5
would both land on authored row 0 — harmless while authored coordinates are non-negative, and a
latent wrong-target bug the moment an out-of-flow `position:` puts a control at a negative
coordinate, which the schema permits (`core::Px` is signed for exactly that reason).

The function **fails closed rather than wrapping**. Physical coordinates and the origin are
`core::Px`, and each scale term is bounded to `[1, maxCoordinateScale]` (`MalformedScale`
otherwise), which keeps every intermediate inside `std::int64_t`. A normalized coordinate that
does not fit back in `core::Px` is `CoordinateOutOfRange`, not a value silently truncated by the
narrowing conversion — a wrapped coordinate could land inside a real control's rectangle, which is
the wrong-target failure this whole clause exists to prevent.

Governed code works only in integer authored `core::Px`. It never sees a physical coordinate, a
scale factor or a sub-pixel value.

### 4. Press arms a target; release activates only the same target

Layered on `resolvePress()`, which is unchanged:

- A `PointerEvent::Down` resolves a target. If it resolves to an eligible control, that control's
  node id is **armed**.
- A `PointerEvent::Up` resolves a target. It **activates** — yields the `PressAction` for the
  operator to act on — only if it resolves to the **same** armed node id.
- A `PointerEvent::Cancel`, a `FocusEvent::Leave` for the armed control, the armed control no
  longer being present in the screen, and a queue overflow (clause 2) each **disarm**. A release
  after a disarm is a no-op, not an error.

This is modeled as `PressLatch` — a value type holding at most one armed `std::string_view`
nodeId, with `arm()`, `release()` (returning `bool`; `true` = activate, `false` = nothing armed
or a different target, and the latch is disarmed either way) and `cancel()`. A release is never an
error: cancellation is an expected outcome, so a stray release is a no-op, not a refusal. It is
pure and implemented in this PR: it holds the state machine, and the caller wires `resolvePress()`
to it. It stores a `string_view`, so a caller that rebuilds its screen storage must `cancel()`
across that boundary — stated in the module and checked by a test.

Half-open rectangles, reverse paint order and every-node occlusion are `resolvePress()`'s
existing rules and are not restated in a second place.

### 5. Keyboard editing is bounded, scalar-indexed, and never partially applied

The caret and every edit position are indexed in **Unicode scalar boundaries** over the closed
interval `[0, length]`. The edit vocabulary is `EditOp` ∈ {`InsertScalar(char32_t)`,
`DeleteBack`, `DeleteForward`, `MoveCaret(to)`}. An edit is bounded by the node's `max_length` and
by the **same two charset questions `mdux.medui.field` asks, in the same order** (#297): first the
font package's `restrictedCharset` — the set it *can draw*, a scalar outside it is
`ScalarNotInFont` (the physical limit, no glyph, no fallback) — then the node's narrowed
`charsetRanges` via `mdux::medui::admits()`, reused not reimplemented, a scalar outside that is
`ScalarNotPermitted` (the policy limit). Asking only the node's set would accept a scalar the
committed font has no glyph for, and the caller would commit an edit that `recordField()` then
rejects with `GlyphNotInPackage`. A rejected edit — either charset bound, an insertion into a full
field, a caret moved out of range — produces **no partial mutation**: the value and caret the
caller holds are untouched and an `InputError` is returned.

`editWouldBeAccepted(scalar, fontCharset, nodeCharset, currentLen, maxLen)` — the pure predicate,
both charset bounds plus capacity, no mutation — was delivered by #315.

**#316 delivered the mutation as `FieldEditor`**: a class over a caller-owned `std::span<char32_t>`
buffer, holding the length and the caret (`std::optional`, `nullopt` = not being edited, which is
exactly `TextInputSlot::caret`'s no-caret state). `create()` validates the **whole** initial value
against both charsets and `max_length` before it writes anything, so a rejected `create()` leaves
the caller's buffer intact, and it copies with an overlap-safe `std::memmove` so `initial` may be
a view of `storage` the caller is adopting in place; `apply(EditOp)` runs `editWouldBeAccepted`
then shifts scalars inside the caller's buffer, all-or-nothing; `focus(FocusEvent)` starts and
ends editing, ignoring an event for another node; `handleKey` / `handleText` route the contract's
events to edits and report a non-edit key (`Commit`, `Cancel`, focus traversal) as not-consumed
rather than as an error. It builds no `TextInputSlot` — that type is `mdux.medui.screen`'s — but
exposes `nodeId()` / `value()` / `caret()` for the caller to assemble one per frame. There is no
on-device text shaping and no unbounded storage (ADR-010, ADR-004).

### 6. One update consumes one batch, in a fixed order

The application update order, as a contract for #316 / #319 / #320 to implement and test:

1. Consume exactly one accepted event batch from the queue.
2. Apply its activations and edits to **caller-owned** application state.
3. Bind exactly one snapshot of that state (`TextBinding`, `SignalBinding`, … as today).
4. `render()` the frame.
5. Capture, if this is a verified run.

State bound in step 3 is the state after step 2, so a capture never shows input from a frame the
operator has not seen resolved, and a replay that injects the same batches and the same time
(`SignalBinding` already takes injected time) produces the same frames. #316 delivered the parts
(`EventQueue::pop` for step 1, `FieldEditor` for step 2, the existing bindings for step 3); the
assembled loop wired to real Vulkan presentation is #318 and its deterministic replay is #320.

### 7. MduX resolves a critical action and traces it; the host executes it

On a critical press, `resolvePress()` already yields `{nodeId, requirement, event}` with `event`
a member of the closed `SystemEvent` set, and already fails closed on a `CriticalButton` that
names an unimplemented event (`ScreenError::UnimplementedEvent`) or declares no requirement
(`ScreenError::UntracedCriticalControl`). This record adds one thing: an `ActionTrace{nodeId,
requirement, event, sequence}` value, with `sequence` a monotonic counter the caller advances, so
a host acting on a critical press holds an ordered, traceable record of it.

**The host executes the action.** MduX asserts nothing about what `TriggerHalt` does to a device —
it is the request for the host's halt path, and the orderly-stop behavior, its timing and its
audit persistence are the host's and the integrating manufacturer's. `NoOp` stays a distinct
member so "recorded and nothing else" is expressible without overloading `TriggerHalt`. "Orderly
stop is tested" means, at this contract's level: the resolved action carries the requirement id,
`NoOp` and `TriggerHalt` are distinguishable, and an unknown or untraced critical press is
refused rather than silently swallowed — all checkable in `InputContractTests` and
`ButtonTests` today.

### 8. Platform capture and windowing are outside the governed zone

Native event capture, the physical→authored scale factor and its source, window and surface
lifecycle, and pointer-capture / grab semantics live in the **un-governed adapter zone** (ADR-004).
The governed `mdux.medui.input` module receives only the normalized vocabulary of clause 1.
Initial adapter scope (#317) is a pointer and a physical keyboard; everything in clause 1's
deferred list is out.

## Alternatives Considered

- **Adopt TrustSC's interaction model directly** — per-target button lists, separately managed
  focus, storage reserved at construction. Rejected: MduX's occlusion is all-node reverse-order,
  not per-list, and its storage is caller-owned by ADR-004 doctrine. Matching names over
  different guarantees is the divergence the parity programme exists to prevent.
- **Truncate-toward-zero coordinate rounding** (a plain `static_cast<Px>`). Rejected: asymmetric
  across zero, so two physical rows map to authored row 0 the moment a control sits at a negative
  authored coordinate — a latent wrong-target activation. Floor is one extra branch and agrees
  with the half-open convention every other part of the layout uses.
- **Round-half-to-even (nearest-cell) rounding.** Rejected: centers a sample on a cell rather
  than treating the cell as the half-open interval `resolvePress()` already treats it as; it
  would make the hit rule inconsistent with itself at cell boundaries.
- **Let MduX execute `TriggerHalt`.** Rejected outright: the issue text and #17 both name this —
  a screen that can name any system event can name one nothing implements, and a library that
  acted on the name would be turning an example screen's label into an unreviewed device control.
  The host executes; MduX resolves and traces.
- **A heap-allocated or globally-owned event queue.** Rejected: ADR-004's bounded caller-owned
  storage doctrine, and `governed.noThrow` / `screen.noheap` would reject it anyway.
- **Implement the `EventQueue` and `applyEdit` bodies in this PR.** Rejected: that is #316's
  named deliverable, and a stacked chain reads better when the "define" PR is the vocabulary and
  the pure pieces and the "implement" PR is the behavior. A half-filled governed module also
  trips the no-heap / no-throw symbol scans for no gain.
- **Keep it all in prose (no module).** Rejected: #316, #317 and #320 would each re-derive the
  event types, which is precisely the Wave-2 failure CONTRIBUTING § "Stacked delivery" records.

## Consequences

### Positive

- One decided vocabulary for input, in one governed module, before three consumers need it.
- PAR-REQ-004–008 move from `Proposed / unreviewed` to ratified dispositions, each tied to a
  clause here and to `InputContractTests`.
- The pure pieces — coordinate normalization, the press latch, the edit predicate — were
  implemented and tested by #315, so #316's work was the ring buffer (`EventQueue`) and the text
  mutation (`FieldEditor`) against fixed types, not the rules.
- The host-executes-`TriggerHalt` boundary is written down where a domain reviewer will find it.

### Negative

- A reader of the input subsystem now has two PRs to follow (#315 defines, #316 implements) and
  an ADR between them. Mitigation: this ADR is the single reference and #316 restates nothing.
- Clause 6 (the update order) is prose, not one assembled function, until #318 wires the loop and
  #320 its replay. #316 delivered the pieces it names but not their composition. Mitigation: those
  issues name this clause as their contract, and a mismatch is a review finding on them.
- `KeyCode` starts as a minimal enum (caret motion, delete, tab, enter, escape). A real adapter
  will need to extend it. Mitigation: it is implementation-local and versioned by this module;
  extending an enum is additive.

### Risks

- **The dispositions are read as a completed safety review.** Mitigation: PAR-REQ-006 keeps an
  explicit open "host execution / audit policy — domain review" note, and this record's status
  and Medical Device Considerations both say the host owns the action.
- **`PressLatch` holds a `string_view` into caller storage that is rebuilt out from under it.**
  Mitigation: the module documents that a caller must `cancel()` across a screen-storage
  rebuild, and a test covers a release after the armed id's backing storage changed.

## Implementation Notes

- The module `include/mdux/medui/Input.cppm` (`mdux.medui.input`) stays header-only like
  `mdux.medui.schema`: `EventQueue`, `FieldEditor` and the rest are `inline` with state in
  caller-owned spans, and all but `FieldEditor::create()` (one overlap-safe `std::memmove`) is
  `constexpr`. No `src/` file and no `PRIVATE` source entry.
- Imports `std`, `mdux.core.units`, `mdux.core.result`, `mdux.medui.schema`,
  `mdux.medui.field`, `mdux.font.schema`. It does **not** import `mdux.medui.screen`, so the
  module graph stays acyclic; the armed target is a bare node-id `string_view` and `FieldEditor`
  exposes `value()`/`caret()`/`nodeId()` for the caller to build a `TextInputSlot`.
- `tests/medui/InputContractTests.cpp` (in `medui_spec`, links `MduX::Core` only) covers the wire
  round-trips and closed sets; `normalizeSurfacePoint` at the boundaries, an oversized scale and an
  out-of-`Px` result; `PressLatch`; `editWouldBeAccepted` against both charsets; **`EventQueue`**
  FIFO order, drop-newest overflow with a saturating counter, ring wrap, `clear()` and a
  zero-capacity queue; **`FieldEditor`** create-time refusals, insert/backspace/delete-forward/caret
  moves, unchanged state on every refusal, wrong-node focus ignored, and `handleKey`/`handleText`
  routing. `tests/medui/InputNoHeapTests.cpp` in a new `input_noheap_spec` binary proves the queue
  and the editor allocate nothing after construction, on the accepted and the refused paths (its
  own binary because `CountingAllocations.hpp` may be included by one TU per binary).
- No `MDX-E` diagnostic, no `medui-conformance.toml`, no compiled-screen schema and no baked
  artifact changes. `docs/architecture.md`'s module table and `docs/roadmap.md`'s #308 section
  gain the module; `docs/parity/requirements.md` and `docs/parity/behavior-matrix.md` record the
  dispositions.
- `docs/adr/README.md` indexes this as ADR-018; the next free number becomes ADR-019.

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) — the governed zone (`std` only, no windowing)
- [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) — Result-returning, `noexcept`
- [ADR-010](ADR-010-no-on-device-text-shaping.md) — no on-device shaping; decision 4
- [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) — the compile boundary the runtime
  types sit behind
- [ADR-015](ADR-015-versioned-sibling-observations.md) — decision D5, the accepted local
  direction this record fills in
- [ADR-016](ADR-016-locally-versioned-observation-profiles.md) — the `mdux.local/*` /
  implementation-local stance this record mirrors for interaction
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-004–008
- [Pinned behavior matrix](../parity/behavior-matrix.md) — the interaction comparison
- [MEDUI-DEC-008](https://github.com/Compliatory/MedUI/blob/80961fd7274992d75c8f86016be04b177c581857/decisions/MEDUI-DEC-008-interaction-profiles.md),
  MedUI [#16](https://github.com/Compliatory/MedUI/issues/16)
- Issues [#308](https://github.com/ambroise-leclerc/MduX/issues/308),
  [#315](https://github.com/ambroise-leclerc/MduX/issues/315),
  [#316](https://github.com/ambroise-leclerc/MduX/issues/316),
  [#317](https://github.com/ambroise-leclerc/MduX/issues/317)
- `include/mdux/medui/Screen.cppm` (`resolvePress`, `PressAction`),
  `include/mdux/medui/Field.cppm` (`admits`), `src/medui/Screen.cpp`

## Approval

- **Proposal date**: 2026-09-08
- **Decision date**: 2026-09-08
- **Approved by**: Ambroise Leclerc, maintainer, by explicit instruction to accept ADR-018 and
  the PAR-REQ-004–008 dispositions it supports under #315.
- **Dispositions ratified**: PAR-REQ-004 **Accepted**; PAR-REQ-005 **Accepted with amendment**
  (`EventQueue` delivered by #316; the assembled batch-consuming update loop is a documented
  contract until #318/#320); PAR-REQ-006 **Accepted with a recorded limitation** (host execution
  and audit policy stay for domain review); PAR-REQ-007 **Accepted** (the edit mutation delivered
  by #316 as `FieldEditor`); PAR-REQ-008 **Accepted with amendment** (the viewport row/bin half
  stays with #322).
- **Still open**: the domain review of the critical-action host policy (PAR-REQ-006); the
  platform adapter (#317); the assembled update loop wired to Vulkan (#318) and its replay (#320);
  PAR-REQ-009/010 keep their own status.
- **Scope**: the input event vocabulary, coordinate-normalization rule, press/release/cancel
  model, bounded-editing contract, update order and critical-action boundary. Windowing,
  platform capture and host action execution stay implementation/host decisions. No interaction
  profile assigns a device's safety class or imports another language's memory-safety claims
  into C++.
