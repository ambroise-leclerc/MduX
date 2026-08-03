---
name: mdux-build-and-test
description: Use when configuring, compiling, testing, or running examples for MduX, or when diagnosing a local build failure (CMake configure errors, module compilation errors, missing Vulkan SDK, toolchain version failures).
---

# MduX build and test

Companion to the "Supported environment and common commands" section of
[`AGENTS.md`](../../../AGENTS.md). This skill covers the mechanics of getting a working build and
gathering evidence; it does not cover module/API design (see `mdux-cpp23-vulkan-development`) or
safety/compliance impact (see `mdux-regulated-change`).

## Prerequisites

Before configuring, check:

1. **Platform**: Windows 10+ or Linux only. There is no macOS build path — a fatal CMake check in
   the root `CMakeLists.txt` rejects other platforms.
2. **Compiler version**: MSVC 17.14+, GCC 16+, or Clang 20+. The root `CMakeLists.txt` fails fast
   with `message(FATAL_ERROR ...)` if the detected compiler is below these floors — read that
   error message; it names the exact required version.
3. **CMake**: 4.0+ (`cmake_minimum_required(VERSION 4.0.0)`).
4. **Vulkan SDK**: 1.3+, discoverable via `find_package(Vulkan REQUIRED)`. Run `vulkaninfo
   --summary` (Linux) or check `%VULKAN_SDK%` (Windows) to confirm the SDK is installed and on the
   search path before configuring — a missing SDK is the most common early configure failure and
   produces a clear "Vulkan SDK not found" error with installation instructions on Windows.

If any prerequisite is unmet and cannot be installed in your environment, **say so explicitly**
(name the missing prerequisite and the version found, if any) instead of attempting a build you
know will fail or guessing at results.

## Workflow

1. **Configure** (out-of-source only — `cmake/PreventInSourceBuilds.cmake` blocks in-source
   builds):
   - Windows: `cmake --preset ninja-msvc` (the only preset defined in `CMakePresets.json`; it
     targets Ninja + MSVC specifically).
   - Linux (no preset exists yet): mirror what `.github/workflows/ci.yml`'s `linux-build` job
     runs:
     ```bash
     cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DMDUX_BUILD_EXAMPLES=ON -DMDUX_BUILD_TESTS=ON -DMDUX_BUILD_DOCS=OFF
     ```
   - Relevant options: `MDUX_BUILD_EXAMPLES`, `MDUX_BUILD_TESTS` (default `ON`),
     `MDUX_BUILD_DOCS`, `MDUX_ENABLE_REGULATORY_DOCS` (default `OFF`/`ON` respectively).

2. **Build** a focused target first, then the full project if needed:
   ```bash
   cmake --build build --target MduX            # library only
   cmake --build build --target MedicalUiExample # one example
   cmake --build build                            # everything enabled by the configure options
   ```
   Known target names (verify against `cmake --build build --target help` if the list may have
   changed since this skill was written): library `MduX`; examples `MedicalUiExample`,
   `VulkanSCTriangleExample`; tests `unit_tests`, `compliance_tests`, `vulkansc_memory_tests`,
   `vulkansc_object_tests`; docs `doxygen-docs` (only exists when `MDUX_BUILD_DOCS=ON`).

3. **Test**, focused before broad:
   ```bash
   ctest --test-dir build -R MduXUnitTests --output-on-failure   # one suite
   ctest --test-dir build --output-on-failure                    # full suite
   ```
   Registered CTest names: `MduXUnitTests`, `MduXComplianceTests`, `VulkanSCMemoryPoolTests`,
   `VulkanSCDeviceObjectTests`.

4. **Run an example** directly once built, e.g. `./build/examples/MedicalUiExample`.

## Compiler-specific module limitations

- **GCC**: the project floor is **GCC 16**, enforced by a `FATAL_ERROR` in the root
  `CMakeLists.txt`. GCC 15 built the library but could not build a SpecLab-based test suite
  (`failed to load pendings for 'std::_Sp_counted_ptr_inplace'`, a defect in GCC's own `std` BMI),
  and it is also the compiler whose `<array>` ICE kept `VulkanSCTriangleExample` excluded. Both
  problems are gone with the floor: the example is now built unconditionally, and there is no
  version guard on it any more.
- **GCC, still current**: `vulkansc_memory_tests` is compiled `-O0` on GCC. That one is *not*
  historical - the ICE in the GIMPLE ealias pass reproduces on GCC 16 as well, and every level
  above `-O0` triggers it. See `tests/CMakeLists.txt` and issue #48.
- **Clang 20**: the Clang CI job in `.github/workflows/ci.yml` is present but commented out. Clang
  builds are not currently verified by CI even though the version floor is enforced in
  `CMakeLists.txt` — treat any Clang build result as unverified against the project's own CI and
  say so.
- **MSVC**: requires `/experimental:module` and `/std:c++latest`, already wired into
  `CMakeLists.txt`; the Visual Studio *generator* is explicitly rejected in favor of Ninja for
  `import std;` support (see the `CMAKE_GENERATOR MATCHES "Visual Studio"` fatal-error check).

If your toolchain does not meet these floors, do not attempt to lower them or bypass the checks —
report the mismatch (found version vs. required version) instead.

## Evidence to include in a handoff

Report, precisely:
- The exact configure command used (with all non-default options).
- The build target(s) invoked and whether each succeeded or failed (with the compiler error if it
  failed).
- Which tests were run (by CTest name) and their pass/fail result.
- Any check or test you skipped, and the specific reason (missing SDK, unsupported compiler
  version, disabled CI job, etc.) — never silently omit a skipped check.

## Non-destructive handling of build artifacts

- Never commit files under `build/` or other generated output (see `.gitignore`).
- Before deleting or re-creating a build directory, check whether it contains state you didn't
  generate this session (`git status` won't show it since it's gitignored — use `ls`/timestamps if
  unsure) and prefer `cmake --build build --target clean` or a fresh `-B` directory over `rm -rf`
  on a directory you didn't create.
