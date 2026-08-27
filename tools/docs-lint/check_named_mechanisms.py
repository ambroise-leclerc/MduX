#!/usr/bin/env python3
"""Fails when documentation names a CI mechanism that the repository cannot resolve.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

This checker deliberately proves less than the prose around a mechanism may claim. It resolves
only three mechanical facts:

- a cited workflow exists under ``.github/workflows`` and has a ``push`` or ``pull_request``
  trigger;
- a label passed to ``ctest -L`` is attached to at least one test in CMake or the test sources;
- a standalone name beginning ``mdux-`` is an ``add_executable`` /
  ``add_custom_target`` target or one of the repository's Python lint tools.

It scans Markdown under ``docs/`` and full-line comments in ``.github/workflows/*.yml``. Markdown
fenced code blocks are examples, not claims about the current tree, and are skipped. A deliberately
aspirational citation outside a fence may be exempted on that line with the visible marker
``mdux-named-mechanisms:aspirational``. The marker is local on purpose: there is no suppression list
where exceptions can become detached from the prose they qualify.

Usage:
    python3 tools/docs-lint/check_named_mechanisms.py [--repo-root PATH]

Exit status 0 when every citation resolves, 1 otherwise.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ASPIRATIONAL_MARKER = "mdux-named-mechanisms:aspirational"

FENCE_PATTERN = re.compile(r"^\s*(?P<fence>`{3,}|~{3,})")
WORKFLOW_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_./-])(?P<path>(?:\.github/workflows/)?[A-Za-z0-9_.-]+\.ya?ml)\b"
)
CTEST_LABEL_PATTERN = re.compile(
    r"\bctest\b[^\n]*?(?:-L|--label-regex)(?:\s+|=)[\"']?"
    r"(?P<label>[A-Za-z0-9_]+(?:[.-][A-Za-z0-9_-]+)*)"
)
MECHANISM_CITATION_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_./-])(?P<name>mdux-[a-z0-9][a-z0-9_.-]*)(?![A-Za-z0-9_./-])"
)

CMAKE_TARGET_PATTERN = re.compile(
    r"\badd_(?:executable|custom_target)\s*\(\s*(?P<name>[A-Za-z0-9_.+-]+)", re.MULTILINE
)
CMAKE_LABEL_PATTERN = re.compile(r"\bLABELS\s+(?:\"(?P<quoted>[^\"]+)\"|(?P<bare>[A-Za-z0-9_.;,+-]+))")
STRING_PATTERN = re.compile(r'"(?P<value>(?:\\.|[^"\\])*)"')
TEST_CASE_PATTERN = re.compile(
    r'\bTEST_CASE\s*\(\s*"(?:\\.|[^"\\])*"\s*,(?P<labels>.*?)\)', re.DOTALL
)
SPEC_REGISTER_PATTERN = re.compile(
    r'\bRegister\s+[A-Za-z_][A-Za-z0-9_]*\s*\{\s*"(?:\\.|[^"\\])*"\s*,\s*'
    r'"(?P<labels>(?:\\.|[^"\\])*)"',
    re.DOTALL,
)
CPP_COMMENT_OR_QUOTED_PATTERN = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.DOTALL
)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    message: str


@dataclass(frozen=True)
class MechanismIndex:
    workflows: dict[str, Path]
    automatic_workflows: frozenset[str]
    labels: frozenset[str]
    targets: frozenset[str]


def has_automatic_trigger(text: str) -> bool:
    """Whether a workflow's top-level ``on`` mapping includes push or pull_request."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        inline = re.match(r"^on:\s*\[(?P<events>[^]]*)\]\s*(?:#.*)?$", line)
        if inline is not None:
            events = {event.strip().strip("'\"") for event in inline.group("events").split(",")}
            return bool(events.intersection({"push", "pull_request"}))

        if not re.match(r"^on:\s*(?:#.*)?$", line):
            continue
        for child in lines[index + 1 :]:
            if child and not child[0].isspace():
                break
            match = re.match(r"^\s+(push|pull_request):(?:\s|$)", child)
            if match is not None:
                return True
        return False
    return False


def cmake_files(root: Path) -> list[Path]:
    """Project CMake inputs, excluding fetched dependencies and build trees."""
    found: list[Path] = []
    for raw_directory, directories, files in os.walk(root):
        directories[:] = [
            name
            for name in directories
            if name not in {".git", "_deps", "__pycache__"} and not name.startswith("build")
        ]
        directory = Path(raw_directory)
        found.extend(
            directory / name for name in files if name == "CMakeLists.txt" or name.endswith(".cmake")
        )
    return sorted(found)


def split_labels(value: str) -> set[str]:
    return {label for label in re.split(r"[\s,;]+", value) if label and "${" not in label}


def cmake_code(text: str) -> str:
    """CMake text with full-line comments blanked so proposals are not target evidence."""
    return "\n".join("" if line.lstrip().startswith("#") else line for line in text.splitlines())


def cpp_without_comments(text: str) -> str:
    """Blank C++ comments while preserving quoted strings and source positions."""
    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        if not token.startswith("/"):
            return token
        return "".join("\n" if character == "\n" else " " for character in token)

    return CPP_COMMENT_OR_QUOTED_PATTERN.sub(replace, text)


def labels_from_test_source(text: str) -> set[str]:
    """Literal labels registered through MduXTest or the SpecLab bridge."""
    text = cpp_without_comments(text)
    labels: set[str] = set()
    for match in TEST_CASE_PATTERN.finditer(text):
        labels.update(string.group("value") for string in STRING_PATTERN.finditer(match.group("labels")))
    for match in SPEC_REGISTER_PATTERN.finditer(text):
        labels.update(split_labels(match.group("labels")))
    return labels


