#!/usr/bin/env python3
"""mdux-governed-lint: enforces ADR-005's governed-zone source rules.

Host-only tool (see ADR-004's trust-zone model) - never built into MduXCore or MduX, no
third-party dependencies, standard library only, so it runs anywhere Python 3.9+ runs without
a build.

## Why this exists

ADR-005 states that governed code does not throw, and ADR-004 that it reaches no platform,
filesystem, time or randomness facility. Until issue #116 nothing checked either claim. The ADR
described a lint "the governed-zone source lint also rejects `throw`, `try`, and `catch`" that had
never been written, which is the worst shape a compliance claim can take: asserted in the present
tense, believed by readers, enforced by nothing.

The intended mechanism was compiling MduXCore with `-fno-exceptions -fno-rtti`. That is not
available while the governed zone uses `import std`: GCC records the language dialect in every
module BMI, and CMake synthesises one shared `std` target for the whole build, so a governed module
built with those flags cannot read the same `std` BMI the adapter and host-tools targets read. See
ADR-005's constraint section.

So the rule is enforced at two levels instead, and this is the source-level one:

- **this lint** reads the source and rejects the construct;
- **`governed.noThrow.symbolScan`** (cmake/MduXNoHeapScan.cmake) reads the emitted objects and
  rejects the symbol, catching a throw that arrives through a std facility the source never
  spells out.

Neither subsumes the other. The scan is blind to a `std::filesystem` call that never throws; this
is blind to a throw inside a header it does not read.

GOV009 additionally closes the gap ADR-004 item 1 names explicitly: denying `MduXCore` Vulkan's
include *directories* does not make a system-installed `<vulkan/vulkan.h>` unreachable, because the
compiler still finds it in a default search path. Only a source-level check does.

## What it scans

**Exactly the sources `CMakeLists.txt` lists for `MduXCore`**, parsed out of the
`target_sources(MduXCore ...)` block rather than globbed. Three reasons, all of them things a glob
gets wrong:

1. `include/mdux/**` also holds the *adapter* modules (`render/`, `vulkansc/`), which are permitted
   exceptions and would be reported as violations.
2. Generated shader C arrays land in `<binary>/mdux_generated/`, outside the source tree - a
   requirement in #116 is that they are not scanned as hand-authored governed source. Deriving from
   the source list excludes them by construction rather than by an exclusion pattern someone has to
   remember to maintain.
3. A module added to MduXCore is covered the moment it is registered, with no second edit here.
   That matters most for #15, which adds several.

If the block cannot be parsed, this exits non-zero rather than scanning nothing. A lint that
silently checks an empty list reports success identically to one that is working.

## How it matches

On the source **with comments and string literals removed**, not on raw lines. The governed tree is
heavily commented and those comments discuss the very constructs banned here - `Kernels.cppm` says
"Never `std::fma`", `Draw.cpp` says "a new command is needed", `Runtime.cppm` says "throw away
exactly the information the incident report needs". Every one of those is prose, and a line-based
matcher reports all of them. Stripping first is what makes the rule enforceable without forcing
authors to reword documentation to appease a regex.

Removal preserves line numbers, so a finding still points at the right line.

## Escape hatch

A line ending with `mdux-governed-lint:allow` is exempt. Intended for the genuinely unavoidable
case - `src/ml/Runtime.cpp` reinterprets a caller-supplied byte span as floats, which is the entire
mechanism ADR-008 chose for weights - and reviewed rather than routine. It is deliberately per-line
and visible in the diff.

Usage:
    python3 tools/governed-lint/mdux_governed_lint.py [--format text|json] [paths...]

Exit status 0 if no findings, 1 otherwise. With no paths, scans MduXCore's declared sources.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SUPPRESS_MARKER = "mdux-governed-lint:allow"

# The block in CMakeLists.txt that declares what the governed target is made of.
GOVERNED_TARGET = "MduXCore"

SOURCE_LINE_RE = re.compile(r"^\s*((?:include|src|tools|tests)/[^\s()]+\.(?:cppm|cpp))\s*$")


@dataclass(frozen=True)
class Rule:
    """One banned construct.

    `code` is stable once published: an agent keys off it, and a reworded `message` must not break
    that. See docs/governance/schemas/diagnostic.schema.json.
    """

    code: str
    pattern: re.Pattern
    message: str
    fix_hint: str


RULES = (
    Rule(
        code="GOV001",
        pattern=re.compile(r"\bthrow\b"),
        message="`throw` in governed code",
        fix_hint=(
            "Return a mdux::core::Result error instead (ADR-005). If the throwing construct is "
            "unavoidable, the module belongs in the host-tools zone - that is where "
            "mdux.text.raster went in #116."
        ),
    ),
    Rule(
        code="GOV002",
        pattern=re.compile(r"\b(?:try|catch)\b"),
        message="`try`/`catch` in governed code",
        fix_hint=(
            "Governed code has nothing to catch, because governed code does not throw (ADR-005). "
            "A module that needs to catch belongs in the host-tools zone."
        ),
    ),
    Rule(
        code="GOV003",
        pattern=re.compile(r"\.value\(\)"),
        message="`.value()` on a Result/expected, which throws when the value is absent",
        fix_hint=(
            "Branch on has_value() or operator bool, then use operator* or operator->, which do "
            "not throw. ADR-005's risk register names this construct specifically."
        ),
    ),
    Rule(
        code="GOV004",
        pattern=re.compile(r"\bnew\s+[A-Za-z_:]|\bdelete\s|\b(?:m|c|re)alloc\s*\(|\bfree\s*\("),
        message="raw owning allocation in governed code",
        fix_hint=(
            "Governed code uses caller-supplied storage or std containers, never a raw new/delete "
            "or the C allocator family. See ADR-004; mdux.ml.runtime's no-heap predict() is the "
            "pattern to follow for a hot path."
        ),
    ),
    Rule(
        code="GOV005",
        pattern=re.compile(
            r"std::filesystem|\bstd::(?:i|o)fstream\b|\bstd::(?:cout|cerr|cin|clog)\b"
            r"|\bstd::getenv\b|\bgetenv\s*\(|\bstd::system\s*\(|\bstd::(?:f|s)?printf\s*\("
        ),
        message="platform, filesystem or console facility in governed code",
        fix_hint=(
            "Governed code performs no I/O: it is meant to run on a device with no filesystem and "
            "no console (ADR-004, ADR-008 decision 2). Move the I/O to a host tool and pass the "
            "bytes in."
        ),
    ),
    Rule(
        code="GOV006",
        pattern=re.compile(
            r"std::chrono::(?:system_clock|steady_clock|high_resolution_clock)"
            r"|std::random_device|std::mt19937|\bstd::s?rand\s*\(|\bstd::time\s*\("
        ),
        message="clock or randomness in governed code, which makes output non-reproducible",
        fix_hint=(
            "A governed result must be a pure function of its inputs, or the evidence pipeline's "
            "byte-identity guarantee (ADR-007) does not hold. Pass a timestamp or seed in as data."
        ),
    ),
    Rule(
        code="GOV007",
        pattern=re.compile(r"\b(?:reinterpret_cast|const_cast)\b"),
        message="unsafe cast in governed code",
        fix_hint=(
            "Prefer std::bit_cast, std::as_bytes or a span conversion. Where the cast is genuinely "
            f"the mechanism - reading a caller-supplied blob - end the line with {SUPPRESS_MARKER} "
            "so the exception is reviewed rather than silent."
        ),
    ),
    Rule(
        code="GOV009",
        pattern=re.compile(
            r"#\s*include\s*<\s*(?:vulkan/|GLFW/|windows\.h|winsock|unistd\.h|sys/|fcntl\.h"
            r"|pthread\.h|dlfcn\.h|X11/|wayland-)",
            re.IGNORECASE,
        ),
        message="platform, graphics or OS header in governed code",
        fix_hint=(
            "ADR-004 item 1 keeps these off the include path that MduXCore is *given*, but a "
            "system-installed header is still findable in the compiler's default search paths - "
            "which is the gap this rule closes. Governed code takes handles and bytes from its "
            "caller; the adapter zone is where a platform header belongs."
        ),
    ),
    Rule(
        code="GOV008",
        pattern=re.compile(r"\bstd::fmaf?\b"),
        message="std::fma in governed code",
        fix_hint=(
            "std::fma rounds once where the scalar multiply-then-add sequence rounds twice, so it "
            "changes results against the golden vectors. Write the multiply and the add "
            "separately. See mdux.ml.kernels' header and ADR-008."
        ),
    ),
)


@dataclass
class Finding:
    """One finding in the shared envelope - docs/governance/schemas/diagnostic.schema.json.

    Deliberately the same field names and severity vocabulary as `mdux::tools::cli::Diagnostic`
    (tools/common/Cli.cppm), mdux-docs-lint and mdux-evidence-lint, so an agent parses one schema
    for the whole repository. `line` and `column` are both 1-based, with 0 meaning "no precise
    position on this axis". This lint reports both: stripping preserves offsets, so the column a
    match lands on is the column in the original file.
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


