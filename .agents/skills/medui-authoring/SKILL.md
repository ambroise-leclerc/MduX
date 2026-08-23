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

One limit is worth knowing before you write a screen: the runtime draws a `Panel`; every other
component, a `Label` included, is visited, counted in `FrameStats::deferred` and left undrawn.
Drawing text means joining a locale-free compiled screen to a text package for the locale the device
is running, which is [#17](https://github.com/ambroise-leclerc/MduX/issues/17); live-data components
have no geometry until the frame does.

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
[`Compliatory/MedUI` at `d5136a8`](https://github.com/Compliatory/MedUI/tree/d5136a8518bd499760ecff2aad215d3721329f20).
A `.medui` file builds something in MduX today, and it reaches the screen: register it with
`mdux_compile_screen()` and it becomes a committed, byte-compared artifact plus generated C++ a
device links, which the governed runtime draws and `ScreenPixelTests` compares pixel by pixel under
lavapipe. It carries text too, since #235: a `t("STR-KEY")` compiles, and the box holding it is
measured against every approved locale's widest translation. What it cannot yet do is *draw* that
text, for the reason above.

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
  and *color* but never its varying value — the golden reference says **where** critical content
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
