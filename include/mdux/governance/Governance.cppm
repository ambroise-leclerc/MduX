/**
 * @file Governance.cppm
 * @brief Governed-zone compliance model: the types a release gate checks before a build is
 *        allowed to claim it satisfies anything.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 *
 * Part of MduXCore. Two groups of types live here:
 *
 * - `Justification` - the machine-readable form of a citation from
 *   docs/governance/citation-convention.md. It mirrors
 *   docs/governance/schemas/justification.schema.json field-for-field, so a Justification built
 *   in C++ and one hand-written in a markdown fence validate against the same rules. This is the
 *   mechanical check that schema states as a JSON Schema document; usable at build time rather
 *   than only by an external JSON Schema validator.
 *
 * - `Requirement`, `Hazard`, `VerificationCase`, `ProblemReport`, `AuditEvent` and the
 *   `ComplianceProgram` that aggregates them - the lifecycle records IEC 62304 and ISO 14971
 *   require a manufacturer to keep, and the cross-references between them that can actually be
 *   machine-checked.
 *
 * ## What `ComplianceProgram::validate()` is for
 *
 * It is the function a release gate calls. Two rules in it are the reason the whole type exists:
 *
 * - **every `Requirement` is covered by at least one `VerificationCase`** - IEC 62304 §5.2.4's
 *   "verify software requirements", reduced to something a build can fail on;
 * - **every `Hazard` names at least one control, and every control resolves to a real
 *   `Requirement`** - the ISO 14971 §7 / IEC 62304 §4.2 join. A hazard whose control is a
 *   dangling id is the exact failure this check exists to catch, and it is invisible to any
 *   per-object validate().
 *
 * It returns the *whole* list of failures rather than the first one, and each failure names the
 * record it is about. An unverified requirement and a hazard with no control are different
 * problems; a gate that reports one at a time turns a release review into a guessing game.
 *
 * ## What is deliberately not checked here
 *
 * This module checks *structure*, never *adequacy*. That a `VerificationCase` exists says
 * nothing about whether the verification was any good, and that a `Hazard` names a control says
 * nothing about whether the control reduces risk acceptably. Those are review judgements owed by
 * the integrating manufacturer under its own quality system; see docs/regulatory-compliance.md
 * for the scope limits this project claims. Nor is a `SafetyClass` a classification MduX can
 * make on a manufacturer's behalf - it is recorded here as a declared input to the program, not
 * as a conclusion (see docs/iec62304/01-scope-and-terms.md).
 *
 * Field names and semantics match the JSON Schemas under docs/<standard>/schemas/; the two are
 * checked against each other by tools/docs-lint/check_schema_type_drift.py.
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
    // Justification
    EmptyJustificationId,
    MalformedJustificationId,      ///< does not match JUS-NNN (three or more digits)
    UnapprovedStandard,
    EmptyClauseRef,
    MalformedClauseRef,            ///< does not match "<Standard> §<clause> <title>"
    ClauseRefStandardMismatch,     ///< clause_ref names a different standard than `standard`
    EmptyRationale,
    EmptyEvidenceRefs,
    EmptyEvidenceRef,
    DuplicateEvidenceRef,
    MalformedJustification,        ///< parsed JSON did not have the expected shape

    // Requirement
    EmptyRequirementId,
    MalformedRequirementId,        ///< does not match REQ-<uppercase, digits, hyphens>
    EmptyRequirementTitle,
    EmptySourceClause,
    MalformedSourceClause,         ///< not a complete "<Standard> §<clause> <title>" citation key
    EmptyVerificationIntent,

    // Hazard
    EmptyHazardId,
    MalformedHazardId,             ///< does not match HAZ-<uppercase, digits, hyphens>
    EmptyHazardDescription,
    EmptyControls,                 ///< controlled_by is empty - the IEC 62304 §4.2 join is missing
    EmptyControlRef,
    DuplicateControlRef,

    // VerificationCase
    EmptyVerificationCaseId,
    MalformedVerificationCaseId,   ///< does not match VER-<uppercase, digits, hyphens>
    EmptyVerifiedRequirementId,

    // ProblemReport
    EmptyProblemReportId,
    MalformedProblemReportId,      ///< does not match PRB-<uppercase, digits, hyphens>
    EmptyProblemDescription,

    // AuditEvent
    EmptyAuditTimestamp,
    MalformedAuditTimestamp,       ///< not an ISO 8601 UTC instant, "YYYY-MM-DDThh:mm:ssZ"
    EmptyAuditSubject,

    // ComplianceProgram - cross-record rules, reachable only from the aggregate
    DuplicateRequirementId,
    DuplicateHazardId,
    DuplicateVerificationCaseId,
    DuplicateProblemReportId,
    DuplicateJustificationId,
    UnverifiedRequirement,         ///< no VerificationCase names this Requirement
    UnresolvedVerifiedRequirement, ///< a VerificationCase names a Requirement that does not exist
    UnresolvedHazardControl,       ///< a Hazard's control names a Requirement that does not exist
    MalformedComplianceProgram,    ///< parsed JSON did not have the expected shape
};

[[nodiscard]] std::string_view describe(GovernanceError error) noexcept;

/**
 * @brief The machine-readable form of a citation from
 *        docs/governance/citation-convention.md.
 *
 * Field names and constraints mirror docs/governance/schemas/justification.schema.json exactly:
 * `justificationId` (`JUS-NNN`, unique across the whole corpus - this type does not itself
 * enforce uniqueness, since that is a property of a *collection* of Justifications, not of one;
 * `ComplianceProgram::validate()` is where the collection-level check lives), `standard` (one of
 * `kApprovedStandards`), `clauseRef` (must start with the same standard), `rationale` (original
 * prose - this type cannot check that a string isn't reproduced text; see mdux-docs-lint for the
 * heuristic that runs over the corpus instead), `requirementId` (optional, `REQ-*`), and
 * `evidenceRefs` (non-empty strings with no duplicates; paths are not checked to exist here
 * because that is a lint-time concern over a real repository checkout).
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

// ---------------------------------------------------------------------------
// Closed vocabularies
//
// Each of these is a fixed set with a canonical wire spelling, so the C++ enum and the JSON
// Schema `enum` keyword describe the same set. Parsing is exact-match: an unrecognized spelling
// is an error rather than a silently-defaulted value, because a governance record that quietly
// downgrades an unknown method to some default is worse than one that fails to load.
// ---------------------------------------------------------------------------

/// IEC 62304 §4.3 software safety classification. Recorded as a *declared input* to a
/// ComplianceProgram, never inferred: only the integrating manufacturer's device-level risk
/// analysis can assign it (see docs/iec62304/01-scope-and-terms.md).
enum class SafetyClass : std::uint8_t { A, B, C };

/// How a VerificationCase establishes that its Requirement is met.
enum class VerificationMethod : std::uint8_t { Test, Analysis, Inspection, Review };

/// The lifecycle phase an AuditEvent belongs to.
enum class AuditCategory : std::uint8_t { Lifecycle, Verification, Change };

[[nodiscard]] std::string_view toWireString(SafetyClass value) noexcept;
[[nodiscard]] std::string_view toWireString(VerificationMethod value) noexcept;
[[nodiscard]] std::string_view toWireString(AuditCategory value) noexcept;

[[nodiscard]] std::optional<SafetyClass> safetyClassFromWire(std::string_view text) noexcept;
[[nodiscard]] std::optional<VerificationMethod> verificationMethodFromWire(
    std::string_view text) noexcept;
[[nodiscard]] std::optional<AuditCategory> auditCategoryFromWire(std::string_view text) noexcept;

/// Every spelling the matching `*FromWire` accepts, in enum order - the same closed set the JSON
/// Schema lists under `enum`. Exposed so the schema-drift check has one place to read the set
/// from rather than re-deriving it.
inline constexpr std::array<std::string_view, 3> kSafetyClassWireValues{"A", "B", "C"};
inline constexpr std::array<std::string_view, 4> kVerificationMethodWireValues{
    "test", "analysis", "inspection", "review"};
inline constexpr std::array<std::string_view, 3> kAuditCategoryWireValues{
    "lifecycle", "verification", "change"};

// ---------------------------------------------------------------------------
// Lifecycle records
// ---------------------------------------------------------------------------

/**
 * @brief One thing the software must do, traceable to the clause that asks for it.
 *
 * `sourceClause` uses the same citation key shape as `Justification::clauseRef`
 * ("<Standard> §<clause> <title>"), so a requirement and the justification for how it is met
 * sort together and can be joined on the clause.
 *
 * The rule that gives a Requirement its regulatory value - that at least one `VerificationCase`
 * covers it - is *not* checked by this type's validate(). It cannot be: coverage is a property
 * of the surrounding collection. `ComplianceProgram::validate()` enforces it.
 */
