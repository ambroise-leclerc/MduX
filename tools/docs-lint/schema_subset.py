"""A validator for the subset of JSON Schema Draft 2020-12 this repository's schemas actually use.

Host-only (ADR-004): standard library only. `jsonschema` is not a dependency of this repository
and a zero-SOUP project does not add one to validate a handful of generated files - so this walks
the keywords the schemas use and nothing more.

Supported: `type` (object/array only, as a dispatch), `required`, `enum`, `pattern`, `minLength`,
`minItems`, `uniqueItems`, `additionalProperties: false`, `minimum`, and `properties`/`items`
recursion. Anything else in a schema is ignored rather than half-checked, because a validator that
silently approximates a keyword is worse than one that visibly does not implement it.

Validating against the schema *files* rather than against a copy of their rules is the point: the
two cannot drift, which is the same reason check_schema_type_drift.py parses the module interface
instead of restating its fields.
"""
from __future__ import annotations

import re


def violations(value, schema: dict, path: str = "$") -> list[str]:
    """Every way `value` fails `schema`, as human-readable messages. Empty means valid."""
    problems: list[str] = []

    if "enum" in schema and value not in schema["enum"]:
        problems.append(f"{path}: {value!r} is not one of {schema['enum']}")
    if "pattern" in schema and isinstance(value, str):
        if not re.search(schema["pattern"], value):
            problems.append(f"{path}: {value!r} does not match {schema['pattern']}")
    if "minLength" in schema and isinstance(value, str):
        if len(value) < schema["minLength"]:
            problems.append(f"{path}: shorter than minLength {schema['minLength']}")
    # `bool` is a subclass of `int`; a JSON boolean is not an integer and must not pass a
    # `minimum` check by accident.
    if "minimum" in schema and isinstance(value, int) and not isinstance(value, bool):
        if value < schema["minimum"]:
            problems.append(f"{path}: {value} is below minimum {schema['minimum']}")

    if schema.get("type") == "object":
        if not isinstance(value, dict):
            return problems + [f"{path}: expected an object, got {type(value).__name__}"]
        for key in schema.get("required", []):
            if key not in value:
                problems.append(f"{path}: missing required '{key}'")
        if schema.get("additionalProperties") is False:
            for key in value:
                if key not in schema.get("properties", {}):
                    problems.append(f"{path}: unexpected property '{key}'")
        for key, subschema in schema.get("properties", {}).items():
            if key in value:
                problems.extend(violations(value[key], subschema, f"{path}.{key}"))

    if schema.get("type") == "array":
        if not isinstance(value, list):
            return problems + [f"{path}: expected an array, got {type(value).__name__}"]
        if len(value) < schema.get("minItems", 0):
            problems.append(f"{path}: fewer than minItems {schema['minItems']}")
        if schema.get("uniqueItems") and len(value) != len(set(map(str, value))):
            problems.append(f"{path}: duplicate items")
        for index, item in enumerate(value):
            problems.extend(violations(item, schema.get("items", {}), f"{path}[{index}]"))

    return problems
