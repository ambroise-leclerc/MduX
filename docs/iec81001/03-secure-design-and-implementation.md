# IEC 81001-5-1:2021 §5.3–§5.5 — Secure design and implementation

Building security properties into the architecture and the code, rather than adding them after the
fact.

Clause numbering verified against IEC 81001-5-1 Edition 1.0 (2021-12); see
[`README.md`](README.md) for the review record.

## §5.3–§5.4 Software architectural design and software design

### Attack surface as an architectural constraint

MduX's trust-zone architecture is the clearest instance of a design-time security property in this
repository. A governed module's link graph is mechanically prevented from reaching Vulkan or a
windowing library — not by convention, and not by a review checklist, but by
`mdux_verify_trust_zones()` failing the configure step. The `Justification` for this is recorded
under security risk management, where the assessment consuming it lives; see
[`02-security-risk-management.md`](02-security-risk-management.md).

What this establishes is bounded and worth stating precisely: it fixes *what code can execute* in
the governed zone. It says nothing about whether that code is free of vulnerabilities, and nothing
at all about the adapter zone, which links Vulkan by design.

### Least privilege in the module graph

The governed zone depends on `std` and nothing else. That is a stronger statement than "few
dependencies": it means the audited source plus a standard library implementation fully determine
governed behaviour, so a driver update or a transitively-pulled third-party library cannot change
it. For a security assessment this collapses a large class of "what else is in the process" to a
short list.

## §5.5 Software unit implementation and verification

### Error handling that cannot escape

The governed zone is `noexcept` throughout and returns `std::expected` rather than throwing
([ADR-005](../adr/ADR-005-error-handling-and-exceptions-policy.md)). The security-relevant part is
not the ergonomics: an unhandled exception crossing a boundary a caller assumed was `noexcept` is
a `std::terminate`, and a reachable `std::terminate` in a device is at minimum an availability
failure. Making the boundary a compile-time property removes that class rather than handling it.

### Fail-closed self-checks on shipped artifacts

Where MduX ships a derived artifact rather than only source, the consumer verifies it before use.
The clearest planned instance is `Classifier1D`'s digest check (issue #62): a model whose bytes do
not match the digest compiled into the binary refuses to load, rather than running on data nobody
verified. That is an integrity control on a shipped artifact — the property the supply-chain half
of this standard is most concerned with — and it is fail-closed by construction.

This is planned, not present. It is named here because it is one of this
corpus's natural hooks, and because stating the intended mechanism is different from claiming it
exists.

### Verified derivation of every generated artifact

Everything MduX generates is re-derived and byte-compared in CI
([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)). A generated file that has been edited by
hand, or produced by a toolchain other than the recorded one, is a build failure. The security
reading of this is that the shipped artifact and the reviewed source cannot silently diverge.

## What no MduX mechanism covers

Secure implementation as this standard means it also includes coding standards enforced across the
codebase, static analysis tuned for security defect classes, memory-safety review of the adapter
zone, and secrets handling. MduX runs `clang-tidy` and compiles warnings-as-errors, which is
general code hygiene rather than a security programme, and has no secrets to handle. A manufacturer
should not read the mechanisms above as covering this practice; they cover parts of it.
