# AGENTS.md

Guidance for coding agents, contributors, and maintainers working in this repository.

## 1. Project purpose and maturity

MduX is an **experimental, proof-of-concept** C++23-modules project exploring a pure-Vulkan
complement library for medical-device-oriented graphics/UI infrastructure. It is not a finished
product.

The repository also contains extensive medical-device regulatory reference material (ISO 14971,
ISO 13485, IEC 62304, IEC 62366). **This material documents intent and provides conceptual
scaffolding — it does not, by itself, establish product certification, production readiness, or
legal regulatory compliance.** Do not represent any change as certified, validated, or
production-ready based solely on the presence of this documentation.

## 2. Instruction scope and sources of truth

These root instructions apply to the whole repository unless a more specific nested `AGENTS.md`
exists closer to the files you are changing (none exists today).

When sources disagree, resolve the discrepancy using this precedence order:

1. Active build/CI configuration and the current implementation (`CMakeLists.txt`,
   `CMakePresets.json`, `.github/workflows/`, `include/`, `src/`).
2. Tests (`tests/`).
3. Current ADRs and contributor documentation (`docs/adr/`, `CONTRIBUTING.md`, `README.md`).
4. Historical or reference material (regulatory framework documents under `docs/`,
   `software_development_file/templates/`).

**Do not invent an answer to resolve a contradiction.** If sources conflict in a way that affects
your task, state the contradiction explicitly and ask, or clearly flag the assumption you are
making and why.

Known existing contradiction: `README.md`'s "Implementation Status" table marks the ISO 14971 /
ISO 13485 risk-management and quality-management frameworks as "Completed" with example
namespaces (`risk::`, `qms::`, `lifecycle::`). No such code exists in `include/` or `src/` today —
treat that table as aspirational/planned, not implemented, per the precedence order above.

### The TrustSC parity programme

