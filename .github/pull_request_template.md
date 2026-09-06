<!--
Every section is required. Where a field does not apply, write "none" or "not stacked" rather
than deleting the heading — a missing heading reads as an oversight, an explicit "none" reads as
an answer. See CONTRIBUTING.md § "Stacked delivery" for what the dependency fields are for.
-->

## Summary

Describe the change and why it is needed.

Closes #

## Invitation and contributor agreement

- [ ] I was invited by a maintainer to contribute this change.
- [ ] I accepted the repository's [Contributor Licence Agreement](../CLA.md) in the GitHub issue identified below.

Invitation / CLA issue: #

## Dependency

- **Base branch:** <!-- `develop`, the predecessor branch this is stacked on, or `master` only for a same-repository `release/vX.Y.Z` branch -->
- **Predecessor PR:** <!-- #NNN, or "not stacked" -->
- **Merge order:** <!-- "mergeable now", or "must not merge until #NNN has merged" -->

<!--
A stacked PR targets its predecessor rather than `develop`, so a reviewer sees one issue's diff
instead of the cumulative one. It must not merge until its predecessor has.
-->

## Shared registries touched

Tick every registry this PR edits. These are the files two branches collide in, and where Wave 2
lost wiring during conflict resolution:

- [ ] `CMakeLists.txt` (root) — targets, trust-zone declarations, install/export set
- [ ] `FILE_SET CXX_MODULES` lists — a module interface added, removed or moved
- [ ] `tools/CMakeLists.txt` — host-tool targets and their sources
- [ ] `tests/CMakeLists.txt` — test source lists and suite membership
- [ ] Schemas under `docs/*/schemas/` or `docs/governance/schemas/`
- [ ] Generated indexes (`AI-Reference.{md,json}`, per-clause indexes)
- [ ] Committed artifacts under `generated/`
- [ ] None of the above

## Rebase state

- **Rebased onto:** <!-- the `develop` SHA this branch sits on, or the predecessor's head SHA -->
- **Conflicts resolved as a union:** <!-- yes / no conflicts. If a conflict was resolved by taking one side, say which and why. -->

## Verification

List the checks performed and their results. Do not tick anything you did not run — "not run
locally, CI covers it" is an acceptable answer and a more useful one than a wrong tick.

- [ ] `cmake --preset ninja-gcc && cmake --build build-gcc`
- [ ] `ctest --test-dir build-gcc` — <!-- N/N passed -->
- [ ] `python3 tools/docs-lint/mdux_docs_lint.py`
- [ ] Other: <!-- sanitizers, evidence re-bake, install consumer, MSVC leg -->

## Post-merge gate

- [ ] I understand that the `develop` workflow must be green after this merges before the next
      dependent PR may merge.
- **Post-merge `develop` run:** <!-- workflow URL or run ID once this has merged, or "no successor" -->
- [ ] That run is green. *(Tick after merging this PR and before merging its successor. "No
      successor" above means there is nothing to gate, and this box may be left unticked.)*

<!--
The first box records that the author knows the rule; the second records that the rule was
followed. They are not redundant - a successor could otherwise merge against an unverified
integration while the template looked complete, which is the gap this section exists to close.
-->


## Regulatory impact

<!--
Required by AGENTS.md § 8. If this change can affect safety behaviour, risk controls, compliance
metadata, traceability, or any claim about a standard, say so and name the artifacts updated.
"None — <reason>" is a complete answer for changes that carry no such impact.
-->
