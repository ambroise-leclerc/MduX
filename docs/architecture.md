# MduX architecture

What is in the tree on `develop`, and what is not.

Every target, module and path named here exists. Where something is planned rather than built, it
says so and links the issue. If you find a statement here that the build contradicts, the statement
is the bug — this document is meant to be checkable against `CMakeLists.txt` and `ctest`, not taken
on trust.

**This document establishes no certification, validation, production readiness, or regulatory
compliance.** MduX is an experimental proof-of-concept. See
[`regulatory-compliance.md`](regulatory-compliance.md) for the scope limits the project claims.

## Verified platforms

Windows and Linux use native Vulkan implementations. Apple Silicon macOS is also a verified target
through one pinned configuration: upstream Clang 21.1.8/libc++, CMake 4.3.1, Ninja, and LunarG
Vulkan SDK 1.4.309.0 with MoltenVK. The preset and toolchain file reject AppleClang, macOS GCC,
Intel Macs, and version drift. MoltenVK is an adapter over Metal, so this adds a distinct driver
path rather than claiming native-Vulkan equivalence; automatic CI must execute both pixel tests and
a three-frame presentation smoke test on that path.

## Trust zones

The organising idea, from [ADR-004](adr/ADR-004-trust-zones-in-cpp.md). Three zones, with the
boundary between them enforced at configure time rather than by review:

| Zone | Targets | May reach | Exceptions |
|---|---|---|---|
| **Governed** | `MduXCore`, `MduX_warnings` | `std` only | never throws ([ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md)) |
| **Adapter** | `MduX` | governed + Vulkan | throws where Vulkan makes it unavoidable |
| **Host tools** | `MduXToolsCommon`, `MduXShaderBakeLib`, `MduXMlBakeLib`, `MduXTextBakeLib`, `MduXMeduiLib`, `MduXVerifyUiLib` | anything | may throw freely; never linked into a device target |

`mdux_verify_trust_zones()` in [`cmake/MduXTrustZones.cmake`](../cmake/MduXTrustZones.cmake) walks
the full link graph of every declared-governed target at the end of configure and fails on a
forbidden dependency.

**What that guarantee is, precisely:** a governed target's link graph reaches no Vulkan or windowing
target, directly or transitively, and the check runs on every configure rather than at review time.
It is a *link-graph* property, checked mechanically.

**What it is not:** it is not `#![forbid(unsafe_code)]`. It does not prove the absence of undefined
behaviour, and no combination of C++ tooling here does. Governed modules are compiled without access
to platform, graphics or OS headers, are checked at the source level by
[`mdux-governed-lint`](../tools/governed-lint/) and at the object level by
`governed.noThrow.symbolScan`, are covered by determinism and no-heap tests, and are held to an
enforced warning set — that is the whole of the claim.

Two things that claim deliberately does not include. Governed code can still reach libstdc++'s
throwing helpers through ordinary `std::string` and `std::vector` use — 14 such references exist and
the scan prints them on every run rather than implying otherwise. And `MduXCore` cannot be built
with `-fno-exceptions`: `import std` and that flag are mutually exclusive on GCC, because the std
BMI records its dialect and CMake synthesises one shared std target. Both are recorded in
[ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md), "What is enforced".

## Modules

C++23 named modules throughout. Interfaces are declared in `FILE_SET CXX_MODULES`; implementations
are ordinary `PRIVATE` sources.

### Governed — `MduXCore`

