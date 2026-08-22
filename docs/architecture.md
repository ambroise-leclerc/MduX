# MduX architecture

What is in the tree on `develop`, and what is not.

Every target, module and path named here exists. Where something is planned rather than built, it
says so and links the issue. If you find a statement here that the build contradicts, the statement
is the bug — this document is meant to be checkable against `CMakeLists.txt` and `ctest`, not taken
on trust.

**This document establishes no certification, validation, production readiness, or regulatory
compliance.** MduX is an experimental proof-of-concept. See
[`regulatory-compliance.md`](regulatory-compliance.md) for the scope limits the project claims.

## Trust zones

The organising idea, from [ADR-004](adr/ADR-004-trust-zones-in-cpp.md). Three zones, with the
boundary between them enforced at configure time rather than by review:

| Zone | Targets | May reach | Exceptions |
|---|---|---|---|
| **Governed** | `MduXCore`, `MduX_warnings` | `std` only | never throws ([ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md)) |
| **Adapter** | `MduX` | governed + Vulkan | throws where Vulkan makes it unavoidable |
| **Host tools** | `MduXToolsCommon`, `MduXShaderBakeLib`, `MduXMlBakeLib`, `MduXTextBakeLib`, `MduXMeduiLib` | anything | may throw freely; never linked into a device target |

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
| `mdux.text.schema` | `include/mdux/text/Schema.cppm` | `src/text/Schema.cpp` |
| `mdux.font.schema` | `include/mdux/font/Schema.cppm` | `src/font/Schema.cpp` |
| `mdux.text.draw` | `include/mdux/text/Draw.cppm` | `src/text/Draw.cpp` |
| `mdux.draw` | `include/mdux/draw/Draw.cppm` | `src/draw/Draw.cpp` |
| `mdux.ml.schema` | `include/mdux/ml/Schema.cppm` | header-only |
| `mdux.ml.kernels` | `include/mdux/ml/Kernels.cppm` | `src/ml/Kernels.cpp` |
| `mdux.ml.runtime` | `include/mdux/ml/Runtime.cppm` | `src/ml/Runtime.cpp` |
| `mdux.medui.schema` | `include/mdux/medui/Schema.cppm` | header-only |
| `mdux.medui.screen` | `include/mdux/medui/Screen.cppm` | `src/medui/Screen.cpp` |

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
| `MduXMlBakeLib` | `tools/ml/` | `mdux-mlbake` |
| `MduXTextBakeLib` | `tools/text/` | `mdux-textbake`; also hosts `mdux.tools.truetype` (the host-only glyf parser with cmap/hmtx, #158), `mdux.tools.atlaspacker` (the shelf packer, #160) and `mdux.text.raster` (the glyph rasteriser, #159) |
| `MduXMeduiLib` | `tools/medui/` | the `.medui` compiler (#15); the shared `MEDUI-E` diagnostic registry (#191), parser (#192), component/theme/locale semantic analyzer (#193), integer-only bounded layout solver (#194), the text-budget check that measures resolved boxes against the widest approved translation (#195), and the golden references that say where safety-critical content must appear (#196), the canonical package with its two C++ emitters (#197) and the compiler driver behind `mdux-meduic` (#198) |

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

Six artifacts are committed today:

| Artifact | Baker | Payload |
|---|---|---|
| `generated/shader/mdux-ui/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/shader/triangle/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/model/ecg-demo/` | `mdux-mlbake` | `weights.bin` |
| `generated/model/ecg-demo-alt/` | `mdux-mlbake` | `weights.bin` |
| `generated/font/dejavu-ui/` | `mdux-textbake` | `atlas.bin` |
| `generated/screen/endoscope-monitor/` | `mdux-meduic` | `package.json` + `goldens.json` |

The screen is the one entry whose payload is not opaque bytes, and ADR-012 explains why: a screen
cannot bake vertices, because four of the eleven components in the dictionary — `NumericDisplay`,
`SignalTrace`, `StatusIndicator` and `Clock` — draw from live data, and their geometry does not exist
until the frame does. What `package.json` carries instead is layout: where each node is, how much it
may draw, and which validated token and key it draws with.

`goldens.json` is a sidecar with a different consumer — #16's frame verifier, not the runtime — and a
different rule. ADR-011 puts **every `@safety_critical` node and every node with an explicit
`position:`** in the golden set, which is why the committed screen has one entry: its `SignalTrace`
is positioned, not annotated. Both files are reviewable as text, which is the point.

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

Floating-point determinism is enforced at configure time by
[`cmake/MduXDeterminism.cmake`](../cmake/MduXDeterminism.cmake), which sets `-ffp-contract=off` and
fails the build if `-ffast-math`, `/fp:fast`, `/fp:contract` or similar reaches a governed target
from anywhere — including through a dependency's interface options.

## Tests

Two frameworks, one discovery contract ([ADR-009](adr/ADR-009-in-repository-test-framework.md)):
the in-repository `MduXTest` across nine executables, and SpecLab for Given/When/Then across seven.
`mdux_discover_tests()` registers one CTest entry per case, so a failure names the scenario rather
than the binary.

Labels select the evidence-bearing suites:

| Label | Means |
|---|---|
| `evidence` | a committed artifact is byte-identical to a freshly baked one — and nothing else |
| `evidence-unit` | unit tests of the evidence modules themselves |
| `determinism` | the ML kernels produce the frozen bit patterns |
| `noheap` | `predict()` performs no allocation |
| `governed` | no governed object contains a throw expression — a gate on GCC/Clang, informational on MSVC ([ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md)) |
| `pixel` | rendered output matches expectations, under lavapipe |
| `regulatory` | corpus indexes and schemas are current |

The `evidence` / `evidence-unit` split is deliberate: a broken SHA-256 test and a drifted artifact
must not produce the same CI signal. `governed` is separate from `noheap` for the same reason: both
run `cmake/MduXNoHeapScan.cmake`, but over different objects with different forbidden sets, and a
CI leg selecting one should not silently start covering the other.

`governed` includes a negative test on GCC/Clang. `governed.noThrow.symbolScan.negative` scans a
deliberately non-conforming object (`tests/governed/ThrowFixture.cpp`) and passes only when the scan
rejects it with the expected message — because a check that has only ever run against conforming
code passes identically when it is working and when it is looking for the wrong thing.

Neither test gates on MSVC, and the reason is worth knowing: the MSVC STL inlines its own throw
sites, so `_CxxThrowException` in a governed object is the same symbol whether it came from a
hand-written `throw` or from a `std::string` growth path. The scan reports there instead of
failing, and [`mdux-governed-lint`](../tools/governed-lint/) — which reads source and is
toolchain-independent — is what enforces the rule on Windows.

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
| `.medui` compiler | [#15](https://github.com/ambroise-leclerc/MduX/issues/15) | complete front to back: parsing, semantic validation, bounded layout, text budgets, golden references, the canonical package, both C++ emitters, `mdux-meduic`, `mdux-medui-check`, and a governed runtime that draws a compiled screen. One committed screen reaches pixels in `ScreenPixelTests`; what remains is the text package (#201) and the components' own geometry (#17) |
| Rendered-truth verification | [#16](https://github.com/ambroise-leclerc/MduX/issues/16) | beyond the current pixel test |
| Content components | [#17](https://github.com/ambroise-leclerc/MduX/issues/17) | `SignalTrace`, `StatusIndicator`, `NumericDisplay` and the rest |
| `constexpr` ML package emitter | [#153](https://github.com/ambroise-leclerc/MduX/issues/153) | would remove the startup JSON parse |

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
