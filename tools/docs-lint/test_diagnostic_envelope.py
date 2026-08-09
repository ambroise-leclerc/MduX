"""Every MduX tool's `--format=json` output must satisfy the one published envelope.

The envelope is `docs/governance/schemas/diagnostic.schema.json` (issue #118). It is checked here,
in the documentation-lint job, because that job needs no C++ toolchain and therefore runs on the
PR that changes a schema - which is exactly the PR that breaks this.

Coverage is deliberately split. The Python lints are exercised end to end, as subprocesses, so
what is validated is the bytes a consumer actually receives rather than a reconstruction of them.
The C++ bakers cannot be run from here without a build, so their side of the contract is held by
two other checks: `check_schema_type_drift.py` binds `mdux::tools::cli::Diagnostic` and the
`describe(Severity)` literals to this schema, and tests/tools/CliTests.cpp pins the rendered JSON
literally. Between them, a field added to the C++ envelope without a schema change fails here, and
a schema change without a C++ change fails there.
"""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

import schema_subset

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "docs/governance/schemas/diagnostic.schema.json"

# Every tool that emits the envelope, as (name, argv). Adding a baker to this list is the cheapest
# possible way to hold it to the contract, and is expected when one lands.
ENVELOPE_TOOLS = (
    ("mdux-docs-lint", ["tools/docs-lint/mdux_docs_lint.py", "--format", "json"]),
    ("mdux-evidence-lint", ["tools/evidence-lint/mdux_evidence_lint.py", "--format", "json"]),
)


def load_schema() -> dict:
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


def finding_schema(schema: dict) -> dict:
    return schema["properties"]["findings"]["items"]


VALID_FINDING = {
    "file": "docs/iec62304/03-development-process.md",
    "line": 42,
    "column": 7,
    "code": "MDX-D001",
    "severity": "error",
    "message": "Unknown or malformed standard identifier 'IEC 62304:2011' in citation key.",
    "fixHint": "Use one of: IEC 62304:2006, ISO 13485:2016",
}


class SchemaShapeTests(unittest.TestCase):
    def setUp(self):
        self.schema = load_schema()

    def test_the_schema_is_valid_json_and_names_itself(self):
        self.assertEqual(
            "https://mdux.dev/schemas/governance/diagnostic.schema.json", self.schema["$id"]
        )

    def test_a_finding_declares_every_envelope_field_as_required(self):
        # An optional field would let two tools disagree about whether to emit it, which is the
        # one thing a single shared envelope exists to prevent.
        self.assertEqual(
            {"file", "line", "column", "code", "severity", "message", "fixHint"},
            set(finding_schema(self.schema)["required"]),
        )

    def test_the_severity_vocabulary_is_closed(self):
        self.assertEqual(
            ["error", "warning", "note"],
            finding_schema(self.schema)["properties"]["severity"]["enum"],
        )


class ValidationTests(unittest.TestCase):
    """The validator must reject, or every assertion built on it is worthless."""

    def setUp(self):
        self.schema = load_schema()
        self.finding = finding_schema(self.schema)

    def test_a_well_formed_finding_validates(self):
        self.assertEqual([], schema_subset.violations(VALID_FINDING, self.finding))

    def test_a_finding_missing_column_is_rejected(self):
        finding = {k: v for k, v in VALID_FINDING.items() if k != "column"}
        self.assertNotEqual([], schema_subset.violations(finding, self.finding))

    def test_an_unknown_field_is_rejected(self):
        # additionalProperties: false is what makes the envelope a contract rather than a
        # suggestion - a tool cannot quietly add a field only its own consumer understands.
        finding = dict(VALID_FINDING, endColumn=9)
        problems = schema_subset.violations(finding, self.finding)
        self.assertTrue(any("endColumn" in p for p in problems), problems)

    def test_an_unknown_severity_is_rejected(self):
        finding = dict(VALID_FINDING, severity="fatal")
        self.assertNotEqual([], schema_subset.violations(finding, self.finding))

    def test_a_negative_position_is_rejected(self):
        self.assertNotEqual(
            [], schema_subset.violations(dict(VALID_FINDING, line=-1), self.finding)
        )

    def test_an_absent_code_is_allowed(self):
        # A tool that has not assigned codes yet still emits a valid envelope.
        self.assertEqual([], schema_subset.violations(dict(VALID_FINDING, code=""), self.finding))

    def test_an_unknown_envelope_level_field_is_rejected(self):
        envelope = {"tool": "mdux-docs-lint", "findings": [], "warnings": []}
        problems = schema_subset.violations(envelope, self.schema)
        self.assertTrue(any("warnings" in p for p in problems), problems)


