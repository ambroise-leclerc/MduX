#!/usr/bin/env python3
"""Fails when the component inventory disagrees with the compiler, with ADR-026, or with the tree.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

`docs/medui/component-inventory.json` records every component epic #388 names against the twelve
admission criteria ADR-026 defines. Half of that file is judgement - whether a criterion is met -
and no script can check a judgement. This one checks the half that is mechanical, which is also the
half that goes stale without anyone noticing:

- the `delivered` components are exactly the components `docs/medui/grammar.json` lists, so the
  inventory cannot claim a component the compiler rejects or omit one it accepts;
- the criteria are exactly the `AC-NN` headings ADR-026 carries, in order;
- a delivered component states every criterion, with a status from the closed set;
- a `met` names evidence, and every evidence path exists;
- a `partial` or `unmet` names the epic child that closes it, and that child is declared;
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

COMPONENT_STATUSES = frozenset({"delivered", "proposed", "declined"})
CRITERION_STATUSES = frozenset({"met", "partial", "unmet", "not-applicable"})
CRITERION_HEADING = re.compile(r"^### (?P<id>AC-\d{2}) (?P<title>.+?)\s*$", re.MULTILINE)
PREFIX = "mdux-component-inventory"


def decision_criteria(text: str) -> list[tuple[str, str]]:
    """Return the (id, title) pairs ADR-026 defines, in document order."""
    return [(match["id"], match["title"]) for match in CRITERION_HEADING.finditer(text)]


def check_criterion(name: str, criterion: str, entry: object, children: set[str], root: Path) -> list[str]:
    """Check one criterion entry of one delivered component."""
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
    elif status in ("partial", "unmet"):
        closed_by = entry.get("closedBy")
        if closed_by not in children:
            findings.append(f"{where}: '{status}' must name a declared child in closedBy, found {closed_by!r}")
        if not entry.get("note"):
            findings.append(f"{where}: '{status}' must say what is missing in note")
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

    children = {item.get("id") for item in inventory.get("children", [])}
    if len(children) != len(inventory.get("children", [])):
        findings.append("children: an id is declared twice")

    components = inventory.get("components", [])
    names = [component.get("name") for component in components]
    for name in sorted({name for name in names if names.count(name) > 1}, key=str):
        findings.append(f"{name}: listed more than once")

    delivered = {c.get("name") for c in components if c.get("status") == "delivered"}
    in_grammar = {component["name"] for component in grammar.get("components", [])}
    for name in sorted(in_grammar - delivered):
        findings.append(f"{name}: in {GRAMMAR} but not 'delivered' in the inventory")
    for name in sorted(delivered - in_grammar):
        findings.append(f"{name}: 'delivered' in the inventory but absent from {GRAMMAR}")

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
        if status == "delivered":
            if not isinstance(criteria, dict) or list(criteria) != criterion_ids:
                findings.append(f"{name}: must state exactly the criteria {criterion_ids}, in order")
                continue
            for criterion, entry in criteria.items():
                findings.extend(check_criterion(name, criterion, entry, children, root))
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
