#!/usr/bin/env python3
"""Regenerates AI-Reference.md for one or more regulatory-corpus directories.

One row per clause heading (`## §N.M Title` or, for a single-clause file, the `# Standard §N —
Title` H1), cross-referenced against every `Justification` object that appears textually under
that heading. Host-only tool (ADR-004): standard library only, no third-party dependencies.

Deliberately mechanical: this script only reads headings and Justification blocks that already
exist in the corpus. It does not decide what a clause covers or invent a Justification - if a row
looks wrong, the fix is in the clause file the row was generated from, not in this script.

Usage:
    python3 tools/docs-lint/generate_ai_reference.py docs/iec62304 [docs/iso13485 ...]

With no arguments, regenerates all five standard corpora under docs/.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

DEFAULT_DIRS = ("docs/iec62304", "docs/iso13485", "docs/iso14971", "docs/iec62366", "docs/iec81001")

STANDARD_NAMES = {
    "iec62304": "IEC 62304:2006",
    "iso13485": "ISO 13485:2016",
    "iso14971": "ISO 14971:2019",
    "iec62366": "IEC 62366-1:2015",
    "iec81001": "IEC 81001-5-1:2021",
}

H2_RE = re.compile(r"^##\s+(.+)$", re.MULTILINE)
H1_RE = re.compile(r"^#\s+(.+)$", re.MULTILINE)
CLAUSE_RE = re.compile(r"(§[\d.]+(?:[–-]§?[\d.]+)?)\s*(?:—|-)?\s*(.*)")
JSON_FENCE_RE = re.compile(r"```json\n(.*?)\n```", re.DOTALL)


def sections_of(text: str) -> list[tuple[str, str]]:
    """Splits `text` into (heading, body) by H2. Falls back to one section keyed by the H1
    when there is no H2 - the case for a file covering exactly one clause."""
    h2_matches = list(H2_RE.finditer(text))
    if h2_matches:
        sections = []
        for i, match in enumerate(h2_matches):
            start = match.end()
            end = h2_matches[i + 1].start() if i + 1 < len(h2_matches) else len(text)
            sections.append((match.group(1), text[start:end]))
        return sections
    h1_match = H1_RE.search(text)
    heading = h1_match.group(1) if h1_match else ""
    return [(heading, text)]


def justification_ids(body: str) -> list[str]:
    ids = []
    for block in JSON_FENCE_RE.findall(body):
        try:
            obj = json.loads(block)
        except json.JSONDecodeError:
            continue
        jid = obj.get("justification_id")
        if jid:
            ids.append(jid)
    return ids


def build_rows(directory: Path) -> list[tuple[str, str, str, str]]:
    rows = []
    for path in sorted(directory.glob("*.md")):
        if path.name in ("README.md", "AI-Reference.md"):
            continue
        text = path.read_text(encoding="utf-8")
        for heading, body in sections_of(text):
            match = CLAUSE_RE.match(heading)
            if match and match.group(1).startswith("§"):
                clause, title = match.group(1), match.group(2) or heading
            else:
                clause, title = "—", heading
            jids = justification_ids(body)
            rows.append((clause, title, path.name, ", ".join(jids) if jids else "—"))
    return rows


def render(standard_name: str, directory: Path, rows: list[tuple[str, str, str, str]]) -> str:
    lines = [
        f"# {standard_name} — per-clause index",
        "",
        "One row per clause covered in this corpus, generated from the clause headings and",
        "`Justification` objects actually present under each heading - not hand-transcribed,",
        "so it cannot drift from the prose it indexes without the source changing too.",
        "Regenerate after editing any file in this directory:",
        "",
        "```",
        f"python3 tools/docs-lint/generate_ai_reference.py {directory}",
        "```",
        "",
        "| Clause | Title | File | Justification(s) |",
        "|---|---|---|---|",
    ]
    for clause, title, fname, jids in rows:
        title_escaped = title.replace("|", "\\|")
        lines.append(f"| {clause} | {title_escaped} | [`{fname}`]({fname}) | {jids} |")
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    targets = [Path(p) for p in argv] if argv else [Path(p) for p in DEFAULT_DIRS]
    for directory in targets:
        if not directory.is_dir():
            print(f"skip: {directory} is not a directory", file=sys.stderr)
            continue
        standard_name = STANDARD_NAMES.get(directory.name, directory.name)
        rows = build_rows(directory)
        output = render(standard_name, directory, rows)
        (directory / "AI-Reference.md").write_text(output, encoding="utf-8")
        print(f"wrote {directory / 'AI-Reference.md'} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
