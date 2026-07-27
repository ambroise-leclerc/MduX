# ISO 14971:2019 — clause-accurate reference

Unlike [`docs/iec62304/`](../iec62304/) and [`docs/iso13485/`](../iso13485/), this directory has no
predecessor to replace — ISO 14971 was referenced from both of those corpora before this issue but
never had its own treatment. It follows the same convention from the start:
[`docs/governance/citation-convention.md`](../governance/citation-convention.md)'s citation-key
format, original prose only, a `Justification` object wherever a real MduX mechanism applies.

## What this is, and is not

ISO 14971 describes a risk management process run *by a manufacturer, against a specific medical
device*. MduX is a UI SDK, not an assembled device with its own risk analysis — nearly everything
this standard asks for (identifying hazardous situations for a device's intended use, evaluating
risk/benefit, reviewing production data) is a device integrator's responsibility, not something
this library performs on their behalf. Where a clause has a genuine MduX-level analog — chiefly, a
handful of risk control measures MduX's architecture enforces regardless of what device it ends up
in — this corpus says so. Everywhere else, it says plainly that the clause belongs to whoever
integrates MduX into a device, rather than manufacturing a citation to look complete.

## Structure

| File | Clause(s) | Covers |
|---|---|---|
| [`01-scope-and-terms.md`](01-scope-and-terms.md) | §1–§3 | Scope, normative references, terms and definitions |
| [`02-risk-management-system.md`](02-risk-management-system.md) | §4 | Risk management process, management responsibilities, competence, risk management plan and file |
| [`03-risk-analysis.md`](03-risk-analysis.md) | §5 | Intended use, hazard identification, risk estimation |
| [`04-risk-evaluation-and-control.md`](04-risk-evaluation-and-control.md) | §6–§7 | Risk evaluation, risk control measures and their verification |
| [`05-overall-residual-risk-and-review.md`](05-overall-residual-risk-and-review.md) | §8–§9 | Evaluation of overall residual risk, risk management review |
| [`06-production-and-post-production.md`](06-production-and-post-production.md) | §10 | Collecting and acting on production and post-production information |

A per-clause index (`AI-Reference.md`) and JSON Schemas are tracked separately as issues #32
and #33.
