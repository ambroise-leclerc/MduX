# ADR-007: Evidence pipeline doctrine

## Status
Accepted (2026-07-27)

## Context
MduX currently has no evidence pipeline at all. The committed SPIR-V under
`examples/shaders/compiled/` has no consumer, no recorded provenance, and no check that it
corresponds to the GLSL beside it — and `.gitignore` ignored `*.spv` until issue #43 fixed the
negations. Nothing in the repository can answer "which tool, at which version, from which input,
produced this binary artifact".

Every asset kind still to be built needs that answer: shaders (issue #13), fonts and text packages
(issue #14), compiled `.medui` screens (issue #15), images and traces (issue #17), and ML model
packages (issue #18). Six bakers, six chances to invent a different provenance format. Deciding the
shape once — before the first baker exists — is cheaper than reconciling six of them later, and it
is the difference between artifacts an auditor can read uniformly and six bespoke conventions.

The constraint that shapes the whole design is that authoring dependencies must not reach the device
build. A font parser, a safetensors reader and a SPIR-V walker are exactly the kind of code a
manufacturer would otherwise have to qualify as SOUP. If baking happens at build time on the host
and the *result* is committed, the runtime build never links any of it.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)
- **Configuration identification of every derived artifact.** A baked artifact is software the
  device executes or reads. Committing it alongside a report naming its tool, tool version, tool git
  SHA and input digests makes each artifact identifiable in the configuration-management sense
  rather than an unexplained binary in the tree.
- **Verification is re-derivation, not inspection.** CI re-runs the baker and asserts the output is
  byte-identical to what is committed. That is a stronger and far cheaper verification argument than
  reviewing a binary diff, which no reviewer can meaningfully do.
- **SOUP avoidance is structural, not asserted.** Authoring-time parsers live in host tools that are
  never linked into `MduXCore` or `MduX` (ADR-004), so they are absent from the device build by
  construction — a property the trust-zone link-graph check already enforces mechanically.

### Risk management considerations
- An artifact that silently drifts from its source is a hazard: the reviewed source and the shipped
  binary stop corresponding, and nothing detects it. Byte-identity verification in CI turns that
  silent drift into a failed PR.
- A baker whose resolved options are not recorded is the subtler version of the same hazard —
  see the `options` rule under Decision.

### Traceability requirements
- `report.json` links each output digest back to a recipe digest and input digests, so the chain
  from authored source to shipped bytes is machine-checkable rather than narrative.

## Decision

Every asset kind follows one pattern:

```
recipes/<kind>/<id>.toml + assets/…  ──[ mdux-<kind>bake ]──▶  generated/<kind>/<id>/
                                                                 package.json
                                                                 report.json
                                                                 payload.bin
```

Host-only tools produce these files. They are **committed**. CI re-runs the baker in verify mode and
asserts byte-identity against the committed copies. Runtime builds consume the committed artifact and
never invoke a baker.

### 1. Sidecar binaries — a deliberate deviation from TrustSC
Bulk payloads go in `payload.bin` (or a kind-specific name such as `atlas.bin`), **not** base64
inside `package.json`.

TrustSC embeds payloads in the package JSON. Committing megabytes of base64 makes git history
unusable, and the diff is meaningless either way — no reviewer reads a changed base64 blob. The
guarantee is unchanged: `package.json` carries the sidecar's SHA-256, so verification covers two
files instead of one, and a tampered sidecar fails against the digest in the package it belongs to.

### 2. Floats as `u32` bit patterns in canonical JSON, never decimal text
A float is encoded as `{"bits": 1065353216}`, never as `1.0`.

`printf("%.9g")` and `std::format("{}", f)` are not guaranteed byte-identical across MSVC, glibc and
libc++, and this pipeline crosses all three. Baked font metrics, layout bounds and ML golden vectors
are all floats. This single rule is what makes cross-toolchain byte-identity achievable at all;
without it the rest of the design cannot deliver its central guarantee.

A CI lint bans `%f`, `%g`, `%e` and float `std::format` specifiers under `src/evidence/` and
`tools/`, because one stray specifier in one baker breaks byte-identity in a way that only reproduces
on someone else's operating system.

### 3. Normal builds never write into the source tree
Bakers write into `${CMAKE_BINARY_DIR}/mdux_bake/<kind>/<id>/` and the `evidence`-labelled ctest
compares that against `generated/<kind>/<id>/`. `cmake --build build --target mdux-bake-update` is
the **only** path that copies build-directory artifacts over `generated/`. An author runs it
deliberately and commits the resulting diff; a reviewer reads that diff.

This mirrors Cargo's `OUT_DIR` discipline, which MduX otherwise loses by having no `build.rs`
equivalent.

### 4. `options` in the report is the fully resolved set
Not the recipe's literal contents — the resolved set with every default expanded. Otherwise changing
a default silently changes every output while every report still looks unchanged, which is precisely
the failure a byte-verified pipeline exists to prevent.

### 4b. A report names every tool that produced an output, not only the one it is registered to

`tool` and `toolVersion` name the tool a bake is *registered to*. For five of the seven committed
artifacts that is the whole story, because one tool writes every file. The screen bundle is not:
`mdux-meduic` compiles it and `mdux-verify-bake` then renders it and writes `verification.json`
(ADR-014 decision 4), under one `mdux_bake_artifact()` registration and one report, because a second
report at the same path is forbidden and a reader holding two would have to decide which is
authoritative.

So the single report carries a `stages` array, one record per output that a tool other than `tool`
produced:

```json
"stages": [
  { "output": "verification.json", "tool": "mdux-verify-bake", "toolVersion": "0.6.0" }
]
```

Absent entirely when a bake has one tool, which is why adding it left the other six artifacts byte
for byte as they were. `validate()` rejects a stage naming an output the report does not list, and
two stages claiming one output — an answer naming a file nobody wrote, or two tools for one file, is
worse than no answer.

This is not a cosmetic field. The purpose stated at the top of this record is to answer "which tool,
at which version, from which input, produced this binary artifact". Without `stages`, the screen's
report answers that wrongly for one of its four outputs: it attributes a rendered frame's evidence
to a compiler that never created a Vulkan device. A digest says what was produced and `options` says
how it was configured; neither says by whom.

### 5. No commit SHA in a byte-compared report — it cannot be made to work
An earlier draft of this ADR had `BakeReport` carry `toolGitSha`, a configure-time `git rev-parse
HEAD` baked in as a compile definition, with CI rejecting `"unknown"` in a committed report.
**This is unsound and was removed before merge** (caught in review — see the PR discussion on
issue #52).

The problem is structural, not a flaky edge case: baking happens at commit H0 (whatever HEAD is
*before* the artifact is committed). Embedding `H0` into `report.json` and committing it creates
H1 — a *different* commit, because it now contains a file H0's tree didn't have. CI checks out H1
(or GitHub's synthetic merge commit) and re-bakes; `git rev-parse HEAD` there returns H1, not H0.
The freshly-baked report embeds H1, the committed one says H0, and the byte-comparison this whole
pipeline exists to run **fails every single time**, for every report, regardless of whether
anything about the actual artifact changed. There is no fixed point: no commit's tree can contain
a file that correctly names that commit's own hash, because computing the hash requires the tree
to already be final.

**The fix is to not put self-referential, commit-dependent data inside a byte-compared artifact at
all.** `BakeReport` has no `toolGitSha` field. `toolVersion` (the project's semantic version,
bumped deliberately and manually) is the report's only "which tooling produced this" identity —
and it is safe precisely because a version bump is a separate, intentional action, not something
automatically derived from "whatever HEAD happens to be when this gets committed."

This does not lose the audit trail it looked like it was buying: `git log --follow
generated/<kind>/<id>/report.json` already tells an auditor exactly which commit(s) produced or
changed a committed artifact, authoritatively, for free. Duplicating that inside the file itself
was redundant even before it turned out to be broken.

### 6. Four legs, in the same PR — a claim TrustSC cannot make
The evidence tests run on Windows/MSVC, Linux/GCC 16, macOS/Clang 21 with libc++ (#222), and
Linux/Clang 21 with libc++ (#246), in the same pull request. Byte-identity across independent
toolchains, standard libraries and floating-point code generators is a strictly stronger determinism
claim than TrustSC obtains from a single rustc, and it costs nothing extra because all four legs
already exist.

The fourth leg is not a fourth toolchain, and it is worth saying why it was added anyway. It runs
the same compiler and standard library as the macOS lane, on the same operating system as the GCC
lane. That is what separates "a different toolchain produced identical bytes" from "a different
*platform* produced identical bytes" — two claims this doctrine had been making as one.

This is written down here specifically so that a future change cannot "simplify" CI to one leg
without knowingly discarding the claim.

**Since #254, each leg must also provide a Vulkan device, and that is a new obligation rather than
an incidental one.** The screen bundle's `verification.json` is re-derived by rendering the screen,
so a leg without an ICD cannot produce the artifact at all - and #254 forbids treating that as a
skip, because a leg that copied the committed file would satisfy the byte comparison while providing
no rendered evidence. Two legs had no device when that landed: Linux/Clang installed the loader
without `mesa-vulkan-drivers`, and Windows/MSVC installed `Vulkan-Loader` with no ICD behind it, so
both enumerated zero physical devices while nothing here rendered. Both now carry a software
rasterizer - lavapipe from the distribution on Linux, and Mesa's own Windows build pinned by release
tag and SHA-256 on Windows - joining the lavapipe and MoltenVK the GCC and macOS legs already had.

The byte-identity claim survives the addition because the artifact records outcomes rather than
samples: four drivers agreeing that a check held produce identical bytes, which is exactly the
property decision 4 of ADR-014 protects by keeping measurements out of the file.

**#255 added a second, differently-scoped verification, and the leg count is deliberately not four.**
The bake above re-derives `verification.json` on all four legs and byte-compares it. The
`verify.screen.<id>` gate runs `mdux-verify-ui` over the **committed** bundle, and it is asserted as
a named step on **three** of them: Linux/GCC 16 and Linux/Clang 21 under lavapipe, macOS/Clang 21
under MoltenVK. Windows/MSVC runs it too - it is an ordinary ctest and that leg's full suite executes
it - but no step there asserts it separately, so a Windows runner that lost its ICD would report the
test as skipped and the leg would stay green on that one check. The three legs above fail on a skip.

That asymmetry is a statement about what each leg is for rather than an oversight. The four-leg claim
is about *bytes*: identical artifacts from unrelated toolchains, and Windows carries its full weight
there. This gate is about *pixels*, and the render legs are the ones this repository already treats
as owning that question - ADR-013 makes skipping the pixel suite a failure on macOS, and the two
Linux legs guard theirs the same way. Adding a fourth assertion would mean adopting Mesa's Windows
build as a gate-critical dependency rather than a build one; that is a change worth making
deliberately, in a diff of its own, if the Windows ICD proves as stable as the distribution ones.

**A correction, kept rather than silently overwritten.** This paragraph previously read "Windows/MSVC
**and** Linux/GCC (and Clang, now that issue #48 re-enabled that leg)". Issue #48 did not re-enable
that leg: it added the GCC 16 leg and left the Clang half of its own title open, and
`clang-build.yml` has carried no `push` or `pull_request` trigger since. The parenthesis asserted in
the present tense a leg that has never run automatically — the same defect class #116 found in
ADR-005, in the paragraph written to stop exactly this claim being weakened by accident. The third
toolchain arrived via macOS rather than the Linux Clang leg. #246 then made that leg run too, and it
earned its place immediately: it caught a stack-frame guard violation in `ShaderPackage::toJson()`
and a standard-library mismatch in the install-tree consumer, both of which three green legs had
missed.

## Alternatives Considered

### 1. Bake at build time, commit nothing (Rejected)
**Pros:** No generated files in git; no `mdux-bake-update` step; no possibility of a stale artifact.
**Cons:** Every authoring dependency — TrueType parser, safetensors reader, SPIR-V walker — becomes
a build dependency of every consumer, including cross-compiled device builds. That is the SOUP
surface this project's zero-SOUP direction exists to avoid, and it makes the device build depend on
tools that would each need qualifying. It also gives up reproducibility as an observable property:
there is no committed artifact to compare against, so "the baker is deterministic" becomes an
assertion rather than a test.

### 2. Commit artifacts but verify by digest only, not by re-baking (Rejected)
**Pros:** Much faster CI; no need for bakers to be runnable in every leg.
**Cons:** Proves only that the committed file matches a committed hash of itself — it detects
corruption, not drift. It cannot detect that the artifact no longer corresponds to its source, which
is the failure mode that matters.

### 3. Base64 payloads inside `package.json`, as TrustSC does (Rejected)
**Pros:** One file per artifact; one digest; nothing to keep in sync.
**Cons:** Unusable git history at the sizes ML weight blobs and font atlases reach. See Decision 1;
the digest in `package.json` recovers the integrity guarantee that the single-file layout provided.

### 4. Decimal float text with a fixed `%.17g` format (Rejected)
**Pros:** Human-readable diffs; obvious at a glance what a metric is.
**Cons:** Not guaranteed byte-identical across the three standard libraries this project builds on,
which defeats the entire purpose. Round-tripping is not the property needed here — *identical bytes*
is, and only the bit pattern delivers that. A future tool may render bit patterns as decimal for
human display; the committed form stays bits.

### 5. Embed the commit SHA that produced the artifact in `report.json` (Rejected — see Decision 5)
**Pros:** A single glance at the file tells you which commit built it, without a separate `git log`.
**Cons:** Provably cannot work for anything that is committed and then byte-compared, for the
structural reason in Decision 5: the SHA of the commit containing a file cannot be known while
that file is being written, so the embedded value and the actual containing commit's hash can
never agree once re-baked. `git log --follow` on the artifact path gives the same information
without the self-reference. If a future need arises for "which exact toolchain build produced
this" for *diagnostic, non-compared* output (e.g. a tool's `--version` string), that is a
legitimate and different use of a commit SHA — the constraint here is specifically about
byte-compared, committed artifacts.

## Consequences

### Positive
- Six bakers produce one audit record shape, so an auditor learns it once.
- Authoring dependencies are absent from the device build by construction, verified by the existing
  trust-zone link-graph check rather than by convention.
- Determinism becomes a test rather than a claim, on three toolchains and three operating systems at once.
- The evidence kernel is reusable as-is by issue #18's `ml.determinism.crossToolchain` check, which
  is the same comparison applied to a baked model package.

### Negative
- `generated/` grows in git, and every artifact change produces a binary diff a reviewer cannot read
  directly — mitigated only partially by `report.json` making the *cause* of the change readable.
- Contributors must remember `mdux-bake-update` after changing a recipe or an asset; forgetting it
  produces a CI failure rather than a silent problem, but it is still an extra step to learn.
- Bakers must be runnable in every CI leg, which constrains them to portable C++ with no
  host-specific dependencies.
- Since #254, every leg must also *render*, which turned a Vulkan device from an accident of two legs
  into an obligation of four and put a software rasterizer in the dependency set of two of them. And
  since #255 three of the four assert a second, non-byte-compared check on top of that, so a leg is
  no longer either "runs the evidence suite" or not — decision 6 now records two different scopes,
  and a future reduction has to say which one it is giving up.

### Risks and Mitigations
- **A baker introduces nondeterminism** (hash-map iteration order, a timestamp, an absolute path, a
  locale-dependent conversion). *Mitigation*: the canonical writer sorts keys and rejects
  timestamps, absolute paths and decimal floats by construction; the four-leg CI check catches what
  the writer cannot.
- **Someone hand-edits a file under `generated/`.** *Mitigation*: the strict reader rejects
  permissive JSON, and re-baking overwrites the edit while the byte-comparison fails the PR.
- **CI is reduced to fewer legs for speed.** *Mitigation*: Decision 6 records what each leg buys,
  now including which three assert the `verify.screen.<id>` gate and why Windows is not among them;
  removing one is then a documented reversal rather than an unnoticed regression.
- **The two scopes in decision 6 drift apart again.** That paragraph has already stated a leg set its
  own decision did not support once (#246), and #255 added a second set beside the first, which is
  exactly the shape that produced the earlier drift. *Mitigation*: none mechanical. The rule for a
  reviewer is that a change to which legs run what is a change to decision 6 and to the consequences
  above in the same diff, and that #255's acceptance criteria asked for precisely this pairing.
- **`generated/` is swallowed by `.gitignore` again.** *Mitigation*: the `git status --porcelain`
  assertion that follows the evidence tests catches both an uncommitted artifact and a build that
  wrote into the source tree.

## Implementation Notes
- `mdux.evidence.digest` (issue #50), `mdux.evidence.json` (#51) and `mdux.evidence.report` (#52)
  are governed-zone modules: `std` only, `noexcept`, no allocation in the digest path. They are
  consumed by host tools *and* by the device runtime, which is why they are governed rather than
  living under `tools/`.
- `cmake/MduXBake.cmake` (#53) provides `mdux_bake_artifact()`; every baker registers through it, so
  adding an asset kind is one function call.
- **Two ctest labels, kept distinct.** `evidence` means only "a committed artifact is byte-identical
  to a freshly baked one", so a failure of `ctest -L evidence` has exactly one interpretation. The
  evidence modules' own unit tests use `evidence-unit`. Merging them would make a broken SHA-256
  test and a drifted font atlas indistinguishable in CI.
- `tools/common/` (#54) holds the shared TOML-subset reader and CLI parser. It is host-tools zone and
  may use exceptions freely (ADR-005).
- Host-tool resolution goes through `MduX::<tool>` uniformly, so a cross-compiling build substitutes
  an imported executable without any call site changing.

## References
- ADR-004: Trust zones in C++ (this repository) — the governed/adapter/tools boundary this doctrine
  is scoped by, and the link-graph check that makes "absent from the device build" mechanical
- ADR-005: Error handling and exceptions policy (this repository) — why the governed evidence modules
  are `noexcept` and the host tools are not
- ADR-006: No reproduction of normative standard text (this repository)
- [TrustSC ADR-007](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) — the pipeline
  this doctrine mirrors, and the source of the two deviations recorded above

## Approval
- **Decision Date**: 2026-07-27
- **Approved By**: Project maintainer
- **Review Date**: when the second baker registers through `mdux_bake_artifact()` (issue #14), the
  first point at which the shared shape is tested by more than one consumer
