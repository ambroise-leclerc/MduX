# ADR-009: In-repository test framework, and SpecLab for BDD

## Status
Accepted (2026-08-03). Supersedes ADR-002.

## Context
ADR-002 selected Catch2 v3 and was never implemented. It has sat at `Proposed` while the repository
grew two testing surfaces that owe nothing to it, so the authoritative decision trail says the
project uses a framework it has never linked. That is the specific problem this ADR closes.

What actually exists on `develop`:

- **`tests/framework/MduXTest.cppm` + `MduXTest.hpp`** — a small in-repository framework used by nine
  test executables (`core_tests`, `evidence_tests`, `tools_tests`, `unit_tests`, `compliance_tests`,
  `render_tests`, `offscreen_tests`, `vulkansc_memory_tests`, `vulkansc_object_tests`).
- **[SpecLab](https://github.com/ambroise-leclerc/SpecLab)** — a Given/When/Then BDD library, pinned
  by commit in `tests/CMakeLists.txt` and fetched with CPM, used by seven spec executables
  (`shader_spec`, `draw_spec`, `tools_spec`, `bridge_spec`, `ml_spec`, `ml_tools_spec`,
  `ml_noheap_spec`).
- **`cmake/MduXTestDiscovery.cmake`** — the discovery contract both surfaces present to CTest.

The BDD requirement ADR-002 identified was real; the conclusion that it needed Catch2 was not.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)
- **A test framework is software the project depends on**, and ADR-002's status left it ambiguous
  which one that was. An auditor reading `docs/adr/` would have concluded Catch2.
- **Traceability is served by the discovery contract, not by the framework.** Every test is a
  separate CTest entry with labels, so a failing requirement names itself rather than naming a
  binary — see the discovery section below.

### Risk management considerations
- A framework linked into test binaries never reaches a device, so its defects cannot injure a
  patient directly. The risk it carries is **false confidence**: a framework that silently skips or
  miscounts turns an absent test into a passing one. Both surfaces are therefore covered by their
  own tests (`bridge_spec` exists solely to test the SpecLab integration).

### Traceability requirements
- One CTest entry per scenario, with labels (`evidence`, `evidence-unit`, `determinism`, `noheap`,
  `pixel`, `regulatory`) that CI steps select on, so a claim of the form "the byte-identity checks
  ran" is answerable by a command rather than by reading a log.

## Decision

### 1. Keep `MduXTest` in the repository
A zero-SOUP library should not depend on a test framework it would itself have to qualify. The
in-repository framework is a few hundred lines, has no dependencies, and is compiled by the same
toolchain as the code under test.

This is the same reasoning ADR-007 applies to SHA-256 and canonical JSON: for a component small
enough to write and check, writing it removes an entire qualification argument.

### 2. Adopt SpecLab for Given/When/Then, as a test-only dependency
SpecLab supplies the BDD structure ADR-002 wanted, without macros. It is **pinned to a commit**, not
a branch or tag — it has no tagged release, and a moving ref would make a build non-reproducible,
which the evidence pipeline cannot tolerate.

It is linked into **test executables only**. No library target links it, it is absent from the
install/export set, and it never reaches a device. It is recorded in
`docs/governance/soup-register.toml` under the `tests` trust zone with `runtime_deployment = false`.

**It is also why this project's GCC floor is 16**: GCC 15 cannot build SpecLab, failing on its own
`std` BMI when reading a named module that re-exports parts of `std`.

### 3. Two frameworks is the honest state, not a transition
Nine `MduXTest` binaries and seven SpecLab ones coexist deliberately. Converting the remainder would
be churn with no evidence value — a passing MduXTest assertion is not improved by being restated as
`Given/When/Then`. New suites should prefer SpecLab where the scenario has a natural narrative
shape, and either is acceptable otherwise.

What is **not** acceptable is a third framework, or a test that reports through neither.

### 4. The discovery contract is the interface, not the framework
Both surfaces implement the same two-part contract from `cmake/MduXTestDiscoveryImpl.cmake`:
`--list-tests` prints one `name<TAB>labels` line per test, and `--run=<name>` executes exactly one
and reports through its exit status.

That contract is what produces a CTest entry per test and what the labels hang off. It is the reason
a framework can be swapped under a suite without the CI configuration changing, and the reason
`tests/framework/SpecLabBridge.hpp` is thin — SpecLab still executes the scenario and still decides
whether it passed.

## Alternatives Considered

**Catch2 v3, as ADR-002 selected.** Rejected in practice by never being adopted, and formally here.
It is a large dependency for a project whose central argument is that it takes very few, its
compile-time cost is significant on a modules build, and the BDD macros it offers are the part
SpecLab provides without macros. The traceability and reporting features ADR-002 credited it with
are supplied here by the CTest discovery contract instead.

**GoogleTest.** Same size objection, and no BDD structure at all.

**`MduXTest` only, writing BDD scaffolding in-repository.** Tempting for consistency, and rejected:
the scaffolding is not the small, obviously-correct kind of code that justifies writing it. SpecLab
is test-only, so the qualification argument that motivates zero-SOUP in the governed zone does not
apply with the same force.

**SpecLab only, deleting `MduXTest`.** Rejected as churn. See decision 3.

## Consequences

**Positive**
- The ADR trail now matches the tree: no document claims a framework the project does not use.
- Test-only dependencies are visible in the SOUP register rather than implicit.
- The discovery contract keeps CI selection stable across both surfaces.

**Negative, and accepted**
- **Two frameworks to learn.** A contributor meets both. Mitigated by the shared discovery contract
  and by `SpecLabBridge.hpp` being short enough to read in one sitting.
- **SpecLab is pinned to a commit**, so updating it is a deliberate edit rather than a version bump,
  and its lack of a release means no changelog to review.
- **The GCC floor is raised by a test dependency.** That is a real cost, recorded here so it is not
  rediscovered as a mystery.

**Risks introduced**
- **The pin drifts or the upstream disappears.** *Mitigation*: CPM caches, the SOUP register records
  the commit, and the in-repository framework covers nine binaries independently — the project would
  not be left with no tests.

## Implementation Notes
- `tests/framework/SpecLabBridge.hpp` adapts SpecLab scenarios to the discovery contract, including
  `mdux::spec::Checks`, which collects expectations so a converted `Then` reports every failure
  rather than only the first.
- Labels are assigned per scenario at registration. `evidence` means exactly "a committed artifact
  is byte-identical to a freshly baked one" and nothing else carries it; unit tests of the evidence
  modules use `evidence-unit` (ADR-007).
- `bridge_spec` is a separate binary from the converted suites on purpose: if the integration breaks,
  it fails with a name that says so rather than looking like a draw-list bug.

## References
- ADR-002: Testing Framework Selection (this repository) — superseded by this ADR
- ADR-004: Trust zones in C++ (this repository) — why a test-only dependency is a different question
  from a device dependency
- ADR-007: Evidence pipeline doctrine (this repository) — the label discipline and the
  write-it-rather-than-qualify-it reasoning this ADR reuses
- `docs/governance/soup-register.toml` — the SpecLab entry

## Approval
- **Decision Date**: 2026-08-03
- **Approved By**: Project maintainer
- **Review Date**: when a third test surface is proposed, or when SpecLab publishes a tagged release
  that would let the commit pin become a version pin
