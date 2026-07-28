# Usability Engineering File — MduX

> Filled-in example for MduX itself. See
> `software_development_file/templates/IEC_62366/Usability_Engineering_File.md` for the blank
> template, and `docs/iec62366/README.md` for the underlying clause-by-clause guidance.

## Document control

- **Product / software item:** MduX's UI layer (`MedicalUiRenderer`, `MedicalUiConfig`,
  `MedicalUiContent`, `UiFileWatcher` — `include/mdux/mdux.cppm`, `src/mdux.cpp`)
- **Scope note:** MduX provides UI *building blocks* — an HTML/CSS-defined interface rendered
  through Vulkan, with basic structural validation — not a finished device's usability engineering
  file. A manufacturer's actual use specification, evaluation results, and summative testing are
  theirs to conduct and document. This file states plainly what MduX's mechanisms can and cannot
  feed into that process.

## 1. Use specification

> `IEC 62366-1:2015 §5.1 General`

Not applicable to MduX as a library — intended use, patient population, and use environment are
application-specific and belong to the manufacturer's own use specification.

## 2. Application specification

> `IEC 62366-1:2015 §5.2 Establish application specification`

MduX renders an application-supplied `MedicalUiContent` (HTML + CSS strings, loaded from a file via
`UiFileWatcher::loadContent()`) through a Vulkan pipeline (`MedicalUiRenderer`). This is the
mechanism, not the application specification itself — the application defines what its UI does.

## 3. Hazard-related use characteristics

> `IEC 62366-1:2015 §5.3 Establish user interface characteristics related to safety, hazards and hazardous situations`

**No mechanism exists in MduX today linking a UI element to a hazard or a
`Requirement`/`Hazard` record** (`mdux.governance.compliance`, issue #35) the way, for example,
TrustSC's `@safety_critical` MedUI annotation does. `MedicalUiContent.validationErrors` and
`RenderStatistics.validationErrors` exist, but check structural completeness — does the file exist,
does it contain HTML/CSS content, is `ComplianceMetadata` non-empty (`src/mdux.cpp` lines around
120-155, 380-395) — not hazard-related use-scenario coverage. A manufacturer identifying
hazard-related use scenarios for their own device does so entirely outside anything MduX currently
provides.

## 4. User interface specification

> `IEC 62366-1:2015 §5.4 Establish user interface specification`

Authored as plain HTML + CSS strings (`MedicalUiConfig.uiDefinitionPath`), loaded and validated for
structural completeness by `UiFileWatcher::loadContent()`. `examples/SimpleMedicalUiExample.cpp` is
a minimal worked example.

## 5. User interface evaluation plan

> `IEC 62366-1:2015 §5.5 Establish user interface evaluation plan`

Not conducted by MduX itself. `MedicalUiConfig.enableValidation` gates a structural
completeness check, not a usability evaluation — do not read "validation" in MduX's own source
comments as a synonym for this clause's "evaluation" activity; they are different things that
happen to share an English word.

## 6. User interface design and implementation

> `IEC 62366-1:2015 §5.6 Perform user interface design and implementation`

Implemented in `MedicalUiRenderer` (Vulkan pipeline: descriptor sets, pipeline layout, the render
path itself) and the Vulkan SC adapter modules `mdux.vulkansc.memory`/`mdux.vulkansc.objects`.

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
  "rationale": "MduX has no mechanism today linking a rendered UI element to a Hazard or Requirement record; MedicalUiContent.validationErrors and RenderStatistics.validationErrors check structural completeness (file exists, has HTML/CSS content, compliance metadata is non-empty), not hazard-related use-scenario coverage - stated here so this gap is not silently assumed closed by the presence of a field with a similar name.",
  "evidence_refs": [
    "include/mdux/mdux.cppm",
    "src/mdux.cpp"
  ]
}
```
