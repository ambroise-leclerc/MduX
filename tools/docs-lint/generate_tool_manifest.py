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
  `mdux_compile_screen()` supplies three of those itself - the `.medui` the recipe names, the
  committed packages the render half reads, and `THEN_TOOLS` - so the wrapper is read as well as the
  call site. Reading only the call site described a screen built by one tool from six files, when it
  is built by two in sequence from nine.
- **Diagnostic codes** come from the string literals in the tool's own source directory. Every code
  in this repository is a `PREFIX###` literal - `mdux.tools.medui.diagnostics` registers them in a
  table and the other tools spell them at the call site, which its own header comment records - so
  scanning the directory finds both arrangements without needing to know which a tool uses.
- **The command grammar** is read from the entry point: a tool calling `cli::parse()` speaks the
  shared `bake`/`verify` grammar, and one that does not has its own. Its options are the `--flag`
  literals in that file, plus the shared grammar's when it speaks it, plus those of a parser it
  delegates to - following the *call* rather than the import, because two tools may import one
  driver and only one of them hand it the command line.

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


def screen_wrapper(root: Path) -> dict:
    """What `mdux_compile_screen()` supplies that a call site does not: tools, inputs and outputs.

    Read from the wrapper rather than restated, because the wrapper is where each of them is *fixed*
    - ADR-012 makes three outputs unconditional and ADR-014 decision 4 adds the fourth, and the
    wrapper's own comment says they sit there so that a call site cannot drop one. A manifest that
    listed only what a call site writes described a screen built by a single tool from six files,
    when it is built by two tools in sequence from nine.
    """
    text = (root / SCREEN_CMAKE).read_text(encoding="utf-8")

    def block(keyword: str) -> list[str]:
        # Anchored to the keyword on its own line: the file's header comment *mentions* OUTPUTS
        # while explaining why the set is fixed there, and a search taking the first match read
        # that sentence instead of the block.
        match = re.search(rf"^\s*{keyword}\s*$(?P<body>.*?)^\s*(?:[A-Z_]+\s*$|\))", text, re.DOTALL | re.MULTILINE)
        if not match:
            return []
        found = []
        for line in match.group("body").splitlines():
            token = line.strip()
            if token and not token.startswith("#") and not token.startswith("$"):
                found.append(token)
        return found

    then = re.search(r"^\s*THEN_TOOLS\s+(?P<tools>[\w\s-]+?)$", text, re.MULTILINE)
    return {
        "thenTools": then.group("tools").split() if then else [],
        # The literal paths the wrapper appends to every screen's SOURCES, beside the `${...}`
        # expansions it also adds - those are the call site's own list and the recipe's source,
        # both resolved separately below.
        "sources": sorted(block("SOURCES")),
        "outputs": sorted(block("OUTPUTS")),
    }


def recipe_source(root: Path, recipe: str) -> list[str]:
    """The `.medui` file a screen recipe names, which the wrapper reads and adds as an input.

    Not in any call site: `_mdux_screen_check_recipe()` parses it out of the recipe and appends it,
    which is why the first draft of this manifest listed a screen's font and text packages and not
    the screen.
    """
    path = root / recipe
    if not path.is_file():
        return []
    match = re.search(r'^\s*source\s*=\s*"([^"]+)"', path.read_text(encoding="utf-8"), re.MULTILINE)
    return [match.group(1)] if match else []


def committed_matches(path: Path, rendered: str) -> bool:
    """Whether the committed file *is* what `render()` produced - a byte comparison, deliberately.

    `read_text()` is not one. Python translates CRLF to LF on read whatever the platform it runs on,
    so a manifest written in text mode on Windows - where `write_text()` turns every LF into CRLF -
    reads back equal to the LF text it was rendered from, and the freshness gate reports a file it
    would not itself have written as current. `docs/tools/** -text` then carries those bytes verbatim
    through every checkout, so the three toolchain legs would agree on a manifest none of them
    renders. The gate compares bytes or it checks nothing.
    """
    return path.read_bytes() == rendered.encode("utf-8")


