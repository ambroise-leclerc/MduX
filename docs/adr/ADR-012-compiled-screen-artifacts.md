# ADR-012: What a compiled screen emits, and which parts are committed

## Status
Accepted (2026-08-11)

## Shared decision identity

This record is MduX's C++ and evidence-format realization of
[`MEDUI-DEC-003`](https://github.com/Compliatory/MedUI/blob/763499016fdf16e9ba697cc938647ea67e8f8145/decisions/MEDUI-DEC-003-compiled-screen-semantics.md)
and `MEDUI-DEC-004`. The shared records define meaning; this ADR remains authoritative for
canonical JSON, generated C++ modules/headers, and MduX's committed artifact layout.

## Context

[ADR-011](ADR-011-deterministic-medui-compile-boundary.md) fixes *where* the work happens: every
stage from `.medui` source to positioned, budgeted layout runs on the build machine, and the
governed runtime only turns a compiled screen into a `DrawList`. This record fixes *what crosses
that boundary* — the files the compiler writes, which of them are committed and byte-compared, and
what the generated C++ contains.

Three precedents already exist in the tree and the question is largely whether screens differ from
them.

**Bakes commit, emissions do not.** `cmake/MduXShaderEmit.cmake` states the rule its way: a bake
produces a committed artifact and gets a byte-comparison test; an emission produces a build artifact
and gets none, "because the bytes it renders are already byte-compared as `package.json` and
`shaders.spv`." Registering an emission as a bake "would claim a second, redundant piece of
evidence." All three existing packages follow the bake half — `generated/<kind>/<id>/` holds
`package.json`, `report.json` and a payload. Only shaders follow the emit half:
`mdux_emit_shader_package()` is the tree's one emitter, and the `.cppm`/`.hpp` rendering it
produces lands in the build tree only. Model and font have no generated-source rendering yet — the
ML example still links a host-tools module to parse `package.json` at startup, which is the
arrangement #153 is open to undo. Screens take the shader shape from the start rather than
arriving at it.

**The two-form emitter is settled.** `<binary>/mdux_generated/<kind>/<identifier>.cppm` plus a
`.hpp` carrying the same data, for consumers that cannot import a named module.

**Canonical JSON is settled.** ADR-007 encodes floats as bit patterns because decimal float text is
not byte-identical across MSVC, glibc and libc++, and the pipeline crosses all three.

What is genuinely open for screens, and what this ADR therefore has to decide rather than inherit:
whether the baked payload is *geometry* or *layout*, and where the golden references for
safety-critical nodes live.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)
- A committed, byte-compared screen package is a configuration item with a digest. "Which screen was
  in this build" becomes answerable from the repository rather than from a build log.
- The bake report records `toolVersion` — the project's semantic version, bumped deliberately and
  manually (ADR-007 decision 5) — so a screen regenerated after a deliberate release of the
  compiler tooling is visible as a diff rather than as identical bytes with a different provenance.
  It deliberately does not record the host C++ compiler or a commit hash: ADR-007 decision 6
  requires MSVC, GCC and Clang to produce the same bytes, so build-dependent provenance inside a
  byte-compared artifact would fail `evidence.screen.<id>` on one leg.

### Risk management
- The golden references this ADR places in a committed sidecar are the input to #16's rendered-truth
  verification. Committing them means the *expected* position and tint of safety-critical content is
  reviewable in a pull request, not derived at verification time from the same source that produced
  the thing being verified.

### Traceability
- `requirement:` strings resolved by the compiler appear in the committed package, so the
  requirement-to-node mapping is diffable.

## Decision

### 1. Three files per screen, under `generated/screen/<id>/`

| File | Committed | Contents |
|---|---|---|
| `package.json` | yes | the compiled screen: compiled nodes, absolute rectangles, `DrawBudget`, requirement ids, and the validated `Theme.Colors.<Token>` and `t("STR-KEY")` names each node draws with. **No locale field, no glyph runs, no RGBA8** |
| `goldens.json` | yes | one golden reference per node matching ADR-011's predicate: `@safety_critical`, or an explicit `position:`, or both merged into one entry (#196). Always written, as `[]` when a screen matches no node |
| `report.json` | yes | the ADR-007 bake report: inputs, digests, tool version |

The colour and text columns are **validated names, not baked values** — ADR-011 fixes that boundary.
The compiler proves every token exists and every key is present in every approved locale, and the
device performs the substitution by a bounded lookup in a governed table. Carrying names rather than
values is also what makes the package readable in a diff: a reviewer sees
`Theme.Colors.ScoreDigits`, not `[33, 184, 107, 255]`.

Canonical JSON throughout, written through `mdux.evidence.json`. Registered via
`mdux_bake_artifact()`, so each screen gets a build-tree bake, an `-update` target and an
`evidence.screen.<id>` byte-comparison test on both toolchain legs. Two consequences of registering
that way, both binding on #198:

