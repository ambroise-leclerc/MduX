import tempfile
import unittest
from pathlib import Path

import mdux_docs_lint as lint


class JustificationTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.repo_root = Path(self.temp_dir.name)
        (self.repo_root / "evidence.txt").write_text("evidence", encoding="utf-8")

    def tearDown(self):
        self.temp_dir.cleanup()

    def check(self, obj_text):
        context = lint.LintContext(repo_root=self.repo_root)
        lint.check_justifications(
            context,
            self.repo_root / "document.md",
            f"```json\n{obj_text}\n```\n",
        )
        return {finding.code for finding in context.findings}

    def test_valid_justification(self):
        codes = self.check(
            """{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.2 Software development planning",
  "rationale": "The plan points to concrete evidence.",
  "evidence_refs": ["evidence.txt"]
}"""
        )
        self.assertEqual(set(), codes)

    def test_evidence_refs_is_required(self):
        codes = self.check(
            """{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.2 Software development planning",
  "rationale": "Missing evidence."
}"""
        )
        self.assertIn("MDX-D011", codes)

    def test_evidence_ref_cannot_escape_repository(self):
        codes = self.check(
            """{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.2 Software development planning",
  "rationale": "Invalid external evidence.",
  "evidence_refs": ["../outside.txt"]
}"""
        )
        self.assertIn("MDX-D019", codes)

    def test_clause_ref_standard_must_match(self):
        codes = self.check(
            """{
  "justification_id": "JUS-001",
  "standard": "IEC 62304:2006",
  "clause_ref": "ISO 13485:2016 §4.1 Quality management system",
  "rationale": "Mismatched citation.",
  "evidence_refs": ["evidence.txt"]
}"""
        )
        self.assertIn("MDX-D017", codes)


if __name__ == "__main__":
    unittest.main()
