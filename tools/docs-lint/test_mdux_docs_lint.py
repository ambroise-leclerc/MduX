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


class InternalLinkTests(unittest.TestCase):
    """Issue #115: Wave 2 deleted documents while live references survived until an integration
    repair. Each case below is one of the shapes that failure took."""

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.repo_root = Path(self.temp_dir.name)
        (self.repo_root / "docs").mkdir()
        (self.repo_root / "docs" / "target.md").write_text(
            "# Real heading\n\nBody.\n\n## Second Heading\n", encoding="utf-8"
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def check(self, text, name="doc.md"):
        context = lint.LintContext(repo_root=self.repo_root)
        path = self.repo_root / "docs" / name
        path.write_text(text, encoding="utf-8")
        lint.check_internal_links(context, path, text)
        return {finding.code for finding in context.findings}

    def test_resolving_link_is_accepted(self):
        self.assertEqual(self.check("See [target](target.md).\n"), set())

    def test_missing_file_is_reported(self):
        self.assertIn("MDX-D010", self.check("See [gone](retired-document.md).\n"))

    def test_dead_anchor_in_other_file_is_reported(self):
        self.assertIn("MDX-D011", self.check("See [x](target.md#no-such-heading).\n"))

    def test_live_anchor_in_other_file_is_accepted(self):
        self.assertEqual(self.check("See [x](target.md#second-heading).\n"), set())

    def test_dead_local_anchor_is_reported(self):
        self.assertIn("MDX-D010", self.check("# Title\n\nSee [x](#nope).\n"))

    def test_live_local_anchor_is_accepted(self):
        self.assertEqual(self.check("# Title\n\nSee [x](#title).\n"), set())

    def test_path_escaping_the_repository_is_reported(self):
        self.assertIn("MDX-D012", self.check("See [x](../../../etc/passwd).\n"))

    def test_external_links_are_not_fetched(self):
        # Network availability must not decide whether CI passes.
        self.assertEqual(
            self.check("See [x](https://example.invalid/nothing-here).\n"), set()
        )

    def test_links_inside_fenced_code_are_ignored(self):
        text = "```\n[example](does-not-exist.md)\n```\n"
        self.assertEqual(self.check(text), set())


class RetiredPathTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.repo_root = Path(self.temp_dir.name)
        record = self.repo_root / "docs" / "governance"
        record.mkdir(parents=True)
        (record / "superseded-documents.md").write_text(
            "| Path | Lines | Retired by | Replaced by | Why |\n"
            "|---|---|---|---|---|\n"
            "| `docs/Old-Framework.md` | 900 | issue #28 | [`docs/new/`](../new/) | Overclaimed. |\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def check(self, text, relative="docs/live.md"):
        context = lint.LintContext(repo_root=self.repo_root)
        path = self.repo_root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        retired = lint.load_retired_paths(self.repo_root)
        lint.check_retired_paths(context, path, text, retired)
        return {finding.code for finding in context.findings}

    def test_retired_path_is_read_from_the_record(self):
        self.assertEqual(lint.load_retired_paths(self.repo_root), {"docs/Old-Framework.md"})

    def test_live_reference_to_a_retired_path_is_reported(self):
        self.assertIn("MDX-D013", self.check("See `docs/Old-Framework.md` for details.\n"))

    def test_reference_need_not_be_a_link(self):
        # Naming the path at all is the failure; a reader follows it either way.
        self.assertIn("MDX-D013", self.check("Formerly docs/Old-Framework.md.\n"))

    def test_the_disposition_record_itself_is_permitted(self):
        text = "| `docs/Old-Framework.md` | 900 | issue #28 | x | y |\n"
        self.assertEqual(
            self.check(text, relative="docs/governance/superseded-documents.md"), set()
        )

    def test_unrelated_document_is_accepted(self):
        self.assertEqual(self.check("See `docs/new/index.md`.\n"), set())