- **All three files are unconditional outputs.** `mdux_bake_artifact()` declares each entry as an
  `add_custom_command` OUTPUT, so a baker that skips `goldens.json` for a screen with nothing to
  pin breaks the build and the comparison test. Hence the empty array above rather than no file.
- **`<id>` is a slug, not the screen name.** `mdux_bake_artifact()` `FATAL_ERROR`s unless the kind
  and id match `^[a-z0-9][a-z0-9-]*$` (`cmake/MduXBake.cmake:95-100`), while a screen's identity in
  the grammar is CamelCase — `Screen NeuroSense500`. The compiler therefore derives the directory
  slug from the screen name rather than using it, and the mapping has to be injective and recorded
  in the recipe, since two screens differing only in case would otherwise collide.

**Locales do not multiply this layout.** One screen, one directory, whatever the approved locale
count — because ADR-011 keeps the package locale-free and leaves the per-locale glyph runs in the
text package. `mdux.text.schema`'s `TextPackage` already carries a single `locale` field and "the
runtime reads no others" (`include/mdux/text/Schema.cppm:145`), so a device pairs one screen package
with the text package for the locale it is running.

This question was recorded as open in an earlier revision, on the assumption that the screen carried
baked glyph runs. It closes with that assumption. The reason to prefer it, stated as a consequence
rather than a preference: **adding an approved locale rewrites no screen artifact**, so a translation
update cannot change the digest of a screen whose layout nobody touched. In a byte-compared evidence
pipeline that is the difference between re-verifying one artifact and re-verifying all of them.

### 2. The payload is **layout, not geometry**

The package carries resolved rectangles, the budget, and the identity of what each node draws. It
does **not** carry vertices and indices.

This is the one place screens genuinely differ from shaders, models and fonts, all of which bake a
finished binary payload. A screen cannot: `NumericDisplay`, `SignalTrace`, `StatusIndicator` and
`Clock` all draw from live data, so their geometry does not exist until the frame does. Baking
vertices would mean baking only the static subset and computing the rest anyway — two mechanisms
where one suffices, and a budget split across both.

What is fixed at compile time is everything that makes the runtime's job bounded: *where* each node
is, *how much* it may draw, and *which* validated token and key it draws with. Turning that into
vertices is a bounded table lookup followed by arithmetic over a known bound, which is exactly what
a governed, allocation-free runtime can do.

### 3. Generated C++ is emitted, never committed

`<binary>/mdux_generated/screen/<identifier>.cppm` and `.hpp`, produced from the committed
`package.json` by the same two-form arrangement `mdux_emit_shader_package()` uses, and covered by
the same argument: the reviewed artifact is the JSON, and a few thousand lines of generated
initialisers under review is noise that hides signal.

The generated source contains `static_assert(screen.validate().has_value())`, so a malformed screen
is a compile error rather than a startup failure. That requires `mdux.medui.schema` to be
header-only and fully `constexpr`, which is a property #197 has to deliver rather than one that
exists — no such module is in the tree. `mdux.ml.schema` is the precedent that it is achievable:
`tests/ml/SchemaTests.cpp:126` already `static_assert`s a reference package validating at compile
time, and #197 should carry the equivalent for screens, built from `constexpr`-compatible types
throughout. The owning `TextPackage` shape is explicitly *not* the model to copy here, since it
holds `std::string` and `std::vector` and so cannot appear in a `static_assert`.

### 4. Goldens are a sidecar, not a section of the package

