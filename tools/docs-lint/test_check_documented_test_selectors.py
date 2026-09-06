"""Tests for check_documented_test_selectors.

The property under test is issue #304's own acceptance criterion, stated directly: an obsolete
`-R` selector must fail and a valid one must pass, against a *real* CTest inventory rather than a
second list of names this suite would have to keep in step with the checker. Every test that
asks "does this selector match" builds a hand-written `CTestTestfile.cmake` and asks the real
`ctest` binary about it - `CTestTestfile.cmake` is a plain script `ctest` reads directly, so this
needs no CMake configure, only the same tool the checker itself shells out to.
"""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import check_documented_test_selectors as selectors

CTEST_AVAILABLE = shutil.which("ctest") is not None


def write_ctest_testfile(build_dir: Path, test_names: list[str]) -> None:
    """A minimal real CTest configuration: one always-passing test per name given.

    `add_test("<name>" "true")` is the old-style positional form `cmake/MduXBake.cmake` and
    `MduXTestDiscoveryImpl.cmake` both use for the same reason documented there - it is what
    `mdux_discover_tests()` actually writes, so a hand-written fixture here matches production
    shape rather than an idealised one.

    The name is quoted, which is not decoration: every discovered case name contains a space
    (`"unit_tests::Version Test"`), and CTest reads an *unquoted* `add_test(unit_tests::Version
    Test "true")` as name `unit_tests::Version`, command `Test`, argument `"true"` - confirmed
    empirically, and exactly the gap between this fixture and production shape that would have
    let every test here pass against a name CTest was never asked to register.
    """
    build_dir.mkdir(parents=True, exist_ok=True)
    lines = [f'add_test("{name}" "true")\n' for name in test_names]
    (build_dir / "CTestTestfile.cmake").write_text("".join(lines), encoding="utf-8")


