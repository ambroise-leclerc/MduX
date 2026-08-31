# ADR-008: Zero-SOUP ML inference

## Status
Accepted (2026-08-03)

## Context
Manufacturers want to embed learned models in a device — 1-D signal classification is the entry
case — without pulling ONNX Runtime, PyTorch, LiteRT or `tract` into a Class B/C build. Any of those
is SOUP of a particularly awkward kind: large, fast-moving, transitively dependent on a
general-purpose numerics stack, and impossible to characterise as "the code that computes this
device's output" in a way a reviewer can follow.

There is a second, less obvious requirement. Manufacturers want to prototype against openly
available pretrained weights *before* they have their own qualified training data, and then move to
production **without re-validating the inference engine**. If swapping weights means recompiling
application source, the demonstrator and the product are different software and the demonstrator's
verification evidence buys nothing.

The evidence pipeline (ADR-007) already answers "which tool, at which version, from which input,
produced this artifact" for baked assets, and `mdux.evidence.digest` (issue #50) already exists and
already anticipates this use. What is missing is the decision about *where inference itself lives*
and what makes its arithmetic trustworthy across two compilers and a device FPU.

This ADR is the MduX equivalent of [TrustSC ADR-017](https://github.com/ambroise-leclerc/TrustSC),
adapted to C++. The adaptation is not cosmetic: two of the decisions below exist because C++ gives
away guarantees Rust holds by default.

## Medical Device Considerations

### IEC 62304 implications (software lifecycle)
- **The inference engine is device software, and it is ours.** With no foreign inference stack
  linked into a device target, there is no SOUP item to characterise, no upstream release cadence to
  track, and no third-party defect list to monitor for this subsystem. The v1 kernel set is a few
  hundred lines of plain loops.
- **A model package is a configuration item, not code.** It is baked offline, committed under
  `generated/model/<id>/`, and byte-verified in CI exactly like every other artifact under ADR-007.
  Its provenance is recorded in `report.json`.
- **Demonstrator and production differ by one committed JSON file plus its sidecar.** That is what
  makes verification of the engine reusable across the swap, and issue #64's weight-swap test is the
  evidence that the claim is true rather than aspirational.

### Risk management considerations
- **Silent floating-point divergence is the hazard this ADR is mostly about.** A device whose FPU,
  compiler or optimisation flags produce different arithmetic than the host that generated the model
  will classify differently from the system that was validated — and nothing about that failure is
  loud. Decisions 3 and 4 exist to convert it into a refusal to construct the classifier.
- **Fail closed, not fail degraded.** A golden-vector mismatch, a digest mismatch or an undersized
  scratch buffer returns an error from `create()`; the `Classifier1D` object is never constructed, so
  there is no partially-trustworthy object a caller can accidentally use.
- **A classifier output is not a diagnosis.** Nothing here establishes clinical validity of any
  model. The committed demonstrator carries an explicit statement to that effect in its recipe, and
  its presence in the tree must not be read as a validated clinical claim.

### Traceability requirements
- `weightsDigest` in the package binds the package to exactly one weight blob; `report.json` binds
  both back to the recipe and the source weights. The chain from imported weights to executing bytes
  is machine-checkable.
- `MlError` carries the layer, golden, and element indices plus the expected and actual bit
  patterns, so a field failure produces an incident record with the divergence in it rather than an
  enumerator.

## Decision

### 1. Four modules, not three
TrustSC splits schema / authoring / runtime. MduX adds a fourth:

| Module | Zone | Imported by |
|---|---|---|
| `mdux.ml.schema` (#57) | governed | everything |
| `mdux.ml.kernels` (#58) | governed | the runtime **and** the host baker |
| `mdux.ml.runtime` (#62) | governed | device applications |
| `tools/ml/*` (#60, #61) | host tools | the baker only, never a device target |

`mdux.ml.kernels` is separate specifically so that both the device runtime and the offline
golden-vector generator import **the same module**. In Rust, "authoring uses the same kernels as the
runtime" is a discipline maintained by review. Here it is one definition, one object file, one set
of compile flags. If the host and the device disagree about what the model computes, that is the
FPU or the toolchain — it cannot be a second implementation that drifted, because there is no second
implementation.

That identity is what makes a golden-vector mismatch *diagnostic*. Without it, a mismatch has two
possible causes and localising it is guesswork.

### 2. Weights are data, not code
A `ModelPackage` is baked offline and committed under `generated/model/<id>/`, byte-verified like
every other ADR-007 artifact. The weights themselves are a `weights.bin` sidecar, supplied to the
runtime by the caller — from an mmap, from ROM, from flash, or from a blob linked in via
`mdux_embed_blob()` (#64).

The runtime performs **no I/O**. That is what keeps it usable on a device with no filesystem, and it
is why `TensorRef` holds a byte offset rather than a pointer: an offset lets `ModelPackage` be a
`constexpr` object while the weights live somewhere else entirely. A pointer would make the package
non-`constexpr` and force multi-megabyte weights into generated source, which MSVC in particular
does not survive in reasonable time.

`mdux-mlemit` renders the committed `package.json` into a build-tree module interface and header.
Both forms hold the package id and schema version, weight digest and byte length, layer descriptors,
input/output/scratch dimensions, and golden bit patterns. Both carry a `static_assert` over
`ModelPackage::validate()`. A device target therefore links `MduX::Core`,
parses nothing at startup, and still receives weights separately. The host-tools `PackageLoad`
module remains available for tooling and tests that intentionally load packages dynamically.

Swapping demonstrator weights for clinically-qualified weights is therefore a re-bake with
**zero application source change**.

### 3. Determinism by strictly-ordered scalar arithmetic
Every accumulation happens in a single fixed order — Conv1D sums over input channel then kernel tap;
Dense sums over input feature — using plain `f32` multiply-then-add.

- **Never `std::fma`.** A fused multiply-add rounds once where the scalar sequence rounds twice, and
  produces a different `f32`.
- **Never SIMD intrinsics.** Same rounding problem, plus lane-order dependence in the reduction.
- The kernels are written as plain, readable loops. **That is not a compromise pending
  optimisation — it is the specification.**

One thing that is *not* a hazard, recorded here because it looks like one: auto-vectorisation cannot
reorder floating-point reductions without `-ffast-math`. `-O2` and `-O3` are safe. Nobody should be
disabling optimisation on these files out of superstition, and nobody should be "fixing" the plain
loops into intrinsics.

C++ does, however, give away two guarantees Rust holds, so correct kernel source is necessary but
not sufficient (#59):

- **FP contraction is on by default.** GCC and Clang default to `-ffp-contract=fast` outside strict
  modes and will fuse `acc += w * x` into an FMA regardless of what decision 3 says. The kernels
  target sets `-ffp-contract=off -fno-fast-math` (`/fp:precise /fp:contract` on MSVC) explicitly.
- **`-ffast-math` can arrive from anywhere** — a preset, a toolchain file, a dependency's interface
  flags. A CMake guard inspects the effective compile options of every governed target and fails the
  configure step on `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations` or `/fp:fast`. This is
  the guard that catches the change nobody thought was related to ML.

There is a third, which only became visible once `Sigmoid` and `Softmax` were implemented:
**`std::exp` cannot carry this claim.** Neither the C++ standard nor IEEE 754 requires a
correctly-rounded `expf`, and glibc and the UCRT are different implementations. A golden vector
containing a sigmoid or softmax output could therefore differ in its low bits between the Linux and
Windows CI legs *while both compilers were behaving correctly* — which would break
`ml.determinism.crossToolchain`, the strongest single piece of evidence in this epic.

So `mdux.ml.kernels` provides its own `expF32()`: range reduction to `x = k·ln2 + r` followed by a
degree-7 polynomial, using only IEEE-754 `f32` add, subtract, multiply and divide, an integer
truncation for the floor, and an exponent built directly as a bit pattern. Every one of those is
exactly specified, so the result is identical on any conforming toolchain. Note the floor is done
by integer conversion rather than `std::floor` specifically so that the sentence below stays true. Accuracy is a few ULP against `std::exp`, which is irrelevant
to a classifier — and, per the `std::fma` argument above, is emphatically not the property being
maximised. This also removes libm from the device-side dependency argument entirely, which is
consistent with the rest of the ADR rather than an exception to it.

### 4. Golden vectors baked into the package, re-run at construction, compared bitwise
The baker generates golden input→output pairs by running the model through `mdux.ml.kernels` — the
same governed module the device will execute — and stores them in the package as `u32` **bit
patterns**.

`Classifier1D::create()` re-runs every one of them and compares bit patterns. Any divergence returns
an error and the object is never constructed.

Comparison is **bitwise, never an epsilon**. An epsilon comparison would silently accept exactly the
floating-point drift this mechanism exists to detect; it would convert the one loud signal that the
toolchain miscompiled the kernels into a tolerance nobody tuned deliberately. Goldens are stored as
`u32` rather than decimal for the same reason — a decimal round-trip is a lossy re-encoding of the
thing being checked.

This is a genuine Class C safety control that happens to run at startup, not a unit test that runs
late. It detects toolchain miscompilation, target floating-point drift, and a corrupted or
mismatched package **before the device classifies anything real**. It costs bounded startup work and
nothing per frame.

### 5. v1 scope is `f32` and eight operations
`Dense`, `Conv1D`, `MaxPool1D`, `AvgPool1D`, `Flatten`, and the `Relu` / `Sigmoid` / `Softmax`
activations. `f32` only; the schema rejects every other dtype.

`int8` quantisation, recurrent layers and attention are **out of scope**. They need their own ADR and
their own determinism argument — quantisation in particular changes the rounding story completely —
not an unreviewed enumerator added to `LayerKind` by whoever needs it first. Model complexity is
deliberately capped by this kernel set.

### 6. Weight import adds no runtime-adjacent SOUP
safetensors is hand-parsed: a `u64` little-endian header length, a JSON header, then raw tensor
bytes. It is small enough to parse directly with the evidence JSON reader, which is the whole reason
it was chosen over a format needing a real dependency.

The parser lives in `tools/ml/`, the host-tools zone. It is never linked into a device target, never
run in CI, and never fed by a build step — so a malformed or hostile weights file cannot affect a
build, and the SOUP register (issue #33) stays short. It validates aggressively anyway: header
length within the file, every byte range in bounds and non-overlapping, declared shape consistent
with byte length, dtype recognised.

## Alternatives Considered

**Link ONNX Runtime (or LiteRT, or `tract`) and be done.** Rejected. It is the largest single SOUP
item this project would ever take on, it moves fast, and its numerics are not specified in a way that
supports a bit-exactness argument between a host baker and a device. Nothing about the v1 scope
justifies it.

**Generate C++ source per model instead of baking a data package.** This is a real technique and it
produces fast code. Rejected because it makes weights *code*: swapping weights becomes a source
change, re-triggering compilation, review and — the expensive part — engine re-validation. It defeats
the requirement in Context.

**Epsilon-tolerant golden comparison.** Rejected, at length, in decision 4. The tolerance is the
failure.

**A reference implementation for golden generation, separate from the runtime kernels.** This is the
conventional split and it is what decision 1 exists to reject. Two implementations disagree
eventually, and when they do, a golden mismatch on device no longer localises the fault.

**`std::fma` for accuracy.** Genuinely more accurate per-operation, and rejected for that reason:
accuracy is not the property being maximised here, reproducibility across two compilers and an
unknown device FPU is. A more accurate result that differs between host and device is a worse
outcome than a less accurate one that is identical on both.

**Weights as a `constexpr` array in generated source.** Rejected on build cost — multi-megabyte
`constexpr` arrays take minutes to compile and can exhaust MSVC — and because it forecloses mmap and
ROM sourcing. `mdux_embed_blob()` (#64) covers the "no filesystem" case with a plain `.cpp` byte
array instead, which compiles in reasonable time precisely because it is not `constexpr`.

## Consequences

**Positive**
- No foreign inference stack is ever linked into a device target. The SOUP argument for this
  subsystem is "there is none", which is the only version of that argument that never expires.
- A demonstrator build and a production build differ by one committed JSON file and its sidecar.
- Bit-exactness is *demonstrated* across MSVC and GCC by `ml.determinism.crossToolchain`, not
  asserted. That is the strongest single piece of evidence in this epic.
- A field failure produces a structured divergence record, not a bare error code.
- The golden self-test costs bounded startup work and nothing per frame.

**Negative, and accepted**
- **The v1 kernel set is small**, and deliberately so. Some models simply cannot be expressed. The
  answer is a follow-up ADR, not an enumerator.
- **Scalar loops are slower than a vectorised library**, by a margin that grows with model size. For
  1-D signal classification at device sample rates this is not the binding constraint; if it ever
  becomes one, the fix is a documented reversal of decision 3 with its own determinism argument, not
  a quiet intrinsic.
- **The baker is another host tool to maintain**, and the safetensors parser is hand-written code
  that could have been a dependency. That is the trade this whole ADR makes on purpose.
- **Cross-toolchain evidence currently means MSVC plus GCC.** Clang is not in that set today because
  its CI leg is disabled (issue #48); adding it back is what makes the evidence stronger, and until
  then the claim must be stated as two toolchains, not three.

**Risks introduced**
- **`ml.determinism.crossToolchain` gets reduced to one CI leg for speed.** *Mitigation*: this ADR
  and issue #59 both record that the two legs *are* the evidence; dropping one is then a documented
  reversal rather than an unnoticed regression.
- **Someone "optimises" the kernels.** *Mitigation*: per-kernel comments stating the accumulation
  order is normative, a bit-exact Conv1D test that fails on any reordering, and the configure-time
  fast-math guard.
- **"No allocation in `predict`" decays silently.** *Mitigation*: issue #63 verifies it three
  independent ways — runtime `operator new` interposition, an object-file symbol scan, and
  clang-tidy — rather than by review discipline.

## Implementation Notes
- Governed modules follow ADR-004 (`std` only) and ADR-005 (`noexcept`, `std::expected`, never
  throws). `Classifier1D` holds only spans and PODs and is asserted trivially destructible.
- The baker registers through `mdux_bake_artifact()` (ADR-007) like every other baker, emits a
  `BakeReport` with a semantic `toolVersion`, resolved options, repository-relative paths, and **no
  commit SHA** — see ADR-007 decision 5 for why that field cannot exist.
- Golden inputs come from fixed patterns plus a seeded PRNG whose algorithm and seed are recorded in
  the recipe. **Never `<random>`'s default engine**: its output is not specified across standard
  library implementations, which would break byte-identity between the two CI legs — the exact
  property the cross-toolchain test exists to demonstrate.
- Host-tool diagnostics use the shared diagnostic envelope (issue #19), so `--format=json` gives an
  agent a machine-readable failure.
- `mdux-mlemit` produces a module interface plus a header fallback from one in-memory rendering.
  The files remain in the build tree; the committed JSON is the reviewed evidence artifact.
- x87 excess precision only bites 32-bit x86, where the FPU computes at 80 bits and rounds
  unpredictably on spill. Windows is 64-bit-only here; any 32-bit Linux target must require SSE2
  math or refuse to build.

## References
- ADR-004: Trust zones in C++ (this repository) — the governed/host-tools boundary decision 6 relies
  on, enforced mechanically by `mdux_verify_trust_zones()`
- ADR-005: Error handling and exceptions policy (this repository) — why the governed ML modules are
  `noexcept` and return `std::expected`
- ADR-007: Evidence pipeline doctrine (this repository) — the recipe→baker→committed-artifact
  pattern a model package is one instance of
- [TrustSC ADR-017](https://github.com/ambroise-leclerc/TrustSC/tree/main/docs/adr) — the decision
  this one adapts, and the source of the four-module deviation recorded above
- [safetensors format specification](https://github.com/huggingface/safetensors) — the container
  parsed by `tools/ml/Safetensors.cppm`

## Approval
- **Decision Date**: 2026-08-03
- **Approved By**: Project maintainer
- **Review Date**: when a second model architecture is baked, or when any of `int8`, recurrent
  layers or attention is first requested — whichever comes first, since that is the point at which
  decision 5's scope cap is actually load-bearing
