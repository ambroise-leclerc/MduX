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
   `risk-assessment-templates.md`).

**Do not invent an answer to resolve a contradiction.** If sources conflict in a way that affects
your task, state the contradiction explicitly and ask, or clearly flag the assumption you are
making and why.

Known existing contradiction: `README.md`'s "Implementation Status" table marks the ISO 14971 /
ISO 13485 risk-management and quality-management frameworks as "Completed" with example
namespaces (`risk::`, `qms::`, `lifecycle::`). No such code exists in `include/` or `src/` today —
treat that table as aspirational/planned, not implemented, per the precedence order above.

## 3. Verified architecture summary

**Module layout** (C++23 `import`/`export` modules, verified against `CMakeLists.txt`):

| Module | Interface | Implementation |
|---|---|---|
| `mdux` | `include/mdux/mdux.cppm` | `src/mdux.cpp` |
| `mdux.vulkansc.memory` | `include/mdux/vulkansc/MemoryPoolManager.cppm` | `src/vulkansc/MemoryPoolManager.cpp` |
| `mdux.vulkansc.objects` (imports `mdux.vulkansc.memory`) | `include/mdux/vulkansc/DeviceObjectManager.cppm` | `src/vulkansc/DeviceObjectManager.cpp` |

Every `.cppm` interface has a matching `.cpp` implementation file, and both are declared in the
`MduX` target in `CMakeLists.txt` (interfaces under `FILE_SET CXX_MODULES`, implementations as
plain `PRIVATE` sources).

**Vulkan boundary / caller-owned resources**: the `mdux` module's `VulkanContext` struct
(`include/mdux/mdux.cppm`) is populated by the caller — `VkDevice`, `VkPhysicalDevice`,
`VkCommandBuffer`, and a compatible `VkRenderPass` are all supplied by the host application.
`MedicalUiRenderer` creates and owns its *own* Vulkan resources (descriptor set layout/pool,
pipeline layout, pipeline) from that caller-supplied context; it does not create a device,
instance, or swapchain.

**Vulkan SC module**: `mdux.vulkansc.memory` and `mdux.vulkansc.objects` implement static
memory-pool and device-lifetime-object management patterns required by Vulkan SC (memory/objects
that cannot be freed until device destruction). These are independent of the `mdux` UI module and
carry their own `@compliance` Doxygen annotations referencing IEC 62304 Class C / ISO 14971.

**Windowing-dependency policy**: the `MduX` library target itself has no windowing dependency —
`include/mdux/mdux.cppm` includes only `<vulkan/vulkan.h>` and `<stdint.h>` in its global module
fragment. GLFW is used only by example programs (`examples/CMakeLists.txt` links GLFW into the
`MedicalUiExample` and `VulkanSCTriangleExample` targets) as *one* way a host application can
create a window and Vulkan surface — it is not a library requirement. `examples/BasicExample.cpp`
exists in the tree but is not currently registered as a build target in
`examples/CMakeLists.txt`; treat it as inactive/legacy unless you verify otherwise.

**Current implementation vs. planned/conceptual**: the `MedicalUiRenderer`, `UiFileWatcher`,
compliance-metadata types, and the two Vulkan SC managers are implemented and built today. The
ISO 14971/ISO 13485 risk-management and quality-management framework described in `README.md`
(risk analysis engine, CAPA system, design-control stages) is conceptual/planned and not present
in `include/` or `src/`.

## 4. Repository map

- `include/mdux/*.cppm`, `include/mdux/vulkansc/*.cppm` — C++23 module interfaces (public API surface).
- `src/*.cpp`, `src/vulkansc/*.cpp` — module implementations.
- `tests/` — unit and compliance test executables, driven by `tests/CMakeLists.txt`.
- `examples/` — example programs and their `CMakeLists.txt`; `examples/*.html` are sample medical
  UI definitions consumed by the examples.
- `cmake/` — CMake support modules (compiler settings, warnings, sanitizers, Doxygen, Vulkan
  discovery helpers, etc.), included from the root `CMakeLists.txt`.
- `docs/adr/` — Architecture Decision Records (see § "Verified architecture summary" above for how
  their status matters).
- `docs/iec62304/`, `docs/iso13485/` — structured regulatory reference documentation with
  AI-automation schemas and code examples.
- Top-level `MduX_IEC-62304-*.md`, `MduX_ISO-13485-*.md`, `MduX_ISO-14971-*.md`,
  `MduX-*-AI-Reference.md`, `risk-assessment-templates.md` — regulatory framework reference
  documents (conceptual; see § 1).
- `.github/workflows/ci.yml` — the authoritative description of what actually gets built/tested in
  CI.
- `CMakePresets.json` — currently defines a single Windows-only preset (`ninja-msvc`); there is no
  Linux preset.
- `CONTRIBUTING.md` — coding style, formatting, and PR conventions.

## 5. Supported environment and common commands

**Platforms**: Windows 10+ and Linux only (enforced by a fatal CMake check; there is no macOS
build path in `CMakeLists.txt`).

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

Detailed procedures live in the skill files, not here — this table only routes.

## 8. Definition of done

- Relevant build and tests were executed where the environment permits, with results reported
  honestly (including what was skipped and why).
- Public API documentation (Doxygen comments) and architecture documentation (this file, ADRs) are
  updated when the change affects them.
- Safety, risk, compliance, and traceability impact has been considered and recorded for any
  change touching safety-relevant behavior (see the `mdux-regulated-change` skill).
- No unverified certification or regulatory-compliance claim has been introduced or implied.
