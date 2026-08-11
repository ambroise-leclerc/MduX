# ADR-011: The deterministic `.medui` compile boundary

## Status
Accepted (2026-08-11)

## Context

`.medui` is to become the single authored UI source for MduX (issue #15). The decision this record
fixes is not the grammar — that is described in the `medui-authoring` skill and settled by the
parser in #192 — but **where the work happens**: which stages run on a build machine and which, if
any, may run on a device.

Three things in the tree already constrain the answer, and they are the reason this ADR can be
narrow.

**The draw layer already exists and already assumes this decision.** `mdux.draw`'s `DrawBudget`
carries the comment "Computed once by the `.medui` compiler for a screen (#15) and baked, so the
storage a device needs is known before the device runs." A `DrawList` is built over caller-owned
storage sized from that budget and never grown. A compiler that could not compute the budget at
build time would leave that type with no way to be used as designed.

**Text shaping is already forbidden on device.** ADR-010 settled that static text bakes to
positioned glyph runs and dynamic text is restricted to a validated charset. A `.medui` runtime
that resolved `t("STR-KEY")` itself would reopen the question ADR-010 closed.

**The governed zone cannot host a parser.** ADR-004 keeps `MduXCore` on `std` alone, and issue #116
made ADR-005's no-throwing rule mechanical — `mdux-governed-lint` rejects `throw`/`try`/`catch` in
governed source on every toolchain. A parser over untrusted input that must report failure without
throwing, without allocating, and without a filesystem is a very different program from one that may
do all three. The host-tools zone exists for the second kind.

What is *not* yet settled, and what this ADR therefore has to state rather than assume, is whether
the compiled result is a data blob a governed runtime interprets, or generated source a compiler
consumes. That choice determines whether a device build links a parser at all.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)
- **Verification moves left.** A screen whose layout, text budgets and colour tokens are resolved at
  build time can be rejected by a build, and a build failure is an artifact of the development
  process rather than a field observation. A screen resolved at runtime can only be verified by
  running it, on every input that changes it.
- **The device's software items shrink.** A runtime containing no parser, no layout solver and no
  shaper has fewer items to verify, and the items it does have are simpler to argue about.

### IEC 62366 implications (usability engineering)
- A text budget checked against **every** approved locale at build time is a usability control that
  cannot silently regress when a translation is updated: the update either fits or the build stops.
  Deferring the check to runtime turns an overflowing label into a field defect.

### Risk management
- Unbounded runtime allocation driven by document content is a hazard class this boundary removes
  outright rather than mitigates. A screen that does not fit its budget fails to compile.
- The residual risk this boundary creates is stated under Consequences: a screen cannot change
  without a rebuild, so a deployed device cannot be corrected by editing a file.

### Traceability
- `requirement:` on a component is resolved at compile time, so the requirement-to-node mapping is a
  build output rather than a claim in prose. #196 makes safety-critical nodes emit golden references
  carrying that mapping.

## Decision

**Every stage from source text to positioned, budgeted geometry runs on the build machine. The
device runtime performs none of them.**

| Stage | Where | Zone |
|---|---|---|
| Lex, parse, build AST | build machine | host tools |
| Resolve theme tokens and locale strings | build machine | host tools |
| Solve layout, flatten `Row`, compute absolute rectangles | build machine | host tools |
| Validate text budgets against every approved locale | build machine | host tools |
| Compute the `DrawBudget` | build machine | host tools |
| Emit golden references for safety-critical nodes | build machine | host tools |
| **Build a `DrawList` from a compiled screen** | **device** | **governed** |

Concretely:

1. **The compiler is host-only.** It lives under `tools/medui/`, is registered on a host-tools
   target, and is deliberately not declared governed — the same arrangement as `mdux-shaderbake`,
   `mdux-mlbake` and `mdux-textbake`. It parses untrusted input and may throw and allocate freely,
   because nothing it contains reaches a device.

2. **The schema is governed and shared.** A single `mdux.medui.schema` module defines the compiled
   screen. It is imported by both the host compiler that writes one and the device runtime that
   reads one. This is ADR-008 decision 1 applied to screens: not a discipline that two
   implementations be kept in step, but one definition that cannot drift from itself.

3. **The compiled screen is `constexpr`-constructible over spans, and contains no parser.** It is
   the shape `mdux.ml.schema`'s `ModelPackage` already has, for the same reason — a device with no
   filesystem must be able to hold one in `.rodata` and validate it at compile time.

4. **The governed runtime's whole job is `compiled screen + scratch storage -> DrawList`.** It
   allocates nothing, throws nothing, and reads no file. It is `mdux.ml.runtime`'s `predict()` in a
   different domain, and #199 builds it into `MduXCore` so that `mdux-governed-lint` and
   `governed.noThrow.symbolScan` cover it without a second registration.

5. **The language is restricted so that the budget is computable.** Loops, conditionals, recursion,
   runtime scripting and nested `Row` are rejected by the parser with a diagnostic. This is not
   minimalism for its own sake: each of them makes the number of primitives depend on something the
   compiler cannot see, and a `DrawBudget` that cannot be computed exactly is not a budget.

6. **Layout arithmetic is integer-only.** The compiled output is committed and byte-compared across
   toolchains under ADR-007, so a float in the solver would make the artifact a property of the
   compiler's code generator. `mdux.text.raster` took this position for the same reason and it held.

## Alternatives Considered

