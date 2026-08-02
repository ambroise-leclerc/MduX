---
name: mdux-cpp23-vulkan-development
description: Use when changing MduX C++23 module interfaces or implementations, Vulkan or Vulkan SC integration code, rendering resources, example programs, or public API surface.
---

# MduX C++23 / Vulkan development

Companion to the "Verified architecture summary" section of
[`AGENTS.md`](../../../AGENTS.md). This skill covers module structure and Vulkan resource
conventions; for configuring/building/testing see `mdux-build-and-test`; for safety/compliance
impact of a change see `mdux-regulated-change`.

## Prerequisites

You can build and test locally, or you know from `mdux-build-and-test` why you currently cannot.
Read the target module interface(s) fully before editing — the existing `.cppm` files are the
ground truth for current conventions, not this document.

## Module file placement and naming

- Interfaces live under `include/mdux/**.cppm`; each has a matching implementation under
  `src/**.cpp` with the same relative path/stem (e.g.
  `include/mdux/vulkansc/MemoryPoolManager.cppm` ↔ `src/vulkansc/MemoryPoolManager.cpp`).
- When you add, remove, or move a module unit, update the `target_sources(MduX ... FILE_SET
  CXX_MODULES FILES ...)` block (interfaces) and the plain source list (implementations) in the
  root `CMakeLists.txt`. A `.cppm`/`.cpp` file that exists on disk but isn't registered there will
  not be compiled — this is a common silent-failure mode.
- Module names use dotted namespacing that mirrors the directory: `mdux`,
  `mdux.vulkansc.memory`, `mdux.vulkansc.objects`. Follow this pattern for new submodules rather
  than inventing a different naming scheme.

## Import/export conventions used in this repository

- Each `.cppm` starts with a `module;` global module fragment containing only the C headers it
  needs (e.g. `#include <vulkan/vulkan.h>`), followed by `export module <name>;`.
- Standard library facilities are brought in with `import std;` — do not add `#include
  <vector>`-style standard headers inside the module purview; match the existing files.
- Public API surface is wrapped in `export namespace mdux { ... }` (or `mdux::vulkansc`). Keep
  non-exported helpers out of the exported namespace block.
- A module that depends on another project module imports it explicitly (see
  `DeviceObjectManager.cppm`: `import mdux.vulkansc.memory;`) rather than relying on transitive
  visibility.
- These conventions exist to work around current C++23 modules toolchain limitations (see
  `docs/adr/ADR-003-compiler-modernization.md`, status "Accepted") — don't restructure them
  without checking whether the change still compiles on all three supported compilers (see
  `mdux-build-and-test` for the per-compiler caveats, notably the GCC 15 ICE and unverified Clang
  CI).

## Vulkan / Vulkan SC resource-ownership model

- `mdux::render::VulkanRenderContext` (`include/mdux/render/VulkanRenderer.cppm`) is populated by
  the **caller**, and its members are exactly: `VkDevice`, `VkPhysicalDevice`, `VkRenderPass`,
  `VkQueue`, `queueFamilyIndex`, a non-zero `viewport` (`mdux::core::Extent2D`) and `subpass`.
  There is **no** `VkCommandBuffer` in it — a command buffer is passed to `UiRenderer::record()`
  per frame, not held in the context, and the queue is there because `create()` uploads a default
  atlas once. Every member is initialised, and `isValid()` rejects any that is null or empty.
  MduX code must never assume it can create these — only consume them.
- `mdux::render::UiRenderer` owns resources it creates itself from that caller-supplied context
  (shader modules, descriptor set layout, descriptor pool and set, pipeline layout, pipeline,
  mapped vertex and index buffers, and a 1x1 default atlas with its view and sampler) and is
  responsible for their cleanup — follow this same caller-owned-input / library-owned-output split
  for any new Vulkan-integrating type.
- `mdux.vulkansc.memory` / `mdux.vulkansc.objects` (`include/mdux/vulkansc/*.cppm`) implement the
  Vulkan SC constraint that certain objects (device memory, command pools, descriptor pools, query
  pools, swapchains) cannot be freed before device destruction. If you add Vulkan SC-facing code
  that allocates one of these object types, route it through the existing pool/manager pattern
  rather than calling the Vulkan free/destroy functions directly.
- Error handling in this layer should be explicit and observable (return codes / validation state
  reported back to the caller), not silent — see `RenderStatistics`, `getValidationErrors()`, and
  `validateCompliance()` in `mdux.cppm` for the existing pattern.

## Windowing-dependency policy

- The `MduX` library target has **no** windowing dependency — `mdux.cppm`'s global module fragment
  includes only `<vulkan/vulkan.h>` and `<stdint.h>`. Do not add a windowing library dependency to
  the `MduX` target.
- GLFW is used only inside `examples/` (`examples/CMakeLists.txt` links GLFW into
  `MedicalUiExample` and `VulkanSCTriangleExample`) to demonstrate one way a host application can
  create a window/surface. Treat GLFW usage in examples as illustrative, not as a library
  requirement — a new example is free to use a different windowing approach as long as it doesn't
  pull that dependency into the `MduX` target itself.
- `examples/BasicExample.cpp` exists on disk but is not currently wired into
  `examples/CMakeLists.txt` — do not assume it builds; verify before referencing or extending it.

## Required test/doc updates

- Add or extend tests under `tests/` following the existing plain-`TestRunner` pattern (see
  `tests/TestMain.cpp`) and register new executables/tests in `tests/CMakeLists.txt` (both the
  `add_executable` and the corresponding `add_test`). There is currently no third-party test
  framework wired in — `docs/adr/ADR-002-testing-framework-selection.md` proposing Catch2/BDD is
  still status "Proposed", not adopted; don't assume Catch2 headers are available.
- Document new public API with Doxygen comments following `CONTRIBUTING.md`'s `@brief`-first
  style.
- If the change affects an existing ADR's decision or introduces a new architectural decision,
  update `docs/adr/` (see `docs/adr/README.md` for the format and index update process).
