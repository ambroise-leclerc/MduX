# Releasing MduX

How a version gets cut, and why each step is a step rather than a habit. Written because the
procedure was inferable from the repository and nowhere stated — `v0.5.0` peels to `origin/master`'s
tip, so tags evidently land on `master`, but nothing said who moves the version, in what order, or
what must be true before the tag exists.

For a medical-device SDK a release is not a convenience. IEC 62304 §5.8 asks a manufacturer to know
precisely what is being released — every included item, its known anomalies, and the configuration it
was built from — and §8.3 asks that changes to that composition be controlled. This repository can
answer both mechanically, because every baked artifact carries a `report.json` recording its inputs
by digest and CI re-derives it on two toolchains. What follows is the procedure that turns those
mechanisms into a release.

## The branch topology

```text
  feature/NNN-slug ──▶ develop ──▶ release/vX.Y.Z ──▶ master  (tagged vX.Y.Z)
                          ▲                              │
                          └───────────── back-merge ─────┘
```

- **`develop`** integrates. Every change arrives by pull request, and a stack of PRs is based on its
  predecessor rather than on `develop` — see [`AGENTS.md`](../AGENTS.md) and issue `#117`.
- **`release/vX.Y.Z`** exists so that release-only changes are reviewable as a diff. It is where the
  version moves and the artifacts are re-baked, both of which touch files no feature branch should.
- **`master`** carries releases and nothing else. Its tip is the most recent tag.

A release branch rather than tagging `develop` directly, for one reason that is specific to this
repository: **the version bump changes committed artifacts**, so it needs review like any other
change to `generated/`. Tagging `develop` would put that change in the same commit stream as feature
work.

## The step that surprises everyone once

`report.json` records `toolVersion`, which is the project version the baker was built from. Six
artifacts carry it today:

```console
$ grep -rl toolVersion generated/ | wc -l
6
```

So **bumping the version in `CMakeLists.txt` invalidates every committed artifact**. Until they are
re-baked, every `evidence.<kind>.<id>` test fails on both toolchain legs — not because anything is
wrong, but because the reports describe a baker that no longer exists.

That is the ordering constraint the whole procedure turns on: bump, re-bake, review the artifact
diff, *then* tag. Re-baking before the bump produces reports that are stale the moment the version
moves.

## The procedure

### 1. Confirm `develop` is releasable

```console
$ git switch develop && git pull
$ gh pr list --state open --base develop      # nothing half-landed
$ gh run list --branch develop --limit 1      # green on both toolchain legs
```

Every epic the release claims must be closed on GitHub, not merely merged. A wave that reads "12/12"
in [`roadmap.md`](roadmap.md) and has an open child issue is a release note that overstates.

### 2. Open the release branch

```console
$ git switch -c release/v0.6.0
```

### 3. Move the version, in the one place it is *defined* and the three that assert it

`CMakeLists.txt`'s `project(MduX VERSION X.Y.Z ...)` is the single source: `MDUX_TOOL_VERSION` flows
from it into every baker and therefore into every `report.json`, and `mdux::Version` from it into
every consumer.

Three tests then assert the value, and they have to move with it:

```console
$ grep -rn 'Version::minor ==\|Version::getString() ==' tests/
tests/TestVersion.cpp:               2 assertions
tests/TestRegulatoryCompliance.cpp:  1 assertion
```

They hard-code the number on purpose, and should keep doing so. `IEC 62304 Version Traceability`
exists to check that the version a build *reports* is the version the project *declares*; derived
from the same macro it would assert nothing. An earlier revision of this document said "nothing else
needs editing" — cutting v0.6.0 found that out, three red tests into a green-looking release.

### 4. Re-bake every artifact, and read the diff

```console
$ cmake --preset <your-preset>
$ cmake --build --preset <your-preset> --target mdux-bake-update
$ git diff --stat generated/
```

**Every changed file must differ only in `toolVersion`.** Anything else — a digest, a payload byte,
a member — means the release is carrying an unreviewed change to a baked artifact, and the release
stops until that is explained. This is the check §8.3 asks for, in the form this repository can
actually perform.

```console
$ git diff generated/ | grep '^[-+]' | grep -v toolVersion | grep -v '^[-+][-+]'
```

That command should print nothing.

### 5. Finish the changelog entry

[`CHANGELOG.md`](../CHANGELOG.md)'s top entry moves from `unreleased` to the date. Its **known
limits** section is not optional: §5.8 asks for the anomalies known at release, and a release note
that omits them describes a different release. If a limit was discovered after the entry was
drafted, it goes in now.

### 6. Run the full suite locally if you can, and let CI decide

```console
$ ctest --preset <your-preset> -L evidence      # the byte comparisons, both legs in CI
$ ctest --preset <your-preset>
```

The `evidence` label is the one that matters here: it is the mechanical answer to "is the committed
artifact the one this source produces".

### 7. Merge to `master` and tag there

```console
$ gh pr create --base master --head release/v0.6.0 --title "Release v0.6.0"
# after review and a green run:
$ git switch master && git pull
$ git tag -a v0.6.0 -m "MduX v0.6.0"
$ git push origin v0.6.0
```

The tag is annotated, and it names the commit on `master`. A lightweight tag records no author and no
date of its own, which is a poor fit for a document that is meant to identify a configuration.

### 8. Back-merge, so the histories do not drift

```console
$ git switch develop && git merge --no-ff master && git push
```

Skipping this is how `master` accumulates commits `develop` does not have. As of this writing
`origin/master...origin/develop` is `1 32` — master carries one commit develop does not, which is
exactly the drift this step prevents.

### 9. Update the roadmap — on the release branch, with the rest

The wave line moves from "in progress" to "shipped vX.Y.Z", the counts move, and the next wave opens.
That file is the programme's own account of what shipped when, and the CHANGELOG's earlier entries
rest on it.

This belongs in the release branch rather than after the tag, which is what cutting v0.6.0 taught:
the tagged tree should describe itself. A roadmap updated afterwards leaves the tag pointing at a
tree that still calls its own wave "in progress".

## What is deliberately not automated

No workflow cuts this release. Three of the steps above are judgement calls a script cannot make:
whether an epic is genuinely closed, whether an artifact diff that is not only `toolVersion` is
acceptable, and whether the known-limits section is honest. A release button that skipped them would
be recording a decision nobody made.

What *is* automated is everything mechanical: the byte comparisons, the trust-zone check, the
governed-source lints, the no-heap scans and the pixel tests all run on every pull request, including
the release one.

## Two things in this history that will confuse you

**`v0.2.0` is not an ancestor of `v0.3.0`.** The symmetric difference is 462 commits against 475.
That is not a mistake: issue `#23` purged reproduced normative text from git history, so everything
before the rewrite sits on a line the current history does not contain. Tooling that walks tag ranges
has to special-case that boundary; every later one is linear.

**Check `origin/master`, never a local `master`.** A local `master` in a long-lived clone can predate
the purge and sit hundreds of commits from the real one. An earlier revision of
[`roadmap.md`](roadmap.md) claimed "develop is 668 commits ahead of master" for exactly that reason —
a real number about the wrong pair of refs.
