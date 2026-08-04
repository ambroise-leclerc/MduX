# Software Architecture Design (SAD) — MduX

> Filled-in example for MduX itself. See
> `software_development_file/templates/IEC_62304/SAD.md` for the blank template, and
> `docs/iec62304/03-development-process.md` for the underlying clause guidance.

## Document control

- **Product / software item:** MduX — an experimental, proof-of-concept C++23-modules Vulkan/Vulkan
  SC UI/rendering library. Not a finished product (see `AGENTS.md` § 1).
- **Version:** the project version reported by `mdux::Version::getString()` (`CMakeLists.txt`'s
  `project(... VERSION ...)`, currently `0.4.0`); see `CMakeLists.txt` for the exact build.
- **Safety classification:** Class A, B, or C, chosen per-application. Unlike its sibling project
  TrustSC (Class B/C only), MduX keeps Class A explicitly in scope (`docs/governance/citation-convention.md`).
  **Note the gap:** `mdux::ComplianceMetadata.deviceClass` (`include/mdux/mdux.cppm`) is a free-form
  `std::string` an application sets, not a checked enum — nothing in MduX today validates that its
  value is one of `"A"`/`"B"`/`"C"`, or cross-checks it against anything.
- **Author(s):** MduX maintainers
- **Date:** see `docs/adr/README.md` for the dated ADR trail this SAD summarizes

## 1. Purpose and scope

This SAD describes MduX's own architecture as a library — the software items an application links
against — not a finished device. It is the applied counterpart to
`docs/iec62304/03-development-process.md`.

## 2. Software items and their decomposition

> `IEC 62304:2006 §5.3.1 Transform requirements into an architecture`

Three trust zones, formalized by [ADR-004](../../../docs/adr/ADR-004-trust-zones-in-cpp.md):

- **`MduXCore` — governed.** `std`-only modules with no Vulkan or windowing dependency:
  `mdux.core.result` (the `Result<T, E>` alias over `std::expected` every governed API returns),
  `mdux.core.units`, `mdux.evidence.digest`/`mdux.evidence.json`/`mdux.evidence.report` (the
  byte-verified evidence pipeline, [ADR-007](../../../docs/adr/ADR-007-evidence-pipeline-doctrine.md)),
  `mdux.governance` (the `Justification`, `Requirement`, `VerificationCase`, `Hazard`,
  `ProblemReport`, `AuditEvent` and `ComplianceProgram` types, issue #34) and
  `mdux.governance.compliance` (the two release-evidence exports over them, issue #35).
- **`MduX` — adapter.** Publicly links `MduXCore` plus `Vulkan::Vulkan`: the facade module
  (`include/mdux/mdux.cppm` - version, compliance metadata and Vulkan capability reporting), the
  renderer `mdux.render.vulkan` (`UiRenderer`, `VulkanRenderContext`) and its headless target
  `mdux.render.offscreen`, and the Vulkan SC modules `mdux.vulkansc.memory` (`MemoryPoolManager`, static pool allocation without runtime
  `vkAllocateMemory` calls per frame) and `mdux.vulkansc.objects` (`DeviceObjectManager`).
- **`examples/` and `tools/` — never shipped.** `examples/VulkanSCTriangleExample.cpp` links GLFW
  for windowing, never linked into `MduX` itself; `SimpleMedicalUiExample.cpp` links neither GLFW
  nor a device, since building a frame requires neither.
  `tools/common` (`MduX::ToolsCommon`, host-only CLI/TOML helpers) and the Python scripts under
  `tools/docs-lint`/`tools/evidence-lint` never compile into a device artifact.

## 3. Interfaces between software items

> `IEC 62304:2006 §5.3.2 Develop an architecture for the interfaces of software items`

`MduXCore`'s governed modules have no dependency on `MduX`; `MduX` depends on `MduXCore` and Vulkan.
Every governed module's public surface is its exported `.cppm` interface, returning
`mdux::core::Result<T, E>` rather than throwing (see §4 of
`docs/adr/ADR-005-error-handling-and-exceptions-policy.md`), so a caller cannot silently ignore a
failure the way an unchecked exception escape or an ignored return code would allow. `examples/`
consumes `MduX`'s public headers only, the same as any external application would.

## 4. Segregation for risk control

> `IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control`

`mdux_verify_trust_zones()` (`cmake/MduXTrustZones.cmake`) walks every declared-governed target's
transitive link graph at configure time and fails the build if it finds a dependency matching
`^Vulkan::`, `^glfw$`, or `^glfw3$` — catching the dependency whether it arrives directly or through
another target, not just a direct `#include`. **State this precisely, not more than it is:** this is
a link-graph name check, not a memory-safety guarantee. C++ has no `#![forbid(unsafe_code)]`
equivalent — `MduXCore` being free of Vulkan/windowing dependencies says nothing about whether its
own code is memory-safe; that remains a property `MduXCore`'s own review and testing must establish,
not something this mechanism checks for it.

## 5. SOUP identification

> `IEC 62304:2006 §5.3.4 Identify SOUP items`

See `software_development_file/regulatory/IEC_62304/SOUP.md`, derived from
`docs/governance/soup-register.toml`.

## 6. Architecture verification

> `IEC 62304:2006 §5.3.5 Verify the architectural design`

Every structural decision above is recorded as an `Accepted` ADR — see
[`docs/adr/README.md`](../../../docs/adr/README.md) (7 ADRs at time of writing: ADR-001 through
ADR-007). `mdux_verify_trust_zones()` also runs on every CI build (`.github/workflows/ci.yml`), so
the segregation described in §4 is checked on every push, not only at review time.

## Justification records

```json
{
  "justification_id": "JUS-015",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control",
  "rationale": "mdux_verify_trust_zones() (cmake/MduXTrustZones.cmake) walks every governed target's transitive link graph at configure time and fails the build on a Vulkan or windowing dependency, catching it whether it arrives directly or transitively through another target - a mechanically enforced, build-time segregation check rather than a reviewed convention, though not a memory-safety guarantee.",
  "evidence_refs": [
    "cmake/MduXTrustZones.cmake",
    "docs/adr/ADR-004-trust-zones-in-cpp.md",
    "CMakeLists.txt"
  ]
}
```
