# ISO 13485:2016 §5 — Management responsibility

§5 is squarely about an organization's own management structure — commitment, policy, planning,
defined authority, and periodic review, exercised by people occupying management roles in a company
with a certified QMS. MduX is an open-source library with a single maintainer and no certified QMS;
most of this clause has no honest MduX analog, and stretching one would be exactly the kind of
decorative citation [`docs/governance/citation-convention.md`](../governance/citation-convention.md)
warns against.

## §5.1–§5.3 Management commitment, customer focus, quality policy

No MduX-specific mechanism. A device manufacturer's own management commitment, customer focus, and
quality policy govern their integration of MduX; nothing in this repository substitutes for them.

## §5.4 Planning
<!-- pointer: No MduX mechanism: the GitHub epic and issue structure is project planning, not the quality-objective planning this clause asks a certified organization for. -->

The nearest honest analog is this project's own roadmap structure — epics and their child issues on
GitHub, each with a stated blocking order (this epic, #8, is blocked by #7 and blocks nothing until
its own children land). That is project planning, not the quality-objective planning this clause
asks a certified organization for, and this corpus does not claim otherwise.

## §5.5 Responsibility, authority and communication

MduX's closest concrete instance is the branch protection requiring pull requests before `master`
changes — an authority boundary enforced by GitHub, not only stated in a policy document. It is
narrower than what this clause asks of a manufacturer's organizational structure, and there is no
`CODEOWNERS`-style routing of review responsibility yet.

## §5.6 Management review

No MduX-specific mechanism. A periodic management review of QMS effectiveness is an organizational
practice this repository cannot perform on a manufacturer's behalf.
