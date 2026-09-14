# Local MedUI previews and Studio

`mdux-preview` is an optional host service for inspecting and editing screens through the production
compiler and renderer. It serves the embedded MedUI Studio editor and, when configured, turns an
edit into a reviewable branch. It never merges. Build with `-DMDUX_BUILD_PREVIEW=ON`, then build target
`mdux-preview`. On the supported macOS tuple:

```sh
cmake --preset ninja-macos-clang -DMDUX_BUILD_PREVIEW=ON
cmake --build --preset ninja-macos-clang --target mdux-preview
```

Supply a file containing a high-entropy bearer token (32–256 non-space printable characters),
outside the repository root and readable only by your user. Start
`mdux-preview --root REPOSITORY --token-file FILE [--port PORT]`. It prints its
`http://127.0.0.1:PORT` address. The default port is dynamically assigned. Every `/api/` request needs
`Authorization: Bearer TOKEN`. Token files are not served. Do not put secrets in source recipes or
asset files. There is no remote bind option, CORS or write access to the served checkout.

## Studio

Open `http://127.0.0.1:PORT/#token=TOKEN` in Chrome, Edge, Firefox or Safari. The token travels in
the URL fragment, which browsers never send to a server; the page removes it from the address bar and
keeps it for the tab only. Without a fragment, the page asks for the token.

The Studio's four files are compiled into the executable (ADR-025): `/`, `/studio.js`, `/model.js` and
`/studio.css`. They need no token, carry a `default-src 'none'` Content-Security-Policy and contain
no repository data. The Host and Origin checks apply to them as to the API.

- **Screen and locale.** Pick a recipe and an approved locale. A source that does not parse cleanly is
  shown with its diagnostics and is not editable.
- **Fixture.** Every dynamic binding needs an explicit synthetic value. The Studio lists the required
  bindings with `null` placeholders; the service refuses them, and no frame is shown until you supply
  values. Frames are synthetic, never device readings.
- **Canvas.** Click a box to select it. Drag nodes that have a `position:`; resize with the handle when
  width and height are fixed pixel sizes; arrow keys nudge (Shift for 10 px); Delete removes the node,
  asking first when it carries safety annotations or a requirement.
- **Palette and inspector.** Components and fields come from the compiler's `grammar()`. A new node
  receives an id and geometry only, so the compiler reports its missing content fields. Required fields
  cannot be removed; a field outside the catalog is shown read-only and preserved.
- **Invalid edits.** While an edit does not compile, the last valid frame and boxes stay on screen,
  greyed and outlined, with the proposed geometry outlined in red. Invalid edits cannot be proposed.
- **Undo and redo.** Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z (or Ctrl+Y), or the toolbar buttons.
- **Versions.** The Studio refuses a catalog, IR or document schema version other than 1. A diagnostic
  code it does not recognise disables proposing until the Studio is updated.

## Proposals

Proposals are disabled unless the service is started with a repository it owns:

```sh
git clone --bare https://github.com/OWNER/REPO.git ~/mdux-proposals.git
git --git-dir ~/mdux-proposals.git config user.name "Your Name"
git --git-dir ~/mdux-proposals.git config user.email you@example.com
mdux-preview --root CHECKOUT --token-file FILE \
    --proposal-git-dir ~/mdux-proposals.git [--proposal-remote origin] [--proposal-pull-requests]
```

The directory must be outside `--root`. `git` must be on PATH; `gh` is needed only with
`--proposal-pull-requests`, and uses its own authentication. Git credentials come from the host's git
configuration; the service disables prompts.

Propose change opens a dialog for the issue number, base (`develop` or an issue branch), title, branch
slug and description. **Review** is a dry run: it compiles the edit and lists the canonical source,
comment loss and safety metadata changes. **Submit** needs acknowledgements for each, then:

1. checks the loaded source digest against the served file and against the file at the freshly fetched
   base;
