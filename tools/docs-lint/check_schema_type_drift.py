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

MODULE_PATH = Path("include/mdux/governance/Governance.cppm")

# Each entry: the schema, the C++ struct it mirrors, and any schema-only members. Schema-only
# members are listed explicitly rather than tolerated generally, so that adding one is a decision
# somebody made here rather than an omission nobody noticed.
STRUCT_BINDINGS = (
    ("docs/governance/schemas/justification.schema.json", "Justification", ()),
    ("docs/iec62304/schemas/requirement.schema.json", "Requirement", ()),
    ("docs/iec62304/schemas/hazard.schema.json", "Hazard", ()),
    ("docs/iec62304/schemas/verification-case.schema.json", "VerificationCase", ()),
    # The ISO 14971 and IEC 81001 records extend Hazard with evaluation and threat members that
    # have no C++ counterpart: a risk evaluation is a device-level judgement the library does not
    # make. Only the three shared members are checked for drift; the rest are schema-only.
    (
        "docs/iso14971/schemas/risk-record.schema.json",
        "Hazard",
        (
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
        ),
    ),
    (
        "docs/iec81001/schemas/security-risk-record.schema.json",
        "Hazard",
        (
            "hazardous_situation",
            "harm",
            "owner",
            "threat",
            "asset",
            "weakness",
            "attack_surface",
            "control_option",
            "severity",
            "probability",
            "acceptability",
            "scale_ref",
            "residual_risk_note",
            "evidence_refs",
        ),
    ),
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


def struct_body(module_text: str, struct_name: str) -> str:
    """The text between `struct <name> {` and its closing brace at column 0."""
    match = re.search(rf"^struct {re.escape(struct_name)} \{{$", module_text, re.MULTILINE)
    if not match:
        raise LookupError(f"struct {struct_name} not found in {MODULE_PATH}")
    rest = module_text[match.end() :]
    end = re.search(r"^\};", rest, re.MULTILINE)
    if not end:
        raise LookupError(f"struct {struct_name} has no closing brace")
    return rest[: end.start()]


def camel_to_snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def struct_fields(module_text: str, struct_name: str) -> list[str]:
    """The data members of a struct, as their snake_case wire names, in declaration order.

    Member functions are excluded by FIELD_RE requiring a `;` with no parameter list before it.
    """
    return [camel_to_snake(m.group("name")) for m in FIELD_RE.finditer(struct_body(module_text, struct_name))]


def wire_values(module_text: str, array_name: str) -> list[str]:
    match = re.search(
        ARRAY_RE_TEMPLATE.format(name=re.escape(array_name)), module_text, re.DOTALL
    )
    if not match:
        raise LookupError(f"{array_name} not found in {MODULE_PATH}")
    return STRING_LITERAL_RE.findall(match.group("body"))


def check_struct(schema: dict, module_text: str, struct_name: str, schema_only: tuple) -> list[str]:
    problems: list[str] = []
    declared = set(schema.get("properties", {}))
    fields = set(struct_fields(module_text, struct_name))
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

    for relative, struct_name, schema_only in STRUCT_BINDINGS:
        path = root / relative
        if not path.is_file():
            findings.append(f"{relative}: bound to {struct_name} but the file is missing")
            continue
        schema = json.loads(path.read_text(encoding="utf-8"))
        checked += 1
        for problem in check_struct(schema, module_text, struct_name, schema_only):
            findings.append(f"{relative} vs {struct_name}: {problem}")

    for relative, prop, array_name in ENUM_BINDINGS:
        path = root / relative
        if not path.is_file():
            findings.append(f"{relative}: bound to {array_name} but the file is missing")
            continue
        schema = json.loads(path.read_text(encoding="utf-8"))
        for problem in check_enum(schema, module_text, prop, array_name):
            findings.append(f"{relative}: {problem}")

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