struct Requirement {
    std::string id;                  ///< `REQ-*`
    std::string title;
    std::string sourceClause;        ///< "<Standard> §<clause> <title>"
    std::string verificationIntent;  ///< how this requirement is meant to be shown met

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;
    [[nodiscard]] static mdux::core::Result<Requirement, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

/**
 * @brief A hazardous situation the software can contribute to, and the requirements that
 *        control it.
 *
 * `controlledBy` must be non-empty. That is the machine-checked half of the ISO 14971 §7 /
 * IEC 62304 §4.2 integration point: a hazard recorded with no risk control is either an
 * unfinished analysis or an accepted risk that nobody wrote down, and both should stop a
 * release. Whether the named controls are *adequate* is a review judgement this type does not
 * make.
 */
struct Hazard {
    std::string id;                         ///< `HAZ-*`
    std::string description;
    std::vector<std::string> controlledBy;  ///< `REQ-*` ids; non-empty, no duplicates

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;
    [[nodiscard]] static mdux::core::Result<Hazard, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

/**
 * @brief Evidence that one Requirement is met, by one method, backed by named artifacts.
 *
 * `evidenceRefs` carries repository-relative paths - a test source, a report, a review record.
 * As in `Justification`, the paths are not checked to exist here; that is a lint-time concern
 * over a real checkout, not a property of the record.
 */
struct VerificationCase {
    std::string id;             ///< `VER-*`
    std::string requirementId;  ///< `REQ-*`; must resolve within the program
    VerificationMethod method{VerificationMethod::Test};
    std::vector<std::string> evidenceRefs;  ///< non-empty, no empty entries, no duplicates

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;
    [[nodiscard]] static mdux::core::Result<VerificationCase, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

/**
 * @brief An IEC 62304 §9 problem report.
 *
 * `affectsRisk` is the field that matters downstream: §9.4 requires a problem that could affect
 * safety to be fed back into risk management. Nothing here enforces that the feedback happened -
 * that is a process obligation, not a data one - but recording the flag is what makes the
 * omission visible.
 */
struct ProblemReport {
    std::string id;  ///< `PRB-*`
    std::string description;
    bool closed{false};
    bool affectsRisk{false};

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;
    [[nodiscard]] static mdux::core::Result<ProblemReport, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

/**
 * @brief One dated entry in the program's history.
 *
 * `timestamp` is an ISO 8601 UTC instant, "YYYY-MM-DDThh:mm:ssZ" - one spelling only, so two
 * records are ordered by comparing their strings and an audit trail cannot be silently reordered
 * by changing timezone notation. `subject` names what the event was about: a requirement id, a
 * release tag, a file path.
 */
struct AuditEvent {
    AuditCategory category{AuditCategory::Lifecycle};
    std::string timestamp;
    std::string subject;

