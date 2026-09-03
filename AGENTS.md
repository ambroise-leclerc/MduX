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

Two documents are now the fastest way to resolve a question about current state, and both are
checked against the build rather than written from memory:
[`docs/architecture.md`](docs/architecture.md) for what exists and what is planned, and
[`docs/getting-started.md`](docs/getting-started.md) for supported build and consumption surfaces.

The `README.md` contradiction previously recorded here — an "Implementation Status" table
advertising risk-management and quality-management frameworks with `risk::`, `qms::` and
`lifecycle::` namespaces that do not exist — was fixed by issue `#111`. No such code exists, and
the README now says so rather than claiming otherwise above the table.

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
3. **Reproduced normative standard text is gone**, from `docs/` and from git history (issue `#7`,
   closed) — see `regulatory-citations` in § 7 and [ADR-006](docs/adr/ADR-006-no-reproduction-of-normative-standard-text.md).
   Do not add new material that reproduces or closely paraphrases a standard's wording.
   `mdux-docs-lint` enforces this in CI.
4. **Evidence is baked and committed** (issue `#12`, closed). Five artifacts live under
   `generated/`: two shader packages, two ML model packages and one font package. A normal build never writes into
   the source tree — `mdux-bake-update` is the only path that does, run deliberately by an author
   who commits the diff. CI asserts byte-identity on both toolchain legs.
5. **Zero-SOUP ML inference has landed** (issue `#18`, closed). `mdux.ml.kernels` is imported by
   both the device runtime and the host baker, and `Classifier1D::create()` fails closed on a
   digest or golden-vector mismatch. See [ADR-008](docs/adr/ADR-008-zero-soup-ml-inference.md).

Waves 1 to 4 of that roadmap have shipped (v0.2.0, v0.3.0, v0.4.0, v0.5.0). Wave 4 was the font
and text pipeline (`#14`), closed by `#162`. Wave 5 is the `.medui` compiler (`#15`), and all
twelve of its children have landed: a screen goes from source to a bounded, budgeted,
golden-annotated set of rectangles, to a committed byte-compared artifact, to `constexpr` C++, to
draw commands recorded without allocating, to a pixel compared under lavapipe. What the wave leaves
behind is component content rather than path: a baked text package now lets the governed runtime
draw a `Label`, while live-data and composite components such as `NumericDisplay` and `SignalTrace`
remain deferred (`#17`).

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
| `mdux.ml.schema` (governed) | `include/mdux/ml/Schema.cppm` | header-only |
| `mdux.ml.kernels` (governed) | `include/mdux/ml/Kernels.cppm` | `src/ml/Kernels.cpp` |
| `mdux.ml.runtime` (governed) | `include/mdux/ml/Runtime.cppm` | `src/ml/Runtime.cpp` |
| `mdux.draw` (governed) | `include/mdux/draw/Draw.cppm` | `src/draw/Draw.cpp` |
| `mdux.verify` (governed) | `include/mdux/verify/Verify.cppm` | `src/verify/Verify.cpp` |
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
example demonstrates. `EcgClassifierExample` links neither Vulkan nor GLFW either — it embeds its
model and weights with `mdux_embed_blob()` and opens no files at all.

All three examples in `examples/CMakeLists.txt` are active build targets; there is no inactive
example source left in the tree.

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
  retired and why. The three framework monoliths (IEC 62304, ISO 13485, ISO 14971),
  `risk-assessment-templates.md` and the Catch2 implementation plan are all retired there, each
  superseded by the clause corpus above or by an ADR. Read that file before concluding content was
  lost — `git log --follow --diff-filter=D -- <path>` recovers any of them in full.
- `.github/workflows/*.yml` — one file per CI job (`windows-build.yml`, `linux-gcc16-build.yml`,
  `macos-arm64-build.yml`,
  `security-analysis.yml`, `compliance-docs.yml`, `docs-lint.yml`, `evidence-lint.yml`, plus
  `codeql.yml`/`osv-scanner.yml`/`scorecard.yml`), the authoritative description of what actually
  gets built/tested in CI.
