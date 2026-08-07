# MduX

An experimental C++23-modules UI library for medical-device software, built on Vulkan.

> **⚠️ Experimental proof-of-concept. Not for production use.**
>
> MduX explores whether C++23 modules, `import std`, and a mechanically enforced trust-zone split
> can carry the kind of evidence a regulated codebase needs. It requires cutting-edge toolchains and
> changes shape as that exploration continues.
>
> **This repository establishes no certification, validation, production readiness, or regulatory
> compliance. Nothing in it has been assessed by a notified body.** The regulatory material under
> `docs/` records how such work would be organised. It is not evidence that it has been done.

![Status](https://img.shields.io/badge/status-experimental-orange)
[![Version](https://img.shields.io/github/v/tag/ambroise-leclerc/MduX?label=version)](https://github.com/ambroise-leclerc/MduX/tags)
[![Windows CI](https://github.com/ambroise-leclerc/MduX/actions/workflows/windows-build.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/windows-build.yml)
[![Linux (GCC 16) CI](https://github.com/ambroise-leclerc/MduX/actions/workflows/linux-gcc16-build.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/linux-gcc16-build.yml)
[![Docs Lint](https://github.com/ambroise-leclerc/MduX/actions/workflows/docs-lint.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/docs-lint.yml)
[![Evidence Lint](https://github.com/ambroise-leclerc/MduX/actions/workflows/evidence-lint.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/evidence-lint.yml)
[![Compliance Docs](https://github.com/ambroise-leclerc/MduX/actions/workflows/compliance-docs.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/compliance-docs.yml)

[![CodeQL](https://github.com/ambroise-leclerc/MduX/actions/workflows/codeql.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/codeql.yml)
[![OSV-Scanner](https://github.com/ambroise-leclerc/MduX/actions/workflows/osv-scanner.yml/badge.svg)](https://github.com/ambroise-leclerc/MduX/actions/workflows/osv-scanner.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ambroise-leclerc/MduX/badge)](https://scorecard.dev/viewer/?uri=github.com/ambroise-leclerc/MduX)
[![License: EPL-2.0](https://img.shields.io/badge/License-EPL--2.0-blue)](LICENSE)

## What this actually is

A library with three zones, checked at configure time rather than by review
([ADR-004](docs/adr/ADR-004-trust-zones-in-cpp.md)):

- **Governed** (`MduXCore`) — reaches `std` and nothing else. Frame description, evidence
  primitives, governance records, ML inference. Never throws.
- **Adapter** (`MduX`) — the Vulkan renderer. The only zone that links Vulkan.
- **Host tools** — bakers that run at authoring time and are never linked into a device target.

`mdux_verify_trust_zones()` walks the full link graph and fails the configure step if a governed
target ever reaches Vulkan or a windowing library.

**What it is not:** a UI toolkit. There is no text, no layout, and no widgets yet — see
[Implementation status](#implementation-status). It is also not a quality-management system, a risk
engine, or a lifecycle framework; there is no `mdux::risk`, `mdux::qms` or `mdux::lifecycle`
namespace, and no code here generates a Design History File, a Risk Management File, or an audit
trail.

## Two ideas worth the tour

**Evidence is baked, committed, and re-derived.** A host tool turns a recipe into
`generated/<kind>/<id>/` — `package.json`, `report.json`, a payload — and those files are committed.
CI re-runs the baker and asserts byte-identity. Four artifacts live there today: two shader packages
and two ML model packages. A normal build never writes into the source tree
([ADR-007](docs/adr/ADR-007-evidence-pipeline-doctrine.md)).

**Embedded ML with no foreign inference stack.** `mdux.ml.kernels` is imported by both the device
runtime and the host golden-vector generator — the same object file, so a golden-vector mismatch on
device means the FPU or the toolchain differs, not that two implementations drifted.
`Classifier1D::create()` verifies the weight digest and re-runs every golden vector bit-for-bit
before it will construct, and refuses to if any diverge
([ADR-008](docs/adr/ADR-008-zero-soup-ml-inference.md)).

## Quick look

```cpp
import std;
import mdux;
import mdux.core.units;
import mdux.draw;

mdux::initialize();

// Governed: no Vulkan types, no allocation, storage supplied by the caller.
static std::array<mdux::draw::UiVertex, 64> vertices;
static std::array<mdux::draw::Index, 96> indices;
static std::array<mdux::draw::DrawCommand, 8> commands;
constexpr mdux::draw::DrawBudget budget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8};

auto list = mdux::draw::DrawList::create(vertices, indices, commands, budget);
if (!list.has_value()) {
    return handleError(mdux::draw::describe(list.error()));
}

// Every add* records the primitive completely or records nothing and returns an error.
// A frame that does not fit its budget is refused, never truncated.
constexpr mdux::core::ColorRgba8 statusGreen{.r = 60, .g = 107, .b = 44, .a = 255};
if (auto added = list->addSolidRect({.x = 16, .y = 64, .width = 120, .height = 24}, statusGreen);
    !added.has_value()) {
    return handleError(mdux::draw::describe(added.error()));
}
```

Every symbol above exists. A module is consumed with `import`, not `#include` of a `.cppm`, and
`mdux::initialize()` takes no arguments — there is no `MedicalDeviceContext`.

`examples/SimpleMedicalUiExample.cpp` is the complete governed half, which needs no device and no
window. `examples/VulkanSCTriangleExample.cpp` is the device half.

## Getting started

Build, test, and consumption instructions are in
**[`docs/getting-started.md`](docs/getting-started.md)**, including the honest limitations.

The short version, on Linux:

```bash
cmake --preset ninja-gcc
cmake --build --preset ninja-gcc
ctest --preset ninja-gcc --output-on-failure
```

Requires **GCC 16+**, **MSVC 17.14+** or **Clang 20+**, **CMake 4.0+**, **Ninja**, and the
**Vulkan SDK 1.3+**. The Visual Studio generator does not support `import std` and is rejected
deliberately.

## Implementation status

Derived from the targets and tests that build on `develop`.

| Area | Status | What is actually there |
|------|--------|------------------------|
| **Governed core** (`MduXCore`, never links Vulkan) | | |
| `mdux.core.result`, `mdux.core.units` | Implemented | `Result` over `std::expected`; `Px`, `Rect`, `ColorRgba8`, `Extent2D` |
| `mdux.draw` | Implemented | 24-byte `UiVertex`, fixed-budget `DrawList`, explicit refusal on overflow |
| `mdux.evidence.*` | Implemented | SHA-256, canonical JSON, `BakeReport` |
| `mdux.governance*` | Implemented | governance records, compliance program types, traceability matrix export |
| `mdux.shader.schema` | Implemented | canonical shader package types; names no Vulkan type |
| `mdux.ml.schema`, `.kernels`, `.runtime` | Implemented | `f32` kernels, fail-closed `Classifier1D`, no heap in `predict()` |
| **Adapter zone** (Vulkan) | | |
| `mdux.render.vulkan` | Implemented | pipeline built from a baked package, fixed-budget `record()` |
| `mdux.render.offscreen` | Implemented | headless target and CPU readback, used by the pixel test |
| `mdux.vulkansc.*` | Partial | memory-pool and device-object patterns; **not** true Vulkan SC |
| **Host tools** (never linked into a device target) | | |
| `mdux-shaderbake`, `mdux-shaderemit` | Implemented | SPIR-V reflection, byte-verified packages, generated C++ |
| `mdux-mlbake` | Implemented | safetensors import, golden generation, byte-verified model packages |
| `mdux-docs-lint`, `mdux-evidence-lint` | Implemented | run in CI |
| **Regulatory material** | | |
| Standards corpus under `docs/` | Documentation only | five clause-structured references with generated indexes and schemas |
| Software Development File | Documentation only | templates and records under `software_development_file/` |
| Risk management, QMS, lifecycle *code* | **Not started** | no `mdux::risk`, `mdux::qms` or `mdux::lifecycle` exists |
| **Not started** | | |
| Text and glyph rendering | Planned | [#14](https://github.com/ambroise-leclerc/MduX/issues/14) |
| `.medui` compiler | Planned | [#15](https://github.com/ambroise-leclerc/MduX/issues/15) |
| Content components (`SignalTrace`, `StatusIndicator`, …) | Planned | [#17](https://github.com/ambroise-leclerc/MduX/issues/17) |

The HTML/CSS path that earlier revisions described was **deleted** by
[#127](https://github.com/ambroise-leclerc/MduX/issues/127) — `MedicalUiRenderer::render()` recorded
no Vulkan commands. `mdux.draw` plus `mdux.render.vulkan` replace it; `.medui` will generate the
former.

## Regulatory material, and what it is for

`docs/iec62304/`, `docs/iso13485/`, `docs/iso14971/`, `docs/iec62366/` and `docs/iec81001/` are
clause-structured reference modules with per-clause indexes and JSON Schemas, linted in CI. They
state, per clause, either the concrete mechanism this repository provides or that none exists — the
second answer appears often, and deliberately.

`docs/governance/` holds the citation convention, the SOUP register, and a record of every retired
document and why.

**MduX is a software library, not a manufacturer.** It runs no management review, no CAPA process
and no supplier qualification, and it has no customers of its own. Integrating it does not transfer
any regulatory obligation away from the manufacturer. See
[`docs/regulatory-compliance.md`](docs/regulatory-compliance.md) for the scope limits this project
claims, and [ADR-006](docs/adr/ADR-006-no-reproduction-of-normative-standard-text.md) for why no
normative standard text appears in this tree.

## Documentation

| | |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | what is in the tree, and what is planned |
| [`docs/getting-started.md`](docs/getting-started.md) | building, testing, consuming, and the limitations |
| [`docs/adr/`](docs/adr/) | the decision trail — start with ADR-004 |
| [`docs/regulatory-compliance.md`](docs/regulatory-compliance.md) | what this project does and does not claim |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | style, formatting, PR conventions |
| [`AGENTS.md`](AGENTS.md) | guidance for coding agents and contributors |

The roadmap is tracked as GitHub epics
[#7–#19](https://github.com/ambroise-leclerc/MduX/issues?q=is%3Aissue+label%3Aepic).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). In short: match the surrounding style, add tests
proportionate to the change, update the affected documentation and ADRs, and do not introduce a
certification or compliance claim.

## License

Eclipse Public License 2.0 ([EPL-2.0](LICENSE)).

## Contact

Open an issue.
