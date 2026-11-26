# ADR-026: Component admission criteria

## Status
Proposed (maintainer acceptance)

## Context

The `.medui` dictionary holds eleven components. Each was admitted by its own issue, and each issue
had to rediscover what "done" meant: #261 had to decide what a button's face is before a button
could be drawn, and #258 and #260 each had to amend [ADR-010](ADR-010-no-on-device-text-shaping.md)
decision 4 before a live value could reach pixels. Those were the right decisions taken at the right
time, but they were taken per component, so the eleven entries do not hold the same set of
properties and nothing records which entry holds which.

Epic #388 proposes to grow the dictionary roughly sixfold, across controls, data display,
containers, clinical components and four tiers of international text. At that size the per-issue
approach stops being affordable. A dictionary whose entries hold different guarantees is worse than
a small one, because a product team reads the dictionary as a single promise: a screen built from
it is bounded, allocation-free, verified per locale and traceable. An entry that silently lacks one
of those is a gap nobody was told about.

Two properties of the present state make the question concrete rather than abstract:

- **The draw budget is declared, not derived.** A screen recipe states `maxVertices`, `maxIndices`
  and `maxCommands`, and `DrawList` fails closed against them at run time. No compiler diagnostic
  compares a screen's content with its budget, because no component states what it costs. Epic #388
  adds components whose cost is a function of their fields (a chart's point count, a table's row
  count), where a declared ceiling nobody can check against the source stops being honest.
- **Interaction and data-quality states have no appearance.** `mdux.medui.screen` says so in its
  own module comment: no pressed, focused or disabled state exists, because no artifact names one
  and no golden pins one. A bound value has no stale or invalid presentation either.

## Medical Device Considerations

- **IEC 62304 software architecture and unit verification.** A component is a software unit with a
  governed runtime half and a host-compiler half. Stating once what every such unit must supply
  makes unit acceptance a checklist a reviewer can run, instead of a judgement re-made per issue.
- **ISO 14971.** Several criteria below are risk controls in the form of a build-time refusal: a
  screen that cannot fit its budget, a control with no requirement, a state told apart by colour
  alone. Admitting a component without them would move the control from the build to review.
- **IEC 62366-1.** Interaction states, data-quality states and international-text behaviour are
  what an operator perceives. Making them admission criteria means the user-interface
  specification of a device built on MduX can cite compiled, verified behaviour.
- **Traceability.** Every actionable component resolves to a closed action and a requirement; the
  criteria make that a property of the dictionary rather than of two of its entries.
- This record defines an engineering bar. It establishes no certification, validation or
  production-readiness claim, and meeting every criterion does not make a product conformant to any
  standard.

## Decision

**A component enters the dictionary only when it meets the twelve criteria below, and the state of
every entry against them is recorded in [`docs/medui/component-inventory.json`](../medui/component-inventory.json).**

Each criterion has an identifier, because the inventory and later issues cite it. A criterion that
does not apply to a component is recorded as `not-applicable` with the reason implied by the
component's nature (a `Label` has no binding); it is never left blank.

### AC-01 Contract status

The entry — its name, fields, field domains and named values — is present in the MedUI release
pinned by `medui-conformance.toml`, or it is explicitly labelled a local extension there and in
`docs/parity/`. A local extension is never part of a conformance claim
([ADR-015](ADR-015-versioned-sibling-observations.md), [ADR-017](ADR-017-sibling-conformance-gate-status.md)).

### AC-02 Closed grammar and governed schema

The entry's field set is closed, every field has a domain, and every enumerated value belongs to a
closed named set. It is emitted by `mdux-meduic --grammar` from the compiler's own tables, with
accepting and rejecting examples. Its compiled form is a spec type in `mdux.medui.schema` whose
validation refuses every malformed instance by a named error, so a screen assembled by hand at run
time meets the same checks a compiled one did. Each new diagnostic has a stable code and an
`--explain` entry.

### AC-03 Declared worst-case cost

The component states its worst-case vertices, indices and commands as a function of its compiled
fields alone — never of run-time data — through one implementation shared by the compiler and the
runtime (ADR-008 decision 1's doctrine, as ADR-010 decision 4 already applies it to pen
arithmetic). The compiler sums the costs of a screen's nodes and refuses a screen whose declared
budget cannot hold them.

