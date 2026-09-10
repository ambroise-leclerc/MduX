/**
 * @file ScenarioDriver.cppm
 * @brief Host-only dynamic-evidence driver: replays a committed `.scenario` and verifies the frames
 *        its `capture` markers settle, in every approved locale (#321, ADR-021).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 * @compliance ADR-021 Dynamic scenario-capture evidence and its CI gate
 *
 * `mdux-verify-ui` renders a screen's **static** bindings. This renders the frame a scenario's
 * interaction produced: it drives the committed `.scenario` through the assembled `updateMonitor()`
 * loop (the same `ScenarioRunner` / `EventQueue` / `FieldEditor` path #320 delivered), and for each
 * `capture` marker records the settled `DemoState` through the production offscreen adapter, once per
 * approved locale. It then discharges two obligation kinds:
 *
 *  - **binding** - the state each `advance` settled equals the scenario's pinned `Expect` values,
 *    per locale (`ScenarioRunner`'s own per-step outcome);
 *  - **rendered** - each capture frame satisfies the screen's own golden and mandatory-text
 *    obligations, via `mdux::tools::verify::evaluateFrame()` - the identical `mdux.verify`
 *    predicates and artifact-derived expectations the screen gate runs, on a different frame.
 *
 * plus a **capture** obligation per declared marker: the replay reached it and the readback formed.
 *
 * The governed predicates and the atomic bundle publish are not here; they are `mdux.verify`'s and
 * `mdux.tools.verify.artifact`'s. This module owns artifact I/O, the replay wiring, a headless
 * device and the complete obligation enumeration. It is neither installed nor linked into a device
 * target.
 */
module;

export module mdux.tools.verify.scenario.driver;

import std;
import mdux.core.units;
import mdux.medui.schema;
import mdux.medui.scenario;
import mdux.tools.cli;
import mdux.verify;

// Re-exported: `RunResult::inputs` is `std::vector<mdux::tools::verify::BoundArtifact>`, and the
// artifact writer and tests reach `evaluateFrame()` / `Outcome` through this module.
export import mdux.tools.verify.driver;

export namespace mdux::tools::verify::scenario {

inline constexpr std::string_view toolName = "mdux-verify-scenario";

/// Stable process outcomes, mirroring `mdux::tools::verify::RunState`. An impossible run is not a
/// verification failure, and an absent Vulkan 1.3 device is the one impossibility a host may
/// legitimately have.
enum class RunState : std::uint8_t {
    Passed,
    ChecksFailed,
    CouldNotRun,
    NoRenderDevice,
};

/// Which of ADR-021 decision 2's three obligation kinds an entry is.
enum class ObligationKind : std::uint8_t {
    Binding,   ///< one `Expect` step, in one locale: the settled state equals the pinned value
    Rendered,  ///< one golden or text check, on one capture frame, in one locale
    Capture,   ///< one declared marker: the replay reached it and its frame rendered
};

[[nodiscard]] constexpr std::string_view toWire(ObligationKind kind) noexcept {
    switch (kind) {
        case ObligationKind::Binding:  return "binding";
        case ObligationKind::Rendered: return "rendered";
        case ObligationKind::Capture:  return "capture";
    }
    return {};
}

/// One item in the complete plan, owned so it survives artifact readers returning. `(kind, scope,
/// capture, stepIndex, nodeId, check)` is its identity - the tuple the artifact writer pairs each
/// outcome against so a recorded outcome cannot name an obligation the run did not enumerate.
struct Obligation {
    ObligationKind kind{ObligationKind::Binding};
    std::string    scope;       ///< the approved locale tag
    std::string    capture;     ///< the marker name; empty for a binding obligation
    std::size_t    stepIndex{0};///< the `Expect` step; 0 for a rendered/capture obligation
    std::string    expectKind;  ///< the `Expect` step's wire kind (e.g. "clock"); empty otherwise
    std::string    nodeId;      ///< the checked node; empty for a binding/capture obligation
    std::string    check;       ///< the check's spelling (e.g. "Bounds"); empty for binding/capture

    [[nodiscard]] bool operator==(const Obligation&) const = default;
};

/// One obligation's result. `finding` is `mdux::verify`'s for a rendered obligation and carries an
/// observation `profile`; a binding or capture obligation is a plain held/failed and `profile` is
/// default. Every field is owned.
struct Outcome {
    ObligationKind                   kind{ObligationKind::Binding};
    std::string                      scope;
    std::string                      capture;
    std::size_t                      stepIndex{0};
    std::string                      expectKind;
    std::string                      nodeId;
    std::string                      check;
    bool                             held{false};
    mdux::verify::Finding            finding{mdux::verify::Finding::Held};
    mdux::verify::ObservationProfile profile{};

