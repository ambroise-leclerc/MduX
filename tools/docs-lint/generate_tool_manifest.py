#!/usr/bin/env python3
"""Generates the host-tool manifest from the tools' own build registration.

Host-only tool (ADR-004): standard library only, no third-party dependencies.

Issue #265 asks for "a tool manifest describing every host tool - its name, its inputs, its outputs
and its diagnostic codes - so an agent can discover the toolchain instead of being told about it",
and fixes how: "generated from the tools' own registration, not hand-written."

That constraint is the whole design. A hand-written manifest is a present-tense claim about what
exists, and the failure it has is the one #249 exists to catch: it stays true until somebody adds a
tool, and then it is a document that looks authoritative and is wrong. `mdux-named-mechanisms` would
not catch it either, because that check resolves the names a document *mentions* rather than
verifying a list is *complete* - a manifest missing a tool mentions nothing that fails to resolve.

## What is read, and why each source is the authority for what it carries

- **The tool list** comes from `add_executable(mdux-... )` in `tools/CMakeLists.txt`. That is where a
  host tool comes into existence; a tool absent from it does not get built, and one present in it
  cannot be missing from this manifest.
- **Inputs and outputs** come from the `mdux_bake_artifact()` call sites in the root
  `CMakeLists.txt`, which already name `TOOL`, `RECIPE`, `SOURCES` and `OUTPUTS` per artifact. The
  build uses those exact values to run the baker and to byte-compare what it produced, so nothing
  else in the tree is a better description of what a baker consumes and emits.
- **Diagnostic codes** come from the string literals in the tool's own source directory. Every code
  in this repository is a `PREFIX###` literal - `mdux.tools.medui.diagnostics` registers them in a
  table and the other tools spell them at the call site, which its own header comment records - so
  scanning the directory finds both arrangements without needing to know which a tool uses.
- **The command grammar** is read from the entry point: a tool calling `cli::parse()` speaks the
  shared `bake`/`verify` grammar, and one that does not has its own. Extra long options are the
  `--flag` literals in that file.

## What it deliberately does not record

No timestamp, no absolute path, no build identity and no version. ADR-007 decision 5's reasoning is
about any artifact a tool emits, not only baked ones: a manifest that changed byte for byte between
two runs of the same commit could not be diffed by the agent it exists to serve, and `--check` could
not be a CI gate at all.

Usage:
    python3 tools/docs-lint/generate_tool_manifest.py [--check] [--repo-root PATH]

`--check` regenerates in memory and fails when the committed manifest is out of date, writing
nothing. Without it the manifest is written.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

TOOLS_CMAKE = Path("tools/CMakeLists.txt")
ROOT_CMAKE = Path("CMakeLists.txt")
MANIFEST = Path("docs/tools/manifest.json")

SCHEMA_VERSION = 1

# `add_executable(mdux-shaderbake shader/ShaderBakeMain.cpp)`
EXECUTABLE_RE = re.compile(r"^add_executable\((?P<name>mdux-[\w-]+)\s+(?P<source>[^\s)]+)\s*\)", re.MULTILINE)

# One `mdux_bake_artifact(...)` call, captured whole so its keyword arguments can be read in order.
BAKE_RE = re.compile(r"^mdux_bake_artifact\((?P<body>.*?)^\)", re.DOTALL | re.MULTILINE)

# `mdux_compile_screen(...)` is a thin front for `mdux_bake_artifact()` that fixes the tool and the
# output set rather than taking them per call. Parsed as well, because a manifest reading only the
# direct calls would report that `mdux-meduic` bakes nothing - which is false, and false in the
# direction that matters: it is the tool whose artifact this repository's whole screen pipeline
# produces.
SCREEN_RE = re.compile(r"^mdux_compile_screen\((?P<body>.*?)^\)", re.DOTALL | re.MULTILINE)
SCREEN_CMAKE = Path("cmake/MduXCompileScreen.cmake")

# `target_link_libraries(mdux-meduic PRIVATE MduX::MeduiLib MduX_warnings)`
LINK_RE = re.compile(r"^target_link_libraries\((?P<tool>mdux-[\w-]+)\s+PRIVATE\s+(?P<libs>[^)]*)\)", re.MULTILINE)

# A published diagnostic code: an uppercase family, then three digits. `MEDUI-E030`, `TXT005`,
# `VUI101`, `MDC001`. Anchored to a whole string literal so a sentence mentioning one is not a
# registration.
CODE_RE = re.compile(r'"([A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*\d{3})"')

# A long option the tool accepts. `--format=json|text` in usage prose is not one, so the value part
# is dropped and the name kept.
OPTION_RE = re.compile(r'"(--[a-z][a-z-]*)(?:=|\\n|"| )')

SINGLE_VALUE_KEYWORDS = ("KIND", "ID", "TOOL", "RECIPE")
LIST_KEYWORDS = ("SOURCES", "OUTPUTS")


def parse_bake_call(body: str) -> dict:
    """One `mdux_bake_artifact()` call as a mapping, keeping list keywords as lists."""
    # Comments are stripped per line, not per token. `#` starts a comment that runs to the end of
    # the line, so filtering tokens that begin with `#` drops the marker and keeps every *word* of
    # the sentence after it - which quietly turned a six-file SOURCES list into a hundred and
    # thirty-four entries made mostly of English.
    lines = [line.split("#", 1)[0] for line in body.splitlines()]
    tokens = " ".join(lines).split()
    call: dict = {}
    key = None
    for token in tokens:
        if token in SINGLE_VALUE_KEYWORDS or token in LIST_KEYWORDS:
            key = token
            call[key] = [] if key in LIST_KEYWORDS else None
            continue
        if key is None:
            continue
        if key in LIST_KEYWORDS:
            call[key].append(token)
        else:
            call[key] = token
            key = None
    return call


def screen_outputs(root: Path) -> list[str]:
    """The output set `mdux_compile_screen()` fixes, read from the wrapper rather than restated.

    ADR-012 makes three of them unconditional and ADR-014 decision 4 adds the fourth, and the
    wrapper's own comment says it fixes them there so a call site cannot drop one. Reading them from
    it keeps this manifest following that decision instead of carrying a fifth copy of it.
    """
    # Anchored to the keyword at the start of its own line: the file's header comment *mentions*
    # OUTPUTS while explaining why the set is fixed here, and a search that took the first match
    # read that sentence instead of the block and reported a screen with no outputs at all.
    text = (root / SCREEN_CMAKE).read_text(encoding="utf-8")
    match = re.search(r"^\s*OUTPUTS\s*$(?P<body>.*?)^\s*\)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return []
    outputs = []
    for line in match.group("body").splitlines():
        token = line.strip()
        if token and not token.startswith("#"):
            outputs.append(token)
    return sorted(outputs)


def artifacts_by_tool(root: Path) -> dict[str, list[dict]]:
    """Every baked artifact, grouped by the tool that bakes it."""
    text = (root / ROOT_CMAKE).read_text(encoding="utf-8")
    grouped: dict[str, list[dict]] = {}

    def record(tool: str, call: dict, outputs: list[str], kind: str) -> None:
        grouped.setdefault(tool, []).append(
            {
                "kind": kind,
                "id": call.get("ID", ""),
                "recipe": call.get("RECIPE", ""),
                "sources": sorted(call.get("SOURCES", [])),
                "outputs": outputs,
            }
        )

    for match in BAKE_RE.finditer(text):
        call = parse_bake_call(match.group("body"))
        tool = call.get("TOOL")
        if tool is not None:
            record(tool, call, sorted(call.get("OUTPUTS", [])), call.get("KIND", ""))

    fixed = screen_outputs(root)
    for match in SCREEN_RE.finditer(text):
        record("mdux-meduic", parse_bake_call(match.group("body")), fixed, "screen")

    for artifacts in grouped.values():
        artifacts.sort(key=lambda entry: (entry["kind"], entry["id"]))
    return grouped


def libraries(root: Path) -> dict[str, list[str]]:
    """The MduX libraries each tool links, which is what decides the codes it can emit."""
    text = (root / TOOLS_CMAKE).read_text(encoding="utf-8")
    linked: dict[str, list[str]] = {}
    for match in LINK_RE.finditer(text):
        libs = [lib for lib in match.group("libs").split() if lib.startswith("MduX::")]
        linked[match.group("tool")] = sorted(libs)
    return linked


def diagnostic_codes(directory: Path) -> list[str]:
    """Every published code the tool's own sources spell, sorted and de-duplicated."""
    codes: set[str] = set()
    if not directory.is_dir():
        return []
    for path in sorted(directory.rglob("*")):
        if path.suffix not in {".cpp", ".cppm"}:
            continue
        codes.update(CODE_RE.findall(path.read_text(encoding="utf-8")))
    return sorted(codes)


