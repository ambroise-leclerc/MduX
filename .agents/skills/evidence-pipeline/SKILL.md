---
name: evidence-pipeline
description: Use when adding or modifying a baked asset (font, shader, image, .medui screen, ML model) or anything under generated/ — the recipe-to-baker-to-committed-artifact doctrine, the canonical JSON rules that make byte-identity possible, and why generated/ is never hand-edited.
---

# MduX evidence pipeline

Companion to § 4 ("Repository map") of [`AGENTS.md`](../../../AGENTS.md). This skill governs the
*baked-artifact* discipline; for `.medui` authoring specifically see `medui-authoring`, and for the
regulatory framing of an evidence artifact see `regulatory-citations`.

## Status: six bakers, eight committed artifacts, byte-compared on four toolchains

This section said "no artifact is committed yet" and named issue #120 as what would change that.
That was true when it was written and has not been for some time; the tree is the authority and it
now holds:

- The evidence kernel — `mdux.evidence.digest`, `mdux.evidence.json`, `mdux.evidence.report` —
  and `cmake/MduXBake.cmake`, which defines `mdux_bake_artifact()`, the `mdux-bake-all` and
  `mdux-bake-update` targets, and the `evidence`-labelled comparison test (#12).
- Six bakers: `mdux-shaderbake`, `mdux-textbake` (which bakes both font and text packages),
  `mdux-imagebake`, `mdux-mlbake` and `mdux-meduic`, with `mdux-verify-bake` extending a screen's
  report with rendered-truth outcomes.
- Eight committed artifacts under `generated/` — two shader packages, one font, one text, one
  image, two ML models and one screen — each with a recipe under `recipes/`, and each re-derived
  and byte-compared by `ctest -L evidence` on every automatic CI leg.

So there *is* a `generated/<kind>/<id>/` to copy, and copying one is the fastest way to see the
shape a baker has to produce.

## The resolved option set is published, and checked

Each kind's resolved options — every default expanded — are documented as
`docs/recipes/<kind>.schema.json` (#264). Two things to know before adding a baker or an option:

- **The schema documents the report, not the recipe.** ADR-007 decision 4 is why: a silently
  changed default must not leave every report looking unchanged, so `report.json`'s `options`
  records what the bake actually did, and the schema describes that. A schema of the literal TOML
  would document a different thing from what the report names.
- **Adding an option means editing the schema in the same change.**
  `tools/docs-lint/check_schema_type_drift.py` validates every committed report against its kind's
  schema in both directions — an option a baker records that the schema does not declare, and a
  property the schema declares that no report carries, are both drift. It runs on the docs-only CI
  job, so it fails on a pull request that touches no C++ as readily as on one that does.

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
