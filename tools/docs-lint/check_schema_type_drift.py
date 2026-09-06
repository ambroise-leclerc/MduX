#!/usr/bin/env python3
"""Fails when a JSON Schema and the C++ governance type it documents have stopped agreeing.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

Issue #33 puts it plainly - the alignment between `docs/<standard>/schemas/*.json` and the types
in `include/mdux/governance/Governance.cppm` is the point of those schemas, not the files. A schema
that silently diverges from the type it documents is worse than no schema: it is a document that
looks authoritative and is wrong, and a manufacturer reading it has no way to tell.

## What this checks, and what it deliberately does not

It checks the things that can be checked without a C++ compiler and that actually break traceability
when they drift:

- **member names**: every property the schema declares exists as a field on the type, under the
  snake_case spelling of the C++ camelCase name, and every field of the type is declared by the
  schema. A member on one side only is drift in either direction - a schema documenting a field
  that no longer exists, or a field a record cannot carry.
- **required sets**: a member the type validates as non-empty is `required` in the schema.
- **closed vocabularies**: a schema `enum` matches the corresponding `k*WireValues` array, in
  order. This is the check that catches a new `VerificationMethod` enumerator whose wire spelling
  nobody added to the schema.

It does not check types, patterns, or semantics. Establishing that a `minLength: 1` and a C++
`if (x.empty())` mean the same thing needs a compiler and a specification, not a regex over a
module interface. This tool is a tripwire for the drift that happens in practice - somebody adds a
field to one side - and it is honest about being only that.

## Why it parses rather than compiles

Running this as a C++ test would give stronger guarantees and would also mean the check only runs
where the project builds. A Python script parsing the module interface runs on the docs-only CI
job, in seconds, on a PR that touches no C++ at all - which is exactly the PR that introduces this
kind of drift.

Usage:
    python3 tools/docs-lint/check_schema_type_drift.py [--repo-root PATH]

Exit status 0 when the schemas and types agree, 1 otherwise. When the governance module is not
present in the tree at all, the check reports that and exits 0: the two live on separate branches
until both land, and failing a documentation job for a file that has not merged yet would be noise.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import NamedTuple

MODULE_PATH = Path("include/mdux/governance/Governance.cppm")
CLI_MODULE_PATH = Path("tools/common/Cli.cppm")
CLI_IMPL_PATH = Path("tools/common/Cli.cpp")

# The governance corpus spells wire names in snake_case; the tool diagnostic envelope spells them
# in camelCase, matching the C++ member exactly. Both are deliberate and neither is going to
# change, so a binding carries its convention rather than this tool assuming one.
SNAKE = "snake"
CAMEL = "camel"


class StructBinding(NamedTuple):
    """One schema bound to the C++ struct it documents.

    `schema_at` descends into the schema to the object that mirrors the struct, for a schema whose
    records are nested rather than at the root - the diagnostic envelope wraps its findings in an
    array, so the struct's counterpart is `properties.findings.items`, not the document itself.
    `schema_only` names properties that exist on that object with no C++ counterpart, listed
    explicitly so adding one is a decision somebody made here rather than an omission nobody
    noticed.
    """

    schema: str
    struct: str
    schema_only: tuple = ()
    module: Path = MODULE_PATH
    naming: str = SNAKE
    schema_at: tuple = ()


# The ISO 14971 and IEC 81001 records extend Hazard with evaluation and threat members that have
# no C++ counterpart: a risk evaluation is a device-level judgement the library does not make.
# Only the three shared members are checked for drift; the rest are schema-only.
_HAZARD_EXTENSION_ONLY = (
    "hazardous_situation",
    "harm",
    "owner",
    "control_option",
    "severity",
    "probability",
    "acceptability",
    "scale_ref",
    "residual_risk_note",
    "evidence_refs",
)

STRUCT_BINDINGS = (
    StructBinding("docs/governance/schemas/justification.schema.json", "Justification"),
    StructBinding("docs/iec62304/schemas/requirement.schema.json", "Requirement"),
    StructBinding("docs/iec62304/schemas/hazard.schema.json", "Hazard"),
    StructBinding("docs/iec62304/schemas/verification-case.schema.json", "VerificationCase"),
    StructBinding(
        "docs/iso14971/schemas/risk-record.schema.json", "Hazard", _HAZARD_EXTENSION_ONLY
    ),
    StructBinding(
        "docs/iec81001/schemas/security-risk-record.schema.json",
        "Hazard",
        _HAZARD_EXTENSION_ONLY + ("threat", "asset", "weakness", "attack_surface"),
    ),
    # The diagnostic envelope every tool emits (issue #118). Bound here for the same reason the
    # governance records are: the envelope is a published contract that agents key off, and the
    # drift that happens in practice is somebody adding a field to `Diagnostic` for a new baker
    # and not to the schema. Binding it before the shader, .medui and ML bakers land is the whole
    # point - the check exists so the envelope stays one envelope.
    StructBinding(
        "docs/governance/schemas/diagnostic.schema.json",
        "Diagnostic",
        module=CLI_MODULE_PATH,
        naming=CAMEL,
        schema_at=("properties", "findings", "items"),
    ),
)

# The severity vocabulary is a closed set shared by the C++ bakers and both Python lints, but it
# has no `k*WireValues` array to bind: `describe(Severity)` is a switch. Extracting its returned
# literals keeps the schema and the function that produces those strings from drifting apart.
SEVERITY_BINDING = (
    "docs/governance/schemas/diagnostic.schema.json",
    ("properties", "findings", "items", "properties", "severity"),
)
DESCRIBE_SEVERITY_RE = re.compile(
    r"std::string_view describe\(Severity[^)]*\)[^{]*\{(?P<body>.*?)^\}", re.DOTALL | re.MULTILINE
)

# The recipe schemas (issue #264), one per baked kind. Each documents the **resolved** option set -
# defaults expanded - which ADR-007 decision 4 already requires `report.json` to record, and which is
# therefore a different thing from the literal TOML a recipe file carries.
#
# ## Why these bind to the committed reports rather than to a C++ struct
#
# The bindings above parse a struct and match its fields. That works where a schema documents a
# record; it does not work here, because a baker's resolved options are a *projection* rather than a
# struct: `ShaderBake`'s `Recipe` holds `modules`, and `toOptions()` flattens it into `moduleIds` and
# `moduleSources`. Binding to the struct would leave the flattened names unchecked, and extracting
# the keys from five `toOptions()` bodies by regex would be a fragile check nobody trusts.
#
# What every baker does produce is a committed `generated/<kind>/<id>/report.json`, whose `options`
# member is exactly the resolved set - and `ctest -L evidence` byte-compares that file against a
# fresh bake on four toolchains. So a schema checked against those reports is a schema checked
# against the bakers, transitively and exactly, with no C++ parsing at all. A baker that adds an
# option and not the schema fails here, which is the drift this exists to catch.
#
# `optional` names properties no committed recipe happens to exercise. Listing one is a decision
# somebody made here rather than a gap nobody noticed - the same role `schema_only` plays above.
RECIPE_SCHEMAS = (
    ("shader", "docs/recipes/shader.schema.json", ()),
    ("font", "docs/recipes/font.schema.json", ()),
    ("text", "docs/recipes/text.schema.json", ()),
    ("image", "docs/recipes/image.schema.json", ()),
    ("model", "docs/recipes/model.schema.json", ()),
    ("screen", "docs/recipes/screen.schema.json", ()),
)

# Each entry: the schema, the property carrying a closed vocabulary, and the C++ array that
# defines it. Order matters - the C++ enumerator's numeric value is its index in that array.
ENUM_BINDINGS = (
    (
        "docs/iec62304/schemas/verification-case.schema.json",
        "method",
        "kVerificationMethodWireValues",
    ),
    (
        "docs/iec62304/schemas/safety-classification.schema.json",
        "class",
        "kSafetyClassWireValues",
    ),
)

# `std::string justificationId;` / `std::vector<std::string> evidenceRefs;` / `bool closed{false};`
FIELD_RE = re.compile(
    r"^\s{4}(?:std::)?(?:[A-Za-z_][\w:<>, ]*?)\s+(?P<name>[a-z][A-Za-z0-9]*)\s*(?:\{[^}]*\})?\s*;",
    re.MULTILINE,
)
# `inline constexpr std::array<std::string_view, 4> kVerificationMethodWireValues{ "test", ... };`
ARRAY_RE_TEMPLATE = r"{name}\s*\{{(?P<body>.*?)\}}"
STRING_LITERAL_RE = re.compile(r'"([^"]*)"')


def struct_body(module_text: str, struct_name: str, module: Path = MODULE_PATH) -> str:
    """The text between `struct <name> {` and its closing brace at column 0."""
    match = re.search(rf"^struct {re.escape(struct_name)} \{{$", module_text, re.MULTILINE)
    if not match:
        raise LookupError(f"struct {struct_name} not found in {module}")
    rest = module_text[match.end() :]
    end = re.search(r"^\};", rest, re.MULTILINE)
    if not end:
        raise LookupError(f"struct {struct_name} has no closing brace")
    return rest[: end.start()]


def camel_to_snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def wire_name(name: str, naming: str) -> str:
    """The member's spelling on the wire, per the binding's convention."""
    return camel_to_snake(name) if naming == SNAKE else name


def struct_fields(
    module_text: str, struct_name: str, naming: str = SNAKE, module: Path = MODULE_PATH
) -> list[str]:
    """The data members of a struct, as their wire names, in declaration order.

    Member functions are excluded by FIELD_RE requiring a `;` with no parameter list before it.
    """
    return [
        wire_name(m.group("name"), naming)
        for m in FIELD_RE.finditer(struct_body(module_text, struct_name, module))
    ]


def descend(schema: dict, path: tuple) -> dict:
    """The sub-schema at `path`, for a schema whose records are nested rather than at the root."""
    node = schema
    for key in path:
        if not isinstance(node, dict) or key not in node:
            raise LookupError(f"schema has no '{'.'.join(path)}'")
        node = node[key]
    return node


def severity_wire_values(impl_text: str) -> list[str]:
    """The severity spellings `describe(Severity)` returns, in case order.

    The trailing defensive `return "error";` after the switch repeats the first case, so the list
    is de-duplicated while preserving order rather than reporting a phantom fourth value.
    """
    match = DESCRIBE_SEVERITY_RE.search(impl_text)
    if not match:
        raise LookupError(f"describe(Severity) not found in {CLI_IMPL_PATH}")
    seen: list[str] = []
    for literal in STRING_LITERAL_RE.findall(match.group("body")):
        if literal not in seen:
            seen.append(literal)
    return seen


def wire_values(module_text: str, array_name: str) -> list[str]:
    match = re.search(
        ARRAY_RE_TEMPLATE.format(name=re.escape(array_name)), module_text, re.DOTALL
    )
    if not match:
        raise LookupError(f"{array_name} not found in {MODULE_PATH}")
    return STRING_LITERAL_RE.findall(match.group("body"))


def check_struct(
    schema: dict,
    module_text: str,
    struct_name: str,
    schema_only: tuple,
    naming: str = SNAKE,
    module: Path = MODULE_PATH,
) -> list[str]:
    problems: list[str] = []
    declared = set(schema.get("properties", {}))
    fields = set(struct_fields(module_text, struct_name, naming, module))
    allowed_extra = set(schema_only)

    for name in sorted(declared - fields - allowed_extra):
        problems.append(
            f"schema declares '{name}', which is not a field of {struct_name}. Either the field "
            f"was renamed or removed, or '{name}' belongs in this binding's schema-only list"
        )
    # Checked in both directions even for an extending schema: a C++ field that the extending
    # schema stopped declaring is still drift.
    for name in sorted(fields - declared):
        problems.append(
            f"{struct_name} has field '{name}', which the schema does not declare - a record "
            f"written against this schema cannot carry it"
        )
    for name in sorted(allowed_extra - declared):
        problems.append(
            f"'{name}' is listed as schema-only for {struct_name} but the schema does not "
            f"declare it; remove it from the binding"
        )
    return problems


def check_enum(schema: dict, module_text: str, prop: str, array_name: str) -> list[str]:
    declared = schema.get("properties", {}).get(prop, {}).get("enum")
    if declared is None:
        return [f"property '{prop}' has no enum, but is bound to {array_name}"]
    expected = wire_values(module_text, array_name)
    if declared != expected:
        return [
            f"property '{prop}' lists {declared}, but {array_name} defines {expected}. "
            f"Order matters: an enumerator's numeric value is its index in that array"
        ]
    return []


JSON_TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "integer": int,
    "number": (int, float),
    "boolean": bool,
    "null": type(None),
}


def validate(value, schema: dict, where: str) -> list[str]:
    """Validates `value` against the JSON Schema subset these schemas use.

    Deliberately a subset, and deliberately written here rather than taken as a dependency - the
    same reasoning ADR-007 applies to SHA-256 and canonical JSON, and ADR-009 to the test framework.
    What the recipe schemas use is `type`, `required`, `properties`, `additionalProperties`, `items`,
    `enum`, `minimum`, `minLength`, `minItems`, `pattern` and `uniqueItems`, and a validator for that
    is fifty lines. `pattern` and `uniqueItems` were in the supported set before they were
    implemented, which is exactly the hole `check_recipe_schema_keywords` exists to close and which
    it could not close about itself - a schema could claim either and the checker would accept a
    report violating it.

    A keyword a schema uses and this does not implement would be silently ignored, which is the one
    failure mode worth naming: `check_recipe_schema_keywords` covers it by refusing a schema that
    uses a keyword outside the supported set.
    """
    problems: list[str] = []

    expected = schema.get("type")
    if expected is not None:
        wanted = JSON_TYPES[expected]
        # `bool` is an `int` in Python and is not an integer in JSON.
        if isinstance(value, bool) != (expected == "boolean") or not isinstance(value, wanted):
            return [f"{where}: expected {expected}, found {type(value).__name__}"]

    if "enum" in schema and value not in schema["enum"]:
        problems.append(f"{where}: {value!r} is not one of {schema['enum']}")
    if "minimum" in schema and isinstance(value, (int, float)) and value < schema["minimum"]:
        problems.append(f"{where}: {value} is below the minimum {schema['minimum']}")
    if "minLength" in schema and isinstance(value, str) and len(value) < schema["minLength"]:
        problems.append(f"{where}: shorter than minLength {schema['minLength']}")
    if "minItems" in schema and isinstance(value, list) and len(value) < schema["minItems"]:
        problems.append(f"{where}: has {len(value)} items, minItems is {schema['minItems']}")
    if "pattern" in schema and isinstance(value, str) and not re.search(schema["pattern"], value):
        problems.append(f"{where}: {value!r} does not match {schema['pattern']}")
    if "uniqueItems" in schema and isinstance(value, list) and schema["uniqueItems"]:
        seen: list = []
        for item in value:
            if item in seen:
                problems.append(f"{where}: {item!r} appears more than once, and uniqueItems is set")
                break
            seen.append(item)

    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name not in value:
                problems.append(f"{where}: required property '{name}' is absent")
        if schema.get("additionalProperties") is False:
            for name in sorted(set(value) - set(properties)):
                problems.append(
                    f"{where}: carries '{name}', which the schema does not declare - a baker "
                    f"records an option this document does not describe"
                )
        for name, child in sorted(value.items()):
            if name in properties:
                problems.extend(validate(child, properties[name], f"{where}.{name}"))
    elif isinstance(value, list) and "items" in schema:
        for index, child in enumerate(value):
            problems.extend(validate(child, schema["items"], f"{where}[{index}]"))

    return problems


SUPPORTED_KEYWORDS = {
    "$schema", "$id", "title", "description", "type", "required", "properties",
    "additionalProperties", "items", "enum", "minimum", "minLength", "minItems",
    "uniqueItems", "pattern", "examples",
}


def check_recipe_schema_keywords(schema: dict, where: str) -> list[str]:
    """Refuses a keyword the validator above does not implement.

    Without this the validator would ignore it in silence, and a schema would appear to constrain
    something it does not - which is the failure a hand-written subset validator actually has.
    """
    problems = []
    for key in sorted(set(schema) - SUPPORTED_KEYWORDS):
        problems.append(
            f"{where}: uses '{key}', which this checker does not implement - implement it in "
            f"validate() or remove it, rather than leaving a keyword that constrains nothing"
        )
    # `additionalProperties` is supported in one spelling only. The schema-valued form -
    # `{"type": "integer"}`, constraining the properties a schema does not name - is a different
    # keyword wearing the same name, and `validate()` reads anything that is not `False` as "do not
    # check", so a schema using it would let an undeclared property of any type through in silence.
    # Refused rather than implemented: no recipe schema needs it, and a form nothing exercises is a
    # form nothing keeps honest.
    if "additionalProperties" in schema and schema["additionalProperties"] is not False:
        problems.append(
            f"{where}: 'additionalProperties' is {schema['additionalProperties']!r}; this checker "
            f"implements the `false` spelling only. The schema-valued form constrains the "
            f"properties a schema does not name, and validate() would ignore it"
        )
    for name, child in sorted(schema.get("properties", {}).items()):
        problems.extend(check_recipe_schema_keywords(child, f"{where}.{name}"))
    if isinstance(schema.get("items"), dict):
        problems.extend(check_recipe_schema_keywords(schema["items"], f"{where}[]"))
    return problems


def check_shader_options(options: dict, where: str) -> list[str]:
    """`moduleIds` and `moduleSources` are parallel arrays, paired by index."""
    ids = options.get("moduleIds", [])
    sources = options.get("moduleSources", [])
    if len(ids) != len(sources):
        return [
            f"{where}: {len(ids)} moduleIds against {len(sources)} moduleSources. They are paired "
            f"by index - `parseRecipe()` refuses a mismatch - and no JSON Schema keyword relates "
            f"the length of one property to another's"
        ]
    return []


def check_ranges(ranges, where: str) -> list[str]:
    """A closed code-point range runs upwards."""
    problems = []
    for index, entry in enumerate(ranges):
        if not isinstance(entry, dict):
            continue
        first, last = entry.get("first"), entry.get("last")
        if isinstance(first, int) and isinstance(last, int) and first > last:
            problems.append(
                f"{where}[{index}]: first {first} is above last {last}, so the range is empty. "
                f"Both bounds are inclusive and the parsers refuse an inverted pair"
            )
    return problems


def check_font_options(options: dict, where: str) -> list[str]:
    return check_ranges(options.get("charset", []), f"{where}.charset")


def check_screen_options(options: dict, where: str) -> list[str]:
    problems = []
    for index, rule in enumerate(options.get("dynamicText", [])):
        if isinstance(rule, dict):
            problems.extend(check_ranges(rule.get("produces", []), f"{where}.dynamicText[{index}].produces"))
    return problems


# The invariants a schema cannot state. JSON Schema relates a value to its own subschema, so a
# constraint *between* two properties - or one that reads a pair of members inside an item - has
# nowhere to live in the document and would otherwise go unchecked here while the parsers enforce it.
#
# Every one below is a rule a baker already refuses at parse time (`ShaderBake.cpp` for the paired
# arrays and the duplicate id, `Compile.cpp` and `TextBake.cpp` for the ranges), so what this adds is
# not a second opinion but the same rule applied to the committed artifact.
SEMANTIC_CHECKS = {
    "shader": check_shader_options,
    "font": check_font_options,
    "screen": check_screen_options,
}


def check_recipe_schemas(root: Path) -> tuple[list[str], int]:
    """Every committed report's resolved options, against the schema for its kind."""
    findings: list[str] = []
    checked = 0

    for kind, relative, optional in RECIPE_SCHEMAS:
        path = root / relative
        if not path.is_file():
            findings.append(f"{relative}: no schema for recipe kind '{kind}'")
            continue
        schema = json.loads(path.read_text(encoding="utf-8"))
        findings.extend(check_recipe_schema_keywords(schema, relative))

        # A schema's own examples, against itself. Cheap, and it catches the one contradiction a
        # reader is most likely to be misled by: an example that would not validate. The font
        # schema shipped with exactly that - `atlas` pinned to `""` and an example saying
        # `"atlas.bin"` - and nothing noticed, because `examples` is documentation to every other
        # part of this checker.
        for index, example in enumerate(schema.get("examples", [])):
            for problem in validate(example, schema, f"{relative} examples[{index}]"):
                findings.append(problem)

        reports = sorted((root / "generated" / kind).glob("*/report.json"))
        if not reports:
            findings.append(
                f"{relative}: no committed report under generated/{kind}/ to check it against, so "
                f"this schema is documentation nothing verifies"
            )
            continue

        declared = set(schema.get("properties", {}))
        for report_path in reports:
            report = json.loads(report_path.read_text(encoding="utf-8"))
            options = report.get("options")
            shown = report_path.relative_to(root).as_posix()
            if not isinstance(options, dict):
                findings.append(f"{shown}: has no 'options' object to check")
                continue
            checked += 1
            for problem in validate(options, schema, f"{shown} options"):
                findings.append(problem)
            semantic = SEMANTIC_CHECKS.get(kind)
            if semantic is not None:
                findings.extend(semantic(options, f"{shown} options"))
            # The other direction: a property the schema declares that no report carries and that
            # nobody listed as optional is a schema describing an option no baker resolves.
            for name in sorted(declared - set(options) - set(optional)):
                findings.append(
                    f"{shown}: the schema declares '{name}', which this report does not carry. "
                    f"Either the baker stopped resolving it, or it belongs in this kind's "
                    f"optional list in RECIPE_SCHEMAS"
                )

    return findings, checked


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root (default: inferred from this script's location)",
    )
    args = parser.parse_args(argv)
    root: Path = args.repo_root

    module_file = root / MODULE_PATH
    if not module_file.is_file():
        print(
            f"mdux-schema-drift: skipped - {MODULE_PATH} is not in this tree. The governance "
            f"module and these schemas land on separate branches; this check becomes live when "
            f"both are on the same one."
        )
        return 0
    module_text = module_file.read_text(encoding="utf-8")

    findings: list[str] = []
    checked = 0
    # A binding may name a module other than the governance one; read each at most once. `None` is
    # a cached negative - a module the binding names that is not on disk - so that a missing file
    # is stat'd once rather than once per binding that points at it.
    module_texts: dict[Path, str | None] = {MODULE_PATH: module_text}

    def module_source(relative_module: Path) -> str | None:
        if relative_module not in module_texts:
            file = root / relative_module
            module_texts[relative_module] = (
                file.read_text(encoding="utf-8") if file.is_file() else None
            )
        return module_texts[relative_module]

    for binding in STRUCT_BINDINGS:
        path = root / binding.schema
        if not path.is_file():
            findings.append(f"{binding.schema}: bound to {binding.struct} but the file is missing")
            continue
        source = module_source(binding.module)
        if source is None:
            findings.append(
                f"{binding.schema}: bound to {binding.struct} in {binding.module}, which is not "
                f"in this tree"
            )
            continue
        schema = json.loads(path.read_text(encoding="utf-8"))
        try:
            bound = descend(schema, binding.schema_at)
        except LookupError as exc:
            findings.append(f"{binding.schema}: {exc}")
            continue
        checked += 1
        for problem in check_struct(
            bound, source, binding.struct, binding.schema_only, binding.naming, binding.module
        ):
            findings.append(f"{binding.schema} vs {binding.struct}: {problem}")

    for relative, prop, array_name in ENUM_BINDINGS:
        path = root / relative
        if not path.is_file():
            findings.append(f"{relative}: bound to {array_name} but the file is missing")
            continue
        schema = json.loads(path.read_text(encoding="utf-8"))
        for problem in check_enum(schema, module_text, prop, array_name):
            findings.append(f"{relative}: {problem}")

    severity_schema_path, severity_at = SEVERITY_BINDING
    impl_file = root / CLI_IMPL_PATH
    schema_file = root / severity_schema_path
    if impl_file.is_file() and schema_file.is_file():
        schema = json.loads(schema_file.read_text(encoding="utf-8"))
        try:
            declared = descend(schema, severity_at).get("enum")
            produced = severity_wire_values(impl_file.read_text(encoding="utf-8"))
        except LookupError as exc:
            findings.append(f"{severity_schema_path}: {exc}")
        else:
            if declared != produced:
                findings.append(
                    f"{severity_schema_path}: severity lists {declared}, but describe(Severity) "
                    f"in {CLI_IMPL_PATH} returns {produced}. These are the strings the envelope "
                    f"actually carries, so the schema is the one that is wrong"
                )

    recipe_findings, recipe_reports = check_recipe_schemas(root)
    findings.extend(recipe_findings)

    if findings:
        for finding in findings:
            print(f"mdux-schema-drift: {finding}", file=sys.stderr)
        print(f"mdux-schema-drift: {len(findings)} finding(s)", file=sys.stderr)
        return 1

    print(
        f"mdux-schema-drift: OK ({checked} schemas checked against "
        f"{len(STRUCT_BINDINGS)} bindings, {len(ENUM_BINDINGS)} closed vocabularies, "
        f"{len(RECIPE_SCHEMAS)} recipe schemas against {recipe_reports} committed reports)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
