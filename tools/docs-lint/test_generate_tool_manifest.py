#!/usr/bin/env python3
"""Tests for the host-tool manifest generator.

The property under test is #265's own acceptance: the manifest is generated from the tools' own
registration rather than hand-written, so a tool added to the build cannot be missing from it. Two
kinds of check follow from that - the parsing is exercised on constructed input, and the generated
manifest is compared against the real tree, which is where a generator that quietly stopped finding
anything would show up.
"""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

import generate_tool_manifest as manifest


class ParseBakeCallTests(unittest.TestCase):
    def test_keywords_and_lists_are_read(self):
        call = manifest.parse_bake_call(
            """
            KIND shader
            ID mdux-ui
            TOOL mdux-shaderbake
            RECIPE recipes/shader/mdux-ui.toml
            SOURCES
                a.spv
                b.spv
            OUTPUTS
                package.json
            """
        )
        self.assertEqual("shader", call["KIND"])
        self.assertEqual("mdux-shaderbake", call["TOOL"])
        self.assertEqual(["a.spv", "b.spv"], call["SOURCES"])
        self.assertEqual(["package.json"], call["OUTPUTS"])

    def test_a_comment_does_not_become_a_source(self):
        # `#` runs to the end of the line, so dropping tokens that *begin* with it keeps every word
        # of the sentence after it. That turned a six-file SOURCES list into a hundred and
        # thirty-four entries made mostly of English, and every one of them looked like a file.
        call = manifest.parse_bake_call(
            """
            SOURCES
                real/path.json
                # The sidecar too, and not as belt and braces: loadLocales() reads these bytes
                other/path.bin
            """
        )
        self.assertEqual(["real/path.json", "other/path.bin"], call["SOURCES"])


class RealRepositoryTests(unittest.TestCase):
    """The generator against the tree it describes."""

    def setUp(self):
        self.root = Path(__file__).resolve().parents[2]
        self.manifest = manifest.build_manifest(self.root)

    def test_every_built_tool_is_listed(self):
        # The acceptance, stated directly: the list comes from `add_executable`, so a tool that
        # exists cannot be absent. Read independently here rather than through the generator's own
        # regex, so a regex that stopped matching fails instead of agreeing with itself.
        text = (self.root / manifest.TOOLS_CMAKE).read_text(encoding="utf-8")
        built = set(re.findall(r"add_executable\((mdux-[\w-]+)", text))
        listed = {tool["name"] for tool in self.manifest["tools"]}
        self.assertEqual(built, listed)
        self.assertGreater(len(built), 1, "the tool list must not be empty or a single accident")

    def test_every_entry_point_exists(self):
        for tool in self.manifest["tools"]:
            with self.subTest(tool=tool["name"]):
                self.assertTrue((self.root / tool["entryPoint"]).is_file())

    def test_every_baked_artifact_names_a_recipe_and_outputs(self):
        seen = 0
        for tool in self.manifest["tools"]:
            for baked in tool["bakes"]:
                with self.subTest(artifact=f"{baked['kind']}/{baked['id']}"):
                    self.assertTrue((self.root / baked["recipe"]).is_file())
                    self.assertTrue(baked["outputs"], "an artifact with no outputs is not an artifact")
                    for source in baked["sources"]:
                        # Every source is a path in the tree. A comment leaking into this list
                        # produced entries that were ordinary English words and passed unnoticed.
                        self.assertTrue((self.root / source).is_file(), f"'{source}' is not a file")
                    seen += 1
        self.assertGreater(seen, 0, "no baked artifact was found, so nothing was checked")

    def test_the_screen_is_attributed_to_the_compiler(self):
        # `mdux_compile_screen()` is a wrapper over `mdux_bake_artifact()`, so a generator reading
        # only the direct calls reports that mdux-meduic bakes nothing.
        meduic = next(t for t in self.manifest["tools"] if t["name"] == "mdux-meduic")
        screens = [baked for baked in meduic["bakes"] if baked["kind"] == "screen"]
        self.assertTrue(screens, "the compiler must be credited with the screen it bakes")
        self.assertEqual(
            ["goldens.json", "package.json", "report.json", "verification.json"],
            screens[0]["outputs"],
            "the four outputs the wrapper fixes, read from it rather than restated here",
        )

    def test_a_shared_grammar_tool_carries_the_shared_options(self):
        # `--format` and `--help` are spelled in tools/common/Cli.cpp, not in each entry point, so a
        # manifest reading only the file would be accurate about the source and wrong about the tool.
        meduic = next(t for t in self.manifest["tools"] if t["name"] == "mdux-meduic")
        self.assertEqual("shared-bake-verify", meduic["grammar"])
        for option in ("--format", "--help", "--dump-ir", "--explain", "--grammar"):
            self.assertIn(option, meduic["options"])

    def test_diagnostic_codes_are_found_and_well_formed(self):
        for tool in self.manifest["tools"]:
            with self.subTest(tool=tool["name"]):
                self.assertTrue(tool["diagnosticCodes"], "every tool publishes at least one code")
                for code in tool["diagnosticCodes"]:
                    self.assertRegex(code, r"^[A-Z][A-Z0-9-]*\d{3}$")

    def test_the_committed_manifest_is_current(self):
        # The gate `--check` applies in CI, asserted here too so the generator and the file cannot
        # drift apart in a tree where only the unit tests are run.
        path = self.root / manifest.MANIFEST
        self.assertTrue(path.is_file(), f"{manifest.MANIFEST} is not committed")
        self.assertEqual(
            path.read_text(encoding="utf-8"),
            manifest.render(self.manifest),
            "regenerate with: python3 tools/docs-lint/generate_tool_manifest.py",
        )

    def test_the_manifest_carries_nothing_that_changes_between_runs(self):
        rendered = manifest.render(self.manifest)
        self.assertEqual(rendered, manifest.render(manifest.build_manifest(self.root)))
        self.assertNotIn(str(self.root), rendered, "no absolute path from this machine")
        self.assertTrue(rendered.endswith("}\n"))
        # Canonical form: sorted keys, so a diff of two versions reads as the change.
        self.assertLess(rendered.index('"schemaVersion"'), rendered.index('"tools"'))


if __name__ == "__main__":
    unittest.main()
