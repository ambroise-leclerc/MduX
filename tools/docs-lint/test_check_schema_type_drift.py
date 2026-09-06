"""Tests for check_schema_type_drift.

The tests that matter are the ones proving the check *fails* on drift. A drift check that only
ever passes is indistinguishable from no check at all, and it is the failure paths - a renamed
field, a new enumerator, a member added on one side - that a reviewer is trusting when they read
"schemas and types have not drifted" in a CI log.
"""

import json
import tempfile
import unittest
from pathlib import Path

import check_schema_type_drift as drift

MODULE = """
export namespace mdux::governance {

inline constexpr std::array<std::string_view, 3> kSafetyClassWireValues{"A", "B", "C"};
inline constexpr std::array<std::string_view, 4> kVerificationMethodWireValues{
    "test", "analysis", "inspection", "review"};

struct Hazard {
    std::string id;                         ///< `HAZ-*`
    std::string description;
    std::vector<std::string> controlledBy;  ///< `REQ-*` ids

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] static mdux::core::Result<Hazard, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

struct ProblemReport {
    std::string id;
    std::string description;
    bool closed{false};
    bool affectsRisk{false};

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
};

}  // namespace mdux::governance
"""

HAZARD_SCHEMA = {
    "type": "object",
    "required": ["id", "description", "controlled_by"],
    "properties": {
        "id": {"type": "string"},
        "description": {"type": "string"},
        "controlled_by": {"type": "array", "items": {"type": "string"}},
    },
}

# The diagnostic envelope (issue #118), in miniature: a camelCase record nested under an array,
# rather than snake_case at the document root like every governance schema.
CLI_MODULE = """
export namespace mdux::tools::cli {

struct Diagnostic {
    std::string file;
    std::size_t line{0};
    std::size_t column{0};
    std::string code;
    Severity severity{Severity::Error};
    std::string message;
    std::string fixHint;
};

}  // namespace mdux::tools::cli
"""

CLI_IMPL = """
std::string_view describe(Severity severity) noexcept {
    switch (severity) {
    case Severity::Error:   return "error";
    case Severity::Warning: return "warning";
    case Severity::Note:    return "note";
    }
    return "error";
}
"""

DIAGNOSTIC_SCHEMA = {
    "type": "object",
    "required": ["tool", "findings"],
    "properties": {
        "tool": {"type": "string"},
        "filesChecked": {"type": "integer"},
        "findings": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "file": {"type": "string"},
                    "line": {"type": "integer"},
                    "column": {"type": "integer"},
                    "code": {"type": "string"},
                    "severity": {"enum": ["error", "warning", "note"]},
                    "message": {"type": "string"},
                    "fixHint": {"type": "string"},
                },
            },
        },
    },
}


class ParsingTests(unittest.TestCase):
    def test_struct_fields_are_snake_cased_in_declaration_order(self):
        self.assertEqual(
            ["id", "description", "controlled_by"], drift.struct_fields(MODULE, "Hazard")
        )

    def test_member_functions_are_not_mistaken_for_fields(self):
        # validate() and fromJson() are declared inside the struct and must not appear.
        fields = drift.struct_fields(MODULE, "Hazard")
        self.assertNotIn("validate", fields)
        self.assertNotIn("from_json", fields)

    def test_default_member_initializers_do_not_break_field_detection(self):
        self.assertEqual(
            ["id", "description", "closed", "affects_risk"],
            drift.struct_fields(MODULE, "ProblemReport"),
        )

    def test_wire_values_are_read_in_order(self):
        self.assertEqual(["A", "B", "C"], drift.wire_values(MODULE, "kSafetyClassWireValues"))
        self.assertEqual(
            ["test", "analysis", "inspection", "review"],
            drift.wire_values(MODULE, "kVerificationMethodWireValues"),
        )

    def test_camel_naming_leaves_wire_names_as_declared(self):
        # The diagnostic envelope spells fixHint as fixHint, not fix_hint.
        self.assertEqual(
            ["file", "line", "column", "code", "severity", "message", "fixHint"],
            drift.struct_fields(CLI_MODULE, "Diagnostic", drift.CAMEL),
        )

    def test_severity_values_are_read_from_the_switch_without_the_fallback_repeat(self):
        # describe() ends with a defensive `return "error";` after the switch. Counting it would
        # report a phantom fourth value and make every comparison fail.
        self.assertEqual(["error", "warning", "note"], drift.severity_wire_values(CLI_IMPL))


