# Cybersecurity Software Architecture Design — MduX

> Filled-in example for MduX itself. See
> `software_development_file/templates/IEC_81001/Cybersecurity_SAD.md` for the blank template, and
> `docs/iec81001/README.md` — note that document's caveat about clause-numbering uncertainty for
> this newer standard, which this filled document follows: practices are named by category, not by
> an invented numbered sub-clause.

## Document control

- **Product / software item:** MduX
- **Scope note:** MduX has no network stack anywhere in the codebase — no module performs network
  I/O. Several activity groups below are therefore stated as not-yet-applicable rather than
  described, and a manufacturer adding connectivity on top of MduX takes on that scope entirely
  themselves.

## 1. Scope and relationship to the IEC 62304 lifecycle

> `IEC 81001-5-1:2021 §4 General requirements`

Security risk management runs alongside, not instead of, the safety risk management described in
`software_development_file/regulatory/ISO_14971/Risk_Management_File.md` — a security issue in
`MduXCore` or `MduX` could also be a safety hazard if it compromises rendering behavior an
application relies on.

## 2. Security risk management

> IEC 81001-5-1:2021, security risk management practice category — see
> `docs/iec81001/02-security-lifecycle-practices.md`.

The trust-zone boundary separating `MduXCore` (governed), `MduX` (adapter), and `tools/`/`examples/`
(never shipped) — see `software_development_file/regulatory/IEC_62304/SAD.md` §2-4 — is also MduX's
primary security control: it confines Vulkan/native-handle-reaching code to a single, reviewable
adapter target rather than spreading it across the whole codebase.
`docs/governance/soup-register.toml` is this
project's dependency-provenance record: every SOUP/build-tool entry's supplier, repository, and
pin location are recorded, a starting point for a manufacturer's own dependency vulnerability
scanning.

## 3. Secure by design and secure implementation

> IEC 81001-5-1:2021, secure-by-design and secure-implementation practice categories.

**State this precisely, not more than it is:** unlike languages with a compiler-enforced memory-safe
subset, MduX has no equivalent guarantee — `mdux_verify_trust_zones()` (`cmake/MduXTrustZones.cmake`)
checks link-graph dependency names (no Vulkan/windowing reaching `MduXCore`), not memory safety of
the code inside either zone. The byte-verified evidence pattern
(`docs/adr/ADR-007-evidence-pipeline-doctrine.md`) is a build-integrity control still in its early
stages: `mdux.evidence.report`'s `BakeReport`/`PackageHeader` shape exists and is tested, but no
baker (issues #13-18) has shipped yet to actually produce a `report.json` a CI `verify` step would
re-derive and compare — the control is designed, not yet exercised end-to-end.

## 4. Security verification and validation

> IEC 81001-5-1:2021, security verification and validation practice category.

The test suites (`core_tests`, `evidence_tests`) run on every push (`.github/workflows/ci.yml`); no
dedicated fuzzing, dependency-vulnerability scanning, or penetration testing is currently part of
this project's own CI — a gap a manufacturer should close in their own security verification plan
rather than assume is covered.

## 5. Vulnerability and defect management

> IEC 81001-5-1:2021, vulnerability and defect management practice category.

`ProblemReport` (`mdux.governance.compliance`, issue #35) is where a discovered vulnerability would
be recorded once triaged — as with the ISO 14971 risk file, this project provides the record type,
not a populated instance for MduX itself, and not the intake process.

## 6. Security update management

> IEC 81001-5-1:2021, security update management practice category.

Not applicable in the current architecture: MduX has no runtime network connectivity, no update
mechanism, and no fielded-device communication path. A manufacturer who adds any of these on top of
MduX owns the entire security-update-management activity group themselves.

## 7. Security documentation and guidance

> IEC 81001-5-1:2021, security documentation and guidance practice category.

Not applicable for the same reason as §6 — MduX ships no operator-facing security guidance because
it has no network-facing or credential-handling surface to guide users on.

## Justification records

```jsonc
{
  "justification_id": "JUS-019",
  "standard": "IEC 81001-5-1:2021",
  "clause_ref": "IEC 81001-5-1:2021 §4 General requirements",
  "rationale": "The governed/adapter/examples-and-tools trust-zone split confines Vulkan and windowing dependencies to a single, narrow adapter target, mechanically checked at configure time by mdux_verify_trust_zones() - a link-graph control, explicitly not a memory-safety guarantee, stated as such here rather than implied to be more than it is.",
  "evidence_refs": [
    "cmake/MduXTrustZones.cmake",
    "docs/adr/ADR-004-trust-zones-in-cpp.md",
    "docs/governance/soup-register.toml"
  ]
}
```
