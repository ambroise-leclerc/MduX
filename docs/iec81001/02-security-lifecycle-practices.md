# IEC 81001-5-1:2021 — Security lifecycle practices

Named by practice category, not by clause number — see the confidence note in
[`README.md`](README.md). Each category below is a well-established security-lifecycle activity;
this project has not verified which specific clause of IEC 81001-5-1 each corresponds to.

## Security risk management

Identifying security risks and feeding them into the device's overall risk management process
alongside safety risk (ISO 14971). MduX performs no device-level security risk assessment — it has
no device to assess. Where MduX's own architecture removes a category of risk regardless of the
device (see "Secure by design" below), this corpus names it; it does not assess residual security
risk for a device it has no visibility into, for the same reason [`../iso14971/`](../iso14971/)
does not assess residual safety risk.

## Secure by design

Building security properties into the architecture rather than adding them after implementation.
MduX's trust-zone architecture is the clearest instance: a governed module's link graph is
mechanically restricted from reaching Vulkan or a windowing library, which is a design-time
constraint on attack surface, not a runtime mitigation layered on afterward.

```json
{
  "justification_id": "JUS-013",
  "standard": "IEC 81001-5-1:2021",
  "clause_ref": "IEC 81001-5-1:2021 §4 General requirements",
  "rationale": "The governed/adapter/tools trust-zone split constrains a governed target's attack surface at the architecture level - mechanically enforced by MduXTrustZones.cmake - rather than relying on a runtime security control layered on afterward. Cited at the general-requirements level rather than a specific security-lifecycle-practice sub-clause, per this corpus's stated confidence limits.",
  "evidence_refs": ["docs/adr/ADR-004-trust-zones-in-cpp.md", "cmake/MduXTrustZones.cmake"]
}
```

## Secure implementation

Coding practices and tooling that prevent implementation-level vulnerabilities. The governed zone's
exception-disabled, `noexcept`-throughout discipline ([ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md))
is a narrow but real instance: an entire class of undefined-behaviour-adjacent failure (an
unhandled exception escaping a boundary the caller assumed was `noexcept`) is a compile-time or
build-configuration failure rather than a runtime crash an attacker could potentially exploit.

## Security verification and validation

Testing that security properties actually hold, not just asserting they were designed in. The
mechanical checks already covered elsewhere in this corpus apply here too:
`mdux_verify_trust_zones()` for the architectural constraint above, and the evidence pipeline's
cross-toolchain byte-identity checks for build-artifact tamper-evidence. Neither was built as a
security test specifically, but both verify a property this practice category cares about.

## Vulnerability and defect management

See the "Vulnerability and defect management" row in
[`01-scope-and-terms.md`](01-scope-and-terms.md): GitHub Issues, with the general-purpose-tracking
limits already discussed in [`../iec62304/07-problem-resolution-process.md`](../iec62304/07-problem-resolution-process.md).

## Security update management

Getting a security fix to a deployed device. MduX has no deployed product yet, so it has no update
mechanism to describe — the same honest gap noted for maintenance in
[`../iec62304/04-maintenance-process.md`](../iec62304/04-maintenance-process.md).

## Security documentation and guidance

Documenting security-relevant information for integrators and users. This corpus, and this
project's ADRs generally, are the closest MduX has to this today — with the caveat, stated
throughout this directory, that none were written against IEC 81001-5-1's specific documentation
requirements.
