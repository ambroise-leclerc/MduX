"""A validator for the subset of JSON Schema Draft 2020-12 this repository's schemas actually use.

Host-only (ADR-004): standard library only. `jsonschema` is not a dependency of this repository
and a zero-SOUP project does not add one to validate a handful of generated files - so this walks
the keywords the schemas use and nothing more.

Supported: `type` (every JSON type, and a list of them as a union), `required`, `enum`, `pattern`,
`minLength`, `minItems`, `uniqueItems`, `additionalProperties: false`, `minimum`, and
`properties`/`items` recursion. Anything else in a schema is ignored rather than half-checked,
because a validator that silently approximates a keyword is worse than one that visibly does not
implement it.

`type` is checked for scalars and not only used to dispatch into the object/array walks. Without
that, a field declared `{"type": "integer"}` accepted the string `"42"` - the envelope's own
`line` and `column` could have been emitted as strings by a tool and still validated, which is
exactly the drift these schemas exist to catch.

Validating against the schema *files* rather than against a copy of their rules is the point: the
two cannot drift, which is the same reason check_schema_type_drift.py parses the module interface
instead of restating its fields.
"""
from __future__ import annotations

import re

# `bool` is a subclass of `int` in Python, so a JSON boolean would satisfy an `isinstance(v, int)`
# test for "integer" and "number". Every numeric predicate below excludes it explicitly.
_TYPE_PREDICATES = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "integer": lambda v: isinstance(v, int) and not isinstance(v, bool),
    "number": lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "boolean": lambda v: isinstance(v, bool),
    "null": lambda v: v is None,
}


def _json_type_name(value) -> str:
    """The JSON name for a Python value's type, so messages read in the schema's vocabulary."""
    for name, predicate in _TYPE_PREDICATES.items():
        if predicate(value):
            return name
    return type(value).__name__


def _type_problems(value, schema: dict, path: str) -> list[str]:
    """`type` as a real check. A list of names is a union, as Draft 2020-12 allows."""
    declared = schema.get("type")
    if declared is None:
        return []

    names = declared if isinstance(declared, list) else [declared]
    unknown = [name for name in names if name not in _TYPE_PREDICATES]
    if unknown:
        # Visibly unimplemented rather than silently approximated - see the module docstring.
        return [f"{path}: schema declares unsupported type(s) {unknown}"]

    if any(_TYPE_PREDICATES[name](value) for name in names):
        return []
    return [f"{path}: expected {' or '.join(names)}, got {_json_type_name(value)}"]


def violations(value, schema: dict, path: str = "$") -> list[str]:
    """Every way `value` fails `schema`, as human-readable messages. Empty means valid."""
    # Type first, and fatal for this node: `minLength` against a value that is not a string, or
    # `properties` against a value that is not an object, produce noise that buries the one
    # message the reader needs.
    type_problems = _type_problems(value, schema, path)
    if type_problems:
        return type_problems

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

    # No isinstance guard needed in either branch: _type_problems() already returned if the value
    # did not match the declared type, and these branches only run when `type` declared one.
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
                problems.extend(violations(value[key], subschema, f"{path}.{key}"))

    if schema.get("type") == "array":
        if len(value) < schema.get("minItems", 0):
            problems.append(f"{path}: fewer than minItems {schema['minItems']}")
        if schema.get("uniqueItems") and len(value) != len(set(map(str, value))):
            problems.append(f"{path}: duplicate items")
        for index, item in enumerate(value):
            problems.extend(violations(item, schema.get("items", {}), f"{path}[{index}]"))

    return problems
