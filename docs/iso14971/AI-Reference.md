# ISO 14971:2019 — per-clause index

One row per clause section in this corpus: the clause, a one-sentence pointer to what
MduX does or does not provide for it, and a deep link to the heading that covers it.
Generated from the headings, `Justification` objects and pointer comments already
present in the modules — not hand-transcribed, so it cannot drift from the prose it
indexes without the source changing too.

Regenerate after editing any file in this directory:

```
python3 tools/docs-lint/generate_ai_reference.py docs/iso14971
```

A clause shown as `—` is one this corpus deliberately does not number — see
[`README.md`](README.md). It is not a gap in the index.

| Clause | Covers | Pointer | Justification(s) |
|---|---|---|---|
| §1 | [Scope](01-scope-and-terms.md#1-scope) | MduX is not a medical device and performs no risk management process of its own against a device's intended use — it has none to evaluate against. | — |
| §2 | [Normative references](01-scope-and-terms.md#2-normative-references) | Read alongside docs/iec62304/ and docs/iso13485/, which cover the two standards this one is written to work with. | — |
| §3 | [Terms and definitions](01-scope-and-terms.md#3-terms-and-definitions) | [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust-zone architecture and [ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)'s exception-disabled governed zone are MduX's two concrete instances. | — |
| §4.1 | [Risk management process](02-risk-management-system.md#41-risk-management-process) | Device-level: MduX runs no risk management process of its own, and has no device whose life cycle one would span. | — |
| §4.2 | [Management responsibilities](02-risk-management-system.md#42-management-responsibilities) | Device-level responsibility (providing resources, defining a risk acceptability policy, reviewing the process's suitability) belongs to a manufacturer's management, not to MduX. | — |
| §4.3 | [Competence of personnel](02-risk-management-system.md#43-competence-of-personnel) | MduX cannot attest to the competence of the people who developed it in the sense this clause asks a manufacturer to establish for its own risk management personnel. | — |
| §4.4 | [Risk management plan](02-risk-management-system.md#44-risk-management-plan) | MduX has no device to plan risk management for. | — |
| §4.5 | [Risk management file](02-risk-management-system.md#45-risk-management-file) | MduX does not maintain one, because it is not the device. | — |
| §5.1 | [Risk analysis process](03-risk-analysis.md#51-risk-analysis-process) | Device-level; no MduX mechanism. | — |
| §5.2 | [Intended use and reasonably foreseeable misuse](03-risk-analysis.md#52-intended-use-and-reasonably-foreseeable-misuse) | MduX has no device-level intended use to state — a manufacturer's intended use for their device is what this sub-clause asks about, and MduX's role within that use is theirs to characterize, not this library's. | — |
| §5.3 | [Identification of characteristics related to safety](03-risk-analysis.md#53-identification-of-characteristics-related-to-safety) | The one MduX-level analog worth naming: which components of a *build* could affect safety if misconfigured — for instance, whether governed code can reach Vulkan or a windowing library. | — |
| §5.4 | [Identification of hazards and hazardous situations](03-risk-analysis.md#54-identification-of-hazards-and-hazardous-situations) | ADR-004 identifies a specific hazardous-situation category - a governed, safety-relevant module reaching Vulkan or a windowing library it should never touch - as one MduX's own architecture can foreclose regardless of the integrating device's specific use, rather than leaving every device manufacturer to rediscover it independently. | JUS-011 |
| §5.5 | [Estimation of the risk(s) for each hazardous situation](03-risk-analysis.md#55-estimation-of-the-risks-for-each-hazardous-situation) | Estimating probability and severity requires a device-level context (who is exposed, what happens if the hazard occurs in *this* device) that MduX does not have. | — |
| §6 | [Risk evaluation](04-risk-evaluation-and-control.md#6-risk-evaluation) | Device-level: acceptability criteria belong to the manufacturer's risk management plan, and MduX has no standing to set them. | — |
| §7.1–§7.2 | [Risk reduction, risk control option analysis](04-risk-evaluation-and-control.md#7172-risk-reduction-risk-control-option-analysis) | MduX's own practice already follows that preference structurally, even though the analysis behind it happened at the architecture level rather than against a named device risk: [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust zones and [ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)'s exception policy are both inherent-safety-by-design measures — a `FATAL_ERROR` at configure time or a compile failure, not a runtime check or a warning label. | — |
| §7.3 | [Implementation of risk control measures](04-risk-evaluation-and-control.md#73-implementation-of-risk-control-measures) | mdux_verify_trust_zones() is a risk control measure implemented as a mechanical build-time check rather than a documented procedure: it walks a governed target's transitive link graph and fails the configure step if it reaches Vulkan or a windowing library, which is the hazardous-situation category identified in docs/iso14971/03-risk-analysis.md. | JUS-012 |
| §7.4 | [Residual risk evaluation](04-risk-evaluation-and-control.md#74-residual-risk-evaluation) | Because MduX's trust-zone check is a hard build failure rather than a probabilistic mitigation, residual risk for the category it covers is effectively zero - contingent on the check still running in CI. | — |
| §7.5 | [Risk/benefit analysis](04-risk-evaluation-and-control.md#75-riskbenefit-analysis) | Device-level; MduX has no benefit analysis of its own to weigh against, since it is not the device delivering a clinical benefit. | — |
| §7.6 | [Risks arising from risk control measures](04-risk-evaluation-and-control.md#76-risks-arising-from-risk-control-measures) | MduX's trust-zone check fails the build rather than passing silently, so a misconfiguration of the control is visible immediately instead of leaving an undetected gap. | — |
| §7.7 | [Completeness of risk control](04-risk-evaluation-and-control.md#77-completeness-of-risk-control) | Device-level: completeness is judged across a device's whole hazard list, which MduX cannot see from inside one dependency. | — |
| §8 | [Evaluation of overall residual risk](05-overall-residual-risk-and-review.md#8-evaluation-of-overall-residual-risk) | Nothing in this corpus should be read as a claim that MduX's two identified hazard categories represent the totality of risk a device integrating MduX carries — they are the two categories MduX's own architecture happens to address, not an exhaustive analysis. | — |
| §9 | [Risk management review](05-overall-residual-risk-and-review.md#9-risk-management-review) | Device-level: a review before release is a management activity MduX has no release and no management to run. | — |
| §10.1 | [General](06-production-and-post-production.md#101-general) | Device-level, and dependent on there being a released device generating field information — MduX has neither yet. | — |
| §10.2 | [Collection of information](06-production-and-post-production.md#102-collection-of-information) | MduX's nearest analog is far narrower: its own CI results and issue tracker are production information about *this repository*, not about a released device. | — |
| §10.3 | [Review of information](06-production-and-post-production.md#103-review-of-information) | Device-level; no MduX mechanism. | — |
| §10.4 | [Actions](06-production-and-post-production.md#104-actions) | Device-level; no MduX mechanism, and no field to act on. | — |
