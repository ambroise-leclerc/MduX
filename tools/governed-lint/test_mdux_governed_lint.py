#!/usr/bin/env python3
"""Unit tests for mdux-governed-lint.

Run with `python3 -m unittest discover -s tools/governed-lint -p "test_*.py"`, the same way
tools/docs-lint and tools/evidence-lint are run in CI.

The tests that matter most here are the *negative* ones - the cases where the lint must not fire.
A source lint over a heavily-commented tree earns its keep by being quiet about prose, and the
governed tree discusses every construct this file bans. A lint that reports those is a lint an
author learns to route around by rewording documentation, which is worse than no lint at all.
"""
from __future__ import annotations

import unittest
from pathlib import Path

import mdux_governed_lint as lint


def findings_for(source: str) -> list[lint.Finding]:
    """Runs the rules over a source string, without touching the filesystem."""
    findings: list[lint.Finding] = []
    code_only = lint.strip_comments_and_literals(source)
    original_lines = source.splitlines()

    line_starts = [0]
    for index, character in enumerate(code_only):
        if character == "\n":
            line_starts.append(index + 1)

    for rule in lint.RULES:
        for match in rule.pattern.finditer(code_only):
            line = sum(1 for start in line_starts if start <= match.start())
            original = original_lines[line - 1] if line <= len(original_lines) else ""
            if original.rstrip().endswith(lint.SUPPRESS_MARKER):
                continue
            findings.append(
                lint.Finding(
                    path="<test>",
                    line=line,
                    code=rule.code,
                    severity="error",
                    message=rule.message,
                )
            )
    return findings


def codes_for(source: str) -> list[str]:
    return sorted(f.code for f in findings_for(source))


class StripCommentsAndLiterals(unittest.TestCase):
    def test_preserves_line_numbers(self):
        source = 'int a;\n// throw\nint b;\n"throw"\nint c;\n'
        stripped = lint.strip_comments_and_literals(source)
        self.assertEqual(source.count("\n"), stripped.count("\n"))

    def test_preserves_offsets(self):
        source = 'x = "abc"; y = 1;\n'
        stripped = lint.strip_comments_and_literals(source)
        self.assertEqual(len(source), len(stripped))
        self.assertEqual(stripped.index("y"), source.index("y"))

    def test_blanks_line_comment(self):
        self.assertNotIn("throw", lint.strip_comments_and_literals("int a; // throw here\n"))

    def test_blanks_block_comment_spanning_lines(self):
        source = "/*\n * throw\n */\nint a;\n"
        stripped = lint.strip_comments_and_literals(source)
        self.assertNotIn("throw", stripped)
        self.assertIn("int a;", stripped)

    def test_blanks_string_literal(self):
        self.assertNotIn("throw", lint.strip_comments_and_literals('auto s = "throw";\n'))

    def test_blanks_raw_string_literal(self):
        source = 'auto s = R"json({"throw": 1})json";\n'
        self.assertNotIn("throw", lint.strip_comments_and_literals(source))

    def test_keeps_code_outside_literals(self):
        self.assertIn("throw", lint.strip_comments_and_literals('throw Err{"msg"};\n'))

    def test_escaped_quote_does_not_end_the_literal(self):
        # The literal runs to the *second* unescaped quote; `throw` is inside it throughout.
        source = 'auto s = "a\\" throw b";\nint after;\n'
        stripped = lint.strip_comments_and_literals(source)
        self.assertNotIn("throw", stripped)
        self.assertIn("int after;", stripped)


class Rules(unittest.TestCase):
    def test_throw(self):
        self.assertEqual(codes_for("throw Error{};\n"), ["GOV001"])

    def test_try_and_catch(self):
        self.assertEqual(codes_for("try { f(); } catch (...) {}\n"), ["GOV002", "GOV002"])

    def test_value_accessor(self):
        self.assertEqual(codes_for("auto x = result.value();\n"), ["GOV003"])

    def test_raw_allocation(self):
        self.assertEqual(codes_for("auto* p = new Widget{};\n"), ["GOV004"])
        self.assertEqual(codes_for("delete p;\n"), ["GOV004"])
        self.assertEqual(codes_for("void* p = malloc(4);\n"), ["GOV004"])

    def test_filesystem_and_console(self):
        self.assertEqual(codes_for("std::filesystem::path p;\n"), ["GOV005"])
        self.assertEqual(codes_for("std::cout << x;\n"), ["GOV005"])

    def test_clock_and_randomness(self):
        self.assertEqual(codes_for("auto t = std::chrono::system_clock::now();\n"), ["GOV006"])
        self.assertEqual(codes_for("std::random_device rd;\n"), ["GOV006"])

    def test_unsafe_casts(self):
        self.assertEqual(codes_for("auto* p = reinterpret_cast<int*>(q);\n"), ["GOV007"])
        self.assertEqual(codes_for("auto* p = const_cast<int*>(q);\n"), ["GOV007"])

    def test_fma(self):
        self.assertEqual(codes_for("acc = std::fma(a, b, acc);\n"), ["GOV008"])

    def test_banned_platform_headers(self):
        self.assertEqual(codes_for("#include <vulkan/vulkan.h>\n"), ["GOV009"])
        self.assertEqual(codes_for("#include <GLFW/glfw3.h>\n"), ["GOV009"])
        self.assertEqual(codes_for("#include <windows.h>\n"), ["GOV009"])
        self.assertEqual(codes_for("#include <sys/mman.h>\n"), ["GOV009"])
        # Spacing variants a real file might use.
        self.assertEqual(codes_for("#  include  < vulkan/vulkan.h >\n"), ["GOV009"])

    def test_permitted_std_headers(self):
        # src/draw/Draw.cppm:39 does exactly this, in a global module fragment.
        self.assertEqual(codes_for("#include <cstddef>\n"), [])
        self.assertEqual(codes_for("#include <stdexcept>\n"), [])

    def test_suppression_marker(self):
        source = "auto* p = reinterpret_cast<int*>(q);  // mdux-governed-lint:allow\n"
        self.assertEqual(codes_for(source), [])


