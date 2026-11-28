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

## Unreleased

### Compiler cache: named-module compilations are cached in CI (#392)

- **CI builds go through [cppmcache](https://github.com/ambroise-leclerc/cppmcache) 0.1.2 on all four
  toolchains**, in place of ccache. ccache could only be trusted with Clang module consumers (#280,
  #386): module interfaces always compiled directly, and on GCC and MSVC every named-module
  compilation bypassed the cache. cppmcache keys a consumer on the content of the BMIs it imports and
  restores the BMI an interface produces, so both are cached on every compiler.
- **`-DCACHE_OPTION=cppmcache`** is the new value of an existing option. ccache stays the default
  for local builds, with unchanged behaviour.
- **`build.cache.namedModuleIntegrity` covers cppmcache**, and with it runs on GCC and MSVC as well
  as Clang. It is the negative case of #280: a changed exported layout must reach a consumer whose
  source did not change, and a content-identical rebuild must be a real cache hit.
- Measured locally with GCC 16.1 on the `ninja-gcc` preset, one run per cell: with the cppmcache
  0.1.0 binary, a clean build takes 60.9 s with an empty cache (53.5 s without a cache) and 9.2 s from
  a clean build directory with a populated one (332 hits); with 0.1.1, the populated-cache build took
  9.5 s and 9.8 s. All 953 tests pass on the build served from the cache. Nothing was measured on
  Clang, MSVC or macOS before this entry was written.
- **0.1.1, not 0.1.0.** cppmcache 0.1.0 did not detect a header created earlier in the search order
  than one a unit included: a pull request adding `include/vulkan/vulkan.h` could have been tested
  with objects compiled against the SDK's header (ambroise-leclerc/cppmcache#24). 0.1.1 detects it,
  and `build.cache.namedModuleIntegrity` now has that negative case: in a fresh build directory with
  a populated cache, a header added earlier in the search order must reach its includer. With 0.1.0
  the check fails.
- The pull-request policy of #386 is unchanged: a pull request restores the cache `develop` saved
  and never saves its own. cppmcache has no eviction, so each workflow drops a restored cache that
  has grown past 1 GiB, and a measurement of that size that fails now fails the step instead of
  reading as 0 MiB. `prune-compiler-cache.yml` runs after each toolchain workflow's `develop` push
  and deletes every cppmcache cache but the newest of each job, so the five jobs' superseded
  per-commit entries do not crowd other caches out of the repository's 10 GB limit. It checks
  nothing out, and holds the `actions: write` permission so that the build jobs, which run pull
  request code, do not.
- **Installed through `.github/actions/setup-compiler-cache`**, shared by the five jobs, with
  cppmcache's setup action pinned by commit and given the archives' SHA-256 digests, which it
  compares with the download before extracting it or running anything from it. Until 0.1.2 the
  action took no digest and checked the archive only against a checksum file from the same release,
  so a release whose two files were replaced together would have run on the runner first
  (ambroise-leclerc/cppmcache#31). 0.1.2 adds that input and changes nothing in the binary but its
  version, which a byte comparison of the two Windows executables confirms.
- **The fixture links MSVC executables in full.** With incremental linking, the Debug default, the
  MSVC leg reported the old layout after a change although every compilation was a correct miss:
  the fixture builds in about a second, and the incremental linker judges a library member by a
  timestamp of one-second resolution. That had nothing to do with the cache, so the fixture now
  links with `/INCREMENTAL:NO`, in every configuration.
- **The fixture's executables stay at the top of its build directory under any generator.** With
  Ninja Multi-Config they went to a per-configuration subdirectory where the test did not look, so
  it failed however the cache behaved; now that cppmcache registers it on every compiler, that
  would have reached any build using that generator.
- **Losing the cache can no longer pass silently.** `-DCACHE_OPTION=cppmcache` fails the configure
  step when cppmcache is missing, not recognised or older than 0.1.1, or when `CACHE_BINARY`
  reports ccache or cppmcache while `CACHE_OPTION` names the other, instead of warning and falling
  back, and
  `build.cache.namedModuleIntegrity` is a named step with `--no-tests=error` on all four toolchains,
  not only on Clang; on macOS the Release job runs it, and the sanitizer job, whose objects differ
  only by instrumentation, excludes it. Its header case also checks, object by object in
  cppmcache's log, that the fresh build directory was served from the populated cache, so it can
  pass neither by missing everything nor on hits counted from other commands.
- cppmcache is recorded in `docs/governance/soup-register.toml` as a build tool.

### Toolchain, CI and dependency versions (#382)

- **CMake 4.3.6 on every CI leg**, where legs had run 4.1.1 or 4.3.1. The qualified window stays
  4.0–4.3. 4.4 and 4.5 were reviewed and refused: with GCC, 4.4 builds sibling-target module imports
  against a different synthesized `std` BMI than their producer ("import 'std' has CRC mismatch"),
  and SpecLab v0.4.0 refuses 4.5. The GCC leg's CMake download is now SHA-256-checked like the
  others.
- **LLVM/Clang 23** on the Linux Clang leg (from 21), and **upstream LLVM 23.1.6** for the verified
  macOS tuple (from 21.1.8), installed from a SHA-256-pinned Homebrew bottle. ADR-013 is amended
  accordingly. The Linux toolchain file is now `cmake/toolchains/linux-clang23-libcxx.cmake`.
- **GCC stays on `gcc:16.1.0`.** `gcc:16.2.0`, the only 16.2 release, still fails to read SpecLab's
  module BMIs ("Bad file data"), retested on this tree (ambroise-leclerc/SpecLab#17).
- **Runners**: the Linux Clang, Windows and macOS legs are pinned to `ubuntu-26.04`,
  `windows-2025-vs2026` and `macos-26` rather than floating, since their toolchains come from the
  image. Container-based and lint jobs stay on `ubuntu-latest`.
- **Vulkan**: LunarG SDK 1.4.363.1 on macOS, 1.4.363.0 headers and loader on Windows (from 1.3.204.0),
  and Windows lavapipe from mesa-dist-win 26.3.1 (SHA-256-pinned).
- **Dependencies**: GLFW 3.5.1 (CPM fallback now pinned by commit), cpp-httplib 0.67.0, CPM.cmake
  0.43.2; SpecLab stays v0.4.0, the latest release. The MedUI corpus stays on `0.3.0-rc.1`, since no
  final 0.3 release exists.
- **Actions**: codeql-action v4.39.5, scorecard-action v2.6.0, osv-scanner-action v2.7.1 (one version
  for both reusable workflows).

---

## 0.8.0 — 6 September 2026

The rest of the component dictionary (#17, all six children and one follow-on), and the rest of
agent/LLM tooling parity (#19, its last three children). Every component ADR-011 names now draws,
and the `.medui` contract is machine-readable end to end: grammar, per-recipe schemas, and a resolved
IR a compiler can dump.

**This release closes the two biggest limits 0.7.0 published.** 0.7.0 said "seven of eleven
components are still undrawn" and "what remains is the recipe schemas and the IR dump" — both
epics behind those sentences are now closed on GitHub, not merely merged.

### The component dictionary, complete (#17, all six children + #297)

- **`Image`, from a QOI-decoded package** — a host-only decoder (~200 lines, lossless, public
  domain; PNG was cut deliberately, since a hand-written DEFLATE inflater is ~400 lines of much
  subtler code for a format designers can export out of), baked into a committed RGBA8 sidecar the
  device never parses (#256).
- **`SignalTrace` draws its waveform** — expanded on the device from a caller-owned ring buffer into
  the vertex budget the compiled screen already declares, each segment a quad with round-cap
  overdraw rather than mitred joins (#257).
- **`NumericDisplay` and `Clock` draw live values**, through a pattern whose slot positions are
  build-time constants — the bounded exception ADR-010 decision 4 admits, extended from a reading's
  fixed literals to a reading's fixed *shape* (#258).
- **`StatusIndicator` draws its state**, one tint per member of a closed `states:` list; a bound
  indicator without per-state colours is refused at start-up rather than left looking the same in
  every state (#259).
- **`TextInput` draws its value and caret**, on a fixed-pitch grid — cell *k* at `k * cellWidth(font)`
  — because its value is characters from an open-ended charset and no fixed pattern describes what
  an operator types. Display and caret only: no IME, which is a platform concern ADR-004's trust
  boundary excludes (#260).
  - **Follow-on: `charset:` now bounds what the *device* displays, not only what the compiler
    accepts (#297).** A compiled node used to carry the charset's name and nothing else, so a field
    declared for digits displayed a letter a host handed it. The node now carries the resolved
    code-point ranges, checked when a value is bound and again when a frame is drawn — a portability
    difference from any implementation that does not carry them, recorded in ADR-012 rather than
    left to be discovered.
- **`Button` and `CriticalButton` draw a face and resolve a press** — a rectangle is the face,
  since nothing in this project had decided otherwise; a press on a `CriticalButton` resolves to a
  closed `SystemEvent` and the requirement it is traced to, refusing an action outside the set
  #219 closed rather than performing a no-op (#261).

This is the last child of #17: every component the component dictionary names now draws. The epic is
closed.

### Agent & LLM tooling parity, complete (#19, its last three children)

- **A published, machine-readable `.medui` grammar** — `docs/medui/grammar.json`, walked out of the
  compiler's own tables (the closed-set enums, the component dictionary) rather than hand-transcribed,
  so the contract and the parser cannot drift apart. `--grammar` prints it; `--explain <CODE>` answers
  for any registered diagnostic (#263).
- **A committed JSON Schema for every recipe kind** — `docs/recipes/*.schema.json`, validated against
  every committed bake report on every push, so a schema and the reports it describes cannot diverge
  silently (#264).
- **`--dump-ir` and a generated host-tool manifest.** The compiler's resolved intermediate
  representation — the bounded box tree, resolved colours as float bit patterns, text budgets — is
  dumpable as canonical JSON, including for a screen a later stage refused, so an author debugging
  `MEDUI-E050` sees the box that failed rather than nothing. `docs/tools/manifest.json` is generated
  from the build's own tool registration — `add_executable`, `mdux_bake_artifact`, the parser each
  tool actually calls — so a tool cannot go undocumented by existing (#265).

`#19` has no open child left either.

### Two things closed along the way

- **An approved locale tag now has a grammar.** `isLocaleTag()` closes a hole `en/US`,
  `(locale-free)` and a kilobyte of text were all falling through, as a `constexpr` check in the
  artifact schema a generated screen's own `static_assert` enforces (#281).
- **The rendered-truth gate is asserted as a named step on the Windows leg**, matching the other
  three render legs, with the failure-diff image uploaded there too (#282).
- **`ccache` is BMI-aware for Clang modules**, closing the local-build corruption 0.7.0 warned about;
  `-DENABLE_CACHE=OFF` is no longer needed as a workaround (#280).
- **Release-branch topology is guarded mechanically** rather than by habit: a check enforced from a
  trusted workflow definition, run on every push to `master` or `develop` and once a day, catches a
  `master` that carries commits `develop` lacks before a release compounds the drift (#284).

### Known limits, stated because they are easy to mistake for defects

- **Verification is still internal consistency, not truth.** Unchanged since 0.7.0: the expectation
  and the frame come from one source, so no amount of checking shows the screen is the *right*
  screen.
- **No IEC 62304 §5.7 system claim.** This release supplies a complete component dictionary and a
  machine-readable contract; it does not supply the system, the requirements it would be tested
  against, or a representative environment.
- **`charsetRanges` is a portability difference from an implementation that does not carry them
  (#297).** The same `.medui` source would display a character on such an implementation that this
  one refuses. MEDUI-DEC-006 delegates the character sets themselves to the implementation, so this
  is not a contract violation — it is recorded in ADR-012 as the kind of divergence upstream should
  decide on, not papered over here.

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
