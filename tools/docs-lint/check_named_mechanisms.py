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
# CONTRIBUTING tells authors to name a tracking issue beside the marker. Checking it is the
# difference between an exception and an untracked permanent one - the suppression list this
# checker deliberately does not have, spelled differently.
ASPIRATIONAL_ISSUE_PATTERN = re.compile(re.escape(ASPIRATIONAL_MARKER) + r"[^\n]*?issue\s+#\d+")
AUTOMATIC_EVENTS = frozenset({"push", "pull_request"})

FENCE_PATTERN = re.compile(r"^\s*(?P<fence>`{3,}|~{3,})")
WORKFLOW_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_./-])(?P<path>(?:\.github/workflows/)?[A-Za-z0-9_.-]+\.ya?ml)\b"
)
# The gap between `ctest` and its `-L` may hold options and preset names, but not prose: without
# a bound, "Run ctest -L pixel and see ctest(1) for -L semantics" reads `semantics` as a label.
# Brackets and commas end a command and start a sentence about one.
CTEST_LABEL_PATTERN = re.compile(
    r"\bctest\b(?P<gap>[^\n(),]{0,60}?)(?:-L|--label-regex)(?:\s+|=)"
    r"(?:(?P<quote>[\"'])(?P<quoted>[^\n]*?)(?P=quote)|(?P<bare>[^\s,\])`]+))"
)
# A name may contain dots but never end on one: a citation closing a sentence would otherwise
# swallow the full stop and be reported as the target `mdux-shaderbake.`, which nobody wrote.
MECHANISM_CITATION_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_./-])(?P<name>mdux-[a-z0-9](?:[a-z0-9_.-]*[a-z0-9])?)(?![A-Za-z0-9_/-])"
)
# Diagnostic prefixes the Python checkers print. They are real, resolvable mechanisms that are
# neither CMake targets nor `mdux_*.py` files - `mdux-file-headers` lives in `check_file_headers.py`
# - so without this a true sentence naming one is rejected.
PYTHON_DIAGNOSTIC_PATTERN = re.compile(r"""["'](?P<name>mdux-[a-z0-9][a-z0-9-]*)(?::|["'])""")

CMAKE_TARGET_PATTERN = re.compile(
    r"\badd_(?:executable|library|custom_target)\s*\(\s*(?P<name>[A-Za-z0-9_.+-]+)", re.MULTILINE
)
# `LABELS a b` is valid CMake and attaches both. The unquoted branch therefore runs to the end of
# the argument list, and `split_labels` drops the ALL-CAPS property keyword that follows.
CMAKE_LABEL_PATTERN = re.compile(r"\bLABELS\s+(?:\"(?P<quoted>[^\"]+)\"|(?P<bare>[^\n)\"]+))")
CMAKE_BRACKET_ARGUMENT_PATTERN = re.compile(r"\[(?P<equals>=*)\[")
CMAKE_BRACKET_COMMENT_PATTERN = re.compile(r"#\[(?P<equals>=*)\[")
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
    """Whether a workflow's top-level ``on`` includes an event that fires without a human.

    All three YAML spellings are accepted, because rejecting one reports a workflow that does run
    on every push as having no trigger, which is worse than saying nothing.
    """
    lines = text.splitlines()
    for index, line in enumerate(lines):
        # Column 0 only. A `push:` nested under some job's `workflow_call: inputs:` is an input
        # named push, not a trigger.
        header = re.match(r"""^(?:on|["']on["']):(?P<rest>.*)$""", line)
        if header is None:
            continue

        rest = header.group("rest").split("#", 1)[0].strip()
        if rest.startswith("["):
            events = {event.strip().strip("'\"") for event in rest.strip("[]").split(",")}
            return bool(events.intersection(AUTOMATIC_EVENTS))
        if rest:
            return rest.strip("'\"") in AUTOMATIC_EVENTS

        # Block form. Only direct children count, and comments and blank lines inside the block
        # are skipped rather than ending it.
        child_indent: int | None = None
        for child in lines[index + 1 :]:
            if not child.strip() or child.lstrip().startswith("#"):
                continue
            indent = len(child) - len(child.lstrip())
            if indent == 0:
                break
            if child_indent is None:
                child_indent = indent
            if indent != child_indent:
                continue
            key = re.match(r"\s*(?P<key>[A-Za-z_][A-Za-z0-9_-]*)\s*:", child)
            if key is not None and key.group("key") in AUTOMATIC_EVENTS:
                return True
            sequence_item = re.match(
                r"\s*-\s*[\"']?(?P<event>[A-Za-z_][A-Za-z0-9_-]*)[\"']?\s*(?:#.*)?$",
                child,
            )
            if sequence_item is not None and sequence_item.group("event") in AUTOMATIC_EVENTS:
                return True
        return False
    return False