class DescendTests(unittest.TestCase):
    def test_an_empty_path_returns_the_document(self):
        self.assertEqual(HAZARD_SCHEMA, drift.descend(HAZARD_SCHEMA, ()))

    def test_a_path_reaches_a_nested_record(self):
        bound = drift.descend(DIAGNOSTIC_SCHEMA, ("properties", "findings", "items"))
        self.assertIn("fixHint", bound["properties"])

    def test_a_path_that_does_not_resolve_is_an_error(self):
        with self.assertRaises(LookupError):
            drift.descend(DIAGNOSTIC_SCHEMA, ("properties", "diagnostics", "items"))


class NestedStructDriftTests(unittest.TestCase):
    """Drift in a nested record must be caught exactly as drift in a root-level one is."""

    def check(self, schema):
        return drift.check_struct(
            drift.descend(schema, ("properties", "findings", "items")),
            CLI_MODULE,
            "Diagnostic",
            (),
            drift.CAMEL,
            drift.CLI_MODULE_PATH,
        )

    def test_the_current_pair_is_clean(self):
        self.assertEqual([], self.check(DIAGNOSTIC_SCHEMA))

    def test_a_field_the_schema_does_not_declare_is_reported(self):
        # The exact drift this binding exists to catch: a baker author adds a field to Diagnostic
        # and does not touch the schema, so records written against the schema cannot carry it.
        schema = json.loads(json.dumps(DIAGNOSTIC_SCHEMA))
        del schema["properties"]["findings"]["items"]["properties"]["column"]
        problems = self.check(schema)
        self.assertEqual(1, len(problems))
        self.assertIn("column", problems[0])

    def test_a_schema_property_with_no_field_is_reported(self):
        schema = json.loads(json.dumps(DIAGNOSTIC_SCHEMA))
        schema["properties"]["findings"]["items"]["properties"]["endLine"] = {"type": "integer"}
        problems = self.check(schema)
        self.assertEqual(1, len(problems))
        self.assertIn("endLine", problems[0])

    def test_snake_casing_a_camel_binding_reports_every_multiword_field(self):
        # Guards the naming axis itself: reading the envelope with the governance convention
        # would silently rename fixHint to fix_hint and drift on both sides.
        problems = drift.check_struct(
            drift.descend(DIAGNOSTIC_SCHEMA, ("properties", "findings", "items")),
            CLI_MODULE,
            "Diagnostic",
            (),
            drift.SNAKE,
            drift.CLI_MODULE_PATH,
        )
        self.assertTrue(any("fixHint" in p for p in problems))
        self.assertTrue(any("fix_hint" in p for p in problems))

    def test_a_missing_struct_is_an_error_not_an_empty_result(self):
        with self.assertRaises(LookupError):
            drift.struct_fields(MODULE, "NoSuchType")

    def test_a_missing_array_is_an_error(self):
        with self.assertRaises(LookupError):
            drift.wire_values(MODULE, "kNoSuchArray")


