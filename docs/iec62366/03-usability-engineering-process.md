# IEC 62366-1:2015 §5 — Usability engineering process

See the confidence note in [`README.md`](README.md): the named steps below are described with
higher confidence than their exact `§5.x` numbering, which should be checked against the actual
standard before being cited elsewhere.

## §5.1 General

Establishing that the usability engineering process runs across the device's life cycle, iterating
as the design evolves. Device-level; no MduX mechanism.

## §5.2 Establish application specification

Documenting the device's intended medical indication, intended patient population, intended part
of the body or tissue interacted with, intended use environment, and intended operator profile.
Entirely device-level information MduX has no visibility into.

## §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations

Identifying which UI characteristics could contribute to a use error with safety consequences —
frequently-used functions, and functions whose incorrect use could cause harm. This is the sub-clause
where a future MduX mechanism is most plausible: once the `.medui` compiler (issue #15) and content
budget validation exist, a compile-time-enforced limit on, say, how much text a safety-critical
field may contain would be a concrete answer to *one* characteristic this sub-clause asks about.
No such mechanism exists today — this corpus states the future shape rather than a present
Justification.

## §5.4 Establish user interface specification

Specifying the UI's design inputs: layout, information to be conveyed, and interaction requirements
that follow from the application specification and identified hazards. Device-level; MduX provides
the compiler and runtime a device's UI specification would be authored against, once built, not the
specification itself.

## §5.5 Establish user interface evaluation plan

Planning what will be evaluated (formative and summative), against what criteria, and by what
method. Device-level; no MduX mechanism.

## §5.6 Perform user interface design and implementation

For MduX, "implementation" of a device's UI *is* authoring and compiling a `.medui` screen (issue
#15) — the point at which this corpus's future mechanisms would most directly apply. Today, there
is no `.medui` compiler yet, so there is no implementation mechanism to cite.

## §5.7 Perform formative evaluation

Iteratively evaluating the UI during design to catch use-related problems before they reach a final
design. Device-level, and dependent on a device's actual users; no MduX mechanism, and none
plausible even once `.medui` exists — this step needs real user interaction, not a compiled
artifact.

## §5.8 (further design iteration, as evaluation results require)

Iterating §5.4 through §5.7 as formative evaluation results warrant. No MduX mechanism, for the
same reasons as the steps it iterates.

## §5.9 Perform summative evaluation

Confirming, on the final design, that residual use-related risk is acceptable — the evidence a
usability engineering file ultimately needs to support a device's release. Device-level; no MduX
mechanism, and the rendered-truth verification epic (issue #16) is the closest MduX comes to an
adjacent idea — confirming a compiled screen renders as specified — without being a substitute for
evaluating actual use by real users.