### AC-04 Governed draw path

The component is drawn by governed code recording into a `DrawList`: allocation-free, `noexcept`,
all-or-nothing per node, and refusing rather than truncating, substituting or clamping silently.
Its appearance when nothing is bound is defined and drawn, or its absence is a stated decision.

### AC-05 Bounded binding

A component showing run-time data receives it through a slot type over caller-owned storage, with
every size bounded by a named constant and every binding validated when it is created rather than
when it is drawn. Nothing about the number or sequence of primitives drawn is a function of the
bound value beyond what AC-03 already bounded.

### AC-06 Interaction

An actionable component defines its hit region, resolves a press to itself and to a member of a
closed action set, and carries a `requirement:` when the action changes device state. It takes a
place in the screen's compiled focus order and defines arm, activate and cancel behaviour under
[ADR-018](ADR-018-bounded-input-and-update-order.md) clause 4, for pointer and for non-pointer
operation alike.

### AC-07 Interaction and data-quality states

The component has a compiled appearance for each state that applies to it — enabled, disabled,
focused, pressed, selected, invalid — and, if it shows a bound value, for each data-quality state:
valid, stale, invalid, out of range. No two states are distinguishable by colour alone, and a
non-valid data state cannot be mistaken for a reading.

### AC-08 Rendered verification

The component states which rendered checks can be discharged on it and in which states, and
provides at least one golden-annotated instance in a committed screen. For content a scenario
drives, it states the weakest honest claim a host check can make
([ADR-014](ADR-014-rendered-truth-verification.md), [ADR-021](ADR-021-dynamic-scenario-capture-evidence.md)).

### AC-09 International text

Every string the component shows comes from a text key or a build-measured pattern, and its box is
checked against every approved locale. The component declares its behaviour under a right-to-left
layout direction — mirrored, or never mirrored with the reason — and uses logical start and end
edges rather than left and right. A component that shows locale-dependent values (numbers, dates,
units) takes their form from a per-locale, build-measured template.

### AC-10 Tests

Unit tests of the schema, the draw path and each refusal; a `noheap` test; a pixel test under the
software rasteriser; a scenario step if the component is interactive; and conformance cases
upstream when AC-01 is met by the shared contract.

### AC-11 Authoring and tooling

