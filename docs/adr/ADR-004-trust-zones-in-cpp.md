# ADR-004: Trust zones in C++

## Status
Accepted (2026-07-26)

## Context
MduX has a Rust sibling, [TrustSC](https://github.com/ambroise-leclerc/TrustSC), targeting the same
problem — a medical-device UI SDK with IEC 62304 Class B/C compliance modelling built in. TrustSC's
workspace is split into three trust zones enforced at the crate boundary:

- **Governed** (`crates/`) — pure Rust, `#![forbid(unsafe_code)]`, may depend only on each other or
  on version-pinned, reviewed crates recorded in a SOUP register. No native SDK handles in public
  APIs.
- **Edge adapters** (`adapters/`) — may use `unsafe` and native bindings (`ash`, `winit`), but every
  public function takes or returns owned Rust data already defined by a governed crate.
- **Host-only tooling** (`tools/`) — build-time utilities compiling source assets into evidence
  artifacts. Never shipped in a runtime artifact.

MduX has no equivalent today. The single `MduX` CMake target links Vulkan and (in examples) GLFW
directly, and there is no mechanism preventing safety-relevant code from acquiring a native handle,
a filesystem dependency, or a source of non-determinism.

This ADR is part of the MduX ↔ TrustSC parity programme (GitHub issue #7-#19); it is the
foundational architectural decision that issues #12 (evidence pipeline), #13 (renderer), #15
(`.medui` compiler), and #18 (ML inference) all build on top of.

## Medical Device Considerations

### IEC 62304 Implications (Software Lifecycle)
- **Segregation for risk control** (IEC 62304:2006 §5.3.3): a governed module that cannot reach
  Vulkan, GLFW, or the filesystem is segregated from those risk sources at the compiler level, not
  merely by convention or code review.
- **SOUP identification** (§5.3.4, §8): confining native/third-party dependencies to the adapter and
  tools zones keeps the SOUP register (tracked separately in issue #9) short and lets every entry
  name the specific zone that reaches it.

### Honesty about what this ADR does *not* provide
Rust's `#![forbid(unsafe_code)]` is a compiler-enforced, audited property: a crate carrying it
**cannot** contain the `unsafe` keyword, full stop. C++ has no equivalent keyword and no borrow
checker. Any claim that this ADR's mechanisms are equivalent to that guarantee would be false and
must not appear in any regulatory document (`software_development_file/`, an SDF-derived claim, or
elsewhere). See "Consequences" below for the precise, narrower claim this ADR actually supports.

## Decision

Introduce three target families, following the existing `include/mdux/<area>/X.cppm` +
`src/<area>/X.cpp` convention and dotted lowercase module names (`mdux.vulkansc.memory` is the
existing precedent):

| Zone | Target | Links | Public API may contain |
|---|---|---|---|
| Governed | `MduXCore` (alias `MduX::Core`) | `std` only — **no `Vulkan::Vulkan`** | `std::span`, `std::string_view`, PODs. No `Vk*` handle. |
| Adapter | `MduX` (existing, alias `MduX::MduX`) | `MduXCore` + `Vulkan::Vulkan` + `Threads` | `Vk*` handles, owned governed types |
| Host tools | `mdux-*` executables under `tools/` | `MduXCore` + host-only object libraries | n/a — never installed or shipped |

`MduX` `PUBLIC`-links `MduXCore`, so existing consumers of `MduX::MduX` are unaffected.

**Mechanically enforced, in order of strength:**

1. **Dependency and include-interface isolation.** `MduXCore`'s link interface never receives
   Vulkan's or GLFW's libraries, compile definitions, or include directories. This prevents
   accidental use of SDK-private headers that are reachable only through those targets. It does
   **not** make system-installed headers unreachable: a compiler may still find Vulkan, platform,
   or OS headers in its default search paths.
2. **A mandatory CI banned-include lint** over governed directories rejects direct inclusion of
   Vulkan, GLFW, platform, and OS headers. The same lint bans `reinterpret_cast`, `const_cast`,
   `new `, `malloc`, `<random>`, `std::chrono::*_clock::now`, `std::fma`, `getenv`,
   `std::filesystem`, and decimal float format specifiers (`%f`, `%g`, `%e`) in evidence-writing
   code. This check, rather than include-directory isolation alone, enforces the source-level header
   boundary on machines where those SDKs are installed.
3. **Configure-time link-graph assertion.** `cmake/MduXTrustZones.cmake` provides
   `mdux_declare_governed(<target>)`, recording the target in a global property, and
   `mdux_verify_trust_zones()`, called once at the end of the top-level `CMakeLists.txt`, which
   transitively walks every governed target's `LINK_LIBRARIES` / `INTERFACE_LINK_LIBRARIES` and
   raises `message(FATAL_ERROR)` on `Vulkan::*`, `glfw`, or any target that is not itself governed.
   This runs on every configure, before anything compiles.
4. **Per-directory `.clang-tidy`.** `include/mdux/core/.clang-tidy` inherits the root configuration
   and adds, with `WarningsAsErrors: '*'`: `cppcoreguidelines-pro-type-reinterpret-cast`,
   `-pro-type-const-cast`, `-pro-type-cstyle-cast`, `-pro-bounds-pointer-arithmetic`,
   `-pro-type-union-access`, `-pro-type-vararg`, `-no-malloc`, `-owning-memory`, `-avoid-goto`,
   `cert-dcl58-cpp`, `misc-new-delete-overloads`, `bugprone-unhandled-self-assignment`,
   `modernize-avoid-c-arrays`.
5. **Float-determinism flag guards** on modules with determinism requirements (see the ML inference
   ADR, issue #18): `-ffp-contract=off -fno-fast-math` / `/fp:precise /fp:contract`, plus a CMake
   check that `FATAL_ERROR`s if `-ffast-math`, `-Ofast`, or `/fp:fast` reaches any governed target.

**Convention only — not mechanically enforced, and this ADR does not claim otherwise:**

- Nothing stops a governed module from writing undefined behavior inside its own translation unit.
  There is no `unsafe` keyword and no borrow checker to forbid.
- `import std;` gives governed code `std::vector`, threads, and file/network I/O. Only the grep
  lint (item 2) and code review keep those out of a module that should not need them.
- clang-tidy's analysis of C++23 module interface units is immature as of this writing. Run it in
  the Clang CI leg only (once issue #48 re-enables that leg); treat its results as advisory on
  Windows/MSVC.

## Alternatives Considered

### 1. No trust-zone split; rely on code review alone (Rejected)
**Pros:** No build-system work; no new targets to maintain.
**Cons:** Nothing prevents a future change from quietly linking Vulkan into a module that should be
governed. Review catches this only if the reviewer happens to notice — exactly the failure mode
this ADR exists to close mechanically.

### 2. Namespace-only separation (e.g. `mdux::core::` vs `mdux::render::`) (Rejected)
**Pros:** No CMake target changes.
**Cons:** A namespace is not a link boundary. Nothing stops `mdux::core::Foo` from `#include`-ing
Vulkan; the separation is purely cosmetic and provides none of the mechanical guarantees above.

### 3. A single governed target with a compile-time feature flag disabling Vulkan (Rejected)
**Pros:** Avoids adding a second CMake target.
**Cons:** The Vulkan-dependent code would still physically live in the same target and the same
translation units as governed code, so the guarantee would depend on the flag always being set
correctly rather than on separate target usage requirements plus mandatory source checks.

## Decision (continued): what "governed" will contain

No code moves out of `mdux.cppm` as part of this ADR — it stays an adapter module for now. Issue
#11 (S7) creates `MduXCore` with initial scaffolding (`mdux.core.units`, `mdux.core.result`) and
wires up the verification mechanism; later epics (`#12`, `#13`, `#15`, `#18`) add governed schema,
evidence, draw-list, and ML modules to it as they land. `mdux.vulkansc.memory` and
`mdux.vulkansc.objects` remain in the adapter zone — they take `VkDevice` in their public APIs by
design and are not candidates for `MduXCore`.

## Consequences

### Positive
- Target usage requirements cannot accidentally propagate native SDK dependencies into governed
  code, while the mandatory banned-include lint closes the system-header search-path gap.
- Every later epic that introduces baked evidence, `.medui` compilation, or ML inference gets a
  pre-existing home (`MduXCore`) and a pre-existing verification mechanism, rather than having to
  invent zone enforcement piecemeal.
- The SOUP register (issue #9) becomes shorter and more precise: an entry can name the exact zone
  that reaches a dependency.

### Negative
- Introduces a second library target and a second `.clang-tidy` file to maintain.
- The link-graph assertion adds a small amount of configure-time CMake logic that must itself be
  kept correct — a bug in `mdux_verify_trust_zones()` could give a false sense of enforcement.
- clang-tidy's incomplete module support means item 4's enforcement is currently partial and
  platform-dependent (see "Decision" above).

### Risks and Mitigations
- **The claim gets overstated in a compliance document.** *Mitigation*: the exact wording below is
  the only wording to use; `sdf-documents` and `mdux-regulated-change` both carry this same
  constraint so it is checked from two directions.
- **A future PR adds an exception "just this once."** *Mitigation*: the link-graph assertion fails
  the build, not a review comment — there is no quiet way to add the exception without either
  fixing the design or deliberately weakening `mdux_verify_trust_zones()`, which is itself
  reviewable.

## The claim, stated exactly

Use this wording, and no broader wording, wherever this architecture is described in a compliance
context:

> Governed targets have no declared platform or graphics dependencies; their sources are checked
> to reject direct inclusion of platform, graphics, and OS headers, are checked by an enforced
> static-analysis profile, and are covered by determinism tests. This is **not** a claim that
> governed modules cannot contain undefined behaviour or that compiler system headers are
> physically inaccessible.

## References
- [TrustSC ADR-005: Pure-Rust project boundary and dependency policy](https://github.com/ambroise-leclerc/TrustSC/blob/main/docs/adr/ADR-005-pure-rust-project-boundary-and-dependency-policy.md)
- [TrustSC ADR-012: Presentation adapter crates and shader artifacts](https://github.com/ambroise-leclerc/TrustSC/blob/main/docs/adr/ADR-012-presentation-adapter-crates-and-shader-artifacts.md)
- IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control
- IEC 62304:2006 §5.3.4 Identify SOUP items

## Approval
- **Decision Date**: 2026-07-26
- **Approved By**: Project maintainer
- **Review Date**: at the next parity-programme epic boundary (issue #12 landing)
