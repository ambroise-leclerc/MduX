#!/usr/bin/env python3
"""Decides whether a pull request's changed paths need the C++ build-and-test workflows.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

Reads one repository-relative path per line on standard input - the file list GitHub reports for a
pull request - and prints ``code=true`` or ``code=false``, a line a workflow step appends to
``$GITHUB_OUTPUT``. ``code=false`` means every path is prose that no build, test or tool reads, so the
toolchain legs can be skipped; a skipped job still satisfies a required status check.

The rule fails toward building. A path is docs-only only when it is a Markdown file in one of the
places below *and* is not one of the documents a build-time check reads. Anything else - source,
CMake, workflows, schemas, JSON or TOML under ``docs/``, an unknown extension - selects the full
build, and so does an empty list, because an empty list means the file list could not be read
rather than that nothing changed.

Usage:
    gh api --paginate repos/OWNER/REPO/pulls/N/files --jq '.[].filename' \\
        | python3 tools/ci/classify_changes.py

Exit status 0 always when the input was read; the decision is in the output.
"""
from __future__ import annotations

import sys
from collections.abc import Iterable
from pathlib import PurePosixPath

# Markdown under these roots, and at the repository root, is prose only.
DOCS_ONLY_ROOTS: tuple[str, ...] = ("docs", "software_development_file", "examples")

# Markdown that a build-time check reads. `tools/docs-lint/check_documented_test_selectors.py` runs
# in the GCC leg against a built CTest tree and parses exactly these files for `ctest -R` selectors,
# so a change to one of them is a change to something that leg verifies.
BUILD_READ_DOCUMENTS: frozenset[str] = frozenset({"AGENTS.md", "docs/getting-started.md"})
BUILD_READ_PREFIXES: tuple[str, ...] = (".agents/",)


def is_docs_only(path: str) -> bool:
    """Returns True when `path` is Markdown that nothing in a build or test reads."""
    normalized = path.strip()
    if normalized.startswith("./"):
        normalized = normalized[2:]
    if not normalized.endswith(".md"):
        return False
    if normalized in BUILD_READ_DOCUMENTS or normalized.startswith(BUILD_READ_PREFIXES):
        return False
    parts = PurePosixPath(normalized).parts
    if len(parts) == 1:
        return True
    return parts[0] in DOCS_ONLY_ROOTS


def needs_code_build(paths: Iterable[str]) -> bool:
    """Returns True unless there is at least one path and every path is docs-only."""
    changed = [path for path in paths if path.strip()]
    if not changed:
        return True
    return not all(is_docs_only(path) for path in changed)


def main() -> int:
    decision = needs_code_build(sys.stdin.read().splitlines())
    print(f"code={'true' if decision else 'false'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