They have a different consumer (#16's verifier, not the runtime), a different lifetime, and a
different review audience — a reviewer checking that a safety-critical node's expected bounds
changed deliberately should not have to find that hunk inside the whole screen. A separate file also
gets its own digest in `report.json`, so a goldens-only change is visible as one.

### 5. One recipe per screen, under `recipes/screen/<id>.toml`

Following the existing bakers: the recipe names the `.medui` source, the approved locales, the font
package to measure against, and the theme token table. Every resolved knob is recorded in the
report, per ADR-007's rule that a silently changed default must not leave every report looking
unchanged.

## Alternatives Considered

### 1. Commit the generated C++ as well (Rejected)
**Pros:** A consumer could build without running the emitter; the exact source compiled into a
device build would be in the repository.
**Cons:** It is a mechanical rendering of bytes that are already committed and already byte-compared.
Committing it claims a second piece of evidence that adds nothing, and puts generated initialisers
in front of reviewers who cannot meaningfully check them. `MduXShaderEmit.cmake` made this argument
first and nothing about screens weakens it.

### 2. Bake vertices and indices (Rejected)
**Pros:** The runtime's job shrinks to memcpy; the artifact is a finished payload like the other
three; byte-identity covers the geometry itself.
**Cons:** Only correct for wholly static screens. Every dynamic component would need a second path,
so the runtime would have to build geometry regardless — and then the baked vertices are a
partial duplicate whose budget has to be reconciled with the dynamic one. Worse, it fixes text
*rendering* at bake time, which is fine for static runs and wrong for a `NumericDisplay` whose value
changes.

### 3. Fold the goldens into `package.json` (Rejected)
**Pros:** One artifact, one `validate()`, one digest.
**Cons:** Couples the rendered-truth verifier to the runtime's schema, so #16 would have to parse a
structure most of which it does not use. Makes a goldens-only diff invisible inside a package-wide
one, at exactly the review moment where visibility matters most.

### 4. Emit a binary package rather than JSON (Rejected)
**Pros:** Smaller, faster to load, no text encoding questions.
**Cons:** Not reviewable in a pull request, which is most of why the committed artifacts are
committed. ADR-007 already settled canonical JSON for the package and a binary payload only for
opaque bulk (`shaders.spv`, `weights.bin`, `atlas.bin`); a screen's package is precisely the part a
human should read.

## Consequences

### Positive
- A screen becomes a reviewable configuration item: a layout change shows up as a rectangle moving
  in a diff, and a safety-critical node's expected bounds change in a file of their own.
- The `evidence.screen.<id>` test extends the existing two-toolchain byte-identity argument to
  screens at no additional conceptual cost.
- A device build links no host-tools module and parses nothing at startup, because the generated
  C++ is `constexpr` and lands in `.rodata`.
- `static_assert` moves malformed-screen detection from startup to compile.

### Negative
- **A fourth artifact kind to maintain**, with its own recipe schema, baker and update target.
- **Layout diffs are verbose.** Moving a container by eight pixels rewrites every descendant's
  absolute rectangle in `package.json`. The alternative — storing relative offsets and resolving on
  device — is the layout solving ADR-011 forbids, so this is accepted rather than solved.
- **Two files must stay consistent.** A node in `goldens.json` names a node in `package.json`;
  nothing in the format prevents them diverging. The check cannot live in the governed
  `validate()`: decision 4 puts the goldens outside the schema precisely because the runtime never
  reads them, so the `static_assert` in §3 sees only `package.json` and has no ids to compare
  against. The mitigation is host-side — the baker writes both files from one AST, and #197 owns
  the test.

  That test compares **sets, not references**. Applying ADR-011's predicate to the nodes in
  `package.json` yields exactly the ids `goldens.json` must contain, so the test derives that set
  and requires equality: an id in the goldens with no node behind it fails, and — the direction
  that matters more — a node the predicate selects with no golden fails too. Checking only that
  listed ids resolve would accept the dangerous case silently, since a safety-critical node whose
  golden was dropped looks exactly like a screen with fewer safety-critical nodes. #16's verifier
  derives its expectations the same way rather than trusting the file to be complete.

### Risks
- **The package grows to carry things the runtime does not need**, because it is the convenient
  place to put them. *Mitigation*: the rule to apply is that `package.json` holds what the runtime
  reads; anything read only by a tool belongs in a sidecar, as the goldens do.
- **A regenerated screen differs on a second toolchain** despite the integer-only layout rule.
  *Mitigation*: `evidence.screen.<id>` runs on both legs, which is the check that would catch it;
  ADR-011's integer-only rule is what makes it expected to pass.

## Implementation Notes

- Directory kind is `screen` (`generated/screen/<id>/`, `recipes/screen/<id>.toml`), consistent with
  `shader`, `model` and `font`.
- Nothing here is implemented at the time this ADR is accepted. The schema and emitters are #197,
  the CMake integration and `mdux-meduic` are #198, the goldens are #196. This record is written
  first so that those are applications of a decision rather than a decision reconstructed from
  whatever they produced.

## References
- ADR-004: Trust zones in C++ — why the compiler is host-only
- ADR-007: Evidence pipeline doctrine — the committed-artifact model, canonical JSON, bake reports
- ADR-008: Zero-SOUP ML inference — decision 2, the constexpr-package end state #153 pursues
- ADR-010: No on-device text shaping — why text is *shaped* before the device sees it; the
  key-to-run lookup this record leaves on device is not shaping
- ADR-011: The deterministic `.medui` compile boundary — the decision this one details
- `cmake/MduXShaderEmit.cmake` and `tools/shader/Emit.cppm` — the bake-versus-emit argument in full
- Issues #196, #197, #198 and #16

## Approval
- **Decision Date**: 2026-08-11
- **Approved By**: Project maintainer
- **Review Date**: when #197 lands the emitters — the first point at which the file layout decided
  here is exercised by something that has to produce it