| Module | Interface | Implementation |
|---|---|---|
| `mdux.core.units` | `include/mdux/core/Units.cppm` | header-only |
| `mdux.core.result` | `include/mdux/core/Result.cppm` | header-only |
| `mdux.evidence.digest` | `include/mdux/evidence/Digest.cppm` | `src/evidence/Digest.cpp` |
| `mdux.evidence.json` | `include/mdux/evidence/Json.cppm` | `src/evidence/Json.cpp` |
| `mdux.evidence.report` | `include/mdux/evidence/Report.cppm` | `src/evidence/Report.cpp` |
| `mdux.governance` | `include/mdux/governance/Governance.cppm` | `src/governance/{Governance,Justification,Program}.cpp` |
| `mdux.governance.compliance` | `include/mdux/governance/Compliance.cppm` | `src/governance/Compliance.cpp` |
| `mdux.shader.schema` | `include/mdux/shader/Schema.cppm` | `src/shader/Schema.cpp` |
| `mdux.image.schema` | `include/mdux/image/Schema.cppm` | `src/image/Schema.cpp` |
| `mdux.text.schema` | `include/mdux/text/Schema.cppm` | `src/text/Schema.cpp` |
| `mdux.font.schema` | `include/mdux/font/Schema.cppm` | `src/font/Schema.cpp` |
| `mdux.text.draw` | `include/mdux/text/Draw.cppm` | `src/text/Draw.cpp` |
| `mdux.draw` | `include/mdux/draw/Draw.cppm` | `src/draw/Draw.cpp` |
| `mdux.ml.schema` | `include/mdux/ml/Schema.cppm` | header-only |
| `mdux.ml.kernels` | `include/mdux/ml/Kernels.cppm` | `src/ml/Kernels.cpp` |
| `mdux.ml.runtime` | `include/mdux/ml/Runtime.cppm` | `src/ml/Runtime.cpp` |
| `mdux.medui.schema` | `include/mdux/medui/Schema.cppm` | header-only |
| `mdux.medui.reading` | `include/mdux/medui/Reading.cppm` | `src/medui/Reading.cpp` |
| `mdux.medui.screen` | `include/mdux/medui/Screen.cppm` | `src/medui/Screen.cpp` |
| `mdux.medui.trace` | `include/mdux/medui/Trace.cppm` | `src/medui/Trace.cpp` |
| `mdux.verify` | `include/mdux/verify/Verify.cppm` | `src/verify/Verify.cpp` |

`mdux.core.result` is a naming alias over `std::expected`, not a reimplementation
([ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md)).

### Adapter — `MduX`

| Module | Interface | Implementation |
|---|---|---|
| `mdux` (facade) | `include/mdux/mdux.cppm` | `src/mdux.cpp` |
| `mdux.render.vulkan` | `include/mdux/render/VulkanRenderer.cppm` | `src/render/VulkanRenderer.cpp` |
| `mdux.render.offscreen` | `include/mdux/render/Offscreen.cppm` | `src/render/Offscreen.cpp` |
| `mdux.vulkansc.memory` | `include/mdux/vulkansc/MemoryPoolManager.cppm` | `src/vulkansc/MemoryPoolManager.cpp` |
| `mdux.vulkansc.objects` | `include/mdux/vulkansc/DeviceObjectManager.cppm` | `src/vulkansc/DeviceObjectManager.cpp` |

`MduX` PUBLIC-links `MduXCore`, so a consumer of `MduX::MduX` gets the governed modules
transitively.

The `mdux` facade is deliberately small: `Version`, `Compliance`, `VulkanSupport`,
`ComplianceMetadata`, and `initialize()` / `shutdown()`. `ComplianceMetadata` is a **metadata
record the caller fills in** — it describes what a manufacturer asserts about their own device. It
performs no checking and confers no compliance.

### Host tools

