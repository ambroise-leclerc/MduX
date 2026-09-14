# ADR-024: Local native preview service

## Status

**Proposed**, 2026-09-13. Implements issue #326 after #325, #316 and #323.
Maintainer engineering and preview-fidelity review remain open.

## Context

An author needs to see the production renderer's interpretation of unsaved source. A separately
painted editor approximation would obscure binding errors and could imply that missing live data
was a measured reading. ADR-023 supplies the grammar, diagnostic and compile contracts.

## Decision

Provide an opt-in host-only `mdux-preview` HTTP service, using pinned cpp-httplib. The HTTP transport
is a C++17 object target with no imports of governed modules. This isolates textual third-party
headers from the C++23 module graph and avoids the LLVM 21/macOS SDK `<random>` header conflict.
The preview library and all compiler/render code remain C++23; this does not change supported
compiler versions. No HTTP dependency enters an installed device library. The transport uses
cpp-httplib in header-only mode to wrap its pinned `SocketStream` read-deadline API. Each
connection serves one request with a five-second total header/body deadline, including reads
before authentication; the deadline does not limit backend execution or response writes.
The contract test occupies both workers with trickled input and checks recovery.

Each request owns an input snapshot. The compiler and authenticated artifact loaders accept an
optional `InputReader`; existing callers retain their previous reads. The service confines every
read, checks input sizes before allocation and caches the bytes it read. Unsaved source overlays
only the recipe's source in memory. There is no write endpoint or call to a baker's write function.
A locally edited repository is trusted against concurrent hostile filesystem replacement; this is
not a sandbox for an adversarial local process. Symlinks, traversal and canonical path escapes are
refused. A caller changing a normal input between requests receives a new snapshot.

Compile all approved locales through the existing compiler. Render the explicitly selected locale
using authenticated packages and `TextBinding`. Require explicit fixture entries for every dynamic
binding; no clock or sample generator runs. Fixtures go through `ReadingBinding`, `StatusBinding`,
`TextInputBinding`, `SignalBinding`, `ViewportBinding`, then `medui::render()`, `UiRenderer` and
`OffscreenTarget`. The host supports the runtime's waterfall viewport interpretation and one image
atlas; unsupported binding arrangements fail instead of producing incomplete frames.

A fixture frame describes synthetic state, not application interaction replay or a device reading.
Return that distinction with the frame, its input digests and backend identity. No shared MedUI
observation profile or verification claim is added. PAR-REQ-009/010 remain prospective; preview
pixels support author inspection and do not discharge scenario replay obligations.

Serve loopback only with a token-file bearer credential, strict Host/Origin checks and no CORS.
Bound request bodies, queued connections, input bytes, surface extent and draw storage. Only one
compile/render request executes at once. All request and GPU owners unwind after readback or error;
there is no persistent screen cache or permanent allocation per edit. GPU execution uses existing
runtime synchronization; network timeouts are not a promise to interrupt a stalled GPU driver.

## Medical Device Considerations

Impact: **potentially safety-relevant preview fidelity and authored-source integrity**. The affected
prospective requirements are PAR-REQ-009/010. The identified risk is a preview silently differing
from production rendering or presenting absent values as readings. The controls are the shared
render path, complete explicit fixture bindings, refusal diagnostics, input identity, and pixel
correspondence tests. Source, annotations, requirement strings, and committed artifacts are read
without modification. No device runtime semantics change.

Verification lives in `tests/preview/`: HTTP contract and refusal tests, unavailable-device tests,
repeated locale previews and pixel tests. Existing compiler evidence checks cover preservation of
baker output when the optional reader is not used. This is experimental infrastructure and makes
no certification, clinical validation or production-readiness claim.

## Alternatives and consequences

A Python wrapper was considered; the user selected native C++ HTTP. A bespoke HTTP parser would
create avoidable protocol maintenance. Monitor-only fixtures were considered; generic explicit
fixtures were selected so edited screen bindings cannot silently inherit demo values.

Per-request compilation and GPU construction cost latency but make ownership explicit. Stateless
PNG responses cost bandwidth but keep stale frames out of server state. Remote deployment, Studio
assets, editing/saving and proposals remain outside #326. Those consumers must follow ADR-023's
schema negotiation and comment-loss requirements.

## References

- [ADR-023](ADR-023-medui-host-editing-api-and-round-trip-source-contract.md)
- [Preview protocol](../tools/preview.md)
- [Prospective requirements](../parity/requirements.md)
- [SOUP inventory](../governance/soup-register.toml)