2. commits the canonical source on the base in a temporary index of the service-owned repository;
3. pushes a new branch `<issue>-<slug>-<commit prefix>`, which cannot overwrite an existing branch;
4. with `--proposal-pull-requests`, opens a draft pull request, reporting a warning if that fails.

The served checkout is never written, indexed or given a ref. The Studio changes source only: re-bake
committed artifacts with `mdux-bake-update` on the branch before expecting CI to pass. While a
proposal runs, other requests receive `PRV007`.

## Version 1 endpoints

| Method / route | Input | Output |
|---|---|---|
| GET `/api/catalog` | none | grammar, fixture kinds, service, IR and document schema versions, `proposals` settings and limits |
| GET `/api/screens` | none | sorted repository-relative screen recipe identifiers |
| GET `/api/screens/detail?recipe=…` | discovered recipe identifier | source and resolved recipe options |
| POST `/api/document` | `schemaVersion: 1`, `recipe`, optional `source` | `document`, `documentSchemaVersion`, `sourceDigest`, `commentLines` |
| POST `/api/compile` | `schemaVersion: 1`, `recipe`, optional `source` or `document` | diagnostics, available IR, package, required bindings, source/package digests; `source` when a document was given |
| POST `/api/frame` | compile input plus `locale`, `fixture`, `clearColor` | compile result plus `pngBase64`, width, height, backend, fixture digest, `synthetic: true` |
| POST `/api/proposals` | see below | review, or the pushed `branch`, `commit`, `baseCommit`, `pullRequestUrl`, `warning` (201) |

JSON uses the existing MduX parser: integers are exact and floats are `{ "bits": UINT32 }`.
Unknown request keys or schema versions fail. Compiler diagnostics retain their existing envelopes
and codes. Failure responses carry the shared tool diagnostic envelope and no image. A source
failure may return IR if layout succeeded before a later compiler stage refused the screen.
Clients must refuse unknown grammar/IR/document versions and diagnostic meanings per ADR-023.

`locale` must name an approved text package. For a screen with no approved text packages it must
be explicitly `null`. All dependent assets must be committed packages under the configured root;
the service checks their canonical representation, identity and digests through existing loaders.

### Documents

A document is the unresolved AST: `{schemaVersion, name, layoutKind, layout, surface, nodes}`, where a
node is `{component, annotations, fields, children}`, an annotation `{name, arguments}`, a field
`{name, value}`, and a value one of `{kind: "Size", fill, pixels}`, `{kind: "Point", x, y}`,
`{kind: "String"|"TextKey"|"ImageRef"|"ColorToken"|"Identifier", text}`, `{kind: "Number", number}` or
`{kind: "List", items}`. Every member is required. Names must be `.medui` identifiers, and a document
whose text would reparse as a different screen is refused. `document` is offered only for a source
that parses with no diagnostics. Serialization is canonical and drops `//` comments (ADR-023).

### Proposal request

`schemaVersion: 1`, `recipe`, `document`, `baseSourceDigest` (the `sourceDigest` the document was loaded
with), `issue` (1–99999999), `slug` (lowercase words joined by single dashes, at most 60 characters),
`base` (`develop` or `<number>-<slug>`), `title` (one line, 1–200 bytes), `description` (at most 16000
bytes), `acknowledgeCommentLoss`, `acknowledgeSafetyChanges` and `dryRun`, all required. A review
returns `source`, `commentLoss`, `safetyChanges` (`nodeId`, `change` of `added`/`removed`/`changed`,
and the annotations and requirement `before` and `after`), `branchPrefix`, `base` and `writesEnabled`.

## Fixture shape

Only supply tables applicable to the compiled screen. Every required key must be present; extra
keys are rejected. The compile response lists `requiredBindings` with kind, key and node ID.