    [[nodiscard]] mdux::core::ResultVoid<GovernanceError> validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;
    [[nodiscard]] static mdux::core::Result<AuditEvent, GovernanceError> fromJson(
        const evidence::json::Value& object) noexcept;
};

// ---------------------------------------------------------------------------
// The aggregate
// ---------------------------------------------------------------------------

/**
 * @brief One thing wrong with a ComplianceProgram, and which record it is wrong about.
 *
 * `subject` is the offending record's id where it has one (`REQ-042`, `HAZ-007`). Records with
 * no id - `AuditEvent` - are identified positionally, as `auditEvents[3]`. For
 * `UnresolvedHazardControl`, `UnresolvedVerifiedRequirement` and the duplicate-id codes,
 * `subject` names the record carrying the fault and `detail` carries the id it points at.
 */
struct ValidationFailure {
    GovernanceError code{};
    std::string subject;
    std::string detail;  ///< empty unless the failure needs a second name, e.g. a dangling id

    // Deliberately a member rather than a defaulted hidden friend. GCC 16.1 ICEs in
    // `module_state::mangle` when it has to mangle a defaulted hidden-friend operator== declared
    // inside an exported struct (segfault at symbol_table::finalize_compilation_unit, reproduced
    // on the CI GCC 16 leg and nowhere else). The member form generates identical semantics for
    // every use this type has - comparing two failure lists - and does not take that code path.
    [[nodiscard]] bool operator==(const ValidationFailure&) const = default;
};

/**
 * @brief The aggregate a release gate validates.
 *
 * `safetyClass` is a declared input, not a conclusion this library reaches - see the note on
 * `SafetyClass`. It is recorded because IEC 62304 scales nearly every other obligation by it, so
 * a program that does not state one cannot be reviewed against the right clause set.
 */
struct ComplianceProgram {
    SafetyClass safetyClass{SafetyClass::A};
    std::vector<Requirement> requirements;
    std::vector<Hazard> hazards;
    std::vector<VerificationCase> verificationCases;
    std::vector<ProblemReport> problemReports;
    std::vector<AuditEvent> auditEvents;
    std::vector<Justification> justifications;

