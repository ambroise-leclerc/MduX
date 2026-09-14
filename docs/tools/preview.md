# Local MedUI previews

`mdux-preview` is an optional host service for inspecting saved or unsaved screens through the
production renderer. Build with `-DMDUX_BUILD_PREVIEW=ON`, then build target `mdux-preview`.
On the supported macOS tuple:

```sh
cmake --preset ninja-macos-clang -DMDUX_BUILD_PREVIEW=ON
cmake --build --preset ninja-macos-clang --target mdux-preview
```

Supply a file containing a high-entropy bearer token (32–256 non-space printable characters),
outside the repository root and readable only by your user. Start `mdux-preview --root REPOSITORY --token-file FILE [--port PORT]`.
It prints its `http://127.0.0.1:PORT` address. The default port is dynamically assigned. Every
request needs `Authorization: Bearer TOKEN`. Token files are not served. Do not put secrets in
source recipes or asset files. There is no remote bind option, CORS, static hosting or write API.
A browser consumer must use the same origin; #327 owns frontend integration.

## Version 1 endpoints

| Method / route | Input | Output |
|---|---|---|
| GET `/api/catalog` | none | grammar, fixture kinds, service schema and limits |
| GET `/api/screens` | none | sorted repository-relative screen recipe identifiers |
| GET `/api/screens/detail?recipe=…` | discovered recipe identifier | source and resolved recipe options |
| POST `/api/compile` | `schemaVersion: 1`, `recipe`, optional `source` | diagnostics, available IR, package, required bindings, source/package digests |
| POST `/api/frame` | compile input plus `locale`, `fixture`, `clearColor` | compile result plus `pngBase64`, width, height, backend, fixture digest, `synthetic: true` |

JSON uses the existing MduX parser: integers are exact and floats are `{ "bits": UINT32 }`.
Unknown request keys or schema versions fail. Compiler diagnostics retain their existing envelopes
and codes. Failure responses carry the shared tool diagnostic envelope and no image. A source
failure may return IR if layout succeeded before a later compiler stage refused the screen.
Clients must refuse unknown grammar/IR versions and diagnostic meanings per ADR-023.

`locale` must name an approved text package. For a screen with no approved text packages it must
be explicitly `null`. All dependent assets must be committed packages under the configured root;
the service checks their canonical representation, identity and digests through existing loaders.

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

| Code | Meaning |
|---|---|
| PRV001 | malformed request, unknown route or schema version |
| PRV002 | invalid or unsupported fixture, binding or input |
| PRV003 | configured resource limit exceeded |
| PRV004 | Vulkan device unavailable |
| PRV005 | renderer or internal operation could not complete |
| PRV006 | input path refused |
| PRV007 | backend busy |

HTTP statuses distinguish malformed requests (400), authentication (401), path/origin refusal
(403), size limits (413), source/state refusal (422), and unavailable/busy rendering (503).
Missing or unreadable inputs return 422 with a file-specific diagnostic; invalid source UTF-8
returns `MEDUI-E004`, including on the detail route. Recipe and source must be distinct files.
No-device failures are not successful previews. Connection-queue overflow may close the connection.
The local repository must not be modified by an adversarial process during a request; confinement
is not an operating-system sandbox. A GPU driver hang is not bounded by HTTP network timeouts.

Run `ctest --test-dir build-macos-clang -L preview --no-tests=error --output-on-failure` after building
with previews enabled. The pixel case requires a working Vulkan device and is not skipped in CI.

The no-device test runs on Linux and macOS. Elevated Windows runners ignore ICD environment
overrides, so Windows runs the contract and real-device pixel tests.
