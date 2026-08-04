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
| **Ninja** | any recent | — |
| **Vulkan SDK** | 1.3+ | `find_package(Vulkan REQUIRED)` |

A version below the floor fails at configure with a message naming the version it found.

**Ninja is not optional.** The Visual Studio generator does not support `import std`; configuring
with it fails deliberately, with instructions.

**GCC 16.0.x snapshots.** Ubuntu ships a pre-release GCC 16 snapshot that ICEs
(`internal compiler error: in nonnull_arg_p`) building the Vulkan adapter. The released 16.1.0
builds the tree cleanly. If you hit that ICE, check `g++ --version` for `experimental`.

## Build and test

Through the presets, which is what CI runs:

```bash
# Linux
cmake --preset ninja-gcc
cmake --build --preset ninja-gcc
ctest --preset ninja-gcc --output-on-failure

# Windows
cmake --preset ninja-msvc
cmake --build --preset ninja-msvc
ctest --preset ninja-msvc
```

Available presets: `ninja-gcc`, `ninja-gcc-debug`, `ninja-msvc`, `ninja-msvc-debug`, `ninja-clang`.
The GCC presets use whatever `g++` is on `PATH`; if your default is older than 16, point at the
right one explicitly:

```bash
cmake -B build -S . -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_BUILD_TYPE=Release \
  -DMDUX_BUILD_EXAMPLES=ON -DMDUX_BUILD_TESTS=ON -DMDUX_BUILD_DOCS=OFF
```

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

## Examples

Built when `MDUX_BUILD_EXAMPLES=ON`.

| Example | Demonstrates | Needs a GPU? |
|---|---|---|
| `MedicalUiExample` | building a frame with `mdux.draw` — no window, no device, no GLFW | no |
| `EcgClassifierExample` | embedded ML: fail-closed startup, golden self-test, classification | no |
| `VulkanSCTriangleExample` | a real Vulkan device rendering from the baked shader package | yes (or lavapipe) |

`EcgClassifierExample` opens no files at all — both its weights and its model package are linked in
as byte arrays. Running it prints the golden-vector count it verified before classifying:

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
# The UUID changes between CMake releases. This one is CMake 4.0-4.1; if yours
# rejects it, take the value from MduX's own top-level CMakeLists.txt.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
find_package(MduX REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE MduX::MduX)

if(TARGET __CMAKE::CXX23)
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

Host tools (`mdux-shaderbake`, `mdux-mlbake`, `mdux-shaderemit`) are **not** exported. They are
build-time only.

## Limitations

Worth knowing before you build on this.

**Platforms.** Windows 10+ and Linux are the intended and tested scope. macOS is not supported:
AppleClang is not a recognised compiler branch, and C++23 module scanning does not work under it, so
a macOS configure fails for unrelated-looking reasons rather than being rejected cleanly.

**Clang is unverified.** The floor is enforced and `ninja-clang` exists, but the Clang CI job is
commented out ([#48](https://github.com/ambroise-leclerc/MduX/issues/48)). Treat Clang as
best-effort. Cross-toolchain evidence claims cover MSVC and GCC only.

**Rendering is a vertical slice, not a UI toolkit.** `mdux.draw` records solid and textured rects
into a fixed budget, and `mdux.render.vulkan` draws them. There is **no text, no layout, and no
widgets** — those are [#14](https://github.com/ambroise-leclerc/MduX/issues/14),
[#15](https://github.com/ambroise-leclerc/MduX/issues/15) and
[#17](https://github.com/ambroise-leclerc/MduX/issues/17).

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
