# ADR-010: No on-device text shaping

## Status
Accepted

## Context

MduX renders text by recording `DrawMode::CoverageR8` rectangles into a `DrawList`
(`include/mdux/draw/Draw.cppm`), sampled against a single R8 atlas the `UiRenderer`
already binds. The question this ADR settles is *what may produce that atlas and the
glyph runs that address it*.

Text rendering in the wider ecosystem is conventionally an online problem: a layout
engine resolves runs, a shaping engine (HarfBuzz, DirectWrite, CoreText) maps
code-point sequences to glyphs using GSUB/GPOS, a rasteriser produces coverage bitmaps,
and the compositor draws. Every one of those steps is a parser, an allocator, or both,
and each carries a non-trivial dependency. For a medical-device UI governed by
IEC 62304 Class B/C, that shape is wrong on three axes:

1. **SOUP.** HarfBuzz and FreeType are SOUP (Software of Unknown Provenance). Linking
   them into a device build adds them to the risk register and gives every deployed
   device a transitive dependence on their defect history. ADR-008 settled this
   question for ML inference; the same argument applies, more sharply, to text.
2. **Determinism.** Shaping output is not byte-stable across versions of the
   shaping library. Two devices baked against different HarfBuzz revisions can render
   the *same* string differently. The evidence pipeline (ADR-007) exists precisely so
   that an artifact is byte-identical on MSVC, glibc and libc++; online shaping makes
   that property unverifiable at the device.
