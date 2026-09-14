/**
 * @file Document.cppm
 * @brief A versioned JSON view of the unresolved `.medui` AST, for host editing tools.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-023 MedUI host editing API and round-trip source contract
 * @compliance ADR-025 MedUI Studio editing and reviewable change proposals
 *
 * Host-only. ADR-023 decision 1 rejected a parallel DTO type family and predicted that "a future JSON
 * view of an `ast::Screen` can be built the same way, from the AST directly". This is that view, and
 * its consumer is `mdux-preview`'s Studio (#327): a browser edits the document, and the service turns
 * it back into `.medui` source with `serializeScreen()` before the compiler sees anything.
 *
 * ## The document is the AST's own shape, not a component model
 *
 * Nodes carry a component name, annotations, fields and children; fields carry a name and a value of
 * one of `ast::ValueKind`'s nine kinds. Nothing here knows which fields a `Label` needs. That is
 * deliberate and is the same boundary `Ast.cppm` draws: an editor that could only express the fields
 * a dictionary names would silently drop the ones it does not (ADR-023 decision 4), and whether a
 * field is required is the compiler's decision, published in `grammar()` and enforced by semantic
 * analysis rather than by this reader.
 *
 * ## What reading refuses
 *
 * Shape only: unknown or missing members, a value of the wrong JSON kind, a negative or oversized
 * integer, and a name that is not a `.medui` identifier. The name check is not cosmetic. A field
 * called `a; requirement` would otherwise serialize into two fields, so a document could say one
 * thing and compile as another. `sourceFromDocument()` then proves the stronger property directly:
 * the text it returns reparses to exactly the screen the document described, or it refuses.
 *
 * ## Versioning
 *
 * `documentSchemaVersion` follows ADR-023 decision 2's discipline for the catalog and IR: bumped when
 * a member is added, removed or reshaped, and a reader refuses a version it was not built for.
 */
module;

export module mdux.tools.medui.document;

import std;
import mdux.evidence.json;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

/// The schema version of `screenDocument()`'s output and `readScreenDocument()`'s input.
inline constexpr std::uint64_t documentSchemaVersion = 1;

/**
 * @brief The JSON document for `screen`.
 *
 * Positions are omitted: an edited document has no source positions worth keeping, and the compiler
 * reports diagnostics against the canonical text the document serializes to.
 */
[[nodiscard]] mdux::evidence::json::Value screenDocument(const ast::Screen& screen);

/**
 * @brief Reads a document back into an AST, checking its shape and nothing else.
 * @return the screen, or a message naming the offending member path
 */
[[nodiscard]] std::expected<ast::Screen, std::string> readScreenDocument(const mdux::evidence::json::Value& document);

/**
 * @brief Serializes a document to canonical `.medui` source, proving the round trip.
 *
 * Refuses a document whose serialized text parses cleanly into a *different* screen. A document whose
 * text does not parse cleanly at all is returned, not refused: its diagnostics are the compiler's to
 * report against that text, which is how an editor shows an author why an edit is invalid.
 */
[[nodiscard]] std::expected<std::string, std::string> sourceFromDocument(const mdux::evidence::json::Value& document);

/// Structural equality over everything the AST carries except source positions.
[[nodiscard]] bool sameScreen(const ast::Screen& a, const ast::Screen& b);

/**
 * @brief True when `source` contains a `//` comment, which canonical serialization discards.
 *
 * Mirrors `Lexer.cpp`: a `//` inside a string literal is not a comment. ADR-023 decision 3 requires a
 * tool that writes source back to detect this loss and obtain explicit acknowledgment before incurring
 * it.
 */
[[nodiscard]] bool containsComments(std::string_view source) noexcept;

/// One node whose authored safety metadata differs between two screens.
struct SafetyChange {
    std::string nodeId;  ///< the node's `id:`, or a path when it has none
    std::string change;  ///< `added`, `removed` or `changed`
    std::string before;  ///< canonical JSON of the annotations and `requirement:`; empty when absent
    std::string after;   ///< as `before`, for the edited screen
};

/**
 * @brief Nodes whose annotations or `requirement:` field were added, removed or changed.
 *
 * The metadata PAR-REQ-010 and ADR-023 decision 4 say an edit must never silently discard. Sorted by
 * node id, so two calls over the same screens agree.
 */
[[nodiscard]] std::vector<SafetyChange> safetyChanges(const ast::Screen& before, const ast::Screen& after);

}  // namespace mdux::tools::medui
