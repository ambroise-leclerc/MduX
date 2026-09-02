# ADR-014: What rendered-truth verification checks, and what it cannot

## Status
Accepted (2026-08-31)

## Shared contract

No MedUI shared decision covers verification, so MduX decides it locally and this ADR carries no
shared identity the way [ADR-011](ADR-011-deterministic-medui-compile-boundary.md) and
[ADR-012](ADR-012-compiled-screen-artifacts.md) do. One part of the mechanism is *not* local,
however, and decision 3 depends on it: the `cv_check` names an author may write belong to the shared
language's `safety` capability, pinned in `medui-conformance.toml` at
`265df1925a672bd556f69123e287215b45cfd210`, and `MEDUI-E071` is
what rejects a name outside the set. Adding a check name is an upstream change followed by a re-pin,
never a change to the verifier.

## Context

Epic #16 renders a compiled screen offscreen, checks that the content a golden reference names
appears where the compiled screen said it would and that every localized text run appears inside its
node, across every approved locale, then emits that as an evidence artifact. Its implementation
children build the checks (#252), the driver (#253), the artifact (#254) and the CI leg (#255). All
four have landed. This record preceded all four so that they apply a recorded decision rather than
reconstructing one from whatever they produced — the same reason #190 preceded the rest of #15, and
decision 5 is what #255 had to add back to it rather than decide in a workflow file.

Most of the ground is already fixed, and this ADR should not relitigate it.

**The frame exists, and it is compared exactly.** `mdux.render.offscreen` renders to an offscreen
target and reads it back (#13, #125, #126), and `tests/render/PixelTests.cpp` compares the result
pixel for pixel with no tolerance — deliberately, since a one-channel difference is "the smallest
wrong answer a renderer can give, and the one a tolerance-based comparison would wave through". The
pixel-labelled suite runs under lavapipe on Linux and MoltenVK on macOS, and
[ADR-013](ADR-013-verified-apple-silicon-macos-toolchain.md) makes skipping it a failure.

**The expectations exist.** `collectGoldens()` (#196) derives ADR-011's golden set from a resolved
screen, and `generated/screen/endoscope-monitor/goldens.json` is a committed, byte-compared sidecar
holding two entries.

**The approved locale set exists.** Since #244 a compiled screen carries `approvedTextPackages`,
one record per locale with the text package's id and canonical digest, so "every approved locale" is
a property of the artifact rather than a list a caller supplies.

**The evidence pattern exists.** [ADR-007](ADR-007-evidence-pipeline-doctrine.md) fixes canonical
JSON, floats as `u32` bit patterns, a resolved-options report, and re-derivation with
byte-comparison on four toolchain legs.

What is genuinely open, and what this ADR therefore has to decide rather than inherit:

1. what a check is *made of*, and which trust zone it lives in;
2. what the verifier may take on trust — the question ADR-012 and epic #16 currently answer
   differently, which is the main reason this record is the epic's first child;
3. what `verification.json` claims, and what it must never contain;
4. what the mechanism is worth to an IEC 62304:2006 §5.7 Software system testing argument, and
   where that worth stops.

**Implementation status.** The epic is implemented. #252, #253 and #254 built `mdux.verify`, the
host-only `mdux-verify-ui` driver and the artifact: the driver loads committed screen, golden,
shader, text and font artifacts, enumerates the complete obligation set, renders once per scope and
reports owning outcomes, and `mdux-verify-bake` serializes those same outcomes as
`generated/screen/<id>/verification.json` inside the screen's one bake registration. #255 added the
automatic CI gate, the failure diff, and decision 5 - the draw rule without which the committed
screen could not discharge the golden obligations it declares.

### The contradiction this record has to settle

Epic #16 carries a "binding constraint from ADR-012 and PR #202" requiring the verifier to "apply
the predicate to `package.json` and check the golden set matches, rather than iterating the entries
it was given". ADR-012 decision 4, as merged, requires the opposite: "#16 derives its expectations
from `goldens.json` itself; what it must never do is re-apply the predicate, since a second
implementation is precisely what would let the two disagree."

The epic quotes an earlier revision of ADR-012. That revision also carried
`NodeProvenance { safetyCritical, positioned }` on every compiled node, which is what would have
made the expected set re-derivable from the package; the two were withdrawn together, for the
reasons ADR-012 decision 4 records.

The consequence is not a matter of taste. **The predicate's inputs are not in `package.json`.** The
committed `endoscope-monitor` package carries no annotation flag and no record of which nodes
declared an explicit `position:`. `insufflation-pressure` is there with a `requirement`,
`ecg-lead-ii` is there without one, both have goldens, and nothing in the file distinguishes either
from a node that would have neither. A verifier holding only the compiled package cannot apply
ADR-011's predicate at all, let alone apply it a second time.

So the rule the epic wants is right and the mechanism it names cannot deliver it. Decision 2 keeps
the rule and replaces the mechanism.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)

This is the mechanism that lets a system-testing claim point at a machine-checked artifact instead
of a manual review record. A reviewer confirming that a safety-critical readout
appears in the right place, in the right colour, in every approved locale, is performing an
inspection that is expensive, dull, and correct until the day it is not. Re-deriving the same fact
from files on every automatic CI event that runs the evidence suite is the form of verification this
repository's whole evidence argument already takes.

The scope limit is as important as the claim, and this corpus already states it:
`docs/iec62304/03-development-process.md` records that MduX has no software *system* in §5.7's
sense — "a system implies an assembled product, and today's repository is foundations and an
evidence kernel, not an assembled UI". This epic supplies a mechanism such an argument could rest
on. It does not supply the system, the requirements it would be tested against, or the
representative environment. A verified screen is evidence about a screen.

### IEC 62366 implications (usability engineering)

None of these checks is a usability control, and none should be described as one. Bounds and tint
say that a thing is where its author declared it and in the colour its token resolves to; whether it
is legible, unambiguous or appropriate is IEC 62366-1:2015 work that no pixel check performs. The
one adjacent control that exists is a compile-time one and predates this epic: text budgets measured
against the widest approved translation (#195).

### Risk management

The failure mode this mechanism is intended to detect is a screen that renders differently from the
screen that was reviewed — content outside the rectangle its budget was computed for, or a critical
readout in a tint that no longer distinguishes an alarm state. No formal hazard or risk-control
record in this repository names that failure mode today, and this ADR does not invent one or claim
that a risk has already been reduced.

The risk it *introduces* is misplaced confidence. Verification compares a frame against the compiled
screen, and both come from one source, so no amount of it can show that the screen is the right
screen. Decision 4 states this as a constraint on the artifact's wording rather than leaving a
reader to infer it.

### Traceability

`requirement:` on a node reaches the compiled package, the node id reaches `goldens.json`, and the
node id and its outcomes reach `verification.json`. A requirement-to-rendered-evidence chain is then
a walk across three committed files rather than a claim in prose.

## Decision

### 1. The checks are pure functions over a CPU framebuffer, and they are governed

`mdux.verify` takes a read-only framebuffer view — bytes, width, height, row stride, format — and one
of two artifact-derived expectation views, and returns an outcome:

- a **golden expectation** carries one golden entry, the compiled node it names and the tint the
  governed theme table resolves when the check needs one;
- a **text expectation** carries one compiled node whose spec names a `textKey`, the approved locale,
  the bound text run and atlas data that the approved font/text artifacts identify, and the resolved
  tint needed to distinguish the run from its background.

Both views are read-only and caller-owned. The host driver constructs them from committed artifacts;
the command line cannot supply or amend their values. A unit test may construct a synthetic view to
exercise a pure check, but production expectations have exactly the artifact sources decision 2
names. No Vulkan type crosses the boundary. The GPU's involvement ends at the readback that already
exists.

Two things follow, and both are the point rather than a convenience:

- **No GPU is needed to test a check.** A framebuffer is an array, so a unit test paints a rectangle
  into one and asserts that `GoldenBounds` passes, then moves it by a pixel and asserts that it
  fails. The check suite stays fast, and it can be built and proven before a driver exists.
- **A failure is legible.** A check names a node, a render scope, an expected value and where it
  looked. That is a sentence someone can act on, which "12,438 pixels differ" is not.

They are **governed**, in `MduXCore`, and that is a choice rather than an inheritance. The checks
have the same shape as #199's runtime — bounded arithmetic over caller-owned storage, no allocation,
no throw, no file — and placing them in the governed zone is what turns "the verifier does no work a
device could not do" from a convention into a lint result: `mdux-governed-lint` and
`governed.noThrow.symbolScan` cover them with no second registration, which is #252's acceptance
criterion already.

**The driver and the artifact writer are not governed.** Rendering, opening files and writing
canonical JSON are exactly what ADR-004 and ADR-005 keep out of that zone. #253's driver and #254's
writer live outside it and call in.

### 2. Every expectation is derived from a committed artifact; none is supplied by the caller

| What | Derived from | Never from |
|---|---|---|
| which golden-scoped nodes are checked, and which `cvChecks` apply | `goldens.json` | a list on the command line |
| which mandatory text checks apply | every node in `package.json` whose compiled spec carries a `textKey` | a `cvCheck`, annotation or command-line switch |
| where a golden node is, and which names it pins | the golden entry, required to agree exactly with the named node in `package.json` | numbers written into the verifier or a test |
| what tint or glyph run a name resolves to | the governed theme table and the approved font/text package artifacts | caller-supplied pixels, text or token values |
| which locales must be rendered | `approvedTextPackages` in `package.json`; one locale-free render scope when that set is empty | `--locales` read as a list of names |

`--locales=all` names the screen's own manifest, and a locale absent from it is an error rather than
a skip. A caller must not be able to narrow what is verified: a verifier whose scope is an argument
reports on whatever it was asked about, in a file that reads as though it reported on the screen.

**The expected golden set is derived, not read — and the derivation runs in the baker on all four
automatic CI legs.** This is the replacement for the withdrawn artifact-level comparison, and it is
stronger than the check the epic asked for rather than a relaxation of it.

`goldens.json` is registered through `mdux_compile_screen()`, a front for `mdux_bake_artifact()`,
so `evidence.screen.<id>` re-runs the compiler into the build tree and byte-compares the result
against the committed file on Windows/MSVC, Linux/GCC 16, macOS/Clang 21 and Linux/Clang 21 (ADR-007
decision 6). The compiler applies ADR-011's predicate to the AST — the only place the predicate's
inputs exist — through the single implementation ADR-012 decision 4 binds the baker to. A golden
missing for a node the predicate selects therefore cannot reach the verifier: it fails four byte
comparisons first. What
the verifier consumes is an expectation set that was re-derived from source, not one it was handed
and believed.

Which mechanism owns which failure, since "derive, don't trust" is otherwise a slogan:

| Failure | Caught by | Where |
|---|---|---|
| a golden missing for a node the predicate selects | re-derivation in `evidence.screen.<id>` | every automatic CI event that runs the four evidence legs |
| a predicate that is wrong | #196's fixtures over known screens | unit tests, with the predicate |
| a golden naming a node that does not exist | the verifier's first lookup | #252, #253 |
| a node verified in one locale and silently not in another | the obligation set of decision 3 | #253 |

The middle two are the ones the epic's wording would have handled badly and well. An artifact-level
comparison between two files written in one pass from one AST cannot see a wrong predicate — ADR-012
decision 4 is where that was established — and a resolve-only walk of `goldens.json` cannot see a
missing entry. Neither mechanism above is asked to do the other's job.

**What the verifier must never do is re-apply the predicate.** Not because it would be redundant,
but because it would be a second implementation, and two implementations of one rule agree until the
day they matter. This is ADR-012 decision 4 restated at the consumer, and it is the direction the
withdrawn wording pointed the wrong way.

### 3. Every obligation is discharged, and nothing is skipped

The verifier has two kinds of **obligation**, both enumerated from the artifact sources above
*before* the first frame is rendered:

1. a **golden obligation** is one golden entry, one of its `cvChecks`, and one render scope;
2. a **text obligation** is one compiled node carrying a `textKey`, one of the mandatory
   `InkContainment` or `LocalizedTextPresence` checks, and one approved locale.

A render scope is each approved locale when the screen carries text. A valid text-bearing screen has
at least one because `ScreenPackage::validate()` refuses a text node with no approved text package.
A textless screen instead has one explicit locale-free render scope, so its `Bounds` and `ColorHash`
obligations do not disappear in a Cartesian product with an empty locale set. A run fails unless
every obligation in the combined set carries a pass or a fail.

Three consequences, each one a failure a skip would hide:

- **A check this build cannot perform fails**, rather than reporting "unsupported, skipped". The set
  of names is closed and contract-owned — `Bounds` and `ColorHash` — so an entry naming anything
  else did not come from a compiler this repository builds.
- **A locale that could not be rendered fails.** The manifest says the screen was compiled against
  it; a driver that cannot render it has not verified the screen.
- **Zero obligations fails.** A run that verified nothing exits non-zero, the way `--no-tests=error`
  guards an empty ctest selection. A green tick over zero assertions is the failure mode this epic
  exists to remove, and it is decided here rather than in #255 because a driver that reports success
  over an empty set is one nobody re-examines.

**Exit status separates a failed check from a run that could not be made.** A missing Vulkan loader
and a node in the wrong place are different facts, and CI must not read the first as the second —
or, worse, as a pass.

**Two of the four checks #252 names are not `cvChecks`, and this is where that is settled.**
`Bounds` maps to `GoldenBounds` and `ColorHash` to the tint comparison; both are opt-in per node,
because an author declares which claims a node makes, and an explicit `position:` makes the `Bounds`
claim on the author's behalf. Ink containment and localized-text presence are **not** opt-in: they
apply to every node whose compiled spec carries a `textKey`, in every approved locale. A glyph run
leaving the box that was budgeted for it is a defect whether or not anyone annotated the node, and
#195 already measured that box at compile time — the rendered check is the same claim, verified.
#252 must therefore not add two `CvCheck` enumerators to make its four checks symmetrical: that set
belongs to the shared contract, and widening it is an upstream change and a re-pin. They are the
second obligation family above, so a text node does not need a golden entry for either check to run.

### 4. `verification.json` records expectations and outcomes, never measurements

ADR-007's pattern rather than a second one: canonical JSON through `mdux.evidence.json`, floats as
`u32` bit patterns, and the existing screen `report.json` carrying the fully resolved option set and
all output digests. `mdux_compile_screen()` remains the **single** owner of
`mdux_bake_artifact(KIND screen ID <id> ...)`; #254 extends that one screen-bundle production step
with `verification.json` and adds the file to its fixed `OUTPUTS`. It must not register the same
kind/id a second time or create a second report at the same path. The one enlarged artifact is then
re-derived and byte-compared on all four legs.

**What it claims**: each outcome names exactly one obligation and makes only that check's claim. A
passing `Bounds` outcome says the named golden node was found at its declared rectangle; a passing
`ColorHash` outcome says it carried the tint its token resolves to; and passing text outcomes say
the approved locale's run was present and its ink remained inside the node. The file names the
screen package, goldens sidecar, font package and per-locale text packages it used by digest. It
never turns a node carrying only `Bounds` into a claim that its tint was checked.

**What it does not claim**: that the screen is correct, complete, legible or clinically appropriate.
It cannot. The expectation and the thing tested come from one source, so what is verified is that
the render agrees with the compiled screen. That is a strong internal-consistency property and not
an external one, and #254's acceptance asks for wording an auditor reads correctly — this is the
sentence that wording has to survive.

**No measured pixel value goes into the artifact**, for two reasons that point the same way:

- It would answer a question the pixel-labelled render tests already answer, and it would change
  whenever anything about rendering changed even though every check still passed. The byte
  comparison would then stop distinguishing "the verification result changed" from "a pixel moved".
- It would make a byte-compared artifact a property of the driver tuple rather than of its declared
  inputs. Exact comparison holds today under lavapipe and MoltenVK for flat fills and baked glyph
  runs, but that is a fact about what is currently drawn, not a promise extended to every component
  #17 adds. Committing samples would turn a rendering question into an evidence failure on one leg.
  ADR-007 decision 5 made the same structural argument about commit SHAs: data that depends on the
  environment rather than on the inputs cannot live inside a byte-compared artifact.

The same rule excludes a timestamp, a commit SHA, an absolute path and a duration. The diff image
#255 attaches on failure is therefore a CI attachment rather than part of the artifact — a
measurement for a human, outside the file that is compared.

### 5. A live-data node paints the field it reserves, in the token its golden names

The runtime draws a node's whole resolved rectangle in its single colour token when that node is a
`NumericDisplay` or a `SignalTrace`. The **reading** inside the field — the digits a template expands
to, the excursion a waveform makes — stays deferred until #258 and #257 give those components a
sample to draw from.

This decision was forced by #255 and belongs here rather than in a workflow file, because without it
the epic's own gate could not be turned on. The consequences section below predicted the state
exactly: the committed screen's two golden entries name a `NumericDisplay` and a `SignalTrace`, the
runtime deferred both, and `verification.json` therefore recorded three `NothingPainted` findings. A
CI gate over that screen is red on the day it is added, and the three ways out that do not involve
drawing anything — delete the goldens, weaken their checks, verify a different screen — are the three
#255 forbids by name.

**The rule is read off the artifact, not invented for the occasion**, and that distinction carries
the decision. `collectGoldens()` applies ADR-011's predicate while the AST still holds its inputs and
writes, per selected node, `bounds` = that node's whole resolved rectangle and `colorToken` = the
single token its author gave it. Decision 2 makes exactly those two values the verifier's
expectation, and `goldenBounds()` reads the first as an equality: the content inside the declared
rectangle has to *be* that rectangle. So the compiled artifact — in a file four legs byte-compare —
already asserts that this node's whole rectangle carries that tint. A runtime painting nothing there
is not declining to invent an appearance; it is disagreeing with the artifact its own compiler
emitted.

What this does **not** license. `mdux.medui.screen` still refuses to fill a `Label`'s box with its
text colour, or to give a `Button` a face: neither has a golden that says so, a `Label`'s token is
its *text* colour over a text-sized box, and no golden can ever name the synthetic `Panel` a `Row`
produces. A `Clock` carries no token and a `StatusIndicator` carries one per state — no single tint,
which is the same fact that makes `collectGoldens()` refuse `ColorHash` for such a node — so both
stay deferred. The rule is "one node, one rectangle, one token a golden can pin", and it is the
narrowest rule that lets the artifact and the frame agree.

One consequence constrains #257 and #258 rather than being free, and it is better stated than
discovered. `colorHash()` admits only pixels that are a blend of the node's ground and its tint at
one coverage, so a reading drawn inside a field in some third colour fails the golden its own screen
was compiled with. A component whose field a golden pins with `ColorHash` therefore has two ways to
show a reading and no others: in the field's own tint, or knocked out of it back to the ground.

### 6. The CI gate is a ctest label, and the failure diff is an attachment

`mdux_compile_screen()` registers `verify.screen.<id>` beside the `evidence.screen.<id>` it already
registers, running `mdux-verify-ui --screen=generated/screen/<id> --locales=all` over the
**committed** bundle. Three legs assert it — lavapipe on Linux/GCC 16 and Linux/Clang 21, MoltenVK on
macOS — with `--no-tests=error`, so a label that matched no screen fails rather than passing over
nothing, and with the same skip guard the pixel legs carry, since exit 77 means an absent device and
nothing else.

Two properties are worth naming. The subject is the committed bundle rather than the freshly baked
one `mdux-verify-bake` renders during the build; the two agree only because `evidence.screen.<id>`
byte-compares them, and a gate that assumed that agreement would rest on the check it is meant to be
independent of. And registering it where the screen is registered is what makes a second committed
screen gated without anyone remembering to add it — the same argument that put the evidence test
there.

**The diff image is a CI artifact and never a fifth file in the bundle.** It is the frame, so
decision 4's rule excludes it: an image is a measurement, and a byte-compared artifact holding one
would become a property of the driver tuple that produced it. It is written under the build tree,
uploaded only when the step fails, and nothing reads it back — an image the driver wrote is never an
input to a verdict, so a defect in it can make a failure harder to read and can never make one pass.
It is a PNG with an in-tree encoder rather than a linked one, for ADR-007's zero-SOUP reason: a
compression library in a build tool's dependency graph would need qualifying, for the sake of an
image no verdict depends on.

## Alternatives Considered

### 1. Compare the whole frame against a committed reference image (Rejected)
**Pros:** No goldens, no predicate, no per-node machinery, and the comparator already exists and
detects a single-channel difference.
**Cons:** It names nothing. A failure is a pixel count, and a reader has to reconstruct which claim
broke. Every deliberate change fails it too, so the reference gets updated reflexively and the check
trains a team to dismiss it. Worse, it compares a frame against a previous frame rather than against
the compiled screen, so it cannot notice that a node was never where its declared position said it
would be. The per-node form is what makes a failure name a node, a check, a locale and an expected
value. Whole-frame comparison keeps its place as a rendering test; it is not a verification
mechanism.

### 2. Re-apply ADR-011's predicate inside the verifier (Rejected — see Decision 2)
**Pros:** What epic #16 asks for in its binding-constraint block; the verifier would be
self-contained and would need no upstream guarantee.
**Cons:** A second implementation of the one rule ADR-012 decision 4 requires to have exactly one.
And it cannot be written anyway: the predicate's inputs are absent from `package.json`, so this
alternative reduces to the next one.

### 3. Restore `NodeProvenance` to the compiled node so the set becomes re-derivable (Rejected)
**Pros:** Makes alternative 2 possible; the completeness check would live in the artifacts rather
than in the pipeline.
**Cons:** ADR-012 decision 4 withdrew exactly this, and its risk section states the general rule:
`package.json` holds what the runtime reads, and anything only a tool reads belongs in a sidecar.
"Two booleans is a small breach of a rule, and a rule breached for something small is how the next
thing gets in." Being the consumer rather than the producer of the artifact changes none of that.

### 4. Put the checks in the host-tools zone beside the driver (Rejected)
**Pros:** One zone for the whole verification story, no governed-lint constraints, and free use of
allocation and exceptions in the checks.
**Cons:** The claim worth having is that the verifier does no work a device could not do, and in the
host-tools zone that is a convention nobody checks. The cost of the governed placement is that the
checks may not allocate — which is also what keeps them testable against a synthetic buffer.

### 5. Verify only the locale a device ships (Rejected)
**Pros:** Fewer renders and a faster CI leg.
**Cons:** The screen's manifest approves a set, and one shared `DrawBudget` covers the widest of
them. Verifying one locale leaves the rest asserted and untested, and a locale is precisely where a
rendered-truth check earns its keep: the layout is shared, so a translation is the input most likely
to move ink out of its box without moving a single rectangle.

### 6. Record measured samples in `verification.json` (Rejected — see Decision 4)
**Pros:** A failure would carry its evidence inside the artifact, and a reader could see how far off
a tint was.
**Cons:** It duplicates what the render tests already prove, it makes the artifact change when no
verification outcome changed, and it makes a byte-compared file depend on the driver that produced
the frame.

## Consequences

### Positive
- The checks are unit-testable without a GPU, so #252 was built and proven before #253. The epic's
  sequencing note became a property of the design rather than a hope.
- A verification failure names a node, a check, a render scope and an expected value, which is what
  separates this from a screenshot test.
- No new evidence pattern: `verification.json` is a fourth file in a directory whose other three are
  already re-derived and byte-compared on four legs, under the same screen registration and report.
- The completeness guarantee is re-derived on every automatic CI event that runs the evidence suite
  rather than inspected once, and the guarantee is stated as a decision with a named owner per
  failure rather than as advice.

### Negative
- **The verifier cannot see a wrong predicate**, and nothing here changes that. #196's fixtures own
  it; this record's only contribution is to say so where a reader of #16 will look for it.
- **Verification is internal consistency, not truth.** It is recorded as a limit on the artifact's
  wording because it cannot be removed by any amount of checking.
- **Two files, one verifier.** ADR-012 decision 4 accepted that cost knowingly on behalf of the
  consumer; the consumer is where it is actually paid.
- **The committed screen could not pass its golden obligations, and decision 5 is how it now does.**
  Its two golden entries are a `NumericDisplay` and a `SignalTrace`; the runtime deferred both, and
  `tests/render/ScreenPixelTests.cpp` asserted that the golden region was empty as a tripwire meant
  to fail the day one of them drew. #255 was that day. The three responses this record and #255 both
  refused — delete the goldens, weaken their checks, verify a different screen — are what decision 5
  exists instead of. The tripwire is gone and the scenario that replaced it is the rendered golden
  consumer ADR-012 describes, reading the committed sidecar against the frame.
- **The reading inside a field is still deferred**, so a green `verification.json` says the two
  safety-critical nodes occupy their rectangles in their tints, and says nothing about what they will
  display. That is a smaller claim than a reader might take "the screen verifies" to mean, and it is
  the honest one until #257 and #258 land.

### Risks
- **The zero-obligation rule is relaxed to get CI green** when the first verified screen has nothing
  to verify. This is the risk most likely to materialise, and there is no mechanical mitigation. The
  rule to apply in review is that the screen changes, not the rule: a driver may legitimately be
  configured over no screens in a tree that has no verifiable one, but a run *over a screen* that
  reports success having discharged nothing is the defect this epic exists to remove. A textless
  screen with goldens is not empty: its singleton locale-free render scope preserves those
  obligations.
- **The obligation set grows multiplicatively** — golden nodes by opted-in checks by render scopes,
  plus text nodes by two mandatory checks by locales — and a slow verification ends up on a nightly,
  which is a check nobody reads. *Mitigation*: only the render is per scope. #253 renders once per
  approved locale, or once for a textless screen, and runs every applicable check against each frame
  in memory rather than rendering per obligation.
- **`verification.json` accretes fields a reader mistakes for a stronger claim.** *Mitigation*:
  decision 4's rule is that the artifact records what was expected and whether it held. A field
  answering "how close was it" is a measurement, and it does not belong.

## Implementation Notes

- Module name `mdux.verify` (governed, in `MduXCore`), following the dotted-lowercase convention.
  The driver and the artifact writer live outside the governed zone.
- The artifact is `generated/screen/<id>/verification.json`, beside the three files ADR-012 fixes.
- `mdux_compile_screen()` remains the only `screen/<id>` bake registration. #254 extends its one
  bundle producer and existing `report.json`; a second `mdux_bake_artifact()` call for the same
  kind/id would collide in its generated target, test and output directory and is forbidden.
- Diagnostics use the shared envelope from #118. Source diagnostics retain #191's `MEDUI-E`
  registry; malformed committed screen packages retain their `SCP` family, and the driver owns the
  stable `VUI` family for planning, artifact binding, execution and rendered findings.
- #252 implements the checks and #253 implements the host-only `MduXVerifyUiLib` /
  `mdux-verify-ui` driver. #254 still owns the artifact and #255 the CI leg, in that order.

## References
- ADR-004: Trust zones in C++ — why the driver and the writer are not governed
- ADR-005: Error handling and exceptions policy — the no-throw rule the checks are held to
- ADR-007: Evidence pipeline doctrine — decisions 2, 4, 5 and 6, the pattern `verification.json`
  follows and the four legs the re-derivation runs on
- ADR-011: The deterministic `.medui` compile boundary — the golden predicate and its inputs
- ADR-012: What a compiled screen emits — decision 4, the sidecar and the single-implementation rule
- ADR-013: Verified Apple Silicon macOS toolchain — why the pixel-labelled suite may not be skipped
- `tools/medui/Goldens.cppm` — `collectGoldens()` and the closed `CvCheck` set
- `tests/render/PixelTests.cpp` and `tests/render/ScreenPixelTests.cpp` — the exact comparator, and
  the rendered golden consumer that replaced the tripwire when decision 5 landed
- `tools/verify/Diff.cppm` — why the failure image is an attachment rather than an artifact, and why
  its encoder is written rather than linked
- `cmake/MduXCompileScreen.cmake` — the `verify.screen.<id>` registration decision 6 describes
- `docs/iec62304/03-development-process.md` — the §5.7 scope limit quoted above
- [TrustSC's ADR series](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) — the
  sibling project's rendered-truth decision, which epic #16 takes its shape from
- Issues #16, #17, #196, #244 and #251-#255

## Approval
- **Decision Date**: 2026-08-31
- **Approved By**: Project maintainer
- **Review Date**: reviewed 2026-09-02 when #253 first ran against a drawn, golden-eligible
  textless fixture; again the same day when #254 committed the first `verification.json` - which
  recorded three `NothingPainted` findings, exactly the consequence this record predicted for the
  current screen; and again when #255 added decisions 5 and 6, which turned those three findings into
  `Held` and put the gate on three CI legs. Review again when #257 and #258 draw a reading inside a
  field, since decision 5's `ColorHash` consequence constrains how they may.
