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

[`AI-Reference.md`](AI-Reference.md) is the per-clause index — one row per clause section, each
with a one-sentence pointer and a deep link to the heading that covers it. It is generated from
this directory's own headings and `Justification` objects, and CI fails if it is out of date.

## Schemas

[`schemas/risk-record.schema.json`](schemas/risk-record.schema.json) is one hazard, its
evaluation, and the requirements that control it.

Its first three members — `id`, `description`, `controlled_by` — are the same fields, with the same
names and the same constraints, as `mdux::governance::Hazard`. That alignment is the deliverable,
not a convenience: it is what makes a risk record written by hand and a `Hazard` built in C++ the
same object, and `tools/docs-lint/check_schema_type_drift.py` fails the build if the two stop
agreeing. `controlled_by` being non-empty is the machine-checked ISO 14971 §7 / IEC 62304 §4.2
join.

The schema deliberately does not define severity or probability scales. §4.4 makes risk
acceptability criteria the manufacturer's to set in its risk management plan, so a fixed enum here
would be this project inventing a criterion it has no standing to set; those members are free
strings, and `scale_ref` names the document that gives them meaning.

## What this directory replaced

Two documents are deleted as of issue #29: `docs/MduX_ISO-14971-Risk-Management-Framework.md`
(713 lines) and `risk-assessment-templates.md` (935 lines). The second documented three C++
namespaces that do not exist in the tree and prescribed severity and probability scales that are
not this project's to prescribe. See
[`../governance/superseded-documents.md`](../governance/superseded-documents.md) for the reasoning,
including why folding the templates into `software_development_file/templates/` — the alternative
issue #29 offered — would have carried a fictional API forward.
