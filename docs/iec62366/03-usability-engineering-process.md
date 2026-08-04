# IEC 62366-1:2015 §5 — Usability engineering process

§5 is the standard's process clause: the sequence of activities by which a manufacturer specifies,
designs, evaluates and finally confirms a user interface against use-related risk.

**Sub-clause numbering is now verified.** An earlier version of this file asserted no sub-clause
numbers, because the ones it had were written from professional familiarity rather than from the
standard. They have been checked against an authorized copy of IEC 62366-1:2015 Edition 1.0
(2015-02) and restored — see [`README.md`](README.md) for the review record.

That check corrected three things, which is why the numbers were worth withholding until someone
could look:

- §5.1 is **Prepare use specification**. The earlier heading said "application specification",
  which is the term used by the superseded IEC 62366:2007 — the 2015 edition renamed it.
- Three steps were **missing entirely**: §5.3, §5.4 and §5.5, the hazard-identification and
  scenario-selection steps that connect this process to ISO 14971. A process description that
  jumps from UI characteristics to UI specification omits how hazards get into it at all.
- "Iterate the design as evaluation results require" was listed as a step. It is not a sub-clause;
  iteration is a property of §5.8, which combines design, implementation and formative evaluation.

## §5.1 Prepare use specification

Documenting the device's intended medical indication, intended patient population, intended part of
the body or tissue interacted with, intended use environment, and intended operator profile.

Entirely device-level information MduX has no visibility into. A UI library is instantiated by a
device; it does not know the patient population it will be read by.

## §5.2 Identify user interface characteristics related to safety and potential use errors
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

## §5.3 Identify known or foreseeable hazards and hazardous situations

Identifying the hazards and hazardous situations that the identified UI characteristics could
contribute to.

Device-level, and the point at which this process joins the device's ISO 14971 risk management
file. MduX has no mechanism: a hazard is a property of the device and its clinical context, not of
a rendering library.

The `hazard_id` field in
[`schemas/usability-engineering-record.schema.json`](schemas/usability-engineering-record.schema.json)
uses the same shape as `mdux::governance::Hazard::id` so that a record written here *joins* to a
device's risk management file — it does not populate one.

## §5.4 Identify and describe hazard-related use scenarios

Describing the specific sequences of user interaction through which a hazardous situation could
arise.

Device-level. No MduX mechanism, and none plausible: a use scenario is a description of what a
person does, which a library cannot enumerate for a device it has never seen.

## §5.5 Select the hazard-related use scenarios for summative evaluation

Choosing, from the scenarios identified in §5.4, the subset whose correct handling must be
confirmed on the final design.

Device-level, and a judgement call the standard deliberately leaves with the manufacturer. No MduX
mechanism.

## §5.6 Establish user interface specification
<!-- pointer: A .medui source file (issue #15) is what a device's UI specification would be authored against, but the specification itself stays the manufacturer's. -->

Specifying the UI's design inputs: layout, information to be conveyed, and interaction requirements
that follow from the use specification and the identified hazard-related use scenarios.

Device-level. Once issue #15 lands, a `.medui` source file is the artifact a device's UI
specification is *authored against* and, in part, expressed in — but the specification itself,
including what a screen must convey and why, remains the device manufacturer's.

## §5.7 Establish user interface evaluation plan

Planning what will be evaluated — formatively and summatively — against what criteria and by what
method.

Device-level; no MduX mechanism, and none plausible: an evaluation plan is a statement about people
and a use environment, neither of which a library has.

The standard divides this clause into general planning, formative evaluation planning and
summative evaluation planning — separating the last two because they answer different questions,
one improving a design in progress and the other producing evidence about a finished one. The
sub-clause numbering was verified alongside the rest, but the divisions get no index rows of their
own: MduX has nothing to offer against any of them, and a row pointing at a heading with nothing
behind it is what issue #32 exists to prevent.

## §5.8 Perform user interface design, implementation and formative evaluation

The standard combines these three into one clause, and the combination is the point — design,
implementation and formative evaluation iterate together until the design is ready for summative
evaluation, rather than running as separate sequential phases.

For a device built on MduX, implementing a user interface *is* authoring and compiling `.medui`
screens (issue #15). This is where the mechanisms tabulated under §5.2 would run, at build time, on
every change.

Formative evaluation itself is device-level and dependent on a device's actual users; there is no
plausible MduX mechanism even once `.medui` exists. This step needs real people interacting with a
real device in a realistic environment. A compiler cannot substitute for that, and a corpus that
implied otherwise would be describing a control the device does not have.

Today there is no `.medui` compiler, so there is no implementation mechanism to cite either.

## §5.9 Perform summative evaluation of the usability of the user interface

Confirming, on the final design, that residual use-related risk is acceptable — the evidence a
usability engineering file ultimately needs to support a device's release.

Device-level. Rendered-truth verification (issue #16) is the closest adjacent idea MduX has, and it
is worth naming the gap precisely: it confirms that a compiled screen renders the state it was
given. Summative evaluation confirms that a clinician, under realistic conditions, reads that
screen and acts correctly. The first is a property of the software; the second is a property of the
software *and* its users, and only the second is what this step asks for.

## §5.10 User interface of unknown provenance

The clause covering a user interface, or part of one, that was not developed under this standard's
process — evaluated under the normative Annex C rather than through §5.1 to §5.9.

**This is the clause an integrator is most likely to apply to MduX itself**, and it was missing
from this corpus entirely until the numbering was verified.

MduX is third-party software a device manufacturer did not develop under their own usability
engineering process. Whether that makes a screen built on it a UOUP is the integrator's
determination, not this project's — but the honest position is that MduX supplies no usability
engineering file, no formative or summative evaluation, and no use specification, because it has
no users of its own. What it does supply, and what an integrator can point at, is the evidence
described in [ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md): committed, byte-verified
artifacts with recorded provenance.

That is evidence about *what the software is*, not about how it was evaluated with users. The
second is the manufacturer's to produce.
