# ADR-019: The windowed presentation and input adapter

## Status

**Accepted**, 2026-09-09, for [#317](https://github.com/ambroise-leclerc/MduX/issues/317), by
maintainer instruction. Acceptance establishes where an optional windowed adapter that presents a
compiled MedUI screen and translates native pointer/keyboard input **lives**, what it owns, the
lifecycle outcomes it must make explicit, and the one governed helper it needs
(`mdux::medui::SurfaceMapping`). It changes no compiled artifact, no committed evidence and no
shared pin, and supersedes no earlier ADR. It refines [ADR-018](ADR-018-bounded-input-and-update-order.md)
clause 3 (coordinate normalization) and clause 8 (platform capture is outside the governed zone),
and it is the record ADR-018's "Still open — the platform adapter (#317)" line points forward to.
The assembled batch-consuming update loop wired to real presentation stays
[#318](https://github.com/ambroise-leclerc/MduX/issues/318) (ADR-018 clause 6); the
host-execution / audit policy for a critical action stays open for domain review (PAR-REQ-006).

## Shared contract

No MedUI shared decision governs windowing, native event capture or presentation. MedUI
[#16](https://github.com/Compliatory/MedUI/issues/16) / MEDUI-DEC-008 name interaction *profiles*
but deliver no adapter contract, and the pinned `v0.3.0-rc.1` checkout carries none. The adapter
this record describes is therefore **implementation-local and example-zone**: it claims no shared
profile, and the Rust sibling's `adapters/trustsc-vulkan-winit` crate is a reference for shape, not
a contract to match member-for-member (its window library, its lifecycle enum and its per-target
button lists are all its own — the same stance ADR-018 took for the input model).

## Context

`mdux.medui.input` (#315/#316) is complete: the closed `PointerEvent` / `KeyEvent` / `TextEvent` /
`FocusEvent` vocabulary, the caller-owned bounded `EventQueue`, the `PressLatch`, the `FieldEditor`,
`normalizeSurfacePoint()` and `ActionTrace`. `mdux.medui.screen` resolves a surface coordinate to a
control (`resolvePress()`) and records a frame (`render()`). `mdux.render.vulkan`'s `UiRenderer`
turns a governed `DrawList` into commands against a **caller-owned** device and render pass. What
does not exist is anything that *drives* those pieces from a real window: the only windowed example,
`examples/VulkanSCTriangleExample.cpp`, opens a GLFW window and a swapchain to draw a triangle, binds
no screen and translates no event.

Two hard boundaries constrain where that driver can go:

- **ADR-004 / ADR-018 clause 8.** Native event capture, the physical→authored scale factor and its
  source, and window/surface lifecycle are un-governed. `MduXCore` reaches `std` only; `MduX` (the
  adapter zone) may name Vulkan but has never linked a windowing library, and
  `mdux_verify_trust_zones()` (`cmake/MduXTrustZones.cmake`) fails the configure if `MduXCore` ever
  reaches `glfw` transitively. GLFW is an **examples-only** dependency today, stated in
  `examples/CMakeLists.txt`.
- **`UiRenderer`'s ownership contract.** `VulkanRenderContext` is borrowed, never owned. The
  adapter must supply an instance, device, queue, swapchain and render pass and keep owning them; it
  must not push that ownership into MduX.

So the driver cannot be one library module. It splits: a **pure governed helper** for the one thing
governed code legitimately needs (mapping a physical coordinate the same way the frame was drawn),
and an **example-zone adapter** for everything that needs a window — GLFW capture, the swapchain
lifecycle, the physical key table, the DPI source.

The forcing question this record answers: #318 (the monitor) and any later windowed example will
each need a window + swapchain + event-translation shell. Written once as a reusable
`examples/support/` header with explicit lifecycle outcomes, it is a shared starting point; rebuilt
per example, it is three subtly different lifecycles and three chances to get resize or teardown
ordering wrong.

## Medical Device Considerations

Impact: **potentially safety-relevant presentation, coordinate mapping and resource lifetime**. This
record is a boundary decision plus one small pure value type; it opens no window, executes no action
and makes no certification or production-readiness claim.

- **IEC 62304:2006 §5.7**: the software-system verification scope limit in
  `docs/iec62304/03-development-process.md` is unchanged — MduX has no assembled software system, and
  an example with a `--smoke-test` mode is development-time evidence, not a verification or release
  activity for one.
- **IEC 62366:2006 (usability)**: a press that resolves to the wrong control, or a control that is
  drawn somewhere other than where it is hit-tested, is the interaction hazard `resolvePress()` and
  the `PressLatch` exist to bound. This record adds the rule that keeps them aligned under a real
  window: coordinates are mapped **once, the same way the frame was scaled**, and the mapping is
  rebuilt — with the armed press cancelled — on every resize and DPI change. It states the rule so a
  usability review has something concrete to assess; it is not that review.
- **Risk management**: no hazard, risk control or software safety class in this repository names a
  wrong-target activation, a swallowed critical press, or a resource leak on window teardown. This
  record does not invent one. The lifecycle outcomes below are engineering concerns. The **host
  executes** a critical action (ADR-018 clause 7); the example only records an `ActionTrace` and
  executes nothing.
- **Traceability**: the adapter half of PAR-REQ-004 and PAR-REQ-005 gains a link from its
  disposition to this record, to `SurfaceMapping`'s `InputContractTests` scenarios, and to the
  `glfw_translation_spec` and `example.monitor.smoke` checks.
- **Cybersecurity**: unaffected. `SurfaceMapping` is `std`-only, caller-owned and bounded; GLFW and
  the swapchain stay in the example binary, absent from the install/export set.

No cross-implementation parity is claimed.

## Decision

### 1. The adapter is example-zone; the only governed addition is `SurfaceMapping`

`MduXCore` and `MduX` gain **no** windowing dependency. The windowed adapter — GLFW window and
callbacks, Vulkan instance/surface/device/swapchain lifecycle, the physical key table, the DPI
source — is a reusable header under `examples/support/`, compiled only into example binaries.
`mdux_verify_trust_zones()` continues to enforce the governed side.

The one piece governed code needs is `mdux::medui::SurfaceMapping`, added to `mdux.medui.input` in
the clause-3 section beside `normalizeSurfacePoint()` — header-only, `constexpr`, `noexcept`,
allocation-free, `std`-only, like the rest of that module. It is a value type: a uniform
authored-per-window-pixel ratio (`scaleNum / scaleDen`, each in `[1, maxCoordinateScale]`) and an
optional window-space origin. It does not "capture" anything — it is the arithmetic state an
adapter keeps so every pointer coordinate is transformed the same way the frame was drawn.

### 2. The screen is drawn 1:1 at the framebuffer origin; coordinates are mapped once, and the mapping is rebuilt on every resize and DPI change

`UiRenderer::record()` sets its own viewport to `{0, 0, context.viewport.w, context.viewport.h}`
and computes NDC from authored pixel coordinates against that viewport. It has no scale or offset
parameter, and #317 does not change its contract. So the example passes `context.viewport =
authoredSurface` and the compiled screen is drawn **at 1:1 authored-pixel scale, pinned to the
framebuffer origin** — a window larger than the surface shows clear colour around it, a window
smaller clips it (the render scissor bounds it). Authored pixels and framebuffer pixels coincide.

The only transform between a native pointer and the authored grid is therefore the display's
**device-pixel ratio** — framebuffer pixels per window pixel. `SurfaceMapping::create(framebuffer,
window)` takes that ratio from the framebuffer width over the window width and **requires the height
to reduce to the same fraction** — a real display's device-pixel ratio is uniform, and a
fractional or anamorphic one is out of initial adapter scope, so this fails closed
(`MalformedScale`) rather than picking an axis. `map(kind, windowX, windowY)` runs exactly one
`normalizeSurfacePoint()` pass (ADR-018 clause 3: floor toward −∞, fail-closed on an
out-of-`core::Px` result). A point off the authored surface is not `SurfaceMapping`'s concern —
`resolvePress()` returns nothing for it.

The adapter builds the mapping once at start-up and again on **every** framebuffer-resize and
content-scale change, from the current `glfwGetFramebufferSize` and window size, and
`PressLatch::cancel()`s across that boundary (ADR-018 clause 4 already lists a screen-storage
rebuild and overflow as disarm points; this adds the surface rebuild). A press is therefore always
hit-tested against the geometry the operator last saw. `glfwGetWindowContentScale` is advisory
only — the framebuffer extent is authoritative because it is the grid the swapchain and the
`UiRenderer` viewport actually use.

A presenter that instead scales the surface to fill the window (letterboxed) would need a
different `SurfaceMapping` factory and either a change to `UiRenderer` or a post-render blit;
that is deferred with #318.

### 3. The event-translation table is fixed and additive

The example-zone adapter translates:

| Native | Contract |
|---|---|
| cursor move / button down / button up / pointer leave-while-pressed | `PointerEvent` (`Move` / `Down` / `Up` / `Cancel`), after one `SurfaceMapping` pass |
| a physical non-text key press/release | `KeyEvent` with a `KeyCode` from the table below, or nothing |
| a committed character (GLFW char callback) | `TextEvent` carrying the scalar the platform's keymap/IME produced (ADR-010: no mapping, no composition) |
| the focused widget changed / the window lost focus | `FocusEvent` (`Enter` / `Leave`) |

The `KeyCode` table is exactly ADR-018's members and no more: Left/Right/Home/End → the four
`Caret*`; Backspace → `DeleteBack`, Delete → `DeleteForward`; Tab → `FocusNext`, Shift+Tab →
`FocusPrev`; Enter/Return → `Commit`; Escape → `Cancel`. Every other physical key maps to nothing
(the char callback carries the character). A platform that delivers both a `KeyEvent` and a
`TextEvent` for one keystroke is expected and each is handled on its own terms.

`EventQueue::push()` returning `DroppedNewest` is a `PressLatch::cancel()` signal (ADR-018 clause 2):
the stream is no longer a complete record, so a press waiting for its release must not activate.

### 4. Every lifecycle outcome is explicit — no silent degradation

| Outcome | Behaviour |
|---|---|
| **Start-up failure** | no GLFW, no window, no Vulkan instance / device / surface, or a swapchain that will not create → the example exits **non-zero** with a message that names the failing step. Never a reduced-function run. |
| **Resize / swapchain recreation** | `vkDeviceWaitIdle`, destroy the swapchain-derived objects (image views, framebuffers) in reverse order, rebuild, rebuild `SurfaceMapping` from the new framebuffer extent, `PressLatch::cancel()`. A `VK_ERROR_OUT_OF_DATE_KHR` from acquire or present triggers the same path. |
| **Focus loss** | `PressLatch::cancel()`; the frame keeps rendering. No queued event that could activate a control. |
| **Close / stop** | `glfwWindowShouldClose`, or — in `--smoke-test` — the presented-frame budget met, or the wall-clock deadline reached (a non-zero exit if the budget was not met). |
| **Teardown** | device idle first, then destroy in strict reverse construction order — sync objects, command pool, framebuffers, pipeline-adjacent objects the example owns, swapchain, device, surface, instance, window — the ordering `VulkanSCTriangleExample::cleanup()` already uses. `UiRenderer` destroys only what it created, in its own destructor. |

`VK_ERROR_DEVICE_LOST` and any other non-recoverable `VkResult` is fatal and reported as itself,
never as a skipped frame.

### 5. A bounded presentation smoke path runs in CI, and an unavailable device fails it

The example takes `--smoke-test`: it presents a fixed number of frames (real swapchain, real
`vkQueuePresentKHR`) and exits, non-zero on the wall-clock deadline or any Vulkan/GLFW failure —
the `VulkanSCTriangleExample` pattern. It is registered as the CTest `example.monitor.smoke` and
**labelled `pixel`**, so the `-L pixel` step every supported CI leg already runs (Linux GCC and
Clang under Xvfb + lavapipe, Windows under lavapipe, macOS under MoltenVK) exercises it without a
new workflow step. It carries **no `SKIP_RETURN_CODE`**: an absent required display or device is a
failure, not a CTest `Skipped`, so a broken ICD cannot turn the only end-to-end presentation check
into a silent no-op — and the Linux and macOS `-L pixel` steps additionally fail the job on any
`Skipped` line.

A second mode, `--headless-frame`, renders one frame through `mdux.render.offscreen` with no window,
reads it back and asserts the topbar and critical-control rectangles land where the compiled screen
places them — deterministic frame-content evidence that does not depend on a swapchain. It is
`example.monitor.headless`, also `pixel`-labelled.

### 6. Coordinate and event translation are unit-tested without a device

`SurfaceMapping` is covered in `tests/medui/InputContractTests.cpp` (`medui_spec`, links
`MduX::Core` only): the identity, integer-ratio, non-uniform-refused and degenerate-refused cases,
no allocation (`tests/medui/InputNoHeapTests.cpp`), and an **agreement** scenario — a window-space
point over a control resolves through `SurfaceMapping` + `resolvePress()` to the same node id at a
1× and a 2× device-pixel ratio, and a point away from the control resolves to nothing. The GLFW key table is covered exhaustively in a
`glfw_translation_spec` binary (links `glfw` for the key constants only; no `glfwInit`, no display),
which runs in the ordinary test job.

## Alternatives Considered

- **A dedicated adapter library target that links GLFW** (mirroring `trustsc-vulkan-winit`).
  Rejected for this issue: it changes the standing "MduX never links a windowing library"
  convention, needs its own trust-zone carve-out, and expands the install/export surface — for a
  shell that, written as an `examples/support/` header, is already reusable by #318. Revisit if a
  third consumer appears.
- **Put the whole driver in the example, no shared header.** Rejected: #318 and later windowed
  examples would each re-derive the swapchain lifecycle and the event table, which is the
  stacked-delivery duplication CONTRIBUTING § "Stacked delivery" exists to prevent.
- **Scale the surface to fill the window (letterboxed) in this PR.** Rejected: `UiRenderer` has no
  scale/offset parameter and #317 does not change its contract, so a fitted presentation needs
  either that change or a post-render blit — orthogonal work that belongs with #318. Drawing 1:1 at
  the framebuffer origin is what the renderer already does, and it keeps rendering ⇄ hit-testing
  trivially consistent.
- **Per-axis (non-uniform) scale in `SurfaceMapping`.** Rejected: `normalizeSurfacePoint()` takes
  one ratio for both axes by design (ADR-018 clause 3), a real display's device-pixel ratio is
  uniform, and squashing a non-uniform ratio onto one axis is exactly the silent wrong-target
  behaviour clause 3 fails closed to avoid.
- **Take the DPI factor from `glfwGetWindowContentScale` and map window coordinates directly.**
  Rejected: the swapchain and `UiRenderer` viewport work in framebuffer pixels, and on X11/Wayland
  the content scale and the framebuffer-to-window ratio can disagree. The framebuffer extent is the
  one that matches what was drawn.
- **Let the example execute `TriggerHalt`.** Rejected, as ADR-018 clause 7 already rejected it —
  the host executes; the example records an `ActionTrace` and does nothing else.
- **A headless-only smoke path (no window).** Rejected as the sole check: it never exercises the
  swapchain, present or resize path the adapter adds, which is exactly the safety-relevant surface
  #317 introduces. Kept as an additional deterministic check.
- **Refactor `VulkanSCTriangleExample` onto the new shared header in this PR.** Deferred: it is a
  working example and the refactor is orthogonal risk. The header is written as the extraction; the
  triangle can adopt it later.

## Consequences

### Positive

- One decided place for the windowed adapter, with explicit lifecycle outcomes, before #318 needs
  it.
- Governed code gains exactly one small, pure, tested value (`SurfaceMapping`) and no windowing
  dependency; `mdux_verify_trust_zones()` still passes.
- The "rendering and hit testing agree" property is a unit test that runs with no GPU, and the
  end-to-end presentation path is a CI check that fails rather than skips without a device.
- The adapter half of PAR-REQ-004/005 moves from "the platform adapter (#317)" to a delivered,
  tested contract.

### Negative

- A reader of the interaction subsystem now follows three records: ADR-018 (the contract), this
  (the adapter boundary) and #318 (the assembled loop). Mitigation: this record refines rather than
  restates ADR-018, and cites the specific clauses.
- The reusable window/swapchain shell lives in `examples/support/`, not under `tests/` or a
  library, so it is exercised only by the example binaries and the `glfw_translation_spec` unit
  test — its swapchain-recreation path has CI coverage only through `--smoke-test` on a resize the
  smoke test does not itself trigger. Mitigation: `--smoke-test` exercises acquire/record/present
  and the `OUT_OF_DATE` branch is shared with the resize path; a full resize test belongs with #318.
- `SurfaceMapping::create` assumes a 1:1 framebuffer-origin renderer. A presenter that scaled or
  letterboxed the surface would need a different factory. Mitigation: it is implementation-local
  and versioned by its module; adding a second constructor is additive, and `identity()` plus the
  raw `scaleNum/scaleDen/originX/originY` fields already cover a caller that computes its own.

### Risks

- **The lifecycle table is read as a completed safety analysis.** Mitigation: the Medical Device
  Considerations section and this record's status both say these are engineering outcomes, no
  hazard or risk control is assigned, and the host owns any real action.
- **A HiDPI configuration with a fractional or per-axis-different framebuffer ratio** yields a
  `num/den` past `maxCoordinateScale` or a height that does not match the width ratio. Mitigation:
  `create()` fails closed (`MalformedScale`) rather than rounding, and the example reports it as a
  start-up failure; a fractional-DPI screen is out of initial adapter scope (ADR-018 clause 8).

## Implementation Notes

- `include/mdux/medui/Input.cppm` gains `SurfaceMapping` in the clause-3 section; no `src/` file,
  no new module, no `PRIVATE` source entry — it stays header-only like the rest of
  `mdux.medui.input`. Imports are unchanged (`std`, `mdux.core.units`, `mdux.core.result`, …); it
  does not import `mdux.medui.screen`.
- `examples/support/GlfwPresentationAdapter.hpp` (+ a `.cpp` if a callback needs a definition):
  `GlfwVulkanWindow` (window + instance/surface/device/swapchain/sync, `beginFrame`/`endFrame`
  returning a `PresentOutcome`, `recreateSwapchain`), the translation free functions, and a
  `WindowEventPump` that installs callbacks pushing into a caller-owned `EventQueue`. GLFW comes
  from the same `find_package(glfw3 QUIET)` / `CPMAddPackage` block the triangle uses.
- `examples/MedicalScreenMonitorExample.cpp` presents `endoscope-monitor` (the committed screen,
  via `mdux.medui.generated.screen_endoscope_monitor`), embedding the `dejavu-ui` atlas, the
  `endoscope-monitor-en-us` text package and the `brand-mark` pixels with `mdux_embed_blob()`. Per
  frame: drain the queue once → route pointers through `PressLatch` + `resolvePress()` (an
  `ActionTrace` printed on the `emergency-halt` activation, nothing executed) → route keys/text
  through a `FieldEditor` bound to `patient-id` → assemble a `TextInputSlot` → `render()` →
  `UiRenderer::record()` → present.
- `tests/adapter/GlfwTranslationTests.cpp` → `glfw_translation_spec` (links `glfw`, `MduX::Core`,
  `speclab`); `tests/medui/InputContractTests.cpp` and `tests/medui/InputNoHeapTests.cpp` gain the
  `SurfaceMapping` scenarios. The object-file symbol scans already cover `Input.cppm`'s object.
- CI: a windowed `--smoke-test` step on `linux-gcc16-build.yml`, `macos-arm64-build.yml`,
  `windows-build.yml` (and `clang-build.yml` if it builds examples); the headless mode where
  `-L pixel` runs.
- `docs/architecture.md` (module table note, examples list, the input §), `docs/roadmap.md` (#308
  section), `docs/parity/requirements.md` (PAR-REQ-004/005 delivery columns) updated.
- `docs/adr/README.md` indexes this as ADR-019; the next free number becomes ADR-020.

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) — the governed zone (`std` only, no windowing)
- [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) — Result-returning, `noexcept`
- [ADR-010](ADR-010-no-on-device-text-shaping.md) — a `TextEvent` carries a resolved scalar
- [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) — the compile boundary the runtime
  types sit behind
- [ADR-018](ADR-018-bounded-input-and-update-order.md) — the bounded input contract this refines
  (clauses 3, 4, 6, 7, 8)
- [Prospective requirements](../parity/requirements.md) — PAR-REQ-004, PAR-REQ-005, PAR-REQ-006
- Issues [#308](https://github.com/ambroise-leclerc/MduX/issues/308),
  [#317](https://github.com/ambroise-leclerc/MduX/issues/317),
  [#318](https://github.com/ambroise-leclerc/MduX/issues/318)
- `include/mdux/medui/Input.cppm` (`SurfaceMapping`, `normalizeSurfacePoint`, `PressLatch`),
  `include/mdux/render/VulkanRenderer.cppm` (`UiRenderer`, `VulkanRenderContext`),
  `examples/VulkanSCTriangleExample.cpp` (the window + swapchain lifecycle this extracts)
- TrustSC [`adapters/trustsc-vulkan-winit`](https://github.com/ambroise-leclerc/TrustSC/blob/4f114dd/adapters/trustsc-vulkan-winit/src/renderer.rs)
  — reference for shape, not a contract

## Approval

- **Proposal date**: 2026-09-09
- **Decision date**: 2026-09-09
- **Approved by**: Ambroise Leclerc, maintainer, by explicit instruction to accept ADR-019 and the
  adapter-half dispositions of PAR-REQ-004/005 under #317.
- **Dispositions**: the *adapter half* of **PAR-REQ-004** (normalize once with the specified
  rule; press arms / release activates the same target; cancel on focus loss / removal / overflow)
  and **PAR-REQ-005** (bounded caller-owned queue, stable order, one accepted batch per update) is
  delivered by `SurfaceMapping` + the `examples/support` adapter + the `PressLatch`/`EventQueue`
  wiring in `MedicalScreenMonitorExample`, and tested by the `InputContractTests` /
  `InputNoHeapTests` / `glfw_translation_spec` / `example.monitor.smoke` checks. **PAR-REQ-006**
  is unchanged: the example records an `ActionTrace` and executes nothing; the host execution /
  audit / orderly-stop policy stays open for domain review.
- **Still open**: the assembled batch-consuming update loop wired to Vulkan presentation and two
  approved locales (#318, ADR-018 clause 6); the critical-action host policy (PAR-REQ-006); a full
  swapchain-resize test (#318); PAR-REQ-009/010 keep their own status.
- **Scope**: where the windowed adapter lives, what it owns, the coordinate-mapping rule and its
  rebuild points, the event-translation table, the lifecycle outcomes and the CI smoke path.
  Windowing, native capture and host action execution stay implementation/host decisions. No
  interaction profile assigns a device's safety class.
