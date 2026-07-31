"""Tests for generate_ai_reference.

The last two classes are the ones that matter most. `InvariantTests` checks issue #32's two
invariants on synthetic corpora, and `RealCorpusInvariantTests` checks them against the corpora
actually in this repository - so a module edited without regenerating its index, or a clause cited
in prose that no row covers, fails here rather than being noticed by a reader six months later.
"""

import re
import tempfile
import unittest
from pathlib import Path

import generate_ai_reference as gen

REPO_ROOT = Path(__file__).resolve().parents[2]

# A section body long enough to clear MIN_SUBSTANTIVE_CHARS, naming MduX so a pointer derives.
BODY = (
    "MduX supplies a mechanical check here rather than a documented procedure, which is the "
    "distinction this clause turns on for a library with no quality system of its own.\n"
)


def write_corpus(directory: Path, files: dict) -> None:
    for name, text in files.items():
        (directory / name).write_text(text, encoding="utf-8")


def anchors_in(path: Path) -> list:
    """Every anchor GitHub would generate for `path`, de-duplicated the way GitHub does."""
    anchors = []
    seen = {}
    for heading in re.findall(r"^#{1,6}\s+(.+)$", path.read_text(encoding="utf-8"), re.MULTILINE):
        anchor = gen.github_anchor(heading)
        count = seen.get(anchor, 0)
        seen[anchor] = count + 1
        anchors.append(f"{anchor}-{count}" if count else anchor)
    return anchors


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


class ClauseTests(unittest.TestCase):
    def test_clause_extracted_from_heading_prefix(self):
        self.assertEqual("§5.2", gen.clause_of("§5.2 Software requirements analysis"))
        self.assertEqual("§9", gen.clause_of("IEC 62304:2006 §9 — Problem resolution"))

    def test_clause_range_is_normalized(self):
        self.assertEqual("§1–§3", gen.clause_of("IEC 62304:2006 §1–§3 — Scope and terms"))

    def test_heading_without_a_clause_has_none(self):
        self.assertIsNone(gen.clause_of("Perform summative evaluation"))

    def test_title_drops_the_clause_and_separator(self):
        self.assertEqual("Software release", gen.title_of("§5.8 Software release"))
        self.assertEqual(
            "Problem resolution", gen.title_of("IEC 62304:2006 §9 — Problem resolution")
        )


class ClauseInheritanceTests(unittest.TestCase):
    """The defect that made IEC 62304 §9 vanish from an earlier version of the index."""

    def test_unnumbered_sections_inherit_a_single_clause_h1(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "07-problem-resolution.md": "# IEC 62304:2006 §9 — Problem resolution\n\n"
                    f"## Reporting a problem\n\n{BODY}\n## Resolving it\n\n{BODY}"
                },
            )
            rows, problems = gen.build_rows(directory)
        self.assertEqual([], problems)
        self.assertEqual(["§9", "§9"], [row.clause for row in rows])

    def test_a_clause_range_is_not_inherited(self):
        # A narrative section inside a §1–§3 file is not all three of those clauses.
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "01-scope.md": "# ISO 14971:2019 §1–§3 — Scope and terms\n\n"
                    f"## §1 Scope\n\n{BODY}\n## Relationship to another standard\n\n{BODY}"
                },
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual(["§1", gen.NO_CLAUSE], [row.clause for row in rows])


