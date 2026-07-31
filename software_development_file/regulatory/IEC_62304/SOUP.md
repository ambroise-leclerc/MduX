# SOUP list and justification — MduX

> Filled-in example for MduX itself. See `software_development_file/templates/IEC_62304/SOUP.md`
> for the blank template. This document summarizes and links to the authoritative machine-readable
> register — it does not duplicate it — at
> `docs/governance/soup-register.toml`.

## Document control

- **Product / software item:** MduX
- **Register scope:** `runtime-adapter-zone-and-build-tooling` (per the register's own `scope`
  field)
- **Owner:** MduX governance maintainers (per the register's `owner` field)

## 1. Purpose

> `IEC 62304:2006 §5.3.4 Identify SOUP items` / `§8.1.3 SOUP identification` / `§8.3.2 SOUP anomaly list`

`docs/governance/soup-register.toml` is the single source of truth for every third-party (SOUP) and
build-tool dependency this project uses. This document is a guided summary of it, not a second copy.

## 2. SOUP register summary

Four entries at time of writing, split by whether they are ever deployed to a device
(`runtime_deployment`):

**Runtime, adapter zone (`runtime_deployment = true`)** — the only entry actually linked into a
built `MduX` binary: the Vulkan SDK/loader (`vulkan-sdk`), confined to the adapter target and
mechanically checked absent from `MduXCore` by `mdux_verify_trust_zones()`.

**Never deployed:** GLFW 3.4 (`glfw-3.4`, examples only, never linked into `MduX` itself), CMake 4.0+
(`cmake-4.0`, a build-time tool), and the MSVC/GCC/Clang C++23 toolchain floor (`cxx23-toolchain`,
also build-time only).

This is a short list by design — see the register's own `scoping_policy` header field for why: the
trust-zone split (ADR-004) confines every entry to adapter/examples/build, and the zero-SOUP
scoping decisions already made (a hand-written SHA-256 and canonical JSON,
`mdux.evidence.digest`/`mdux.evidence.json`, issue #12, rather than a hashing or JSON library) or
planned (hand-parsed TrueType/safetensors/SPIR-V, QOI instead of PNG — issues #13/#14/#17/#18) are
meant to keep it that way as each baker lands.

## 3. Known anomalies (§8.3.2)

Not yet tracked as a distinct list separate from each entry's `risk_controls` field. Each entry
does carry a `known_anomaly_tracking` field pointing at how a defect would be recorded (a
`ProblemReport`, `mdux.governance.compliance`, issue #35) — but as of this document, zero
`ProblemReport`s have actually been recorded against any SOUP entry; this is a statement of the
mechanism, not evidence that it has been exercised.

## 4. SOUP update policy

CMake version floors and compiler version floors are enforced by explicit checks at configure time
(`CMakeLists.txt`; see `docs/adr/ADR-003-compiler-modernization.md` for the compiler floor's
rationale), so an update to either requires an explicit, reviewed change to that check rather than
silently drifting to whatever a developer's machine happens to have installed. GLFW's version is
pinned by an exact `GIT_TAG 3.4` in the CPM fallback path in `examples/CMakeLists.txt`.

## Justification records

```jsonc
{
  "justification_id": "JUS-017",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3.4 Identify SOUP items",
  "rationale": "Every SOUP or build-tool dependency is recorded once in docs/governance/soup-register.toml with supplier, license, integration_path, pinned_by, and risk_controls, and the only runtime-deployed entry (the Vulkan SDK) is mechanically confined to the adapter trust zone by mdux_verify_trust_zones().",
  "evidence_refs": [
    "docs/governance/soup-register.toml",
    "cmake/MduXTrustZones.cmake"
  ]
}
```