def label_is_attached(label: str, labels: frozenset[str]) -> bool:
    """Whether ``ctest -L <label>`` would select anything.

    ``-L`` takes a regular expression, so `-L determin` really does select `determinism`. Matching
    ctest's own semantics keeps a legitimate partial citation from being called unattached.
    """
    if label in labels:
        return True
    try:
        pattern = re.compile(label)
    except re.error:
        return False
    return any(pattern.search(known) is not None for known in labels)


def unclosed_fence_line(text: str) -> int | None:
    """Line number of a fence that is never closed, if there is one.

    An unterminated fence makes every later line invisible to this checker. Silently skipping the
    rest of a document is the blind spot this tool exists to prevent, so it is reported.
    """
    fence_character: str | None = None
    fence_length = 0
    opened_at = 0
    for line_number, line in enumerate(text.splitlines(), start=1):
        fence = FENCE_PATTERN.match(line)
        if fence is None:
            continue
        token = fence.group("fence")
        if fence_character is None:
            fence_character, fence_length, opened_at = token[0], len(token), line_number
        elif token[0] == fence_character and len(token) >= fence_length:
            fence_character, fence_length = None, 0
    return opened_at if fence_character is not None else None


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
    """Label tokens from one LABELS argument list.

    `LABELS a b` attaches both, so the unquoted match runs to the end of the argument list and the
    next property keyword has to be recognised here. CMake property keywords are ALL-CAPS and this
    project's labels are lowercase, so the first ALL-CAPS token ends the list.
    """
    labels: set[str] = set()
    for token in re.split(r"[\s,;]+", value):
        if not token:
            continue
        if re.fullmatch(r"[A-Z][A-Z_]*", token):
            break
        if "${" in token:
            continue
        labels.add(token)
    return labels


def cmake_code(text: str) -> str:
    """CMake text with comments blanked while quoted and bracket arguments remain intact."""
    output: list[str] = []
    index = 0
    quoted = False
    bracket_end: str | None = None
    comment_end: str | None = None
    while index < len(text):
        if comment_end is not None:
            if text.startswith(comment_end, index):
                output.extend(" " * len(comment_end))
                index += len(comment_end)
                comment_end = None
            else:
                output.append("\n" if text[index] == "\n" else " ")
                index += 1
            continue

        if bracket_end is not None:
            if text.startswith(bracket_end, index):
                output.extend(bracket_end)
                index += len(bracket_end)
                bracket_end = None
            else:
                output.append(text[index])
                index += 1
            continue

        character = text[index]
        if quoted:
            output.append(character)
            index += 1
            if character == "\\" and index < len(text):
                output.append(text[index])
                index += 1
            elif character == '"':
                quoted = False
            continue

        if character == '"':
            quoted = True
            output.append(character)
            index += 1
            continue

        bracket = CMAKE_BRACKET_ARGUMENT_PATTERN.match(text, index)
        if bracket is not None:
            opener = bracket.group(0)
            bracket_end = "]" + bracket.group("equals") + "]"
            output.extend(opener)
            index += len(opener)
            continue

        if character == "#":
            bracket_comment = CMAKE_BRACKET_COMMENT_PATTERN.match(text, index)
            if bracket_comment is not None:
                opener = bracket_comment.group(0)
                comment_end = "]" + bracket_comment.group("equals") + "]"
                output.extend(" " * len(opener))
                index += len(opener)
                continue
            newline = text.find("\n", index)
            end = len(text) if newline == -1 else newline
            output.extend(" " * (end - index))
            index = end
            continue

        output.append(character)
        index += 1
    return "".join(output)


