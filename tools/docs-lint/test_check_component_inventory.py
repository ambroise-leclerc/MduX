"""Tests for check_component_inventory.

Each rejection case is the control for one rule: a checker that accepts the committed file proves
nothing unless the same checker is shown to refuse the file each rule exists to refuse.
"""

import copy
import json
import tempfile
import unittest
from pathlib import Path

import check_component_inventory as inventory

DECISION = "# ADR\n\n### AC-01 Contract status\n\ntext\n\n### AC-02 Tests\n\ntext\n"
GRAMMAR = {"components": [{"name": "Label", "fields": []}]}
BASE = {
    "criteria": [{"id": "AC-01", "title": "Contract status"}, {"id": "AC-02", "title": "Tests"}],
    "children": [{"id": "A5", "title": "States"}, {"id": "B1", "title": "Choice"}],
    "components": [
        {
            "name": "Label",
            "status": "delivered",
            "criteria": {
                "AC-01": {"status": "met", "evidence": ["evidence.txt"]},
                "AC-02": {"status": "partial", "closedBy": "A5", "note": "no state test"},
            },
        },
        {"name": "Checkbox", "status": "proposed", "workstream": "B1"},
    ],
}


class CheckComponentInventoryTests(unittest.TestCase):
    def findings_for(self, document: dict, grammar: dict = GRAMMAR, decision: str = DECISION) -> list[str]:
        """Write the smallest repository carrying `document` and return the checker's findings."""
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "docs" / "medui").mkdir(parents=True)
            (root / "docs" / "adr").mkdir()
            (root / "evidence.txt").write_text("", encoding="utf-8")
            (root / inventory.INVENTORY).write_text(json.dumps(document), encoding="utf-8")
            (root / inventory.GRAMMAR).write_text(json.dumps(grammar), encoding="utf-8")
            (root / inventory.DECISION).write_text(decision, encoding="utf-8")
            return inventory.check(root)

    def mutated(self, change) -> dict:
        """Return a deep copy of the accepted document with `change` applied."""
        document = copy.deepcopy(BASE)
        change(document)
        return document

    def assert_one_finding(self, document: dict, fragment: str, **inputs) -> None:
        """Assert the document is refused for the reason `fragment` names."""
        findings = self.findings_for(document, **inputs)
        self.assertTrue(any(fragment in finding for finding in findings), findings)

    def test_accepts_a_consistent_inventory(self) -> None:
        """Accept the base document, so every rejection below is caused by its one change."""
        self.assertEqual(self.findings_for(BASE), [])

    def test_rejects_a_grammar_component_the_inventory_omits(self) -> None:
        """Refuse an inventory that is silent about a component the compiler accepts."""
        grammar = {"components": [{"name": "Label"}, {"name": "Image"}]}
        self.assert_one_finding(BASE, "Image: in docs/medui/grammar.json", grammar=grammar)

    def test_rejects_a_delivered_component_the_grammar_lacks(self) -> None:
        """Refuse a component recorded as delivered that the compiler does not know."""
        self.assert_one_finding(BASE, "Label: 'delivered' in the inventory but absent", grammar={"components": []})

    def test_rejects_criteria_that_differ_from_the_decision(self) -> None:
        """Refuse a criterion list the decision record does not define."""
        self.assert_one_finding(BASE, "criteria differ", decision="### AC-01 Contract status\n")

    def test_rejects_met_without_existing_evidence(self) -> None:
        """Refuse a met criterion resting on a path that is not there."""
        document = self.mutated(
            lambda d: d["components"][0]["criteria"]["AC-01"].update(evidence=["missing.txt"])
        )
        self.assert_one_finding(document, "evidence path 'missing.txt' does not exist")
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-01"].pop("evidence"))
        self.assert_one_finding(document, "'met' names no evidence")

    def test_rejects_a_gap_nobody_owns(self) -> None:
        """Refuse a shortfall with no declared child to close it, or no statement of what it is."""
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-02"].update(closedBy="Z9"))
        self.assert_one_finding(document, "must name a declared child")
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-02"].pop("note"))
        self.assert_one_finding(document, "must say what is missing")

    def test_rejects_a_delivered_component_missing_a_criterion(self) -> None:
        """Refuse a delivered component that leaves a criterion unstated."""
        document = self.mutated(lambda d: d["components"][0]["criteria"].pop("AC-02"))
        self.assert_one_finding(document, "must state exactly the criteria")

    def test_rejects_an_unknown_status(self) -> None:
        """Refuse statuses outside both closed sets."""
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-01"].update(status="done"))
        self.assert_one_finding(document, "status 'done'")
        document = self.mutated(lambda d: d["components"][1].update(status="planned"))
        self.assert_one_finding(document, "status 'planned'")

    def test_rejects_a_proposed_component_that_asserts_criteria_or_no_workstream(self) -> None:
        """Refuse criteria claimed for an undesigned component, and an undeclared workstream."""
        document = self.mutated(lambda d: d["components"][1].update(criteria={}))
        self.assert_one_finding(document, "asserts criteria")
        document = self.mutated(lambda d: d["components"][1].update(workstream="Q1"))
        self.assert_one_finding(document, "is not a declared child")

    def test_rejects_duplicates_and_reasonless_declines(self) -> None:
        """Refuse a name listed twice and a declined component with no recorded reason."""
        document = self.mutated(lambda d: d["components"].append(copy.deepcopy(d["components"][1])))
        self.assert_one_finding(document, "Checkbox: listed more than once")
        document = self.mutated(lambda d: d["components"][1].update(status="declined"))
        self.assert_one_finding(document, "'declined' records no reason")


if __name__ == "__main__":
    unittest.main()
