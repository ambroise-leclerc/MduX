#!/usr/bin/env python3
"""Fails when a C++ file's opening Doxygen block does not follow CONTRIBUTING.md.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

`CONTRIBUTING.md` fixes one shape for a file-level block - `@file <name.ext>`, then `@brief`, no
`@author` - and records why with a measurement rather than a preference: `@file` is what attaches
the block to the *file*. Without it the block is not ignored, it silently becomes the documentation
of whatever declaration follows. On `tests/framework/RunRecords.hpp` with Doxygen 1.15, dropping the
tag moved the file's brief onto `namespace mdux::spec` - wrong documentation, and no warning.

## Why a lint and not another sweep

The rule has been settled twice. #180 changed it to match what the tree did, and issue #223 found ten
files still in the retired order - all in one epic, spread by copying the neighbour. A sweep fixes
the ten; it does nothing about the eleventh. Worse, the drift has a second cost that a sweep cannot
touch: review bots cite `CONTRIBUTING.md`, find the tree contradicting it, and raise the same finding
on unrelated pull requests, which trains everyone to wave it through.

So this runs in `Documentation Lint`, on a job that needs no C++ toolchain, and answers the question
a reviewer would otherwise have to ask by hand.

## What it checks, and what it deliberately does not

- the first `/** ... */` block in the file opens with `@file`;
- its argument is the file's own name **with extension and no path**, since `Draw.cppm` and
  `Draw.cpp` are different files with one stem and this tree has several such pairs;
- `@brief` follows it, and is present at all;
- no `@author` anywhere in the block.

It does not check the prose, the `@compliance` lines, or whether the brief is any good. It reads the
first block only: a file whose first block is a licence banner rather than documentation would be a
different convention, and the tree has none.

Usage:
    python3 tools/docs-lint/check_file_headers.py [--repo-root PATH] [paths...]

Exit status 0 when every scanned file conforms, 1 otherwise.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = (".cpp", ".cppm", ".hpp")

# Where MduX's own C++ lives. `_deps` holds fetched dependencies and `build*` holds build trees;
# neither is this project's to format.
SCAN_ROOTS = ("include", "src", "tools", "tests", "examples")
SKIP_DIRECTORY_NAMES = {"_deps", "__pycache__", "fixtures"}

BLOCK_PATTERN = re.compile(r"/\*\*(.*?)\*/", re.DOTALL)

# A Doxygen tag is a line whose content starts with `@name`, after the leading ` * `. Matching the
# bare word anywhere would read prose as markup: a block that discusses `@author` in a sentence -
# this file's own tests do - is not a block that carries the tag.
TAG_LINE_PATTERN = re.compile(r"^\s*\*?\s*@(\w+)(?:[ \t]+(.*))?$")


@dataclass(frozen=True)
class Finding:
    path: Path
    message: str


@dataclass(frozen=True)
class Tag:
    name: str
    argument: str


def first_block(text: str) -> str | None:
    """The body of the first `/** ... */` comment, or None when the file has none."""
    match = BLOCK_PATTERN.search(text)
    return match.group(1) if match else None


def tags_of(block: str) -> list[Tag]:
    """Every Doxygen tag the block carries, in order."""
    found: list[Tag] = []
    for line in block.splitlines():
        match = TAG_LINE_PATTERN.match(line)
        if match is not None:
            found.append(Tag(match.group(1), (match.group(2) or "").strip()))
    return found


def check_text(path: Path, text: str) -> list[Finding]:
    """Every way `text`'s opening block departs from the documented shape."""
    block = first_block(text)
    if block is None:
        return [Finding(path, "has no file-level Doxygen block; open one with @file then @brief")]

    tags = tags_of(block)
    names = [tag.name for tag in tags]
    findings: list[Finding] = []

    if "author" in names:
        findings.append(Finding(path, "carries an @author tag, which this project does not use"))

    if "file" not in names:
        findings.append(
            Finding(
                path,
                "opens a block without @file. Doxygen then attaches the block to whatever "
                "declaration follows instead of to the file",
            )
        )
    else:
        named = tags[names.index("file")].argument
        if named != path.name:
            findings.append(
                Finding(
                    path,
                    f"@file names '{named}'; it must name '{path.name}', with the extension and no path",
                )
            )

    if "brief" not in names:
        findings.append(Finding(path, "has a file-level block with no @brief"))
    elif "file" in names and names.index("brief") < names.index("file"):
        findings.append(Finding(path, "puts @brief before @file; CONTRIBUTING.md fixes the order the other way"))
    elif "file" in names and names.index("file") != 0:
        findings.append(Finding(path, f"opens with @{names[0]} rather than @file"))

    return findings


def collect_sources(root: Path, paths: list[str]) -> list[Path]:
    """The files to scan: the ones named, or every C++ source under the scan roots."""
    if paths:
        return [Path(entry) if Path(entry).is_absolute() else root / entry for entry in paths]

    found: list[Path] = []
    for scan_root in SCAN_ROOTS:
        base = root / scan_root
        if not base.is_dir():
            continue
        for candidate in sorted(base.rglob("*")):
            if candidate.suffix not in SOURCE_SUFFIXES or not candidate.is_file():
                continue
            if SKIP_DIRECTORY_NAMES.intersection(part for part in candidate.parts):
                continue
            found.append(candidate)
    return found


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root (default: inferred from this script's location)",
    )
    parser.add_argument("paths", nargs="*", help="files to check (default: every C++ source in the tree)")
    args = parser.parse_args(argv)
    root: Path = args.repo_root

    sources = collect_sources(root, args.paths)
    findings: list[Finding] = []
    for source in sources:
        findings.extend(check_text(source, source.read_text(encoding="utf-8", errors="replace")))

    if findings:
        for finding in findings:
            relative = finding.path.relative_to(root) if finding.path.is_relative_to(root) else finding.path
            print(f"mdux-file-headers: {relative}: {finding.message}", file=sys.stderr)
        print(f"mdux-file-headers: {len(findings)} finding(s)", file=sys.stderr)
        print("mdux-file-headers: the rule and the reason are in CONTRIBUTING.md, 'Documentation'", file=sys.stderr)
        return 1

    print(f"mdux-file-headers: OK ({len(sources)} sources checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
