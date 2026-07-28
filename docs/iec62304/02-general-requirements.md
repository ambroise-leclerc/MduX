# IEC 62304:2006 §4 — General requirements

## §4.1 Quality management system context
<!-- pointer: IEC 62304 expects these processes to run inside a manufacturer's QMS; MduX has none, and docs/iso13485/ covers that side rather than this file. -->

IEC 62304 does not stand alone: it expects the software life cycle processes it describes to run
inside a manufacturer's broader quality management system, typically one built to ISO 13485. This
corpus's [`docs/iso13485/`](../iso13485/) directory covers that side; nothing in this file repeats
it.

## §4.2 Risk management context

Every clause in this directory that names a risk-related consequence is scoped by this one: a
requirement's rigor is proportional to what happens if the software fails, not to the software's
size or complexity. MduX's structural response to this is architectural rather than procedural
where possible — see [ADR-004](../adr/ADR-004-trust-zones-in-cpp.md)'s trust-zone split, which
makes an entire category of failure (a governed module reaching platform/graphics code it should
never touch) a compile error instead of a review item. [`docs/iso14971/`](../iso14971/)
is where the risk-management process itself gets a clause-accurate treatment; this file only
establishes that classification decisions elsewhere in this corpus are risk-driven, not
size-driven.

## §4.3 Software safety classification

IEC 62304 defines three safety classes for a software system, ordered by the severity of harm a
software failure could cause to a patient, operator, or bystander — from software that cannot
contribute to a hazardous situation, up to software whose failure could contribute to death or
serious injury. The standard does not provide a formula for assigning a class; it is a documented
judgment call made against the device's risk analysis, and higher classes carry more extensive
development and verification requirements at every later clause in this corpus.

**MduX's own components are declared Class A** — code that renders a UI, bakes an asset, or infers
from a classifier is not itself the safety function; a device integrating MduX is responsible for
its own classification decision, informed by how it uses these components. Where a later clause
describes a requirement that scales with class (test coverage in §5.5–§5.7, for instance), this
corpus states the Class A baseline and does not extrapolate a Class B/C claim MduX has not earned.
TrustSC, MduX's sibling project, targets Class B/C directly and its documentation should be
consulted for that scope rather than assuming parity here.

A classification decision, once made, is itself something a Justification object
(see [`docs/governance/citation-convention.md`](../governance/citation-convention.md)) can record:
which class, why, and which risk analysis it was drawn from. No such Justification exists yet in
this repository — MduX has not yet reached the point of having a device-level risk analysis to
classify against — but the schema is ready for one once issue #29 (ISO 14971) and a real risk
analysis exist to cite.