class AnchorTests(unittest.TestCase):
    def test_anchor_matches_github_slugging(self):
        self.assertEqual(
            "52-software-requirements-analysis",
            gen.github_anchor("§5.2 Software requirements analysis"),
        )
        self.assertEqual("one-risk-file-not-two", gen.github_anchor("One risk file, not two"))

    def test_repeated_headings_get_githubs_suffix(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {"a.md": f"# ISO 14971:2019 §4 — Title\n\n## Notes\n\n{BODY}\n## Notes\n\n{BODY}"},
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual(["notes", "notes-1"], [row.anchor for row in rows])

    def test_rows_link_to_the_anchor_not_just_the_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory, {"a.md": f"# ISO 14971:2019 §4 — Title\n\n## §4.1 Sub\n\n{BODY}"}
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual("a.md#41-sub", rows[0].link)


class PointerTests(unittest.TestCase):
    def test_authored_comment_wins(self):
        body = "<!-- pointer: The authored one. -->\n\n" + BODY
        self.assertEqual("The authored one.", gen.pointer_for("§1 X", body))

    def test_justification_rationale_is_used_when_there_is_no_comment(self):
        body = (
            BODY
            + '\n```json\n{"justification_id": "JUS-001", '
            '"rationale": "MduXTrustZones.cmake fails the configure step. More detail follows."}'
            "\n```\n"
        )
        # The Justification outranks the prose sentence, and only its first sentence is used.
        self.assertEqual(
            "MduXTrustZones.cmake fails the configure step.", gen.pointer_for("§1 X", body)
        )

    def test_falls_back_to_the_first_usable_mdux_sentence(self):
        body = "A clause paraphrase with no mechanism in it. MduX has no mechanism here.\n"
        self.assertEqual("MduX has no mechanism here.", gen.pointer_for("§1 X", body))

    def test_table_rows_and_bare_pronouns_are_not_usable_pointers(self):
        # Both would produce a row that either breaks the index's own table or refers to an
        # antecedent a reader of the index cannot see.
        self.assertFalse(gen.is_usable_pointer("| MduX | something |"))
        self.assertFalse(gen.is_usable_pointer("It is MduX's mechanism."))
        self.assertFalse(gen.is_usable_pointer("A sentence not naming the library."))
        self.assertTrue(gen.is_usable_pointer("MduX has no mechanism here."))

    def test_sub_headings_do_not_leak_into_a_pointer(self):
        body = "### Attack surface\n\nMduX constrains the link graph at configure time.\n"
        self.assertEqual(
            "MduX constrains the link graph at configure time.",
            gen.pointer_for("Secure design", body),
        )

    def test_a_section_with_no_pointer_raises(self):
        with self.assertRaises(gen.PointerMissing):
            gen.pointer_for("§1 X", "Prose that never names the library at all.\n")


class BuildRowsTests(unittest.TestCase):
    def test_skips_readme_and_existing_ai_reference(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "README.md": f"# Readme\n\n## §1 Not indexed\n\n{BODY}",
                    "AI-Reference.md": f"# Index\n\n## §2 Not indexed\n\n{BODY}",
                    "01-real.md": f"# ISO 14971:2019 §4 — Title\n\n## §4.1 Indexed\n\n{BODY}",
                },
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual(["§4.1"], [row.clause for row in rows])

    def test_attributes_a_justification_to_the_heading_it_appears_under(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "a.md": "# ISO 14971:2019 §4 — Title\n\n"
                    f"## §4.1 First\n\n{BODY}\n"
                    f"## §4.2 Second\n\n{BODY}\n"
                    '```json\n{"justification_id": "JUS-007"}\n```\n'
                },
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual((), rows[0].justifications)
        self.assertEqual(("JUS-007",), rows[1].justifications)

    def test_files_are_processed_in_sorted_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "02-b.md": f"# ISO 14971:2019 §5 — B\n\n## §5.1 B\n\n{BODY}",
                    "01-a.md": f"# ISO 14971:2019 §4 — A\n\n## §4.1 A\n\n{BODY}",
                },
            )
            rows, _ = gen.build_rows(directory)
        self.assertEqual(["01-a.md", "02-b.md"], [row.file_name for row in rows])


