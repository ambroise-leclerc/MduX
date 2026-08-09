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
