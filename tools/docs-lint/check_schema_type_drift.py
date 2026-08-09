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

    if findings:
        for finding in findings:
            print(f"mdux-schema-drift: {finding}", file=sys.stderr)
        print(f"mdux-schema-drift: {len(findings)} finding(s)", file=sys.stderr)
        return 1

    print(
        f"mdux-schema-drift: OK ({checked} schemas checked against "
        f"{len(STRUCT_BINDINGS)} bindings, {len(ENUM_BINDINGS)} closed vocabularies)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