3. **Predictability.** A medical-device screen names the content it presents and the
   tint it presents it in. The `.medui` compiler (epic #15) and the rendered-truth
   verifier (epic #16) require the screen's geometry to be a build-time constant so a
   machine-checked artifact can attest it. Online shaping can reflow a label past the
   bounds a safety-critical node declared, silently, at a time no reviewer is looking.

What *is* needed on-device is drawing glyphs at positions the baker already computed.
Latin/Cyrillic/Greek LTR text with no ligatures, no GPOS, no contextual forms and no
RTL/contextual behaviour — the entire v1 scope of epic #14 — admits this: the shaping
relationship is the identity, and the "run" is a sequence of code points each mapping
to one glyph baked at a known atlas slot with a known advance.

## Medical Device Considerations

- **IEC 62304 §5.1.2 (software architecture).** A device that does no parsing, layout
  or shaping at runtime has no parser, layout engine or shaping engine in its
  software unit decomposition. The architectural hazards those units introduce
  (buffer handling, state machines, table lookups) are absent rather than mitigated.
- **IEC 62304 §5.5 (software unit verification) and §5.7 (system testing).** A baked
  text package is a fixed input; unit and system tests assert against its bytes and
  the rendered pixel output, both of which are reproducible by an evidence test
  (ADR-007). Online shaping would make §5.7 a statistical claim.
- **ISO 14971.** Foreseeable hazards from misrendered safety-critical text — wrong
  dose, wrong patient, wrong alarm — are controlled at the build step rather than by
  runtime defences. The font baker's restricted-charset rejection (epic #14 child
  #161) is a risk control: a string the baker cannot render never reaches a device.
- **IEC 62366-1.** Usability engineering records the visible content of every screen.
  A baked text package makes "what the user sees" a committed, reviewable artifact
  rather than a function of the runtime's view of the font.
- **Traceability.** The earliest point at which a safety-critical text string can be
  rejected is at the baker (S5, #161). This ADR makes that rejection *architectural*
  rather than best-effort: unsupported scripts fail the baker, and a device that
  cannot present a string refuses to present it rather than presenting it shaped.

## Decision

**Text shaping stays offline. On-device text is positioned glyph runs from a baked
text package. The device runtime performs no parsing, no layout, and no shaping.**

Concretely:

1. A text baker (`mdux-textbake`, child #157) produces committed `generated/text/<id>/`
   packages of positioned glyph runs through `mdux_bake_artifact()` (ADR-007). A text
   package references one font package by id; it does not embed the atlas. See
   `mdux.text.schema`'s module comment for the two reasons (atlas reuse across screens,
   evidence-boundary separation between font and text artifacts).
2. A font baker (#160, S4) produces committed `generated/font/<id>/` packages containing
   the R8 coverage atlas, glyph advances, kerning pairs the baker chose to bake, and — via
   #161 (S5) — the restricted-charset table for dynamic text. The font baker is what
   rejects unsupported scripts; the text baker is what rejects a string the referenced
   font package cannot render.
3. A `.medui` screen (epic #15) records static text as fixed-position glyph runs
   against that package, authored or laid out at build time against the baked
   metrics. Dynamic text may reference only code points the restricted-charset table
   declares.
4. The runtime's text path is `DrawList` recording of `CoverageR8` rectangles sampled
   against the baked atlas. There is no on-device code that walks a font table, no
   on-device code that maps code points to glyphs, and no on-device code that
   advances a pen by a runtime-computed width.
5. Unsupported scripts (anything outside Latin/Cyrillic/Greek LTR in v1), composite
   glyph substitutions the baker did not pre-bake, CFF/CFF2 outlines, GPOS
   positioning, ligatures and hinting **fail the font baker (#160/#161)** with stable codes
   and therefore fail the build. They never reach a device. The restricted-charset table
   of #161 (S5) is what makes this enforceable rather than aspirational.
6. The canonical schema that describes both baked packages (`mdux.text.schema`,
   `mdux.font.schema`) is governed and imported by both the host baker and the
   device runtime — one definition, one set of compile flags (ADR-008 decision 1,
   applied to text). The text schema references a font-package id; the font schema
   holds the atlas, metrics and charset. They land in separate waves (#157 and #161
   respectively) and live in separate modules, but share the same host/device
   sharing doctrine.

The enforcement point is **compile-time in the build host**: `mdux-textbake` and the
`.medui` compiler reject any string, format or font that would require on-device
shaping. A device build that links a parser is a trust-zone violation (ADR-004).

## Alternatives Considered

### A. Online shaping against a restricted font (HarfBuzz/FreeType at runtime)
Rejected. Contradicts ADR-008 (SOUP), ADR-007 (device-side determinism), and the
architectural predictability epic #16 (rendered-truth verification) requires. The
"restricted font" claim is also weak: HarfBuzz will still reflow across versions even
on a limited character repertoire. Two implementations disagree eventually, and the
disagreement surfaces on a deployed device.

### B. Online shaping with the shaping library in `MduXCore` governed code
Rejected. Even setting SOUP aside, a shaping engine in governed code would be the
only governed module with an unbounded runtime memory profile (the GSUB/GPOS table
walk), which breaks the no-heap-at-runtime guarantee `MduXCore` advertises in ADR-005
and which epic #18 (#63) verifies three independent ways for ML. A parser in a
governed target is also exactly what `mdux_verify_trust_zones()` rejects at
configure time today; admitting one would require a new trust-zone carve-out and
would weaken the guarantee every *other* governed module offers.

### C. Bake per locale at install time, not at build time
Rejected. An install-time bake can run on a host whose toolchain differs from the
build host's and whose font assets differ from the committed ones. The evidence
pipeline's whole point (ADR-007) is that the committed artifacts are the ones
reviewed, byte-compared, and shipped; deferring the bake to install time moves the
artefact out from under the evidence contract. An install-time baker is also a
parser running on the *installation* host, which is a device host for MduX's
intended deployment model.

### D. Two shaping implementations (host baker + runtime reference)
Rejected. This is the conventional split ADR-008 decision 1 exists to refuse. Two
implementations drift; when they do, a device rendering disagreement has two possible
causes and localising it is guesswork. The canonical-schema-shared-by-host-and-runtime
pattern (ADR-008 decision 1, applied here as `mdux.text.schema` / `mdux.font.schema`)
is what makes a rendered mismatch *diagnostic*.

## Consequences

### Positive

- The device runtime contains zero text-shaping code. That is a measurable absence:
  no shaping parser in any governed module, no shaping dependency in the device link
  graph, and `mdux_verify_trust_zones()` continues to fail a build if one appears.
- A baked text package is byte-identical on MSVC and GCC (ADR-007). The text the
  reviewer signs off on is the text the device draws.
- Every safety-critical string is either present at the declared bounds or the build
  fails. The rendered-truth verifier (epic #16) can check localised text presence as
  a pure function over the baked atlas without a shaping engine.
- A font change is a re-bake and a commit, reviewed like any other diff. The
  deployed text corpus is auditable from `git log -- generated/font/ generated/text/`,
  the two artifact kinds tracked separately because a text package carries only the
  runs local to a screen and a font package carries the shared atlas.

### Negative

- **No complex scripts in v1.** Latin, Cyrillic and Greek LTR are the entire initial
  repertoire. Arabic, Devanagari, Han, Hangul, Thai and all complex-shaping scripts
  are out of scope until a later wave explicitly widens the baker. A device that
  needs them today gets a build error rather than a rendering.
- **No runtime substitution.** If a string contains a code point not in the
  restricted_charset table for the locale it is bound to, the build fails rather
  than the runtime falling back. This is deliberate and is the risk control, but it
  pushes the failure to a point in the workflow where a translator may be the first
  to see it.
- **No IME integration.** Epic #17 explicitly cuts IME from the component dictionary
  for the same trust-zone reason; this ADR is consistent with that cut. Input-method
  editing is a platform concern, not a governed-renderer concern.
- **Atlas size is a build-time dimension.** A larger supported repertoire costs more
  atlas memory on every device whether a given screen uses it or not. The packer
  (#160) is responsible for power-of-two sizing and for failing closed on over-budget
  glyph sets; there is no runtime growth path, by design.

### Risks

- **Static-text positioning drifts under locale.** Mitigated: the `.medui` compiler
  (epic #15, child #15-S5) re-runs layout per approved locale at build time and
  rejects a layout that escapes declared bounds. The baker, not the device, is the
  authority.
- **A future contributor links a shaping library "just for the example".** Mitigated
  by `mdux_verify_trust_zones()` (ADR-004); a parser in a governed target is a
  configure-time `FATAL_ERROR`. A parser in the host-tools zone is fine and is where
  the baker lives.
- **Bypass via `mdux.draw`'s `CoverageR8` path.** The runtime *can* technically
  record arbitrary coverage rectangles from arbitrary UVs; nothing in `DrawList`
  enforces that those UVs come from a baked atlas. This ADR is an architectural
  rule, not an allocation-site check; the `.medui` compiler and the host baker are
  the enforcement point. A future ADR may add a stronger mechanism if the risk
  materialises; for v1 the rule plus the host-side enforcement is the control.

## Implementation Notes

- This ADR lands with issue #157 (S1 — `mdux.text.schema` and `mdux-textbake`
  skeleton). The schema module's `@compliance` block cites this ADR alongside
  ADR-004/005/007, matching how ADR-008 is cited in `mdux.ml.schema`.
- The build-fail enforcement described in Decision paragraph 5 is delivered by the
  restricted-charset validation in #161 (S5); this ADR records the commitment it
  will enforce. The gap between #157 landing and #161 closing is an interval in
  which the rule is stated but not mechanically enforced — the same shape as the
  interval between ADR-008 being accepted and #18's children closing.
- A `.medui` developer-facing error for "code point not in the restricted charset"
  will use the shared `cli::Diagnostic` envelope (ADR-007, #118) with a `TXT` /
  `MED` code prefix and a `fixHint` pointing at the atlas recipe.

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) — trust zones; the mechanism that makes a
  governed parser a configure-time `FATAL_ERROR`.
- [ADR-005](ADR-005-error-handling-and-exceptions-policy.md) — error handling; the
  reason the governed runtime has no heap/parser failure paths to manage.
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md) — evidence pipeline; the reason
  the baker is the only producer of a text package.
- [ADR-008](ADR-008-zero-soup-ml-inference.md) — zero-SOUP ML; decision 1 (one shared
  schema/module for host and device) is the model this ADR mirrors for text.
- Epic [#14](https://github.com/ambroise-leclerc/MduX/issues/14) — Font & text
  pipeline; child issues #157–#162.
- Epic [#15](https://github.com/ambroise-leclerc/MduX/issues/15) — `.medui` compiler;
  the host-side enforcement point for dynamic text.
- Epic [#16](https://github.com/ambroise-leclerc/MduX/issues/16) — rendered-truth
  verification; the consumer of this ADR's predictability guarantee.

## Approval
- **Decision Date**: 2026-08-06
- **Approved By**: MduX maintainers
- **Review Date**: (not yet scheduled; review when a later wave proposes widening the
  script repertoire past Latin/Cyrillic/Greek LTR)