The component is documented in the `medui-authoring` skill, appears in `docs/medui/grammar.json`,
survives the canonical serializer's round trip ([ADR-023](ADR-023-medui-host-editing-api-and-round-trip-source-contract.md)),
is offered by the Studio editor, has a preview fixture, and has an instance in the component
gallery (epic #388, H1) in every state, theme and approved locale.

### AC-12 Lifecycle records

The requirements the component satisfies and the hazards its failure modes relate to are recorded
in `software_development_file/` before it is admitted, registered prospectively rather than
reconstructed.

### The inventory

`docs/medui/component-inventory.json` lists every component epic #388 names, with one of three
statuses:

- `delivered` — in the dictionary today. Carries a status per criterion (`met`, `partial`, `unmet`,
  `not-applicable`); every `partial` or `unmet` names the epic child that closes it, and every `met`
  that is not self-evident from the grammar names a repository path as evidence.
- `proposed` — named by the epic, not in the dictionary. Carries its workstream and nothing else,
  since asserting criteria for a component nobody has designed would be fiction.
- `declined` — considered and closed with a recorded reason.

`tools/docs-lint/check_component_inventory.py` gates it: the set of `delivered` entries must equal
the component set in `docs/medui/grammar.json`, every criterion identifier must be one this record
defines, and every evidence path must exist. The file therefore cannot disagree with the compiler
about what exists, and cannot claim a criterion on the strength of a file that is not there.

### The existing eleven are audited, not grandfathered

The inventory records the eleven present components against all twelve criteria as they stand. None
meets all twelve. The shortfalls are systematic rather than per-component, and each belongs to a
child of epic #388:

- **AC-03 is unmet for every component**: no cost function exists and no diagnostic compares a
  screen with its budget.
- **AC-07 is unmet for every actionable or bound component**: no interaction or data-quality state
  has an appearance. `StatusIndicator` additionally tells its states apart by tint alone until a
  locale is bound.
- **AC-06 is partial for the three actionable components**: a press resolves, but no focus order
  exists, so non-pointer operation is not possible.
- **AC-09 is partial throughout**: text is key-based and budget-checked across locales, but no
  component declares a direction behaviour, and layout speaks only of absolute coordinates.
- **AC-11 is partial throughout**: there is no component gallery.
- **AC-12 is partial throughout**: requirement and hazard records exist for the behaviours the
  parity programme registered, not per component.

A criterion is not weakened to make an existing component pass. The components remain in the
dictionary — removing working, verified entries would serve nobody — and the inventory is the
record of what is still owed on each.

## Alternatives Considered

**A. Keep deciding per component.** Rejected: it is what produced the present unevenness, and its
cost grows with every entry. It also leaves the cross-cutting properties — cost, states, direction —
with no owner, since no single component's issue is the place to design them.

**B. A prose checklist in the authoring skill.** Rejected: a checklist nothing checks is a
convention, and the same reasoning that moved the tool manifest and the grammar into generated,
gated files applies. The inventory is deliberately a file a linter reads.

**C. Generate the inventory entirely from source.** Rejected for now. Which criteria a component
meets is partly a judgement (does a golden exist *in every state*), and a generator would have to
either guess or encode the judgement in annotations spread through the tree. The chosen form gates
the mechanical half — existence, identifiers, evidence paths — and leaves the judgement in one
reviewed file. As criteria gain mechanical checks (AC-03's cost function, AC-11's gallery), those
columns can move to generation.

**D. Grandfather the eleven as compliant by definition.** Rejected: it would define the bar as
"whatever exists", and the first new component would be held to a standard its predecessors visibly
do not meet.

**E. Fewer, broader criteria.** Considered. Twelve is the number of distinct things that have each,
at least once, been missing from an otherwise finished component; merging them would hide exactly
the gaps this record exists to show.

## Consequences

### Positive
- "Is this component finished" has an answer that does not depend on who is asked.
- Cross-cutting work in epic #388 — the cost model, states, focus, layout direction — has a stated
  consumer: the criterion it makes satisfiable.
- The gap between the present dictionary and the intended one is a committed, reviewable file.

### Negative
- Admitting a component costs more than it did. That is the intent, and it is also real schedule.
- The inventory is a hand-maintained file for its judgement half, and can be wrong where the linter
  cannot see.

### Risks
- **The criteria become a formality ticked without evidence.** Mitigation: every non-evident `met`
  carries a path the linter resolves, and a `met` with nothing behind it is a review finding.
- **A criterion proves unachievable for a legitimate component.** Mitigation: the criterion is
  amended here, by a recorded decision, for every component at once — not waived for one.
- **The foundations the criteria presuppose (A3–A9 of epic #388) slip, blocking all admissions.**
  Mitigation: a component may be merged behind a `partial` status that names the child closing it;
  what it may not do is be recorded as `met`.

## Implementation Notes

- This record changes no runtime, no schema and no committed artifact under `generated/`.
- AC-01 depends on the upstream proposal tracked as epic #388 child A2; until then every new entry
  is a labelled local extension.
- AC-03's shared cost function is new governed code and needs its own design before the first
  component whose cost depends on a field.

## References
- Epic #388, child #390.
- [ADR-007](ADR-007-evidence-pipeline-doctrine.md), [ADR-008](ADR-008-zero-soup-ml-inference.md),
  [ADR-010](ADR-010-no-on-device-text-shaping.md), [ADR-011](ADR-011-deterministic-medui-compile-boundary.md),
  [ADR-012](ADR-012-compiled-screen-artifacts.md), [ADR-014](ADR-014-rendered-truth-verification.md),
  [ADR-018](ADR-018-bounded-input-and-update-order.md), [ADR-021](ADR-021-dynamic-scenario-capture-evidence.md).
- [`docs/medui/grammar.json`](../medui/grammar.json), [`docs/medui/component-inventory.json`](../medui/component-inventory.json).

## Approval
- **Decision Date**: 2026-11-26
- **Approved By**: pending maintainer acceptance
- **Review Date**: when the first component is admitted under these criteria
