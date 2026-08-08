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
| **Host tools** | `MduXToolsCommon`, `MduXShaderBakeLib`, `MduXMlBakeLib`, `MduXTextBakeLib` | anything | may throw freely; never linked into a device target |

`mdux_verify_trust_zones()` in [`cmake/MduXTrustZones.cmake`](../cmake/MduXTrustZones.cmake) walks
the full link graph of every declared-governed target at the end of configure and fails on a
forbidden dependency.

**What that guarantee is, precisely:** a governed target's link graph reaches no Vulkan or windowing
target, directly or transitively, and the check runs on every configure rather than at review time.
It is a *link-graph* property, checked mechanically.

**What it is not:** it is not `#![forbid(unsafe_code)]`. It does not prove the absence of undefined
behaviour, and no combination of C++ tooling here does. Governed modules are compiled without access
to platform, graphics or OS headers, are covered by determinism and no-heap tests, and are held to
an enforced warning set — that is the whole of the claim.

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
| `mdux.text.raster` | `include/mdux/text/Raster.cppm` | `src/text/Raster.cpp` |
| `mdux.draw` | `include/mdux/draw/Draw.cppm` | `src/draw/Draw.cpp` |
| `mdux.ml.schema` | `include/mdux/ml/Schema.cppm` | header-only |
| `mdux.ml.kernels` | `include/mdux/ml/Kernels.cppm` | `src/ml/Kernels.cpp` |
| `mdux.ml.runtime` | `include/mdux/ml/Runtime.cppm` | `src/ml/Runtime.cpp` |

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
| `MduXTextBakeLib` | `tools/text/` | `mdux-textbake`; also hosts `mdux.tools.truetype` (the host-only glyf parser, #158) |

Host tools parse untrusted input, so they are deliberately outside the governed zone. They are
never linked into `MduXCore` or `MduX` and are absent from the install/export set.

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

Four artifacts are committed today:

| Artifact | Baker | Payload |
|---|---|---|
| `generated/shader/mdux-ui/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/shader/triangle/` | `mdux-shaderbake` | `shaders.spv` |
| `generated/model/ecg-demo/` | `mdux-mlbake` | `weights.bin` |
| `generated/model/ecg-demo-alt/` | `mdux-mlbake` | `weights.bin` |

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
| `pixel` | rendered output matches expectations, under lavapipe |
| `regulatory` | corpus indexes and schemas are current |

The `evidence` / `evidence-unit` split is deliberate: a broken SHA-256 test and a drifted artifact
must not produce the same CI signal.

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
| Font and text pipeline | [#14](https://github.com/ambroise-leclerc/MduX/issues/14) | S1 (`mdux.text.schema` + `mdux-textbake` skeleton) landed. S2 (`mdux.tools.truetype` glyf parser, #158) landed. S3 (`mdux.text.raster` coverage rasteriser, #159) landed; S4–S6 open |
| `.medui` compiler | [#15](https://github.com/ambroise-leclerc/MduX/issues/15) | the replacement for the retired HTML/CSS path |
| Rendered-truth verification | [#16](https://github.com/ambroise-leclerc/MduX/issues/16) | beyond the current pixel test |
| Content components | [#17](https://github.com/ambroise-leclerc/MduX/issues/17) | `SignalTrace`, `StatusIndicator`, `NumericDisplay` and the rest |
| `constexpr` ML package emitter | [#153](https://github.com/ambroise-leclerc/MduX/issues/153) | would remove the startup JSON parse |

### The HTML/CSS path is gone, not planned

`MedicalUiRenderer`, `MedicalUiConfig`, `MedicalUiContent`, `UiFileWatcher`, `UiReloadEvent` and
`RenderStatistics` were **deleted** by [#127](https://github.com/ambroise-leclerc/MduX/issues/127).
`MedicalUiRenderer::render()` recorded no Vulkan commands and `uiPipeline` stayed `VK_NULL_HANDLE`
for the object's whole lifetime; it was removed rather than fixed.

What replaces it is `mdux.draw` (a governed description of a frame) plus `mdux.render.vulkan` (the
adapter that renders it). `.medui` will generate the former. **Do not reintroduce HTML/CSS parsing.**

## Reading order

1. [ADR-004](adr/ADR-004-trust-zones-in-cpp.md) — trust zones, the organising constraint
2. [ADR-005](adr/ADR-005-error-handling-and-exceptions-policy.md) — error handling per zone
3. [ADR-007](adr/ADR-007-evidence-pipeline-doctrine.md) — why artifacts are committed
4. [`getting-started.md`](getting-started.md) — building and consuming
5. [`regulatory-compliance.md`](regulatory-compliance.md) — what is and is not claimed
