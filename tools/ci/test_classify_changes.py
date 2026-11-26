#!/usr/bin/env python3
"""Unit tests for classify_changes.py. Run: python3 -m unittest discover -s tools/ci -p "test_*.py"."""
from __future__ import annotations

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import classify_changes  # noqa: E402


class DocsOnlyClassificationTests(unittest.TestCase):
    def test_prose_under_docs_and_at_root_is_docs_only(self) -> None:
        for path in ("docs/adr/ADR-009-in-repository-test-framework.md", "README.md",
                     "CHANGELOG.md", "software_development_file/README.md",
                     "examples/README-VulkanSCTriangle.md", "./docs/roadmap.md"):
            with self.subTest(path=path):
                self.assertTrue(classify_changes.is_docs_only(path))

    def test_documents_a_build_time_check_reads_are_not_docs_only(self) -> None:
        for path in ("AGENTS.md", "docs/getting-started.md",
                     ".agents/skills/mdux-build-and-test/SKILL.md"):
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_docs_only(path))

    def test_non_markdown_under_docs_is_not_docs_only(self) -> None:
        for path in ("docs/medui/grammar.json", "docs/governance/soup-register.toml",
                     "docs/recipes/screen.schema.json", "docs/iec62304/schemas/x.schema.json"):
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_docs_only(path))

    def test_markdown_outside_the_prose_roots_is_not_docs_only(self) -> None:
        for path in ("tests/README.md", "tools/preview/studio/NOTES.md",
                     ".github/pull_request_template.md"):
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_docs_only(path))

    def test_source_and_build_files_are_not_docs_only(self) -> None:
        for path in ("CMakeLists.txt", "src/draw/Draw.cpp", ".github/workflows/lint.yml"):
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_docs_only(path))


class BuildDecisionTests(unittest.TestCase):
    def test_all_docs_only_skips_the_build(self) -> None:
        self.assertFalse(classify_changes.needs_code_build(["docs/roadmap.md", "README.md"]))

    def test_one_code_path_selects_the_build(self) -> None:
        self.assertTrue(classify_changes.needs_code_build(["docs/roadmap.md", "src/mdux.cpp"]))

    def test_an_empty_list_selects_the_build(self) -> None:
        # An empty list means the file list could not be read, not that nothing changed.
        self.assertTrue(classify_changes.needs_code_build([]))
        self.assertTrue(classify_changes.needs_code_build(["", "  "]))

    def test_main_prints_a_github_output_line(self) -> None:
        for stdin, expected in (("docs/roadmap.md\n", "code=false"), ("src/mdux.cpp\n", "code=true")):
            with self.subTest(stdin=stdin), mock.patch("sys.stdin", io.StringIO(stdin)):
                out = io.StringIO()
                with redirect_stdout(out):
                    self.assertEqual(classify_changes.main(), 0)
                self.assertEqual(out.getvalue().strip(), expected)


if __name__ == "__main__":
    unittest.main()