MduX has a Rust sibling, [TrustSC](https://github.com/ambroise-leclerc/TrustSC), targeting the same
problem (a medical-device UI SDK with IEC 62304 Class B/C compliance modelling built in) with a more
coherent architecture. MduX is being deliberately steered toward structural parity with it. The full
roadmap is tracked as GitHub epics `#7`-`#19` on `ambroise-leclerc/MduX`, each with child issues.
Three decisions from that programme apply repository-wide:

1. **The HTML/CSS UI story is gone.** `UiFileWatcher`, `MedicalUiContent`, `MedicalUiConfig`,
   `MedicalUiRenderer`, `RenderStatistics` and `VulkanContext` were deleted by issue `#127`. What
   replaces them is `mdux.draw` (a governed description of a frame) plus `mdux.render.vulkan` (the
   adapter that renders it); `.medui` (issue `#15`) will generate the former. Do not reintroduce
   HTML/CSS parsing.
2. **The trust-zone split has landed**: `MduXCore` is a governed target that never receives
   Vulkan's include directories, so `#include <vulkan/vulkan.h>` in governed code is a link-graph
   error `mdux_verify_trust_zones()` reports at configure time (issue `#11`).
3. **Reproduced normative standard text is being purged** from `docs/` and from git history
   (issue `#7`) — see `regulatory-citations` in § 7. Do not add new material that reproduces or
   closely paraphrases a standard's wording, even though older files in the tree still do.

Treat any AGENTS.md section below that describes current architecture as authoritative for *today's
code*; treat this subsection as the direction that code is moving in.

## 3. Verified architecture summary

**Module layout** (C++23 `import`/`export` modules, verified against `CMakeLists.txt`):

| Module | Interface | Implementation |
|---|---|---|
| `mdux` (facade: version, compliance, Vulkan capability) | `include/mdux/mdux.cppm` | `src/mdux.cpp` |
| `mdux.core.result`, `mdux.core.units` | `include/mdux/core/` | header-only |
| `mdux.evidence.digest`, `.json`, `.report` | `include/mdux/evidence/` | `src/evidence/` |
| `mdux.governance`, `mdux.governance.compliance` | `include/mdux/governance/` | `src/governance/` |
| `mdux.shader.schema` (governed) | `include/mdux/shader/Schema.cppm` | `src/shader/Schema.cpp` |
| `mdux.draw` (governed) | `include/mdux/draw/Draw.cppm` | `src/draw/Draw.cpp` |
| `mdux.render.vulkan`, `mdux.render.offscreen` (adapter) | `include/mdux/render/` | `src/render/` |
| `mdux.vulkansc.memory` | `include/mdux/vulkansc/MemoryPoolManager.cppm` | `src/vulkansc/MemoryPoolManager.cpp` |
| `mdux.vulkansc.objects` (imports `mdux.vulkansc.memory`) | `include/mdux/vulkansc/DeviceObjectManager.cppm` | `src/vulkansc/DeviceObjectManager.cpp` |

Interfaces are declared under `FILE_SET CXX_MODULES` and implementations as plain `PRIVATE`
sources. Note the split: the governed modules belong to `MduXCore` and the adapter ones to `MduX`.
Not every interface has an implementation file - `mdux.core.result` and `mdux.core.units` are
header-only - and `src/governance/Justification.cpp` and `Program.cpp` implement parts of
`mdux.governance` without interfaces of their own.

**Vulkan boundary / caller-owned resources**: `mdux::render::VulkanRenderContext`
(`include/mdux/render/VulkanRenderer.cppm`) is populated by the caller — `VkDevice`,
`VkPhysicalDevice`, `VkRenderPass` and a `VkQueue` are all supplied by the host application, and
every member is initialised so a partially-filled context fails validation rather than faulting.
`mdux::render::UiRenderer` creates and owns its *own* Vulkan resources (shader modules, descriptor
set layout and pool, pipeline layout, pipeline, the two frame buffers and a default atlas) from
that context; it does not create a device, instance, or swapchain.

**Vulkan SC module**: `mdux.vulkansc.memory` and `mdux.vulkansc.objects` implement static
memory-pool and device-lifetime-object management patterns required by Vulkan SC (memory/objects
that cannot be freed until device destruction). These are independent of the `mdux` UI module and
carry their own `@compliance` Doxygen annotations referencing IEC 62304 Class C / ISO 14971.

**Windowing-dependency policy**: the `MduX` library target itself has no windowing dependency.
GLFW is linked only into `VulkanSCTriangleExample`, as *one* way a host application can create a
window and Vulkan surface — it is not a library requirement. `MedicalUiExample` deliberately does
not link it: building a frame needs neither a window nor a device, and that is part of what the
example demonstrates. `examples/BasicExample.cpp`
exists in the tree but is not currently registered as a build target in
`examples/CMakeLists.txt`; treat it as inactive/legacy unless you verify otherwise.

**Current implementation vs. planned/conceptual**: the shader pipeline (`mdux.shader.schema`,
`mdux-shaderbake`, `mdux-shaderemit`), the governed draw types (`mdux.draw`), the Vulkan renderer
and offscreen target (`mdux.render.*`), the compliance-metadata types and the two Vulkan SC
managers are implemented and built today. The
ISO 14971/ISO 13485 risk-management and quality-management framework described in `README.md`
(risk analysis engine, CAPA system, design-control stages) is conceptual/planned and not present
in `include/` or `src/`.

## 4. Repository map

- `include/mdux/*.cppm`, `include/mdux/vulkansc/*.cppm` — C++23 module interfaces (public API surface).
- `src/*.cpp`, `src/vulkansc/*.cpp` — module implementations.
- `tests/` — unit and compliance test executables, driven by `tests/CMakeLists.txt`.
- `examples/` — example programs and their `CMakeLists.txt`. `MedicalUiExample`
  (`SimpleMedicalUiExample.cpp`) builds a frame with no device and no window;
  `VulkanSCTriangleExample` owns a device and a window and renders from the baked shader package.
  `examples/*.html` are inactive/legacy mockups nothing consumes; issue #42 tracks their deletion.
- `cmake/` — CMake support modules (compiler settings, warnings, sanitizers, Doxygen, Vulkan
  discovery helpers, etc.), included from the root `CMakeLists.txt`.
- `docs/adr/` — Architecture Decision Records (see § "Verified architecture summary" above for how
  their status matters).
- `docs/iec62304/`, `docs/iso13485/`, `docs/iso14971/`, `docs/iec62366/`, `docs/iec81001/` — the
  regulatory corpus, one directory per standard: clause-range modules, a generated per-clause
  `AI-Reference.md`/`.json` index, and `schemas/*.json` field-aligned with the governance types.
  `tools/docs-lint/` checks all of it in CI.
- `docs/regulatory-compliance.md` — the scope limits this project claims, and the ones it does not.
- `docs/governance/` — the citation convention, the shared `Justification` schema, the SOUP
  register, and `superseded-documents.md`, which records every point-in-time document that was
  retired and why. Three were: the ISO 13485 and ISO 14971 framework monoliths and
  `risk-assessment-templates.md`, all superseded by the corpus above. Read that file before
  concluding content was lost.
- `docs/MduX_IEC-62304-Software-Lifecycle-Framework.md` — the last remaining framework monolith,
  superseded by `docs/iec62304/` but not yet retired; epic #10's "archive the point-in-time docs"
  covers its disposition.
- `.github/workflows/ci.yml` — the authoritative description of what actually gets built/tested in
  CI.
- `CMakePresets.json` — `ninja-msvc`, `ninja-msvc-debug`, `ninja-gcc`, `ninja-gcc-debug` and
  `ninja-clang` (issue #45).
- `CONTRIBUTING.md` — coding style, formatting, and PR conventions.

## 5. Supported environment and common commands

**Platforms**: Windows 10+ and Linux only are supported and intended — but this is not, as earlier
text here claimed, enforced by a fatal CMake check. The guard in `CMakeLists.txt` is
`if(NOT WIN32 AND NOT UNIX)`, and CMake sets `UNIX` on macOS too, so that check does not actually
block macOS. In practice macOS is excluded by the compiler-version gate instead: `AppleClang` is not
one of the recognized `CMAKE_CXX_COMPILER_ID` branches (MSVC/GNU/Clang), so it falls through to a
non-fatal `message(WARNING ...)` rather than a `FATAL_ERROR` — and even past that warning, C++23
modules scanning is not functional under AppleClang, so a macOS configure fails later for unrelated
reasons rather than being rejected up front. Treat "Windows/Linux only" as the intended, tested
scope, not a mechanically enforced restriction.

**Toolchain minimums** (enforced by fatal CMake checks in the root `CMakeLists.txt`):
- MSVC 17.14+ (Visual Studio 2022 version 17.10+)
- GCC 15+
- Clang 20+ — note the Clang CI job in `.github/workflows/ci.yml` is currently commented out
  (disabled), so Clang support is unverified in CI even though the version floor is enforced.
- CMake 4.0+
- Vulkan SDK 1.3+, discoverable by CMake's `find_package(Vulkan REQUIRED)`

**Configuring**: this is an out-of-source-build project (`cmake/PreventInSourceBuilds.cmake`
enforces this). On Windows, the `ninja-msvc` preset in `CMakePresets.json` is available:

```bash
cmake --preset ninja-msvc
cmake --build --preset ninja-msvc
```

On Linux (no preset exists — use the explicit form CI actually runs):

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DMDUX_BUILD_EXAMPLES=ON -DMDUX_BUILD_TESTS=ON -DMDUX_BUILD_DOCS=OFF
cmake --build build
```

**Build options** (`option(...)` in root `CMakeLists.txt`):
- `MDUX_BUILD_EXAMPLES` (default `ON`)
- `MDUX_BUILD_TESTS` (default `ON`)
- `MDUX_BUILD_DOCS` (default `OFF`)
- `MDUX_ENABLE_REGULATORY_DOCS` (default `ON`)

**Targets** (verified in `CMakeLists.txt`, `examples/CMakeLists.txt`, `tests/CMakeLists.txt`):
- Library: `MduX` (alias `MduX::MduX`)
- Examples: `MedicalUiExample`; `VulkanSCTriangleExample` (skipped on GCC 15 due to a documented
  compiler internal-compiler-error with C++23 modules — see the comment in
  `examples/CMakeLists.txt`)
- Tests: `unit_tests`, `compliance_tests`, `vulkansc_memory_tests`, `vulkansc_object_tests`,
  registered with CTest as `MduXUnitTests`, `MduXComplianceTests`, `VulkanSCMemoryPoolTests`,
  `VulkanSCDeviceObjectTests`
- Documentation: `doxygen-docs` (only available when `MDUX_BUILD_DOCS=ON`)

**Testing**:
```bash
ctest --test-dir build --output-on-failure
```

Do not commit generated build output; keep the build directory out-of-source and out of version
control (see `.gitignore`).

For the detailed build/test workflow, toolchain diagnosis, and evidence checklist, see
[`.agents/skills/mdux-build-and-test/SKILL.md`](.agents/skills/mdux-build-and-test/SKILL.md).

## 6. Repository-wide working rules

- Follow the naming, formatting, and documentation conventions in
  [`CONTRIBUTING.md`](CONTRIBUTING.md): `UpperCamelCase` classes/structs, `lowerCamelCase`
  functions/methods/variables, lowercase namespaces, no macros, 4-space indentation, Doxygen
  `@brief`-first comments. `.clang-format` and `.clang-tidy` are present at the repository root —
  run them before submitting changes.
- Keep changes focused and proportionate to the task.
- When adding, removing, or moving a `.cppm`/`.cpp` module pair, update the corresponding
  `FILE_SET CXX_MODULES` / source list in the relevant `CMakeLists.txt`.
- Preserve deterministic resource ownership and explicit error handling in safety-relevant code
  (see § 8 and the `mdux-regulated-change` skill for what counts as safety-relevant).
- Add or update unit, compliance, or integration tests proportionate to the change, following the
  existing patterns in `tests/`.
- Update affected documentation and ADRs when behavior or architecture changes.
- Never claim a command, build, or test passed unless you actually ran it in this session. If an
  environment limitation prevents running something, say so precisely instead of guessing at the
  outcome.

## 7. Skill routing table

| Skill | Use when | Covers |
|---|---|---|
| [`mdux-build-and-test`](.agents/skills/mdux-build-and-test/SKILL.md) | Configuring, compiling, testing, running examples, or diagnosing a local build failure. | Toolchain/SDK checks, verified configure/build/test commands, target list, evidence to report. |
| [`mdux-cpp23-vulkan-development`](.agents/skills/mdux-cpp23-vulkan-development/SKILL.md) | Changing module interfaces/implementations, Vulkan or Vulkan SC integration, rendering resources, examples, or public APIs. | Module file placement and CMake registration, import/export conventions, the Vulkan/Vulkan SC resource-ownership model, windowing policy, required test/doc updates. |
| [`mdux-regulated-change`](.agents/skills/mdux-regulated-change/SKILL.md) | A change can affect safety behavior, risk controls, compliance metadata, traceability, auditability, lifecycle documents, or claims about medical-device standards. | Impact classification, affected-artifact identification, proportionate documentation updates, traceability, review/escalation triggers, evidence-vs-intent-vs-certification distinctions. |
| [`regulatory-citations`](.agents/skills/regulatory-citations/SKILL.md) | Writing or reviewing anything that claims alignment with IEC 62304, ISO 13485, ISO 14971, IEC 62366-1, or IEC 81001-5-1. | Citation-key format, the `Justification` object, the prohibition on reproducing normative text. **Target convention** — see § 2's parity-programme note. |
| [`evidence-pipeline`](.agents/skills/evidence-pipeline/SKILL.md) | Adding or modifying a baked asset (font, shader, image, `.medui` screen, ML model) or anything under `generated/`. | Recipe→baker→committed-artifact doctrine, canonical-JSON rules, why `generated/` is never hand-edited. **Live** — the evidence kernel and `mdux-shaderbake` exist; no artifact is committed under `generated/` yet (issue `#120`). |
| [`medui-authoring`](.agents/skills/medui-authoring/SKILL.md) | Authoring or discussing a `.medui` screen. | Grammar, component dictionary, theme tokens, text budgets, `@safety_critical`. **Planned** — no `.medui` compiler exists yet (issue `#15`). |
| [`sdf-documents`](.agents/skills/sdf-documents/SKILL.md) | Filling in or reviewing a `software_development_file/` document. | Structure, the summarize-don't-duplicate rule, citing into the corpus. **Planned** — `software_development_file/` doesn't exist yet (issue `#9`). |

Detailed procedures live in the skill files, not here — this table only routes.

## 8. Definition of done

- Relevant build and tests were executed where the environment permits, with results reported
  honestly (including what was skipped and why).
- Public API documentation (Doxygen comments) and architecture documentation (this file, ADRs) are
  updated when the change affects them.
- Safety, risk, compliance, and traceability impact has been considered and recorded for any
  change touching safety-relevant behavior (see the `mdux-regulated-change` skill).
- No unverified certification or regulatory-compliance claim has been introduced or implied.