class DocumentDiscoveryTests(unittest.TestCase):
    def test_finds_agents_md_getting_started_and_every_skill(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "AGENTS.md").write_text("", encoding="utf-8")
            (root / "docs").mkdir()
            (root / "docs" / "getting-started.md").write_text("", encoding="utf-8")
            (root / ".agents" / "skills" / "zeta-skill").mkdir(parents=True)
            (root / ".agents" / "skills" / "zeta-skill" / "SKILL.md").write_text("", encoding="utf-8")
            (root / ".agents" / "skills" / "alpha-skill").mkdir(parents=True)
            (root / ".agents" / "skills" / "alpha-skill" / "SKILL.md").write_text("", encoding="utf-8")
            # A skill directory with no SKILL.md is not a document to scan - the glob is on the
            # filename, not the directory, so this cannot silently start reading the wrong file.
            (root / ".agents" / "skills" / "no-skill-file").mkdir(parents=True)

            found = selectors.documents_to_scan(root)

            self.assertEqual(
                found,
                [
                    root / "AGENTS.md",
                    root / "docs" / "getting-started.md",
                    root / ".agents" / "skills" / "alpha-skill" / "SKILL.md",
                    root / ".agents" / "skills" / "zeta-skill" / "SKILL.md",
                ],
            )

    def test_a_repository_missing_every_document_scans_nothing_rather_than_failing(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            self.assertEqual(selectors.documents_to_scan(Path(raw)), [])


class CitationExtractionTests(unittest.TestCase):
    def test_extracts_a_bare_selector_only_inside_a_fence(self) -> None:
        text = (
            "Mention `-R MduXUnitTests` in prose names nothing to check.\n"
            "```bash\n"
            "ctest --test-dir build -R MduXUnitTests --output-on-failure\n"
            "```\n"
        )
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual(len(citations), 1)
            self.assertEqual(citations[0].selector, "MduXUnitTests")
            self.assertEqual(citations[0].line, 3)

    def test_extracts_a_quoted_selector_and_the_long_flag_spelling(self) -> None:
        text = (
            "```console\n"
            "ctest --test-dir build -R '^unit_tests::' --output-on-failure\n"
            "ctest --test-dir build --tests-regex \"^medui_tools_spec::\"\n"
            "```\n"
        )
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["^unit_tests::", "^medui_tools_spec::"])

    def test_a_label_selector_is_not_mistaken_for_a_name_selector(self) -> None:
        # `-L`/`--label-regex` is a different, already-checked mechanism
        # (check_named_mechanisms.py, against the static CMake/TEST_CASE index rather than a
        # built tree) - this checker's whole reason to exist is that a `-R` name cannot be
        # validated that way, so it must not silently absorb `-L` citations too.
        text = "```bash\nctest --test-dir build -L evidence --output-on-failure\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            self.assertEqual(selectors.find_citations(path), [])

    def test_an_unterminated_fence_still_has_its_content_checked(self) -> None:
        text = "```bash\nctest --test-dir build -R Stale\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["Stale"])

    def test_a_selector_on_a_shell_continuation_line_is_still_found(self) -> None:
        # `ctest` and `-R` on different physical lines used to produce no citation at all - a
        # command that reads as documented verification and is silently never checked, which is
        # the exact failure shape this whole tool exists to close.
        text = "```bash\nctest --test-dir build \\\n    -R MduXUnitTests --output-on-failure\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["MduXUnitTests"])
            # The line a reader would look for the command on - the first of the two, where
            # `ctest` itself appears - not the continuation line the flag happened to land on.
            self.assertEqual(citations[0].line, 2)

    def test_a_long_option_list_before_the_flag_does_not_hide_it(self) -> None:
        # A regex once bounded the gap between `ctest` and `-R` to 60 characters - shorter than a
        # realistic option list, so this exact line matched nothing at all and the selector inside
        # it was never checked.
        text = (
            "```bash\n"
            "ctest --test-dir build --output-on-failure --no-tests=error --parallel 4 -R ObsoleteSuite\n"
            "```\n"
        )
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["ObsoleteSuite"])

    def test_an_unquoted_selector_keeps_its_punctuation(self) -> None:
        # A regex once excluded `,` (and `)`, `]`) from an unquoted selector's characters, so
        # `^unit_tests::Version,obsolete$` was read as `^unit_tests::Version` - a *different*,
        # actually-live selector silently substituted for the one the document names, which is
        # worse than missing it: the check would have reported this citation live.
        text = "```bash\nctest --test-dir build -R ^unit_tests::Version,obsolete$\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["^unit_tests::Version,obsolete$"])

    def test_a_trailing_comment_does_not_become_part_of_the_selector(self) -> None:
        text = "```bash\nctest --test-dir build -R MduXUnitTests --output-on-failure  # one suite\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["MduXUnitTests"])

    def test_a_line_with_unbalanced_quotes_is_skipped_rather_than_guessed_at(self) -> None:
        text = "```bash\nctest --test-dir build -R 'unterminated\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            self.assertEqual(selectors.find_citations(path), [])

    def test_a_continuation_is_not_read_across_a_fence_boundary(self) -> None:
        # A line ending a fenced block in a trailing backslash - unusual, but not this tool's to
        # assume can't happen - must not absorb the next block's first line as if it were the
        # same command.
        text = "```bash\necho done \\\n```\n```bash\nctest --test-dir build -R Live\n```\n"
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "doc.md"
            path.write_text(text, encoding="utf-8")
            citations = selectors.find_citations(path)
            self.assertEqual([c.selector for c in citations], ["Live"])


