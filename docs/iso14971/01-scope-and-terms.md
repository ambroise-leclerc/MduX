# ISO 14971:2019 §1–§3 — Scope, normative references, terms and definitions

## §1 Scope

ISO 14971:2019 specifies a process for a manufacturer to identify hazards associated with a medical
device, estimate and evaluate the associated risks, control those risks, and monitor the
effectiveness of that control — across the device's whole life cycle, not only before its first
release. It applies to the manufacturer of the device, evaluating risk in the context of that
device's intended use.

MduX is not a medical device and performs no risk management process of its own against a device's
intended use — it has none to evaluate against. A manufacturer integrating MduX runs this process
for their device, informed by (among other things) how MduX behaves. This corpus documents that
latter part: where MduX's own architecture removes a category of risk regardless of the integrating
device's specific use, and where it does not.

## §2 Normative references
<!-- pointer: Read alongside docs/iec62304/ and docs/iso13485/, which cover the two standards this one is written to work with. -->

ISO 14971:2019 is written to work alongside IEC 62304 (software life cycle, for the
software-specific portions of a device's risk) and ISO 13485 (the quality management system the
risk management process runs inside). See [`../iec62304/`](../iec62304/) and
[`../iso13485/`](../iso13485/) for this project's treatment of those two.

## §3 Terms and definitions

As with the other two standards, this corpus does not restate the glossary. The terms below recur
across this directory.

| Term | Where MduX makes it concrete |
|---|---|
| Hazard | A potential source of harm. This corpus only names a hazard where MduX's own architecture forecloses it structurally (see [`04-risk-evaluation-and-control.md`](04-risk-evaluation-and-control.md)) — it does not enumerate hazards for a use case MduX itself has no visibility into. |
| Risk control measure | A mechanism that reduces risk to an acceptable level. [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust-zone architecture and [ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)'s exception-disabled governed zone are MduX's two concrete instances. |
| Verification (of a risk control measure) | As elsewhere in this corpus, "verified" means a specific, re-runnable check — `mdux_verify_trust_zones()`, an exception-disabled build leg — not a review that happened once. |
| Residual risk | The risk remaining after control measures are applied. MduX cannot evaluate a device's overall residual risk — that requires the device-level analysis this corpus repeatedly notes MduX does not perform. |