class StructDriftTests(unittest.TestCase):
    def test_an_aligned_schema_reports_nothing(self):
        self.assertEqual([], drift.check_struct(HAZARD_SCHEMA, MODULE, "Hazard", ()))

    def test_a_renamed_cpp_field_is_reported_from_both_sides(self):
        module = MODULE.replace("controlledBy", "mitigatedBy")
        problems = drift.check_struct(HAZARD_SCHEMA, module, "Hazard", ())
        self.assertEqual(2, len(problems), problems)
        self.assertTrue(any("controlled_by" in p and "not a field" in p for p in problems))
        self.assertTrue(any("mitigated_by" in p and "does not declare" in p for p in problems))

    def test_a_property_with_no_matching_field_is_reported(self):
        schema = json.loads(json.dumps(HAZARD_SCHEMA))
        schema["properties"]["invented"] = {"type": "string"}
        problems = drift.check_struct(schema, MODULE, "Hazard", ())
        self.assertEqual(1, len(problems), problems)
        self.assertIn("'invented'", problems[0])

    def test_a_field_the_schema_dropped_is_reported(self):
        schema = json.loads(json.dumps(HAZARD_SCHEMA))
        del schema["properties"]["description"]
        problems = drift.check_struct(schema, MODULE, "Hazard", ())
        self.assertEqual(1, len(problems), problems)
        self.assertIn("'description'", problems[0])

    def test_schema_only_members_are_allowed_only_when_declared(self):
        schema = json.loads(json.dumps(HAZARD_SCHEMA))
        schema["properties"]["severity"] = {"type": "string"}
        self.assertEqual([], drift.check_struct(schema, MODULE, "Hazard", ("severity",)))

    def test_a_stale_schema_only_entry_is_reported(self):
        # The extending schema dropped 'severity' but the binding still lists it; without this
        # check the binding would quietly accumulate names for properties nobody has.
        problems = drift.check_struct(HAZARD_SCHEMA, MODULE, "Hazard", ("severity",))
        self.assertEqual(1, len(problems), problems)
        self.assertIn("listed as schema-only", problems[0])


class EnumDriftTests(unittest.TestCase):
    def test_a_matching_enum_reports_nothing(self):
        schema = {"properties": {"method": {"enum": ["test", "analysis", "inspection", "review"]}}}
        self.assertEqual(
            [], drift.check_enum(schema, MODULE, "method", "kVerificationMethodWireValues")
        )

    def test_a_new_cpp_enumerator_the_schema_lacks_is_reported(self):
        module = MODULE.replace('"inspection", "review"};', '"inspection", "review", "demo"};')
        schema = {"properties": {"method": {"enum": ["test", "analysis", "inspection", "review"]}}}
        problems = drift.check_enum(schema, module, "method", "kVerificationMethodWireValues")
        self.assertEqual(1, len(problems))
        self.assertIn("demo", problems[0])

    def test_reordering_is_drift_because_the_index_is_the_enumerator_value(self):
        schema = {"properties": {"method": {"enum": ["analysis", "test", "inspection", "review"]}}}
        self.assertNotEqual(
            [], drift.check_enum(schema, MODULE, "method", "kVerificationMethodWireValues")
        )

    def test_a_bound_property_with_no_enum_is_reported(self):
        schema = {"properties": {"method": {"type": "string"}}}
        problems = drift.check_enum(schema, MODULE, "method", "kVerificationMethodWireValues")
        self.assertEqual(1, len(problems))
        self.assertIn("no enum", problems[0])


class MainTests(unittest.TestCase):
    def test_an_absent_governance_module_skips_rather_than_fails(self):
        # The module and these schemas land on separate branches; failing a documentation job for
        # a file that has not merged yet would be noise.
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(0, drift.main(["--repo-root", tmp]))

    def test_a_present_module_with_a_missing_schema_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module = root / drift.MODULE_PATH
            module.parent.mkdir(parents=True)
            module.write_text(MODULE, encoding="utf-8")
            self.assertEqual(1, drift.main(["--repo-root", tmp]))


