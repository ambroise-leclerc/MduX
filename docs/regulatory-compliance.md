# Regulatory compliance

## Purpose and scope

This document explains how MduX's engineering practices are designed to align with medical-device
software-development expectations, and how the artifacts it generates are meant to feed a
manufacturer's own technical file and notified-body audits.

**This document, and MduX itself, is not a regulatory clearance, not a Quality Management System,
and not a substitute for a manufacturer's own processes.** MduX is engineering scaffolding: an
experimental, proof-of-concept C++23-modules Vulkan/Vulkan SC UI library (`AGENTS.md` § 1) that a
manufacturer integrates into their own ISO 13485 QMS, their own ISO 14971 risk management file, and
their own engagement with a notified body. Nothing in this repository is itself a certified or
cleared medical device.

## 1. Standards covered, and why each is in scope

- **IEC 62304:2006** (software lifecycle) — the software-development-process backbone every other
  standard here assumes. MduX keeps **Class A in scope explicitly**, unlike its sibling project
  TrustSC, which models Class B/C only (`docs/governance/citation-convention.md`).
- **ISO 13485:2016** (quality management systems) — the design-and-development-control clauses
  (§7.3 in particular) that a library's engineering artifacts can feed into, even though MduX does
  not operate a QMS of its own.
- **ISO 14971:2019** (risk management) — the hazard/requirement/verification data model MduX's
  governance types are shaped around.
- **IEC 62366-1:2015** (usability engineering) — relevant because MduX renders a user interface;
  in scope for the same reason ISO 13485 is, not because MduX performs usability evaluation itself.
- **IEC 81001-5-1:2021** (health software security) — the lowest-confidence corpus in this project
  (`docs/iec81001/README.md`'s own caveat: MduX holds no copy of this standard's text and has
  verified clause-numbering confidence only through §4). Included because a manufacturer building
  a connected device on MduX will need to address it regardless of MduX's own confidence level.

## 2. Artifact mapping

### Governance types: real, tested, not yet populated for MduX itself

