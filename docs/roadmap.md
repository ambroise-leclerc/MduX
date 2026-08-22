# MduX → TrustSC parity roadmap

> Backlog · ambroise-leclerc/MduX · updated 22 August 2026
> Epic status verified against `develop` @ `45eecbe` · 22 August 2026 · `#15` current through `#199`.
> The divergence table below was last re-verified on 17 August 2026 and is not re-checked here.

MduX (C++23 / Vulkan) and TrustSC (Rust) target the same problem — a medical-device UI
SDK with IEC 62304 Class B/C compliance modelling built in. This is the dependency-ordered
backlog that closes the gap. Waves 1, 2 and 3 have shipped — the renderer draws its first
pixel, zero-SOUP ML inference is in the tree, and the documentation has been rebuilt from
what the build actually produces. Track C's authoring story is what remains: #14 closed
Wave 4 with the font and text pipeline, and #15 is underway in Wave 5 with the compiler
that generates the screens it draws. Eleven of its twelve children have landed — the ADRs,
the diagnostic registry, the front end, semantic analysis, bounded layout, per-locale text budgets,
golden references, the canonical package with its C++ emitters, and the `mdux-meduic` compiler with
its CMake registration — so the compiler now reads a `.medui` file, resolves it to a bounded box
tree, refuses a box that cannot hold its widest approved translation, says where safety-critical
content must appear, and writes the result as a byte-compared artifact and as `constexpr` C++ a
device links without a parser. The first compiled screen is committed under
`generated/screen/endoscope-monitor/`, and the governed runtime draws one without allocating. #200
is next: `mdux-medui-check`, the authoring-side checker.

| Metric | Count |
|---|---|
| Epics | 13 |
| Delivered | 9 |
| Remaining | 4 |
| Waves shipped | 4 |

## The thesis

### Where the two diverge

Re-verified against `develop` on 17 August 2026. Eight of the nine rows have closed since
this table was first written; the one that remains is the `.medui` authoring story, which is
the whole of Track C.

