# Software Architecture Design (SAD)

> Template — IEC 62304 §5.3 (Software architectural design). Fill in every `[ ... ]` placeholder
> for your own device software. See
> `docs/iec62304/03-development-process.md`
> for the underlying clause-by-clause guidance and
> [`docs/governance/citation-convention.md`](../../../docs/governance/citation-convention.md) for
> the citation format used below. This template contains no MduX-specific content — see
> `software_development_file/regulatory/IEC_62304/SAD.md` (issue #38, a later PR in this stack) for
> MduX's own filled-in example.

## Document control

- **Product / software item:** [ ... ]
- **Version:** [ ... ]
- **Safety classification:** [ Class A | Class B | Class C ] — see `IEC 62304:2006 §4.3 Software safety classification`
- **Author(s):** [ ... ]
- **Date:** [ YYYY-MM-DD ]
- **Approval:** [ ... ]

## 1. Purpose and scope

[ What software item does this SAD describe? What's explicitly out of scope? ]

## 2. Software items and their decomposition

> `IEC 62304:2006 §5.3.1 Transform requirements into an architecture`

[ List the software items composing the system. If built on MduX, state which governed modules
(the `mdux.*` modules linked through `MduXCore`), adapter code (linked through `MduX`), and
host-only tools are used, and which are your own. ]

## 3. Interfaces between software items

> `IEC 62304:2006 §5.3.2 Develop an architecture for the interfaces of software items`

[ Describe each interface: what crosses it, in which direction, and any contract it must satisfy. ]

## 4. Segregation for risk control

> `IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control`

[ Which software items are segregated from which, and why? What verifies the segregation holds?
If built on MduX, note that `MduXCore` (the governed zone) is mechanically checked to reach no
Vulkan or windowing dependency by `mdux_verify_trust_zones()` (`cmake/MduXTrustZones.cmake`) at
every configure — a segregation enforced by tooling, not by convention alone. ]

## 5. SOUP identification

> `IEC 62304:2006 §5.3.4 Identify SOUP items`

[ List Software Of Unknown Provenance used by this software item, or reference your SOUP
register — see [`software_development_file/templates/IEC_62304/SOUP.md`](SOUP.md). If built on
MduX, MduX's own SOUP is recorded in
`docs/governance/soup-register.toml`; list only
what you add on top of it. ]

## 6. Architecture verification

> `IEC 62304:2006 §5.3.5 Verify the architectural design`

[ How was this architecture reviewed/verified? Reference review records, ADRs, or design review
minutes. ]

## Justification records

[ Optional: inline `Justification` objects (see
`docs/governance/schemas/justification.schema.json`)
tying specific architectural decisions above to their clause. Fenced as `jsonc` below, not `json`,
because this is an illustrative placeholder, not a binding Justification — `mdux-docs-lint` does
not validate a `jsonc` block. Fence your own filled-in Justification as `json` so it is checked. ]

```jsonc
{
  "justification_id": "JUS-NNN",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.3.3 Identify segregation necessary for risk control",
  "rationale": "[ ... ]",
  "evidence_refs": ["[ ... ]"]
}
```