    [[nodiscard]] bool operator==(const Outcome&) const = default;
};

struct RunResult {
    RunState                                        state{RunState::CouldNotRun};
    std::size_t                                     renderCount{0};
    std::vector<Obligation>                         obligations;
    std::vector<Outcome>                            outcomes;
    std::vector<mdux::tools::verify::BoundArtifact> inputs;
    std::vector<mdux::tools::cli::Diagnostic>       diagnostics;

    /// The `VkPhysicalDeviceProperties.deviceName` the run rendered on, verbatim. Never committed
    /// (ADR-007 decision 5); a diagnostic line and the diagnostic digest manifest only.
    std::string backend;

    /// Frame images this run wrote, one per capture per locale. Empty when no destination was set.
    /// Paths, not digests: these are diagnostic attachments for a person, never committed evidence
    /// (ADR-021 decision 3).
    std::vector<std::filesystem::path> frameImages;

    /// The per-capture, per-locale `rgba8-sha256` of each readback - the backend-specific baseline
    /// PAR-REQ-009 keeps out of the byte-compared bundle. Written to the diagnostic manifest only.
    struct CaptureDigest {
        std::string capture;
        std::string scope;
        std::string sha256;
    };
    std::vector<CaptureDigest> captureDigests;
};

/// What a run may choose, which is locations and never expectations (Epic #16's rule, ADR-014 D2).
struct RunOptions {
    /// The committed-artifact root - the `generated/` tree the screen, shader, font, text and image
    /// packages sit under. Empty means the current directory.
    std::filesystem::path artifactRoot;

    /// Where to write `<scenario>.<capture>.<scope>.frame.png` for every capture. Empty writes none.
    std::filesystem::path frameImageDirectory;

    /// Where to write `<scenario>.captures.sha256`, the diagnostic per-backend digest manifest.
    /// Empty writes none. Never `generated/` (ADR-021 decision 3).
    std::filesystem::path captureDigestPath;
};

/// Reads `<scenarioDirectory>/scenario.json`, resolves the screen bundle and approved locales it
/// names, replays it and verifies every capture. `--locales` cannot narrow the manifest.
[[nodiscard]] RunResult run(const std::filesystem::path& scenarioDirectory);
[[nodiscard]] RunResult run(const std::filesystem::path& scenarioDirectory, const RunOptions& options);

// --- Pure pieces, exposed for direct library tests (ADR-021 decision 2) -----------------------------

/**
 * @brief The complete obligation set for `scenario` over `locales`: binding, rendered and capture.
 *
 * One binding obligation per `Expect` step per locale; one rendered obligation per golden/text check
 * per declared capture per locale (the checks `mdux::tools::verify::enumerate()` names for `screen`);
 * one capture obligation per declared `captureNames` entry per locale. Pure - it renders nothing and
 * derives no expectation.
 */
[[nodiscard]] std::vector<Obligation> enumerateObligations(const mdux::medui::CompiledScenario&      scenario,
                                                           std::span<const std::string_view>        locales,
                                                           const mdux::medui::ScreenPackage&        screen,
                                                           std::span<const mdux::verify::GoldenEntry> goldens);

/**
 * @brief Pairs `outcomes` against `obligations`, appends a failed entry for any obligation with no
 *        outcome, flags a duplicated one, and returns the verdict.
 *
 * `RunState::CouldNotRun` is never returned here - a structural impossibility is the caller's to
 * detect before a frame is rendered. This returns `Passed` when every obligation has exactly one
 * `held` outcome, and `ChecksFailed` otherwise, appending one diagnostic per fault.
 */
[[nodiscard]] RunState reconcile(std::span<const Obligation>               obligations,
                                 std::vector<Outcome>&                     outcomes,
                                 std::vector<mdux::tools::cli::Diagnostic>& diagnostics);

/// 0 pass, 1 an obligation failed, 3 the run could not be made (device absent included).
[[nodiscard]] constexpr int exitStatus(RunState state) noexcept {
    switch (state) {
        case RunState::Passed:
            return 0;
        case RunState::ChecksFailed:
            return 1;
        case RunState::CouldNotRun:
        case RunState::NoRenderDevice:
            return 3;
    }
    return 3;
}

/// `frameImageDirectory` / `meduiEvidence`-style trailing members: appended so a positional
/// aggregate init of an earlier field is not silently rebound (the reason `Invocation` in the
/// screen driver appends `frameImageDirectory`).
struct Invocation {
    std::filesystem::path    scenarioDirectory;
    mdux::tools::cli::Format  format{mdux::tools::cli::Format::Text};
    std::filesystem::path    frameImageDirectory;
    std::filesystem::path    captureDigestPath;
};

[[nodiscard]] std::string usage();
[[nodiscard]] Invocation  parseArguments(std::span<const std::string_view> arguments);
[[nodiscard]] Invocation  parseArguments(int argc, const char* const* argv);

}  // namespace mdux::tools::verify::scenario
