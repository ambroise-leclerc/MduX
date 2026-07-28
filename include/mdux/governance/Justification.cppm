/**
 * @file Justification.cppm
 * @brief Governed-zone type for a `Justification` object: the machine-readable form of a
 *        citation from docs/governance/citation-convention.md.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 *
 * Part of MduXCore. Mirrors docs/governance/schemas/justification.schema.json field-for-field,
 * so a `Justification` built in C++ and one hand-written in a markdown fence validate against
 * the same rules - this module is the mechanical check that schema states as a JSON Schema
 * document; this is its C++ counterpart, usable at build time rather than only by an external
 * JSON Schema validator.
 *
 * This is the type issue #8's regulatory corpus already emits by hand in fenced ```json blocks.
 * Issue #35 (traceability matrix export) reads every such block back into this type to build a
 * cross-corpus matrix; this module is only the type and its validation, not the export.
 */
module;

export module mdux.governance;

import std;
import mdux.core.result;
import mdux.evidence.json;

export namespace mdux::governance {

/// The five standards this project cites against - the same closed set as
/// docs/governance/citation-convention.md and the JSON Schema. Kept as a compile-time array
/// (rather than an enum) so the exact string a `standard` field must equal is defined once and
/// used identically for validation and for building the field's value.
inline constexpr std::array<std::string_view, 5> kApprovedStandards{
    "IEC 62304:2006", "ISO 13485:2016", "ISO 14971:2019", "IEC 62366-1:2015",
    "IEC 81001-5-1:2021"};

[[nodiscard]] bool isApprovedStandard(std::string_view standard) noexcept;

enum class GovernanceError : std::uint8_t {
    EmptyJustificationId,
    MalformedJustificationId,     ///< does not match JUS-NNN (three or more digits)
    UnapprovedStandard,
    EmptyClauseRef,
    MalformedClauseRef,           ///< does not match "<Standard> §<clause> <title>"
    ClauseRefStandardMismatch,    ///< clause_ref names a different standard than `standard`
    EmptyRationale,
    EmptyEvidenceRefs,
    EmptyEvidenceRef,
    DuplicateEvidenceRef,
    MalformedJustification,       ///< parsed JSON did not have the expected shape
};

[[nodiscard]] std::string_view describe(GovernanceError error) noexcept;

/**
 * @brief The machine-readable form of a citation from
 *        docs/governance/citation-convention.md.
 *
 * Field names and constraints mirror docs/governance/schemas/justification.schema.json exactly:
 * `justificationId` (`JUS-NNN`, unique across the whole corpus - this type does not itself
 * enforce uniqueness, since that is a property of a *collection* of Justifications, not of one),
 * `standard` (one of `kApprovedStandards`), `clauseRef` (must start with the same standard),
 * `rationale` (original prose - this type cannot check that a string isn't reproduced text; see
 * mdux-docs-lint for the heuristic that runs over the corpus instead), `requirementId` (optional,
 * `REQ-*`), and `evidenceRefs` (non-empty strings with no duplicates; paths are not checked to
 * exist here because that is a lint-time concern over a real repository checkout).
 */
struct Justification {
    std::string justificationId;
    std::string standard;
    std::string clauseRef;
    std::string rationale;
    std::optional<std::string> requirementId;
    std::vector<std::string> evidenceRefs;

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, GovernanceError> write() const noexcept;

    /// Parses a single Justification object. Strict: rejects a malformed shape and validates
    /// the result before returning it.
    [[nodiscard]] static mdux::core::Result<Justification, GovernanceError> parse(
        std::string_view text) noexcept;
};

}  // namespace mdux::governance