class InvariantTests(unittest.TestCase):
    """Issue #32's two invariants, on synthetic corpora."""

    def test_invariant_one_a_stub_section_is_reported_not_indexed(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "a.md": "# ISO 14971:2019 §4 — Title\n\n"
                    f"## §4.1 Real\n\n{BODY}\n## §4.2 Stub\n\nTBD.\n"
                },
            )
            rows, problems = gen.build_rows(directory)
        self.assertEqual(["§4.1"], [row.clause for row in rows])
        self.assertTrue(any("no substantive content" in p for p in problems), problems)

    def test_invariant_one_a_justification_makes_a_short_section_substantive(self):
        # A checked Justification is content: mdux-docs-lint verifies its evidence paths exist.
        body = '```json\n{"justification_id": "JUS-001", "rationale": "MduX does the thing."}\n```\n'
        self.assertTrue(gen.is_substantive(body))
        self.assertFalse(gen.is_substantive("TBD.\n"))

    def test_invariant_two_a_clause_cited_in_prose_with_no_row_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "a.md": "# ISO 14971:2019 §4 — Title\n\n## §4.1 Covered\n\n"
                    + BODY
                    + "\nThis also depends on §4.9, which has no section of its own.\n"
                },
            )
            rows, _ = gen.build_rows(directory)
            missing = gen.unindexed_clauses(directory, rows)
        self.assertEqual(["§4.9"], missing)

    def test_invariant_two_a_parent_clause_is_covered_by_its_children(self):
        rows = [
            gen.Row("§7.3", "t", "a.md", "a", "p", ()),
            gen.Row("§1–§3", "t", "a.md", "b", "p", ()),
        ]
        covered = gen.indexed_clauses(rows)
        self.assertIn("§7", covered)
        self.assertIn("§7.3", covered)
        self.assertIn("§2", covered)
        self.assertNotIn("§7.4", covered)

    def test_invariant_two_ignores_another_standards_clauses(self):
        # A module citing "IEC 62304:2006 §5.1" from inside docs/iso14971/ points at a different
        # corpus; demanding an ISO 14971 §5.1 row for it would be wrong.
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / "iso14971"
            directory.mkdir()
            write_corpus(
                directory,
                {
                    "a.md": "# ISO 14971:2019 §4 — Title\n\n## §4.1 Covered\n\n"
                    + BODY
                    + "\nSee IEC 62304:2006 §5.1 Software development planning for that side.\n"
                },
            )
            rows, _ = gen.build_rows(directory)
            self.assertEqual([], gen.unindexed_clauses(directory, rows))


class RenderTests(unittest.TestCase):
    def test_render_includes_regenerate_command_and_table_header(self):
        output = gen.render("ISO 14971:2019", Path("docs/iso14971"), [])
        self.assertIn("python3 tools/docs-lint/generate_ai_reference.py docs/iso14971", output)
        self.assertIn("| Clause | Covers | Pointer | Justification(s) |", output)

    def test_render_escapes_pipes_in_title_and_pointer(self):
        row = gen.Row("§1", "a|b", "f.md", "anchor", "p|q", ("JUS-001",))
        output = gen.render("S", Path("d"), [row])
        self.assertIn("a\\|b", output)
        self.assertIn("p\\|q", output)
        self.assertIn("[a\\|b](f.md#anchor)", output)


class CheckModeTests(unittest.TestCase):
    def test_check_reports_an_out_of_date_index_without_writing_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            write_corpus(
                directory,
                {
                    "a.md": f"# ISO 14971:2019 §4 — Title\n\n## §4.1 Sub\n\n{BODY}",
                    "AI-Reference.md": "stale\n",
                },
            )
            ok, messages = gen.process(directory, check_only=True)
            self.assertFalse(ok)
            self.assertIn("out of date", messages[0])
            self.assertEqual("stale\n", (directory / "AI-Reference.md").read_text())

            self.assertTrue(gen.process(directory, check_only=False)[0])
            self.assertTrue(gen.process(directory, check_only=True)[0])


class RealCorpusInvariantTests(unittest.TestCase):
    """The same invariants, against the corpora actually in this repository."""

    def corpora(self):
        for name in gen.DEFAULT_DIRS:
            directory = REPO_ROOT / name
            if directory.is_dir():
                yield directory

    def test_there_are_corpora_to_check(self):
        # Guards every other test in this class: a path change that found nothing would make
        # them all pass vacuously.
        self.assertEqual(len(gen.DEFAULT_DIRS), len(list(self.corpora())))

    def test_every_corpus_generates_without_problems(self):
        for directory in self.corpora():
            with self.subTest(corpus=directory.name):
                _, problems = gen.build_rows(directory)
                self.assertEqual([], problems)

    def test_every_cited_clause_has_a_row(self):
        for directory in self.corpora():
            with self.subTest(corpus=directory.name):
                rows, _ = gen.build_rows(directory)
                self.assertEqual([], gen.unindexed_clauses(directory, rows))

    def test_every_index_is_up_to_date(self):
        for directory in self.corpora():
            with self.subTest(corpus=directory.name):
                ok, messages = gen.process(directory, check_only=True)
                self.assertTrue(ok, messages)

    def test_every_row_links_to_a_heading_that_exists(self):
        for directory in self.corpora():
            rows, _ = gen.build_rows(directory)
            self.assertTrue(rows, f"{directory} produced no rows")
            for row in rows:
                with self.subTest(corpus=directory.name, row=row.title):
                    target = directory / row.file_name
                    self.assertTrue(target.is_file(), f"{row.link} names a missing file")
                    self.assertIn(row.anchor, anchors_in(target), f"{row.link} is a dead anchor")

    def test_no_pointer_merely_restates_its_own_title(self):
        """The defect the review named: a row whose pointer repeats its heading.

        Length is deliberately not the test. "No MduX-specific mechanism." is a shorter pointer
        than the clause title it sits beside and a perfectly good one - it answers the question a
        reader brought to the index. Nor is "the pointer contains the title" a defect: a pointer
        for a risk-management clause will naturally say "risk management". What must not happen
        is the pointer *being* the title, which is what the previous generator produced.
        """
        def normalize(text):
            return re.sub(r"[^a-z0-9 ]", "", text.lower()).strip()

        for directory in self.corpora():
            rows, _ = gen.build_rows(directory)
            for row in rows:
                with self.subTest(corpus=directory.name, row=row.title):
                    pointer, title = normalize(row.pointer), normalize(row.title)
                    self.assertTrue(pointer, "pointer is empty")
                    self.assertNotEqual(pointer, title, "pointer restates its own heading")