def strip_comments_and_literals(text: str) -> str:
    """Blanks out comments and string/character literals, preserving length and line breaks.

    Every removed character becomes a space, and newlines inside a removed run are kept, so an
    offset into the result is an offset into the original: line and column numbers survive.

    This is the inverse of mdux-evidence-lint's `extract_string_literals`, which keeps the literals
    and discards the code. Both exist because the two lints ask opposite questions - a format
    specifier can only be in a literal, and a `throw` can only be outside one.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]

        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue

        if two == "/*":
            end = text.find("*/", i + 2)
            end = n if end == -1 else end + 2
            for c in text[i:end]:
                out.append("\n" if c == "\n" else " ")
            i = end
            continue

        # Raw string literal: R"delim(body)delim", no escape processing inside.
        if text[i] == "R" and text[i + 1 : i + 2] == '"':
            open_paren = text.find("(", i + 2)
            if open_paren != -1:
                terminator = ")" + text[i + 2 : open_paren] + '"'
                end = text.find(terminator, open_paren + 1)
                if end != -1:
                    end += len(terminator)
                    for c in text[i:end]:
                        out.append("\n" if c == "\n" else " ")
                    i = end
                    continue

        if text[i] in ('"', "'"):
            quote = text[i]
            start = i
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                if text[i] == "\n":  # unterminated; let the compiler complain about it
                    break
                i += 1
            for c in text[start:i]:
                out.append("\n" if c == "\n" else " ")
            continue

        out.append(text[i])
        i += 1
    return "".join(out)


def governed_sources(root: Path) -> list[Path]:
    """The files CMakeLists.txt lists for MduXCore, in declaration order.

    Raises ValueError if the block cannot be found or yields nothing - see the module docstring on
    why this fails rather than returning an empty list.
    """
    cmakelists = root / "CMakeLists.txt"
    try:
        text = cmakelists.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"could not read {cmakelists}: {exc}") from exc

    marker = f"target_sources({GOVERNED_TARGET}"
    start = text.find(marker)
    if start == -1:
        raise ValueError(
            f"no `{marker}` block in CMakeLists.txt. The governed source list is derived from "
            "that block; if the target was renamed, update GOVERNED_TARGET in this file."
        )

    # Scan from the opening paren itself, not from the end of the marker: starting past it leaves
    # depth at 0, so the block's own closing paren takes it to -1, the `depth == 0` test never
    # fires, and the "block" runs to end of file - swallowing the *adapter* target's source list
    # and reporting its permitted exceptions as governed violations. Observed while writing this.
    open_paren = text.index("(", start)
    depth = 0
    end = start
    for index in range(open_paren, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                end = index
                break
    else:
        raise ValueError(f"unterminated `{marker}` block in CMakeLists.txt")

    block = strip_cmake_comments(text[start:end])
    paths = []
    for line in block.splitlines():
        match = SOURCE_LINE_RE.match(line)
        if match:
            paths.append(root / match.group(1))

    if not paths:
        raise ValueError(
            f"the `{marker}` block parsed to zero sources. Refusing to report success on an "
            "empty scan."
        )
    return paths


def strip_cmake_comments(text: str) -> str:
    """Drops `#` comments from a CMake fragment. No literals to worry about in a source list."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def check_file(path: Path, root: Path, findings: list[Finding]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        findings.append(
            Finding(
                path=str(path.relative_to(root)),
                line=0,
                code="GOV000",
                severity="error",
                message=f"could not read file: {exc}",
            )
        )
        return

    relative = str(path.relative_to(root))
    code_only = strip_comments_and_literals(text)
    original_lines = text.splitlines()

    # Offset of the first character of each 1-based line, for turning a match position into a
    # line and column without scanning the whole prefix per match.
    line_starts = [0]
    for index, character in enumerate(code_only):
        if character == "\n":
            line_starts.append(index + 1)

    def position_of(offset: int) -> tuple[int, int]:
        low, high = 0, len(line_starts) - 1
        while low < high:
            mid = (low + high + 1) // 2
            if line_starts[mid] <= offset:
                low = mid
            else:
                high = mid - 1
        return low + 1, offset - line_starts[low] + 1

    for rule in RULES:
        for match in rule.pattern.finditer(code_only):
            line, column = position_of(match.start())
            original = original_lines[line - 1] if line <= len(original_lines) else ""
            if original.rstrip().endswith(SUPPRESS_MARKER):
                continue
            findings.append(
                Finding(
                    path=relative,
                    line=line,
                    column=column,
                    code=rule.code,
                    severity="error",
                    message=f"{rule.message}: {match.group(0)!r}",
                    fix_hint=rule.fix_hint,
                )
            )

    findings.sort(key=lambda f: (f.path, f.line, f.column, f.code))


