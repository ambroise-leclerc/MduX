# IEC 62366-1:2015 §4 — Principles

Verified against an authorized copy of IEC 62366-1:2015 Edition 1.0 (2015-02); see
[`README.md`](README.md) for the review record.

The earlier title for this clause — "General requirements for the application of usability
engineering to medical devices" — was wrong in a way worth recording: **"General requirements" is
§4.1, not §4.** The clause itself is titled *Principles*. Citing §4 under its sub-clause's name
would send a reader to the wrong scope, and that is the class of error the verification pass
existed to find.

This clause establishes that a manufacturer must apply a usability engineering process to their
device, document it, and integrate it with the device's overall risk management (ISO 14971) and
quality management system (ISO 13485). It also permits a manufacturer to justify not applying the
full process to interface elements with no bearing on safety.

MduX has no device-level usability engineering process to establish, for the same reason it has no
device-level risk analysis (see [`../iso14971/`](../iso14971/)): it is a UI SDK, not the device. The
scoping judgment this clause asks a manufacturer to make — which interface elements are safety-
relevant — is theirs to make about their own device's UI, informed by what MduX actually renders,
not something MduX can pre-decide on their behalf.

## §4.1 General requirements

The obligation to apply a usability engineering process and to keep its records. The standard
divides it into three sub-clauses.

MduX provides no mechanism against this clause and cannot: applying a usability engineering process
requires a device, its users and its use environment, none of which a rendering library has.

## §4.1.1 Usability engineering process

That a process exists and is applied. Device-level; MduX supplies no process, having no users of
its own.

## §4.1.2 Risk control as it relates to user interface design

The join between this standard and ISO 14971: user-interface design decisions are risk controls and
are recorded as such.

MduX's planned mechanisms attach to this sub-clause more directly than to any other in §4. A text
budget the compiler enforces, or a `@safety_critical` annotation binding an element to the
requirement it satisfies (issue #15), is a design-time risk control rather than an instruction to a
user. None of
it is built; see [`03-usability-engineering-process.md`](03-usability-engineering-process.md) under
§5.2 for what each planned mechanism would and would not establish.

## §4.1.3 Information for safety as it relates to usability

Where risk cannot be reduced by design, information for safety is the last resort, and this
sub-clause constrains how far it may be relied on.

No MduX mechanism, and the boundary is worth stating plainly: MduX renders whatever text a device
gives it. Whether that text constitutes adequate information for safety is a judgement about
content and comprehension, which is the manufacturer's.

## §4.2 Usability engineering file

The record of the process — what was specified, evaluated, found and concluded.

Device-level. MduX supplies no usability engineering file. What it does supply, and what can be
referenced *from* one, is the committed byte-verified evidence described in
[ADR-007](../adr/ADR-007-evidence-pipeline-doctrine.md): provenance for the artifacts a screen is
built from, not evidence about how that screen was evaluated with users.

## §4.3 Tailoring of the usability engineering effort

The permission to scale the effort to the risk, and to justify not applying the full process to
interface elements with no bearing on safety — a scoping decision the manufacturer must document
and defend, not skip silently.

Device-level. A manufacturer tailoring their effort may reasonably treat parts of a UI built on
MduX differently from others; MduX neither makes nor records that judgement.
