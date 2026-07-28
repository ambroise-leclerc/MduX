# IEC 81001-5-1:2021 — Security verification, vulnerability handling and update management

Confirming that security properties hold, handling vulnerabilities and defects once found, and
getting a fix to a device that is already deployed.

Practice categories are named rather than numbered throughout this directory; see
[`README.md`](README.md) for what this corpus asserts and what it does not.

## Security verification and validation
<!-- pointer: MduX verifies two properties this practice cares about - the trust-zone dependency boundary and cross-toolchain byte identity - and fuzzes none of its parsing surfaces, which is a gap rather than a scope exclusion. -->

Testing that a security property actually holds, rather than asserting it was designed in.

Two MduX mechanisms verify properties this practice cares about. Neither was written as a security
test, and both are stated here for what they check rather than for what they were for:

| Mechanism | The property it verifies | The limit |
|---|---|---|
| `mdux_verify_trust_zones()` at configure time | No governed target's link closure reaches Vulkan or a windowing library | Verifies the dependency boundary; says nothing about defects in the code inside it |
| Cross-toolchain byte-identity checks in CI ([ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md)) | Every generated artifact re-derives to identical bytes from its recorded inputs | Covers the build, not a deployed system; detects divergence, not malice in the inputs themselves |

What is absent is the security testing this practice is mostly about: fuzzing of parsing surfaces,
penetration testing, abuse-case testing against the threat model. MduX has parsing surfaces — the
JSON reader, the TOML subset, and eventually the `.medui` compiler — and none of them is fuzzed
today. That is a real gap, not a scope exclusion, and it is stated here rather than left for a
reader to notice by absence.

## Vulnerability and defect management

MduX tracks defects in GitHub Issues, with the general-purpose-tracking limits already set out in
[`../iec62304/07-problem-resolution-process.md`](../iec62304/07-problem-resolution-process.md).

The security-specific parts this practice asks for and MduX does not have:

- **a coordinated disclosure route** — no `SECURITY.md`, no security contact, no embargo process. A
  researcher who found a vulnerability today would have to open a public issue, which is the wrong
  channel for a security report;
- **security triage** distinct from ordinary defect triage, with severity assessment against a
  threat model;
- **a notification path** to downstream integrators when a vulnerability is found.

All three are gaps rather than exclusions. A library a device depends on is part of that device's
attack surface, and the manufacturer's own vulnerability handling process cannot cover a
dependency that has no way to tell it anything.

## Security update management

Getting a security fix onto a device that is already in the field.

MduX has no deployed product, so it has no update mechanism to describe — the same honest gap
already recorded for maintenance in
[`../iec62304/04-maintenance-process.md`](../iec62304/04-maintenance-process.md). What a
manufacturer needs from this library is narrower and does exist in part: versioned releases whose
contents are verifiable, so that "which MduX is in this device" has an answer. The evidence
pipeline supplies the verifiability; semantic versioning supplies the identity.

What is missing is the part that only matters once there are downstream users: an advisory feed, a
statement of which versions a fix applies to, and a supported-version policy. None of these can be
usefully invented before there is anyone to notify.

## Security documentation and guidance

Documenting security-relevant information for the integrators and users who need it.

This corpus and this project's ADRs are the closest MduX has today. The caveat stated throughout
this directory applies with full force here: none of them was written against IEC 81001-5-1's
documentation requirements, so they should be read as material a manufacturer can use, not as a
deliverable this standard asks for and MduX has produced.
