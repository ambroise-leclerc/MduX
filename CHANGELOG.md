# Changelog

Written for a reader deciding whether a version does what they need, and for an auditor asking what
changed between two artefacts they hold. Entries name the issue that owns the work, because the
issue carries the reasoning and this file carries the outcome.

## How to read the earlier entries

This file starts at 0.6.0. **The entries for 0.2.0 through 0.5.0 are reconstructed from
[`docs/roadmap.md`](docs/roadmap.md)'s own wave-by-wave record rather than derived from git history**,
and that distinction is worth one paragraph because a reader may try to check them.

Two of the four earlier tags — `v0.2.0` and `v0.5.0` — are not ancestors of `develop`, so
`git log v0.4.0..v0.5.0` does not describe what a reader would expect it to. What the roadmap
records is what each wave closed, and that is the account these entries summarise. Anything more
precise would need the release ritual written down, which
[`docs/roadmap.md`](docs/roadmap.md#when-v060-gets-cut) flags as the second thing to settle before
tagging 0.6.0.

---

## 0.6.0 — unreleased

Wave 5: the `.medui` compiler, end to end. An authored screen becomes a committed artifact, becomes
`constexpr` C++, and reaches compared pixels.

### The compiler (#15, all twelve children)

- **The boundary and the artifacts** — ADR-011 fixes what a compiled screen is and is not; ADR-012
  fixes the three files a screen emits and which are committed (#190, amended by #203 to keep the
  compiled screen locale-free).
- **Diagnostics** — the shared `MEDUI-E` registry, in the envelope every MduX tool emits (#191).
- **The front end** — lexer, parser, AST and a fixture corpus of real files (#192); semantic
  analysis over the closed component dictionary, field value domains, theme tokens and locale-checked
  strings (#193).
- **Bounded layout** — integer-only resolution to absolute rectangles, with `Row` flattening and a
  synthesised background node (#194).
- **Text budgets** — every resolved box measured against the *widest approved translation*, not the
  authoring one (#195).
- **Golden references** — one entry per `@safety_critical` node and per explicitly positioned node,
  written as a sidecar (#196).
- **The canonical package and two C++ emitters** — a compiled screen as canonical JSON, and as a
  `.cppm` and `.hpp` carrying `static_assert(screen.validate().has_value())`, so a malformed screen
  is a build error rather than a startup failure (#197).
- **`mdux-meduic`** — the compiler driver and `mdux_compile_screen()`, with the first committed
  screen under `generated/screen/endoscope-monitor/`, byte-compared on both toolchain legs (#198).
- **The governed runtime** — a compiled screen becomes draw commands with no allocation, no parsing
  and work bounded by numbers known before the device runs (#199).
- **`mdux-medui-check`** — validates one file without a build, and names the two checks a file on its
  own cannot cover (#200).
- **The first end-to-end screen** — an authored `.medui` file drawn through Vulkan into an offscreen
  target and compared pixel by pixel under lavapipe (#201).

### Enforcement that had been asserted but not run (#11)

- `mdux-governed-lint` over governed source, and `governed.noThrow.symbolScan` over the emitted
  objects. ADR-005 had asserted a lint that did not exist while `src/text/Raster.cpp` — governed,
  shipped in 0.5.0 — contained `try` and three `catch` clauses. The rasteriser moved to the
  host-tools zone, and ADR-004 and ADR-005 were rewritten to describe only mechanisms CI runs (#116).
- A stack-safe PR and post-merge policy (#117).

### Also

- Dual licensing under EUPL-1.2.
- A file-header lint, after ten files were found in a documentation order `CONTRIBUTING.md` had
  retired twice (#223).
- SpecLab pinned to its first tagged release.

### Known limits, stated because they are easy to mistake for defects

- **No text package is baked**, so a screen carrying `t("STR-KEY")` cannot be compiled end to end
  ([#235](https://github.com/ambroise-leclerc/MduX/issues/235)). The committed screen is text-free
  and its safety-critical node is a `NumericDisplay`, the one traceable component with no text key.
- **The runtime draws a `Panel`**; every other component is visited, counted in
  `FrameStats::deferred`, and left undrawn. Text needs the package above; live-data components have
  no geometry until a frame exists (#17).
- **The golden sidecar has a static consumer, not a rendered one.** A test cross-checks it against
  the compiled screen; verifying it against a frame is #16, over content #17 teaches to draw.

---

## 0.5.0 — 9 August 2026

Wave 4: the font and text pipeline (#14). Static text bakes to positioned glyph runs per locale;
dynamic text gets a restricted charset table, and the compiler rejects a format that could escape it.
Hand-parsed TrueType, an integer-only rasteriser with coverage antialiasing, an atlas packer, and the
first committed font package.

## 0.4.0 — 4 August 2026

Wave 3: documentation architecture rebuilt from what the build produces (#10); the shader pipeline
and renderer slice, where MduX draws its first pixel from library code (#13); and zero-SOUP ML
inference with a fail-closed golden self-test and no heap in `predict`, verified three ways (#18).

## 0.3.0 — 1 August 2026

Wave 2: a clause-accurate regulatory corpus across five standards with per-clause indexes and JSON
Schemas (#8); governance types, a SOUP register and both Software Development File trees (#9); and
the evidence kernel — SHA-256, canonical JSON, bake reports and `mdux_bake_artifact()`, with
committed artifacts re-derived and byte-compared on both CI legs (#12).

## 0.2.0 — 27 July 2026

Wave 1: copyright remediation, including a purge of reproduced normative text from git history (#7);
and the trust-zone skeleton — `MduXCore`, which never receives Vulkan's include directories, with
link-graph verification at configure time (#11).
