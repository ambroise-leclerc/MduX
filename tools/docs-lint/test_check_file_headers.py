"""Tests for check_file_headers.

The tests that matter are the ones proving the check *fails*. A header lint that only ever passes is
indistinguishable from no lint, and it is the failure paths a reviewer trusts when they read
"file headers OK" in a CI log - so every departure CONTRIBUTING.md names has a test that it is
caught, and the shape the tree actually uses has one that it is not.
"""

import tempfile
import unittest
from pathlib import Path

import check_file_headers as headers

CONFORMING = """\
/**
 * @file Widget.cppm
 * @brief One sentence on what this file is.
 *
 * @compliance ADR-004 Trust zones in C++
 *
 * Prose that mentions @author and @brief in passing, well after the tags.
 */
module;
export module mdux.widget;
"""


def check(name: str, text: str) -> list[str]:
    return [finding.message for finding in headers.check_text(Path(name), text)]


class CheckFileHeadersTests(unittest.TestCase):
    def test_accepts_the_documented_shape(self) -> None:
        self.assertEqual(check("Widget.cppm", CONFORMING), [])

    def test_rejects_brief_before_file(self) -> None:
        swapped = CONFORMING.replace(
            " * @file Widget.cppm\n * @brief One sentence on what this file is.\n",
            " * @brief One sentence on what this file is.\n * @file Widget.cppm\n",
        )
        findings = check("Widget.cppm", swapped)
        self.assertEqual(len(findings), 1)
        self.assertIn("@brief before @file", findings[0])

    def test_rejects_a_block_opening_with_another_tag(self) -> None:
        # A block whose first tag is not @file but which still has both, in the right order relative
        # to each other. CONTRIBUTING says to *open* with @file, so this is its own finding rather
        # than a variant of the ordering one - and without this case that branch was never executed.
        moved = CONFORMING.replace(
            " * @file Widget.cppm\n",
            " * @compliance ADR-004 Trust zones in C++\n * @file Widget.cppm\n",
        )
        findings = check("Widget.cppm", moved)
        self.assertEqual(len(findings), 1)
        self.assertIn("opens with @compliance", findings[0])

    def test_rejects_a_missing_file_tag(self) -> None:
        # The case with teeth: without @file the block documents whatever declaration follows it,
        # silently and with no Doxygen warning.
        findings = check("Widget.cppm", CONFORMING.replace(" * @file Widget.cppm\n", ""))
        self.assertEqual(len(findings), 1)
        self.assertIn("without @file", findings[0])

    def test_rejects_a_file_tag_naming_another_file(self) -> None:
        findings = check("Widget.cppm", CONFORMING.replace("@file Widget.cppm", "@file Widget.cpp"))
        self.assertEqual(len(findings), 1)
        self.assertIn("must name 'Widget.cppm'", findings[0])

    def test_rejects_a_file_tag_carrying_a_path(self) -> None:
        # `Draw.cppm` and `Draw.cpp` are different files with one stem, and this tree has several
        # such pairs, so the extension is load-bearing - and so is the absence of a path.
        findings = check("Widget.cppm", CONFORMING.replace("@file Widget.cppm", "@file include/mdux/Widget.cppm"))
        self.assertEqual(len(findings), 1)
        self.assertIn("no path", findings[0])

    def test_rejects_an_author_tag(self) -> None:
        findings = check("Widget.cppm", CONFORMING.replace(" * @brief", " * @author A. Person\n * @brief"))
        self.assertEqual(len(findings), 1)
        self.assertIn("@author", findings[0])

    def test_rejects_a_file_with_no_block_at_all(self) -> None:
        findings = check("Widget.cppm", "module;\nexport module mdux.widget;\n")
        self.assertEqual(len(findings), 1)
        self.assertIn("no file-level Doxygen block", findings[0])

    def test_rejects_a_block_with_no_brief(self) -> None:
        findings = check("Widget.cppm", "/**\n * @file Widget.cppm\n */\nmodule;\n")
        self.assertEqual(len(findings), 1)
        self.assertIn("no @brief", findings[0])

    def test_reads_only_the_first_block(self) -> None:
        # A later block documenting a function must not be mistaken for the file's own.
        text = CONFORMING + "\n/**\n * @brief A function, documented after the file block.\n */\nvoid f();\n"
        self.assertEqual(check("Widget.cppm", text), [])

    def test_collects_sources_and_skips_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "src").mkdir()
            (root / "src" / "Widget.cpp").write_text(CONFORMING, encoding="utf-8")
            (root / "src" / "notes.md").write_text("not a source", encoding="utf-8")
            (root / "tests" / "_deps" / "vendor").mkdir(parents=True)
            (root / "tests" / "_deps" / "vendor" / "Vendor.cpp").write_text("no block here", encoding="utf-8")
            (root / "tests" / "fixtures").mkdir(parents=True)
            (root / "tests" / "fixtures" / "Fixture.cpp").write_text("no block here either", encoding="utf-8")

            # `.h` is deliberately absent from the suffix list: CONTRIBUTING fixes this project's
            # extensions as .hpp, .cpp and .cppm, so a `.h` in the tree is a naming violation rather
            # than a file this lint should be asking for Doxygen blocks in - and treating it as a
            # source would quietly bless it.
            (root / "src" / "legacy.h").write_text("no block here", encoding="utf-8")

            collected = [path.name for path in headers.collect_sources(root, [])]
            self.assertEqual(collected, ["Widget.cpp"])

    def test_the_tree_itself_conforms(self) -> None:
        # The sweep in #223 is what makes this pass; it is here so that a file added later cannot
        # reintroduce the deviation without a test saying so.
        root = Path(__file__).resolve().parents[2]
        offenders = [
            f"{path.relative_to(root)}: {message}"
            for path in headers.collect_sources(root, [])
            for message in check(path.name, path.read_text(encoding="utf-8", errors="replace"))
        ]
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
