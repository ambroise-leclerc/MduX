# IEC 62304:2006 §6 — Software maintenance process

Maintenance is the development process's mirror image, run after release: a plan for how changes
reach a shipped product, and a route from an observed problem back through analysis to an
implemented, verified change.

## §6.1 Establish software maintenance plan

A maintenance plan states how a manufacturer will collect feedback on released software, decide
which issues need a change, and carry that change through the same rigor the original development
process required. MduX has no released product yet, so it has no maintenance plan yet either —
recording that plainly is more useful than a placeholder document with nothing behind it.

## §6.2 Problem and modification analysis
<!-- pointer: No MduX mechanism for field problems, since there is no field yet; the ADRs' Consequences sections are the nearest analog, and they run at design time. -->

Before a reported problem becomes a change, its cause and its blast radius need establishing:
which software items are affected, and whether the fix itself could introduce new risk. This
project's nearest existing analog is the discipline behind its ADRs' "Consequences" sections —
each one records not just what changes, but what risk the change accepts or mitigates — though that
happens at design time, not in response to a reported field problem, since there is no field yet.

## §6.3 Modification implementation
<!-- pointer: A maintenance change to MduX runs through the ordinary pull-request process; that route is the mechanism, not a separate one this file would invent. -->

A modification is implemented and verified through the same development-process clauses as new
work — §5.1 through §5.8 apply again, at whatever scope the change actually touches. This corpus
does not restate those clauses here; a maintenance change to MduX runs through the ordinary PR
process this repository already uses; that route is the mechanism, not a separate one this file
would need to invent.
