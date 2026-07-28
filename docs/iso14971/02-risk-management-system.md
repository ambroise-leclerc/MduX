# ISO 14971:2019 §4 — General requirements for risk management system

## §4.1 Risk management process
<!-- pointer: Device-level: MduX runs no risk management process of its own, and has no device whose life cycle one would span. -->

A manufacturer must establish, document, and maintain a risk management process spanning the whole
device life cycle. This is device-level scope; MduX does not run this process itself. The nearest
adjacent practice this repository has is procedural rather than a risk management process in the
standard's sense: every ADR's "Consequences" section records risks accepted or mitigated by that
specific decision, at the point the decision is made — a narrower, per-decision habit, not a
continuous device-level process.

## §4.2 Management responsibilities

Device-level responsibility (providing resources, defining a risk acceptability policy, reviewing
the process's suitability) belongs to a manufacturer's management, not to MduX.

## §4.3 Competence of personnel

Device-level responsibility. MduX cannot attest to the competence of the people who developed it in
the sense this clause asks a manufacturer to establish for its own risk management personnel.

## §4.4 Risk management plan

A risk management plan sets the scope, criteria for risk acceptability, and verification activities
for a *specific device's* risk management. MduX has no device to plan risk management for. Where
MduX's own architecture bears on risk (see [`04-risk-evaluation-and-control.md`](04-risk-evaluation-and-control.md)),
the applicable "plan" is closer to the relevant ADR's own decision record than a risk management
plan in this standard's sense.

## §4.5 Risk management file

A risk management file is the collected record — analysis, evaluation, control, verification,
review — for a specific device. MduX does not maintain one, because it is not the device. What this
corpus can offer a manufacturer building their own risk management file is a pointer: the specific
ADRs and mechanisms in [`04-risk-evaluation-and-control.md`](04-risk-evaluation-and-control.md) are
citable evidence a manufacturer's own file could reference, not a risk management file in
themselves.