def find_repository_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in here.parents:
        if (candidate / ".git").exists():
            return candidate
    return here.parents[2]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Enforce ADR-005's governed-zone source rules over MduXCore's sources."
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="Files to scan. Defaults to MduXCore's declared sources.",
    )
    args = parser.parse_args(argv)

    root = find_repository_root()
    if args.paths:
        sources = [p if p.is_absolute() else (Path.cwd() / p) for p in args.paths]
    else:
        try:
            sources = governed_sources(root)
        except ValueError as exc:
            print(f"mdux-governed-lint: {exc}", file=sys.stderr)
            return 1

    missing = [s for s in sources if not s.is_file()]
    if missing:
        for path in missing:
            print(f"mdux-governed-lint: declared source not found: {path}", file=sys.stderr)
        return 1

    findings: list[Finding] = []
    for source in sources:
        check_file(source, root, findings)

    if args.format == "json":
        # The shared envelope: docs/governance/schemas/diagnostic.schema.json.
        print(
            json.dumps(
                {
                    "tool": "mdux-governed-lint",
                    "filesChecked": len(sources),
                    "findings": [f.to_json() for f in findings],
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        for finding in findings:
            print(
                f"{finding.path}:{finding.line}:{finding.column}: {finding.severity}: "
                f"[{finding.code}] {finding.message}"
            )
            if finding.fix_hint:
                print(f"    fix: {finding.fix_hint}")
        if findings:
            print(f"mdux-governed-lint: {len(findings)} finding(s) in {len(sources)} file(s)")
        else:
            print(f"mdux-governed-lint: OK ({len(sources)} files checked, 0 findings)")

    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
