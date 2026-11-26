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

DECISION = "".join(f"### {identifier} {title}\n\ntext\n\n" for identifier, title in (
    ("AC-01", "Contract"), ("AC-02", "Grammar"), ("AC-03", "Cost"), ("AC-04", "Draw"), ("AC-05", "Binding")
))
GRAMMAR = {"components": [{"name": "Label", "fields": []}]}
BASE = {
    "criteria": [
        {"id": "AC-01", "title": "Contract"},
        {"id": "AC-02", "title": "Grammar"},
        {"id": "AC-03", "title": "Cost"},
        {"id": "AC-04", "title": "Draw"},
        {"id": "AC-05", "title": "Binding"},
    ],
    "floor": ["AC-01", "AC-02", "AC-04", "AC-05"],
    "children": [{"id": "A5", "title": "States"}, {"id": "B1", "title": "Choice"}],
    "components": [
        {
            "name": "Label",
            "status": "provisional",
            "criteria": {
                "AC-01": {"status": "met", "evidence": ["evidence.txt"]},
                "AC-02": {"status": "met", "evidence": ["evidence.txt"]},
                "AC-03": {"status": "partial", "closedBy": "A5", "note": "no cost function"},
                "AC-04": {"status": "met", "evidence": ["evidence.txt"]},
                "AC-05": {"status": "not-applicable", "reason": "static text; no value is bound at run time"},
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

    def test_rejects_a_dictionary_component_the_grammar_lacks(self) -> None:
        """Refuse a component recorded as in the dictionary that the compiler does not know."""
        self.assert_one_finding(BASE, "Label: in the dictionary according to the inventory but absent", grammar={"components": []})

    def test_rejects_criteria_that_differ_from_the_decision(self) -> None:
        """Refuse a criterion list the decision record does not define."""
        self.assert_one_finding(BASE, "criteria differ", decision="### AC-01 Contract\n")

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
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-03"].update(closedBy="Z9"))
        self.assert_one_finding(document, "must name a declared child")
        document = self.mutated(lambda d: d["components"][0]["criteria"]["AC-03"].pop("note"))
        self.assert_one_finding(document, "must say what is missing")

    def test_rejects_a_dictionary_component_missing_a_criterion(self) -> None:
        """Refuse a component in the dictionary that leaves a criterion unstated."""
        document = self.mutated(lambda d: d["components"][0]["criteria"].pop("AC-02"))
        self.assert_one_finding(document, "must state exactly the criteria")

    def test_rejects_a_shortfall_on_a_floor_criterion(self) -> None:
        """Refuse a component in the dictionary that falls short of the floor, and a lowered floor."""
        document = self.mutated(
            lambda d: d["components"][0]["criteria"].update(
                {"AC-04": {"status": "partial", "closedBy": "A5", "note": "allocates"}}
            )
        )
        self.assert_one_finding(document, "a floor criterion cannot be 'partial'")
        document = self.mutated(lambda d: d.update(floor=["AC-01"]))
        self.assert_one_finding(document, "floor is ['AC-01']")

    def test_rejects_a_status_the_criteria_contradict(self) -> None:
        """Refuse 'admitted' with something owed, and 'provisional' with nothing owed."""
        document = self.mutated(lambda d: d["components"][0].update(status="admitted"))
        self.assert_one_finding(document, "'admitted' but ['AC-03'] are not met")
        document = self.mutated(
            lambda d: d["components"][0]["criteria"].update({"AC-03": {"status": "met", "evidence": ["evidence.txt"]}})
        )
        self.assert_one_finding(document, "'provisional' but records no shortfall")
        document["components"][0]["status"] = "admitted"
        self.assertEqual(self.findings_for(document), [])

    def test_rejects_not_applicable_on_a_universal_criterion(self) -> None:
        """Refuse waiving a criterion that holds for every entry, floor or not, with or without a reason."""
        for criterion in ("AC-01", "AC-02", "AC-03", "AC-04"):
            document = self.mutated(
                lambda d, c=criterion: d["components"][0]["criteria"].update(
                    {c: {"status": "not-applicable", "reason": "said not to apply"}}
                )
            )
            self.assert_one_finding(document, f"Label {criterion}: 'not-applicable' is not available")

    def test_rejects_admission_by_waiving_every_criterion(self) -> None:
        """Refuse the bypass itself: every criterion waived and the component declared admitted."""
        document = self.mutated(
            lambda d: d["components"][0].update(
                status="admitted",
                criteria={c["id"]: {"status": "not-applicable", "reason": "waived"} for c in d["criteria"]},
            )
        )
        findings = self.findings_for(document)
        for criterion in ("AC-01", "AC-02", "AC-03", "AC-04"):
            self.assertTrue(any(f"Label {criterion}: 'not-applicable' is not available" in f for f in findings), findings)
        self.assertFalse(any(f.startswith("Label AC-05") for f in findings), findings)

    def test_rejects_not_applicable_without_a_reason(self) -> None:
        """Refuse a conditional criterion waived with no reason, a blank one, or a claim attached."""
        for entry in ({"status": "not-applicable"}, {"status": "not-applicable", "reason": "  "},
                      {"status": "not-applicable", "reason": 3}):
            document = self.mutated(lambda d, e=entry: d["components"][0]["criteria"].update({"AC-05": e}))
            self.assert_one_finding(document, "Label AC-05: 'not-applicable' must say why in reason")
        document = self.mutated(
            lambda d: d["components"][0]["criteria"]["AC-05"].update(evidence=["evidence.txt"])
        )
        self.assert_one_finding(document, "it carries only a reason")

    def test_the_conditional_set_never_reaches_the_universal_floor(self) -> None:
        """Hold the constant itself: of the floor, only the binding criterion may not apply."""
        self.assertEqual(inventory.CONDITIONAL & set(inventory.FLOOR), {"AC-05"})

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
