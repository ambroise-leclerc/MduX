# ADR-005: Error handling and exceptions policy

## Status
Accepted (2026-07-26)

## Context
MduX has no stated error-handling policy, and current code is inconsistent with the direction the
MduX ↔ TrustSC parity programme requires:

- `MedicalUiRenderer`'s constructor threw `std::runtime_error`. That type was deleted with the
  HTML/CSS path (issue #127), which resolved this instance by removal rather than by rewrite - as
  this ADR anticipated below. Deliberately cited without a file:line anchor: the code is gone, and
  `src/mdux.cpp` was trimmed from 575 lines to 85 in the same change, so any line number here
  would now point at something unrelated. Git history is the reference for what it looked like.
- `include/mdux/vulkansc/MemoryPoolManager.cppm:100` documents throwing behavior.
- `.clang-tidy` **disabled** `bugprone-exception-escape` — the check that would otherwise flag an
  exception escaping a `noexcept` boundary. Re-enabled by issue #116; see "What is enforced" below
  for what that is and is not worth.

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
| Governed (`MduXCore`) | `std::expected` and `noexcept` throughout. No throwing — see "What is enforced" for the two layers that check this and the one thing they do not cover. |
| Adapter (`MduX`) | Exceptions permitted at construction boundaries only, never across a `noexcept` render/predict path. |
| Host tools (`tools/`) | Exceptions freely — they are never shipped, and their code is not qualified. |

**Why `std::expected` and not exceptions, for the governed zone specifically:** Class C and Vulkan
SC deployments routinely build with `-fno-exceptions`. That the governed zone cannot *currently* be
built that way (see "What is enforced") does not weaken this reasoning — it is what keeps the
option open, since an exception-based API would foreclose it permanently rather than temporarily.
The non-throwing operations on
`std::expected` do not require exception handling, and it is available on MSVC 19.33+, GCC 12+, and
Clang 16+ — all comfortably below this project's enforced floor of MSVC 19.40 / GCC 15 / Clang 20
(ADR-003), so adopting it costs nothing in toolchain reach. Throwing observers such as `.value()`
remain prohibited in governed code.

**Make it a build error, not a review comment.** A stray throwing construct fails CI rather than
merely triggering a warning a busy reviewer can miss. What does the failing is described in "What
is enforced" below, and it is not what this ADR originally specified.

**Error types carry evidence.** A governed-zone error is a struct, not a bare enum, whenever the
failure has diagnostic content worth keeping — e.g. the planned `MlError` (issue #18) records the
diverging layer index, golden-vector index, element index, and both the expected and actual bit
patterns. That struct is the audit record if a device fails closed in the field; a bare error code
would discard exactly the information an incident report needs.

## What is enforced

Added by issue #116, which found this ADR asserting in the present tense a lint that had never been
written. The list below is exhaustive, and each entry names the file that does the work so a reader
can check the claim rather than take it.

### The constraint that shaped this

**`import std` and `-fno-exceptions` are mutually exclusive on GCC 16.** This ADR originally
specified compiling `MduXCore` with `-fno-exceptions -fno-rtti` / `/EHs-c- /GR-`. That is not
available while the governed zone uses `import std`:

- GCC records the language dialect in every module BMI. A governed module compiled with either flag
  is rejected against the shared `std` BMI with `language dialect differs 'C++23', expected
  'C++23/no-exceptions/no-rtti'`. Each flag fails this way on its own, not only in combination.
- CMake synthesises exactly one `__CMAKE__CXX23` std target for the whole build, shared by the
  adapter and host-tools targets, which must keep exceptions. There is no per-target std dialect.

A separately built governed-dialect `std` BMI does work when driven by hand, so the constraint is
the *sharing*, not the flags. Hand-rolling one would displace the most fragile part of this build,
and was judged not worth it against the alternatives below.

Two consequences worth stating plainly:

1. The "usable in `-fno-exceptions` builds" claim under Consequences is **not currently true**, and
   is not achievable by any project consuming `MduXCore` through `import std`. It is retained below
   as the motivation for the `std::expected` API, which it still is, and marked as unmet.
2. Revisiting this needs either a per-target std dialect from CMake, or the governed zone giving up
   `import std`. Neither is scheduled.

### The layers that do run

| Layer | Reads | Where | Gates on |
|---|---|---|---|
| `mdux-governed-lint` | the source | `tools/governed-lint/`, CI job `Governed Source Lint` | every toolchain |
| `governed.noThrow.symbolScan` | the emitted objects | `cmake/MduXNoHeapScan.cmake`, a ctest | GCC/Clang; reports only on MSVC |
| `bugprone-exception-escape` | the AST | `.clang-tidy` | nothing yet — see below |

Only the first is a gate everywhere. That is the honest reading of the table, and the reason the
source lint rather than the symbol scan is what the no-throw claim rests on.

**`mdux-governed-lint`** rejects `throw`, `try`/`catch`, throwing `.value()`, raw allocation,
filesystem and console facilities, clocks and randomness, unsafe casts, and `std::fma`. It scans
exactly the sources `CMakeLists.txt` lists for `MduXCore`, parsed out of that block rather than
globbed, and matches on the source with comments and string literals removed.

**`governed.noThrow.symbolScan`** sees a throw arriving through a std facility the source never
spells out, which `-fno-exceptions` would only have caught by refusing to compile the facility. It
forbids `__cxa_throw` across all 29 governed objects, and none references it.

It **tolerates**, and prints on every run, libstdc++'s throw *helpers*: `__throw_length_error`,
`__throw_logic_error` and `__throw_out_of_range_fmt`, 14 references across eight objects. They
arrive from inside `std::string`, `std::vector` and `std::string_view::substr` — `Json.cpp`'s
parser and `Governance.cpp`'s id splitting are both of that shape, and in both the caller's
invariant makes the throw unreachable. Forbidding them means banning those types from the governed
zone outright, which may be the right end state for a Class C build but is a larger decision than
#116 and needs its own ADR.

**This layer is a gate on GCC/Clang only, and is informational on MSVC.** The distinction it rests
on — a literal `throw` emits one symbol, the library's own throw sites emit another — is a property
of libstdc++'s code generation, not a portable one. The MSVC STL inlines its throw sites, so an
ordinary `std::string` growth path emits `_CxxThrowException` directly into the governed object,
the identical symbol a hand-written `throw` would emit. Nine governed objects reference it while
`mdux-governed-lint` confirms none of their sources contains a `throw`.

Forbidding that symbol would fail the build on correct code; tolerating it would forbid nothing. So
under `dumpbin` the profile forbids nothing, reports what it finds, and its closing message states
that no verdict is claimed. The rule is still enforced on Windows — by the source lint, which is
toolchain-independent. What is GCC-specific is only the object-level corroboration, and the
negative fixture is not registered on MSVC for the same reason.

**`bugprone-exception-escape`** is re-enabled, as this ADR's original follow-up asked, but it is
*available* rather than *enforced*: the only CI job running clang-tidy invokes it with `|| true`
against a GCC-generated compile database it cannot parse for module interface units. It becomes a
real gate when the Clang leg is restored (issue #48).

### What is therefore claimed

Governed code contains no throw expression and no source-level construct from the banned list,
checked mechanically on every pull request and on every toolchain by `mdux-governed-lint`. On
GCC/Clang that is independently corroborated in the emitted objects.

Three things are **not** established. Governed code can still reach the standard library's throwing
helpers through ordinary `std::string` and `std::vector` use. The object-level check does not gate
on MSVC, so the no-throw property is single-sourced to the source lint there. And `MduXCore` cannot
currently be compiled with `-fno-exceptions` on any toolchain. Anything stronger than the paragraph
above is not established.

**`MedicalUiRenderer`'s throwing constructor was grandfathered**, not retrofitted — it lived in the
adapter zone and was scheduled for deletion with the HTML/CSS UI path rather than for a
`std::expected` rewrite in place. Issue #127 deleted it, so the grandfathering has expired with
nothing left under it. Its replacement, `mdux::render::UiRenderer`, is `Result`-returning and
`noexcept` throughout, as this policy requires of new adapter code.

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
- **Unmet.** The governed zone was to become usable in `-fno-exceptions` builds, a prerequisite for
  taking any Vulkan SC / Class C deployment claim seriously rather than as aspiration. `import std`
  blocks it — see "The constraint that shaped this". The `std::expected` API is still the right
  shape for that goal, and is what makes it reachable if the constraint ever lifts, but the goal is
  not reached today.
- Structured error types double as incident-evidence records, which is directly useful for the
  fail-closed patterns already planned for `.medui` compilation and ML inference.
- A throwing construct fails CI — through `mdux-governed-lint` at the source level and
  `governed.noThrow.symbolScan` at the object level, rather than through the exception-disabled
  object library this ADR first proposed.

### Negative
- Two error-handling idioms coexist in the codebase: the host-tools zone throws (the TOML parser
  and the CLI, where an exception cannot reach a device) while the governed and adapter zones
  return `std::expected`. The transitional third case - a throwing constructor in the adapter zone
  - ended with `MedicalUiRenderer`'s deletion in issue #127.
- `std::expected`-based APIs are more verbose at call sites than exceptions for the common
  happy-path case — an accepted cost given the deployment constraint that motivates this decision.

### Risks and Mitigations
- **A future governed-zone PR quietly adds a `throw`.** *Mitigation*: `mdux-governed-lint` rejects
  the construct and `governed.noThrow.symbolScan` rejects the resulting `__cxa_throw` reference.
  Two independent layers, both required to pass, neither able to see everything the other does.
- **`std::expected` gets used inconsistently** (some call sites check it, others `.value()` it and
  risk termination when the expected is empty). *Mitigation*: `mdux-governed-lint` rejects
  `.value()` (GOV003); callers must branch on `has_value()`/`operator bool` and then use
  non-throwing accessors.
- **A governed module reaches a throwing std facility on a path that is actually taken.** The
  tolerated `__throw_*` helper references are believed unreachable because of caller invariants,
  and that belief is not mechanically checked. *Mitigation*: none today, stated rather than
  papered over. The scan prints all 14 references on every run so they stay counted, and the ADR
  that decides whether to ban `std::string` and `std::vector` from the governed zone is the
  place to resolve it.
- **A module is added to `MduXCore` and the lint does not notice.** *Mitigation*: the lint derives
  its file list from the `target_sources(MduXCore ...)` block, so registering the module is what
  enrols it. There is no second list to forget.

## References
- [TrustSC ADR-001 through ADR-003](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) (the text/font pipeline's fail-closed self-test pattern this policy is modeled on)
- ADR-003: Compiler Modernization for C++23 Modules Support (this repository) — the toolchain floor that makes `std::expected` unconditionally available
- ADR-004: Trust zones in C++ (this repository) — defines the governed/adapter/tools boundary this policy is scoped by

## Approval
- **Decision Date**: 2026-07-26
- **Approved By**: Project maintainer
- **Amended**: 2026-08-11 (issue #116) — added "What is enforced", recording that `import std`
  blocks the `-fno-exceptions` build this ADR specified, and replacing the never-written
  exception-disabled object library with the two layers that do run.
- **Review Date**: when either CMake gains a per-target `import std` dialect or the governed zone
  stops using `import std`, whichever comes first — both would reopen the `-fno-exceptions`
  question this ADR had to leave unmet.
