#!/usr/bin/env python3
"""mdux-docs-lint: regulatory citation and reproduced-text checks for MduX documentation.

Host-only tool (see ADR-004's trust-zone model) - never built into MduXCore or MduX, no
third-party dependencies, standard library only, so it runs anywhere Python 3.9+ runs
without a build.

Checks, per docs/governance/citation-convention.md and ADR-006:

1. Citation-key format: every "<Standard> §<clause> <title>"-shaped string must use one of
   the five approved standard identifiers and a well-formed clause number.
2. Justification objects (fenced ```json blocks containing a "justification_id" key) must
   carry all required fields, a justification_id matching JUS-NNN unique across the corpus,
   a standard from the approved set, and evidence_refs[] paths that exist in the repository.
3. Reproduced-text heuristics: flag files whose prose self-describes as a transcription of a
   standard, or that contain long unbroken runs of externally-sourced normative-style text.

Usage:
    python3 tools/docs-lint/mdux_docs_lint.py [--format text|json] [paths...]

Exit status 0 if no findings, 1 otherwise. With no paths, scans docs/ and
software_development_file/ (if present) under the repository root.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

APPROVED_STANDARDS = (
    "IEC 62304:2006",
    "ISO 13485:2016",
    "ISO 14971:2019",
    "IEC 62366-1:2015",
    "IEC 81001-5-1:2021",
)

# "<Standard> §<clause> <title>" - clause is digits and dots, title is free text up to
# end of line or closing markup (backtick, paren, bracket, pipe for table cells).
CITATION_RE = re.compile(
    r"(?P<standard>[A-Z]{2,4}\s?\d[\d.\-]*:\d{4})\s*§\s*(?P<clause>\d[\d.]*)\s+(?P<title>[^`)\]\|\n]+)"
)

# Deliberately loose - this is a heuristic, not a parser. Matches the two phrasings found
# in the files ADR-006 removed, plus close variants.
REPRODUCTION_MARKERS = (
    re.compile(r"comprehensive markdown version of", re.IGNORECASE),
    re.compile(r"compiled from (the )?(public|official) source", re.IGNORECASE),
    re.compile(r"\bverbatim\b", re.IGNORECASE),
    re.compile(r"full text of (the )?standard", re.IGNORECASE),
)

# A file may suppress the reproduction heuristic for a specific line by ending it with
# this marker, e.g. when quoting a standard's title (not its normative text) for citation
# purposes. Used sparingly and reviewed - this is an escape hatch, not a bypass.
SUPPRESS_MARKER = "mdux-docs-lint:allow-reproduction-marker"

JUSTIFICATION_ID_RE = re.compile(r"^JUS-\d{3,}$")
REQUIRED_JUSTIFICATION_FIELDS = (
    "justification_id",
    "standard",
    "clause_ref",
    "rationale",
    "evidence_refs",
)

DEFAULT_SCAN_DIRS = ("docs", "software_development_file")

# Root documents are scanned too. Wave 2 deleted and renamed files under docs/ while live
# references in README.md and AGENTS.md survived until an integration repair - exactly the failure
# the link check below exists to catch, and it would be missed if the scan stopped at docs/.
DEFAULT_SCAN_FILES = ("README.md", "AGENTS.md", "CONTRIBUTING.md")

# Where retired paths are recorded. References to a path listed here are permitted inside this
# document (it is the disposition record) and rejected everywhere else.
SUPERSEDED_RECORD = "docs/governance/superseded-documents.md"

MARKDOWN_LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING_RE = re.compile(r"^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$")
# Paths look like a/b or a/b.ext; a bare word in backticks is prose, not a path.
RETIRED_PATH_RE = re.compile(r"[A-Za-z0-9._-]+(?:/[A-Za-z0-9._*-]+)+")


@dataclass
class Finding:
    """One finding in the shared envelope - docs/governance/schemas/diagnostic.schema.json.

    Deliberately the same field names and severity vocabulary as `mdux::tools::cli::Diagnostic`
    (tools/common/Cli.cppm) and mdux-evidence-lint, so an agent parses one schema for the whole
    repository. `line` and `column` are both 1-based, with 0 meaning "no precise position on this
    axis": a finding about a whole block sets the line and leaves the column 0.
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


