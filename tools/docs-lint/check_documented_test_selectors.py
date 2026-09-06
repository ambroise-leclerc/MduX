#!/usr/bin/env python3
"""Fails when a documented `ctest -R <selector>` example matches no registered test.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

Issue #304's finding, made mechanical rather than left to the next reader to rediscover:
`.agents/skills/mdux-build-and-test/SKILL.md` recommended
`ctest --test-dir build -R MduXUnitTests --output-on-failure`, a name from before
`mdux_discover_tests()` replaced hand-written suite registrations with one CTest entry per
`TEST_CASE`, named `<target>::<case>`. Running the documented command against the tree it
describes returned `Total Tests: 0` - a command that reads as verification evidence and produces
none, silently, because CTest treats an empty selection as a successful run of nothing unless
`--no-tests=error` says otherwise.

This tool asks a real, built CTest configuration whether a documented selector still selects
anything - `ctest --show-only=json-v1 -R <selector>` - rather than keeping a second list of
expected names in this file for the documentation to drift away from. That second list is
exactly what issue #304 asks not to be built: the source of truth for "does this selector match a
test" is the test tree itself, on the day the check runs, not a snapshot of it copied in here.

## What is scanned, and why this set

`AGENTS.md`, every `.agents/skills/*/SKILL.md`, and `docs/getting-started.md` - the documents an
agent reads to learn *how to run tests*, which is the same set issue #304 audited by hand. Not all
of `docs/`: `check_named_mechanisms.py` already resolves `ctest -L <label>` citations there
statically (a label is a literal string attached in CMake or a `TEST_CASE`, so no build is
needed), and a changelog or roadmap entry quoting a historical command is describing what a past
release did, not instructing a reader what to run today.

Only fenced code blocks are scanned, which is the opposite convention from
`check_named_mechanisms.py` and deliberately so: that checker treats a fence as an illustrative
example exempt from checking prose claims, while a `ctest` invocation in one of these documents
*is* the claim - it is presented as the command to run, and #304 is exactly the case of a fenced
example nobody could run correctly.

## What "obsolete" means here, precisely

A selector that CTest's `--show-only=json-v1` reports zero tests for, against the build directory
given. That is the one fact this tool proves: it does not check that the prose around the command
is otherwise accurate, that the build directory itself is current, or that a selector matching one
stale test is the *right* test - see #304's own acceptance criteria for why a mechanical checker
does not stand in for reading the surrounding text.

Usage:
    python3 tools/docs-lint/check_documented_test_selectors.py --build-dir PATH [--repo-root PATH]

Exit status 0 when every documented selector matches at least one registered test, 1 otherwise -
including when `--build-dir` holds no CTest configuration to ask, which would otherwise make an
empty scan look identical to a clean one.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

FENCE_PATTERN_CHARS = ("```", "~~~")

# Mirrors CTEST_LABEL_PATTERN in check_named_mechanisms.py, for `-R`/`--tests-regex` instead of
# `-L`/`--label-regex`. The gap between `ctest` and the flag may hold options and a preset name,
# bounded so a sentence rather than a command line cannot be captured across a bracket or comma.
CTEST_SELECTOR_PATTERN = re.compile(
    r"\bctest\b(?P<gap>[^\n(),]{0,60}?)(?:-R\b|--tests-regex\b)(?:\s+|=)"
    r"(?:(?P<quote>[\"'])(?P<quoted>[^\n]*?)(?P=quote)|(?P<bare>[^\s,\])`]+))"
)


@dataclass(frozen=True)
class Citation:
    path: Path
    line: int
    selector: str


@dataclass(frozen=True)
class Finding:
    citation: Citation
    reason: str


def documents_to_scan(root: Path) -> list[Path]:
    """The fixed set of "how to run tests" documents - see the module docstring for why this set
    and not all of `docs/`. Skills are discovered by glob rather than named individually, so a
    skill added later is scanned without editing this function - the one place a hardcoded list
    would have been tempting and is not needed.
    """
    paths = []
    agents_md = root / "AGENTS.md"
    if agents_md.is_file():
        paths.append(agents_md)
    getting_started = root / "docs" / "getting-started.md"
    if getting_started.is_file():
        paths.append(getting_started)
    skills_root = root / ".agents" / "skills"
    if skills_root.is_dir():
        paths.extend(sorted(skills_root.glob("*/SKILL.md")))
    return paths


def fenced_code_lines(text: str) -> list[tuple[int, str]]:
    """Lines strictly inside a fenced code block, 1-indexed, excluding the fence delimiters
    themselves. An unterminated fence is treated as open to the end of the file - the same
    fail-open-to-scanning choice `check_named_mechanisms.py` makes for the opposite case, applied
    here so a malformed document still gets its fenced content checked rather than none of it.
    """
    lines: list[tuple[int, str]] = []
    in_fence = False
    for index, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        if stripped.startswith(FENCE_PATTERN_CHARS):
            in_fence = not in_fence
            continue
        if in_fence:
            lines.append((index, line))
    return lines


def find_citations(path: Path) -> list[Citation]:
    text = path.read_text(encoding="utf-8", errors="replace")
    citations = []
    for line_number, line in fenced_code_lines(text):
        for match in CTEST_SELECTOR_PATTERN.finditer(line):
            selector = match.group("quoted") if match.group("quoted") is not None else match.group("bare")
            citations.append(Citation(path, line_number, selector))
    return citations


def matching_test_count(ctest_command: str, build_dir: Path, selector: str) -> int:
    """How many tests `--show-only=json-v1 -R <selector>` reports against `build_dir`.

    `--show-only` applies the same `-R` filtering a real run would (checked empirically: a
    selector matching zero tests returns an empty `tests` array rather than the whole inventory),
    so this asks the identical question the documented command would answer, without running
    anything a test binary does.

    Raises `RuntimeError` for anything that is not "the selector matched N tests" - a missing
    `ctest`, a build directory with no test configuration, or output this tool does not
    understand - so a caller can tell "the selector is stale" apart from "the question could not
    be asked", which is the distinction issue #304's own acceptance criteria ask for.
    """
    try:
        completed = subprocess.run(
            [ctest_command, "--test-dir", str(build_dir), "--show-only=json-v1", "-R", selector],
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError as error:
        raise RuntimeError(f"'{ctest_command}' could not be run: {error}") from error

    if completed.returncode != 0 or not completed.stdout.strip():
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit status {completed.returncode}"
        raise RuntimeError(f"'{ctest_command} --test-dir {build_dir} --show-only=json-v1' failed: {detail}")

    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"'{ctest_command} --show-only=json-v1' did not print JSON: {error}") from error

    tests = document.get("tests")
    if not isinstance(tests, list):
        raise RuntimeError("'--show-only=json-v1' output has no 'tests' array - unexpected CTest version or format")
    return len(tests)


def check_repository(root: Path, build_dir: Path, ctest_command: str) -> tuple[list[Finding], int, int]:
    """Returns (findings, documents checked, citations checked)."""
    findings: list[Finding] = []
    citations_checked = 0
    documents = documents_to_scan(root)
    for document in documents:
        for citation in find_citations(document):
            citations_checked += 1
            try:
                count = matching_test_count(ctest_command, build_dir, citation.selector)
            except RuntimeError as error:
                findings.append(Finding(citation, str(error)))
                continue
            if count == 0:
                findings.append(
                    Finding(citation, f"'-R {citation.selector}' matches 0 registered tests in {build_dir}")
                )
    return findings, len(documents), citations_checked


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root (default: inferred from this script's location)",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        required=True,
        help="a configured and built CMake binary directory to ask CTest about",
    )
    parser.add_argument(
        "--ctest-command",
        default="ctest",
        help="the ctest executable to invoke (default: 'ctest', resolved via PATH)",
    )
    args = parser.parse_args(argv)

    root = args.repo_root.resolve()
    build_dir = args.build_dir.resolve()

    if not (build_dir / "CTestTestfile.cmake").is_file():
        print(
            f"mdux-doc-selectors: '{build_dir}' has no CTestTestfile.cmake - configure and build "
            "first, or pass --build-dir at the tree that was",
            file=sys.stderr,
        )
        return 1

    findings, documents_checked, citations_checked = check_repository(root, build_dir, args.ctest_command)

    if findings:
        for finding in findings:
            path = finding.citation.path
            relative = path.relative_to(root) if path.is_relative_to(root) else path
            print(f"mdux-doc-selectors: {relative}:{finding.citation.line}: {finding.reason}", file=sys.stderr)
        print(f"mdux-doc-selectors: {len(findings)} finding(s)", file=sys.stderr)
        print(
            "mdux-doc-selectors: update the documented selector to a name or pattern "
            "'ctest --test-dir <build> -N' actually lists",
            file=sys.stderr,
        )
        return 1

    if documents_checked == 0:
        # The same vacuous-success guard check_named_mechanisms.py applies: a scan of nothing
        # printing "OK" is indistinguishable from a scan that found everything correct.
        print(
            f"mdux-doc-selectors: no documents found under '{root}' - --repo-root is wrong or the layout moved",
            file=sys.stderr,
        )
        return 1

    print(f"mdux-doc-selectors: OK ({documents_checked} document(s), {citations_checked} selector(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