| Member | Keys | Value |
|---|---|---|
| `readings` | NumericDisplay node ID | signed fixed-point integer; template comes from recipe |
| `statuses` | StatusIndicator node ID | nonnegative state index |
| `fields` | TextInput node ID | `{text: UTF8_STRING, caret: CODEPOINT_INDEX_OR_NULL}` |
| `signals` | stream source | `{samples: [FLOAT32_BITS], minimum: FLOAT32_BITS, maximum: FLOAT32_BITS, strokeWidth: INTEGER}` |
| `viewports` | stream source | `{rows: [[FLOAT32_BITS]], bins: INTEGER, minimum: FLOAT32_BITS, maximum: FLOAT32_BITS, lowColor: RGBA, highColor: RGBA}` |
| `clock` | one screen-wide object | `{year, month, day, hour, minute, second, colorToken}`; all fields required |

RGBA is `{r, g, b, a}`, each an integer 0–255. `clearColor` uses the same shape. Clock dates must be
valid civil dates and the color token must be governed. Sample order is oldest first. Runtime limits
apply: at most 256 trace samples, and 16 rows × 32 bins per waterfall. Empty samples or rows are
explicit empty data, never automatically populated. Every returned frame represents fixtures,
not live device readings or evidence that application event handling was replayed.

## Limits and failures

Two HTTP workers accept at most eight queued connections. Only one request executes the backend
at once; overlap receives 503. Request bodies and source text are limited to 4 MiB; source nesting is limited to 64 levels and
65536 tokens before parsing. Request-owned file inputs are limited to
128 MiB, draw buffers to 64 MiB, and each surface dimension to 4096 pixels. Network
writes time out after five seconds of inactivity. Each connection accepts one request,
with a five-second total deadline shared by header and body reads. The 128 MiB limit covers
the authoritative input snapshot; compiler and loader working copies are additional memory. No server frame cache survives a request.
Proposal git commands time out after 30 seconds locally and 120 seconds for fetch, push and `gh`.

| Code | Meaning |
|---|---|
| PRV001 | malformed request, unknown route or schema version |
| PRV002 | invalid or unsupported fixture, binding, document or input |
| PRV003 | configured resource limit exceeded |
| PRV004 | Vulkan device unavailable |
| PRV005 | renderer or internal operation could not complete |
| PRV006 | input path refused |
| PRV007 | backend busy |
| PRV008 | proposal writes disabled |
| PRV009 | proposal base is stale: the served file or the base branch differs from the loaded source |
| PRV010 | comment loss not acknowledged |
| PRV011 | safety annotation or requirement change not acknowledged |
| PRV012 | git operation failed |

HTTP statuses distinguish malformed requests (400), authentication (401), path/origin refusal or
disabled writes (403), stale bases and missing acknowledgements (409), size limits (413),
source/state refusal (422), failed git operations (502), and unavailable/busy rendering (503).
Missing or unreadable inputs return 422 with a file-specific diagnostic; invalid source UTF-8
returns `MEDUI-E004`, including on the detail route. Recipe and source must be distinct files.
No-device failures are not successful previews. Connection-queue overflow may close the connection.
The local repository must not be modified by an adversarial process during a request; confinement
is not an operating-system sandbox. A GPU driver hang is not bounded by HTTP network timeouts.

## Tests

Run `ctest --test-dir build-macos-clang -L preview --no-tests=error --output-on-failure` after building
with previews enabled. The pixel case requires a working Vulkan device and is not skipped in CI.
`preview.proposals` needs `git` but no GPU, and runs on every leg that builds the service.

The no-device test runs on Linux and macOS. Elevated Windows runners ignore ICD environment
overrides, so Windows runs the contract and real-device pixel tests.

`ctest -L studio` is the bounded real-renderer browser smoke test: headless Chrome or Chromium drives
the Studio against a real service and GPU, edits, undoes, proposes and checks the pushed branch, with
a 300-second limit. CMake registers it when it finds a browser (override with
`-DMDUX_STUDIO_BROWSER=PATH`); the macOS, Windows and Linux Clang workflows assert it with
`--no-tests=error`. The GCC container legs have no browser and do not run it.