- `CMakePresets.json` — `ninja-msvc`, `ninja-msvc-debug`, `ninja-gcc`, `ninja-gcc-debug`,
  `ninja-clang`, `ninja-macos-clang` and `ninja-macos-clang-debug`.
- `CONTRIBUTING.md` — coding style, formatting, and PR conventions.

## 5. Supported environment and common commands

**Platforms**: Windows 10+, Linux, and Apple Silicon macOS are supported and tested. macOS has one
verified tuple: upstream LLVM/Clang 21.1.8 with libc++, CMake 4.3.1, Ninja, and LunarG Vulkan SDK
1.4.309.0/MoltenVK. `cmake --preset ninja-macos-clang` selects the repository toolchain file, which
also supplies `llvm-ar`, `llvm-ranlib`, the libc++ modules manifest and the active macOS SDK.
AppleClang, GCC on macOS, Intel Macs, and version drift are rejected deliberately. Do not generalise
a result from that tuple into support for an untested macOS configuration.

**Toolchain minimums** (enforced by fatal CMake checks in the root `CMakeLists.txt`):
- MSVC 17.14+ (Visual Studio 2022 version 17.10+)
- GCC 16+
- Clang 20+ on Linux remains manual-CI only. macOS requires upstream Clang 21.1.8 exactly and is
  automatic CI; that does not verify Linux Clang.
- CMake 4.0-4.3. This is a window, not a floor: the root `CMakeLists.txt` rejects 4.4+ with a
  fatal error until that series' experimental `import std` gate has been reviewed, because the
  gate UUID is version-specific and an unreviewed value silently disables `import std` rather
  than failing. Raise the ceiling deliberately when qualifying a new series.
- Vulkan SDK 1.3+, discoverable by CMake's `find_package(Vulkan REQUIRED)`

**Configuring**: this is an out-of-source-build project (`cmake/PreventInSourceBuilds.cmake`
enforces this). Plain CMake on Linux and Windows:

```bash
mkdir build && cd build
cmake .. -G Ninja
cmake --build .
```

`-G Ninja` is mandatory and checked at configure time: CMake implements C++ modules only for the
Ninja family and Visual Studio 17.4+, and Visual Studio cannot do `import std`. A bare `cmake ..`
takes the platform default and stops with a message naming the generator it found.
`export CMAKE_GENERATOR=Ninja` removes the need for the flag.

On Apple Silicon macOS use the verified preset instead:

```bash
export MDUX_LLVM_ROOT=/path/to/llvm-21.1.8
cmake --preset ninja-macos-clang
cmake --build --preset ninja-macos-clang
ctest --preset ninja-macos-clang --output-on-failure
```

`CMakePresets.json` also defines `ninja-gcc`, `ninja-msvc`, `ninja-clang`,
`ninja-macos-clang` and `-debug` variants.
Those exist so each CI workflow invokes a configuration this repository owns instead of a
look-alike command line; they are not needed to build by hand, and each uses its own binary dir.

**Build options** (`option(...)` in root `CMakeLists.txt`):
- `MDUX_BUILD_EXAMPLES` (default `ON`)
- `MDUX_BUILD_TESTS` (default `ON`)
- `MDUX_BUILD_DOCS` (default `OFF`)
- `MDUX_ENABLE_REGULATORY_DOCS` (default `ON`)

**Targets** (verified in `CMakeLists.txt`, `examples/CMakeLists.txt`, `tests/CMakeLists.txt`):
- Libraries: `MduXCore` (alias `MduX::Core`, governed) and `MduX` (alias `MduX::MduX`, adapter;
  PUBLIC-links `MduXCore`)
- Host-tool libraries and executables: `MduX::ToolsCommon`, `MduX::ShaderBakeLib`,
  `MduX::MlBakeLib`, `MduX::TextBakeLib`, `MduX::MeduiLib`, `MduX::VerifyUiLib`;
  `mdux-shaderbake`, `mdux-shaderemit`, `mdux-mlbake`, `mdux-mlemit`, `mdux-textbake`,
  `mdux-meduic`, `mdux-medui-check`, `mdux-screenemit`, `mdux-verify-ui`, and `mdux-verify-bake`.
  Not exported.