class ClauseIndexExportTests(unittest.TestCase):
    """The JSON export must satisfy docs/governance/schemas/clause-index.schema.json.

    Checked against the schema file itself rather than against a copy of its rules, so the two
    cannot drift. `jsonschema` is not a dependency of this repository - a zero-SOUP project does
    not add one to validate five generated files - so this walks the subset of Draft 2020-12 the
    schema actually uses: required, enum, pattern, minItems and additionalProperties.
    """

    def setUp(self):
        import json

        self.schema = json.loads(
            (REPO_ROOT / "docs/governance/schemas/clause-index.schema.json").read_text(
                encoding="utf-8"
            )
        )

    def check(self, value, schema, path="$"):
        """Returns a list of violation messages. Only the keywords this schema uses."""
        problems = []
        if "enum" in schema and value not in schema["enum"]:
            problems.append(f"{path}: {value!r} is not one of {schema['enum']}")
        if "pattern" in schema and not re.search(schema["pattern"], value):
            problems.append(f"{path}: {value!r} does not match {schema['pattern']}")
        if "minLength" in schema and len(value) < schema["minLength"]:
            problems.append(f"{path}: shorter than minLength")
        if schema.get("type") == "object":
            for key in schema.get("required", []):
                if key not in value:
                    problems.append(f"{path}: missing required '{key}'")
            if schema.get("additionalProperties") is False:
                for key in value:
                    if key not in schema.get("properties", {}):
                        problems.append(f"{path}: unexpected property '{key}'")
            for key, subschema in schema.get("properties", {}).items():
                if key in value:
                    problems.extend(self.check(value[key], subschema, f"{path}.{key}"))
        if schema.get("type") == "array":
            if len(value) < schema.get("minItems", 0):
                problems.append(f"{path}: fewer than minItems")
            if schema.get("uniqueItems") and len(value) != len(set(map(str, value))):
                problems.append(f"{path}: duplicate items")
            for i, item in enumerate(value):
                problems.extend(self.check(item, schema.get("items", {}), f"{path}[{i}]"))
        return problems

    def test_the_subset_validator_rejects_something(self):
        # Guards every assertion below: a validator that always returns [] would make them pass.
        broken = {"standard": "Not A Standard", "corpus": "docs/x", "rows": []}
        self.assertNotEqual([], self.check(broken, self.schema))

    def test_every_generated_export_validates(self):
        import json

        for name in gen.DEFAULT_DIRS:
            path = REPO_ROOT / name / "AI-Reference.json"
            with self.subTest(export=name):
                self.assertTrue(path.is_file(), f"{path} was not generated")
                document = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual([], self.check(document, self.schema))

    def test_the_export_carries_the_same_rows_as_the_markdown(self):
        import json

        for name in gen.DEFAULT_DIRS:
            directory = REPO_ROOT / name
            with self.subTest(corpus=name):
                rows, _ = gen.build_rows(directory)
                document = json.loads(
                    (directory / "AI-Reference.json").read_text(encoding="utf-8")
                )
                self.assertEqual(len(rows), len(document["rows"]))
                for row, exported in zip(rows, document["rows"]):
                    self.assertEqual(row.clause, exported["clause"])
                    self.assertEqual(row.anchor, exported["anchor"])
                    self.assertEqual(row.pointer, exported["pointer"])


if __name__ == "__main__":
    unittest.main()
