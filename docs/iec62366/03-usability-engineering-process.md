# IEC 62366-1:2015 §5 — Usability engineering process

§5 is the standard's process clause: the sequence of activities by which a manufacturer specifies,
designs, evaluates and finally confirms a user interface against use-related risk.

**This file asserts no sub-clause numbers.** An earlier version numbered each step below, from 5.1 to 5.9. Those numbers were written from professional familiarity with the process rather
than from the standard, which this project does not hold a copy of (see
[ADR-006](../adr/ADR-006-no-reproduction-of-normative-standard-text.md)), and the citation
convention requires a clause number to be confirmed before it is cited. They are therefore removed
rather than caveated: a number carrying a warning label is still a citation, and a reader who
copies it into a design history file has cited something nobody checked. The named steps below are
the part this corpus does assert; the only citation key it uses is `IEC 62366-1:2015 §5 Usability
engineering process`, at the level [`README.md`](README.md) explains is confirmed.

A maintainer with access to IEC 62366-1:2015 can restore the sub-clause numbering by checking each
heading against the standard's §5 and adding the number back. That is a small, bounded task; it is
tracked on issue #30 and is the only thing standing between this file and a full clause-accurate
index entry.

## Establish the application specification

Documenting the device's intended medical indication, intended patient population, intended part of
the body or tissue interacted with, intended use environment, and intended operator profile.

Entirely device-level information MduX has no visibility into. A UI library is instantiated by a
device; it does not know the patient population it will be read by.

## Establish user interface characteristics related to safety
<!-- pointer: The step MduX's planned mechanisms attach to - @safety_critical annotations, requirement binding, locale text budgets (issue #15) and rendered-truth verification (issue #16) - none of which exists yet. -->

Identifying which UI characteristics could contribute to a use error with safety consequences —
frequently-used functions, and functions whose incorrect use could cause harm.

This is the step where MduX's planned mechanisms attach most directly, and it is worth being
precise about which ones and what they would and would not establish:

| Planned mechanism | Issue | What it would establish | What it would not |
|---|---|---|---|
| `@safety_critical` annotation on a `.medui` element | #15 | That an element the author considers safety-relevant is *marked*, so every other mechanism below can be scoped to it | Whether the author marked the right elements — that judgement is the device's |
| `requirement:` binding on an annotated element | #15 | That every safety-critical element names the requirement it exists to satisfy, checked at compile time rather than in review | That the requirement is the correct one, or that it is met |
| Text-budget validation against every approved locale | #15 | That an annotated field cannot overflow or truncate in any locale the device ships, in any build — a use-error mode (a value read as `12` when it is `120`) removed by construction | Anything about legibility, contrast, or comprehension |
| Rendered-truth verification | #16 | That the compiled screen renders the values it was given — the pixels match the model | That the values themselves are correct, or that a clinician reads them correctly |

None of these exist today: the `.medui` compiler has not been built. This corpus states the shape
they would take rather than manufacturing a `Justification` for a mechanism that does not exist.
The distinction matters more here than in most of this repository — a usability engineering file
citing a control that is not implemented is exactly the failure mode this standard's evaluation
steps exist to catch.

## Establish the user interface specification
<!-- pointer: A .medui source file (issue #15) is what a device's UI specification would be authored against, but the specification itself stays the manufacturer's. -->

Specifying the UI's design inputs: layout, information to be conveyed, and interaction requirements
that follow from the application specification and the identified hazards.

Device-level. Once issue #15 lands, a `.medui` source file is the artifact a device's UI
specification is *authored against* and, in part, expressed in — but the specification itself,
including what a screen must convey and why, remains the device manufacturer's.

## Establish the user interface evaluation plan

Planning what will be evaluated — formatively and summatively — against what criteria and by what
method. Device-level; no MduX mechanism, and none plausible: an evaluation plan is a statement
about people and a use environment, neither of which a library has.

## Perform user interface design and implementation

For a device built on MduX, implementing a user interface *is* authoring and compiling `.medui`
screens (issue #15). This is where the mechanisms tabulated above would run, at build time, on
every change.

Today there is no `.medui` compiler, so there is no implementation mechanism to cite.

## Perform formative evaluation

Iteratively evaluating the UI during design, to find use-related problems before the design is
final.

Device-level and dependent on a device's actual users; no MduX mechanism, and none plausible even
once `.medui` exists. This step needs real people interacting with a real device in a realistic
environment. A compiler cannot substitute for that, and a corpus that implied otherwise would be
describing a control the device does not have.

## Iterate the design as evaluation results require

Repeating specification, design, implementation and formative evaluation as the results warrant.
No MduX mechanism, for the same reasons as the steps it iterates.

## Perform summative evaluation

Confirming, on the final design, that residual use-related risk is acceptable — the evidence a
usability engineering file ultimately needs to support a device's release.

Device-level. Rendered-truth verification (issue #16) is the closest adjacent idea MduX has, and it
is worth naming the gap precisely: it confirms that a compiled screen renders the state it was
given. Summative evaluation confirms that a clinician, under realistic conditions, reads that
screen and acts correctly. The first is a property of the software; the second is a property of the
software *and* its users, and only the second is what this step asks for.