- Examples: `MedicalUiExample`; `VulkanSCTriangleExample` (built on every supported compiler; the
  GCC 15 ICE guard was removed when the floor rose to GCC 16); `EcgClassifierExample` (epic #18 -
  links `MduX::Core`, needs no Vulkan or window, consumes generated `constexpr` model metadata, and
  embeds only its weight blob with `mdux_embed_blob()`)
- Tests: twenty-five executables. Nine on the in-repository MduXTest framework (`core_tests`,
  `evidence_tests`, `tools_tests`, `unit_tests`, `compliance_tests`, `render_tests`,
  `offscreen_tests`, `vulkansc_memory_tests`, `vulkansc_object_tests`) and fifteen on SpecLab
  (`shader_spec`, `draw_spec`, `tools_spec`, `bridge_spec`, `ml_spec`, `ml_tools_spec`,
  `ml_noheap_spec`, `font_spec`, `text_spec`, `text_tools_spec`, `medui_spec`,
  `medui_tools_spec`, `medui_noheap_spec`, `verify_spec`, `verify_ui_spec`) — see ADR-009 — plus the
  dedicated `verify_ui_pixel_test`. `mdux_discover_tests()` registers one CTest entry per case, so
  `ctest -R <scenario>` selects an individual test.
- Test labels, which the CI steps select on: `evidence` (a committed artifact is byte-identical to
  a freshly baked one, and nothing else carries it), `evidence-unit`, `determinism`, `noheap`,
  `pixel`, `regulatory`, `verify` (`mdux-verify-ui` over a committed screen bundle, registered per
  screen by `mdux_compile_screen()`; asserted on the three render legs, and distinct from `evidence`
  because it compares a frame to a screen rather than bytes to bytes).
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

### Branch naming

Work branches use the scheme GitHub's **"Create a branch for this issue"** button generates: the
issue number, a dash, then the slugified issue title.

```
158-hand-parsed-truetype-glyf-only
14-text-schema
```

This is not cosmetic. Every workflow filters `pull_request` on the pattern `[0-9]+-*`, and
**those filters match a pull request's base branch, not its head**. A PR whose base matches no
listed pattern reports no checks at all — not failures, *nothing* — which is the failure mode
easiest to miss on review. So:

- Create branches from the issue, with that button or by writing the same name by hand.
- Target `develop`, or the predecessor branch for a stack. The only branches that target `master`
  are same-repository `release/vX.Y.Z` branches; the `Branch Topology` check rejects every other
  source.