def build_index(root: Path) -> MechanismIndex:
    workflow_dir = root / ".github" / "workflows"
    workflow_paths = sorted((*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")))
    workflows = {path.name: path for path in workflow_paths}
    automatic = frozenset(
        path.name for path in workflow_paths if has_automatic_trigger(path.read_text(encoding="utf-8"))
    )

    targets: set[str] = set()
    labels: set[str] = set()
    for path in cmake_files(root):
        text = cmake_code(path.read_text(encoding="utf-8", errors="replace"))
        targets.update(match.group("name") for match in CMAKE_TARGET_PATTERN.finditer(text))
        for match in CMAKE_LABEL_PATTERN.finditer(text):
            labels.update(split_labels(match.group("quoted") or match.group("bare")))

    test_root = root / "tests"
    if test_root.is_dir():
        for path in sorted(test_root.rglob("*")):
            if path.is_file() and path.suffix in {".cpp", ".hpp"} and "_deps" not in path.parts:
                labels.update(labels_from_test_source(path.read_text(encoding="utf-8", errors="replace")))

    tools_root = root / "tools"
    if tools_root.is_dir():
        for path in tools_root.rglob("mdux_*.py"):
            targets.add(path.stem.replace("_", "-"))

    # A standalone `mdux-*` code span can also name one of the repository's working procedures.
    # Discover those directories rather than maintaining an allow-list: they are real, resolvable
    # mechanisms, but not CMake targets or installed tool binaries.
    skills_root = root / ".agents" / "skills"
    if skills_root.is_dir():
        targets.update(path.name for path in skills_root.glob("mdux-*") if path.is_dir())

    return MechanismIndex(workflows, automatic, frozenset(labels), frozenset(targets))


def markdown_prose_lines(text: str) -> list[tuple[int, str]]:
    """Numbered Markdown lines outside backtick and tilde fenced blocks."""
    visible: list[tuple[int, str]] = []
    fence_character: str | None = None
    fence_length = 0
    for line_number, line in enumerate(text.splitlines(), start=1):
        fence = FENCE_PATTERN.match(line)
        if fence is not None:
            token = fence.group("fence")
            if fence_character is None:
                fence_character = token[0]
                fence_length = len(token)
                continue
            if token[0] == fence_character and len(token) >= fence_length:
                fence_character = None
                fence_length = 0
                continue
        if fence_character is None:
            visible.append((line_number, line))
    return visible


def workflow_comment_lines(text: str) -> list[tuple[int, str]]:
    """Numbered full-line YAML comments; run scripts and action configuration are not prose."""
    return [
        (line_number, match.group("comment"))
        for line_number, line in enumerate(text.splitlines(), start=1)
        if (match := re.match(r"^\s*#(?P<comment>.*)$", line)) is not None
    ]


def check_lines(path: Path, lines: list[tuple[int, str]], index: MechanismIndex) -> list[Finding]:
    findings: list[Finding] = []
    for line_number, line in lines:
        if ASPIRATIONAL_MARKER in line:
            continue

        for match in WORKFLOW_PATTERN.finditer(line):
            citation = match.group("path")
            name = Path(citation).name
            if name not in index.workflows:
                findings.append(Finding(path, line_number, f"workflow '{citation}' does not exist"))
            elif name not in index.automatic_workflows:
                findings.append(
                    Finding(
                        path,
                        line_number,
                        f"workflow '{citation}' has no push or pull_request trigger",
                    )
                )

        for match in CTEST_LABEL_PATTERN.finditer(line):
            label = match.group("label")
            if label not in index.labels:
                findings.append(Finding(path, line_number, f"ctest label '{label}' is not attached to any test"))

        for match in MECHANISM_CITATION_PATTERN.finditer(line):
            name = match.group("name")
            if name not in index.targets:
                findings.append(Finding(path, line_number, f"CMake/tool target '{name}' does not exist"))
    return findings


def check_repository(root: Path) -> tuple[list[Finding], int]:
    index = build_index(root)
    findings: list[Finding] = []
    checked = 0

    docs_root = root / "docs"
    if docs_root.is_dir():
        for path in sorted(docs_root.rglob("*.md")):
            checked += 1
            findings.extend(
                check_lines(path, markdown_prose_lines(path.read_text(encoding="utf-8", errors="replace")), index)
            )

    workflow_root = root / ".github" / "workflows"
    if workflow_root.is_dir():
        for path in sorted((*workflow_root.glob("*.yml"), *workflow_root.glob("*.yaml"))):
            checked += 1
            findings.extend(
                check_lines(path, workflow_comment_lines(path.read_text(encoding="utf-8", errors="replace")), index)
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

    root = args.repo_root.resolve()
    findings, checked = check_repository(root)
    if findings:
        for finding in findings:
            relative = finding.path.relative_to(root) if finding.path.is_relative_to(root) else finding.path
            print(
                f"mdux-named-mechanisms: {relative}:{finding.line}: {finding.message}",
                file=sys.stderr,
            )
        print(f"mdux-named-mechanisms: {len(findings)} finding(s)", file=sys.stderr)
        print(
            "mdux-named-mechanisms: fix the citation or append "
            f"'{ASPIRATIONAL_MARKER}' to a deliberately aspirational line",
            file=sys.stderr,
        )
        return 1

    print(f"mdux-named-mechanisms: OK ({checked} documents/workflows checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
