#!/usr/bin/env python3
"""mdux-evidence-lint: bans decimal float formatting in the evidence pipeline.

Host-only tool (see ADR-004's trust-zone model) - never built into MduXCore or MduX, no
third-party dependencies, standard library only, so it runs anywhere Python 3.9+ runs without
a build.

## Why this exists

ADR-007 encodes every real number in an evidence artifact as its `u32` bit pattern,
`{"bits": 1065353216}`, never as decimal text. The reason is narrow and load-bearing:
`printf("%.9g")` and `std::format("{}", f)` are not guaranteed byte-identical across MSVC,
glibc and libc++, and the evidence pipeline crosses all three. Byte-identity across toolchains
is the pipeline's central guarantee, and a single stray float specifier in a single baker
breaks it - in a way that reproduces only on someone else's operating system, which is the
worst possible failure mode to debug.

Code review cannot be relied on to catch a `%g` buried in a diagnostic string. This can.

## What it checks

Over C++ sources under the scanned roots, **inside string literals only**:

1. printf-family float conversions: `%f`, `%e`, `%g`, `%a` and their uppercase forms, with any
   flags, width or precision in between.
2. `std::format`/`std::print` replacement fields with a float presentation type: `{:.3f}`,
   `{:e}`, `{:g}`, `{:a}` and uppercase equivalents.

Scanning only string literals is what makes this precise. A format specifier can only ever
appear in one, and the alternative - scanning whole lines - produces false positives that are
not fixable by rewording: `Mode mode{Mode::Bake};` looks exactly like a `{:...e}` field, and
`a %factor` looks exactly like `%f`. Restricting to literals also means prose *explaining* this
rule, including the `%.9g` in ADR-007 and in the Json module's own documentation, is never
scanned at all.

One acknowledged blind spot: a specifier split across adjacent concatenated literals, as in
`"%" "f"`, is not detected. Nobody writes that, and detecting it would mean modelling
concatenation for no practical gain.

## Escape hatch

A line ending with `mdux-evidence-lint:allow` is exempt. Intended for the rare case of a
host-only tool rendering a float for human display, where the output is not part of any
artifact. It is reviewed, not routine - a baker never needs it.

Usage:
    python3 tools/evidence-lint/mdux_evidence_lint.py [--format text|json] [paths...]

Exit status 0 if no findings, 1 otherwise. With no paths, scans the default roots below.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_SCAN_DIRS = (
    "src/evidence",
    "include/mdux/evidence",
    "tools",
)

SOURCE_SUFFIXES = (".cpp", ".cppm", ".hpp", ".h", ".ixx", ".cc", ".cxx")

# printf-family float conversion. `%%` is an escaped percent and must not match, so a percent
# preceded by another percent is excluded via the leading (?<!%) guard.
PRINTF_FLOAT_RE = re.compile(r"(?<!%)%[-+ #0']*[0-9*]*(?:\.[0-9*]+)?[aAeEfFgG]")

# std::format replacement field with a float presentation type. Matches "{:", an optional
# fill/align/sign/width/precision run that contains no braces, then a float type before "}".
FORMAT_FLOAT_RE = re.compile(r"\{[^{}]*:[^{}]*[aAeEfFgG]\}")

SUPPRESS_MARKER = "mdux-evidence-lint:allow"


@dataclass
class Finding:
    """One finding in the shared envelope - docs/governance/schemas/diagnostic.schema.json.

    Deliberately the same field names and severity vocabulary as `mdux::tools::cli::Diagnostic`
    (tools/common/Cli.cppm) and mdux-docs-lint, so an agent parses one schema for the whole
    repository. `line` and `column` are both 1-based, with 0 meaning "no precise position on this
    axis". This lint reports lines only: a banned construct is located by
    `extract_string_literals`, which yields the literal's start line without its start column, so
    reporting a column here would mean inventing one.
    """

    path: str
    line: int
    code: str
    severity: str
    message: str
    fix_hint: str = ""
    column: int = 0

    def to_json(self) -> dict:
        return {
            "file": self.path,
            "line": self.line,
            "column": self.column,
            "code": self.code,
            "severity": self.severity,
            "message": self.message,
            "fixHint": self.fix_hint,
        }


def extract_string_literals(text: str) -> list[tuple[int, str]]:
    """Returns (1-based start line, literal body) for every string and character literal.

    Comments are skipped entirely - a `%.9g` in a comment is documentation, not a format string.
    Raw string literals are returned with their body intact and no escape processing, which is
    what `R"(%.3f)"` needs. A literal that spans lines is reported at its first line.
    """
    literals: list[tuple[int, str]] = []
    i = 0
    n = len(text)
    line = 1
    while i < n:
        c = text[i]

        if text[i : i + 2] == "//":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text[i : i + 2] == "/*":
            i += 2
            while i < n and text[i : i + 2] != "*/":
                if text[i] == "\n":
                    line += 1
                i += 1
            i += 2
            continue

        # Raw string literal: R"delim(body)delim", no escape processing inside.
        if c == "R" and text[i + 1 : i + 2] == '"':
            open_paren = text.find("(", i + 2)
            if open_paren != -1:
                delim = text[i + 2 : open_paren]
                terminator = ")" + delim + '"'
                end = text.find(terminator, open_paren + 1)
                if end != -1:
                    body = text[open_paren + 1 : end]
                    literals.append((line, body))
                    line += text.count("\n", i, end + len(terminator))
                    i = end + len(terminator)
                    continue

        if c in ('"', "'"):
            quote = c
            start_line = line
            i += 1
            body_start = i
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    if text[i + 1] == "\n":
                        line += 1
                    i += 2
                    continue
                if text[i] == quote:
                    break
                if text[i] == "\n":  # unterminated literal; let the compiler complain about it
                    line += 1
                    break
                i += 1
            literals.append((start_line, text[body_start:i]))
            i += 1
            continue

        if c == "\n":
            line += 1
        i += 1
    return literals


def check_file(path: Path, root: Path, findings: list[Finding]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        findings.append(
            Finding(
                path=str(path.relative_to(root)),
                line=0,
                code="EVL000",
                severity="error",
                message=f"could not read file: {exc}",
            )
        )
        return

    original_lines = text.splitlines()

    for number, literal in extract_string_literals(text):
        original = original_lines[number - 1] if number <= len(original_lines) else ""
        if original.rstrip().endswith(SUPPRESS_MARKER):
            continue

        relative = str(path.relative_to(root))
        for match in PRINTF_FLOAT_RE.finditer(literal):
            findings.append(
                Finding(
                    path=relative,
                    line=number,
                    code="EVL001",
                    severity="error",
                    message=(
                        f"printf-family float conversion {match.group(0)!r} in the evidence "
                        "pipeline. Decimal float text is not byte-identical across MSVC, glibc "
                        "and libc++, which breaks the cross-toolchain byte-identity guarantee."
                    ),
                    fix_hint=(
                        "Encode the value as its u32 bit pattern via "
                        "mdux::evidence::json::Value::float32(), which writes {\"bits\": N}. "
                        "See ADR-007."
                    ),
                )
            )
        for match in FORMAT_FLOAT_RE.finditer(literal):
            findings.append(
                Finding(
                    path=relative,
                    line=number,
                    code="EVL002",
                    severity="error",
                    message=(
                        f"std::format float presentation type {match.group(0)!r} in the "
                        "evidence pipeline. std::format's float output is not guaranteed "
                        "byte-identical across standard libraries."
                    ),
                    fix_hint=(
                        "Encode the value as its u32 bit pattern via "
                        "mdux::evidence::json::Value::float32(). See ADR-007."
                    ),
                )
            )


def find_repository_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in here.parents:
        if (candidate / ".git").exists():
            return candidate
    return here.parents[2]


def collect_sources(paths: list[Path]) -> list[Path]:
    sources: list[Path] = []
    for path in paths:
        if path.is_file():
            if path.suffix in SOURCE_SUFFIXES:
                sources.append(path)
        elif path.is_dir():
            for suffix in SOURCE_SUFFIXES:
                sources.extend(path.rglob(f"*{suffix}"))
    return sorted(set(sources))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Ban decimal float formatting in the MduX evidence pipeline (ADR-007)."
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args(argv)

    root = find_repository_root()
    if args.paths:
        targets = [p if p.is_absolute() else (Path.cwd() / p) for p in args.paths]
    else:
        targets = [root / d for d in DEFAULT_SCAN_DIRS]
    targets = [t for t in targets if t.exists()]

    findings: list[Finding] = []
    sources = collect_sources(targets)
    for source in sources:
        check_file(source, root, findings)

    if args.format == "json":
        # The shared envelope: docs/governance/schemas/diagnostic.schema.json.
        print(
            json.dumps(
                {
                    "tool": "mdux-evidence-lint",
                    "filesChecked": len(sources),
                    "findings": [f.to_json() for f in findings],
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        for finding in findings:
            location = f"{finding.path}:{finding.line}" + (
                f":{finding.column}" if finding.column else ""
            )
            print(f"{location}: {finding.severity}: [{finding.code}] {finding.message}")
            if finding.fix_hint:
                print(f"    fix: {finding.fix_hint}")
        if findings:
            print(f"mdux-evidence-lint: {len(findings)} finding(s) in {len(sources)} file(s)")
        else:
            print(f"mdux-evidence-lint: OK ({len(sources)} files checked, 0 findings)")

    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
