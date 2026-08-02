---
name: mdux-regulated-change
description: Use whenever a change can affect safety behavior, risk controls, compliance metadata, traceability, auditability, lifecycle documents, or claims about medical-device standards (IEC 62304, IEC 62366, ISO 14971, ISO 13485) in MduX.
---

# MduX regulated change handling

Companion to § 1 ("Project purpose and maturity") and § 8 ("Definition of done") of
[`AGENTS.md`](../../../AGENTS.md). This skill governs how to handle the *safety and regulatory*
dimension of a change; for the mechanics of building/testing see `mdux-build-and-test`, and for
module/Vulkan implementation conventions see `mdux-cpp23-vulkan-development`.

## Step 1 — Classify impact, with rationale

Before making the change, classify it as one of:

- **Not safety-relevant** — e.g. build tooling, documentation formatting, non-functional refactors
  that don't touch rendering correctness, compliance metadata, or resource lifetime.
- **Potentially safety-relevant** — touches code adjacent to safety behavior but the change itself
  doesn't alter behavior (e.g. adding a test, adding logging that doesn't change control flow).
- **Safety-relevant** — changes behavior in `mdux.draw` / `mdux.render.vulkan` rendering paths, the
  Vulkan SC device-lifetime object/memory managers (`mdux.vulkansc.*`), compliance-metadata
  handling (`ComplianceMetadata`, `Compliance`, validation logic), or anything that changes what
  the software does at runtime in a way a user or reviewer would need to know about.

Write down the classification and a one-sentence rationale as part of your change description —
don't leave it implicit.

## Step 2 — Identify affected artifacts

For potentially- or safety-relevant changes, identify:
- Affected requirements or design statements (check `docs/adr/` for the relevant ADR, and
  `README.md`'s stated capabilities — remembering that some of README's claims are aspirational,
  not implemented; see § 2 of `AGENTS.md`).
- Affected hazards/risk controls, if any (`software_development_file/templates/ISO_14971/Risk_Management_File.md`,
  `docs/iso14971/`, and the `Hazard`/`Requirement` records a
  `mdux::governance::ComplianceProgram` tracks, issue #34).
- Affected software items and interfaces (which module(s) from the table in `AGENTS.md` § 3).
- Verification evidence that will need to change (which tests in `tests/` cover this behavior
  today, and what new/updated test proves the change is correct).

## Step 3 — Make proportionate documentation updates

Match the scope of the documentation update to the scope of the code change — don't rewrite whole
regulatory frameworks for a small fix, and don't skip documentation for a real behavior change.
Relevant locations, by standard:
- ISO 14971 (risk management): `software_development_file/templates/ISO_14971/Risk_Management_File.md`,
  `docs/iso14971/`.
- ISO 13485 (quality management): `docs/iso13485/`.
- IEC 62304 (software lifecycle / safety classification): `docs/iec62304/`.
- IEC 62366-1 (usability): `docs/iec62366/`.
- IEC 81001-5-1 (health software security): `docs/iec81001/`.
- Architecture-level rationale: the relevant file in `docs/adr/` (create a new ADR if the change
  represents a new architectural decision; update an existing one if it revises a prior decision —
  see `docs/adr/README.md` for format and index maintenance).

## Step 4 — Maintain traceability

Where a requirement, hazard/risk control, or safety classification exists for the area you're
touching, make sure the chain from requirement → implementation → verification evidence (test)
stays intact and discoverable. If you're adding new safety-relevant behavior with no existing
requirement/risk-control entry, say so explicitly rather than inventing a corresponding entry after
the fact — flag it for maintainer/domain-expert review instead (see Step 5).

## Step 5 — Review and escalation

- **Safety-relevant** changes require maintainer or domain-expert review before being considered
  complete — do not merge or present such a change as done on your own assessment alone.
- If you are uncertain whether a change is safety-relevant, classify it as potentially
  safety-relevant and say so, rather than defaulting to "not safety-relevant" to avoid the
  overhead.
- Escalate (ask, rather than proceed) when: the correct risk control or requirement to update is
  ambiguous, the change would alter documented safety behavior without an existing test covering
  it, or you find a contradiction between code and regulatory documentation that you cannot
  resolve using the precedence order in `AGENTS.md` § 2.

## Strict distinction: implemented evidence vs. project intent vs. certification claims

- **Implemented evidence** = code that exists and tests that pass, right now, in this repository.
- **Project intent** = ADRs, README roadmap sections, and regulatory framework documents that
  describe where the project is headed or what a full implementation would look like.
- **Certification/regulatory claim** = an assertion that MduX (or a specific change) satisfies a
  named standard's requirements for real-world use.

Never let the second or third category be described using language that implies the first. In
particular, `README.md`'s "Completed" status for the ISO 14971/13485 risk and quality-management
frameworks is project intent, not implemented evidence (see `AGENTS.md` § 2) — do not cite it as
proof that a compliance requirement is already satisfied, and do not introduce new claims of that
kind for the change you are making.

## Handoff checklist

When you finish a change in this category, record:
- [ ] Impact classification (not / potentially / safety-relevant) and rationale.
- [ ] Documents changed (list files under `docs/`, ADRs, or top-level regulatory `.md` files).
- [ ] Verification performed (tests run/added, by name, and their result).
- [ ] Residual uncertainty (anything you weren't able to verify or resolve).
- [ ] Follow-up work needed (including any maintainer/domain-expert review still required).