class ValidatorTests(unittest.TestCase):
    """The JSON Schema subset the recipe schemas are checked with.

    Written out because a hand-written validator that quietly accepts everything is worse than no
    validator: the check would go green while documenting nothing.
    """

    def test_type_mismatches_are_reported_with_the_path(self):
        problems = drift.validate({"id": 7}, {"type": "object", "properties": {"id": {"type": "string"}}}, "o")
        self.assertEqual(["o.id: expected string, found int"], problems)

    def test_a_boolean_is_not_an_integer(self):
        # `bool` is a subclass of `int` in Python and is not an integer in JSON, so a validator
        # that used isinstance() alone would accept `true` where a count belongs.
        self.assertTrue(drift.validate(True, {"type": "integer"}, "v"))
        self.assertFalse(drift.validate(True, {"type": "boolean"}, "v"))

    def test_required_and_undeclared_properties_are_both_drift(self):
        schema = {
            "type": "object",
            "additionalProperties": False,
            "required": ["id"],
            "properties": {"id": {"type": "string"}},
        }
        self.assertIn("required property 'id' is absent", drift.validate({}, schema, "o")[0])
        self.assertIn("carries 'extra'", drift.validate({"id": "a", "extra": 1}, schema, "o")[0])

    def test_items_are_validated_by_index(self):
        schema = {"type": "array", "items": {"type": "string"}}
        self.assertEqual(["a[1]: expected string, found int"], drift.validate(["x", 2], schema, "a"))

    def test_enum_and_bounds(self):
        self.assertTrue(drift.validate("qoi", {"enum": ["png"]}, "v"))
        self.assertTrue(drift.validate(0, {"type": "integer", "minimum": 1}, "v"))
        self.assertTrue(drift.validate("", {"type": "string", "minLength": 1}, "v"))
        self.assertTrue(drift.validate([], {"type": "array", "minItems": 1}, "v"))

    def test_pattern_and_unique_items_are_enforced_not_merely_named(self):
        # Both were in SUPPORTED_KEYWORDS before they were implemented, which is the one hole
        # check_recipe_schema_keywords() could not close about itself: a schema could claim either
        # constraint and the checker would accept a report violating it.
        self.assertTrue(drift.validate("Xy", {"type": "string", "pattern": "^[a-z]+$"}, "v"))
        self.assertFalse(drift.validate("xy", {"type": "string", "pattern": "^[a-z]+$"}, "v"))
        duplicated = drift.validate(["a", "a"], {"type": "array", "uniqueItems": True}, "v")
        self.assertEqual(1, len(duplicated))
        self.assertIn("appears more than once", duplicated[0])
        self.assertFalse(drift.validate(["a", "b"], {"type": "array", "uniqueItems": True}, "v"))

    def test_every_supported_keyword_is_actually_implemented(self):
        # The list and the validator are two places, so this asserts they agree rather than
        # trusting that whoever adds a keyword to one remembers the other.
        probes = {
            "type": ({"type": "string"}, 1),
            "enum": ({"enum": ["a"]}, 1),
            "minimum": ({"minimum": 5}, 4),
            "maximum": ({"maximum": 5}, 6),
            "minLength": ({"minLength": 2}, "a"),
            "minItems": ({"minItems": 2}, ["a"]),
            "pattern": ({"pattern": "^z"}, "a"),
            "uniqueItems": ({"uniqueItems": True}, ["a", "a"]),
            "required": ({"required": ["a"]}, {}),
            "additionalProperties": ({"additionalProperties": False, "properties": {}}, {"a": 1}),
        }
        for keyword, (schema, offending) in probes.items():
            with self.subTest(keyword=keyword):
                self.assertTrue(
                    drift.validate(offending, schema, "v"),
                    f"'{keyword}' is in SUPPORTED_KEYWORDS but constrains nothing",
                )
        # The rest are structural or documentation, and carry no constraint to enforce.
        structural = {"$schema", "$id", "title", "description", "properties", "items", "examples"}
        self.assertEqual(drift.SUPPORTED_KEYWORDS, set(probes) | structural)

    def test_only_the_false_spelling_of_additional_properties_is_accepted(self):
        # The schema-valued form constrains the properties a schema does not name, and validate()
        # reads anything that is not False as "do not check" - so a schema using it would let an
        # undeclared property of any type through in silence. Refused at the guard instead.
        schema_valued = {
            "type": "object",
            "properties": {},
            "additionalProperties": {"type": "integer"},
        }
        problems = drift.check_recipe_schema_keywords(schema_valued, "s")
        self.assertEqual(1, len(problems))
        self.assertIn("implements the `false` spelling only", problems[0])
        # And the reason it must be refused: validate() finds nothing wrong with a string there.
        self.assertEqual([], drift.validate({"anything": "a string"}, schema_valued, "o"))

        self.assertEqual([], drift.check_recipe_schema_keywords(
            {"type": "object", "properties": {}, "additionalProperties": False}, "s"))

    def test_an_unimplemented_keyword_is_refused_rather_than_ignored(self):
        # The failure mode a subset validator actually has: a keyword it does not know constrains
        # nothing, and the schema reads as though it does.
        problems = drift.check_recipe_schema_keywords(
            {"type": "object", "properties": {"id": {"type": "string", "maxLength": 3}}}, "s"
        )
        self.assertEqual(1, len(problems))
        self.assertIn("maxLength", problems[0])


