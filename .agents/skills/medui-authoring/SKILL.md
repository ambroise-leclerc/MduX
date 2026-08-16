---
name: medui-authoring
description: Use when authoring or reviewing a .medui screen, or discussing MduX's planned UI-description language — the grammar, component dictionary, theme tokens, text budgets, and when @safety_critical / requirement: are mandatory.
---

# MduX `.medui` authoring

Companion to § 3 ("Verified architecture summary") of [`AGENTS.md`](../../../AGENTS.md). This skill
governs the *authoring* of a `.medui` screen; for the baking mechanics behind it see
`evidence-pipeline`, and for the compliance framing of a safety-critical node see
`regulatory-citations`.

## Status: parsing, semantic validation, bounded layout, and text budgets are implemented; emission and runtime are not

**A `.medui` lexer, AST, parser, semantic analyzer, integer-only bounded layout solver, and
text-budget check exist** in the host-tools zone (`tools/medui/`). The diagnostic codes they emit
are registered as `MEDUI-E###` (#191). The parser rejects structural violations (#192); the
analyzer checks the closed component dictionary, each field's value domain, theme-token names, and
text-key presence across every approved locale (#193); the solver flattens Vertical/Row layout to
absolute rectangles without floating-point arithmetic (#194); and the budget check measures each
resolved box against the widest approved translation and each named dynamic-text source against the
font package's restricted charset (#195). The AST keeps names unresolved.

**There is still no compiler, no emitter and no runtime.** The HTML/CSS path that used
to stand in for one - `UiFileWatcher::loadContent()`, which sniffed a file extension and stored
the file as a string, with no parsing, layout or rendering behind it - was deleted by
[issue #127](https://github.com/ambroise-leclerc/MduX/issues/127).

What exists now is the layer a `.medui` compiler will target: `mdux.draw` describes a frame as
bounded vertex, index and command buffers with a compiler-computed budget, and
`mdux.render.vulkan` renders one. So the compiler's job is to emit a `DrawList` and a
`DrawBudget`, not to invent a rendering path. `.medui` itself is
[issue #15](https://github.com/ambroise-leclerc/MduX/issues/15). The implementation pins the shared
contract in `medui-conformance.toml`. This skill records MduX status and integration; canonical
grammar, component semantics, diagnostics, and portable guidance live in
[`Compliatory/MedUI` at `d5136a8`](https://github.com/Compliatory/MedUI/tree/d5136a8518bd499760ecff2aad215d3721329f20).
Do not write a `.medui` file expecting it to build anything in MduX until the compiler waves land.

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
reference (`node_id`, `bounds`, `text_key?`, `color_token?`, `cv_checks`) that the rendered-truth
verifier checks against. Rules:

- **A safety-critical component must carry an explicit `requirement:`.** If you're annotating a
  node with `@safety_critical` and it has no `requirement:` field, something is wrong — either the
  annotation or the missing requirement.
- **Any node with an explicit `position:` gets an automatic `Bounds` golden reference**, even
  without `@safety_critical` — a declared position is a safety-relevant claim by itself.
- **A node with both gets exactly one merged entry** (deduplicated `cv_checks`), never two.
- Dynamic content (`NumericDisplay`, `StatusIndicator`, `Clock`, `SignalTrace`) pins its *bounds*
  and *color* but never its varying value — the golden reference says **where** critical content
  must appear and in what tint, not what the live number is.

## Checking a file without a full build

`mdux-medui-check path/to/screen.medui` (issue #200) will validate a single file and print
diagnostics — once it exists. The parser, analyzer, solver, and budget check it will call already
detect syntax errors, structural violations, unknown dictionary/theme names, hardcoded text in
localizable fields, wrong field value domains, incomplete locale keys, layout overflow, a box that
cannot contain its widest approved translation, and a dynamic-text source that could escape the
baked charset; what is missing is the command that exposes them. Until it lands, review a `.medui`
file by hand against this grammar and the component table above.