@dataclass
class LintContext:
    repo_root: Path
    findings: list[Finding] = field(default_factory=list)
    seen_justification_ids: dict[str, str] = field(default_factory=dict)

    def __post_init__(self) -> None:
        # macOS exposes /tmp through a /private/var symlink. Resolve the root once so a target
        # resolved below it is not mistaken for a path escaping the repository.
        self.repo_root = self.repo_root.resolve()

    def relativize(self, path: Path) -> str:
        try:
            return str(path.resolve().relative_to(self.repo_root)) if path.is_absolute() else str(path)
        except ValueError:
            return str(path)

    def report(
        self,
        path: Path,
        line: int,
        code: str,
        severity: str,
        message: str,
        fix_hint: str = "",
        *,
        column: int = 0,
    ) -> None:
        # `column` is keyword-only so the existing positional call sites, which have no column to
        # report, keep working unchanged rather than every one of them growing a `0`.
        self.findings.append(
            Finding(self.relativize(path), line, code, severity, message, fix_hint, column)
        )


def check_citation_keys(ctx: LintContext, path: Path, text: str) -> None:
    for lineno, line in enumerate(text.splitlines(), start=1):
        for match in CITATION_RE.finditer(line):
            standard = match.group("standard").strip()
            # Normalize internal whitespace (e.g. "IEC 62304 :2006" typos) before comparing.
            normalized = re.sub(r"\s+", " ", standard)
            if normalized not in APPROVED_STANDARDS:
                ctx.report(
                    path, lineno, "MDX-D001", "error",
                    f"Unknown or malformed standard identifier '{standard}' in citation key.",
                    f"Use one of: {', '.join(APPROVED_STANDARDS)}",
                    # A line may carry several citations; the column is what distinguishes which
                    # one is wrong, and it is known exactly here.
                    column=match.start() + 1,
                )


def iter_justification_blocks(text: str):
    """Yield (start_line, json_text) for every fenced ```json block containing a
    justification_id key - a cheap pre-filter before attempting to parse as JSON."""
    fence_re = re.compile(r"```json\s*\n(.*?)```", re.DOTALL)
    for match in fence_re.finditer(text):
        block = match.group(1)
        if "justification_id" not in block:
            continue
        start_line = text.count("\n", 0, match.start()) + 1
        yield start_line, block


def check_justifications(ctx: LintContext, path: Path, text: str) -> None:
    for start_line, block in iter_justification_blocks(text):
        try:
            obj = json.loads(block)
        except json.JSONDecodeError as exc:
            ctx.report(
                path, start_line, "MDX-D010", "error",
                f"Justification block is not valid JSON: {exc.msg}",
                "Fix the JSON syntax; see docs/governance/citation-convention.md for the expected shape.",
            )
            continue

        if not isinstance(obj, dict):
            ctx.report(path, start_line, "MDX-D010", "error", "Justification block must be a JSON object.")
            continue

        missing = [f for f in REQUIRED_JUSTIFICATION_FIELDS if f not in obj]
        if missing:
            ctx.report(
                path, start_line, "MDX-D011", "error",
                f"Justification block is missing required field(s): {', '.join(missing)}.",
                f"Required fields: {', '.join(REQUIRED_JUSTIFICATION_FIELDS)}.",
            )

        jid = obj.get("justification_id")
        if jid is not None:
            if not JUSTIFICATION_ID_RE.match(str(jid)):
                ctx.report(
                    path, start_line, "MDX-D012", "error",
                    f"justification_id '{jid}' does not match the required JUS-NNN format.",
                )
            elif jid in ctx.seen_justification_ids:
                ctx.report(
                    path, start_line, "MDX-D013", "error",
                    f"Duplicate justification_id '{jid}' - already used in "
                    f"{ctx.seen_justification_ids[jid]}. justification_id must be unique "
                    "across the whole corpus, not per file.",
                )
            else:
                ctx.seen_justification_ids[jid] = ctx.relativize(path)

        standard = obj.get("standard")
        if standard is not None and standard not in APPROVED_STANDARDS:
            ctx.report(
                path, start_line, "MDX-D014", "error",
                f"Justification 'standard' field '{standard}' is not an approved identifier.",
                f"Use one of: {', '.join(APPROVED_STANDARDS)}",
            )

        clause_ref = obj.get("clause_ref")
        if isinstance(clause_ref, str):
            match = CITATION_RE.fullmatch(clause_ref)
            if match is None or match.group("standard") not in APPROVED_STANDARDS:
                ctx.report(
                    path, start_line, "MDX-D016", "error",
                    f"clause_ref '{clause_ref}' is not a complete approved citation key.",
                    "Use '<Standard> §<clause> <Short clause title>'.",
                )
            elif standard in APPROVED_STANDARDS and match.group("standard") != standard:
                ctx.report(
                    path, start_line, "MDX-D017", "error",
                    f"clause_ref standard '{match.group('standard')}' does not match "
                    f"the standard field '{standard}'.",
                )

        evidence_refs = obj.get("evidence_refs")
        if evidence_refs is not None and (
            not isinstance(evidence_refs, list)
            or not evidence_refs
            or not all(isinstance(ref, str) and ref for ref in evidence_refs)
        ):
            ctx.report(
                path, start_line, "MDX-D018", "error",
                "evidence_refs must be a non-empty array of repository-relative path strings.",
            )
            evidence_refs = []

        for ref in evidence_refs or []:
            ref_path = Path(ref)
            candidate = (ctx.repo_root / ref_path).resolve()
            try:
                candidate.relative_to(ctx.repo_root.resolve())
            except ValueError:
                ctx.report(
                    path, start_line, "MDX-D019", "error",
                    f"evidence_refs entry '{ref}' resolves outside the repository.",
                    "Use a repository-relative path without '..' traversal.",
                )
                continue
            if ref_path.is_absolute() or not candidate.exists():
                ctx.report(
                    path, start_line, "MDX-D015", "error",
                    f"evidence_refs entry '{ref}' does not exist in the repository.",
                    "evidence_refs must be real repository paths, checked at lint time so "
                    "they can't silently drift after the file they point at is renamed or removed.",
                )