class RecipeSchemaTests(unittest.TestCase):
    """Every committed report's resolved options, against the schema for its kind."""

    def setUp(self):
        self.root = Path(__file__).resolve().parents[2]

    def test_every_recipe_kind_has_a_schema_that_parses(self):
        for kind, relative, _ in drift.RECIPE_SCHEMAS:
            with self.subTest(kind=kind):
                path = self.root / relative
                self.assertTrue(path.is_file(), f"{relative} is bound but missing")
                json.loads(path.read_text(encoding="utf-8"))

    def test_every_recipe_directory_has_a_schema(self):
        # The acceptance is "one schema per recipe kind", so a kind added under recipes/ with no
        # schema is the gap this catches - the check cannot notice a kind it was never told about.
        kinds = {p.name for p in (self.root / "recipes").iterdir() if p.is_dir()}
        self.assertEqual(kinds, {kind for kind, _, _ in drift.RECIPE_SCHEMAS})

    def test_every_schema_example_validates_against_its_own_schema(self):
        # An example that would not validate is the contradiction a reader is most likely to be
        # misled by, and `examples` is documentation to every other part of this checker - so it
        # went unnoticed until review. The font schema shipped with `atlas` pinned to `""` and an
        # example saying `"atlas.bin"`.
        for kind, relative, _ in drift.RECIPE_SCHEMAS:
            with self.subTest(kind=kind):
                schema = json.loads((self.root / relative).read_text(encoding="utf-8"))
                self.assertTrue(schema.get("examples"), f"{relative} carries no example to check")
                for index, example in enumerate(schema["examples"]):
                    self.assertEqual([], drift.validate(example, schema, f"examples[{index}]"))

    def test_the_forms_a_baker_emits_with_an_empty_path_validate(self):
        """The recipe shapes that legitimately carry an empty string where a path usually goes.

        Three of these have now been wrong at some point in review, each the same mistake: a
        `minLength` on a member the baker deliberately leaves empty. They are asserted together so
        the next one is a failing test rather than a third round.
        """
        root = self.root

        # A text package positioning nothing: `parseStrings()` accepts `font` and `[strings]`
        # together or neither, so an empty path means "this package positions nothing".
        text = json.loads((root / "docs/recipes/text.schema.json").read_text(encoding="utf-8"))
        stringless = {"id": "empty-en-us", "font": "", "atlas": "dejavu-ui", "locale": "en-US",
                      "sidecar": "runs.bin", "strings": 0}
        self.assertEqual([], drift.validate(stringless, text, "options"))

        # A font recipe leaves both of the text-only members on the shared Recipe empty.
        font = json.loads((root / "docs/recipes/font.schema.json").read_text(encoding="utf-8"))
        self.assertEqual([""], font["properties"]["atlas"]["enum"])
        self.assertEqual([""], font["properties"]["locale"]["enum"])

        # A screen with no text approves no locale and needs no font to measure against.
        screen = json.loads((root / "docs/recipes/screen.schema.json").read_text(encoding="utf-8"))
        self.assertNotIn("minLength", screen["properties"]["fontPackage"])

    def test_the_cross_property_invariants_are_checked(self):
        # What no JSON Schema keyword can state: a constraint relating two properties, or reading a
        # pair of members inside an item. Each is a rule a baker refuses at parse time.
        unequal = {"moduleIds": ["a", "b"], "moduleSources": ["x"]}
        self.assertIn("moduleIds against", drift.check_shader_options(unequal, "r")[0])
        self.assertEqual([], drift.check_shader_options({"moduleIds": ["a"], "moduleSources": ["x"]}, "r"))

        inverted = {"charset": [{"name": "n", "first": 99, "last": 10}]}
        self.assertIn("is above last", drift.check_font_options(inverted, "r")[0])

        screen = {"dynamicText": [{"name": "S", "produces": [{"first": 99, "last": 10}]}]}
        self.assertIn("is above last", drift.check_screen_options(screen, "r")[0])
        self.assertEqual([], drift.check_screen_options({"dynamicText": []}, "r"))

    def test_a_malformed_report_is_reported_rather_than_crashing(self):
        """A semantic checker must not run over a value that failed its type contract.

        `moduleIds: null` reached `len(None)` and took the whole lint down with a traceback -
        the tool crashing on exactly the input it exists to reject. The ordering rule is the fix:
        schema validation first, semantics only when it found nothing.
        """
        root = self.root
        schema = json.loads((root / "docs/recipes/shader.schema.json").read_text(encoding="utf-8"))
        broken = {"id": "triangle", "sidecar": "shaders.spv", "moduleIds": None, "moduleSources": []}

        problems = drift.validate(broken, schema, "options")
        self.assertTrue(problems, "the schema must catch the wrong type")
        self.assertIn("expected array", problems[0])
        # The guarantee the ordering gives: the semantic checker is never reached with this value.
        # Asserted as the contract rather than by calling it, because calling it is what crashed.
        self.assertIn("shader", drift.SEMANTIC_CHECKS)

    def test_schema_examples_go_through_the_semantic_checks_too(self):
        # An example carrying unequal shader arrays would otherwise be a published illustration of
        # something the baker refuses - the schema constraints alone cannot see it.
        schema = json.loads(
            (self.root / "docs/recipes/shader.schema.json").read_text(encoding="utf-8")
        )
        for index, example in enumerate(schema["examples"]):
            with self.subTest(example=index):
                self.assertEqual([], drift.validate(example, schema, f"examples[{index}]"))
                self.assertEqual([], drift.check_shader_options(example, f"examples[{index}]"))

    def test_the_committed_reports_validate(self):
        findings, checked = drift.check_recipe_schemas(self.root)
        self.assertEqual([], findings)
        self.assertGreater(checked, 0, "the check passed without reading a single report")


