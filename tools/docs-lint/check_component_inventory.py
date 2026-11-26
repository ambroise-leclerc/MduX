#!/usr/bin/env python3
"""Fails when the component inventory disagrees with the compiler, with ADR-026, or with the tree.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

`docs/medui/component-inventory.json` records every component epic #388 names against the twelve
admission criteria ADR-026 defines. Half of that file is judgement - whether a criterion is met -
and no script can check a judgement. This one checks the half that is mechanical, which is also the
half that goes stale without anyone noticing:

- the `admitted` and `provisional` components together are exactly the components
  `docs/medui/grammar.json` lists, so the inventory cannot claim a component the compiler rejects
  or omit one it accepts;
- the floor is the one ADR-026 fixes, and no component in the dictionary falls short of it;
- an `admitted` component records no shortfall and a `provisional` one records at least one, so
  neither status can be claimed for a component the criteria describe differently;
- the criteria are exactly the `AC-NN` headings ADR-026 carries, in order;
- a component in the dictionary states every criterion, with a status from the closed set;
- a `met` names evidence, and every evidence path exists;
- a `not-applicable` is recorded only against a criterion ADR-026 makes conditional, and states
  why in `reason`; a universal criterion - one whose requirement holds for every entry of the
  dictionary, AC-01, AC-02 and AC-04 among them - is never `not-applicable`, so the status cannot
  be used to step around the floor or to reach `admitted` without evidence;
- a `partial` or `unmet` names the epic child that closes it - or the children, as a list, when
  what is owed falls to more than one - and each is declared;
- a `proposed` component names a declared workstream and asserts no criteria, because nobody can
  have met a criterion for a component that has not been designed;
- a `declined` component records why.

Usage:
    python3 tools/docs-lint/check_component_inventory.py [--repo-root PATH]

Exit status 0 when the inventory is consistent, 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

INVENTORY = "docs/medui/component-inventory.json"
GRAMMAR = "docs/medui/grammar.json"
DECISION = "docs/adr/ADR-026-component-admission-criteria.md"

COMPONENT_STATUSES = frozenset({"admitted", "provisional", "proposed", "declined"})
IN_DICTIONARY = frozenset({"admitted", "provisional"})
# ADR-026's floor: the criteria a component may not fall short of in any status. Fixed here rather
# than read from the inventory alone, so that editing the file cannot lower it.
FLOOR = ("AC-01", "AC-02", "AC-04", "AC-05")
# The criteria whose subject a component may simply lack: a value bound at run time (AC-05), an
# interaction (AC-06), an interaction or data-quality state (AC-07), a drawn face (AC-08). Every
# other criterion is universal, and that is the default on purpose: a criterion added to ADR-026
# cannot be waived until someone lists it here, in a reviewed change to this file.
CONDITIONAL = frozenset({"AC-05", "AC-06", "AC-07", "AC-08"})
SHORTFALLS = frozenset({"partial", "unmet"})
CRITERION_STATUSES = frozenset({"met", "partial", "unmet", "not-applicable"})
CRITERION_HEADING = re.compile(r"^### (?P<id>AC-\d{2}) (?P<title>.+?)\s*$", re.MULTILINE)
PREFIX = "mdux-component-inventory"


def decision_criteria(text: str) -> list[tuple[str, str]]:
    """Return the (id, title) pairs ADR-026 defines, in document order."""
    return [(match["id"], match["title"]) for match in CRITERION_HEADING.finditer(text)]


def check_criterion(name: str, criterion: str, entry: object, children: set[str], root: Path) -> list[str]:
    """Check one criterion entry of one component in the dictionary."""
    where = f"{name} {criterion}"
    if not isinstance(entry, dict):
        return [f"{where}: must be an object"]
    status = entry.get("status")
    if status not in CRITERION_STATUSES:
        return [f"{where}: status {status!r} is not one of {sorted(CRITERION_STATUSES)}"]
    findings: list[str] = []
    if status == "met":
        evidence = entry.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            findings.append(f"{where}: 'met' names no evidence")
        else:
            for path in evidence:
                if not isinstance(path, str) or not (root / path).exists():
                    findings.append(f"{where}: evidence path {path!r} does not exist")
    elif status in SHORTFALLS:
        if criterion in FLOOR:
            findings.append(f"{where}: a floor criterion cannot be '{status}'; the component may not be in the dictionary")
        closed_by = entry.get("closedBy")
        owners = closed_by if isinstance(closed_by, list) else [closed_by]
        if not owners or len(set(map(str, owners))) != len(owners) or any(owner not in children for owner in owners):
            findings.append(f"{where}: '{status}' must name a declared child in closedBy, found {closed_by!r}")
        if not entry.get("note"):
            findings.append(f"{where}: '{status}' must say what is missing in note")
    else:
        if criterion not in CONDITIONAL:
            findings.append(
                f"{where}: 'not-applicable' is not available for a universal criterion; "
                f"only {sorted(CONDITIONAL)} may not apply"
            )
        reason = entry.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            findings.append(f"{where}: 'not-applicable' must say why in reason")
        if "evidence" in entry or "closedBy" in entry:
            findings.append(f"{where}: 'not-applicable' claims nothing and owes nothing; it carries only a reason")
    return findings


def check(root: Path) -> list[str]:
    """Return every finding for the inventory under `root`; empty when it is consistent."""
    try:
        inventory = json.loads((root / INVENTORY).read_text(encoding="utf-8"))
        grammar = json.loads((root / GRAMMAR).read_text(encoding="utf-8"))
        decision = (root / DECISION).read_text(encoding="utf-8")
    except (OSError, json.JSONDecodeError) as error:
        return [f"cannot read an input: {error}"]

    findings: list[str] = []

    declared = [(item.get("id"), item.get("title")) for item in inventory.get("criteria", [])]
    defined = decision_criteria(decision)
    if declared != defined:
        findings.append(
            f"criteria differ from the headings of {DECISION}: inventory has {declared}, the decision defines {defined}"
        )
    criterion_ids = [identifier for identifier, _ in declared]

    if tuple(inventory.get("floor", ())) != FLOOR:
        findings.append(f"floor is {inventory.get('floor')!r}; {DECISION} fixes it at {list(FLOOR)}")

    children = {item.get("id") for item in inventory.get("children", [])}
    if len(children) != len(inventory.get("children", [])):
        findings.append("children: an id is declared twice")

    components = inventory.get("components", [])
    names = [component.get("name") for component in components]
    for name in sorted({name for name in names if names.count(name) > 1}, key=str):
        findings.append(f"{name}: listed more than once")

    in_dictionary = {c.get("name") for c in components if c.get("status") in IN_DICTIONARY}
    in_grammar = {component["name"] for component in grammar.get("components", [])}
    for name in sorted(in_grammar - in_dictionary):
        findings.append(f"{name}: in {GRAMMAR} but neither 'admitted' nor 'provisional' in the inventory")
    for name in sorted(in_dictionary - in_grammar):
        findings.append(f"{name}: in the dictionary according to the inventory but absent from {GRAMMAR}")

    for component in components:
        name = component.get("name")
        status = component.get("status")
        if not isinstance(name, str) or not name:
            findings.append("a component has no name")
            continue
        if status not in COMPONENT_STATUSES:
            findings.append(f"{name}: status {status!r} is not one of {sorted(COMPONENT_STATUSES)}")
            continue
        criteria = component.get("criteria")
        if status in IN_DICTIONARY:
            if not isinstance(criteria, dict) or list(criteria) != criterion_ids:
                findings.append(f"{name}: must state exactly the criteria {criterion_ids}, in order")
                continue
            for criterion, entry in criteria.items():
                findings.extend(check_criterion(name, criterion, entry, children, root))
            owed = sorted(
                criterion
                for criterion, entry in criteria.items()
                if isinstance(entry, dict) and entry.get("status") in SHORTFALLS
            )
            if status == "admitted" and owed:
                findings.append(f"{name}: 'admitted' but {owed} are not met; it is 'provisional'")
            if status == "provisional" and not owed:
                findings.append(f"{name}: 'provisional' but records no shortfall; it is 'admitted'")
            continue
        if criteria is not None:
            findings.append(f"{name}: a '{status}' component asserts criteria")
        if status == "proposed" and component.get("workstream") not in children:
            findings.append(f"{name}: workstream {component.get('workstream')!r} is not a declared child")
        if status == "declined" and not component.get("reason"):
            findings.append(f"{name}: 'declined' records no reason")
    return findings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    arguments = parser.parse_args(argv)
    findings = check(arguments.repo_root)
    for finding in findings:
        print(f"{PREFIX}: {finding}", file=sys.stderr)
    if findings:
        print(f"{PREFIX}: {len(findings)} finding(s)", file=sys.stderr)
        return 1
    print(f"{PREFIX}: {INVENTORY} is consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