@unittest.skipUnless(CTEST_AVAILABLE, "ctest is not on PATH in this environment")
class LiveCTestQueryTests(unittest.TestCase):
    """`matching_test_count()` against a real, hand-written CTest configuration."""

    def test_a_selector_matching_registered_tests_returns_their_count(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            build_dir = Path(raw)
            write_ctest_testfile(build_dir, ["sample::CaseOne", "sample::CaseTwo", "other::CaseOne"])
            self.assertEqual(selectors.matching_test_count("ctest", build_dir, "^sample::"), 2)

    def test_a_name_containing_a_space_is_registered_whole_rather_than_split(self) -> None:
        # Every discovered case name contains a space (`unit_tests::Version Test`), and an
        # unquoted `add_test(unit_tests::Version Test "true")` is read by CTest as name
        # `unit_tests::Version`, command `Test` - a fixture bug that would have let every test
        # here pass against a name that was never actually registered. Pinned directly, rather
        # than relying on the other scenarios happening to use a prefix selector that cannot tell
        # the difference.
        with tempfile.TemporaryDirectory() as raw:
            build_dir = Path(raw)
            write_ctest_testfile(build_dir, ["unit_tests::Version Test"])
            self.assertEqual(selectors.matching_test_count("ctest", build_dir, "^unit_tests::Version$"), 0)
            self.assertEqual(selectors.matching_test_count("ctest", build_dir, "^unit_tests::Version Test$"), 1)

    def test_a_selector_matching_nothing_returns_zero_rather_than_raising(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            build_dir = Path(raw)
            write_ctest_testfile(build_dir, ["sample::CaseOne"])
            self.assertEqual(selectors.matching_test_count("ctest", build_dir, "DoesNotExist"), 0)

    def test_an_unresolvable_ctest_command_raises_rather_than_reporting_zero(self) -> None:
        # Distinct from "matched zero tests": "could not be asked" is a different fact, and
        # collapsing the two would report an infrastructure problem as a documentation defect.
        with tempfile.TemporaryDirectory() as raw:
            build_dir = Path(raw)
            write_ctest_testfile(build_dir, ["sample::CaseOne"])
            with self.assertRaises(RuntimeError):
                selectors.matching_test_count("mdux-ctest-does-not-exist", build_dir, "sample")


@unittest.skipUnless(CTEST_AVAILABLE, "ctest is not on PATH in this environment")
class CheckRepositoryTests(unittest.TestCase):
    """The end-to-end property: an obsolete selector fails, a valid one passes."""

    def make_repository(self, root: Path, skill_text: str) -> Path:
        skill_dir = root / ".agents" / "skills" / "sample-skill"
        skill_dir.mkdir(parents=True)
        skill_path = skill_dir / "SKILL.md"
        skill_path.write_text(skill_text, encoding="utf-8")
        return skill_path

    def test_an_obsolete_selector_is_a_finding_and_a_live_one_is_not(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            build_dir = root / "build"
            write_ctest_testfile(build_dir, ["unit_tests::Version Test"])

            self.make_repository(
                root,
                "```bash\n"
                "ctest --test-dir build -R MduXUnitTests --output-on-failure\n"
                "```\n",
            )
            findings, documents, citations = selectors.check_repository(root, build_dir, "ctest")
            self.assertEqual(documents, 1)
            self.assertEqual(citations, 1)
            self.assertEqual(len(findings), 1)
            self.assertIn("MduXUnitTests", findings[0].reason)
            self.assertIn("0 registered tests", findings[0].reason)

    def test_a_selector_that_matches_is_not_a_finding(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            build_dir = root / "build"
            write_ctest_testfile(build_dir, ["unit_tests::Version Test"])

            self.make_repository(
                root,
                "```bash\n"
                "ctest --test-dir build -R '^unit_tests::' --output-on-failure\n"
                "```\n",
            )
            findings, documents, citations = selectors.check_repository(root, build_dir, "ctest")
            self.assertEqual(citations, 1)
            self.assertEqual(findings, [])


@unittest.skipUnless(CTEST_AVAILABLE, "ctest is not on PATH in this environment")
class MainEntryPointTests(unittest.TestCase):
    """Literal exit-status proof of #304's acceptance criterion, via the script as a subprocess -
    the same way CI invokes it."""

    SCRIPT = Path(__file__).resolve().with_name("check_documented_test_selectors.py")

    def run_script(self, root: Path, build_dir: Path) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(self.SCRIPT), "--repo-root", str(root), "--build-dir", str(build_dir)],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_exits_nonzero_for_an_obsolete_selector(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            build_dir = root / "build"
            write_ctest_testfile(build_dir, ["unit_tests::Version Test"])
            (root / ".agents" / "skills" / "s").mkdir(parents=True)
            (root / ".agents" / "skills" / "s" / "SKILL.md").write_text(
                "```bash\nctest -R MduXUnitTests\n```\n", encoding="utf-8"
            )
            result = self.run_script(root, build_dir)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MduXUnitTests", result.stderr)

    def test_exits_zero_for_a_live_selector(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            build_dir = root / "build"
            write_ctest_testfile(build_dir, ["unit_tests::Version Test"])
            (root / ".agents" / "skills" / "s").mkdir(parents=True)
            (root / ".agents" / "skills" / "s" / "SKILL.md").write_text(
                "```bash\nctest -R '^unit_tests::'\n```\n", encoding="utf-8"
            )
            result = self.run_script(root, build_dir)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("OK", result.stdout)

    def test_a_build_directory_with_no_ctest_configuration_is_reported_distinctly(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            result = self.run_script(root, root / "not-a-build-dir")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CTestTestfile.cmake", result.stderr)


if __name__ == "__main__":
    unittest.main()