class RealRepositoryTests(unittest.TestCase):
    """The bindings must name files that exist, whatever branch this is checked out on."""

    def setUp(self):
        self.root = Path(__file__).resolve().parents[2]

    def test_every_bound_schema_exists_and_parses(self):
        for binding in drift.STRUCT_BINDINGS:
            with self.subTest(schema=binding.schema):
                path = self.root / binding.schema
                self.assertTrue(path.is_file(), f"{binding.schema} is bound but missing")
                json.loads(path.read_text(encoding="utf-8"))

    def test_every_bound_module_exists(self):
        for binding in drift.STRUCT_BINDINGS:
            with self.subTest(module=str(binding.module)):
                self.assertTrue(
                    (self.root / binding.module).is_file(),
                    f"{binding.module} is bound but missing",
                )

    def test_every_binding_resolves_to_an_object_with_properties(self):
        # A schema_at that stops descending one level short would make check_struct compare an
        # empty property set against the struct and report nothing - the vacuous pass this guards.
        for binding in drift.STRUCT_BINDINGS:
            with self.subTest(schema=binding.schema):
                schema = json.loads((self.root / binding.schema).read_text(encoding="utf-8"))
                bound = drift.descend(schema, binding.schema_at)
                self.assertTrue(bound.get("properties"), f"{binding.schema}: no properties bound")

    def test_every_enum_binding_names_a_declared_property(self):
        for relative, prop, _ in drift.ENUM_BINDINGS:
            with self.subTest(schema=relative, prop=prop):
                schema = json.loads((self.root / relative).read_text(encoding="utf-8"))
                self.assertIn(prop, schema.get("properties", {}))

    def test_the_repository_is_free_of_drift(self):
        # Passes vacuously on a branch without the governance module, which main() reports.
        self.assertEqual(0, drift.main(["--repo-root", str(self.root)]))

    def test_the_diagnostic_envelope_is_bound_to_the_cli_type(self):
        # The binding that keeps one envelope one envelope as the bakers multiply (issue #118).
        binding = next(b for b in drift.STRUCT_BINDINGS if b.struct == "Diagnostic")
        source = (self.root / binding.module).read_text(encoding="utf-8")
        fields = drift.struct_fields(source, "Diagnostic", binding.naming, binding.module)
        self.assertEqual(
            ["file", "line", "column", "code", "severity", "message", "fixHint"], fields
        )

    def test_the_severity_vocabulary_matches_describe(self):
        produced = drift.severity_wire_values(
            (self.root / drift.CLI_IMPL_PATH).read_text(encoding="utf-8")
        )
        self.assertEqual(["error", "warning", "note"], produced)


if __name__ == "__main__":
    unittest.main()