FENCE_RE = re.compile(r"^\s*```")


def check_reproduction_markers(ctx: LintContext, path: Path, text: str) -> None:
    # The heuristic targets prose claiming to reproduce a standard, not source/config
    # examples that happen to use words like "verbatim" as a technical term (e.g. CMake's
    # add_custom_target(... VERBATIM) argument) - skip fenced code blocks entirely.
    in_fence = False
    for lineno, line in enumerate(text.splitlines(), start=1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence or SUPPRESS_MARKER in line:
            continue
        for marker in REPRODUCTION_MARKERS:
            if marker.search(line):
                ctx.report(
                    path, lineno, "MDX-D020", "error",
                    f"Line matches a reproduced-text heuristic ({marker.pattern!r}).",
                    "Rewrite as original prose explaining the requirement's intent (see "
                    "ADR-006), or suppress a deliberate false positive by appending "
                    f"'{SUPPRESS_MARKER}' to the line.",
                )
                break  # one finding per line is enough



def slugify_heading(text: str) -> str:
    """GitHub's anchor slug: lowercase, drop punctuation, spaces to hyphens.

    Markdown formatting is stripped first, so `## The `foo` rule` anchors as `the-foo-rule` rather
    than keeping the backticks a reader does not type into a URL.
    """
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[*_~]", "", text)
    slug = text.strip().lower()
    slug = re.sub(r"[^\w\s-]", "", slug)
    return re.sub(r"[\s]+", "-", slug).strip("-")


def collect_anchors(text: str) -> set[str]:
    """Every heading anchor in `text`, with GitHub's -1/-2 suffixes for repeats."""
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    in_fence = False
    for line in text.splitlines():
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = HEADING_RE.match(line)
        if not match:
            continue
        slug = slugify_heading(match.group(1))
        if not slug:
            continue
        seen = counts.get(slug, 0)
        anchors.add(slug if seen == 0 else f"{slug}-{seen}")
        counts[slug] = seen + 1
    return anchors


def check_internal_links(ctx: LintContext, path: Path, text: str) -> None:
    """Repository-relative link targets resolve, and local anchors exist.

    External links are deliberately not fetched: network availability must not decide whether CI
    passes. This check is entirely local and therefore deterministic.
    """
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        for target in MARKDOWN_LINK_RE.findall(line):
            if target.startswith(("http://", "https://", "mailto:", "#!")):
                continue

            if target.startswith("#"):
                anchor = target[1:]
                if anchor and anchor not in collect_anchors(text):
                    ctx.report(
                        path, number, "MDX-D010", "error",
                        f"Link target '{target}' does not match any heading in this document.",
                        "Check the heading spelling, or link to the file instead of an anchor.",
                    )
                continue

            file_part, _, anchor = target.partition("#")
            if not file_part:
                continue

            resolved = (path.parent / file_part).resolve()
            try:
                relative = resolved.relative_to(ctx.repo_root)
            except ValueError:
                ctx.report(
                    path, number, "MDX-D012", "error",
                    f"Link target '{target}' escapes the repository.",
                    "Use a path inside the repository, or an external https:// URL.",
                )
                continue

            if not resolved.exists():
                ctx.report(
                    path, number, "MDX-D010", "error",
                    f"Link target '{target}' does not exist ({relative}).",
                    "Fix the path, or remove the link if the document was retired.",
                )
                continue

            if anchor and resolved.is_file() and resolved.suffix == ".md":
                try:
                    other = resolved.read_text(encoding="utf-8")
                except (OSError, UnicodeDecodeError):
                    continue
                if anchor not in collect_anchors(other):
                    ctx.report(
                        path, number, "MDX-D011", "error",
                        f"Link target '{target}' names a heading '{anchor}' that {relative} "
                        "does not have.",
                        "Check the heading spelling in the linked document.",
                    )


def load_retired_paths(repo_root: Path) -> set[str]:
    """Paths retired by the disposition record, read from its table's first column."""
    record = repo_root / SUPERSEDED_RECORD
    if not record.is_file():
        return set()
    retired: set[str] = set()
    for line in record.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 2 or not cells[0].startswith("`"):
            continue
        candidate = cells[0].strip("`")
        # Glob entries such as `docs/.../*.json` name a set of files rather than one path; a
        # literal match on the glob text is still worth catching, so keep them as written.
        if RETIRED_PATH_RE.fullmatch(candidate):
            retired.add(candidate)
    return retired


def check_retired_paths(ctx: LintContext, path: Path, text: str, retired: set[str]) -> None:
    """A retired path may be named in the disposition record and nowhere else.

    Wave 2 deleted documents while live references survived, so a reader followed a link into
    nothing. Naming the path at all is the failure - the reference does not have to be a link.
    """
    if ctx.relativize(path).replace("\\", "/") == SUPERSEDED_RECORD:
        return
    for number, line in enumerate(text.splitlines(), start=1):
        for candidate in retired:
            if candidate in line:
                ctx.report(
                    path, number, "MDX-D013", "error",
                    f"'{candidate}' was retired and must not be referenced outside "
                    f"{SUPERSEDED_RECORD}.",
                    "Point at the replacement named in the disposition record, or drop the "
                    "reference.",
                )


def lint_file(ctx: LintContext, path: Path, retired: set[str] | None = None) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        ctx.report(path, 1, "MDX-D000", "error", "File is not valid UTF-8.")
        return
    check_citation_keys(ctx, path, text)
    check_justifications(ctx, path, text)
    check_reproduction_markers(ctx, path, text)
    check_internal_links(ctx, path, text)
    check_retired_paths(ctx, path, text, retired if retired is not None else set())


def find_repo_root(start: Path) -> Path:
    current = start.resolve()
    for candidate in (current, *current.parents):
        if (candidate / ".git").exists():
            return candidate
    return start.resolve()


def collect_markdown_files(repo_root: Path, paths: list[str]) -> list[Path]:
    if paths:
        roots = [Path(p) for p in paths]
    else:
        roots = [repo_root / d for d in DEFAULT_SCAN_DIRS if (repo_root / d).is_dir()]
        roots += [repo_root / f for f in DEFAULT_SCAN_FILES if (repo_root / f).is_file()]

    files: list[Path] = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.is_dir():
            files.extend(sorted(root.rglob("*.md")))
    return files


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="*", help="Files or directories to lint (default: docs/, software_development_file/, and the root README/AGENTS/CONTRIBUTING)")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)

    repo_root = find_repo_root(Path.cwd())
    ctx = LintContext(repo_root=repo_root)

    files = collect_markdown_files(repo_root, args.paths)
    retired = load_retired_paths(repo_root)
    for path in files:
        lint_file(ctx, path, retired)

    if args.format == "json":
        # The shared envelope: docs/governance/schemas/diagnostic.schema.json. `tool` is what
        # keeps findings attributable once several tools' output is aggregated.
        print(
            json.dumps(
                {
                    "tool": "mdux-docs-lint",
                    "filesChecked": len(files),
                    "findings": [f.to_json() for f in ctx.findings],
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        if not ctx.findings:
            print(f"mdux-docs-lint: OK ({len(files)} files checked, 0 findings)")
        else:
            for f in ctx.findings:
                loc = f"{f.path}:{f.line}" + (f":{f.column}" if f.column else "")
                print(f"{loc}: {f.severity}: [{f.code}] {f.message}")
                if f.fix_hint:
                    print(f"  hint: {f.fix_hint}")
            print(f"mdux-docs-lint: {len(ctx.findings)} finding(s) in {len(files)} files checked")

    return 1 if ctx.findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
