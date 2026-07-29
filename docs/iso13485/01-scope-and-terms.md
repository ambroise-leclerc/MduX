# ISO 13485:2016 §1–§3 — Scope, normative references, terms and definitions

## §1 Scope

ISO 13485:2016 specifies requirements for a quality management system used by an organization
across a medical device's life cycle — design, production, installation, and servicing — and by
suppliers providing product to such an organization. It is written to support regulatory
requirements rather than to be a general quality-management standard; the intent is a QMS whose
records can demonstrate, to a regulator, that the organization consistently meets applicable
requirements.

MduX is a software library, not the organization operating a device's QMS. Where this corpus cites
a clause, it is describing what MduX's own repository practices contribute to a device
manufacturer's QMS — evidence a manufacturer could point to, not a QMS in its own right. Most of
this standard (facility requirements, supplier audits, servicing records) has no MduX analog at all,
and this corpus says so rather than manufacturing a citation.

## §2 Normative references

ISO 13485:2016 is written to be read alongside ISO 9000 (fundamentals and vocabulary) and, for the
risk-management activities it requires throughout, ISO 14971. MduX's own regulatory corpus covers
IEC 62304 ([`../iec62304/`](../iec62304/)) directly; `docs/iso14971/` (issue #29) is where risk
management gets the same clause-accurate treatment.

## §3 Terms and definitions

As with the equivalent IEC 62304 section, this corpus does not restate the standard's glossary —
restating a definition is restating normative text. The terms below recur across this directory;
each points at where MduX makes the concept concrete rather than defining it in prose.

| Term | Where MduX makes it concrete |
|---|---|
| Documented information | Every ADR, this corpus's own clause files, and `docs/governance/` are documented information in this standard's sense: recorded, version-controlled, and reviewable through git history rather than only asserted. |
| Design and development controls | [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust-zone architecture and its mechanical verification (`cmake/MduXTrustZones.cmake`) is MduX's clearest instance — a design decision enforced at every build, not only at review time. |
| Nonconformity | A failed CI check — a rejected `evidence` byte-comparison, a `mdux-docs-lint` finding, a trust-zone violation — is MduX's concrete form of a detected nonconformity: specific, reproducible, and blocking merge until resolved. |
| Corrective action | GitHub issues and the pull requests that resolve them are MduX's corrective-action record today — see [`06-measurement-analysis-improvement.md`](06-measurement-analysis-improvement.md) for where this is honest about not yet being a formal CAPA process. |