`mdux.governance` (issue #34) holds the types — `Justification`, `Requirement`,
`VerificationCase`, `Hazard`, `ProblemReport`, `AuditEvent`, and the `ComplianceProgram` that
aggregates them. `mdux.governance.compliance` (issue #35) holds two pure exports over that
program, `traceabilityMatrix()` and `releaseEvidenceSummary()`. Together they are working,
unit-tested code an application composes to record its own requirements, hazards, and
verifications in a structured, exportable form:

- `ComplianceProgram::validate()` refuses to pass unless every `Requirement` is discharged by at
  least one `VerificationCase`, and every `Hazard` names at least one controlling `Requirement` that
  actually exists.
- `traceabilityMatrix()` exports a requirement → verification → evidence row per requirement,
  including a row for an unverified requirement rather than omitting it — the question the export
  exists to answer is "what is *not* covered."
- `releaseEvidenceSummary()` exports the IEC 62304 §5.8 release view: validation status, open
  `ProblemReport`s, and the digest of every committed `generated/` artifact.

**State the gap plainly, because it is the one that matters most:** no `ComplianceProgram` has
actually been populated for MduX itself. The test suite's fixtures (`REQ-001`, `HAZ-001`, and
similar) are synthetic data proving the types work, not a real risk analysis for this project.
Unlike TrustSC, which composes these types in `examples/hello_world`/`examples/class_c_monitor`,
MduX has no example application doing the same yet.

### Evidence pipeline: the kernel exists, no baker uses it yet

Every future asset pipeline (fonts, images, shaders, ML weights — issues #13-18, all still open) is
meant to follow one shape (`docs/adr/ADR-007-evidence-pipeline-doctrine.md`): a host-only baker
compiles a reviewed source input into a committed `package.json`/`report.json` pair recording a
SHA-256 digest, tool version, and the exact options used; CI re-derives it and checks byte-identity.
`mdux.evidence.digest` (hand-written SHA-256), `mdux.evidence.json` (canonical, byte-stable JSON),
and `mdux.evidence.report` (the shared `BakeReport`/`PackageHeader` shape) are the kernel this
pattern is built on — real, tested, and already following the "no commit-SHA self-reference in a
committed artifact" rule the doctrine requires. **No baker exists yet.** This is a designed pattern
with a tested foundation, not evidence that a single asset has actually been baked and verified this
way.

### SOUP register: real, and short by design

`docs/governance/soup-register.toml` is a live, structured register
of every third-party and build-tool dependency (issue #36). Four entries at time of writing: the
Vulkan SDK (the only one actually deployed to a device), GLFW 3.4 (examples only), CMake, and the
MSVC/GCC/Clang C++23 floor. Each entry records `component_id`, supplier, license, `usage`,
`integration_path`, `pinned_by`, `runtime_deployment`, `support_model`, `boundary_rationale`, and
`risk_controls` as data, not prose scattered across a wiki. The register's own header states why
this list is expected to stay short as bakers land: the trust-zone split confines every entry to
adapter/examples/build, and zero-SOUP scoping decisions (a hand-written SHA-256 and canonical JSON
now; hand-parsed TrueType/safetensors/SPIR-V and QOI instead of PNG, planned for issues #13/#14/#17/#18)
keep third-party surface area from growing with each new baker.

### No zero-SOUP ML story yet

Unlike TrustSC, which has a shipped, zero-SOUP ML inference engine with a fail-closed
golden-vector self-test (`ADR-017` on that project), MduX has no ML capability at all — issue #18
(zero-SOUP ML inference) is open, not started. There is nothing to describe here yet beyond the
scoping intent already recorded in the SOUP register's header.

## 3. Technical file structure

[`software_development_file/`](../software_development_file/README.md) has a `templates/` tree
(issue #37) any manufacturer fills in, and a `regulatory/` tree (issue #38) with the same documents
filled in for MduX itself, citing real ADRs, real `mdux.governance` types, and — where
MduX genuinely has no populated example — saying so rather than inventing one:

```text
software_development_file/
├── templates/    # blank, fillable by any manufacturer
│   ├── IEC_62304/{SAD,SDD,SOUP}.md
│   ├── IEC_62366/Usability_Engineering_File.md
│   ├── IEC_81001/Cybersecurity_SAD.md
│   ├── ISO_13485/README.md
│   └── ISO_14971/Risk_Management_File.md
└── regulatory/   # the same tree, filled in for MduX
    └── ...
```

These documents still need a manufacturer's own content, review, and sign-off before they
constitute part of a real technical file — see the scope boundary below.

## 4. Design rationale trail

The [7 accepted ADRs](adr/README.md) (ADR-001 through ADR-007) are this project's design-history
record: why the governed/adapter/examples-and-tools trust-zone boundary exists (ADR-004), why error
handling returns `Result` rather than throwing (ADR-005), why no normative standard text is
reproduced in this repository (ADR-006), and why the evidence pipeline is shaped the way it is
(ADR-007). Collectively they are the kind of rationale trail a technical file's
design-and-development section draws on — read the index rather than this document re-deriving
each one.

## The trust-zone architecture narrows what needs deep review — precisely stated

`MduXCore` (governed: `std`-only, no Vulkan or windowing dependency) and `MduX` (adapter: publicly
links `MduXCore` plus `Vulkan::Vulkan`) are separated per
[ADR-004](adr/ADR-004-trust-zones-in-cpp.md), mechanically checked by `mdux_verify_trust_zones()`
(`cmake/MduXTrustZones.cmake`) at every CMake configure. **State exactly what that check is, not
more:** it walks a governed target's transitive link graph and fails the build if it finds a
dependency named `Vulkan::*`, `glfw`, or `glfw3` — a link-graph name check, run mechanically rather
than left to review. It is not a memory-safety guarantee: C++ has no `#![forbid(unsafe_code)]`
equivalent, and `MduXCore` being free of Vulkan/windowing dependencies says nothing about whether
its own code is memory-safe. The practical consequence for a reviewer is scoping, not exemption:
`MduXCore` is the small core most worth reviewing line-by-line; `MduX`, `examples/`, and `tools/`
are handled through the SOUP register and (once bakers exist) generated, byte-verified evidence.

## Governance types are scaffolding, not an operating QMS

`mdux.governance`'s types give an application a place to *record* requirements, hazards,
verifications, and problem reports in a structured, exportable form. They do not, by themselves,
constitute an operating quality system: nothing in this repository performs management review,
CAPA, supplier qualification, post-market surveillance, or any of the other ISO 13485 processes a
manufacturer's QMS is responsible for. A manufacturer populates and operates these types as part of
their own process — MduX supplies the data model and the export format, not the process itself, and
as stated in §2 above, has not yet populated them for itself either.

## 5. Scope limits

**MduX has a specific reason to lead with this rather than bury it:** root `README.md`'s
"Implementation Status" table currently marks Risk Management System, Quality Management System,
Software Lifecycle Framework, Design History File (DHF), and Risk Management File (RMF) as
**"Completed,"** and its usage section shows a `#include "mdux/compliance/MedicalDevice.cppm"` /
`mdux::MedicalDeviceContext` code sample. Neither that header nor that type exists anywhere in this
repository (verified by searching `include/` and `src/`) — `AGENTS.md` § 2 already flags README's
claims as partly aspirational. Correcting that table is tracked separately from this document; this
document is what states the real scope so the claim does not quietly come back.

> **MduX does not provide:** an operating ISO 13485 QMS (management review, CAPA, supplier
> qualification), a completed risk file, clinical evaluation, notified-body engagement, product
> clearance, tool qualification, or legal/regulatory advice.
>
> **MduX provides:** a trust-zone-separated library with a documented, mechanically-checked
> boundary; a designed (not yet exercised) byte-verified evidence-generation pattern; working
> requirement/hazard/verification/problem-report types with structured export, not yet populated
> for MduX's own release; a short, real SOUP register; and a design-rationale trail across 7
> accepted ADRs.

Throughout this document, wording is deliberately chosen to say "supports," "provides evidence
for," "is designed to align with," or "is intended to" — never "guarantees," "ensures," "is
compliant with," or "certified." Treat any stronger claim found elsewhere in this repository
(root `README.md`'s "Implementation Status" table being the known instance) as inaccurate, not as
a fact this document endorses by proximity.

## Where to go next

- [ADR index](adr/README.md) — all 7 accepted architecture decision records.
- `docs/governance/soup-register.toml` — the SOUP register itself (issue #36).
- `docs/governance/citation-convention.md` — the citation-key format and the `Justification`
  object shared by every regulatory document in this repository.
- [`software_development_file/README.md`](../software_development_file/README.md) — the templates
  and MduX's own filled-in software development file.
