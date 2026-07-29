# IEC 62366-1:2015 §1–§3 — Scope, normative references, terms and definitions

## §1 Scope

IEC 62366-1 specifies a process for a manufacturer to analyze, specify, design, verify, and
validate the usability of a medical device's user interface, so far as usability relates to safety.
It is explicitly scoped to *use-related* risk — errors and hazards arising from how a user
interacts with the device — rather than the device's broader functional risk, which ISO 14971
covers.

MduX renders content described by a device's own UI design; it does not itself define what that
device's users see or how they interact with it in a specific clinical context. This corpus
documents where MduX's rendering and compilation mechanisms bear on use-related risk regardless of
the device (chiefly, once built: a compile-time-enforced content budget that keeps critical
information from being silently truncated or occluded) and states plainly where the process this
standard describes remains entirely the integrating device's responsibility.

## §2 Normative references
<!-- pointer: Read alongside docs/iso14971/, which covers the risk-management framework this standard's use-related risk analysis sits inside. -->

IEC 62366-1 is written to be read alongside ISO 14971, for the risk-management framework its
use-related risk analysis sits inside. See [`../iso14971/`](../iso14971/) for this project's
treatment of that standard.

## §3 Terms and definitions

As elsewhere in this corpus, definitions are not restated. The terms below recur across this
directory.

| Term | Where MduX makes it concrete |
|---|---|
| User interface | For MduX, the concrete artifact is a compiled `.medui` screen — a `CompiledScreenPackage` (issue #15) — not the source `.medui` file, since the runtime only ever sees the compiled form. |
| Use error | An error arising from user interaction with the interface, as distinct from a device malfunction. MduX has no mechanism addressing use error directly yet; the closest adjacent idea is rendered-truth verification (issue #16), which confirms *what is displayed* matches the compiled screen's specification, a precondition for a user being able to act on correct information at all. |
| Formative evaluation | Evaluation performed during design, to identify use-related problems and inform further design iteration. No MduX mechanism yet — this belongs to a device's own UI design process, once `.medui` screens exist for a device to iterate on. |
| Summative evaluation | Evaluation performed on the final user interface design, to confirm safe use has been achieved. Device-level; no MduX mechanism. |