def cpp_without_comments(text: str) -> str:
    """Blank C++ comments while preserving quoted strings and source positions."""
    def replace(match: re.Match[str]) -> str:
        """Blank a comment token without changing its line count or quoted tokens."""
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
    """Resolve the workflows, labels, targets, tools, and skills present under ``root``."""
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
        # A checker's diagnostic name is not always its filename: `mdux-file-headers` is printed by
        # `check_file_headers.py`. Read the names they print rather than inferring from the path,
        # so documenting a check does not fail the check.
        for path in sorted(tools_root.rglob("*.py")):
            if path.name.startswith("test_"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            targets.update(match.group("name") for match in PYTHON_DIAGNOSTIC_PATTERN.finditer(text))

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
    comments: list[tuple[int, str]] = []
    scalar_parent_indent: int | None = None
    scalar_header = re.compile(
        r"^(?P<indent>\s*)(?:(?:-\s*)?[^#:\n]+:\s*|-(?:\s*))"
        r"[|>][1-9+-]{0,2}\s*(?:#.*)?$"
    )
    for line_number, line in enumerate(text.splitlines(), start=1):
        indent = len(line) - len(line.lstrip())
        if scalar_parent_indent is not None:
            if not line.strip() or indent > scalar_parent_indent:
                continue
            scalar_parent_indent = None

        header = scalar_header.match(line)
        if header is not None:
            scalar_parent_indent = len(header.group("indent"))
            continue

        comment = re.match(r"^\s*#(?P<comment>.*)$", line)
        if comment is not None:
            comments.append((line_number, comment.group("comment")))
    return comments


def ctest_label_expression(match: re.Match[str]) -> str:
    """Complete CTest label expression captured with or without shell quotes."""
    if match.group("quoted") is not None:
        return match.group("quoted")
    # Keep the historical sentence-boundary behavior: a full stop closing prose is not part of
    # the unquoted expression. Quoted expressions remain byte-for-byte intact.
    return match.group("bare").rstrip(".")


def check_lines(path: Path, lines: list[tuple[int, str]], index: MechanismIndex) -> list[Finding]:
    """Report every unresolved mechanism citation in numbered prose or comment lines."""
    findings: list[Finding] = []
    for line_number, line in lines:
        if ASPIRATIONAL_MARKER in line:
            if ASPIRATIONAL_ISSUE_PATTERN.search(line) is None:
                findings.append(
                    Finding(
                        path,
                        line_number,
                        "aspirational marker names no tracking issue; append 'issue #NNN' to it",
                    )
                )
            continue

        for match in WORKFLOW_PATTERN.finditer(line):
            citation = match.group("path")
            name = Path(citation).name
            if name not in index.workflows:
                # A bare filename is only a workflow citation if it names one. The tree holds YAML
                # that is not a workflow - `.github/dependabot.yml`, `.github/codeql/*.yml` - and
                # calling a file that exists missing would be a false positive on true prose.
                if "/" not in citation:
                    continue
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
            label = ctest_label_expression(match)
            if not label_is_attached(label, index.labels):
                findings.append(Finding(path, line_number, f"ctest label '{label}' is not attached to any test"))

        for match in MECHANISM_CITATION_PATTERN.finditer(line):
            name = match.group("name")
            if name not in index.targets:
                findings.append(Finding(path, line_number, f"CMake/tool target '{name}' does not exist"))
    return findings


def check_repository(root: Path) -> tuple[list[Finding], int]:
    """Check every Markdown document and workflow comment, returning findings and file count."""
    index = build_index(root)
    findings: list[Finding] = []
    checked = 0

    docs_root = root / "docs"
    if docs_root.is_dir():
        for path in sorted(docs_root.rglob("*.md")):
            checked += 1
            text = path.read_text(encoding="utf-8", errors="replace")
            opened_at = unclosed_fence_line(text)
            if opened_at is not None:
                findings.append(
                    Finding(
                        path,
                        opened_at,
                        "fenced block is never closed, so the rest of this document is unchecked",
                    )
                )
            findings.extend(check_lines(path, markdown_prose_lines(text), index))

    workflow_root = root / ".github" / "workflows"
    if workflow_root.is_dir():
        for path in sorted((*workflow_root.glob("*.yml"), *workflow_root.glob("*.yaml"))):
            checked += 1
            findings.extend(
                check_lines(path, workflow_comment_lines(path.read_text(encoding="utf-8", errors="replace")), index)
            )
    return findings, checked


def main(argv: list[str]) -> int:
    """Run the repository check and render its stable text diagnostic envelope."""
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

    if checked == 0:
        # A gate that scans nothing and reports success is a green tick over zero assertions - the
        # exact shape this checker exists to catch. It is reachable: the root is inferred from this
        # file's location, so moving or vendoring the script would silently empty the scan.
        print(
            "mdux-named-mechanisms: no documents or workflows found under "
            f"'{root}'; --repo-root is wrong or the layout moved",
            file=sys.stderr,
        )
        return 1

    print(f"mdux-named-mechanisms: OK ({checked} documents/workflows checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
