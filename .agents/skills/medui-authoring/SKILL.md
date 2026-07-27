---
name: medui-authoring
description: Use when authoring or reviewing a .medui screen, or discussing MduX's planned UI-description language — the grammar, component dictionary, theme tokens, text budgets, and when @safety_critical / requirement: are mandatory.
---

# MduX `.medui` authoring

Companion to § 3 ("Verified architecture summary") of [`AGENTS.md`](../../../AGENTS.md). This skill
governs the *authoring* of a `.medui` screen; for the baking mechanics behind it see
`evidence-pipeline`, and for the compliance framing of a safety-critical node see
`regulatory-citations`.

## Status: planned, not yet implemented

**There is no `.medui` parser, compiler, or runtime in MduX today.** The current UI story is
`UiFileWatcher::loadContent()` in `include/mdux/mdux.cppm` / `src/mdux.cpp`, which sniffs a file
extension and stores the file as a string — no parsing, no layout, no rendering. That path is
being retired, not extended (tracked in
[issue #13](https://github.com/ambroise-leclerc/MduX/issues/13), S9). `.medui` itself is
[issue #15](https://github.com/ambroise-leclerc/MduX/issues/15). This skill describes the *target*
grammar, adapted from the sibling Rust project TrustSC, so authoring work can start the moment the
compiler exists — do not write a `.medui` file expecting it to build anything yet.

## Grammar shape

```
Screen NeuroSense500 {
    layout: Vertical { spacing: 8px; padding: 0px; }
    surface: 1920px, 1080px;

    @safety_critical(cv_check: [Bounds, ColorHash])
    NumericDisplay {
        id: sedation-index;
        width: 512px; height: 512px;
        position: 1392px, 80px;
        requirement: "REQ-NS-001";
        template: "TPL-SEDATION-INDEX-160";
        source: "SEDATION_INDEX";
        color: Theme.Colors.ScoreDigits;
    }
}
```

- Sizes are `Npx` or `Fill`. `position: Xpx, Ypx` takes a node out of flow at exact pixel coords.
- `Row { id; height; background?; spacing? }` is a single-level horizontal group, flattened at
  compile time — it cannot nest another `Row`.
- Text is **always** `t("STR-KEY")` against an approved text package. Hardcoded strings are a
  compile error by design — the compiler validates every key against **every** approved locale and
  rejects a component whose bounds are narrower than the widest approved translation.
- Colors are `Theme.Colors.<Token>` from a governed table. An unknown token is a compile error.
- Forbidden, deliberately: loops, conditionals, recursion, runtime scripting, deep nesting.

## Component dictionary (target)

| Component | Required fields | Notes |
|---|---|---|
| `CriticalButton` | `id, requirement, width, height, label, color, on_press` | always needs `requirement:` |
| `Button` | `id, width, height, label, color, source` | `requirement:` optional |
| `VulkanViewport` | `id, width, height, stream_source` | |
| `SignalTrace` | `id, width, height, stream_source, color` | scrolling waveform |
| `NumericDisplay` | `id, width, height, requirement, template, source, color` | live value, restricted charset |
| `StatusIndicator` | `id, width, height, requirement, source, states` | `states: [t(...), ...]` |
| `Label` | `id, width, height, text, color` | |
| `Clock` | `id, width, height, format` | |
| `Image` | `id, width, height, source` | `source: img("ID")` |
| `TextInput` | `id, width, height, source, max_length, color` | display + caret only, no IME |

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

`mdux-medui-check path/to/screen.medui` (issue #15, S11) will validate a single file and print
diagnostics — once it exists. Until then, review a `.medui` file by hand against this grammar and
the component table above; there is no tooling shortcut yet.
