# ISO 13485:2016 quality management system — scope note

> Template — ISO 13485:2016. See
> [`docs/iso13485/README.md`](../../../docs/iso13485/README.md) for the underlying clause-by-clause
> guidance. See
> `software_development_file/regulatory/ISO_13485/README.md` (issue #38, a later PR in this stack)
> for how MduX states this same scope note for itself.

A single markdown file cannot substitute for a manufacturer's actual ISO 13485 quality management
system — that system spans document control, management review, resource management, and much
more that lives in a manufacturer's own QMS tooling, not in a software repository. This file exists
to state explicitly, for whoever reads this `software_development_file/`, **which pieces of this
product's engineering scaffolding intentionally support a QMS process, and which the manufacturer's
own QMS must still supply.**

## What this product's engineering artifacts can feed into your QMS

> `ISO 13485:2016 §4.2 Documentation requirements` and `ISO 13485:2016 §7.3 Design and development`

[ List: e.g. requirement/verification traceability records
(`mdux.governance.compliance::traceabilityMatrix()`, issue #35), generated build evidence
(`mdux.evidence.report`, issue #12), the ADR trail — whichever of these your own build actually
produces, with file paths. ]

## What your QMS must still supply

[ List: e.g. management review records, CAPA process, supplier evaluation, complaint handling,
regulatory reporting — i.e. everything ISO 13485 requires that is not a software engineering
artifact. ]

## Justification records

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "ISO 13485:2016",
  "clause_ref": "ISO 13485:2016 §7.3 Design and development",
  "rationale": "[ ... ]",
  "evidence_refs": ["[ ... ]"]
}
```