| Target | Sources | Produces |
|---|---|---|
| `MduXToolsCommon` | `tools/common/` | TOML subset reader, CLI parser, shared diagnostic envelope |
| `MduXShaderBakeLib` | `tools/shader/` | `mdux-shaderbake`, `mdux-shaderemit` |
| `MduXMlBakeLib` | `tools/ml/` | `mdux-mlbake`, `mdux-mlemit` |
| `MduXTextBakeLib` | `tools/text/` | `mdux-textbake`; also hosts `mdux.tools.truetype` (the host-only glyf parser with cmap/hmtx, #158), `mdux.tools.atlaspacker` (the shelf packer, #160) and `mdux.text.raster` (the glyph rasteriser, #159) |
| `MduXImageBakeLib` | `tools/image/` | `mdux-imagebake`; its dependency-free QOI decoder is host-only and writes a committed straight-alpha RGBA8 sidecar (#256) |
| `MduXMeduiLib` | `tools/medui/` | the `.medui` compiler (#15); the shared `MEDUI-E` diagnostic registry (#191), parser (#192), component/theme/locale semantic analyzer (#193), integer-only bounded layout solver (#194), the text-budget check that measures resolved boxes against the widest approved translation (#195), and the golden references that say where safety-critical content must appear (#196), the canonical package with its two C++ emitters (#197) and the compiler driver behind `mdux-meduic` (#198) |
| `MduXVerifyUiLib` | `tools/verify/` | `mdux-verify-ui` (#253): committed-artifact loading, complete golden/text obligation planning, headless offscreen rendering once per locale, owning outcomes and distinct check-failed/run-impossible statuses |

Host tools parse untrusted input, so they are deliberately outside the governed zone. They are
never linked into `MduXCore` or `MduX` and are absent from the install/export set.

`mdux.text.raster` was governed until [#116](https://github.com/ambroise-leclerc/MduX/issues/116).
It allocates, and `std::vector` reports failure by throwing, so its `noexcept` entry point has to
catch — which [ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md) forbids in governed
code. It runs once per glyph at build time and never on a device, which is what made the host-tools
zone the right home rather than a reason to rewrite it.

## The Vulkan boundary

`mdux::render::VulkanRenderContext` is **populated by the caller**: `VkDevice`, `VkPhysicalDevice`,
`VkRenderPass` and a `VkQueue` all come from the host application. Every member is initialised, so a
partially-filled context fails validation rather than faulting.

`mdux::render::UiRenderer` creates and owns its own resources from that context — shader modules,
descriptor set layout and pool, pipeline layout, pipeline, frame buffers, default atlas. It does
**not** create an instance, device or swapchain. Those are the application's.

**Windowing is not a library dependency.** GLFW is linked into `VulkanSCTriangleExample` only, as
one way an application might create a surface. `MedicalUiExample` deliberately does not link it —
building a frame needs neither a window nor a device, which is part of what it demonstrates.

## Evidence pipeline

From [ADR-007](adr/ADR-007-evidence-pipeline-doctrine.md). A host tool bakes an artifact, the
artifact is committed, and CI re-derives it and asserts byte-identity:

```
recipes/<kind>/<id>.toml  ──[ mdux-<kind>bake ]──▶  generated/<kind>/<id>/
                                                      package.json
                                                      report.json
                                                      <payload>.bin
```

Eight artifacts are committed today:

| Artifact | Baker | Payload |
|---|---|---|
| `generated/shader/mdux-ui/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/shader/triangle/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/model/ecg-demo/` | `mdux-mlbake` | `weights.bin` |
| `generated/model/ecg-demo-alt/` | `mdux-mlbake` | `weights.bin` |
| `generated/font/dejavu-ui/` | `mdux-textbake` | `atlas.bin` |
| `generated/text/endoscope-monitor-en-us/` | `mdux-textbake` | `runs.bin` |
| `generated/image/brand-mark/` | `mdux-imagebake` | `pixels.rgba` |
| `generated/screen/endoscope-monitor/` | `mdux-meduic`, then `mdux-verify-bake` | `package.json` + `goldens.json` + `verification.json` |

The screen is the one entry whose payload is not opaque bytes, and ADR-012 explains why: a screen
cannot bake vertices, because four of the eleven components in the dictionary — `NumericDisplay`,
`SignalTrace`, `StatusIndicator` and `Clock` — draw from live data, and the *reading* they show does
not exist until the frame does. What `package.json` carries instead is layout: where each node is,
how much it may draw, and which validated token and key it draws with.

An `Image` joins that screen to one approved `mdux.image.schema` package by id, canonical-package
digest and intrinsic extent. The QOI decoder runs only in `mdux-imagebake`; neither compressed bytes
nor decoder code enter `MduXCore` or `MduX`. At startup the runtime authenticates the committed RGBA8
sidecar, and each frame records one full-sheet `SampledRgba` rectangle. The UI shader keeps coverage
and RGBA textures on distinct fixed descriptor bindings, so a frame can render text and an image
without changing its descriptor shape or allocating.

Two of those four have one part that *is* in the artifact, and since #255 the runtime paints it: a
`NumericDisplay` and a `SignalTrace` carry a single colour token over a single rectangle, which is
exactly the pair their golden entry pins. ADR-014 decision 5 is why that is read off the golden
sidecar rather than invented in the renderer, and why a `Clock` (no token) and a `StatusIndicator`
(one per state) are not in it. A `StatusIndicator` acquires one the moment a state is bound, which is
what #259 draws — the plural was the obstacle, not the absence.

Both now draw the live part as well. `mdux.medui.trace` expands a **caller-owned ring
buffer** into stroke quads — segments as quads with square joint caps rather than mitred joins, which
have no unbounded spike as an angle closes — and `SignalBinding` is what joins a stream name the
screen carries to the samples and the scale only the host knows. The samples never enter the
artifact and never could: what a sample of `ECG_LEAD_II` means in millivolts is a property of the
amplifier, not of the layout.

Three properties are worth naming because they are what makes that safe rather than merely working.
The expansion writes into the vertex budget the screen already declares, sized once and never grown.
A ring past `maxSamplesPerTrace` is **refused**, not truncated — a waveform silently showing a
different window is indistinguishable on a monitor from a correct reading of different data. And a
bound trace paints its field at reduced coverage under a full-tint stroke, which is the one
composition an additive draw list and a `ColorHash` golden both admit; an *unbound* trace is
unchanged, which is why the committed screen's pixel and `verify` legs are unchanged too — both
render it without signals.

`mdux.medui.reading` (#258) does the same for a `NumericDisplay`'s digits and a `Clock`'s time, and
it needed an **amendment to ADR-010** to exist at all. That ADR's decision 4 forbade "on-device code
that advances a pen by a runtime-computed width", while its decision 3 permitted dynamic text — a
contradiction nobody had to resolve while no component drew a live value. The amendment resolves it
narrowly: a reading is drawn from a *pattern* (`HH:MM:SS`, `###.# mmHg`) whose literals, slot
positions and glyph count are build-time constants, whose worst-case ink envelope the compiler
measures against the node that will hold it, and whose pen arithmetic has one implementation the
host budget stage imports rather than copies. Only which digit stands in each slot varies.

What a `template:` renders as is a **product table**, supplied by the screen recipe rather than
resolved into the artifact — so the compiled screen still carries a validated name and the shared
contract's compiled-screen semantics are untouched. The obvious exposure of that split is closed the
way a `Label`'s is: the runtime measures what it actually drew against the node and refuses the frame
if it does not fit, so a device holding a table the compiler never saw cannot put digits over a
neighbour.

`mdux.medui.screen`'s `StatusBinding` (#259) is the third join, and the smallest: a slot names a node
and a **position in that node's own `states:` list**. The list is closed in the artifact — every key
validated against every approved locale, the widest of them measured against the node's box — so an
index outside it is refused at `create()` and again in the frame, never clamped, wrapped or drawn as
a blank box, each of which would show a state the device is not in. A bound indicator paints its
field in that state's tint, with the state's word over it when a locale is bound; unbound it is
deferred, because a default state is a reading nobody supplied. One refusal there is about appearance
rather than names: a node that declares no `colors:` cannot be bound at all, since with no per-state
tint its states are told apart by nothing a frame carries unless a locale happens to be bound.

`goldens.json` is a sidecar with a different consumer — #16's frame verifier, not the runtime — and a
different rule. ADR-011 puts **every `@safety_critical` node and every node with an explicit
`position:`** in the golden set, which is why the committed screen has two entries: its
`NumericDisplay` is safety-critical and its `SignalTrace` is positioned. Both files are reviewable
as text, which is the point.

`mdux-verify-ui --screen=generated/screen/<id> --locales=all` consumes this bundle without changing
it. It derives render scopes only from the screen manifest, rejects locale subsets and zero
obligations, and distinguishes a completed check failure from a run that Vulkan or an artifact
problem made impossible. The committed endoscope screen discharges all five of its obligations since
#255 — two golden checks on the `NumericDisplay`, one on the `SignalTrace`, and the two mandatory
text checks on the `Label`.

`mdux_compile_screen()` registers that invocation as `verify.screen.<id>`, so the gate covers every
committed screen and a new one is gated by being committed. Three legs assert it as a named step —
lavapipe on Linux/GCC 16 and Linux/Clang 21, MoltenVK on macOS — with `--no-tests=error`, so a label
matching no screen fails rather than passing over nothing. It has no skip status: `mdux-verify-ui`
exits 3 for an absent device as for any impossible run, and since #254 made the bake render, a leg
without a device fails to build long before this test could be reached.

When a check fails, the driver writes `<screen>.<scope>.png` under the build tree — the rendered
frame dimmed, with each failed obligation's expected rectangle outlined in magenta and what was
actually found in cyan. The scope is percent-encoded rather than filtered, so two scopes of one
screen cannot overwrite each other's image. CI uploads it. It is an attachment rather than a
fifth file in the bundle because it *is* the frame, and ADR-014 decision 4 keeps measurements out of
a byte-compared artifact.

The screen is also the one entry baked by a *sequence* rather than a single tool (#254).
`mdux-meduic` compiles it, then `mdux-verify-bake` renders the result and writes `verification.json`
beside it, extending the same `report.json` with the new output's digest and the locale set the run
resolved. Both stages are one `mdux_bake_artifact()` registration and one `evidence.screen.<id>`
comparison, so the rendered half cannot end up with weaker evidence than the compiled half. The two
are separate tools because their dependencies differ: reading a `.medui` file needs no GPU, and
fusing them would make a Vulkan device a prerequisite of every screen compile.

That artifact records one outcome per enumerated obligation and nothing else — no measured pixel, no
path, no duration. What it says is that the frame agreed with the compiled screen, which is internal
consistency rather than truth: the expectation and the frame come from one source. It carried three
`NothingPainted` findings between #254 and #255, because an artifact that hid them would have been
worse than none; #255 resolved them by drawing the fields rather than by removing the obligations.

Every baker registers through `mdux_bake_artifact()`
([`cmake/MduXBake.cmake`](../cmake/MduXBake.cmake)), which creates the bake target, an
`evidence.<kind>.<id>` test, and membership in `mdux-bake-all` / `mdux-bake-update`.

**A normal build never writes into the source tree.** It bakes into the build directory and
compares. `cmake --build <dir> --target mdux-bake-update` is the only path that stages an artifact
into `generated/`, run deliberately by an author who then commits and reviews the diff. CI has a
step asserting the source tree is unmodified after a build.

Canonical JSON encodes floats as bit patterns, because decimal float text is not byte-identical
across MSVC, glibc and libc++ — and this pipeline crosses all three.

## ML inference

From [ADR-008](adr/ADR-008-zero-soup-ml-inference.md). No foreign inference stack is linked into any
device target.

`mdux.ml.kernels` is imported by **both** the device runtime and the host golden-vector generator —
one definition, one object file, one set of compile flags. That identity is what makes a
golden-vector mismatch on device mean "the FPU or the toolchain differs" rather than "one of two
implementations drifted".

`Classifier1D::create()` validates the package, verifies `sha256(weights)` against the digest it was
baked with, checks the scratch budget, requires at least one golden vector, and re-runs every one of
them through the real kernels comparing bit patterns. Any divergence returns an error and the object
is never constructed.

`mdux-mlemit` mechanically renders the committed package metadata and golden vectors as a generated
module plus a header fallback. Both carry a compile-time schema assertion; weights remain a separate
caller-supplied blob. `EcgClassifierExample` therefore links only `MduX::Core`, opens no files, and
runs no parser at startup. The host-only `PackageLoad` remains for dynamic tooling such as the
two-package weight-swap test.

Floating-point determinism is enforced at configure time by
[`cmake/MduXDeterminism.cmake`](../cmake/MduXDeterminism.cmake), which sets `-ffp-contract=off` and
fails the build if `-ffast-math`, `/fp:fast`, `/fp:contract` or similar reaches a governed target
from anywhere — including through a dependency's interface options.

## Tests

Two frameworks, one discovery contract ([ADR-009](adr/ADR-009-in-repository-test-framework.md)):
the in-repository `MduXTest` across nine executables, and SpecLab for Given/When/Then across seventeen.
One additional dedicated executable runs the production verification driver against Vulkan and can
report CTest skip status when no implementation is present.
`mdux_discover_tests()` registers one CTest entry per case, so a failure names the scenario rather
than the binary.

Labels select the evidence-bearing suites:

| Label | Means |
|---|---|
| `evidence` | a committed artifact is byte-identical to a freshly baked one — and nothing else |
| `evidence-unit` | unit tests of the evidence modules themselves |
| `determinism` | the ML kernels produce the frozen bit patterns |
| `noheap` | `predict()` performs no allocation |
| `governed` | no governed source contains a throw expression; object-symbol gate on libstdc++, informational on MSVC/libc++ ([ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md)) |
| `pixel` | rendered output matches expectations, under lavapipe and MoltenVK |
| `regulatory` | corpus indexes and schemas are current |

The `evidence` / `evidence-unit` split is deliberate: a broken SHA-256 test and a drifted artifact
must not produce the same CI signal. `governed` is separate from `noheap` for the same reason: both
run `cmake/MduXNoHeapScan.cmake`, but over different objects with different forbidden sets, and a
CI leg selecting one should not silently start covering the other.

`governed` includes a negative test where the standard library keeps its own throws distinguishable
from source throws (the GCC/libstdc++ leg). `governed.noThrow.symbolScan.negative` scans a
deliberately non-conforming object (`tests/governed/ThrowFixture.cpp`) and passes only when the scan
rejects it with the expected message — because a check that has only ever run against conforming
code passes identically when it is working and when it is looking for the wrong thing.

The object scan is informational on MSVC and macOS libc++: both emit library-owned throw references
that are indistinguishable from a hand-written throw in the same object. The scan reports those
references, and [`mdux-governed-lint`](../tools/governed-lint/) — which reads source and is
toolchain-independent — enforces the rule on those legs.

## Install and export

`install(TARGETS MduXCore MduX MduX_options MduX_warnings EXPORT MduXTargets ...)` with
`FILE_SET CXX_MODULES` installed to the include destination. Consumers use
`find_package(MduX)` and link `MduX::Core` or `MduX::MduX`. An install-tree consumer test builds
against the exported package.

Host tools are not exported. They are build-time only.

## Regulatory material

`docs/iec62304/`, `docs/iso13485/`, `docs/iso14971/`, `docs/iec62366/` and `docs/iec81001/` are
clause-structured reference modules with a generated per-clause index and JSON Schemas, linted in
CI by `tools/docs-lint/`.

They **describe clauses and point at mechanisms**. They do not certify anything, and no document in
this repository does. Reproducing normative standard text is forbidden outright
([ADR-006](adr/ADR-006-no-reproduction-of-normative-standard-text.md)).

`docs/governance/` holds the citation convention, the shared `Justification` schema, the SOUP
register, and `superseded-documents.md` — which records every retired document and why. Read that
before concluding content was lost.

## Planned, not built

The items below have not shipped — or, where noted, only the first slice has. Each row links the
tracking issue; the issue is authoritative for what remains.

| Planned | Issue | Note |
|---|---|---|
| `.medui` compiler | [#15](https://github.com/ambroise-leclerc/MduX/issues/15) | complete front to back: parsing, semantic validation, bounded layout, text budgets, golden references, the canonical package, both C++ emitters, `mdux-meduic`, `mdux-medui-check`, and a governed runtime that draws a compiled screen. One committed screen reaches pixels in `ScreenPixelTests`, carrying text (#242), fields (#255) and a baked QOI-derived Image (#256); it also carries a `StatusIndicator` (#259) whose bound state reaches pixels in the same suite — word and tint — and which `EcgClassifierExample` binds as a tint alone, since it opens no files to join a locale with. Interactive component content remains under #17 |
| Rendered-truth verification | [#16](https://github.com/ambroise-leclerc/MduX/issues/16) | beyond the current pixel test |
| Content components | [#17](https://github.com/ambroise-leclerc/MduX/issues/17) | `TextInput`, `Button` and `CriticalButton` remain. `Image` shipped with #256, `SignalTrace` with #257 — the `EcgClassifierExample` binds the same ring its classifier reads — `NumericDisplay` and `Clock` with #258, and `StatusIndicator` with #259, which the same demonstrator binds its classifier's output class to |

### The HTML/CSS path is gone, not planned

`MedicalUiRenderer`, `MedicalUiConfig`, `MedicalUiContent`, `UiFileWatcher`, `UiReloadEvent` and
`RenderStatistics` were **deleted** by [#127](https://github.com/ambroise-leclerc/MduX/issues/127).
`MedicalUiRenderer::render()` recorded no Vulkan commands and `uiPipeline` stayed `VK_NULL_HANDLE`
for the object's whole lifetime; it was removed rather than fixed.

What replaces it is `mdux.draw` (a governed description of a frame) plus `mdux.render.vulkan` (the
adapter that renders it). `.medui` generates the former today, through the chain in the previous section. **Do not reintroduce HTML/CSS parsing.**

## Reading order

1. [ADR-004](adr/ADR-004-trust-zones-in-cpp.md) — trust zones, the organising constraint
2. [ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md) — error handling per zone
3. [ADR-007](adr/ADR-007-evidence-pipeline-doctrine.md) — why artifacts are committed
4. [`getting-started.md`](getting-started.md) — building and consuming
5. [`regulatory-compliance.md`](regulatory-compliance.md) — what is and is not claimed
