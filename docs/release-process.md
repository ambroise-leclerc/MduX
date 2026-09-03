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
  NNN-slug ──▶ develop ──▶ release/vX.Y.Z ──▶ master  (tagged vX.Y.Z)
                  ▲                              │
                  └───────────── back-merge ─────┘
```

A working branch is `<issue-number>-<slug>` — the scheme GitHub's "create a branch for this issue"
button generates, and the one every workflow's `branches:` filter matches (`'[0-9]+-*'`, plus
`'feat/**'` for branches that predate the convention). A branch named anything else gets **no checks
at all**, silently, which is worse than a failing one.

- **`develop`** integrates. Every change arrives by pull request, and a stack of PRs is based on its
  predecessor rather than on `develop` — see [`AGENTS.md`](../AGENTS.md) and issue `#117`.
- **`release/vX.Y.Z`** exists so that release-only changes are reviewable as a diff. It is where the
  version moves and the artifacts are re-baked, both of which touch files no feature branch should.
- **`master`** carries releases and nothing else. Its tip is the most recent tag. Branch protection
  rejects direct pushes, and the `Branch Topology` check accepts only same-repository
  `release/vX.Y.Z` pull requests targeting it.

A release branch rather than tagging `develop` directly, for one reason that is specific to this
repository: **the version bump changes committed artifacts**, so it needs review like any other
change to `generated/`. Tagging `develop` would put that change in the same commit stream as feature
work.

## The step that surprises everyone once

`report.json` records `toolVersion`, which is the project version the baker was built from. Seven
artifacts carry it today:

```console
$ grep -rl toolVersion generated/ | wc -l
7
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
$ git switch develop && git pull --ff-only    # fails rather than merging, if local has drifted
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
$ cmake --preset <your-preset>
$ cmake --build --preset <your-preset>
$ ctest --preset <your-preset> -L evidence
$ git diff --stat generated/
```

Run the update, configure, build and evidence-test sequence to a **fixpoint**: repeat it until the
evidence tests pass and an update pass reports that every artifact is already up to date. Some
artifacts consume other committed artifacts, but their independent `-update` targets have no
execution order. A version bump therefore needs successive passes to propagate the new upstream
package digests through text and screen reports. [`MduXBake.cmake`](../cmake/MduXBake.cmake) records
the mechanism in detail. Do not treat the first clean-looking diff as the end of the update.

**Every changed file must differ only in `toolVersion`.** Anything else — a digest, a payload byte,
a member — means the release is carrying an unreviewed change to a baked artifact, and the release
stops until that is explained. This is the check §8.3 asks for, in the form this repository can
actually perform.

```console
$ git diff generated/ | grep '^[-+]' | grep -v toolVersion | grep -v '^[-+][-+]'
```

That command should print nothing.

#### When you cannot run the bake

This tree requires GCC 16 or MSVC with `import std`, and a preparer may have neither. The fallback is
**bounded, and only this**: when the release changes no recipe, no input and no option — a pure
version bump — the expected bytes are derivable by inspection, because `toolVersion` is the only
field that moves. Write it, and let the release be gated on `evidence.<kind>.<id>` passing on **both**
toolchain legs, which re-bakes every artifact from its recipe and byte-compares.

Two things about that are worth being precise on, because it is a deviation from "run the baker":

- **It is verification, not trust.** Two independent toolchains re-deriving the same bytes is a
  stronger check than one unverified local bake. ADR-007 anticipated the case: it lists a hand-edited
  file under `generated/` as a risk whose mitigation is that "re-baking overwrites the edit while the
  byte-comparison fails the PR" — which is exactly the gate this leans on.
- **It does not extend to anything else.** If any recipe, source or resolved option changed, the
  bytes are not derivable by inspection and this fallback does not apply: find a supported toolchain.
  A release that used it for more than a version bump would be asserting an artifact nobody derived.

Record in the release PR that the fallback was used, so the next reviewer knows which of the two
paths produced the bytes they are looking at.

### 5. Finish the changelog entry

[`CHANGELOG.md`](../CHANGELOG.md)'s top entry moves from `unreleased` to the date. Its **known
limits** section is not optional: §5.8 asks for the anomalies known at release, and a release note
that omits them describes a different release. If a limit was discovered after the entry was
drafted, it goes in now.

### 6. Update the roadmap

The wave line moves from "in progress" to "shipped vX.Y.Z", the counts move, and the next wave opens.
So do the provenance header and footer, which name the commit the status was verified at — a tagged
roadmap whose banner names an older baseline contradicts itself in the one document a reader consults
to find out what shipped.

This is on the release branch, before the merge, and the ordering is the point: a roadmap updated
after the tag leaves the tag pointing at a tree that still calls its own wave "in progress", and
fixing it afterwards means re-tagging.

### 7. Run the full suite locally if you can, and let CI decide

```console
$ ctest --preset <your-preset> -L evidence      # the byte comparisons, both legs in CI
$ ctest --preset <your-preset>
```

The `evidence` label is the one that matters here: it is the mechanical answer to "is the committed
artifact the one this source produces".

### 8. Merge to `master`, tag there, and publish the release

```console
$ gh pr create --base master --head release/vX.Y.Z --title "Release vX.Y.Z"
# after review and a green run - --merge, never --squash, see step 9:
$ gh pr merge <pr> --merge
# then, without checking master out:
$ git fetch origin
$ git tag -a vX.Y.Z -m "MduX vX.Y.Z" origin/master
$ git push origin vX.Y.Z
$ gh release create vX.Y.Z --verify-tag --title "vX.Y.Z — Wave N: <what it is>" --notes-file <notes>
```

