# ADR-005: Error handling and exceptions policy

## Status
Accepted (2026-07-26)

## Context
MduX has no stated error-handling policy, and current code is inconsistent with the direction the
MduX ↔ TrustSC parity programme requires:

- `src/mdux.cpp:221` throws `std::runtime_error` from `MedicalUiRenderer`'s constructor.
- `include/mdux/vulkansc/MemoryPoolManager.cppm:100` documents throwing behavior.
- `.clang-tidy:9` **disables** `bugprone-exception-escape` — the check that would otherwise flag an
  exception escaping a `noexcept` boundary.

Separately, ADR-004 introduces a governed zone (`MduXCore`) whose whole purpose is to be usable in
contexts a Vulkan-linked adapter target is not — including Class C / Vulkan SC deployments that
routinely build with `-fno-exceptions`. A governed library that cannot be compiled that way is not
deployable in exactly the deployment target this project names in its own compliance material,
regardless of how clean its exception hierarchy is.

## Medical Device Considerations

### IEC 62304 Implications (Software Lifecycle)
- **Fail-closed error handling** is a recurring safety pattern in this project's regulatory
  material (see the planned `mdux.ml.runtime::Classifier1D`, issue #18, which must refuse
  construction rather than silently degrade on a golden-vector mismatch). A `std::expected`-based
  API makes "did this fail, and how" part of the type signature every caller must handle, rather
  than an exception path a caller can forget to catch.
- **Error evidence, not just an error code.** A safety-relevant failure should carry enough
  structured detail (which layer, which check, which value) to serve as an incident record — a bare
  `bool` or a generic exception message does not.

## Decision

| Zone | Rule |
|---|---|
| Governed (`MduXCore`) | `std::expected` and `noexcept` throughout. No throwing. |
| Adapter (`MduX`) | Exceptions permitted at construction boundaries only, never across a `noexcept` render/predict path. |
| Host tools (`tools/`) | Exceptions freely — they are never shipped, and their code is not qualified. |

**Why `std::expected` and not exceptions, for the governed zone specifically:** Class C and Vulkan
SC deployments routinely build with `-fno-exceptions`. The non-throwing operations on
`std::expected` do not require exception handling, and it is available on MSVC 19.33+, GCC 12+, and
Clang 16+ — all comfortably below this project's enforced floor of MSVC 19.40 / GCC 15 / Clang 20
(ADR-003), so adopting it costs nothing in toolchain reach. Throwing observers such as `.value()`
remain prohibited in governed code.

**Make it a build error, not a review comment.** Compile `MduXCore` (or, at minimum, any future
`src/ml/` target — see issue #18) with `-fno-exceptions -fno-rtti` / `/EHs-c- /GR-` in its own
object library. The governed-zone source lint also rejects `throw`, `try`, and `catch`; on MSVC,
the exception-disabled diagnostics are promoted to errors. A stray throwing construct therefore
fails the build rather than merely triggering a warning that a busy reviewer can miss.

**Error types carry evidence.** A governed-zone error is a struct, not a bare enum, whenever the
failure has diagnostic content worth keeping — e.g. the planned `MlError` (issue #18) records the
diverging layer index, golden-vector index, element index, and both the expected and actual bit
patterns. That struct is the audit record if a device fails closed in the field; a bare error code
would discard exactly the information an incident report needs.

**Re-enable `bugprone-exception-escape`** in the governed zone's `.clang-tidy` (see ADR-004) once
`MduXCore` exists, so a `noexcept` violation is caught by static analysis rather than only at
runtime or at link time.

**`MedicalUiRenderer`'s existing throwing constructor is grandfathered**, not retrofitted — it lives
in the adapter zone today and is scheduled for deletion (issue #13, S9) when the HTML/CSS UI path
is retired, not for a `std::expected` rewrite in place.

## Alternatives Considered

### 1. Exceptions everywhere, including the governed zone (Rejected)
**Pros:** Familiar C++ idiom; no new error-handling vocabulary to learn.
**Cons:** Directly incompatible with `-fno-exceptions` builds, which this project's own regulatory
material (Vulkan SC, Class C) treats as a real deployment target. Adopting exceptions in the
governed zone would make a stated compliance goal unreachable by construction.

### 2. Error codes (bare enums / `int`) everywhere (Rejected)
**Pros:** Zero-overhead, no library dependency, familiar to C-adjacent embedded developers.
**Cons:** Nothing in the type system forces a caller to check the code — the classic C failure mode
this project's safety-critical framing is trying to avoid. Also discards diagnostic detail unless
paired with an out-of-band side channel, which reintroduces most of `std::expected`'s complexity
without its type safety.

### 3. A custom `Result<T, E>` type instead of `std::expected` (Rejected)
**Pros:** Full control over the API surface; no dependency on a specific standard library feature.
**Cons:** `std::expected` already exists, is standardized, and is available on every compiler this
project supports (see "Decision" above) — writing a bespoke equivalent would be exactly the kind of
unnecessary custom infrastructure this project's zero-SOUP direction (issue #18) argues against
elsewhere. `MduXCore` may still expose a `mdux::Result<T, E>` alias over `std::expected` for
call-site brevity; that is a naming convenience, not a reimplementation.

## Consequences

### Positive
- The governed zone becomes usable in `-fno-exceptions` builds, which is a prerequisite for taking
  any Vulkan SC / Class C deployment claim seriously rather than as aspiration.
- Structured error types double as incident-evidence records, which is directly useful for the
  fail-closed patterns already planned for `.medui` compilation and ML inference.
- The exception-disabled object library turns throwing constructs into build failures, closing the
  gap left by `.clang-tidy:9` currently disabling the one check that would otherwise catch this.

### Negative
- Two error-handling idioms coexist in the codebase (adapter exceptions, governed `std::expected`)
  during the transition, which is a real source of confusion until `MedicalUiRenderer` is deleted.
- `std::expected`-based APIs are more verbose at call sites than exceptions for the common
  happy-path case — an accepted cost given the deployment constraint that motivates this decision.

### Risks and Mitigations
- **A future governed-zone PR quietly adds a `throw`.** *Mitigation*: the exception-disabled object
  library makes this a compile failure, not a style question.
- **`std::expected` gets used inconsistently** (some call sites check it, others `.value()` it and
  risk termination when the expected is empty). *Mitigation*: the governed-zone CI lint rejects
  `.value()`; callers must branch on `has_value()`/`operator bool` and then use non-throwing
  accessors. The re-enabled `bugprone-exception-escape` check remains defense in depth for other
  exception paths.

## References
- [TrustSC ADR-001 through ADR-003](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) (the text/font pipeline's fail-closed self-test pattern this policy is modeled on)
- ADR-003: Compiler Modernization for C++23 Modules Support (this repository) — the toolchain floor that makes `std::expected` unconditionally available
- ADR-004: Trust zones in C++ (this repository) — defines the governed/adapter/tools boundary this policy is scoped by

## Approval
- **Decision Date**: 2026-07-26
- **Approved By**: Project maintainer
- **Review Date**: when `MduXCore` first ships a public API (issue #11, S7)
