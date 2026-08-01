# Usability Engineering File

> Template — IEC 62366-1:2015+AMD1:2020. Fill in every `[ ... ]` placeholder. See
> `docs/iec62366/README.md` for the underlying clause-by-clause
> guidance. See
> `software_development_file/regulatory/IEC_62366/Usability_Engineering_File.md` (issue #38, a
> later PR in this stack) for MduX's own filled-in example.

## Document control

- **Product / software item:** [ ... ]
- **Version:** [ ... ]
- **Author(s):** [ ... ]
- **Date:** [ YYYY-MM-DD ]
- **Approval:** [ ... ]

## 1. Use specification

> `IEC 62366-1:2015 §5.1 General`

[ Intended medical indication, patient population, intended user profile(s), intended use
environment, operating principle. ]

## 2. Application specification

> `IEC 62366-1:2015 §5.2 Establish application specification`

[ What the device/software application does, in enough detail to identify use scenarios from. ]

## 3. Hazard-related use characteristics

> `IEC 62366-1:2015 §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations`

[ List of use scenarios, marking which are hazard-related. Cross-reference the `Hazard` records a
`mdux::governance::ComplianceProgram` (issue #34) tracks where a use scenario maps onto
one. ]

## 4. User interface specification

> `IEC 62366-1:2015 §5.4 Establish user interface specification`

[ How the UI is specified — reference screen designs, DSL source files, or a design system if one
is used. ]

## 5. User interface evaluation plan

> `IEC 62366-1:2015 §5.5 Establish user interface evaluation plan`

[ Plan for formative and summative evaluation — methods, participants, acceptance criteria. ]

## 6. User interface design and implementation

> `IEC 62366-1:2015 §5.6 Perform user interface design and implementation`

[ Reference to the actual implementation. ]

## 7. Formative evaluation

> `IEC 62366-1:2015 §5.7 Perform formative evaluation`

[ Results of formative (iterative, developmental) evaluation activities. ]

## 8. Summative evaluation

> `IEC 62366-1:2015 §5.9 Perform summative evaluation`

[ Results of summative (validation) evaluation — required for hazard-related use scenarios. Note
the standard has no separately numbered §5.8 clause of its own content beyond marking a possible
further design iteration; see
`docs/iec62366/03-usability-engineering-process.md`. ]

## Justification records

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "IEC 62366-1:2015",
  "clause_ref": "IEC 62366-1:2015 §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations",
  "rationale": "[ ... ]",
  "evidence_refs": ["[ ... ]"]
}
```
