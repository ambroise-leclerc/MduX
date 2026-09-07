/**
 * @file MeduiEvidence.cppm
 * @brief Derives MduX's own MEDUI-PROFILE-RENDERED E01 evidence envelope from a verify run.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (a host-dependent value never enters a
 *             byte-compared artifact)
 * @compliance ADR-014 What rendered-truth verification checks - decision 4, no measured pixel in
 *             a committed artifact
 * @compliance ADR-016 Locally versioned observation profiles
 *
 * MedUI's `schemas/evidence.schema.json` is the shared E01 identity envelope: an `obligations`
 * array and a `rows` array, each entry a 15-field identity. This module turns a `RunResult` into
 * that shape for `MEDUI-PROFILE-RENDERED`, so MduX can *publish* evidence in the form the shared
 * contract defines while the byte-compared `verification.json` keeps its own local shape.
 *
 * It is the **RENDERED subset** of a run: `Bounds`, `ColorHash` and `InkContainment` map to the
 * shared rendered-check ids `extent-equality`, `tint-composition` and `ink-containment`.
 * `LocalizedTextPresence` (`mdux.local/ink-coverage`) has no shared rendered-check id - it is an
 * implementation-local check (ADR-016) - so its outcomes are excluded and counted.
 *
 * The envelope is **derived and uncommitted**: written to the build tree via
 * `mdux-verify-ui --medui-evidence-out`, never committed, never byte-compared. `producer.source`
 * is `MDUX_BUILD_DIAGNOSTIC_SHA` (a configure-time `git rev-parse HEAD`), which is only ever legal
 * in output of exactly this kind - see `cmake/MduXBuildInfo.cmake`.
 */
module;

export module mdux.tools.verify.medui_evidence;

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.tools.verify.driver;

export namespace mdux::tools::verify {

enum class EvidenceError : std::uint8_t {
    NotRun,                ///< the run did not render and evaluate, so there is nothing to publish
    NoMappableObligation,  ///< the run produced no outcome that maps to a shared rendered-check id
    ManifestUnreadable,    ///< `medui-conformance.toml` could not be read for the contract SHA
    SerializationFailed,   ///< assembling the JSON value failed
};

[[nodiscard]] std::string_view describe(EvidenceError error) noexcept;

/// The derived envelope plus what it left out.
struct RenderedEvidence {
    /// `{ "obligations": [...], "rows": [...] }`, valid against `schemas/evidence.schema.json`.
    mdux::evidence::json::Value envelope;
    /// Outcomes with no shared rendered-check id (`LocalizedTextPresence`), excluded from the
    /// envelope. Reported so a reader knows the envelope is the RENDERED subset of the run.
    std::size_t                 excludedOutcomes{0};
};

/**
 * @brief Builds the `MEDUI-PROFILE-RENDERED` E01 envelope for `result`.
 *
 * `screenId` names the screen; `manifestPath` is `medui-conformance.toml`, read for its `commit`
 * (the envelope's `contract`). A passing run's obligation set and its report rows are identical by
 * construction here: each mapped outcome yields one obligation and one row with the same identity,
 * so `aggregate-evidence` over the result is `pass` exactly when every mapped check held.
 */
[[nodiscard]] mdux::core::Result<RenderedEvidence, EvidenceError>
deriveRenderedEvidence(const RunResult& result, std::string_view screenId, const std::filesystem::path& manifestPath);

}  // namespace mdux::tools::verify