| Area | MduX today | TrustSC today |
|---|---|---|
| UI authoring | Partly closed, and moving. The HTML path is deleted (#127) and `mdux.draw` now describes a frame in governed code. The compiler is complete front to back — lexer, parser, AST, semantic analysis, bounded layout, per-locale text budgets and safety-critical goldens, all host-only and conformance-tested against the shared MedUI spec, then the canonical package, the two C++ emitters, `mdux-meduic` and a committed screen artifact. The governed runtime draws one without allocating. What is still ahead is the authoring-side checker (#200) and the first end-to-end screen (#201), which is what the missing text package blocks. | `.medui` compiled at build time to a `CompiledScreenPackage`. The runtime never parses, never solves layout, never shapes text. |
| Rendering | Closed (#13). A real Vulkan renderer, an offscreen target with readback, and the project's first pixel test running under lavapipe in CI. | A real Vulkan renderer, plus offscreen verification of rendered truth. |
| Evidence | Closed (#12). SHA-256, canonical JSON, bake reports and `mdux_bake_artifact()`. Six artifacts committed under `generated/`, re-derived and byte-compared on both CI legs. | Every asset baked by a host tool into committed `package.json` / `report.json`, byte-verified in CI. |
| ML | Closed (#18). Governed f32 kernels shared by host and device, a fail-closed golden self-test, no heap in `predict` verified three ways, and a committed ECG demonstrator whose weights swap with zero source change. | Zero-SOUP deterministic f32 inference with a golden-vector, fail-closed self-test. |
| Trust zones | Closed (#11). `MduXCore` is governed and never receives Vulkan's include directories; `mdux_verify_trust_zones()` walks the link graph at configure time, `mdux-governed-lint` rejects the banned construct at source level, and `governed.noThrow.symbolScan` rejects it in the emitted objects (#116). | `crates/` / `adapters/` / `tools/` with enforced dependency rules. |
| Docs | Closed (#8, #10). Five standards on real clause structure with per-clause indexes and JSON Schemas, plus the documentation architecture — README derived from real targets, a contiguous ADR index, and a CI lint for internal links and retired paths. | Five standards, clause-accurate modules, per-clause index, JSON Schemas, CI-linted. |
| Copyright | Closed (#7). Reproduced text removed from the tree and from history, with `mdux-docs-lint` in CI to keep it out. | Reproducing normative text is forbidden outright; original prose only. |
| Tests | Closed. 438 tests at `v0.5.0`, and more since, across the in-repository `MduXTest` and SpecLab BDD scenarios. Labelled suites for cross-toolchain byte identity (`evidence`), FP determinism, no-heap verification, governed-zone no-throw (`governed`, #116, with a negative fixture) and rendered truth (`pixel`), plus an ASan/UBSan leg (#179) that found two use-after-frees a green build had missed. | Real suites, including cross-toolchain byte-identity and rendered-truth checks. |
| Packaging | Closed (#11). Install/export restored; MSVC, GCC and Clang presets, with MSVC and GCC 16 both green in CI. | Workspace builds `--locked` on Linux and in containers. |

## Dependency order

### Six waves

An epic opens when every epic it depends on has closed. Four waves have shipped
(v0.2.0, v0.3.0, v0.4.0, v0.5.0), one epic per wave closing the dependency it held.
Wave 5 is in progress: #15's blockers were #12 and #14, both closed. #11 closed ahead of it with
`#116` and `#117`, so no enforcement gap carries into the largest epic of the programme.
#19 spans waves by design; its S3–S6 follow #15.

```text
Wave 1 · shipped v0.2.0     #7 (done)   #11 (done)  #19 (S4–S6 open)
Wave 2 · shipped v0.3.0     #8 (done)   #9 (done)   #12 (done)
Wave 3 · shipped v0.4.0     #10 (done)  #13 (done)  #18 (done)
Wave 4 · shipped v0.5.0     #14 (done)
Wave 5 · in progress        #15 (S1–S11 done · S12 next)
Wave 6                      #16  #17
```

#### When v0.6.0 gets cut

**After #201, not before.** The convention above is one version per wave, and Wave 5 is #15 alone.
Cutting a version at 10/12 would break the pattern the last four releases followed, and would ship a
compiler whose runtime draws one component kind and whose text half is missing: no text package is
baked in this tree, so no screen carrying `t("STR-KEY")` compiles at all. Both gaps close in #201, and
a 0.6.0 released before them would need its notes to explain that it cannot draw a label.

If a version has to be cut sooner than that, the line *after* #199 is the one to take rather than the
line before it. Today's `develop` is a compiler producing artifacts nothing on a device consumes;
#199 is what makes a device able to draw one, so it is the better boundary of the two.

Two things worth settling before the tag rather than during it:

- **There is no CHANGELOG and no release workflow.** What shipped in each of v0.2.0 through v0.5.0 is
  recoverable only from this document's prose. If a release is two issues away, that gap costs least
  to close now.
- **`v0.5.0` is an ancestor of neither `develop` nor `master`**, and `develop` is 668 commits ahead of
  `master`. Whatever the release ritual is, it is not readable from the repository - worth knowing
  before 0.6.0 rather than at the moment of cutting it.

## The backlog

### Thirteen epics

Child issues exist for the six actionable epics. The rest keep their breakdown as a
checklist in the epic body, promoted when unblocked.

---

### Track A · Documentation, governance, regulatory

#### #7 — Copyright remediation · **Done v0.2.0**

The corpus rebuild has no point while the tree still ships reproduced normative text —
including a tracked 1.8 MB draft standard and a copyrighted book.

- #20 ADR: no reproduction of normative text
- #21 Inventory reproduced text
- #22 Remove from the working tree
- #23 Purge from git history
- #24 Delete `inputs/` and `libstd/`
- #25 `mdux-docs-lint` CI check

_Blocks #8, #9_

#### #8 — Clause-accurate corpus · **Done v0.3.0**

Five standards on real clause structure, each with a per-clause index and JSON Schemas.
Adds ISO 14971, IEC 62366-1 and IEC 81001-5-1 — the first of which is the standard most
relevant to a UI SDK.

- #26 Citation convention + `Justification` schema
- #27 Rewrite `docs/iec62304/`
- #28 Rewrite `docs/iso13485/`
- #29 New `docs/iso14971/`
- #30 New `docs/iec62366/`
- #31 New `docs/iec81001/`
- #32 Per-clause index per standard
- #33 JSON Schemas per standard

_Blocks #10_

#### #9 — Governance & the Software Development File · **Done v0.3.0**

Makes "traceable" a property of the repository rather than a claim in a README:
governance types that validate, a real SOUP register, and both SDF trees.

- #34 `mdux.governance` module
- #35 Traceability matrix export
- #36 `soup-register.toml`
- #37 SDF templates
- #38 SDF filled in for MduX itself
- #39 Scope-limits document

_Runs alongside #8_

#### #10 — Documentation architecture & README honesty · **Done v0.4.0**

The Implementation Status table marks five compliance frameworks "Completed" and shows
includes for four `.cppm` files that did not exist when this epic was opened. Two ADRs
were both numbered 002. The epic closes the track by re-baselining the ADR series,
authoring `docs/architecture.md` and `docs/getting-started.md` from the real build graph,
rewriting the README from built reality, aligning `AGENTS.md`, and retiring the
point-in-time documents. All nine children merged to `develop` in PR #154.

- #108 Re-baseline ADR numbering and supersede Catch2
- #109 `docs/architecture.md` from the current build graph
- #110 `docs/getting-started.md` using supported surfaces
- #111 Rewrite README implementation status from built reality
- #65 Land and align `AGENTS.md`
- #112 Retire superseded point-in-time documentation
- #113 Verify IEC 62366-1 corpus mappings against an authorized source
- #114 Verify IEC 81001-5-1 corpus mappings against an authorized source
- #115 Internal-link and retired-path CI lint

_All nine closed in PR #154. `#65` also closes the S1 child of #19_

---

### Track B · Load-bearing foundations

#### #11 — Foundations & trust-zone skeleton · **Done**

A new `MduXCore` that never receives Vulkan's include directories — so
`#include <vulkan/vulkan.h>` in governed code is rejected on every platform. Plus
the prerequisites that produce no demo and block everything.

The foundations shipped in Wave 1: `MduXCore` split, link-graph verification, the test
framework, presets, install/export, and `#48` (GCC 16 CI green; the Clang CI leg stays
disabled, an honesty scope pinned in ADR-007). The two enforcement gaps that postdate
v0.2.0 closed before Wave 5 opened, which closes the epic.

`#116` found the gap it was written for to be live rather than theoretical. ADR-005
asserted in the present tense a governed-source lint that had never been written, and
`src/text/Raster.cpp` — governed, shipped in v0.5.0 — contained `try` and three `catch`
clauses. The intended fix, `-fno-exceptions` on `MduXCore`, turned out to be unavailable:
GCC records the language dialect in every module BMI and CMake synthesises one shared
`std` target, so `import std` and `-fno-exceptions` are mutually exclusive. Enforcement
landed instead as `mdux-governed-lint` over the source and `governed.noThrow.symbolScan`
over the emitted objects; the rasteriser moved to the host-tools zone; and ADR-004 and
ADR-005 were rewritten to describe only mechanisms CI runs, including what they still
cannot claim.

- #40 ADR: trust zones in C++
- #41 ADR: error handling and exceptions
- #42 Delete dead code from `examples/`
- #43 `.gitignore` negations
- #44 A real test framework
- #45 Linux and Clang presets
- #46 Split out `MduXCore`
- #47 Restore install/export
- #48 Unblock the compilers
- #116 S10 — Enforce governed-zone source and exception policy
- #117 S11 — Stack-safe PR integration and post-merge policy

_All eleven closed. `#117`'s PR template and merge-ordering policy land ahead of Wave 5,
which is the most stacked epic of the programme_

> **Closure depended on two independent merges**, and this line stays so that a reader can check
> rather than assume: `#116` closed with PR #189 (the last of a three-PR stack behind #188 and
> #187), and `#117` closed with PR #186, which targeted `develop` on its own. All four merged on
> 11 August 2026, so the "Done" above is behind the tree rather than ahead of it — which is the
> only direction this status line is allowed to be wrong in.

#### #12 — Evidence kernel · **Done v0.3.0**

Host-only bakers produce committed artifacts; CI re-derives them and asserts byte-identity.
Canonical JSON encodes floats as bit patterns, because decimal float text is not
byte-identical across MSVC, glibc and libc++ — and this pipeline crosses all three.

- #49 ADR: evidence pipeline doctrine
- #50 SHA-256
- #51 Canonical JSON writer and strict reader
- #52 Bake-report types
- #53 `cmake/MduXBake.cmake`
- #54 Shared TOML and CLI
- #55 CI verify on both toolchain legs

_Blocks #13, #14, #18_

---

### Track C · `.medui`

#### #13 — Shader pipeline & renderer slice · **Done v0.4.0**

Where MduX draws its first pixel from library code. One 24-byte vertex, one triangle-list
pipeline, four modes; buffers sized once from a compiler-computed budget and never grown.

- S1 Shader schema and SPIR-V baker
- S2 Author and bake the UI shaders
- S3 The C++ emitter, with a header fallback
- S4 Migrate the triangle example
- S5 `DrawList`, `DrawCommand`, `DrawBudget`
- S6 Fixed-budget solid-rect recording
- S7 Offscreen target and readback
- S8 The project's first pixel test
- S9 Retire the HTML path

_Blocks #14, #16_

#### #14 — Font & text pipeline · **Done v0.5.0**

Static text bakes to positioned glyph runs per locale. Dynamic text gets a restricted
charset table — and the compiler rejects any format that could escape it, which turns
"no shaping on device" from a slogan into a compile error.

- S1 Text schema and baker — `mdux.text.schema`, `mdux-textbake`
- S2 Hand-parsed TrueType (`glyf` only) — `mdux.tools.truetype`, host-only
- S3 Rasteriser with coverage AA — `mdux.text.raster`, integer-only
- S4 Atlas packer and font baker — the first committed font package
- S5 Metrics and the tabular-figure assertion — `mdux.font.schema`
- S6 Coverage draw path and text pixel tests — `mdux.text.draw`, rendered under lavapipe

_Unblocks #15_

#### #15 — `.medui` compiler & build integration · **In progress · Wave 5 · 11/12**

The schema module is imported by both the device runtime and the host compiler — one
definition, shared. The runtime never sees the parser, which lives in a host-only tool.
Rust shares types across the crate boundary; C++ can do better.

The front end is in the tree and conformance-tested against the shared MedUI spec
(`medui-conformance.toml`, capabilities `syntax`, `semantics`, `layout`, `safety` — the last claimed
with #196, which is what made the pinned `MEDUI-E070` case executable rather than skipped). ADR-011 and
ADR-012 were amended by #203 before any code depended on them: the compiled screen is
locale-free, so the per-locale text stays in the text package — which is why S6 measures every
approved locale and reserves the worst of them, rather than sizing a box to the locale its author
happened to read.

- #190 S1 ADRs: DSL boundary and generated artifacts · _closed_
- #191 S2 Diagnostics and the MDX-E code registry · _closed_
- #192 S3 Lexer, parser, AST and the fixture corpus · _closed_
- #193 S4 Theme tokens and locale-checked strings · _closed_
- #194 S5 Bounded layout and `Row` flattening · _closed (PR #212)_
- #195 S6 Text-budget validation against every approved locale · _closed_
- #196 S7 Golden references for safety-critical nodes · _closed_
- #197 S8 Canonical package and C++ emitters · _closed (PRs #220, #221)_
- #198 S9 CMake integration and the `mdux-meduic` host tool · _closed (PRs #225, #231)_
- #199 S10 Allocation-free screen runtime · _closed (PR #232)_
- #200 S11 `mdux-medui-check` · _closed (PR #233)_
- #201 S12 First end-to-end screen · **next**

_Blocks #16, #17_

#### #16 — Rendered-truth verification · **Blocked #13, #15**

Render offscreen, then check that critical content appears where the compiled screen says
it will, in the declared tint, in every approved locale — and emit that as evidence. Bounds
and colour checks are exercisable before a single glyph exists.

- S1 ADR: automated UI verification
- S2 Bounds, ink containment, colour hash
- S3 The verify driver
- S4 Evidence report emission
- S5 CI across all locales

#### #17 — Content components · **Blocked #15**

The rest of the component dictionary. Two deliberate scope cuts: QOI rather than PNG in v1,
and no IME — input-method editing is a platform concern that does not belong inside a
governed renderer.

- S1 Image baker and the `Image` component
- S2 `SignalTrace` — shares the demonstrator's sample ring
- S3 `NumericDisplay` and `Clock`
- S4 `StatusIndicator` — has a waiting consumer in the ECG demonstrator
- S5 `TextInput` (display and caret only)
- S6 Buttons with requirement binding

---

### Track D · "LM" — embedded ML and agent tooling

#### #18 — Zero-SOUP ML inference · **Done v0.4.0**

Embed learned models without linking any foreign inference stack. Weights are data:
swapping demonstrator weights for clinically-qualified ones is a re-bake, with zero
application source change. Runs in parallel with all of Track C.

- #56 ADR: zero-SOUP ML pipeline
- #57 `mdux.ml.schema`
- #58 `mdux.ml.kernels`
- #59 Determinism enforcement
- #60 Safetensors import and validation
- #61 Golden generation and the model baker
- #62 `Classifier1D`, fail-closed
- #63 No heap in `predict`, verified three ways
- #64 ECG demonstrator and weight-swap test
- #153 Follow-up: `constexpr` package emitter

_All nine closed · 360/360 on `develop`_

#### #19 — Agent & LLM tooling parity · **Partly done**

A diagnostic envelope of file, line, code, severity and fix hint is what lets an agent
fix a `.medui` error without parsing prose. With a published grammar, it is the difference
between guessing at the DSL and being handed its contract.

AGENTS.md is aligned with the v0.4.0+ architecture, the repository skills are present, and
the stable JSON diagnostic envelope is landed across the tools. What remains is the
machine-readable contract side, which follows the surfaces it describes.

- #65 Land and align `AGENTS.md` · _closed_
- #66 Repository skills · _closed_
- #118 Stable JSON diagnostic envelope across all tools · _closed_
- S4 Machine-readable `.medui` grammar
- S5 Recipe schemas
- S6 IR dump and tool manifest

_S1–S3 closed; S4–S6 follow #15 and #18_

---

## Read before starting

### Three things worth flagging

#### The copyright problem is in history — resolved

`#23` is closed. The reproduced normative text — a tracked 1.8 MB draft standard, both
transcriptions, and a copyrighted book — was purged from the working tree *and* from git
history, and `mdux-docs-lint` now runs in CI to keep it out. This removed the residual
clone-time exposure that a HEAD-only deletion leaves behind.

#### Two claims C++ makes better

Running the evidence tests on MSVC and GCC in the same pull request proves byte-identity
across two independent toolchains, standard libraries and floating-point code generators.
TrustSC gets its determinism from a single `rustc`.

And "the host baker uses the same ML kernels as the device runtime" stops being a
discipline: it is one governed module imported by both. If they ever disagree, it is the
FPU, not the code — which is exactly what the golden vectors exist to detect.

#### One claim it cannot make

`#![forbid(unsafe_code)]`'s audit property is not reproducible in C++. The substitute —
denying governed targets the platform headers, an enforced static-analysis profile, a grep
lint — is real, but it is narrower. The wording is fixed in #40 and #38:

> Governed modules are compiled without access to platform, graphics, or OS headers, are
> checked by an enforced static-analysis profile, and are covered by determinism tests —
> not that they cannot contain undefined behaviour.

---

_Epic status verified at `develop` @ `45eecbe` · 22 August 2026_
_13 epics · 9 delivered · Waves 1–4 shipped · Wave 5 in progress (#15 at 11/12) · no enforcement gaps outstanding_
_All epics on GitHub_