class NotFindings(unittest.TestCase):
    """Constructs that must stay quiet. Each quotes a real line from the governed tree.

    The `/**` and `*/` around the block-comment cases are not decoration. A comment *interior*
    passed on its own is, correctly, code - which is what these fixtures asserted on the first
    attempt, and they failed for exactly that reason. The delimiters are what makes the fixture
    the thing it claims to quote.
    """

    def test_prose_about_throwing(self):
        # include/mdux/ml/Runtime.cppm:46
        source = "/**\n * throw away exactly the information the incident report needs\n */\n"
        self.assertEqual(codes_for(source), [])

    def test_prose_about_fma(self):
        # include/mdux/ml/Kernels.cppm:28
        source = (
            "/**\n"
            " * - **Never `std::fma`.** It rounds once where the scalar sequence rounds twice.\n"
            " */\n"
        )
        self.assertEqual(codes_for(source), [])

    def test_prose_about_a_new_command(self):
        # src/draw/Draw.cpp:168
        source = "    // A new command is needed when nothing has been recorded yet\n"
        self.assertEqual(codes_for(source), [])

    def test_prose_about_catching(self):
        # include/mdux/text/Draw.cppm:54
        source = "/**\n * is not an error the type system can catch, but\n */\n"
        self.assertEqual(codes_for(source), [])

    def test_has_value_is_not_value(self):
        self.assertEqual(codes_for("if (result.has_value()) { }\n"), [])

    def test_identifier_containing_new(self):
        self.assertEqual(codes_for("auto renewed = compute();\n"), [])

    def test_string_mentioning_a_banned_construct(self):
        self.assertEqual(codes_for('report("cannot throw here");\n'), [])


class GovernedSourceList(unittest.TestCase):
    def test_parses_the_real_cmakelists(self):
        root = lint.find_repository_root()
        sources = lint.governed_sources(root)
        self.assertTrue(sources)
        for path in sources:
            self.assertTrue(path.is_file(), f"{path} does not exist")

    def test_excludes_the_adapter_target(self):
        """The block scan must stop at MduXCore's closing paren.

        It did not, in the first version of this file: the scan started one character past the
        opening paren, so depth never returned to zero and the "block" ran to end of file,
        swallowing MduX's source list. The adapter is permitted to throw, so the lint reported
        every one of DeviceObjectManager.cpp's throws as a governed violation.
        """
        root = lint.find_repository_root()
        names = {str(p.relative_to(root)) for p in lint.governed_sources(root)}
        self.assertNotIn("src/vulkansc/DeviceObjectManager.cpp", names)
        self.assertNotIn("src/render/Offscreen.cpp", names)
        self.assertIn("src/ml/Runtime.cpp", names)

    def test_excludes_generated_sources(self):
        """#116 requires that generated shader C arrays are not scanned as governed source.

        They are emitted into the build tree, so nothing under the repository's source list can
        name one - this asserts that property rather than trusting it.
        """
        root = lint.find_repository_root()
        for path in lint.governed_sources(root):
            self.assertNotIn("mdux_generated", str(path))

    def test_rejects_a_cmakelists_without_the_block(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "CMakeLists.txt").write_text("project(Nothing)\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                lint.governed_sources(Path(tmp))


class TreeIsClean(unittest.TestCase):
    def test_no_findings_on_the_governed_tree(self):
        """The lint passes on `develop`. Kept as a test so a violation fails here too, not only
        in the CI job - a contributor running the unit tests locally should see it."""
        root = lint.find_repository_root()
        findings: list[lint.Finding] = []
        for source in lint.governed_sources(root):
            lint.check_file(source, root, findings)
        self.assertEqual(
            [],
            [f"{f.path}:{f.line}: [{f.code}] {f.message}" for f in findings],
        )


if __name__ == "__main__":
    unittest.main()
