# Software Design Description (SDD)

> Template — IEC 62304 §5.4 (Software detailed design). Fill in every `[ ... ]` placeholder. See
> `docs/iec62304/03-development-process.md` for
> the underlying clause-by-clause guidance. See
> `software_development_file/regulatory/IEC_62304/SDD.md` (issue #38, a later PR in this stack) for
> MduX's own filled-in example.

## Document control

- **Software item(s) covered:** [ ... ]
- **Version:** [ ... ]
- **Author(s):** [ ... ]
- **Date:** [ YYYY-MM-DD ]
- **Approval:** [ ... ]

## 1. Purpose and scope

[ Which software item(s)/units from the SAD does this SDD detail? ]

## 2. Detailed design per software unit

> `IEC 62304:2006 §5.4.1 Refine the software architecture into a detailed design`

For each software unit:

### Unit: [ name ]
- **Responsibility:** [ ... ]
- **Internal structure:** [ modules, types, key algorithms ]
- **Dependencies:** [ other units, SOUP ]

## 3. Interface detailed design

> `IEC 62304:2006 §5.4.2 Develop a detailed design for interfaces`

[ Public function signatures, data formats, error conditions for each interface identified in the
SAD. If a unit is a `mdux.*` C++ module, its exported interface is the module's `.cppm` file
itself — cite it directly rather than re-transcribing its signatures here. ]

## 4. Detailed design verification

> `IEC 62304:2006 §5.4.3 Verify the detailed design`

[ How was this detailed design verified — reviews, unit test coverage, static analysis? Reference
the `Requirement`/`VerificationCase` records a
`mdux.governance.compliance::ComplianceProgram` (issue #35) tracks, rather than restating coverage
numbers that live there. ]

## Justification records

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.4.1 Refine the software architecture into a detailed design",
  "rationale": "[ ... ]",
  "evidence_refs": ["[ ... ]"]
}
```
