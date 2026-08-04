# IEC 62366-1:2015 — per-clause index

One row per clause section in this corpus: the clause, a one-sentence pointer to what
MduX does or does not provide for it, and a deep link to the heading that covers it.
Generated from the headings, `Justification` objects and pointer comments already
present in the modules — not hand-transcribed, so it cannot drift from the prose it
indexes without the source changing too.

Regenerate after editing any file in this directory:

```
python3 tools/docs-lint/generate_ai_reference.py docs/iec62366
```

A clause shown as `—` is one this corpus deliberately does not number — see
[`README.md`](README.md). It is not a gap in the index.

| Clause | Covers | Pointer | Justification(s) |
|---|---|---|---|
| §1 | [Scope](01-scope-and-terms.md#1-scope) | MduX renders content described by a device's own UI design; it does not itself define what that device's users see or how they interact with it in a specific clinical context. | — |
| §2 | [Normative references](01-scope-and-terms.md#2-normative-references) | Read alongside docs/iso14971/, which covers the risk-management framework this standard's use-related risk analysis sits inside. | — |
| §3 | [Terms and definitions](01-scope-and-terms.md#3-terms-and-definitions) | MduX has no mechanism addressing use error directly yet; the closest adjacent idea is rendered-truth verification (issue #16), which confirms *what is displayed* matches the compiled screen's specification, a precondition for a user being able to act on correct information at all. | — |
| §4.1 | [General requirements](02-principles.md#41-general-requirements) | MduX provides no mechanism against this clause and cannot: applying a usability engineering process requires a device, its users and its use environment, none of which a rendering library has. | — |
| §4.1.1 | [Usability engineering process](02-principles.md#411-usability-engineering-process) | Device-level; MduX supplies no process, having no users of its own. | — |
| §4.1.2 | [Risk control as it relates to user interface design](02-principles.md#412-risk-control-as-it-relates-to-user-interface-design) | MduX's planned mechanisms attach to this sub-clause more directly than to any other in §4. | — |
| §4.1.3 | [Information for safety as it relates to usability](02-principles.md#413-information-for-safety-as-it-relates-to-usability) | No MduX mechanism, and the boundary is worth stating plainly: MduX renders whatever text a device gives it. | — |
| §4.2 | [Usability engineering file](02-principles.md#42-usability-engineering-file) | MduX supplies no usability engineering file. | — |
| §4.3 | [Tailoring of the usability engineering effort](02-principles.md#43-tailoring-of-the-usability-engineering-effort) | A manufacturer tailoring their effort may reasonably treat parts of a UI built on MduX differently from others; MduX neither makes nor records that judgement. | — |
| §5.1 | [Prepare use specification](03-usability-engineering-process.md#51-prepare-use-specification) | Entirely device-level information MduX has no visibility into. | — |
| §5.2 | [Identify user interface characteristics related to safety and potential use errors](03-usability-engineering-process.md#52-identify-user-interface-characteristics-related-to-safety-and-potential-use-errors) | The step MduX's planned mechanisms attach to - @safety_critical annotations, requirement binding, locale text budgets (issue #15) and rendered-truth verification (issue #16) - none of which exists yet. | — |
| §5.3 | [Identify known or foreseeable hazards and hazardous situations](03-usability-engineering-process.md#53-identify-known-or-foreseeable-hazards-and-hazardous-situations) | MduX has no mechanism: a hazard is a property of the device and its clinical context, not of a rendering library. | — |
| §5.4 | [Identify and describe hazard-related use scenarios](03-usability-engineering-process.md#54-identify-and-describe-hazard-related-use-scenarios) | No MduX mechanism, and none plausible: a use scenario is a description of what a person does, which a library cannot enumerate for a device it has never seen. | — |
| §5.5 | [Select the hazard-related use scenarios for summative evaluation](03-usability-engineering-process.md#55-select-the-hazard-related-use-scenarios-for-summative-evaluation) | No MduX mechanism. | — |
| §5.6 | [Establish user interface specification](03-usability-engineering-process.md#56-establish-user-interface-specification) | A .medui source file (issue #15) is what a device's UI specification would be authored against, but the specification itself stays the manufacturer's. | — |
| §5.7 | [Establish user interface evaluation plan](03-usability-engineering-process.md#57-establish-user-interface-evaluation-plan) | Device-level; no MduX mechanism, and none plausible: an evaluation plan is a statement about people and a use environment, neither of which a library has. | — |
| §5.8 | [Perform user interface design, implementation and formative evaluation](03-usability-engineering-process.md#58-perform-user-interface-design-implementation-and-formative-evaluation) | For a device built on MduX, implementing a user interface *is* authoring and compiling `.medui` screens (issue #15). | — |
| §5.9 | [Perform summative evaluation of the usability of the user interface](03-usability-engineering-process.md#59-perform-summative-evaluation-of-the-usability-of-the-user-interface) | Rendered-truth verification (issue #16) is the closest adjacent idea MduX has, and it is worth naming the gap precisely: it confirms that a compiled screen renders the state it was given. | — |
| §5.10 | [User interface of unknown provenance](03-usability-engineering-process.md#510-user-interface-of-unknown-provenance) | **This is the clause an integrator is most likely to apply to MduX itself**, and it was missing from this corpus entirely until the numbering was verified. | — |