def options(entry_point: Path, shared: list[str]) -> list[str]:
    """The long options this tool accepts: its own, plus the shared grammar's when it speaks it.

    A tool calling `cli::parse()` accepts `--format` and `--help` without naming either in its own
    source, because both are spelled in `tools/common/Cli.cpp`. A manifest listing `--dump-ir` for
    `mdux-meduic` and not `--format` would be accurate about the file and wrong about the tool.
    """
    if not entry_point.is_file():
        return []
    own = set(OPTION_RE.findall(entry_point.read_text(encoding="utf-8")))
    return sorted(own | set(shared))


def build_manifest(root: Path) -> dict:
    tools_text = (root / TOOLS_CMAKE).read_text(encoding="utf-8")
    baked = artifacts_by_tool(root)
    linked = libraries(root)
    shared_options = sorted(set(OPTION_RE.findall((root / "tools/common/Cli.cpp").read_text(encoding="utf-8"))))

    entries = []
    for match in EXECUTABLE_RE.finditer(tools_text):
        name = match.group("name")
        source = match.group("source")
        entry_point = root / "tools" / source
        directory = entry_point.parent

        text = entry_point.read_text(encoding="utf-8") if entry_point.is_file() else ""
        # A tool speaking the shared grammar takes `bake <recipe> <output-dir>` and
        # `verify <recipe> <package> <report>`; one that does not documents its own.
        speaks_shared = "cli::parse(" in text
        entry = {
            "name": name,
            "entryPoint": f"tools/{source}",
            "grammar": "shared-bake-verify" if speaks_shared else "tool-specific",
            "options": options(entry_point, shared_options if speaks_shared else []),
            "libraries": linked.get(name, []),
            # The codes this tool's *family* publishes, not the subset one executable happens to
            # reach. Three tools live in `tools/medui/` and link one `MduX::MeduiLib`, so they share
            # a code family - and attributing a code to the library that defines it is exact where
            # attributing it to an executable would be a guess about which call sites run.
            "diagnosticCodes": diagnostic_codes(directory),
            "sourceDirectory": f"tools/{Path(source).parent.as_posix()}",
            "bakes": baked.get(name, []),
        }
        entries.append(entry)

    entries.sort(key=lambda entry: entry["name"])
    return {"schemaVersion": SCHEMA_VERSION, "tools": entries}


def render(manifest: dict) -> str:
    """Canonical form: sorted keys, two-space indent, LF, trailing newline.

    The same shape `mdux.evidence.json` writes, so a consumer reading a baked artifact and this
    manifest needs one reader rather than two.
    """
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate in memory and fail when the committed manifest is out of date",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root (default: inferred from this script's location)",
    )
    args = parser.parse_args(argv)
    root: Path = args.repo_root

    rendered = render(build_manifest(root))
    path = root / MANIFEST

    if args.check:
        if not path.is_file():
            print(f"mdux-tool-manifest: {MANIFEST} is not committed", file=sys.stderr)
            return 1
        committed = path.read_text(encoding="utf-8")
        if committed != rendered:
            print(
                f"mdux-tool-manifest: {MANIFEST} is out of date. Regenerate it with\n"
                f"  python3 tools/docs-lint/generate_tool_manifest.py",
                file=sys.stderr,
            )
            return 1
        count = len(json.loads(committed)["tools"])
        print(f"mdux-tool-manifest: OK ({count} tools, manifest current)")
        return 0

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(rendered, encoding="utf-8")
    print(f"mdux-tool-manifest: wrote {MANIFEST} ({len(json.loads(rendered)['tools'])} tools)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
