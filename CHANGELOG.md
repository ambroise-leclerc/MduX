# Changelog

Written for a reader deciding whether a version does what they need, and for an auditor asking what
changed between two artefacts they hold. Entries name the issue that owns the work, because the
issue carries the reasoning and this file carries the outcome.

## How to read the earlier entries

This file starts at 0.6.0. **The entries for 0.2.0 through 0.5.0 summarise
[`docs/roadmap.md`](docs/roadmap.md)'s own wave-by-wave record rather than deriving from git
history**, and one boundary is the reason.

`git log v0.2.0..v0.3.0` does not describe what a reader would expect: `v0.2.0` is not an ancestor
of `v0.3.0`, and the two tags differ symmetrically by 462 commits against 475. That discontinuity has
a known cause rather than being a mystery — #23 purged reproduced normative text from git history,
not only from the working tree, so everything before that rewrite sits on a line the current one does
not contain.

Every later boundary is ordinary. `v0.3.0..v0.4.0` and `v0.4.0..v0.5.0` are both linear ranges
(`0 93` and `0 72`), so a reader wanting commit-level detail for 0.4.0 or 0.5.0 can walk them and get
exactly what they expect. These entries stay at wave granularity for consistency with 0.6.0's, not
because the history is unavailable.

---

## 0.7.0 — 3 September 2026

Rendered-truth verification (#16, all five children), and the text path it needed. A committed screen
is now rendered offscreen and checked against the artifact that produced it, on every automatic CI
event, across every approved locale.

**This release closes two of the three limits 0.6.0 published, and narrows the third.** That is the
shortest description of what changed: 0.6.0 shipped a compiler whose output nothing verified and
whose text nothing baked.

### Rendered-truth verification (#16, all five children)

- **The boundary** — ADR-014 fixes what a check is made of, what the verifier may take on trust, what
  `verification.json` may claim, and where its worth to an IEC 62304 §5.7 argument stops. It settles a
  contradiction between the epic and ADR-012 in ADR-012's favour, and for a reason the epic could not
  have known: the golden predicate's inputs are not in `package.json`, so the check the epic asked for
  was unwritable (#251).
- **The checks** — `mdux.verify`, four pure functions over a CPU framebuffer: `goldenBounds()`,
  `colorHash()`, `inkContainment()` and `localizedTextPresence()`. Governed, so they allocate nothing
  and throw nothing, and testable against a framebuffer a unit test painted itself (#252).
- **The driver** — `mdux-verify-ui --screen=generated/screen/<id> --locales=all`. It enumerates the
  complete obligation set before the first frame, renders once per approved locale (or once in an
  explicit locale-free scope), and separates a check that failed from a run that could not be made. A
  locale subset is refused: a verifier whose scope is an argument reports on whatever it was asked
  about, in a file that reads as though it reported on the screen (#253).
- **The artifact** — `verification.json`, a fourth committed file in the screen bundle, re-derived and
  byte-compared on all four toolchain legs. It records one outcome per obligation and no measurement:
  no found rectangle, no sampled colour, no path, no duration, because a byte-compared file holding
  one becomes a property of the driver that produced the frame rather than of its declared inputs
  (#254).
- **The CI gate and the failure diff** — `verify.screen.<id>`, registered beside the screen's own
  evidence test, asserted on the three render legs. A failing check writes a PNG of the frame with
  each failed obligation's expected rectangle in magenta and what was found in cyan, uploaded as a CI
  artifact (#255).

ADR-014 gained a fifth decision on the way. The committed screen's two golden nodes were a
`NumericDisplay` and a `SignalTrace`, both deferred by the runtime, so the gate would have been red on
the day it was added. They now paint the **field** they reserve — the node's whole rectangle in its
single colour token — while the reading inside it waits on #257 and #258. The rule is read off the
golden sidecar rather than invented: `collectGoldens()` already pairs each node's rectangle with its
token, and `goldenBounds()` reads that pairing as an equality, so a runtime painting nothing there was
disagreeing with the artifact its own compiler emitted.

### The text path, completed (#14, #15 follow-ons)

- **A baked text package** — `mdux-textbake` positions a run from a string and emits a per-locale
  package, so a screen carrying `t("STR-KEY")` compiles end to end (#235).
- **A drawn `Label`** — the governed runtime joins a compiled screen to a text package and the glyphs
  reach the display (#242).
- **An authenticated join** — a screen records the digests of the text packages it was compiled
  against; `TextBinding::create()` refuses one it was not, and `render()` refuses a binding another
  screen approved. Without it, a second individually-valid package could display wording the screen's
  review never approved (#244).
- **`ClockFormat` and `SystemEvent` closed** — an open format name can only be looked up, never
  measured, so a `Clock` could not have a text budget. Both are now closed sets fixed by the shared
  contract (#219).

### Platforms and CI

- **Apple Silicon macOS is a verified target** (ADR-013): upstream Clang 21.1.8, libc++, CMake 4.3.1
  and MoltenVK, pinned exactly. AppleClang, GCC on macOS and Intel hardware are rejected at configure
  time rather than tolerated (#241).
- **The Linux Clang leg runs on every pull request.** It earned its place immediately, catching a
  stack-frame guard violation and an install-tree standard-library mismatch that three green legs had
  missed (#246).
- **Four evidence legs, each with a Vulkan device.** Since the bake renders, a leg without one fails
  to build rather than producing a weaker artifact, so Linux/Clang and Windows/MSVC gained software
  rasterizers (#254).
- **`mdux-ml` emits `constexpr` model packages**, the same treatment shaders and screens already had
  (#271).
- **A lint that checks documents name CI mechanisms that actually run** (#249) — added after ADR-005
  was found asserting a lint that did not exist.

### Known limits, stated because they are easy to mistake for defects

- **Verification is internal consistency, not truth.** The expectation and the frame come from one
  source, so no amount of it can show that the screen is the right screen. ADR-014 records this as a
  constraint on the artifact's wording because no amount of checking removes it.
- **The reading inside a field is still deferred.** A green `verification.json` says the two
  safety-critical nodes occupy their declared rectangles in their declared tints. It says nothing
  about what they will display: the digits a template expands to and the excursion a waveform makes
  need a sample the runtime is not given (#257, #258).
- **Seven of eleven components are still undrawn.** `Image`, `VulkanViewport`, `Clock`,
  `StatusIndicator`, `Button`, `CriticalButton` and `TextInput` are visited, counted in
  `FrameStats::deferred`, and left alone (#17). This narrows 0.6.0's "the runtime draws a `Panel`"
  rather than closing it.
- **The gate is not asserted on Windows.** That leg runs `verify.screen.<id>` as an ordinary ctest,
  but no step fails on it, so a lost ICD there would show as a pass on that one check. ADR-007
  decision 6 records the reasoning; #282 asks whether to change it.
- **An approved locale tag has no grammar** in the artifact schema, so `en/US` is accepted today. The
  one consumer that could have been hurt by it is defended; the schema question is #281.
- **`ccache` corrupts local module builds** and its symptoms look like a host miscompilation. CI is
  unaffected — runners have no ccache — but a contributor building locally should configure with
  `-DENABLE_CACHE=OFF` until #280 is resolved.
- **No IEC 62304 §5.7 system claim.** This release supplies a mechanism such an argument could rest
  on; it does not supply the system, the requirements it would be tested against, or a representative
  environment. A verified screen is evidence about a screen.

---

## 0.6.0 — 23 August 2026

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

- [`docs/release-process.md`](docs/release-process.md), the procedure for cutting a version —
  written before the version it is for.
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