### 1. Interpret `.medui` on the device (Rejected)
**Pros:** A screen could be replaced without rebuilding; one artifact instead of a compile step;
matches how the deleted HTML/CSS path was shaped.
**Cons:** Puts a parser over untrusted input inside the governed zone, which ADR-004 forbids and
#116 now checks mechanically. Makes memory use a function of document content, so `DrawBudget`
stops being knowable ahead of time and the bounded-storage design of `mdux.draw` has no basis.
Moves every check this ADR pulls forward — locale coverage, text budgets, unknown colour tokens —
into runtime failures on a device.

### 2. Compile at build time, but ship the result as data a governed parser reads (Rejected)
**Pros:** One committed artifact; no generated source in the build; a screen could in principle be
swapped without recompiling the application.
**Cons:** "Parser" is the operative word — a JSON or binary reader in the governed zone is still a
reader over external input, with the failure modes and the verification burden that implies. It is
also the arrangement #153 was opened to *undo* for ML packages, where the device currently links a
host-tools module to parse `package.json` at startup. Adopting for screens the shape ML is trying
to leave would be a decision made twice in opposite directions.

Note that this is rejected as the *only* mechanism, not as a capability. ADR-012 keeps a canonical
JSON package as the committed evidence artifact; what is rejected is a device reading it.

### 3. A general-purpose language subset rather than a restricted grammar (Rejected)
**Pros:** Familiar to authors; expresses data-dependent screens directly; less compiler work to
say no to things.
**Cons:** A conditional or a loop makes the primitive count depend on data the compiler does not
have, so the budget becomes an upper bound someone has to guess. Guessed budgets are either wasteful
or wrong, and a wrong one fails on the frame that exceeds it — on a device, in the field. The
restriction is what makes the guarantee exact rather than probable.

### 4. Generate C++ only, with no schema module shared with the runtime (Rejected)
**Pros:** Fewer modules; the generated source can be shaped freely for each screen.
**Cons:** The runtime would need its own definition of what a screen is, and two definitions of the
same structure drift — quietly, and in the direction that the tests happen not to cover. ADR-008
made the opposite choice for ML kernels and the golden vectors exist to detect the case where it
fails; there is no reason to relitigate it here.

## Consequences

### Positive
- `DrawBudget` becomes computable exactly, which is what `mdux.draw`'s bounded storage was designed
  against and has so far only been supplied by hand.
- Locale coverage, text overflow, unknown colour tokens and missing `requirement:` fields become
  build failures. Each is a class of defect that currently has no mechanism at all.
- A device build links no compiler, no parser, and no host-tools module for screens. The trust-zone
  claim is structural rather than conventional.
- The compiled screen is byte-comparable across toolchains, so it can carry the same
  two-toolchain evidence argument the shader, model and font packages already carry.

### Negative
- **No hot reload, and no field-editable screens.** Changing a screen requires recompiling and
  redeploying the application. The deleted HTML/CSS path had a file watcher; nothing here replaces
  it, and nothing is planned to. This is the price of the budget guarantee and it is accepted
  knowingly.
- **Data-dependent structure cannot be expressed.** A screen showing a variable number of rows must
  be authored with the maximum number and hide the unused ones. That is more work for an author and
  it wastes budget in the common case.
- **Authoring feedback requires a build**, until #200's `mdux-medui-check` gives a single-file path.
  Until then a typo in a colour token is found by a compile rather than by an editor.

### Risks
- **The restriction is worked around rather than accepted** — for instance by generating `.medui`
  from a script, reintroducing data-dependence one level up. *Mitigation*: none mechanical, and
  stated plainly here rather than implied. A generated `.medui` is still compiled and still budgeted,
  so the guarantee holds; what is lost is the reviewability of the authored source, which is a
  review concern rather than a runtime one.
- **The governed runtime grows toward being an interpreter** as components accumulate in #17.
  *Mitigation*: `mdux-governed-lint` rejects the constructs an interpreter needs (allocation,
  filesystem, unbounded loops are not covered — allocation and filesystem are), and #199's no-heap
  test is the standing check. The rule to apply in review is that the runtime may compute geometry
  from a compiled screen and may not compute *structure*.
- **Two definitions of a screen appear anyway**, one in the emitter and one in the schema.
  *Mitigation*: #197 requires a test that compiles both emitted forms in one binary and asserts they
  describe the same screen, which is what `shader_spec` already does for shaders.

## Implementation Notes

- Module names follow the existing dotted-lowercase convention: `mdux.medui.schema` (governed),
  `mdux.medui.screen` (governed runtime, #199), `mdux.tools.meduic` (host compiler, #198).
- The compiler's diagnostics use the shared envelope from #118 and the code registry from #191.
- Nothing in this ADR is implemented at the time it is accepted. It is written first deliberately —
  #190 is the first child of #15 — so that the stages that follow are applications of a recorded
  decision rather than a decision assembled from whatever the code turned out to do. Every
  mechanism named above is marked with the issue that will build it; none of them exists yet.

## References
- ADR-004: Trust zones in C++ — the governed/adapter/host-tools boundary this decision is scoped by
- ADR-005: Error handling and exceptions policy — why a parser cannot live in the governed zone
- ADR-007: Evidence pipeline doctrine — the committed-artifact model ADR-012 applies to screens
- ADR-008: Zero-SOUP ML inference — decision 1, the shared-schema pattern this reuses
- ADR-010: No on-device text shaping — the text half of the same boundary
- [TrustSC ADR-001 through ADR-003](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) — the sibling project's compiled-screen model
- Issue #15 and its children #190-#201

## Approval
- **Decision Date**: 2026-08-11
- **Approved By**: Project maintainer
- **Review Date**: when #201 lands the first end-to-end screen — the first point at which this
  boundary has been exercised rather than only described