**A green check is not evidence that the CI ran.** GitHub does not run `pull_request` workflows
when it cannot construct a merge commit, so an unmergeable release PR can display a passing review
bot while every build, test and lint check is absent. Do not merge or tag from that state. Every
status check required by `master` must be present and successful before you merge. Branch
protection keeps an absent required check pending and blocks the merge. Resolve the conflict and
wait for every required check to report success. That protection is also `strict`, so a release
branch behind `master` must absorb it before the merge button unblocks — normally moot, because a
release branch descends from `develop` and `develop` back-merged `master` at step 9, but not on a
hotfix or a second release opened while one is still in flight.

Four things this spells out because cutting v0.6.0 found each of them the hard way.

**Merge with `--merge`, mechanically.** The reason is in step 9, but the choice is made *here*, and
the web UI remembers whichever method was used last — which is precisely how v0.6.0 was squashed
against the intended topology. Naming the method on the command line does not depend on anyone
recalling a warning further down this page.

**Tag `origin/master`, not a checked-out `master`.** A local `master` in a long-lived clone can be
hundreds of commits from the real one — 462, in the clone this procedure was written in — so
`git switch master && git pull` attempts a merge rather than a fast-forward. Tagging the remote ref
needs no checkout and cannot pick up a stale branch.

**The tag is annotated.** A lightweight tag records no author and no date of its own, which is a poor
fit for an object meant to identify a configuration.

**Publish a GitHub release too.** Every wave release from v0.2.0 onward has one, titled
`vX.Y.Z — Wave N: <subject>`; the `v0.1` tag predates the convention and has none. The tag alone is
not what a reader lands on. Its notes are the
changelog entry's highlights *and its known-limits section* — §5.8's anomalies belong where someone
reads them, not only in a file they might open.

### 9. Back-merge, so the histories do not drift

```console
$ git switch develop && git pull --ff-only && git merge --no-ff origin/master && git push
$ git rev-list --left-right --count origin/master...origin/develop
0	34
```

The left column must be `0`: `master` carries nothing `develop` lacks. Skipping this step is how that
number grows. `--ff-only` is the same guard as step 1 — if local `develop` has drifted or carries an
unpushed commit, the pull must fail rather than quietly add a merge, because an extra merge here is
indistinguishable in the final count from the drift this step exists to measure.

[`branch-topology.yml`](../.github/workflows/branch-topology.yml) enforces that invariant on every
push to `master` or `develop`, and once a day. Deliberately not on a pull request: the counts come
from the branch tips, which no PR can move, so a PR would report the state of the branches around it
and stay red until someone with push access to `develop` acted. If the check reports commits unique
to `master`, back-merge `origin/master` into `develop` before continuing feature or release work; do
not make a duplicate change independently on both branches.

**Merge the release PR, do not squash it.** A squash gives the release commit a single parent, so no
release-branch commit is in `master`'s ancestry, and the back-merge falls back to the last common
commit — for v0.6.0, the *previous* release. Everything both sides changed since then conflicts:
four files, three of them `add/add` (files neither side had at that point) and one an ordinary
content conflict. Not every touched file — `CMakeLists.txt` merged cleanly. The conflicts are noise
the merge button creates and a real merge avoids.

If it was squashed anyway, do **not** resolve globally to `master`. That is only right when the
squashed delta is a superset of `develop`'s — true for v0.6.0 because nothing landed on `develop`
while the release PR was open, and not guaranteed by anything in this procedure. Take it per path, against the commit the release branched from. A squash-merged PR's branch is
usually deleted, but GitHub keeps its head, so the branch point is always recoverable:

```console
$ git fetch origin refs/pull/<pr>/head
$ base=$(git merge-base FETCH_HEAD origin/develop)
$ git log --oneline $base..origin/develop -- <path>
```

Empty means `develop` did not touch the path after the release branched, so the conflict is the
squash alone and `master`'s side is the whole content:

```console
$ git checkout --theirs -- <path> && git add -- <path>
$ git diff --quiet --cached origin/master -- <path>   # what is staged is what master has
```

Non-empty means two real changes collided, and the merge is an ordinary one: keep both sides. For the
shared registries — the diagnostic codes, the roadmap tables — that is the same union-by-default rule
that applies to every other merge in this repository. If the two cannot be reconciled by reading
them, stop and decide it explicitly; a release is the worst place to let a resolution rule delete
work silently.

## What is deliberately not automated

No workflow cuts this release. Three of the steps above are judgement calls a script cannot make:
whether an epic is genuinely closed, whether an artifact diff that is not only `toolVersion` is
acceptable, and whether the known-limits section is honest. A release button that skipped them would
be recording a decision nobody made.

What *is* automated is everything mechanical: branch containment, the byte comparisons, the
trust-zone check, the governed-source lints, the no-heap scans and the pixel tests all run on every
mergeable pull request, including the release one.

## Two things in this history that will confuse you

**`v0.2.0` is not an ancestor of `v0.3.0`.** The symmetric difference is 462 commits against 475.
That is not a mistake: issue `#23` purged reproduced normative text from git history, so everything
before the rewrite sits on a line the current history does not contain. Tooling that walks tag ranges
has to special-case that boundary; every later one is linear.

**Check `origin/master`, never a local `master`.** A local `master` in a long-lived clone can predate
the purge and sit hundreds of commits from the real one. An earlier revision of
[`roadmap.md`](roadmap.md) claimed "develop is 668 commits ahead of master" for exactly that reason —
a real number about the wrong pair of refs.