    /**
     * @brief Checks every record's own shape and every cross-record rule, reporting all failures
     *        rather than the first.
     *
     * Failures are ordered deterministically - by collection in declaration order, then by
     * position within the collection - so two runs over the same program produce identical
     * output and a CI diff of the failure list is meaningful.
     *
     * An empty failure list is success, so the return is `ResultVoid`:
     * `if (auto ok = program.validate(); !ok) { for (const auto& f : ok.error()) ... }`.
     */
    [[nodiscard]] mdux::core::ResultVoid<std::vector<ValidationFailure>> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first, and
    /// collapses any failure list to `MalformedComplianceProgram` - call validate() directly
    /// when the reasons matter, which for a release gate they always do.
    [[nodiscard]] mdux::core::Result<std::string, GovernanceError> write() const noexcept;

    /// Parses canonical program JSON. Strict about shape, and validates the result.
    [[nodiscard]] static mdux::core::Result<ComplianceProgram, GovernanceError> parse(
        std::string_view text) noexcept;
};

}  // namespace mdux::governance

/// Deliberately outside the `export` block: these are shared between this module's three
/// implementation units and invisible to anything that imports mdux.governance. Declaring them
/// here rather than duplicating an anonymous namespace in each .cpp is what keeps
/// Requirement::validate() and Justification::validate() agreeing on what an id looks like.
namespace mdux::governance::detail {

/// `<prefix>` followed by at least one uppercase letter, digit, or hyphen. The shape every id
/// in this module uses except `JUS-NNN`, which predates the others and is digits-only.
[[nodiscard]] bool isWellFormedId(std::string_view id, std::string_view prefix) noexcept;

/// `JUS-` followed by three or more digits - matches JUSTIFICATION_ID_RE in
/// tools/docs-lint/mdux_docs_lint.py exactly.
[[nodiscard]] bool isWellFormedJustificationId(std::string_view id) noexcept;

/// Returns the standard named by a "<Standard> §<clause> <title>" citation key, but only when
/// the whole key is well formed *and* that standard is approved. `std::nullopt` means the key
/// is malformed; a caller that also cares whether the key agrees with a separate `standard`
/// field compares the returned value itself.
[[nodiscard]] std::optional<std::string_view> citationKeyStandard(std::string_view key) noexcept;

/// Exactly "YYYY-MM-DDThh:mm:ssZ", with in-range field values. One spelling only - see the note
/// on AuditEvent::timestamp.
[[nodiscard]] bool isWellFormedUtcTimestamp(std::string_view text) noexcept;

/// The non-empty / no-empty-entry / no-duplicate checks shared by `evidenceRefs` and
/// `controlledBy`. Returns the first failing code, or `std::nullopt` when the list is fine.
[[nodiscard]] std::optional<GovernanceError> checkRefList(std::span<const std::string> refs,
                                                          GovernanceError whenEmpty,
                                                          GovernanceError whenEntryEmpty,
                                                          GovernanceError whenDuplicate) noexcept;

[[nodiscard]] mdux::core::Result<std::string, GovernanceError> requireString(
    const evidence::json::Value& object, std::string_view key, GovernanceError malformed) noexcept;

[[nodiscard]] mdux::core::Result<bool, GovernanceError> requireBool(
    const evidence::json::Value& object, std::string_view key, GovernanceError malformed) noexcept;

[[nodiscard]] mdux::core::Result<std::vector<std::string>, GovernanceError> requireStringArray(
    const evidence::json::Value& object, std::string_view key, GovernanceError malformed) noexcept;

/// Builds a JSON array of strings.
[[nodiscard]] mdux::core::Result<evidence::json::Value, GovernanceError> stringArray(
    std::span<const std::string> values, GovernanceError malformed) noexcept;

/// Rejects an object carrying members the reader does not know about, mirroring
/// `additionalProperties: false`. `expected` is the exact member count a valid object has.
[[nodiscard]] bool hasExactly(const evidence::json::Value& object, std::size_t expected) noexcept;

}  // namespace mdux::governance::detail