class ToolOutputTests(unittest.TestCase):
    """What each tool actually prints, over the real repository, must validate."""

    def setUp(self):
        self.schema = load_schema()

    def run_tool(self, argv: list[str]) -> dict:
        completed = subprocess.run(
            [sys.executable, *argv],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        # Exit status is 1 when there are findings and 0 when there are none; both print an
        # envelope, and both are acceptable here. Only a crash is not.
        self.assertIn(completed.returncode, (0, 1), completed.stderr)
        return json.loads(completed.stdout)

    def test_every_tool_emits_a_valid_envelope(self):
        for name, argv in ENVELOPE_TOOLS:
            with self.subTest(tool=name):
                envelope = self.run_tool(argv)
                self.assertEqual(name, envelope["tool"])
                self.assertEqual([], schema_subset.violations(envelope, self.schema))

    def test_a_findings_bearing_run_also_validates(self):
        # The clean repository yields an empty findings array, which would let a malformed
        # finding shape pass unnoticed. Lint a file planted to fail instead.
        import tempfile

        with tempfile.TemporaryDirectory(dir=REPO_ROOT) as tmp:
            offending = Path(tmp) / "citation.md"
            offending.write_text(
                'A citation key of "IEC 62304:2011 §5.1 Planning" names an edition '
                "this repository does not recognise.\n",
                encoding="utf-8",
            )
            relative = offending.relative_to(REPO_ROOT)
            envelope = self.run_tool(
                ["tools/docs-lint/mdux_docs_lint.py", "--format", "json", str(relative)]
            )

        self.assertTrue(envelope["findings"], "the planted file produced no finding")
        self.assertEqual([], schema_subset.violations(envelope, self.schema))
        # And the column is a real one, not a placeholder: the citation does not start at the
        # beginning of the line.
        self.assertGreater(envelope["findings"][0]["column"], 1)

    def test_evidence_lint_findings_also_validate(self):
        # mdux-evidence-lint needs its own planted failure for the same reason: over a clean
        # repository it reports nothing, so its per-finding shape - including whether it emits
        # the `column` key this PR adds - would never be exercised by the empty case above.
        import tempfile

        with tempfile.TemporaryDirectory(dir=REPO_ROOT) as tmp:
            offending = Path(tmp) / "Offender.cpp"
            offending.write_text(
                '// A decimal float conversion in an evidence-pipeline source.\n'
                'void report(double value) { std::printf("%f\\n", value); }\n',
                encoding="utf-8",
            )
            relative = offending.relative_to(REPO_ROOT)
            envelope = self.run_tool(
                ["tools/evidence-lint/mdux_evidence_lint.py", "--format", "json", str(relative)]
            )

        self.assertEqual("mdux-evidence-lint", envelope["tool"])
        self.assertTrue(envelope["findings"], "the planted file produced no finding")
        self.assertEqual([], schema_subset.violations(envelope, self.schema))
        finding = envelope["findings"][0]
        self.assertEqual("EVL001", finding["code"])
        # The envelope requires `column` from every tool, not only the ones that compute a
        # precise one; evidence-lint reports whole lines, so 0 is its honest answer.
        self.assertIn("column", finding)


if __name__ == "__main__":
    unittest.main()
