# Getting started

Building MduX, running its tests, and consuming it from your own project.

Every command here is one CI runs or one this repository's own tests exercise. Where something is
unsupported or unfinished, it says so rather than being omitted — see [Limitations](#limitations),
which is worth reading before you invest much time.

**MduX is an experimental proof-of-concept.** Nothing here establishes certification, validation,
production readiness, or regulatory compliance. See
[`regulatory-compliance.md`](regulatory-compliance.md).

## Prerequisites

C++23 named modules and `import std` are the binding constraints, and they are why the floors are
this high.

| | Minimum | Enforced by |
|---|---|---|
| **MSVC** | 17.14 (VS 2022 17.10+) | fatal check in `CMakeLists.txt` |
| **GCC** | 16 | fatal check in `CMakeLists.txt` |
| **Clang** | 20 | fatal check in `CMakeLists.txt` |
| **CMake** | 4.0+ | `cmake_minimum_required` |
| **Ninja** | any recent | fatal check in `CMakeLists.txt` |
| **Vulkan SDK** | 1.3+ | `find_package(Vulkan REQUIRED)` |

On macOS the supported tuple is exact rather than a minimum: Apple Silicon, upstream LLVM/Clang
21.1.8, libc++, CMake 4.3.1, Ninja, and LunarG Vulkan SDK 1.4.309.0 with MoltenVK. AppleClang,
Homebrew GCC, Intel Macs, and other macOS toolchain versions are intentionally unsupported.

A version below the floor fails at configure with a message naming the version it found.

**Ninja is not optional.** CMake implements C++ modules for the Ninja, Ninja Multi-Config and
Visual Studio 17.4+ generators only, and Visual Studio cannot do `import std` — so the Ninja
family is the whole supported set. Configuring with anything else, including the platform default
(`Unix Makefiles` on Linux), stops at a configure-time check that names the generator it found
and the flag to pass. `export CMAKE_GENERATOR=Ninja` makes it the default for your shell.

**GCC 16.0.x snapshots.** Ubuntu ships a pre-release GCC 16 snapshot that ICEs
(`internal compiler error: in nonnull_arg_p`) building the Vulkan adapter. The released 16.1.0
builds the tree cleanly. If you hit that ICE, check `g++ --version` for `experimental`.

## Build and test

Ordinary out-of-source CMake, on Windows or Linux:

```bash
mkdir build && cd build
cmake .. -G Ninja
cmake --build .
ctest --output-on-failure
```

That is the whole thing. `MDUX_BUILD_EXAMPLES` and `MDUX_BUILD_TESTS` default to `ON`, so the
default configure already builds everything the test suite needs.

On Apple Silicon macOS, install CMake 4.3.1, Ninja, GLFW, upstream LLVM 21.1.8 and LunarG's Vulkan
SDK, source the SDK's `setup-env.sh`, then use the guarded preset:

```bash
export MDUX_LLVM_ROOT=/path/to/llvm-21.1.8
cmake --preset ninja-macos-clang
cmake --build --preset ninja-macos-clang
ctest --preset ninja-macos-clang --output-on-failure
```

The toolchain file discovers Homebrew's `llvm` installation when `MDUX_LLVM_ROOT` is unset, but
still rejects any version other than 21.1.8. MoltenVK is a Vulkan portability implementation over
Metal: application instances must enable portability enumeration and devices must enable
`VK_KHR_portability_subset` when exposed. MduX's headless harness and triangle example do this;
host applications remain responsible for the same setup.

If your default compiler is not the one you want — Ubuntu's `g++` may be an older release, or a
GCC 16 pre-release snapshot — select it with the standard variables:

```bash
CC=gcc-16 CXX=g++-16 cmake .. -G Ninja
```

### Presets

`CMakePresets.json` defines `ninja-gcc`, `ninja-gcc-debug`, `ninja-msvc`, `ninja-msvc-debug` and
`ninja-clang`, `ninja-macos-clang` and `ninja-macos-clang-debug`. They exist for CI, not for you: each workflow invokes a named configuration this
repository owns, so a reviewer can read what CI built instead of trusting that a hand-written
command line in a YAML file still matches the one in this document. Building by hand needs none
of them.

If you want one anyway — say, to reproduce a CI leg exactly:

```bash
cmake --preset ninja-gcc          # configures into build-gcc/
cmake --build --preset ninja-gcc
ctest --preset ninja-gcc --output-on-failure
```

Note that each preset uses its own binary directory (`build-gcc`, `build-clang`, …), so a preset
build and a plain `build/` one do not collide.

### Options

| Option | Default |
|---|---|
| `MDUX_BUILD_EXAMPLES` | `ON` |
| `MDUX_BUILD_TESTS` | `ON` |
| `MDUX_BUILD_DOCS` | `OFF` |
| `MDUX_ENABLE_REGULATORY_DOCS` | `ON` |

### Selecting suites

Tests are registered one CTest entry per case, with labels:

```bash
ctest --test-dir build -L evidence      # committed artifacts still bake byte-identically
ctest --test-dir build -L determinism   # ML kernels produce the frozen bit patterns
ctest --test-dir build -L noheap        # predict() allocates nothing
ctest --test-dir build -L pixel         # rendered output (needs a Vulkan ICD; lavapipe is fine)
ctest --test-dir build -L regulatory    # corpus indexes and schemas are current
```

`ctest -R <name>` selects an individual scenario by name.

### Verifying a compiled screen

`mdux-verify-ui` is a host-only build tool. It reads the committed bundle and every artifact that
bundle identifies, renders once for every approved locale (or once in the explicit locale-free
scope for a textless screen), then runs the complete obligation set:

```bash
./build/tools/mdux-verify-ui \
  --screen=generated/screen/endoscope-monitor \
  --locales=all
```

Locale subsets are deliberately rejected. Exit status `0` means every obligation held, `1` means
verification failed (a check failed or the plan contained zero obligations), `2` is command-line
misuse, and `3` means the run could not be made (for example a missing Vulkan implementation or
inconsistent artifact).

Add `--diff-image-dir=<dir>` to get a picture of a failure. Each render scope that fails writes
`<screen>.<scope>.png` there: the frame it rendered, dimmed, with every failed obligation's expected
rectangle outlined in magenta and whatever was actually found outlined in cyan. The scope is
percent-encoded, so an ordinary locale gives `endoscope-monitor.en-US.png` while a textless screen's
locale-free scope gives `<screen>.%28locale-free%29.png` — encoded rather than filtered so that two
scopes of one screen can never overwrite each other's image. It is written only on
a failure, it never goes into `generated/`, and nothing reads it back — it is for you, not for a
check. CI passes the same flag and uploads the directory when the step fails.

You do not have to run it by hand to get it run. `mdux_compile_screen()` registers the same
invocation as the ctest `verify.screen.<id>`, so:

```bash
ctest --preset <preset> -L verify -V --no-tests=error
```

verifies every committed screen. `--no-tests=error` matters: without it, a label that matched no
screen would pass the step over nothing.

## Examples

Built when `MDUX_BUILD_EXAMPLES=ON`.

| Example | Demonstrates | Needs a GPU? |
|---|---|---|
| `MedicalUiExample` | building a frame with `mdux.draw` — no window, no device, no GLFW | no |
| `EcgClassifierExample` | embedded ML: fail-closed startup, golden self-test, classification | no |
| `VulkanSCTriangleExample` | a real Vulkan device rendering from the baked shader package | yes (or lavapipe) |

`EcgClassifierExample` opens no files at all: its model package is generated `constexpr` data and
its weights are a separately linked byte array. It links no host-tools parser. Running it prints the
golden-vector count it verified before classifying:

```
$ ./build/examples/EcgClassifierExample
MduX ECG classifier demonstrator
  NOTE: synthetic weights, no training, no clinical validity. See ADR-008.

package        : ecg-demo
layers         : 5
goldens re-run : 4 (all reproduced bit for bit)
```

Its weights are synthetic and carry no clinical meaning whatsoever — see
`recipes/model/ecg-demo.toml`.

## Consuming MduX

Install, then `find_package`:

```bash
cmake --install build --prefix /your/prefix
```

```cmake
cmake_minimum_required(VERSION 4.0.0)

# Required, and it must come BEFORE project(): `import std` is still an
# experimental CMake feature gated behind a UUID that is checked when the CXX
# language is enabled. Omit it and configure fails at generate time with
# "requires that the __CMAKE::CXX23 target exist, but it was not provided by
# the toolchain" - which does not obviously point back at this line.
#
# The UUID changed in CMake 4.3. MduX rejects 4.4+ until its next value and
# behavior have been reviewed.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
find_package(MduX REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE MduX::MduX)

if(23 IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
    set_target_properties(myapp PROPERTIES CXX_MODULE_STD ON)
endif()
```

```cpp
import std;
import mdux;

int main() {
    std::println("MduX {}", mdux::Version::getString());

    if (!mdux::initialize()) {
        return 1;
    }
    // ... your application ...
    mdux::shutdown();
    return 0;
}
```

Configure with `-DCMAKE_PREFIX_PATH=/your/prefix`.

This is the same shape as the `InstallTreeConsumer` test, which installs to a scratch prefix, builds
an external project against it and runs the result — so a passing test means these instructions
work, rather than that the files merely landed on disk.

### Which target to link

| Target | Gets you | Links Vulkan? |
|---|---|---|
| `MduX::Core` | governed modules only — evidence, governance, draw, shader/ML schema, ML runtime | no |
| `MduX::MduX` | everything, including the Vulkan renderer | yes |

Link `MduX::Core` if you want the governed pieces without a Vulkan dependency. That is not a
convenience — `mdux_verify_trust_zones()` mechanically enforces that `MduXCore`'s link graph never
reaches Vulkan.

Host tools (`mdux-shaderbake`, `mdux-mlbake`, `mdux-textbake`, `mdux-meduic`,
`mdux-medui-check`, the emitters and `mdux-verify-ui`) are **not** exported. They are build-time
only.

## Limitations

Worth knowing before you build on this.

**Platforms.** Windows 10+, Linux, and Apple Silicon macOS are tested. macOS support means only the
exact upstream-Clang/libc++/MoltenVK tuple above; AppleClang, GCC, Intel hardware and unpinned SDK
combinations fail at configure time. MoltenVK translates Vulkan to Metal and does not imply native
Vulkan feature or performance parity.

**Linux Clang is unverified.** The general floor is enforced and `ninja-clang` exists, but its CI
job is manual-only ([#48](https://github.com/ambroise-leclerc/MduX/issues/48)). The automatic macOS
Clang job covers only the exact Apple Silicon tuple above. Cross-toolchain evidence claims remain
anchored by MSVC and GCC; the macOS run is an additional check, not a certification claim.

**Rendering is a vertical slice, not a UI toolkit.** `mdux.draw` records solid and textured rects
into a fixed budget, `mdux.render.vulkan` draws them, and the `.medui` path supplies bounded layout
plus baked `Label` text. The remaining component appearances and live-data geometry are still
[#17](https://github.com/ambroise-leclerc/MduX/issues/17); they are not a general widget toolkit.

**The HTML/CSS path is gone.** If you find documentation elsewhere describing `MedicalUiRenderer`,
`UiFileWatcher` or loading `.html` files, it is stale — that path was deleted by
[#127](https://github.com/ambroise-leclerc/MduX/issues/127) because it rendered nothing.

**ML is `f32` and eight operations.** Dense, Conv1D, MaxPool1D, AvgPool1D, Flatten, and
relu/sigmoid/softmax. `int8`, recurrent layers and attention are out of scope by decision, not by
omission ([ADR-008](adr/ADR-008-zero-soup-ml-inference.md)).

**Compliance metadata is a record you fill in.** `mdux::ComplianceMetadata` stores what *you* assert
about *your* device. It checks nothing and confers nothing.

## Where next

- [`architecture.md`](architecture.md) — what is in the tree, and what is planned
- [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — style, formatting and PR conventions
- [`adr/`](adr/) — the decision trail, starting with trust zones (ADR-004)
- [`regulatory-compliance.md`](regulatory-compliance.md) — the scope limits this project claims
