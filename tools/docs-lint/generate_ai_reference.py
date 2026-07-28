#!/usr/bin/env python3
"""Regenerates AI-Reference.md for one or more regulatory-corpus directories.

One row per clause section, each carrying a one-sentence pointer and a deep link to the heading
that section lives under - so a reader locates the right paragraph, not merely the right file.
Host-only tool (ADR-004): standard library only, no third-party dependencies.

Deliberately mechanical: this script reads headings, `Justification` objects and pointer comments
that already exist in the corpus. It never decides what a clause covers and never invents a
pointer. If a row looks wrong, the fix is in the clause file the row was generated from.

## The three things this script gets right that a naive version does not

**Clause inheritance.** A section heading carrying no clause number of its own inherits the one
from its file's H1. `docs/iec62304/07-problem-resolution-process.md` is headed
`# IEC 62304:2006 §9 - Software problem resolution process` and its H2s are unnumbered; without
inheritance every one of them indexes as clause "-", which is how §9 went missing from an earlier
version of this index.

**Pointers, not restated titles.** A row whose pointer repeats its own title tells a reader
nothing the heading did not. The pointer is resolved, in order, from an authored
`<!-- pointer: ... -->` comment, then the section's first `Justification` rationale (which names a
real mechanism by construction, since mdux-docs-lint checks its evidence paths exist), then the
first sentence of the body that mentions MduX. A section yielding none of the three is a
**generator error**, not a padded row - see `PointerMissing`.

**Deep anchors.** Every link targets the heading's GitHub anchor, with the same de-duplication
suffix GitHub applies when two headings in one file slugify identically.

Usage:
    python3 tools/docs-lint/generate_ai_reference.py [--check] [docs/iec62304 ...]

`--check` regenerates in memory and reports any index that is out of date, without writing -
this is what CI runs. Exit status 0 when everything is in order, 1 otherwise.

With no directory arguments, processes all five standard corpora under docs/.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_DIRS = ("docs/iec62304", "docs/iso13485", "docs/iso14971", "docs/iec62366", "docs/iec81001")

STANDARD_NAMES = {
    "iec62304": "IEC 62304:2006",
    "iso13485": "ISO 13485:2016",
    "iso14971": "ISO 14971:2019",
    "iec62366": "IEC 62366-1:2015",
    "iec81001": "IEC 81001-5-1:2021",
}

H1_RE = re.compile(r"^#\s+(.+)$", re.MULTILINE)
H2_RE = re.compile(r"^##\s+(.+)$", re.MULTILINE)
JSON_FENCE_RE = re.compile(r"```json\n(.*?)\n```", re.DOTALL)
ANY_FENCE_RE = re.compile(r"```.*?```", re.DOTALL)
POINTER_RE = re.compile(r"<!--\s*pointer:\s*(.+?)\s*-->", re.DOTALL)

# A clause number at the start of a heading, or anywhere in an H1 ("IEC 62304:2006 §9 - ...").
# Accepts a range ("§1-§3", "§6-§7") because several files cover more than one clause.
CLAUSE_IN_HEADING_RE = re.compile(r"§\s*(\d[\d.]*(?:\s*[–-]\s*§?\s*\d[\d.]*)?)")

# Any clause citation appearing in prose, used by the "every referenced clause is indexed"
# invariant. Matches both the citation-key form and the bare "§N.M" the modules use in text.
CLAUSE_CITATION_RE = re.compile(r"§\s*(\d+(?:\.\d+)*)")

NO_CLAUSE = "—"

# Issue #32's first invariant: an index row must have real content behind its link. "Real" here
# means prose a reader learns something from, counted after code fences and comments are removed
# so a JSON block's length cannot pad a section that says nothing. The threshold is low on
# purpose - "Device-level; no MduX mechanism." is a complete and useful answer, not a placeholder,
# and a corpus that had to pad such sections to reach a word count would be worse, not better.
MIN_SUBSTANTIVE_CHARS = 80


class PointerMissing(Exception):
    """A section yielded no pointer by any of the three routes.

    Raised rather than defaulted: a row whose pointer restates its title is the defect this
    generator was rewritten to remove, so producing one silently would reintroduce it.
    """


@dataclass(frozen=True)
class Row:
    clause: str
    title: str
    file_name: str
    anchor: str
    pointer: str
    justifications: tuple[str, ...]

    @property
    def link(self) -> str:
        return f"{self.file_name}#{self.anchor}"


def github_anchor(heading: str) -> str:
    """The anchor GitHub derives from a markdown heading.

    Lowercase; drop everything that is not alphanumeric, a space, a hyphen or an underscore
    (which is what removes `§`, `—` and punctuation); spaces to hyphens. Callers de-duplicate.
    """
    text = heading.strip().lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return re.sub(r"\s+", "-", text).strip("-")


def clause_of(heading: str) -> str | None:
    """The clause a heading names, normalized to `§N` / `§N.M` / `§N–§M`, or None."""
    match = CLAUSE_IN_HEADING_RE.search(heading)
    if not match:
        return None
    raw = re.sub(r"\s+", "", match.group(1))
    parts = re.split(r"[–-]", raw, maxsplit=1)
    if len(parts) == 2:
        return f"§{parts[0]}–§{parts[1].lstrip('§')}"
    return f"§{raw}"


def title_of(heading: str) -> str:
    """The heading reduced to what the Covers column shows.

    Drops the standard identifier and the clause number, both of which already have their own
    column or are implied by the corpus. `IEC 62304:2006 §9 — Problem resolution` becomes
    `Problem resolution`; this matters for the H1-fallback case, where a file with no H2 is
    indexed by its title heading and would otherwise carry the standard's name into every row.
    """
    stripped = heading.strip()
    for standard in STANDARD_NAMES.values():
        if stripped.startswith(standard):
            stripped = stripped[len(standard) :].strip()
            break
    stripped = CLAUSE_IN_HEADING_RE.sub("", stripped, count=1).strip()
    stripped = re.sub(r"^[–—-]\s*", "", stripped).strip()
    return stripped or heading.strip()


def sections_of(text: str) -> list[tuple[str, str]]:
    """Splits `text` into (heading, body) by H2.

    Falls back to one section keyed by the H1 when there is no H2 - the case for a file covering
    exactly one clause with no subdivisions.
    """
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


def file_clause(text: str) -> str | None:
    """The clause an unnumbered section inherits from its file's H1, if any.

    Only a file naming exactly one clause lends it. A file headed `§1–§3` covers three clauses,
    and a narrative section inside it - "Relationship to IEC 62304", say - is not all three of
    them; indexing it as `§1–§3` would assert a clause mapping nobody made. Single-clause files
    are the case this exists for: `07-problem-resolution-process.md` is headed `§9` and its H2s
    are unnumbered, and without inheritance every one of them indexes as no clause at all.
    """
    h1_match = H1_RE.search(text)
    if not h1_match:
        return None
    clause = clause_of(h1_match.group(1))
    return None if clause and "–" in clause else clause


def justification_objects(body: str) -> list[dict]:
    objects = []
    for block in JSON_FENCE_RE.findall(body):
        try:
            obj = json.loads(block)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict) and obj.get("justification_id"):
            objects.append(obj)
    return objects


def justification_ids(body: str) -> list[str]:
    return [obj["justification_id"] for obj in justification_objects(body)]


def prose_of(body: str) -> str:
    """The body reduced to prose: code fences, comments and sub-headings removed.

    Sub-headings go because a heading is not a sentence. Leaving them in glues an H3's title to
    the front of the sentence that follows it, which is how a pointer ends up reading as two
    fragments run together.
    """
    text = ANY_FENCE_RE.sub(" ", body)
    text = re.sub(r"<!--.*?-->", " ", text, flags=re.DOTALL)
    return re.sub(r"^#{1,6}\s+.*$", " ", text, flags=re.MULTILINE)


def first_sentence(text: str) -> str:
    """The first sentence of `text`, collapsed to one line.

    Abbreviations are not special-cased. The corpus uses none that would split wrongly, and a
    sentence splitter with an exception list is a maintenance burden for a cosmetic gain.
    """
    flat = re.sub(r"\s+", " ", text).strip()
    match = re.search(r"^(.+?[.!?])(?:\s|$)", flat)
    return (match.group(1) if match else flat).strip()


def pointer_for(heading: str, body: str) -> str:
    """One sentence naming what this clause points at, resolved in three steps.

    1. An authored `<!-- pointer: ... -->` comment - always wins, and is how a section says
       something the other two routes cannot derive.
    2. The first `Justification` rationale in the section. This names a real MduX mechanism by
       construction: mdux-docs-lint rejects a Justification whose evidence paths do not exist.
    3. The first sentence of the body mentioning MduX - which for this corpus is reliably the
       sentence that either names a mechanism or states that there is none.

    Raises PointerMissing when none applies.
    """
    authored = POINTER_RE.search(body)
    if authored:
        return re.sub(r"\s+", " ", authored.group(1)).strip()

    objects = justification_objects(body)
    if objects:
        rationale = objects[0].get("rationale", "").strip()
        if rationale:
            return first_sentence(rationale)

    flat = re.sub(r"\s+", " ", prose_of(body))
    for sentence in re.split(r"(?<=[.!?])\s+", flat):
        if is_usable_pointer(sentence):
            return sentence.strip()

    raise PointerMissing(heading)


# A sentence lifted out of a paragraph has to stand on its own in a table cell. These two rules
# are what stop the derived pointers reading badly: a table row picked up mid-section arrives as
# pipe-separated fragments that break the index's own table, and a sentence opening with a bare
# pronoun refers to an antecedent the reader cannot see from the index. A section whose only
# MduX-mentioning sentences fail both is exactly the case an authored
# `<!-- pointer: ... -->` comment exists for.
LEADING_PRONOUN_RE = re.compile(r"^(it|this|that|these|those|they|there)\b", re.IGNORECASE)


def is_usable_pointer(sentence: str) -> bool:
    text = sentence.strip()
    if "MduX" not in text or "|" in text:
        return False
    return not LEADING_PRONOUN_RE.match(text)


def is_substantive(body: str) -> bool:
    """Issue #32's first invariant: a row's section must have real content behind the link.

    A section carrying a `Justification` counts regardless of prose length: a Justification is
    checked content - mdux-docs-lint verifies its evidence paths exist - so a section built around
    one is the opposite of a placeholder.
    """
    if len(prose_of(body).strip()) >= MIN_SUBSTANTIVE_CHARS:
        return True
    return bool(justification_objects(body))


def module_paths(directory: Path) -> list[Path]:
    return [
        path
        for path in sorted(directory.glob("*.md"))
        if path.name not in ("README.md", "AI-Reference.md")
    ]


def build_rows(directory: Path) -> tuple[list[Row], list[str]]:
    """Returns (rows, problems). `problems` is non-empty when a section could not be indexed."""
    rows: list[Row] = []
    problems: list[str] = []

    for path in module_paths(directory):
        text = path.read_text(encoding="utf-8")
        inherited = file_clause(text)
        seen_anchors: dict[str, int] = {}

        for heading, body in sections_of(text):
            anchor = github_anchor(heading)
            # GitHub appends -1, -2 ... to the second and later headings sharing a slug.
            count = seen_anchors.get(anchor, 0)
            seen_anchors[anchor] = count + 1
            if count:
                anchor = f"{anchor}-{count}"

            clause = clause_of(heading) or inherited or NO_CLAUSE

            if not is_substantive(body):
                problems.append(
                    f"{path}: section '{heading}' has no substantive content behind it "
                    f"({len(prose_of(body).strip())} chars of prose); issue #32 forbids a row "
                    f"pointing at a stub"
                )
                continue

            try:
                pointer = pointer_for(heading, body)
            except PointerMissing:
                problems.append(
                    f"{path}: section '{heading}' yields no pointer. Add a "
                    f"<!-- pointer: ... --> comment under the heading, or a sentence naming "
                    f"what MduX does or does not provide for this clause"
                )
                continue

            rows.append(
                Row(
                    clause=clause,
                    title=title_of(heading),
                    file_name=path.name,
                    anchor=anchor,
                    pointer=pointer,
                    justifications=tuple(justification_ids(body)),
                )
            )

    return rows, problems


def referenced_clauses(directory: Path) -> set[str]:
    """Every clause number cited in this corpus's own modules, in headings or prose.

    Issue #32's second invariant: each must appear in the index. Only same-standard citations
    count - a module citing `ISO 14971:2019 §7` from inside docs/iec62304/ is pointing at another
    corpus, and requiring an index row for it here would be wrong.
    """
    standard = STANDARD_NAMES.get(directory.name)
    found: set[str] = set()
    for path in module_paths(directory):
        text = ANY_FENCE_RE.sub(" ", path.read_text(encoding="utf-8"))
        # Drop citations that name a different standard before scanning for bare clause numbers.
        for other in STANDARD_NAMES.values():
            if other != standard:
                text = re.sub(re.escape(other) + r"\s*§\s*\d+(?:\.\d+)*", " ", text)
        for match in CLAUSE_CITATION_RE.finditer(text):
            found.add(f"§{match.group(1)}")
    return found


def indexed_clauses(rows: list[Row]) -> set[str]:
    """The clauses an index covers, with ranges expanded and parents implied by their children.

    A row for §7.3 makes §7 indexed: a reader looking for §7 finds it. The reverse is not true,
    which is why a §7.4 cited in prose with no §7.4 row is a finding.
    """
    covered: set[str] = set()
    for row in rows:
        if row.clause == NO_CLAUSE:
            continue
        parts = [p.strip().lstrip("§") for p in row.clause.split("–")]
        if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
            for number in range(int(parts[0]), int(parts[1]) + 1):
                covered.add(f"§{number}")
            continue
        for part in parts:
            segments = part.split(".")
            for i in range(1, len(segments) + 1):
                covered.add("§" + ".".join(segments[:i]))
    return covered


def clause_sort_key(clause: str) -> list[int]:
    return [int(n) for n in clause.lstrip("§").split(".") if n.isdigit()]


def unindexed_clauses(directory: Path, rows: list[Row]) -> list[str]:
    missing = referenced_clauses(directory) - indexed_clauses(rows)
    return sorted(missing, key=clause_sort_key)


def display_path(directory: Path) -> str:
    """The path to print inside a generated index: `docs/<corpus>`, however it was invoked.

    The regenerate command is part of the file's bytes, so deriving it from the argument as given
    would make `--check` fail purely because CI passed an absolute path and a developer passed a
    relative one. Taking the last two components is enough - every corpus lives at docs/<name>.
    """
    return "/".join(directory.parts[-2:])


def render(standard_name: str, directory: Path, rows: list[Row]) -> str:
    lines = [
        f"# {standard_name} — per-clause index",
        "",
        "One row per clause section in this corpus: the clause, a one-sentence pointer to what",
        "MduX does or does not provide for it, and a deep link to the heading that covers it.",
        "Generated from the headings, `Justification` objects and pointer comments already",
        "present in the modules — not hand-transcribed, so it cannot drift from the prose it",
        "indexes without the source changing too.",
        "",
        "Regenerate after editing any file in this directory:",
        "",
        "```",
        f"python3 tools/docs-lint/generate_ai_reference.py {display_path(directory)}",
        "```",
        "",
        f"A clause shown as `{NO_CLAUSE}` is one this corpus deliberately does not number — see",
        "[`README.md`](README.md). It is not a gap in the index.",
        "",
        "| Clause | Covers | Pointer | Justification(s) |",
        "|---|---|---|---|",
    ]
    for row in rows:
        title = row.title.replace("|", "\\|")
        pointer = row.pointer.replace("|", "\\|")
        jids = ", ".join(row.justifications) if row.justifications else NO_CLAUSE
        lines.append(f"| {row.clause} | [{title}]({row.link}) | {pointer} | {jids} |")
    lines.append("")
    return "\n".join(lines)


def process(directory: Path, check_only: bool) -> tuple[bool, list[str]]:
    """Returns (ok, messages)."""
    standard_name = STANDARD_NAMES.get(directory.name, directory.name)
    rows, problems = build_rows(directory)
    problems.extend(
        f"{directory}: clause {clause} is cited in the prose but has no index row"
        for clause in unindexed_clauses(directory, rows)
    )
    if problems:
        return False, problems

    output = render(standard_name, directory, rows)
    target = directory / "AI-Reference.md"
    if check_only:
        current = target.read_text(encoding="utf-8") if target.exists() else None
        if current != output:
            return False, [f"{target} is out of date; run generate_ai_reference.py to regenerate"]
        return True, [f"{target} up to date ({len(rows)} rows)"]

    target.write_text(output, encoding="utf-8")
    return True, [f"wrote {target} ({len(rows)} rows)"]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="report out-of-date indexes without writing (what CI runs)",
    )
    parser.add_argument("directories", nargs="*", help="corpus directories; default is all five")
    args = parser.parse_args(argv)

    targets = (
        [Path(p) for p in args.directories]
        if args.directories
        else [Path(p) for p in DEFAULT_DIRS]
    )

    ok = True
    for directory in targets:
        if not directory.is_dir():
            print(f"skip: {directory} is not a directory", file=sys.stderr)
            continue
        succeeded, messages = process(directory, args.check)
        stream = sys.stdout if succeeded else sys.stderr
        for message in messages:
            print(message, file=stream)
        ok = ok and succeeded
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
