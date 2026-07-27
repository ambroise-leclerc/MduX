import tempfile
import unittest
from pathlib import Path

import generate_ai_reference as gen


class SectionsOfTests(unittest.TestCase):
    def test_splits_on_h2(self):
        text = "# Title\n\nintro\n\n## §1 First\n\nbody one\n\n## §2 Second\n\nbody two\n"
        sections = gen.sections_of(text)
        self.assertEqual([h for h, _ in sections], ["§1 First", "§2 Second"])
        self.assertIn("body one", sections[0][1])
        self.assertIn("body two", sections[1][1])
        self.assertNotIn("body two", sections[0][1])

    def test_falls_back_to_h1_when_no_h2(self):
        text = "# IEC 62366-1:2015 §4 — General requirements\n\nsome prose\n"
        sections = gen.sections_of(text)
        self.assertEqual(len(sections), 1)
        self.assertEqual(sections[0][0], "IEC 62366-1:2015 §4 — General requirements")


class JustificationIdsTests(unittest.TestCase):
    def test_extracts_id_from_fenced_json(self):
        body = (
            "prose\n\n```json\n"
            '{"justification_id": "JUS-099", "standard": "IEC 62304:2006"}'
            "\n```\nmore prose\n"
        )
        self.assertEqual(["JUS-099"], gen.justification_ids(body))

    def test_ignores_non_json_fences_and_malformed_json(self):
        body = "```text\nnot json\n```\n```json\n{not valid json\n```\n"
        self.assertEqual([], gen.justification_ids(body))

    def test_returns_multiple_ids_in_order(self):
        body = (
            '```json\n{"justification_id": "JUS-001"}\n```\n'
            '```json\n{"justification_id": "JUS-002"}\n```\n'
        )
        self.assertEqual(["JUS-001", "JUS-002"], gen.justification_ids(body))


class BuildRowsTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write(self, name: str, content: str) -> None:
        (self.directory / name).write_text(content, encoding="utf-8")

    def test_skips_readme_and_existing_ai_reference(self):
        self.write("README.md", "# README\n\n## §1 Ignored\n")
        self.write("AI-Reference.md", "# stale index\n\n## §9 Ignored\n")
        self.write(
            "01-real.md",
            "# Real file\n\n## §1 Real clause\n\nprose\n",
        )
        rows = gen.build_rows(self.directory)
        self.assertEqual([r[1] for r in rows], ["Real clause"])

    def test_attributes_justification_to_the_heading_it_appears_under(self):
        self.write(
            "01-file.md",
            "# Title\n\n"
            "## §1 First\n\nno justification here\n\n"
            "## §2 Second\n\n"
            '```json\n{"justification_id": "JUS-005", "standard": "IEC 62304:2006"}\n```\n',
        )
        rows = gen.build_rows(self.directory)
        by_title = {title: jids for _, title, _, jids in rows}
        self.assertEqual(by_title["First"], "—")
        self.assertEqual(by_title["Second"], "JUS-005")

    def test_clause_extracted_from_heading_prefix(self):
        self.write("01-file.md", "# Title\n\n## §5.3 Some clause title\n\nprose\n")
        rows = gen.build_rows(self.directory)
        clause, title, fname, _ = rows[0]
        self.assertEqual(clause, "§5.3")
        self.assertEqual(title, "Some clause title")
        self.assertEqual(fname, "01-file.md")

    def test_heading_without_clause_number_gets_em_dash(self):
        self.write("01-file.md", "# Title\n\n## Not clause-numbered\n\nprose\n")
        rows = gen.build_rows(self.directory)
        clause, title, _, _ = rows[0]
        self.assertEqual(clause, "—")
        self.assertEqual(title, "Not clause-numbered")

    def test_multiple_files_processed_in_sorted_order(self):
        self.write("02-b.md", "# B\n\n## §2 In B\n")
        self.write("01-a.md", "# A\n\n## §1 In A\n")
        rows = gen.build_rows(self.directory)
        self.assertEqual([r[2] for r in rows], ["01-a.md", "02-b.md"])


class RenderTests(unittest.TestCase):
    def test_render_includes_regenerate_command_and_table(self):
        rows = [("§1", "Scope", "01-scope.md", "—")]
        output = gen.render("IEC 62304:2006", Path("docs/iec62304"), rows)
        self.assertIn("# IEC 62304:2006 — per-clause index", output)
        self.assertIn("python3 tools/docs-lint/generate_ai_reference.py docs/iec62304", output)
        self.assertIn("| §1 | Scope | [`01-scope.md`](01-scope.md) | — |", output)

    def test_render_escapes_pipe_in_title(self):
        rows = [("§7.5", "Risk/benefit | analysis", "04-file.md", "—")]
        output = gen.render("ISO 14971:2019", Path("docs/iso14971"), rows)
        self.assertIn("Risk/benefit \\| analysis", output)


if __name__ == "__main__":
    unittest.main()