def artifacts_by_tool(root: Path) -> dict[str, list[dict]]:
    """Every baked artifact, grouped by the tool that bakes it."""
    text = (root / ROOT_CMAKE).read_text(encoding="utf-8")
    grouped: dict[str, list[dict]] = {}

    def record(tool: str, call: dict, outputs: list[str], kind: str, toolchain: list[str] | None = None) -> None:
        grouped.setdefault(tool, []).append(
            {
                "kind": kind,
                "id": call.get("ID", ""),
                "recipe": call.get("RECIPE", ""),
                "sources": sorted(call.get("SOURCES", [])),
                "outputs": outputs,
                # Every tool that runs, in order. One entry for a baker that produces its artifact
                # alone; two for a screen, which `mdux-meduic` compiles and `mdux-verify-bake` then
                # renders and extends. A consumer asking "how do I produce this bundle" reads this.
                "toolchain": toolchain if toolchain is not None else [tool],
            }
        )

    for match in BAKE_RE.finditer(text):
        call = parse_bake_call(match.group("body"))
        tool = call.get("TOOL")
        if tool is not None:
            record(tool, call, sorted(call.get("OUTPUTS", [])), call.get("KIND", ""))

    wrapper = screen_wrapper(root)
    for match in SCREEN_RE.finditer(text):
        call = parse_bake_call(match.group("body"))
        recipe = call.get("RECIPE", "")
        # Everything the screen is actually built from: the call site's list, the `.medui` the
        # recipe names, and the committed packages the wrapper appends for the render half.
        call["SOURCES"] = sorted(set(call.get("SOURCES", [])) | set(wrapper["sources"]) | set(recipe_source(root, recipe)))
        # Both tools, in the order they run. A bundle produced by `mdux-meduic` alone is missing
        # `verification.json`, so a manifest naming one tool would tell a consumer how to produce
        # three quarters of an artifact - see `toolchain` on the entry.
        record("mdux-meduic", call, wrapper["outputs"], "screen", ["mdux-meduic", *wrapper["thenTools"]])
        for follower in wrapper["thenTools"]:
            record(follower, call, wrapper["outputs"], "screen", ["mdux-meduic", *wrapper["thenTools"]])

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


# `invocation = verify::parseArguments(argc, argv);` - a tool delegating its command line to a
# parser that lives in another translation unit.
DELEGATED_PARSER_RE = re.compile(r"\b(?:\w+::)?parseArguments\s*\(")
PARSER_DEFINITION_RE = re.compile(r"^\w[\w:<>, ]*\s+parseArguments\(", re.MULTILINE)


def delegated_options(entry_point: Path) -> set[str]:
    """The options of the parser this tool delegates to, when it delegates to one.

    Following the *call* rather than the import, which is the distinction that makes this exact.
    `VerifyUiMain.cpp` and `VerifyBakeMain.cpp` both import `mdux.tools.verify.driver`, and only the
    first hands it the command line - the second parses inline and accepts `--help` alone. An
    import-following rule would have credited `mdux-verify-bake` with `--screen` and `--locales`,
    which it rejects.

    The search is the tool's own directory: a parser a tool calls and a tool that calls it are in
    one family by construction, and widening it further would start attributing a sibling tool's
    flags.
    """
    text = entry_point.read_text(encoding="utf-8")
    if not DELEGATED_PARSER_RE.search(text):
        return set()
    found: set[str] = set()
    for path in sorted(entry_point.parent.rglob("*.cpp")):
        if path == entry_point:
            continue
        source = path.read_text(encoding="utf-8")
        if PARSER_DEFINITION_RE.search(source):
            found.update(OPTION_RE.findall(source))
    return found


def options(entry_point: Path, shared: list[str]) -> list[str]:
    """The long options this tool accepts: its own, its delegated parser's, and the shared grammar's.

    A tool calling `cli::parse()` accepts `--format` and `--help` without naming either in its own
    source, because both are spelled in `tools/common/Cli.cpp`. A manifest listing `--dump-ir` for
    `mdux-meduic` and not `--format` would be accurate about the file and wrong about the tool - and
    a tool whose whole command line is parsed elsewhere, as `mdux-verify-ui`'s is in `Driver.cpp`,
    would come out with no options at all.
    """
    if not entry_point.is_file():
        return []
    own = set(OPTION_RE.findall(entry_point.read_text(encoding="utf-8")))
    return sorted(own | delegated_options(entry_point) | set(shared))


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
        if not committed_matches(path, rendered):
            print(
                f"mdux-tool-manifest: {MANIFEST} is out of date. Regenerate it with\n"
                f"  python3 tools/docs-lint/generate_tool_manifest.py",
                file=sys.stderr,
            )
            return 1
        count = len(json.loads(rendered)["tools"])
        print(f"mdux-tool-manifest: OK ({count} tools, manifest current)")
        return 0

    path.parent.mkdir(parents=True, exist_ok=True)
    # Bytes for the reason `committed_matches()` gives: text mode would write CRLF on Windows, and
    # `-text` would then keep it. What is rendered is what is written, on every platform.
    path.write_bytes(rendered.encode("utf-8"))
    print(f"mdux-tool-manifest: wrote {MANIFEST} ({len(json.loads(rendered)['tools'])} tools)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
