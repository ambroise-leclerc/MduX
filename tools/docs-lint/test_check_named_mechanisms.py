"""Tests for check_named_mechanisms.

The failure cases are the control: each mechanism class has a test proving that an unresolved name
is rejected, while the proposal and opt-out cases pin the boundaries that keep the checker usable.
"""

import tempfile
import unittest
from pathlib import Path

import check_named_mechanisms as mechanisms


class CheckNamedMechanismsTests(unittest.TestCase):
    def make_repository(self, raw: str) -> Path:
        root = Path(raw)
        (root / "docs").mkdir()
        (root / ".github" / "workflows").mkdir(parents=True)
        (root / "tools" / "governed-lint").mkdir(parents=True)
        (root / "tests").mkdir()
        (root / "CMakeLists.txt").write_text(
            'add_custom_target(mdux-bake-update)\n'
            'set_tests_properties(a PROPERTIES LABELS "evidence")\n',
            encoding="utf-8",
        )
        (root / "tools" / "CMakeLists.txt").write_text(
            "add_executable(mdux-shaderbake main.cpp)\n", encoding="utf-8"
        )
        (root / "tools" / "governed-lint" / "mdux_governed_lint.py").write_text("", encoding="utf-8")
        (root / "tests" / "Cases.cpp").write_text(
            'TEST_CASE("A deterministic case", "determinism") {}\n'
            'const mdux::spec::Register noHeap{"No allocation", "noheap", [] {}};\n',
            encoding="utf-8",
        )
        (root / ".github" / "workflows" / "build.yml").write_text(
            "name: Build\non:\n  pull_request:\njobs: {}\n", encoding="utf-8"
        )
        (root / ".github" / "workflows" / "manual.yml").write_text(
            "name: Manual\non:\n  workflow_dispatch:\njobs: {}\n", encoding="utf-8"
        )
        return root

    def findings_for(self, root: Path, text: str) -> list[str]:
        path = root / "docs" / "claim.md"
        path.write_text(text, encoding="utf-8")
        index = mechanisms.build_index(root)
        return [
            finding.message
            for finding in mechanisms.check_lines(path, mechanisms.markdown_prose_lines(text), index)
        ]

    def test_accepts_resolved_workflow_labels_targets_and_python_tools(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            text = (
                "`build.yml` runs `mdux-shaderbake` and `mdux-governed-lint`.\n"
                "Use `mdux-bake-update`, then ctest -L evidence and ctest --label-regex=determinism.\n"
                "ctest -L noheap is registered through SpecLab.\n"
            )
            self.assertEqual(self.findings_for(root, text), [])

    def test_rejects_a_missing_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(
                self.findings_for(root, "`.github/workflows/absent.yml` runs in CI.\n"),
                ["workflow '.github/workflows/absent.yml' does not exist"],
            )

    def test_rejects_a_dispatch_only_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(
                self.findings_for(root, "`manual.yml` runs on every change.\n"),
                ["workflow 'manual.yml' has no push or pull_request trigger"],
            )

    def test_rejects_an_unregistered_ctest_label(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(
                self.findings_for(root, "Run ctest --label-regex missing-label.\n"),
                ["ctest label 'missing-label' is not attached to any test"],
            )

    def test_rejects_an_unknown_standalone_mdux_name(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(
                self.findings_for(root, "The `mdux-imaginary-lint` check gates every PR.\n"),
                ["CMake/tool target 'mdux-imaginary-lint' does not exist"],
            )

    def test_a_commented_cmake_target_is_not_implementation_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            with (root / "CMakeLists.txt").open("a", encoding="utf-8") as cmake:
                cmake.write("# add_custom_target(mdux-proposed-target)\n")
            self.assertEqual(
                self.findings_for(root, "The `mdux-proposed-target` runs on every change.\n"),
                ["CMake/tool target 'mdux-proposed-target' does not exist"],
            )

    def test_commented_tests_do_not_register_labels(self) -> None:
        source = (
            '// TEST_CASE("Disabled", "ghost-line") {}\n'
            '/* const mdux::spec::Register disabled{"Disabled", "ghost-block", [] {}}; */\n'
            'TEST_CASE("Live", "live") {}\n'
        )
        self.assertEqual(mechanisms.labels_from_test_source(source), {"live"})

    def test_does_not_treat_a_package_path_as_a_target(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(self.findings_for(root, "See `generated/shader/mdux-ui/`.\n"), [])

    def test_skips_backtick_and_tilde_fenced_proposals(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            text = (
                "```yaml\n# .github/workflows/future.yml\n```\n"
                "~~~sh\nctest -L future\n`mdux-future-tool`\n~~~\n"
            )
            self.assertEqual(self.findings_for(root, text), [])

    def test_accepts_a_visible_same_line_aspirational_marker(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            text = (
                "A future `mdux-audit` will run in `future.yml`. "
                "<!-- mdux-named-mechanisms:aspirational; issue #300 -->\n"
            )
            self.assertEqual(self.findings_for(root, text), [])

    def test_scans_only_full_line_workflow_comments(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            index = mechanisms.build_index(root)
            path = root / ".github" / "workflows" / "claim.yml"
            text = (
                "# build.yml and `mdux-shaderbake` are current controls.\n"
                "uses: owner/repo/.github/workflows/external.yml@deadbeef\n"
            )
            findings = mechanisms.check_lines(path, mechanisms.workflow_comment_lines(text), index)
            self.assertEqual(findings, [])

    def test_rejects_an_unknown_bare_tool_in_a_workflow_comment(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            index = mechanisms.build_index(root)
            path = root / ".github" / "workflows" / "claim.yml"
            text = "# mdux-imaginary-lint gates this job.\n"
            findings = mechanisms.check_lines(path, mechanisms.workflow_comment_lines(text), index)
            self.assertEqual(
                [finding.message for finding in findings],
                ["CMake/tool target 'mdux-imaginary-lint' does not exist"],
            )

    def test_the_current_tree_resolves_without_suppressions(self) -> None:
        root = Path(__file__).resolve().parents[2]
        findings, _ = mechanisms.check_repository(root)
        rendered = [
            f"{finding.path.relative_to(root)}:{finding.line}: {finding.message}" for finding in findings
        ]
        self.assertEqual(rendered, [])


if __name__ == "__main__":
    unittest.main()


class ReviewRegressionTests(CheckNamedMechanismsTests):
    """One test per defect found reviewing this checker, so none of them can come back quietly."""

    def test_a_citation_closing_a_sentence_keeps_its_full_stop_out_of_the_name(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(self.findings_for(root, "Built by mdux-shaderbake.\n"), [])

    def test_yaml_that_is_not_a_workflow_is_not_read_as_a_missing_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(self.findings_for(root, "The dependabot.yml file pins actions.\n"), [])
            # A path that does claim to be a workflow is still checked.
            self.assertEqual(
                self.findings_for(root, "See .github/workflows/absent.yml\n"),
                ["workflow '.github/workflows/absent.yml' does not exist"],
            )

    def test_every_spelling_of_an_automatic_trigger_is_recognised(self) -> None:
        block = "name: A\non:\n  # push:\n  push:\n    branches: [ main ]\njobs: {}\n"
        self.assertTrue(mechanisms.has_automatic_trigger(block))
        self.assertTrue(mechanisms.has_automatic_trigger("name: A\non: push\njobs: {}\n"))
        self.assertTrue(mechanisms.has_automatic_trigger('name: A\n"on":\n  pull_request:\njobs: {}\n'))
        self.assertTrue(mechanisms.has_automatic_trigger("name: A\non: [push]\njobs: {}\n"))
        self.assertTrue(mechanisms.has_automatic_trigger("name: A\non:\n  workflow_call:\njobs: {}\n"))
        # A commented-out trigger is not a trigger, and a nested key named push is not one either.
        self.assertFalse(
            mechanisms.has_automatic_trigger("name: A\non:\n  # push:\n  workflow_dispatch:\njobs: {}\n")
        )
        self.assertFalse(
            mechanisms.has_automatic_trigger(
                "name: A\non:\n  workflow_dispatch:\n    inputs:\n      push:\n        type: boolean\njobs: {}\n"
            )
        )

    def test_a_checkers_own_diagnostic_name_resolves(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            (root / "tools" / "docs-lint").mkdir(parents=True)
            (root / "tools" / "docs-lint" / "check_file_headers.py").write_text(
                'print("mdux-file-headers: OK")\n', encoding="utf-8"
            )
            self.assertEqual(self.findings_for(root, "The mdux-file-headers check gates every PR.\n"), [])

    def test_prose_after_a_command_does_not_invent_a_label(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            text = "Run ctest -L evidence and see ctest(1) for -L semantics.\n"
            self.assertEqual(self.findings_for(root, text), [])

    def test_a_partial_label_selects_the_way_ctest_would(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            self.assertEqual(self.findings_for(root, "ctest -L determin runs it.\n"), [])
            self.assertEqual(
                self.findings_for(root, "ctest -L zzz runs it.\n"),
                ["ctest label 'zzz' is not attached to any test"],
            )

    def test_unquoted_labels_after_the_first_are_attached(self) -> None:
        self.assertEqual(mechanisms.split_labels("unit fast"), {"unit", "fast"})
        # The next property keyword ends the list rather than becoming a label.
        self.assertEqual(mechanisms.split_labels("unit fast TIMEOUT 5"), {"unit", "fast"})

    def test_an_unclosed_fence_is_reported_rather_than_blinding_the_file(self) -> None:
        self.assertEqual(mechanisms.unclosed_fence_line("a\n```\nb\n"), 2)
        self.assertIsNone(mechanisms.unclosed_fence_line("a\n```\nb\n```\n"))

    def test_the_marker_must_name_a_tracking_issue(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = self.make_repository(raw)
            bare = "ctest -L invented <!-- mdux-named-mechanisms:aspirational -->\n"
            self.assertEqual(
                self.findings_for(root, bare),
                ["aspirational marker names no tracking issue; append 'issue #NNN' to it"],
            )
            tracked = "ctest -L invented <!-- mdux-named-mechanisms:aspirational; issue #249 -->\n"
            self.assertEqual(self.findings_for(root, tracked), [])

    def test_scanning_nothing_is_a_failure_rather_than_a_pass(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            self.assertEqual(mechanisms.main(["--repo-root", raw]), 1)
