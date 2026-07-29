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
| §4 | [General requirements for the application of usability engineering to medical devices](02-general-requirements.md#iec-62366-12015-4-general-requirements-for-the-application-of-usability-engineering-to-medical-devices) | MduX has no device-level usability engineering process to establish, for the same reason it has no device-level risk analysis (see [`../iso14971/`](../iso14971/)): it is a UI SDK, not the device. | — |
| §5 | [Establish the application specification](03-usability-engineering-process.md#establish-the-application-specification) | Entirely device-level information MduX has no visibility into. | — |
| §5 | [Establish user interface characteristics related to safety](03-usability-engineering-process.md#establish-user-interface-characteristics-related-to-safety) | The step MduX's planned mechanisms attach to - @safety_critical annotations, requirement binding, locale text budgets (issue #15) and rendered-truth verification (issue #16) - none of which exists yet. | — |
| §5 | [Establish the user interface specification](03-usability-engineering-process.md#establish-the-user-interface-specification) | A .medui source file (issue #15) is what a device's UI specification would be authored against, but the specification itself stays the manufacturer's. | — |
| §5 | [Establish the user interface evaluation plan](03-usability-engineering-process.md#establish-the-user-interface-evaluation-plan) | Device-level; no MduX mechanism, and none plausible: an evaluation plan is a statement about people and a use environment, neither of which a library has. | — |
| §5 | [Perform user interface design and implementation](03-usability-engineering-process.md#perform-user-interface-design-and-implementation) | For a device built on MduX, implementing a user interface *is* authoring and compiling `.medui` screens (issue #15). | — |
| §5 | [Perform formative evaluation](03-usability-engineering-process.md#perform-formative-evaluation) | Device-level and dependent on a device's actual users; no MduX mechanism, and none plausible even once `.medui` exists. | — |
| §5 | [Iterate the design as evaluation results require](03-usability-engineering-process.md#iterate-the-design-as-evaluation-results-require) | No MduX mechanism, for the same reasons as the steps it iterates. | — |
| §5 | [Perform summative evaluation](03-usability-engineering-process.md#perform-summative-evaluation) | Rendered-truth verification (issue #16) is the closest adjacent idea MduX has, and it is worth naming the gap precisely: it confirms that a compiled screen renders the state it was given. | — |
