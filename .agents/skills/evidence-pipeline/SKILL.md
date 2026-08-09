---
name: evidence-pipeline
description: Use when adding or modifying a baked asset (font, shader, image, .medui screen, ML model) or anything under generated/ — the recipe-to-baker-to-committed-artifact doctrine, the canonical JSON rules that make byte-identity possible, and why generated/ is never hand-edited.
---

# MduX evidence pipeline

Companion to § 4 ("Repository map") of [`AGENTS.md`](../../../AGENTS.md). This skill governs the
*baked-artifact* discipline; for `.medui` authoring specifically see `medui-authoring`, and for the
regulatory framing of an evidence artifact see `regulatory-citations`.

## Status: infrastructure and the first baker exist; no artifact is committed yet

What is in the tree today:

- The evidence kernel — `mdux.evidence.digest`, `mdux.evidence.json`, `mdux.evidence.report` —
  and `cmake/MduXBake.cmake`, which defines `mdux_bake_artifact()`, the `mdux-bake-all` and
  `mdux-bake-update` targets, and the `evidence`-labelled comparison test. Issue
  [#12](https://github.com/ambroise-leclerc/MduX/issues/12) is complete.
- `mdux-shaderbake` (`tools/shader/`), the first baker, from issue
  [#119](https://github.com/ambroise-leclerc/MduX/issues/119). It is the worked example to copy
  when writing the font, image, `.medui` and ML bakers.

What is **not** in the tree yet: any `recipes/` or `generated/` directory. `mdux_bake_artifact()`
has no call sites, so `ctest -L evidence` currently selects no tests. Issue
[#120](https://github.com/ambroise-leclerc/MduX/issues/120) commits the first artifact and is what
makes the CI byte-comparison start doing work.

So: the doctrine below is live and the machinery is real, but if you are looking for an existing
`generated/<kind>/<id>/` to copy, there is not one yet — copy the tool instead.

## The pattern

```
recipes/<kind>/<id>.toml  +  assets/…   ──[ mdux-<kind>bake ]──▶  generated/<kind>/<id>/
                                                                    package.json   (metadata, canonical)
                                                                    report.json    (digests, tool, options)
                                                                    payload.bin    (bulk binary sidecar)
```

A host-only baker tool (`tools/<kind>/`, never linked into `MduXCore` or `MduX`) consumes a recipe
and source assets, and produces **committed** artifacts. CI re-runs the baker in `verify` mode and
asserts byte-identity against what's committed. Runtime builds never invoke the baker.

## Rules that make byte-identity possible

- **Canonical JSON**: keys sorted, 2-space indent, LF, UTF-8 no BOM, no timestamps, no absolute
  paths. **Floats are encoded as `u32` bit patterns, never decimal text** — `printf("%.9g")` is not
  guaranteed byte-identical across MSVC, glibc, and libc++, and this pipeline crosses all three.
- **Bulk binaries go in a sidecar `payload.bin`**, not base64 inside `package.json` — this is a
  deliberate deviation from TrustSC (which bases64-encodes into the JSON); committing megabytes of
  base64 makes git history unusable. `package.json` carries the sidecar's SHA-256.
- **`report.json`'s `options` field is the fully resolved set with defaults expanded**, not the
  recipe's literal contents — otherwise a default change silently changes output while every report
  still looks unchanged.

## The source-tree rule

A normal build **never writes into the source tree**. It bakes into the build directory and
compares. `cmake --build build --target mdux-bake-update` is the only path that copies build-dir
artifacts over `generated/` — run it deliberately, then commit the diff and review it like any
other change. If you find yourself hand-editing a file under `generated/`, stop: re-run the
matching baker instead.

## Verifying

```sh
ctest --test-dir build -L evidence --output-on-failure
git status --porcelain   # must be empty after a build
```

Run on **both** the Windows/MSVC and Linux/GCC CI legs — cross-toolchain byte-identity is the whole
point, and it is a stronger determinism claim than a single-compiler check would be. Do not "fix" a
flaky evidence test by dropping a leg; find the toolchain-specific divergence (usually a float
formatted as decimal text somewhere in the baker) instead.
