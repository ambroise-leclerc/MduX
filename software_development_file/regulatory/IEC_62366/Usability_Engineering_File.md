# Usability Engineering File — MduX

> Filled-in example for MduX itself. See
> `software_development_file/templates/IEC_62366/Usability_Engineering_File.md` for the blank
> template, and `docs/iec62366/README.md` for the underlying clause-by-clause guidance.

## Document control

- **Product / software item:** MduX's UI layer — `mdux.draw` (`include/mdux/draw/Draw.cppm`, the
  governed description of a frame) and `mdux.render.vulkan` (`include/mdux/render/`, the adapter
  that renders one)
- **Revision note:** this file previously described an HTML/CSS path — `MedicalUiRenderer`,
  `MedicalUiConfig`, `MedicalUiContent`, `UiFileWatcher`. Issue #127 deleted it. That path never
  rendered anything: `MedicalUiRenderer::render()` recorded no Vulkan commands and nothing parsed
  the HTML, so the mechanisms this file used to cite were not mechanisms.
- **Scope note:** MduX provides UI *building blocks* — bounded, budgeted geometry rendered through
  Vulkan — not a finished device's usability engineering file. A manufacturer's actual use specification, evaluation results, and summative testing are
  theirs to conduct and document. This file states plainly what MduX's mechanisms can and cannot
  feed into that process.

## 1. Use specification

> `IEC 62366-1:2015 §5.1 General`

Not applicable to MduX as a library — intended use, patient population, and use environment are
application-specific and belong to the manufacturer's own use specification.

## 2. Application specification

> `IEC 62366-1:2015 §5.2 Establish application specification`

MduX renders an application-supplied `mdux::draw::DrawList` — rectangles in pixel coordinates,
each carrying a colour and a mode — through a fixed-budget Vulkan pipeline
(`mdux::render::UiRenderer`). This is the mechanism, not the application specification itself: the
application defines what its UI does, and for now defines it by calling `addSolidRect` directly.
Issue #15's `.medui` compiler is what will let a screen be *declared* rather than assembled.

## 3. Hazard-related use characteristics

> `IEC 62366-1:2015 §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations`

**No mechanism exists in MduX today linking a UI element to a hazard or a
`Requirement`/`Hazard` record** (`mdux.governance`, issue #34) the way, for example,
TrustSC's `@safety_critical` MedUI annotation does.

This gap did not narrow when the renderer became real. What `mdux.draw` validates is budget and
geometry — that a frame fits the storage it was given, that a rectangle has positive extent — and
what `evidence.shader.*` validates is that committed shader bytes are the reviewed ones. Both are
worth having and neither is a statement about use scenarios or hazards. A manufacturer identifying
hazard-related use scenarios for their own device does so entirely outside anything MduX currently
provides.

## 4. User interface specification

> `IEC 62366-1:2015 §5.4 Establish user interface specification`

Expressed in code: an application builds a `DrawList` over storage it owns, within a
`DrawBudget` fixed before the frame starts. `examples/SimpleMedicalUiExample.cpp` is a minimal
worked example, and needs neither a device nor a window to run.

There is no declarative UI specification format yet — issue #15. Until then a manufacturer's user
interface specification is a document about their own code, not something MduX can generate from
an artifact it holds.

## 5. User interface evaluation plan

> `IEC 62366-1:2015 §5.5 Establish user interface evaluation plan`

Not conducted by MduX itself. What MduX can now do that it could not before is show that a given
frame renders to the pixels it was asked for: `tests/render/PixelTests.cpp` renders offscreen and
compares every pixel against an expectation (issues #125, #126).

That is rendering correctness, not a usability evaluation. It answers "did the rectangle land
where the code said" and says nothing about whether a user can operate the resulting interface
safely — do not read "verification" of the former as evidence for the latter.

## 6. User interface design and implementation

> `IEC 62366-1:2015 §5.6 Perform user interface design and implementation`

Implemented in `mdux.draw` (governed: vertices, indices, draw commands, budget enforcement),
`mdux.render.vulkan` (adapter: shader modules, descriptor set layout, pipeline layout, pipeline,
the frame buffers and the record path) and the Vulkan SC adapter modules
`mdux.vulkansc.memory`/`mdux.vulkansc.objects`.

## 7. Formative evaluation

> `IEC 62366-1:2015 §5.7 Perform formative evaluation`

Not conducted by MduX itself — iterative usability evaluation with representative users is the
manufacturer's own activity, using their own HTML/CSS UI definitions as the artifact under test.

## 8. Summative evaluation

> `IEC 62366-1:2015 §5.9 Perform summative evaluation`

Not conducted by MduX itself, and not automated by anything in this repository — summative
evaluation for hazard-related use scenarios requires real representative users and is the
manufacturer's responsibility. See
`docs/iec62366/03-usability-engineering-process.md` for the clause's own note that §5.8 marks a
possible further design iteration rather than a separately numbered activity.

## Justification records

```json
{
  "justification_id": "JUS-018",
  "standard": "IEC 62366-1:2015",
  "clause_ref": "IEC 62366-1:2015 §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations",
  "rationale": "MduX has no mechanism today linking a rendered UI element to a Hazard or Requirement record. What mdux.draw validates is budget and geometry, and what the evidence pipeline validates is that committed shader bytes are the reviewed ones; neither is a statement about use scenarios or hazards - stated here so the gap is not silently assumed closed by the presence of verification machinery aimed at something else.",
  "evidence_refs": [
    "include/mdux/mdux.cppm",
    "src/mdux.cpp"
  ]
}
```
