# ISO 14971:2019 §8–§9 — Evaluation of overall residual risk, risk management review

## §8 Evaluation of overall residual risk

After individual risks are controlled, this clause asks whether the *combined* residual risk across
the whole device remains acceptable — a risk can be individually acceptable while a combination
of them is not. This evaluation requires a complete, device-level risk analysis to sum across, which
MduX does not have. Nothing in this corpus should be read as a claim that MduX's two identified
hazard categories represent the totality of risk a device integrating MduX carries — they are the
two categories MduX's own architecture happens to address, not an exhaustive analysis.

## §9 Risk management review
<!-- pointer: Device-level: a review before release is a management activity MduX has no release and no management to run. -->

Before release, a manufacturer reviews whether the risk management plan was carried out, overall
residual risk is acceptable, and appropriate methods are in place to gather production information
(§10). This is a device-level release gate MduX, having no release of its own in the device sense,
does not perform. The closest structural analog is this repository's own CI gate — a pull request
cannot merge with a failing trust-zone check, evidence mismatch, or lint finding — but that gates
*this repository's* changes, not a device's release.
