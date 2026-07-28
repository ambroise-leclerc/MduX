# IEC 62304:2006 §7 — Software risk management process

§7 is where IEC 62304 hands off to ISO 14971: software-specific risk analysis, the risk control
measures that follow from it, verifying those measures actually work, and re-running the analysis
whenever the software changes in a way that could affect it.

## §7.1 Analysis of software contributing to hazardous situations

This sub-clause asks which software items could contribute to a hazardous situation, and how. It
presupposes a device-level risk analysis to analyze *against* — MduX, as a UI SDK rather than an
assembled device, does not perform this analysis itself; a device integrating MduX is responsible
for analyzing how its use of MduX's components could contribute to a hazard in that device's
specific context. What this corpus can do is describe, honestly, where MduX's own architecture
already forecloses a category of failure regardless of the integrating device's risk analysis —
see §7.2.

## §7.2 Risk control measures

A risk control measure is a specific mechanism that keeps an identified risk from occurring or
reduces its severity. MduX's clearest example is architectural rather than procedural: the
trust-zone split ([ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)) forecloses an entire class of
risk — a governed, safety-relevant module reaching a windowing or graphics dependency it should
never touch — by making that reachability a `FATAL_ERROR` at configure time. A control enforced by
the build graph cannot be silently skipped the way a documented procedure can be.

```json
{
  "justification_id": "JUS-005",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §7.2 Risk control measures",
  "rationale": "mdux_verify_trust_zones() walks a governed target's full transitive link graph and fails the build if it reaches Vulkan or a windowing library, which controls the risk of an unintended dependency reaching safety-relevant code without requiring a reviewer to catch it by inspection.",
  "evidence_refs": ["cmake/MduXTrustZones.cmake", "docs/adr/ADR-004-trust-zones-in-cpp.md"]
}
```

A second example, narrower but concrete: [ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)
requires the governed zone to compile with exceptions disabled, so a stray `throw` in
safety-relevant code is a build failure rather than a runtime behaviour nobody tested for.

## §7.3 Verification of risk control measures
<!-- pointer: mdux_verify_trust_zones() and the exception-disabled governed build are verified by running on every CI leg, so a regression is a build failure rather than a missed review. -->

A risk control measure that is not itself verified is an assertion, not a control. Both examples
above are verified the same way they are enforced: `mdux_verify_trust_zones()` runs at every
configure of every CI leg, and the exception-disabled governed build is exercised on every build
too — a regression in either is a build failure, not something that depends on a reviewer noticing.

## §7.4 Risk management of software changes

Re-analyzing risk after a change is what keeps §7.1–§7.3 from being a one-time exercise. MduX's
mechanical controls give this a specific, checkable form: a change that would introduce a new
Vulkan dependency into a governed target, or re-enable exceptions in the governed zone, fails CI
immediately rather than waiting for a risk-management review to notice it weeks later. That does
not eliminate the need for human risk-management judgment on changes these mechanisms don't cover
— only the specific classes of regression named above are addressed this way.
