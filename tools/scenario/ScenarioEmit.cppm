/**
 * @file ScenarioEmit.cppm
 * @brief Turns a committed `scenario.json` into C++ a device build can hold as `constexpr` data.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-012 What a compiled artifact emits, and which parts are committed
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * The precedent is `mdux-screenemit` (ADR-012 decision 3): the canonical artifact is
 * `generated/scenario/<id>/{scenario.json, report.json}` - reviewed as JSON, byte-compared in CI -
 * and the C++ is a mechanical rendering of exactly those bytes, regenerated every build, never
 * committed. The generated source carries `static_assert(scenario.validate().has_value())`, so a
 * scenario that does not satisfy `mdux.medui.scenario` is a build failure in whatever links it.
 */
module;

export module mdux.tools.scenarioemit;

import std;
import mdux.tools.cli;

export namespace mdux::tools::scenario {

inline constexpr std::string_view emitToolName = "mdux-scenarioemit";

/// The generated sources, held in memory so both outputs derive from one rendering.
struct EmitOutputs {
    std::string moduleName;    ///< e.g. `mdux.medui.generated.scenario_endoscope_monitor_basics`
    std::string moduleSource;  ///< the .cppm text
    std::string headerSource;  ///< the .hpp text
    std::string stem;          ///< filename stem for both, e.g. `scenario_endoscope_monitor_basics`
};

/// The C++ identifier a scenario id maps to: `endoscope-monitor-basics` becomes
/// `scenario_endoscope_monitor_basics`. Prefixed unconditionally, `identifierForScreen()`'s
/// reasoning. `mdux_scenario_identifier()` in `cmake/MduXScenarioEmit.cmake` implements the same
/// rule and a parity test runs both.
[[nodiscard]] std::string identifierForScenario(std::string_view scenarioId);

/// Reads a committed `scenario.json` and renders both outputs. `std::nullopt` with a `SCE0NN`
/// diagnostic when the file cannot be read or does not describe a valid scenario.
[[nodiscard]] std::optional<EmitOutputs> renderScenario(const std::filesystem::path&  scenarioPath,
                                                        std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir` as `<stem>.cppm` and `<stem>.hpp`, only when the content
/// differs from what is there.
[[nodiscard]] bool writeScenario(const EmitOutputs& outputs, const std::filesystem::path& outputDir,
                                 std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::scenario