- A stacked PR (one targeting its predecessor rather than `develop`, so a reviewer sees one
  issue's diff instead of the cumulative one) is covered automatically, because its base is
  itself an issue branch.
- `master`, `develop` and the older `feat/**` prefix also match. `feat/**` predates this
  convention and is kept only for branches already in flight.
- A branch named anything else (`fix-typo`, `wip`, `my-feature`) gets **no CI on a PR based on
  it**. If you need one, add its pattern to the `branches:` list of every workflow under
  `.github/workflows/` that has a `pull_request:` trigger.

`push:` triggers stay limited to `master` and `develop` deliberately: an open PR already covers its
own branch, and adding work branches there would run every workflow twice per commit.

**An unmergeable PR does not run GitHub's `pull_request` workflows.** It can therefore show a green
review-bot check while every build, test and lint check is absent. Treat missing checks as missing
evidence, never as a pass. Branch protection on `master` requires `Branch Topology` and the
repository's build, test, lint, documentation, compliance and security gates, so an absent check
remains pending and blocks the merge. Resolve the conflict and wait for every required workflow to
report success.

### Stacked delivery

Branch naming above is what keeps CI *attached* to a stack. This is what keeps a stack *correct*.
The full policy is in [`CONTRIBUTING.md`](CONTRIBUTING.md) § "Stacked delivery"; the rules an agent
has to apply while working are:

- **Declare the base.** A PR states whether it targets `develop` or a predecessor branch, and
  which PR that predecessor is. `.github/pull_request_template.md` has the fields.
- **Never merge a successor before its predecessor.** After the predecessor merges, rebase onto
  current `develop` and re-request review of the final diff against `develop` — not against the
  predecessor.
- **Wait for the post-merge `develop` run**, not just the PR check, before merging the next
  dependent PR. The PR check proves the branch builds; only the `develop` run proves the
  integration does.
- **Resolve a shared-registry conflict as a union by default.** The root `CMakeLists.txt`, the
  `FILE_SET CXX_MODULES` lists, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`, the schemas, the
  generated indexes and the committed artifacts under `generated/` are the files where taking one
  side deletes the other side's work while leaving a build that still compiles and tests that
  still pass. If a conflict genuinely has to be resolved by taking one side, say in the PR
  description which side you took and why — an undocumented one-sided resolution is the Wave 2
  failure, a documented one is a decision.
- **Land canonical types and schemas before their consumers.** Import what the predecessor
  defined; do not restate it on a parallel branch.

This is not general good practice written down for its own sake. Wave 2's stacked PRs lost CMake,
module and test wiring during conflict resolution, and allowed two incompatible governance models
to coexist until integration found them — repaired by #104 and reconciled by #105. Each rule above
is one of the things that would have caught that earlier.

### Conventions

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
| [`mdux-clang-tidy`](.agents/skills/mdux-clang-tidy/SKILL.md) | Running clang-tidy, interpreting its findings, or remediating static-analysis warnings. | Compilation-database selection, focused analysis, reporting, and the limits of clang-tidy on C++ module interfaces. |
| [`mdux-cpp23-vulkan-development`](.agents/skills/mdux-cpp23-vulkan-development/SKILL.md) | Changing module interfaces/implementations, Vulkan or Vulkan SC integration, rendering resources, examples, or public APIs. | Module file placement and CMake registration, import/export conventions, the Vulkan/Vulkan SC resource-ownership model, windowing policy, required test/doc updates. |
| [`mdux-regulated-change`](.agents/skills/mdux-regulated-change/SKILL.md) | A change can affect safety behavior, risk controls, compliance metadata, traceability, auditability, lifecycle documents, or claims about medical-device standards. | Impact classification, affected-artifact identification, proportionate documentation updates, traceability, review/escalation triggers, evidence-vs-intent-vs-certification distinctions. |
| [`regulatory-citations`](.agents/skills/regulatory-citations/SKILL.md) | Writing or reviewing anything that claims alignment with IEC 62304, ISO 13485, ISO 14971, IEC 62366-1, or IEC 81001-5-1. | Citation-key format, the `Justification` object, the prohibition on reproducing normative text. **Target convention** — see § 2's parity-programme note. |
| [`evidence-pipeline`](.agents/skills/evidence-pipeline/SKILL.md) | Adding or modifying a baked asset (font, shader, image, `.medui` screen, ML model) or anything under `generated/`. | Recipe→baker→committed-artifact doctrine, canonical-JSON rules, why `generated/` is never hand-edited. **Live** — `mdux-shaderbake` and `mdux-mlbake` both register through `mdux_bake_artifact()`, and `generated/shader/` and `generated/model/` are committed and byte-verified. |
| [`medui-authoring`](.agents/skills/medui-authoring/SKILL.md) | Authoring or discussing a `.medui` screen. | Grammar, component dictionary, theme tokens, text budgets, `@safety_critical`. **Live** — a `.medui` file compiles to a committed artifact, emits `constexpr` C++, and reaches compared pixels; what it cannot yet carry is text (issue `#235`). |
| [`sdf-documents`](.agents/skills/sdf-documents/SKILL.md) | Filling in or reviewing a `software_development_file/` document. | Structure, the summarize-don't-duplicate rule, citing into the corpus. **Live** — `software_development_file/` exists with templates and records (issue `#9`). |

Detailed procedures live in the skill files, not here — this table only routes.

## 8. Definition of done

- Relevant build and tests were executed where the environment permits, with results reported
  honestly (including what was skipped and why).
- Public API documentation (Doxygen comments) and architecture documentation (this file, ADRs) are
  updated when the change affects them.
- Safety, risk, compliance, and traceability impact has been considered and recorded for any
  change touching safety-relevant behavior (see the `mdux-regulated-change` skill).
- No unverified certification or regulatory-compliance claim has been introduced or implied.
