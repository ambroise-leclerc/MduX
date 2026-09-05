---
name: medui-authoring
description: Use when authoring or reviewing a .medui screen, or discussing MduX's planned UI-description language — the grammar, component dictionary, theme tokens, text budgets, and when @safety_critical / requirement: are mandatory.
---

# MduX `.medui` authoring

Companion to § 3 ("Verified architecture summary") of [`AGENTS.md`](../../../AGENTS.md). This skill
governs the *authoring* of a `.medui` screen; for the baking mechanics behind it see
`evidence-pipeline`, and for the compliance framing of a safety-critical node see
`regulatory-citations`.

## Status: an authored screen reaches pixels, text included up to the draw

**A `.medui` lexer, AST, parser, semantic analyzer, integer-only bounded layout solver, text-budget
check, and golden-reference pass exist** in the host-tools zone (`tools/medui/`). The diagnostic
codes they emit are registered as `MEDUI-E###` (#191). The parser rejects structural violations
(#192); the analyzer checks the closed component dictionary, each field's value domain, theme-token
names, and text-key presence across every approved locale (#193); the solver flattens Vertical/Row
layout to absolute rectangles without floating-point arithmetic (#194); the budget check measures
each resolved box against the widest approved translation and each named dynamic-text source
against the font package's restricted charset (#195); and the golden pass applies the
`@safety_critical` rules and derives the reference set #16's verifier will consume (#196). The AST
keeps names unresolved.

**The back end exists now**, and this paragraph used to say the opposite. `mdux-meduic` compiles a
screen from a recipe into `generated/screen/<id>/` — the compiled package, the golden sidecar and a
bake report, byte-compared across toolchains (#198). Two emitters render that package as a `.cppm`
and a `.hpp` carrying `static_assert(screen.validate().has_value())`, so a malformed screen is a
build error rather than a startup failure (#197). A governed runtime draws one without allocating
(#199), and `mdux-medui-check` validates a single file (#200).

A font package and a text package are both baked (#235), so a screen carrying `t("STR-KEY")` compiles
end to end and its boxes are measured against the widest approved translation. Writing one means
writing three files, not one: the `.medui` source, a `recipes/text/<id>-<locale>.toml` per locale the
font approves, and the screen recipe's `[text]` table naming them — a locale the font approves with
no package listed is a build error, because a budget checked against fewer locales than were approved
is a claim nobody made.

The runtime draws a `Panel` and, given a `TextBinding`, a `Label` (#242) — the join a locale-free
compiled screen needs to reach glyphs: the font package, the text package for the locale the device
is running, and its sidecar. Without one, a label is deferred rather than refused.

It also draws the **field** a `NumericDisplay` or a `SignalTrace` reserves (#255): that node's whole
rectangle, in the single token it carries, which is exactly the pair its golden entry pins.

A `NumericDisplay` draws its **digits** and a `Clock` its **time**, given a `ReadingBinding` (#258).
Three things to know before writing either:

- **A `template:` needs a `[numericTemplates]` entry in the screen recipe**, naming what it renders
  as — `##.# mmHg`, where `#` is a digit slot and every other character is a literal. Without one
  the compile fails with `MEDUI-E053`, because a template the compiler cannot expand is one whose
  widest reading it cannot measure against your box. The slot character is `#` rather than a letter
  precisely so a unit like `mmHg` stays a unit.
- **The value is a fixed-point integer**, in the template's own units: `1234` under `###.#` is
  `123.4`. A value with more digits than its slots is refused, never truncated.
- **A `Clock` has no `color:` field**, so its tint comes from the binding rather than from your
  screen. That is the one appearance decision the runtime leaves to the host, and it is why a golden
  never pins a clock's colour.

A `StatusIndicator` draws its **state**, given a `StatusBinding` (#259). Three things to know before
you write one:

- **A bound indicator must declare `colors:`.** The field is optional in the dictionary and stays
  optional here, but a node with no per-state tint cannot be bound at all - `StatusHasNoTint`, at
  start-up. With no tint, the only thing that varies between its states is the word, and the word
  needs a locale a device may not have joined yet; an indicator that paints the same rectangle in
  every state is the failure that looks most like a working one.
- **A state is a position in your `states:` list**, not a name and not an open value. The list is
  closed by the time a device holds it, so an index past the end is `StateOutOfRange` and refuses the
  frame - never clamped to the last state, never wrapped to the first, never drawn blank.
- **Without a slot the node is deferred**, not painted in state 0. A device that has not read its
  first status yet is in a normal state, and a default one would be a reading nobody supplied.

What it draws is the state's own tint over the node's whole rectangle, with the state's word over it
when a `TextBinding` is bound - the field dimming to quarter coverage under the word, exactly as a
bound `NumericDisplay`'s does.

A `TextInput` draws its **value and caret**, given a `TextInputBinding` (#260). Four things to know
before you write one:

- **It is a grid, not a run.** Cell *k* sits at `k * cellWidth(font)`, where the pitch is the widest
  advance the font package's charset admits, so a proportional font comes out looking monospaced.
  That is the price of a placement a compiler can certify - ADR-010 decision 4 forbids a pen whose
  width is computed at run time, and a proportional field is exactly that pen.
- **`max_length` is measured against your box**, since this issue: `max_length` cells of the font's
  widest glyph plus the caret's column must fit, or the screen fails to compile with `MEDUI-E050`.
  A `max_length` past `maxFieldCells` (64) is `MEDUI-E053` instead, because no box makes it drawable.
- **Your `charset:` is a compile-time claim about the *source*, not a runtime filter.** It says which
  code points this field's data can produce, and the compiler checks that the font package can draw
  all of them (`MEDUI-E053`). It does not reach the device: a compiled node carries the charset's
  *name*, not its set, so what the runtime refuses is a character the **font package's** charset does
  not admit — which is wider than yours whenever you narrowed it. A host that sends a letter to a
  digits-only field gets a letter on screen. The box is measured against the font's charset too, for
  the same reason, which is conservative in the only safe direction. Narrowing enforcement to the
  node's own set needs the compiled screen to carry resolved ranges, which is
  [#297](https://github.com/ambroise-leclerc/MduX/issues/297).
- **Display and caret only.** No composition, no candidate window, no key handling. The host edits
  the value; the screen shows it. A value longer than the field, or a character the font package's
  restricted charset does not admit, refuses the frame rather than truncating or substituting — the
  charset is the bound, not the glyph table, which may carry more than it declares.

A `SignalTrace` draws its **waveform**, given a `SignalBinding` (#257) — the second join, and
the one whose inputs no artifact carries. A slot names the node's `stream_source`, a caller-owned
ring of samples, and the range those samples are read against; that range is the host's because what
a sample means in millivolts is a property of an amplifier rather than of a layout. Two things to
know before you write one: a ring past `maxSamplesPerTrace` (256) is **refused rather than
truncated**, and a bound trace dims its field so the full-tint stroke over it is visible — an unbound
one is the opaque field #255 draws, unchanged.

One limit is worth knowing before you write a screen: `Button` and `CriticalButton` are still
visited, counted in `FrameStats::deferred` and left undrawn. A button is more than its text — it has
a face nothing in this project has decided — and inventing one here is not this module's call. Both
are [#17](https://github.com/ambroise-leclerc/MduX/issues/17).

The HTML/CSS path that used to stand in for all of this - `UiFileWatcher::loadContent()`, which
sniffed a file extension and stored the file as a string, with no parsing, layout or rendering
behind it - was deleted by
[issue #127](https://github.com/ambroise-leclerc/MduX/issues/127).

What exists now is the layer a `.medui` compiler will target: `mdux.draw` describes a frame as
bounded vertex, index and command buffers with a compiler-computed budget, and
`mdux.render.vulkan` renders one. So the compiler's job is to emit a `DrawList` and a
`DrawBudget`, not to invent a rendering path. `.medui` itself is
[issue #15](https://github.com/ambroise-leclerc/MduX/issues/15). The implementation pins the shared
contract in `medui-conformance.toml`. This skill records MduX status and integration; canonical
grammar, component semantics, diagnostics, and portable guidance live in
[`Compliatory/MedUI` at `265df19`](https://github.com/Compliatory/MedUI/tree/265df1925a672bd556f69123e287215b45cfd210).
A `.medui` file builds something in MduX today, and it reaches the screen: register it with
`mdux_compile_screen()` and it becomes a committed, byte-compared artifact plus generated C++ a
device links, which the governed runtime draws and `ScreenPixelTests` compares pixel by pixel under
lavapipe. It carries text too, since #235: a `t("STR-KEY")` compiles, and the box holding it is
measured against every approved locale's widest translation. Since #242 the runtime draws it — bind
the packages with `TextBinding::create()` and a label's glyphs reach the frame.

## Grammar shape

```
Screen NeuroSense500 {
    layout: Vertical { spacing: 8px; padding: 0px; }
    surface: 1920px, 1080px;

    @safety_critical(cv_check: [Bounds, ColorHash])
    NumericDisplay {
        id: sedation-index;
        width: 512px;
        height: 512px;
        position: 1392px, 80px;
        requirement: "REQ-NS-001";
        template: "TPL-SEDATION-INDEX-160";
        source: "SEDATION_INDEX";
        color: Theme.Colors.ScoreDigits;
    }
}
```

- Sizes are `Npx` or `Fill`. `position: Xpx, Ypx` takes a node out of flow at exact absolute
  surface coordinates. A top-level positioned node must remain inside the padded content box; a
  positioned Row child must remain inside that Row's already-resolved absolute band.
- **One property per line.** The sibling implementation parses a component body line by line and
  splits each on its first `:`, so `width: 512px; height: 512px;` on one line is read as a width of
  `512px; height: 512px` and rejected. This example previously showed that form; it was condensed
  prose, not a supported shorthand, and TrustSC's own `neurosense.medui` has always had the two on
  separate lines. MduX's parser is token-based and would accept either, so a screen written this
  way stays portable in both directions - which is the reason to write it this way.
- `position` requires fixed `width` and `height`; `Fill` is flow-only, and combining them is a
  compile error in the reference implementation (#194 owns this check in MduX).
- `Row { id; height; background?; spacing? }` is a single-level horizontal group, flattened at
  compile time — it cannot nest another `Row`.
- Text is **always** `t("STR-KEY")` against an approved text package. Hardcoded strings are a
  compile error by design — the compiler validates every key against **every** approved locale and
  rejects a component whose bounds are narrower than the widest approved translation.
- Colors are `Theme.Colors.<Token>` from a governed table. An unknown token is a compile error.
- Forbidden, deliberately: loops, conditionals, recursion, runtime scripting, deep nesting.

## Component dictionary (target)

| Component | Required fields | Optional fields |
|---|---|---|
| `Row` | `id`, `height` | `spacing`, `background` |
| `CriticalButton` | `id`, `requirement`, `width`, `height`, `label`, `color`, `on_press` | `position` |
| `Button` | `id`, `width`, `height`, `label`, `color`, `source` | `position`, `requirement` |
| `VulkanViewport` | `id`, `width`, `height`, `stream_source` | `position` |
| `SignalTrace` | `id`, `width`, `height`, `stream_source`, `color` | `position` |
| `NumericDisplay` | `id`, `width`, `height`, `requirement`, `template`, `source`, `color` | `position` |
| `StatusIndicator` | `id`, `width`, `height`, `requirement`, `source`, `states` | `position`, `colors` |
| `Label` | `id`, `width`, `height`, `text`, `color` | `position` |
| `Clock` | `id`, `width`, `height`, `format` | `position` |
| `Image` | `id`, `width`, `height`, `source` | `position` |
| `TextInput` | `id`, `width`, `height`, `source`, `max_length`, `color` | `position`, `charset`, `requirement` |

## Closed named values: `format:` and `on_press:`

Two fields take a member of a fixed set rather than any identifier. The sets are the shared
contract's (MEDUI-DEC-006), not this compiler's, so the same spellings hold for every
implementation.

| Field | Component | Members | Renders |
|---|---|---|---|
| `format` | `Clock` | `TimeSeconds` | `HH:MM:SS` |
| | | `DateTimeSeconds` | `YYYY-MM-DD HH:MM:SS` |
| `on_press` | `CriticalButton` | `NoOp` | nothing |
| | | `TriggerHalt` | the host's halt path |

A well-formed identifier outside the set is **`MEDUI-E034`**, not `MEDUI-E033`. The distinction is
worth knowing because the fix differs: `MEDUI-E033` means the *kind* is wrong (`format: 42`), while
`MEDUI-E034` means the kind is right and only the membership is wrong (`format: HH_MM`).

Because the renderings are fixed above, a clock's box is **measured** rather than declared: the
compiler knows a `TimeSeconds` clock draws eight glyphs and checks them against the node's bounds.
There is no product-supplied table to configure, and a box too narrow for the format is a compile
error.

`charset:` on `TextInput` stays an open name — it resolves against the character sets a build bakes,
which the contract does not enumerate. It bounds what the field may *display*; what its box must
*hold* is measured against the font package's own charset, for the reason the `TextInput` notes
above give.

## `@safety_critical` — when it's mandatory, and when it's automatic

`@safety_critical(cv_check: [Bounds, ColorHash])` on the line before a component emits a golden
reference (`nodeId`, `bounds`, `textKey?`, `colorToken?`, `cvChecks`) that the rendered-truth
verifier checks against. Rules:

- **A safety-critical component must carry an explicit `requirement:`.** If you're annotating a
  node with `@safety_critical` and it has no `requirement:` field, something is wrong — either the
  annotation or the missing requirement.
- **Any node with an explicit `position:` gets an automatic `Bounds` golden reference**, even
  without `@safety_critical` — a declared position is a safety-relevant claim by itself.
- **A node with both gets exactly one merged entry** (deduplicated `cvChecks`), never two.
- Dynamic content (`NumericDisplay`, `StatusIndicator`, `Clock`, `SignalTrace`) pins its *bounds*
  and *color* but never its varying value — and a `StatusIndicator` carries one colour per state, so
  `ColorHash` is refused for it and only its bounds can be pinned — the golden reference says **where** critical content
  must appear and in what tint, not what the live number is.

## Checking a file without a full build

```console
mdux-medui-check path/to/screen.medui [--format=json|text]
```

Validates one file and writes nothing. Exits non-zero when anything of error severity was found, so
it drops into a pre-commit hook or an agent loop as it stands. `--format=json` emits the same
envelope every MduX tool emits (#118), so a finding is `{file, line, column, code, severity,
message, fixHint}` rather than prose to parse.

**What it checks:** the grammar, the closed component dictionary, each field's value domain,
hardcoded strings in localizable fields, theme tokens against the governed table, the
`@safety_critical` rules including an unknown `cv_check` — and, when the file declares a `surface:`,
bounded layout with its overflow and containment rules plus the golden set that follows.

**What it cannot check, and says so.** Text keys and text budgets need the approved locales a recipe
names, and a single file names none. Rather than reporting every `t("STR-KEY")` as absent from every
locale — a vacuous truth dressed as a finding — the checker skips those and emits a note, `MDC001`.
A screen with no `surface:` gets `MDC002` for the same reason: layout, overflow and golden bounds
went unchecked. Notes do not fail the run; they are what makes a clean result visibly *partial*
rather than silently so.

For the two it cannot cover, compile the screen through `mdux-meduic` with a recipe carrying a
`[text]` table: that checks every key against every approved locale, and every box against the
widest translation of the text it holds.
