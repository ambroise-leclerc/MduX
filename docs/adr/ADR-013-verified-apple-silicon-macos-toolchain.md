# ADR-013: Verified Apple Silicon macOS toolchain

## Status

Accepted (2026-08-23)

Supersedes [ADR-001](ADR-001-multiplatform-graphics-framework.md)'s Windows/Linux-only platform
decision and [ADR-003](ADR-003-compiler-modernization.md)'s compiler/toolchain matrix.

## Context

MduX previously excluded macOS because AppleClang could not provide the named-module and
`import std` behavior the project requires, while Vulkan on Apple platforms needs a portability
implementation over Metal. Treating every compiler and driver combination as equivalent would make
build and rendered-output results difficult to reproduce.

The project now has an Apple Silicon host on which upstream Clang/libc++ builds the complete module
graph. MoltenVK exposes the Vulkan portability extensions needed by the existing offscreen path and
by the GLFW presentation example. This is useful additional platform evidence, but it does not make
this experimental project certified, validated, production-ready, or equivalent to a native Vulkan
driver.

## Decision

Support one macOS configuration:

- Apple Silicon (`arm64`);
- upstream LLVM/Clang 21.1.8 and its libc++ standard-library modules;
- CMake 4.3.1 and Ninja;
- LLVM's `llvm-ar` and `llvm-ranlib`;
- LunarG Vulkan SDK 1.4.309.0 with MoltenVK in automatic CI.

`cmake/toolchains/macos-arm64-llvm.cmake` and `ninja-macos-clang` encode that tuple. Configure fails
closed for AppleClang, GCC on macOS, Intel Macs, a different Clang/CMake version, missing libc++
module metadata, or non-LLVM archive tools. CMake's rotating experimental `import std` UUID is
selected by reviewed CMake release range, and an unreviewed future range is rejected.

The Vulkan headless harness and presentation example enumerate available extensions. They set
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` and enable
`VK_KHR_portability_enumeration` only when the loader exposes it, and enable
`VK_KHR_portability_subset` only when the selected physical device exposes it. Native Vulkan paths
therefore retain their existing extension set.

The automatic macOS workflow must:

- configure, build, and run the full suite on an explicit arm64 image;
- fail if MoltenVK cannot expose a real Metal-backed physical device;
- fail if the pixel-labelled suite is skipped;
- build, run, and present three frames from `VulkanSCTriangleExample --smoke-test`;
- exercise evidence, determinism, no-heap, regulatory, install-consumer, ASan, and UBSan checks;
- reject source-tree writes from ordinary build/test execution.

## Consequences

- macOS regressions become pull-request failures rather than best-effort local observations.
- The supported tuple is intentionally narrow. Updating any pinned tool is a reviewed change that
  reruns the complete macOS evidence set.
- MoltenVK adds a separate adapter and driver lifecycle to review. A passing macOS run is evidence
  for the tested tuple only; it is not evidence for AppleClang, Intel Macs, other SDK versions, or
  native-Vulkan feature/performance parity.
- libc++ emits some library-owned throw references directly in governed objects. The object-symbol
  scan cannot distinguish those from source throws, so it is informational on this leg, as it is on
  MSVC. The toolchain-independent governed-source lint remains the gate. The no-heap runtime and
  object scans continue to gate.
- SpecLab v0.1.0 is still pinned to the recorded commit. A repository-owned, reviewed patch only
  updates its CMake 4.3 import-std gate; the dependency source identity and runtime behavior do not
  float.

## Alternatives considered

### AppleClang

Rejected because it does not provide the required `import std` module surface for this build.

### GCC on macOS

Rejected because the project has no verified GCC/libstdc++ module and Vulkan tuple on macOS.

### Intel macOS

Rejected because there is no automatic hardware runner or maintained toolchain tuple for it.

### Floating latest toolchains

Rejected because CMake's experimental import-std gate, compiler module metadata, and driver behavior
can change independently. A floating green result cannot identify which input changed when it turns
red.

## Verification and review

The workflow provides repeatable engineering evidence for the declared configuration. A maintainer
and a rendering-domain reviewer must review changes to the pins, portability setup, pixel checks, or
support claims. Real-device validation remains a merge gate; a local run in an environment that
cannot expose Metal may report the render suite as skipped but cannot satisfy that gate.

## Related artifacts

- Issue #222
- `cmake/toolchains/macos-arm64-llvm.cmake`
- `CMakePresets.json`
- `.github/workflows/macos-arm64-build.yml`
- `tests/render/HeadlessDevice.hpp`
- `examples/VulkanSCTriangleExample.cpp`
