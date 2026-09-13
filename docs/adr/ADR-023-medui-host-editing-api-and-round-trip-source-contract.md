# ADR-023: MedUI host editing API and round-trip source contract

## Status

**Proposed**, 2026-09-13, for [#325](https://github.com/ambroise-leclerc/MduX/issues/325), the first
of epic [#311](https://github.com/ambroise-leclerc/MduX/issues/311)'s three children (authoring
tools / Studio integration). It records the reuse decision #325's acceptance criteria ask for, names
the host-only compile/diagnostic/catalog surfaces that already satisfy the "versioned interface"
bullet, states the schema-negotiation and unsupported-capability rule those surfaces share, and
resolves PAR-REQ-010's open question - lossless versus canonical source round-tripping - explicitly.
It builds on [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) (the host-only compile
boundary this record stays inside) and [ADR-015](ADR-015-versioned-sibling-observations.md) decision
D5, which names #325 as the record that must finalize source-round-trip rules.

It changes no compiled screen, no governed schema, and `medui-conformance.toml`. It adds one
host-tools module, `mdux.tools.medui.serialize`, to `MduXMeduiLib` - the same trust zone
`mdux.tools.medui.grammar` and `mdux.tools.medui.ir` already occupy.

## Shared contract

No MedUI shared decision governs an editing API or a source serializer. MEDUI-DEC-007/008
([MedUI #15](https://github.com/Compliatory/MedUI/issues/15)/[#16](https://github.com/Compliatory/MedUI/issues/16))
concern the `.medui` DSL's syntax and its interaction/binding profile names, and deliver no schema
field and no corpus for how a host tool edits or re-emits source text. This record therefore claims
no shared profile and proposes no `medui-conformance.toml` change, exactly as ADR-020 and ADR-022
recorded for their own local designs.

## Context

TrustSC's comparison target, `tools/trustsc-medui-studio`, is a working editor backend: a JSON DTO
layer (`dto.rs`) mirroring its governed authoring crate's AST, a REST API (`api.rs`:
`/api/screens/{id}`, `/api/compile`, `/api/frame`, `/api/palette`, `/api/serialize`,
`/api/proposals`) and a change-proposal flow that pushes a branch and opens a pull request
(`proposals.rs`). Nothing here copies that implementation - ADR-015's own doctrine forbids it - but
`#325`'s own acceptance criteria ask the same first question ADR-022 answered for the viewport: does
MduX already have what a consumer needs, or does something have to be built first.

Three things are already built, and none of them were built for this issue:

- **The catalog.** `mdux.tools.medui.grammar`'s `grammar()` derives `components` (from
  `mdux.tools.medui.semantic`'s `componentDictionary()`), `fieldDomains`, `namedValues`,
  `themeTokens` and `diagnostics` from the compiler's own tables, carries an explicit
  `schemaVersion` (currently `1`), and is committed as canonical JSON at `docs/medui/grammar.json`.
  This is TrustSC's `GET /api/palette` widget/domain half, already published, already versioned, and
  already derived rather than hand-maintained (`Grammar.cppm`'s own module comment states the "a
  hand-maintained copy would drift" rule this record leans on).
- **The diagnostic envelope.** `mdux::tools::cli::Diagnostic` and `mdux::tools::cli::render(...,
  Format::Json, ...)` give every MedUI diagnostic the one shared envelope
  `docs/governance/schemas/diagnostic.schema.json` publishes - the same shape TrustSC's
  `DiagnosticDto` exists to build for one tool. `mdux.tools.medui.diagnostics`'s registry makes a
  code's meaning stable once published (a comment reword is not a breaking change; a meaning change
  takes a new number).
- **The compile/IR surface.** `mdux.tools.medui.compile`'s driver and `mdux.tools.medui.ir`'s
  `screenIr()`/`screenIrJson()` already expose a resolved, canonical, versioned (`irSchemaVersion`)
  view of one compiled screen - TrustSC's `POST /api/compile` compiled-summary half, minus the HTTP
  transport.

What is missing, and the one thing this issue's own acceptance criteria ask this record to either
implement or specify, is the reverse direction: turning an in-memory `ast::Screen` back into
`.medui` source text. No such function exists anywhere in `tools/medui/` today. `Emit.cppm` writes a
*compiled* screen as C++; nothing writes a *parsed* one back as source.

## Medical Device Considerations

Impact: **potentially safety-relevant authored-source integrity**, per #325's own Impact section.
This record is a design decision plus one new pure, host-only module; it renders nothing, resolves
no name against a governed table, and touches no compiled artifact.

- **IEC 62304:2006 §5.2/§5.3 (software design)**: a software-item design record for a new host-tools
  serializer. No existing verified behaviour changes; `Schema.cppm`, `Screen.cppm` and
  `medui-conformance.toml` are unmodified.
- **IEC 62366:2006 (usability)**: out of scope. This record specifies a machine-to-machine text
  format contract, not a user-facing editing surface - that is #326/#327's work, which this record
  exists to unblock.
- **Risk management**: the one risk this record identifies and mitigates directly is silent loss of
  authored safety metadata across an edit round trip. Decision 4 states the rule (annotations,
  requirement strings and every field the dictionary does not yet recognise all survive unchanged);
  Decision 3 states the one loss this design accepts (`//` comments) and requires it be surfaced to
  an author explicitly rather than discovered later, exactly as TrustSC's own `allow_comment_loss`
  gate does for the same reason.
- **Traceability**: unaffected. `serializeScreen()` writes back exactly the `requirement:` strings
  and `@safety_critical` annotations an `ast::Screen` already carries; it resolves nothing and
  invents nothing.
- **Cybersecurity**: unaffected. The new module imports `std` and `mdux.tools.medui.ast` only,
  allocates freely (host-tools zone, ADR-004/ADR-005) and reads no file.

No cross-implementation parity is claimed. This is not a certification, validation or
production-readiness claim.

## Decision

### 1. Reuse: adapt the shape, not the code, and land nothing an existing surface already provides

MduX will not fork or port TrustSC Studio's Rust DTO/API layer, and will not introduce a parallel
C++ DTO type family duplicating `ast::Screen`/`ast::Node`/`ast::Value` the way `dto.rs` duplicates
`trustsc_ui_dsl_authoring`'s types for `serde`. Two reasons, checked rather than assumed:

- **The duplication `dto.rs` accepts exists to work around a Rust constraint that does not apply
  here.** Its own module comment states why the mirror exists: `trustsc-ui-dsl-authoring` has no
  `serde` dependency by its own ADR, so every `Serialize`/`Deserialize` derive has to live in the
  tool instead. C++ has no equivalent split to work around - `mdux.evidence.json` already builds
  JSON documents from any type's own accessors (`grammar()`, `screenIr()` both do this today), so a
  future JSON view of an `ast::Screen` can be built the same way, from the AST directly, with no
  parallel type family to keep in step.
- **The three surfaces #325's acceptance criteria ask for already exist**, built for #192 (parser),
  #118 (diagnostic envelope) and #263/#265 (grammar/IR), none of which anticipated this issue. Their
  existing shape is the "host editing API"; this record's job is to name that explicitly, complete
  the one missing direction, and state how a consumer negotiates versions across them - not to
  design a new API surface a consumer would have to learn instead.

This is the same conclusion ADR-022 reached for the viewport contract (bullet 3, "the existing
compiled schema suffices") applied one level up: before specifying something new, check what the
compiler's own tables already publish.

### 2. Versioning and negotiation: each surface is already versioned, by two different disciplines, and a consumer names the one it targets

| Surface | Versioning discipline | Where |
|---|---|---|
| Catalog (`grammar()`) | Explicit incrementing `schemaVersion`, bumped when a section is added, removed or reshaped | `grammarSchemaVersion`, `Grammar.cppm` |
| Compile/IR (`screenIr()`) | Explicit incrementing `schemaVersion`, same rule | `irSchemaVersion`, `Ir.cppm` |
| Diagnostics (`cli::Diagnostic`) | Additive-only stability: a member is never removed or renamed, and a `code`'s meaning is fixed once published | `docs/governance/schemas/diagnostic.schema.json`, `Diagnostics.cppm`'s "Stability" section |

**A consumer pins the exact `schemaVersion`(s) it was built against for the catalog and IR
surfaces, and the set of `code`s it recognises for diagnostics.** There is no downgrade path and no
best-effort interpretation: a consumer reading a `schemaVersion` it does not recognise, or a `code`
that is not in `retired()` and not in its own known set, must refuse to proceed with editing support
for that document rather than guess at an unfamiliar section's meaning. This is the same fail-closed
posture `medui-conformance.toml`'s own consumers already take toward an unclaimed phase or profile
(ADR-015 D1: "unknown or unclaimed behavior is unsupported, not passing"), applied to a schema
version instead of a conformance phase. Declaring this rule here, once, is what #325's "document
schema negotiation and unsupported capabilities" bullet asks for; no new code enforces it, because
there is no consumer yet to enforce it against - #326/#327 are that consumer.

The same fail-closed posture applies one level down, to `serializeScreen()` itself: a caller must
gate on `ParseResult::diagnostics` being empty before treating `ParseResult::screen` as editable,
never on `screen.has_value()` alone. `Parser.cpp`'s error recovery keeps building a screen after a
recoverable field error (`recover()`/`recoverAfterMissingFieldTerminator()`), so a rejected source
can still produce one - just missing the field that failed, silently, because `serializeScreen()`
has no diagnostic of its own to raise about a field the parser already discarded. This is
`Compile.cppm`'s own "a stage that reports anything stops the compile" rule, extended to the one new
stage this record adds; `SerializeTests.cpp`'s boundary scenario demonstrates the silent absence
directly rather than leaving it as an inference from the parser's recovery behaviour.

### 3. Round-trip: canonical serialization, not lossless - and the one loss it accepts is named, not hidden

`ast::Screen` (and everything it owns) carries no trivia slot: `Ast.cppm`'s own module comment says
the parser is "deliberately unresolved" about names and values, but it is silent about comments
because there is nothing to be silent about - `Lexer.cppm`'s `skipComment()` consumes `//` to end of
line and produces no token, so no comment's text or position reaches the AST at all. This mirrors
TrustSC's own disposition exactly: its serializer's own doc comment states "the AST has no trivia
slots", and its studio API refuses to overwrite a file containing `//` comment lines unless the
caller explicitly sets `allow_comment_loss`.

MduX makes the same choice, for the same underlying reason, rather than inventing a different rule
for a language whose comment syntax exists for the reason `Lexer.cppm` documents - so that an author
can write down *why* a `requirement:` or `@safety_critical` annotation is what it is. Losing that
prose on an edit round trip is a real cost, which is exactly why bullet 3 of #325's acceptance
criteria requires it be an *explicit* treatment rather than a silent one:

- **`serializeScreen()` is canonical, not lossless.** It always produces one fixed rendering of an
  `ast::Screen`'s content - the same annotations, fields, values and nesting a second serialization
  of the reparsed result would produce - never a preservation of the original file's exact
  whitespace, quoting style or comment placement.
- **Comments are the one thing this design accepts losing, and it is the only thing.** Every other
  syntactic construct the grammar admits round-trips exactly (Decision 4).
- **A future editing tool must detect and gate on this loss before it discards it**, the way
  TrustSC's `has_comment_lines`/`allow_comment_loss` pair does. This record specifies that
  requirement for whichever of #326/#327 builds the tool that writes a file back to disk; it does
  not implement the detector itself, because there is no consumer yet to wire it to and a design
  record is not the place to add unreachable code (AGENTS.md's don't-build-ahead-of-need rule
  applies to a spec issue as much as to any other).

**Alternative rejected: build trivia-preserving parsing now.** Capturing comment text and position
in the AST and re-attaching it on serialization is possible, but it changes `Lexer.cppm`,
`Parser.cpp` and `Ast.cppm` - three files ADR-011 keeps deliberately minimal - for a capability
#325's own acceptance criteria do not require ("explicit treatment", not "no loss"), and for which
neither #326 nor #327 yet has a caller. Revisiting this decision when an editing tool's own
requirements actually need it costs an ADR amendment; building it speculatively now costs a lexer
and parser change nobody can test end-to-end yet.

### 4. Everything else round-trips exactly, including what this compiler build does not recognise

The AST is the author's unresolved description (`Ast.cppm`'s own words), which is what makes the
rest of this decision hold without new code:

- **Fields, in the order the `ast::Node`/`ast::Screen` vectors carry them.** A field an editor never
  touched keeps its authored position in that order; a field an editor added is appended where the
  edit placed it. This satisfies PAR-REQ-010's "order where observable" directly, because the order
  observed *is* the vector's order - there is no second, hidden ordering to disagree with it.
- **Every `ast::Value` kind** - `Size`, `Point`, `String`, `TextKey`, `ImageRef`, `ColorToken`,
  `Identifier`, `Number`, `List` - serializes back to the exact syntax `Parser.cpp`'s `parseValue()`
  accepts for that kind, with `String`/`TextKey`/`ImageRef` text re-escaping the four sequences
  `Lexer.cpp`'s string scanner resolves (`\"`, `\\`, `\n`, `\t`).
- **`@safety_critical` and every other annotation, with all their arguments** - the authored safety
  metadata #325's Impact section names explicitly - round-trip unchanged, because `Node::annotations`
  is serialized the same generic way `Node::fields` is.
- **A field name the current component dictionary does not recognise still round-trips**, because
  `Field::name` and `Value` carry exactly what the author (or an edit) wrote, checked against nothing
  at this stage - the same "structure the grammar fixes, not the dictionary" boundary `Ast.cppm`
  states between parsing and semantic analysis (#192 versus #193). A serializer that instead walked a
  fixed per-component field list would silently drop such a field; this one cannot, because it never
  has one to consult.

`SerializeTests.cpp` pins all of this as `serialize(parse(source)) -> reparse -> compare`
scenarios, not as an implementation detail nobody exercises.

## Alternatives Considered

- **Build a parallel HTTP editing service now, matching TrustSC Studio's `api.rs` route for route.**
  Rejected: #325 is blocked by nothing but #312 and blocks #326 ("serve real MduX previews") and
  #327 (Studio integration) in turn - the roadmap's own dependency order puts the HTTP-serving
  concern one and two issues later. Landing it here would build a consumer before its own contract
  existed to review against, the exact ordering ADR-011's driver-first precedent and this repository's
  "land canonical types/contracts before consumers" delivery rule argue against.
- **A new DTO module duplicating `ast::Screen` for JSON, matching `dto.rs`.** Rejected in Decision 1:
  the constraint that forces the duplication in Rust (`serde` absent from the governed crate) is not
  a constraint C++ has, and `mdux.evidence.json` already builds documents from a type's own read
  access without a parallel type.
- **Lossless (trivia-preserving) round-tripping, implemented now.** Rejected in Decision 3: not
  required by the issue's own acceptance criteria, no caller yet, and a change to three files ADR-011
  keeps minimal for a capability nothing in this repository can exercise end-to-end today.
- **Treat comment loss as acceptable without saying so.** Rejected outright: #325's own Impact
  section says "never silently discard authored safety metadata", and while comments are prose
  rather than the structured safety metadata that survives unchanged (Decision 4), the issue's
  acceptance criteria ask for *explicit* treatment either way. Naming the loss and the acknowledgment
  a future tool must obtain before it is incurred satisfies that without pretending the loss does not
  exist.

## Consequences

### Positive

- Bullets 1 and 2 of #325's acceptance criteria are resolved with no new HTTP surface, no new DTO
  type family, and no `medui-conformance.toml`, `Schema.cppm` or `Screen.cppm` change: the catalog,
  diagnostic and compile/IR surfaces this record names already exist and are already independently
  versioned.
- Bullets 3 and 4 gain one new, small, pure, host-only module (`serializeScreen()`) plus a test suite
  exercising exact round-tripping of every value kind, every annotation, an artificially unknown
  field, and the one accepted loss (comments) - closing the "Implement or specify source
  round-tripping" bullet with an implementation rather than a specification alone.
- #326 ("serve real MduX previews") and #327 (Studio integration) now have a named, versioned
  contract to build against instead of an open design question.

### Negative

- An editor round trip through this serializer loses `//` comments. Mitigation: named explicitly
  (Decision 3), not silently accepted, and gated by an acknowledgment requirement this record places
  on whichever issue writes the file back to disk.
- The negotiation rule in Decision 2 is fail-closed: a consumer built against an older
  `grammarSchemaVersion` or `irSchemaVersion` stops working entirely on a bump, rather than degrading
  gracefully. Mitigation: this is the same trade-off ADR-015 D1 already made for conformance claims,
  for the same reason - a tool that guessed at an unfamiliar shape could silently misinterpret
  safety-relevant content, and refusing loudly is preferable to that.

### Risks

- **A future editing tool (#326/#327) skips the comment-loss acknowledgment this record specifies.**
  Mitigation: `SerializeTests.cpp` asserts the loss is real and reproducible, so any tool that claims
  lossless editing without checking for it can be caught by exercising this module's own tests
  against a source carrying comments.
- **"Canonical, not lossless" is read as license to reformat unrelated fields on every save.**
  Mitigation: Decision 4's exact-round-trip guarantee is the boundary - only comments are lost;
  everything else, including field order, is unchanged by a save that touched nothing.

## Implementation Notes

**#325** (2026-09-13):

- `tools/medui/Serialize.cppm`/`Serialize.cpp` (new, `mdux.tools.medui.serialize`, added to
  `MduXMeduiLib`'s `FILE_SET CXX_MODULES`/`PRIVATE` source lists beside `Grammar.cppm`/`Ir.cppm`):
  `serializeScreen(const ast::Screen&) -> std::string`. Pure, deterministic, `noexcept`-free (host
  tools zone) - no file, no clock, no environment. Throws `std::logic_error` (`Layout.cpp`'s own "a
  gate was bypassed" precedent) for a null `ast::Field`/annotation-argument/list-element value
  reached via a hand-built or edited `ast::Screen` - `Parser.cpp` never leaves one null, but every
  member `require()` guards is public, so an editing tool could. Added during review (Copilot and
  CodeRabbit both flagged the unguarded `appendList()` dereference); the same guard was then applied
  to the two other value sites this module already had a silent-skip for, so all three fail the same
  way instead of two of them merely omitting output.
- `tests/medui/SerializeTests.cpp` (new, added to `medui_tools_spec`): parses every
  `tests/medui/fixtures/accepted-*.medui` fixture, serializes, reparses and compares the second
  serialization against the first (a fixed point, since the serializer's canonical form need not
  match an arbitrary hand-authored file byte for byte); asserts a reparsed, re-serialized screen
  compiles with the same semantic-analysis outcome; asserts `@safety_critical` annotations and
  `requirement:` fields survive exactly; asserts a field name added to the AST outside the current
  component dictionary still round-trips (Decision 4's "unknown fields" claim); asserts the one
  accepted loss is real by confirming a fixture's header comments do not appear in its serialized
  form (Decision 3); and asserts a null field, annotation-argument or list-element value throws
  rather than being dereferenced.
- No change to `Schema.cppm`, `Screen.cppm`, `medui-conformance.toml`, any recipe, or any committed
  `generated/` artifact.
- `docs/architecture.md` (`MduXMeduiLib` row), `docs/roadmap.md` (#311/#325), `docs/parity/requirements.md`
  (the #325 decision-map row), `docs/adr/README.md` (index ADR-023; next free is ADR-024).

## References

- [ADR-004](ADR-004-trust-zones-in-cpp.md) - the governed / adapter / host-tools split this module
  stays inside
- [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) - the host-only compile boundary; this
  record adds no new stage to it
- [ADR-015](ADR-015-versioned-sibling-observations.md) decision D5 - names #325 as the record that
  must finalize source-round-trip rules
- [ADR-022](ADR-022-streaming-viewport-data-and-composition-contract.md) - the "check what already
  exists before specifying something new" precedent this record follows (Decision 1)
- [Prospective requirements](../parity/requirements.md) - PAR-REQ-010
- Issues [#311](https://github.com/ambroise-leclerc/MduX/issues/311),
  [#325](https://github.com/ambroise-leclerc/MduX/issues/325),
  [#326](https://github.com/ambroise-leclerc/MduX/issues/326),
  [#327](https://github.com/ambroise-leclerc/MduX/issues/327)
- `tools/medui/Ast.cppm`, `Lexer.cppm`, `Parser.cpp` - the grammar this record's serializer inverts
- `tools/medui/Grammar.cppm`, `Ir.cppm`, `Diagnostics.cppm` - the existing catalog/compile/diagnostic
  surfaces this record names as the host editing API
- `tools/trustsc-medui-studio/src/dto.rs`, `api.rs`, `proposals.rs` (TrustSC) - the comparison target
  whose *shape* this record adapts and whose comment-loss disposition it mirrors, without copying its
  implementation

## Approval

- **Proposal date**: 2026-09-13
- **Decision date**: pending
- **Approved by**: pending - maintainer engineering acceptance, per #325's own Impact section
  ("safety-relevant implementation requires maintainer/domain-expert review").
- **Scope**: the reuse decision (Decision 1), the schema-negotiation rule (Decision 2), the
  round-trip disposition (Decisions 3-4), and `mdux.tools.medui.serialize`'s `serializeScreen()`.
- **Review date**: alongside #326/#327, once they give this contract its first real consumer.
