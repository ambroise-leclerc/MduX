#!/usr/bin/env python3
"""Unit tests for classify_changes.py. Run: python3 -m unittest discover -s tools/ci -p "test_*.py"."""
from __future__ import annotations

import io
import json
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

    def test_a_source_file_renamed_into_docs_selects_the_build(self) -> None:
        # The new path alone is prose; the rename still removes src/example.cpp.
        entry = {"filename": "docs/example.md", "previous_filename": "src/example.cpp",
                 "status": "renamed"}
        paths = classify_changes.paths_from_github_files([json.dumps(entry)])
        self.assertEqual(paths, ["docs/example.md", "src/example.cpp"])
        self.assertTrue(classify_changes.needs_code_build(paths))

    def test_a_prose_rename_inside_docs_stays_docs_only(self) -> None:
        entry = {"filename": "docs/new-name.md", "previous_filename": "docs/old-name.md",
                 "status": "renamed"}
        paths = classify_changes.paths_from_github_files([json.dumps(entry)])
        self.assertFalse(classify_changes.needs_code_build(paths))

    def test_a_malformed_github_listing_selects_the_build(self) -> None:
        for stdin in ("not json\n", '{"status": "modified"}\n', "[1, 2]\n"):
            with self.subTest(stdin=stdin), mock.patch("sys.stdin", io.StringIO(stdin)):
                out = io.StringIO()
                with redirect_stdout(out):
                    classify_changes.main(["--github-files"])
                self.assertEqual(out.getvalue().strip(), "code=true")

    def test_main_reads_github_file_entries(self) -> None:
        stdin = json.dumps({"filename": "docs/example.md", "previous_filename": "src/example.cpp"})
        with mock.patch("sys.stdin", io.StringIO(stdin + "\n")):
            out = io.StringIO()
            with redirect_stdout(out):
                classify_changes.main(["--github-files"])
            self.assertEqual(out.getvalue().strip(), "code=true")

    def test_main_prints_a_github_output_line(self) -> None:
        for stdin, expected in (("docs/roadmap.md\n", "code=false"), ("src/mdux.cpp\n", "code=true")):
            with self.subTest(stdin=stdin), mock.patch("sys.stdin", io.StringIO(stdin)):
                out = io.StringIO()
                with redirect_stdout(out):
                    self.assertEqual(classify_changes.main([]), 0)
                self.assertEqual(out.getvalue().strip(), expected)


if __name__ == "__main__":
    unittest.main()
