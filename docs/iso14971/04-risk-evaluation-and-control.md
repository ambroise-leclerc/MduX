# ISO 14971:2019 §6–§7 — Risk evaluation and risk control

## §6 Risk evaluation
<!-- pointer: Device-level: acceptability criteria belong to the manufacturer's risk management plan, and MduX has no standing to set them. -->

Deciding whether an estimated risk is already acceptable against the manufacturer's own criteria,
or needs further control. This decision is device-level and depends on the risk-acceptability
policy from a manufacturer's risk management plan (§4.4) — MduX has neither the device context nor
the acceptability policy to make this evaluation.

## §7.1–§7.2 Risk reduction, risk control option analysis

Choosing which control options (inherent safety by design, protective measures, information for
safety) to apply, and in what order — the standard has a stated preference for inherent safety by
design over the alternatives. MduX's own practice already follows that preference structurally,
even though the analysis behind it happened at the architecture level rather than against a named
device risk: [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust zones and
[ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)'s exception policy are both
inherent-safety-by-design measures — a `FATAL_ERROR` at configure time or a compile failure, not a
runtime check or a warning label.

## §7.3 Implementation of risk control measures

Putting a decided control measure into effect, and being able to show that it is in effect rather
than merely decided. This is the sub-clause where MduX has its clearest instance, because the
control and the evidence that it runs are the same artifact:

```json
{
  "justification_id": "JUS-012",
  "standard": "ISO 14971:2019",
  "clause_ref": "ISO 14971:2019 §7.3 Implementation of risk control measures",
  "rationale": "mdux_verify_trust_zones() is a risk control measure implemented as a mechanical build-time check rather than a documented procedure: it walks a governed target's transitive link graph and fails the configure step if it reaches Vulkan or a windowing library, which is the hazardous-situation category identified in docs/iso14971/03-risk-analysis.md.",
  "evidence_refs": ["cmake/MduXTrustZones.cmake", "docs/adr/ADR-004-trust-zones-in-cpp.md"]
}
```

## §7.4 Residual risk evaluation
<!-- pointer: Because MduX's trust-zone check is a hard build failure rather than a probabilistic mitigation, residual risk for the category it covers is effectively zero - contingent on the check still running in CI. -->

Evaluating the risk remaining after a control measure is applied. Since the trust-zone check is a
hard build failure rather than a probabilistic mitigation, the residual risk it leaves behind (a
governed module reaching a forbidden dependency, undetected) is effectively zero *for the specific
category it covers* — contingent on the check itself running in CI, which is why
[ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)'s byte-identity verification and the
overall CI matrix matter to this evaluation too: a control measure that could silently stop running
would reintroduce the residual risk it was meant to close.

## §7.5 Risk/benefit analysis

Weighing an unacceptable risk against the medical benefit a device provides, when further risk
reduction is not practicable. Device-level; MduX has no benefit analysis of its own to weigh
against, since it is not the device delivering a clinical benefit.

## §7.6 Risks arising from risk control measures
<!-- pointer: MduX's trust-zone check fails the build rather than passing silently, so a misconfiguration of the control is visible immediately instead of leaving an undetected gap. -->

A control measure can itself introduce new risk. The trust-zone check's own failure mode is a hard
build stop, not a silent pass-through — a configuration error in the check would be visible
immediately (the build breaks) rather than manifesting as an undetected gap, which is the failure
mode this sub-clause is most concerned with.

## §7.7 Completeness of risk control
<!-- pointer: Device-level: completeness is judged across a device's whole hazard list, which MduX cannot see from inside one dependency. -->

Confirming every identified risk has been addressed by a control measure. This corpus's own honesty
about scope is the relevant discipline here: [`03-risk-analysis.md`](03-risk-analysis.md) names
exactly two hazard categories MduX addresses, and this file names control measures for exactly
those two — no broader completeness claim is made, because MduX has not performed a device-level
risk analysis broad enough to support one.
