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
        for relative, _, _ in drift.STRUCT_BINDINGS:
            with self.subTest(schema=relative):
                path = self.root / relative
                self.assertTrue(path.is_file(), f"{relative} is bound but missing")
                json.loads(path.read_text(encoding="utf-8"))

    def test_every_enum_binding_names_a_declared_property(self):
        for relative, prop, _ in drift.ENUM_BINDINGS:
            with self.subTest(schema=relative, prop=prop):
                schema = json.loads((self.root / relative).read_text(encoding="utf-8"))
                self.assertIn(prop, schema.get("properties", {}))

    def test_the_repository_is_free_of_drift(self):
        # Passes vacuously on a branch without the governance module, which main() reports.
        self.assertEqual(0, drift.main(["--repo-root", str(self.root)]))


if __name__ == "__main__":
    unittest.main()
