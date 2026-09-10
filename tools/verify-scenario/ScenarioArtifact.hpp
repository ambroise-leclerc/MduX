/**
 * @file ScenarioArtifact.hpp
 * @brief `scenario-verification.json`, and the members it adds to the scenario bundle's report.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-021 Dynamic scenario-capture evidence and its CI gate
 *
 * `mdux-verify-scenario-bake`'s half of the scenario bundle: `ScenarioDriver` replays and evaluates,
 * and this turns the `RunResult` it returns into the committed evidence file plus the `report.json`
 * members that name it. It enumerates nothing, resolves nothing and re-applies no predicate - it
 * serialises outcomes it was handed.
 *
 * ## What the file says, and what it must never say
 *
 * One entry per obligation, each making only that obligation's claim: a binding entry says a settled
 * value equalled a pinned one; a rendered entry says one golden or text check held on one capture
 * frame in one locale, under its observation profile; a capture entry says the replay reached a
 * marker and its frame formed. Every input is named by digest.
 *
 * **No measured pixel appears in it.** ADR-014 decision 4 keeps a driver-tuple-dependent measurement
 * out of a byte-compared artifact, and ADR-021 decision 3 makes the per-backend capture digest a
 * diagnostic attachment instead. Findings are outcome-preserving identities the way the screen
 * bundle's are: nothing here is named `passed`, `valid` or `correct`.
 */
#pragma once

/**
 * ## Not a C++20 module - a plain header, for `ScenarioDriver.hpp`'s reason.
 *
 * Include **after** `import std;`, `import mdux.core.result;`, `import mdux.evidence.json;` and
 * `#include "ScenarioDriver.hpp"` (which needs its own imports first - see there).
 */

namespace mdux::tools::verify::scenario {

/// The file name this writer owns, fixed so the CMake `OUTPUTS` entry and the writer cannot drift.
inline constexpr std::string_view verificationFileName = "scenario-verification.json";

/// The tool name that appears in diagnostics and in the report stage record.
inline constexpr std::string_view artifactToolName = "mdux-verify-scenario-bake";

enum class ArtifactError : std::uint8_t {
    NotRun,               ///< the run could not be made, so there is no outcome set to serialise
    NoObligations,        ///< a run that verified nothing is not evidence (ADR-014 decision 3)
    OutcomeMismatch,      ///< outcomes and obligations disagree; the driver's own invariant broke
    RenderedProfileInvalid, ///< a rendered outcome carries no observation profile, or the wrong one
    MalformedReport,      ///< the bundle's `report.json` did not parse as a bake report
    ReportRewriteFailed,  ///< the extended report failed its own validation
    SerializationFailed,  ///< canonical JSON refused a member this writer built
    PublishFailed,        ///< a bundle file could not be staged or promoted; the bundle is unchanged
};

[[nodiscard]] std::string_view describe(ArtifactError error) noexcept;

/**
 * @brief The canonical `scenario-verification.json` text for a completed run, trailing newline
 *        included.
 *
 * Fails rather than writing a file for a run that could not be made, and rather than committing a
 * rendered outcome whose observation profile is not the one its check reports under - the same
 * fail-closed rules `writeVerification()` applies to the screen bundle.
 */
[[nodiscard]] mdux::core::Result<std::string, ArtifactError> writeScenarioVerification(const RunResult& result, std::string_view scenarioId);

/// The resolved verification options the report records: the approved locale set actually verified
/// and the capture markers, so the report shows the run was not narrowed (ADR-007 decision 4).
[[nodiscard]] mdux::core::Result<evidence::json::Value, ArtifactError> verificationOptions(const RunResult& result);

/**
 * @brief Re-emits the scenario bundle's `report.json` with the verification output, options and the
 *        producing-tool stage.
 *
 * The bundle has one report; this extends it. The top-level `tool` stays `mdux-scenariobake`; the
 * stage names `mdux-verify-scenario-bake`, which is what produced `scenario-verification.json` -
 * without it a reader would attribute the file to the scenario compiler, which never saw a frame.
 */
[[nodiscard]] mdux::core::Result<std::string, ArtifactError>
extendScenarioReport(std::string_view reportText, std::string_view verificationJson, const evidence::json::Value& options, std::string_view toolVersion);

}  // namespace mdux::tools::verify::scenario
