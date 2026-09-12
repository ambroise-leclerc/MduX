# ADR-022: Streaming viewport data and composition contract

## Status

**Proposed**, 2026-09-12, for [#322](https://github.com/ambroise-leclerc/MduX/issues/322), the
epic-[#310](https://github.com/ambroise-leclerc/MduX/issues/310) design track. It records the data
shape and composition rules a `VulkanViewport`'s live content will draw under, and resolves the one
open question its acceptance criteria pose explicitly: whether the existing compiled schema suffices,
or a shared MedUI decision is needed first.

It changes no compiled screen, no shader, no shared pin, and adds no code to `Schema.cppm` or
`Screen.cppm`. It defines one new header-only governed module, `mdux.medui.viewport`
(`include/mdux/medui/Viewport.cppm`), that #323 will bind to a live screen exactly as
`mdux.medui.trace` (#257) is bound by `SignalBinding` and `render()` today. It builds on
[ADR-014](ADR-014-rendered-truth-verification.md) (what a rendered check may and may not claim),
[ADR-015](ADR-015-versioned-sibling-observations.md) decision D5 ("#322 must finalize viewport
numeric/composition rules"), and [ADR-021](ADR-021-dynamic-scenario-capture-evidence.md) (the
presence-not-pixels verification a scene-driven node already gets).

## Shared contract

No MedUI shared decision governs streaming-viewport content. MEDUI-DEC-007/008
([MedUI #15](https://github.com/Compliatory/MedUI/issues/15)/[#16](https://github.com/Compliatory/MedUI/issues/16))
concern the `.medui` DSL's general syntax and its interaction/binding profile names
(`MEDUI-PROFILE-INTERACTION`/`-BINDING`/`-PRESENTATION`), and deliver no schema field and no corpus
for a stream's numeric domain, row/bin shape or colour. `VulkanViewportSpec`'s `streamSource` field
already exists in the published grammar and needs no change to carry this contract - see Decision 1.
This record therefore claims no shared profile and proposes no `medui-conformance.toml` change,
exactly as ADR-020 recorded for scenario data.

## Context

`recipes/screen/endoscope-monitor/EndoscopeMonitor.medui` already declares a `VulkanViewport` node,
`endoscope-view` (1280x424px, `stream_source: "ENDOSCOPE_PRIMARY"`), and it has reserved that
rectangle since the component dictionary shipped in v0.8.0. Nothing draws into it:
`mdux::medui::fieldColorToken()` has no case for `VulkanViewportSpec`, so the runtime's per-node
field-painting path - the one every other live component takes - skips it entirely, and no golden or
verification entry names `endoscope-view`. The roadmap's own comparison table calls this out
plainly: "MduX reserves geometry but defers stream content."

TrustSC's comparison target composes a bounded streaming waterfall into the equivalent rectangle.
Nothing here copies that implementation - ADR-015's own doctrine forbids it - but the shape of the
problem is real regardless of the sibling: a `VulkanViewport` is authored, compiled, laid out and
budgeted like every other node, and today it is the only one of the eleven with no defined content
model at all, live or static.

Two things make this design question different from every earlier live component's:

- **The compiled node carries no colour token, and never has.** `NumericDisplay`, `SignalTrace`,
  `StatusIndicator` and (once bound) `TextInput` all draw a governed tint `ColorHash` can check.
  `tools/medui/Goldens.cpp` already lists `VulkanViewport` beside `Image` and `Clock` as declaring no
  golden colour state. Whether that absence is an oversight to correct or a boundary to keep is
  exactly bullet 3 of the issue's acceptance criteria, and it has to be answered before a single line
  of composition math is written, because the answer decides whether this record needs a MedUI
  upstream decision at all.
- **The content is not a value at a point, or a value over time at one point - it is a 2-D field that
  scrolls.** `mdux.medui.trace` already solved "a caller-owned ring of live numbers, safely expanded
  into bounded geometry" for one dimension. This record's job is to decide what changes and what does
  not when a second dimension - history - is added, not to re-derive the first dimension's answers.

## Medical Device Considerations

Impact: **safety-relevant streaming-content design**. This record is a design decision plus one new
data-and-pure-function module; it renders nothing (there is no `DrawList` dependency anywhere in it)
and binds to no live screen.

- **IEC 62304:2006 §5.2/§5.3 (software design)**: this is a software-item design record for a
  not-yet-implemented content model. No existing verified behaviour changes; `Schema.cppm` and
  `Screen.cppm` are unmodified.
- **IEC 62366:2006 (usability)**: `endoscope-view`'s name is inherited from the existing committed
  recipe and does not itself authorize real endoscopic video; Decision 6 excludes actual video decode
  by design, and nothing in this record depends on the node's name meaning anything clinically.
- **Risk management**: no hazard, risk control or software safety class in this repository names a
  streaming-waterfall failure mode. This record does not invent one; it is example/demonstrator
  content exactly as the ECG classifier and the synthetic pressure reading already are.
- **Traceability**: `VulkanViewportSpec` carries no `requirement` field and this record adds none -
  consistent with the existing dictionary, where `Image`, `Clock`, `SignalTrace` and
  `VulkanViewport` are the components that take no requirement (`tools/medui/Goldens.cppm`'s own
  comment).
- **Cybersecurity**: unaffected. The new module imports `std` only, allocates nothing, and reads no
  file.

No cross-implementation parity is claimed. This is not a certification, validation or
production-readiness claim.

## Decision

### 1. No schema extension - `streamSource` is the whole of the compiled contract

`VulkanViewportSpec` keeps exactly the one field it has today. The numeric domain, the colour ramp
and the ring itself all arrive from the caller at run time, in `WaterfallStyle` and `WaterfallGrid` -
the same split `SignalTraceSpec`/`TraceStyle` already draw between "what the screen decides" (where a
stream's content goes, and in `SignalTrace`'s case its one governed tint) and "what only the producer
knows" (`TraceStyle`'s numeric range). Because `VulkanViewport` has never had a governed tint to
carry, the split here is simpler: nothing about its appearance is compiled at all. This resolves
bullet 3 of the issue without an upstream MedUI decision, because nothing about the published grammar
needs to change.

### 2. No governed colour token - the ramp is caller-supplied, and that is a boundary kept, not a gap

A screen with a governed colour token can show only as many tints as it declares, which is right for
a face with a finite number of visual states and wrong for a continuous numeric field: the moment two
adjacent cells need visibly different tints from the same one-token vocabulary `ColorHash` can check,
the token stops describing the picture. `VulkanViewport` was already excluded from that vocabulary
before this issue existed (Context, above). This record keeps that exclusion rather than reversing
it, and gives the caller a two-colour linear ramp (`WaterfallStyle::lowColor`/`highColor`) instead -
unverified, exactly as `TraceStyle`'s numeric range is unverified today, for the identical reason: no
committed artifact can describe values that do not exist until the frame does.

### 3. `WaterfallGrid` rings at row granularity, not scalar granularity

`mdux.medui.trace`'s `SampleRing` wraps one scalar per tick, which fits a producer that emits one
reading at a time. A waterfall's producer emits a whole row at a time - one spectrum, one sweep - so
`WaterfallGrid` wraps at row granularity: `bins` (the fixed width of every row, declared once) and
`oldestRow`/`rowCount` (`SampleRing`'s `oldest`/`count`, moved up one level). `at(row, col)` reads
within the ring exactly as `SampleRing::at()` does, oldest row first.

### 4. Composition: the live extent fills the band, exactly as a trace's x-axis does

`mdux.medui.trace` does not reserve room for `maxSamplesPerTrace` and leave the unused part of the
band blank while a ring warms up; `columnFor()` spans whichever `ring.count` is current across the
*whole* band. `waterfallCellRect(band, rows, cols, row, col)` applies the identical rule on both
axes, with `rows` the caller's *live* `rowCount` and `cols` the ring's fixed `bins`: the grid always
tiles the whole reserved rectangle, growing new rows into it exactly as a trace grows new segments
into its width. Division is remainder-absorbing (every cell but the last on each axis gets the plain
`band.dimension / count`, and the last keeps what integer division left over), so the union of every
cell is `band` exactly - no gap, no overflow, and therefore no clip rectangle is needed for a
composition #323 will build entirely from `addSolidRect()`. Row 0 (oldest) sits at the band's top
edge, the newest live row at its bottom; column 0 sits at the band's left edge - the same "oldest
first, ascending" convention `mdux.medui.trace`'s own module comment states for its one axis,
generalised to two.

### 5. Colour: linear interpolation between two caller-supplied tints, clamped at the rails

`waterfallCellColor(value, style)` normalises `value` against `[minimum, maximum]` and interpolates
each RGBA channel linearly between `lowColor` and `highColor`, rounding the way `medui::quantise()`
rounds a linear channel - multiply, add and the rounding offset kept as separate statements, for the
same cross-toolchain determinism reason. A value outside the declared range clamps to the rail it
overshot rather than being refused or extrapolated past the ramp's own ends - `TraceStyle`'s "a real
excursion is shown pinned to its rail" rule, unchanged, and a NaN sample is treated as at-or-below the
low rail rather than propagated, matching `quantise()`'s "NaN maps to the value this function cannot
interpret" doctrine.

### 6. Excluded scope: no video decode, no adapter-owned GPU resource, no new lifecycle

Every quantity this contract produces - a cell's rectangle, a cell's colour - is ordinary
`mdux::draw::DrawList` geometry: the same untextured `addSolidRect()` every other component already
calls. There is no separate GPU-owned texture and no foreign handle anywhere in this module or in
what #323 will need to add, so:

- **CPU/GPU ownership** is unchanged from every other live component: the ring is CPU-owned caller
  storage, and the GPU only ever sees the same per-frame vertex/index buffers.
- **Update synchronization** needs nothing beyond what `DrawList` already guarantees - the ring is
  read fresh each frame, exactly as `SampleRing` is.
- **Resizing** is `mdux::medui::SurfaceMapping`'s concern (ADR-019), unchanged: the compiled node's
  rectangle never changes shape, only the surface's presentation scale.
- **Teardown** does not exist as a separate concept: nothing outlives the frame except the caller's
  own ring storage, which the caller owns for as long as it wants live data, exactly as a
  `SampleRing` is owned today.
- **General video decoding and a native Vulkan SC deployment path** are out of scope by design, not
  by omission - the "stream" this contract describes is always this bounded synthetic numeric grid,
  never compressed frames from a codec.

### 7. Verification stays what it already is for a node with no governed tint

The static per-screen gate (`mdux-verify-ui`, `verify.screen.<id>`) continues to check only
`endoscope-view`'s compiled bounds; it gains no `ColorHash` obligation, because the node still
declares none (Decision 2). Once #323/#324 bind live content, a scenario that drives that content
inherits the disposition ADR-021 already established for a scene-driven node: a rendered check
asserts *presence* (`mdux::verify::regionPainted()`), never an exact tint, and PAR-REQ-009's "no
exact-pixel claim without a declared backend profile" stays intact. This record adds no new
verification predicate and proposes no PAR-REQ-009 amendment; it inherits the existing one.

### 8. Bounds, chosen against real budget arithmetic

`maxWaterfallRows = 16` bounds the most *live* history rows a frame may carry; `maxWaterfallBins =
32` bounds a row's declared width, fixed for the ring's life. Both are type-level constants, exactly
as `maxSamplesPerTrace` is - a number a device can multiply by its node count before it runs, not one
that moves with a producer's sampling rate. A full-cap grid costs `rows * cols` solid quads, one per
cell, with no shared cap or joint to add: 512 quads is 2048 vertices and 3072 indices - **exactly
half** of the 4096/6144 the committed `endoscope-monitor` screen declares for its whole frame, so a
reviewer can check in their head that a full-history waterfall leaves room for every other node the
rest of that screen already carries.

## Alternatives Considered

- **Add a `colorToken` to `VulkanViewportSpec` and reuse the single-tint-at-two-coverages composition
  `NumericDisplay`/`SignalTrace` use.** Rejected: that composition exists because those components
  carry exactly one tint, and `Screen.cppm`'s own module comment states the constraint it lives
  inside - a second colour fails a `ColorHash` golden. A heatmap needs visibly different colours
  across adjacent cells by definition; forcing it through a one-tint scheme would either make the
  picture wrong or make `ColorHash` stop meaning what it means everywhere else it is checked. It is
  also the schema extension the issue asks to avoid needing when the existing fields already suffice.
- **A per-scalar ring (`SampleRing` reused directly) instead of a per-row `WaterfallGrid`.** Rejected:
  a scalar ring has no place to declare where one row ends and the next begins, so every reader would
  have to be told the row width out of band, which is exactly what `bins` exists to avoid.
- **Cell size fixed against the ring's declared *capacity* rather than its live `rowCount`.**
  Rejected: it has no precedent in this codebase. `mdux.medui.trace`'s x-axis spans whichever
  `ring.count` is current, not `maxSamplesPerTrace`, and a waterfall that reserved vertical space for
  rows that have not arrived yet would draw an empty gap no other live component draws while warming
  up.
- **An adapter-owned streaming GPU texture, updated by `memcpy` each frame.** Rejected: it reintroduces
  a foreign handle into a governed-adjacent type for no benefit over quads at this cell budget, needs
  new synchronization and teardown machinery the epic explicitly asks to avoid, and could not be
  exercised by the existing GPU-free `medui_spec`/`medui_noheap_spec` test binaries the way this
  module is.
- **Refuse, rather than clamp, a sample outside the declared numeric range.** Rejected for the same
  reason `TraceStyle` clamps rather than refuses: an excursion past the display range is a real
  reading, and refusing the frame would blank the display at the exact moment the stream became most
  interesting.
- **Bound `rows`/`cols` by a single combined `maxWaterfallCells` rather than two separate caps.**
  Rejected: `bins` is fixed for a ring's whole life while `rowCount` grows and shrinks with the
  producer's own history depth, so they are different kinds of quantity and a reviewer reasoning about
  "how wide can one row be" should not have to know how many rows are currently live to bound it.

## Consequences

### Positive

- Bullet 3 of the issue is resolved with no upstream MedUI decision needed, and no change to
  `Schema.cppm`, `Screen.cppm` or `medui-conformance.toml`.
- The composition and colour math are pure, `constexpr`, `noexcept` and header-only - testable without
  a `DrawList`, a screen, or a GPU, and already covered by 16 scenarios in
  `tests/medui/ViewportContractTests.cpp` (`medui_spec`), all passing.
- The eventual render step (#323) has nothing left to design: it calls `validate()` once, then
  `addSolidRect(waterfallCellRect(...), waterfallCellColor(...))` per cell, reusing machinery every
  other component already exercises.
- No new GPU resource, synchronization primitive or lifecycle concept enters the codebase.

### Negative

- A caller now owns two closely related concepts - `SampleRing` (scalar-granularity) and
  `WaterfallGrid` (row-granularity) - rather than one generalised ring type. Mitigation: a single
  generalised ring would have made the common, simpler `SignalTrace` case carry a `bins` field it
  never uses; the split keeps each type's contract exactly as wide as its own component needs.
- The two-colour ramp's endpoints are demonstrator aesthetics with no clinical grounding, not derived
  from a device requirement. Mitigation: flagged here for the same domain review every other example
  visual choice in this programme carries, and `WaterfallStyle` is caller-supplied, so a real consumer
  chooses its own ramp.

### Risks

- **The "no schema extension" conclusion is read as foreclosing a future one.** Mitigation: nothing
  here prevents `VulkanViewportSpec` from gaining a field later if a genuine shared-decision need
  arises; this record states only that today's `streamSource` already suffices for the contract #322
  was asked to specify.
- **`maxWaterfallRows`/`maxWaterfallBins` are picked for one screen's arithmetic and might not suit a
  different-sized viewport.** Mitigation: they are type-level ceilings, not a specific screen's
  budget - a smaller viewport uses fewer of the 512 admitted cells, and a screen needing more would
  need a fresh justification against its own `DrawBudget`, exactly as raising `maxSamplesPerTrace`
  would.

## Implementation Notes

- `include/mdux/medui/Viewport.cppm` (`mdux.medui.viewport`, header-only, added to `MduXCore`'s
  `FILE_SET CXX_MODULES` beside `mdux.medui.trace`): `maxWaterfallRows`, `maxWaterfallBins`,
  `WaterfallGrid`, `WaterfallStyle`, `WaterfallError` + `describe()`, `quadsForWaterfall()`,
  `waterfallCellRect()`, `lerpByte()`, `waterfallCellColor()`, `validate()`. No `.cpp` - every
  function is `constexpr noexcept`, so there is nothing a translation unit would add, matching
  `mdux.medui.input`'s and `mdux.medui.scenario`'s header-only shape.
- `tests/medui/ViewportContractTests.cpp` (`medui_spec`): cell tiling (exact and remainder-absorbing),
  degenerate-index rectangles, ramp endpoints/clamping/degenerate-style fallback, wrapped-ring read
  order, the well-formed and every malformed-input validation path, `describe()` coverage, and the
  cost-model-versus-budget arithmetic Decision 8 states. 16 scenarios, GPU-free, run in the existing
  `medui_spec` binary.
- No change to `Schema.cppm`, `Screen.cppm`, `medui-conformance.toml`, any recipe, or any committed
  `generated/` artifact. `#323` is expected to add a `ViewportBinding` to `mdux.medui.screen` (the
  `SignalBinding` analogue) and a `recordWaterfall()`-shaped call into `DrawList`, neither of which
  this record specifies further than Decisions 4-6 already constrain.
- `docs/architecture.md` (module table + governed-runtime narrative), `docs/roadmap.md` (#310/#322),
  `docs/parity/requirements.md` (the #322 decision-map row; PAR-REQ-008's "viewport row/bin semantics"
  amendment is resolved by Decisions 3-4), `docs/parity/behavior-matrix.md` (the `VulkanViewport` row),
  `docs/adr/README.md` (index ADR-022; next free is ADR-023).

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) - the governed / adapter / host-tools split
- [ADR-010](ADR-010-no-on-device-text-shaping.md) - the determinism doctrine `quantise()`-style
  rounding follows
- [ADR-014](ADR-014-rendered-truth-verification.md) - what a rendered check may claim
- [ADR-015](ADR-015-versioned-sibling-observations.md) decision D5 - names #322 as the record that
  must finalize viewport numeric/composition rules
- [ADR-018](ADR-018-bounded-input-and-update-order.md), [ADR-019](ADR-019-windowed-presentation-and-input-adapter.md) -
  the bounded-contract and pure-function precedent this record follows
- [ADR-021](ADR-021-dynamic-scenario-capture-evidence.md) - the presence-not-pixels disposition a
  scene-driven node already has, inherited rather than reargued (Decision 7)
- [Prospective requirements](../parity/requirements.md) - PAR-REQ-008, PAR-REQ-009
- Issues [#310](https://github.com/ambroise-leclerc/MduX/issues/310),
  [#322](https://github.com/ambroise-leclerc/MduX/issues/322)
- `include/mdux/medui/Trace.cppm`, `src/medui/Trace.cpp` - the one-dimensional precedent this record
  generalises
- `include/mdux/medui/Schema.cppm` (`VulkanViewportSpec`), `include/mdux/medui/Screen.cppm`
  (`fieldColorToken()`), `tools/medui/Goldens.cpp` - the evidence that `VulkanViewport` already
  declares no golden colour state

## Approval

- **Proposal date**: 2026-09-12
- **Decision date**: pending
- **Approved by**: pending - maintainer engineering acceptance, and a domain review of the
  demonstrator waterfall's numeric bounds and colour ramp (no clinical grounding is claimed for
  either).
- **Scope**: `mdux.medui.viewport`'s data types and pure composition functions - `WaterfallGrid`,
  `WaterfallStyle`, `WaterfallError`, `waterfallCellRect()`, `waterfallCellColor()`, `validate()` -
  and the "no schema extension" and "no governed colour token" dispositions (Decisions 1-2). Binding
  this contract to a live `ScreenPackage` and a `DrawList` is #323's scope, not this record's.
- **Review date**: with the verifier-area domain review already tracked for
  ADR-016/ADR-017/ADR-021, once #323/#324 give it live content to assess.
