# ISO 14971:2019 §5 — Risk analysis

## §5.1 Risk analysis process

Documenting a risk analysis process, its scope, and the device it covers. Device-level; no MduX
mechanism.

## §5.2 Intended use and reasonably foreseeable misuse

Identifying how a device is intended to be used, and how it could foreseeably be misused. MduX has
no device-level intended use to state — a manufacturer's intended use for their device is what this
sub-clause asks about, and MduX's role within that use is theirs to characterize, not this
library's.

## §5.3 Identification of characteristics related to safety

Identifying which qualitative and quantitative characteristics of a device could affect safety.
The one MduX-level analog worth naming: which components of a *build* could affect safety if
misconfigured — for instance, whether governed code can reach Vulkan or a windowing library. That
is answered structurally rather than by enumeration; see §5.4.

## §5.4 Identification of hazards and hazardous situations

This is the sub-clause where MduX's architecture has a genuine, if narrow, answer. A governed
module reaching a platform, graphics, or windowing dependency it should never touch is a hazardous
situation category MduX's own trust-zone architecture identifies and forecloses by construction,
independent of what specific device MduX ends up in:

```json
{
  "justification_id": "JUS-011",
  "standard": "ISO 14971:2019",
  "clause_ref": "ISO 14971:2019 §5.4 Identification of hazards and hazardous situations",
  "rationale": "ADR-004 identifies a specific hazardous-situation category - a governed, safety-relevant module reaching Vulkan or a windowing library it should never touch - as one MduX's own architecture can foreclose regardless of the integrating device's specific use, rather than leaving every device manufacturer to rediscover it independently.",
  "evidence_refs": ["docs/adr/ADR-004-trust-zones-in-cpp.md"]
}
```

A second, narrower hazard category: an exception escaping a `noexcept` boundary in code that must
run under `-fno-exceptions` (a common Class C / Vulkan SC deployment constraint) — addressed by
[ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md).

## §5.5 Estimation of the risk(s) for each hazardous situation

Estimating probability and severity requires a device-level context (who is exposed, what happens
if the hazard occurs in *this* device) that MduX does not have. This corpus names the hazard
categories above without estimating their risk, because that estimation only makes sense once a
specific device's exposure is known.
