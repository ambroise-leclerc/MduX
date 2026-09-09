/**
 * @file ScenarioTrace.cppm
 * @brief The allocating host-side step-by-step replay trace (#320, ADR-020 §3).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * Separate from the governed `mdux::medui::ScenarioRunner` on purpose (issue #320, acceptance
 * bullet 4): the runner records a bounded per-expectation outcome and allocates nothing; this
 * turns that outcome plus the `CompiledScenario` into a human-readable, allocating trace for CI
 * evidence. The trace is **derived and uncommitted** (ADR-014 D4 stance); #321 gates it and adds
 * the per-obligation observed-value detail.
 */
module;

export module mdux.tools.scenario.trace;

import std;
import mdux.medui.scenario;

export namespace mdux::tools::scenario {

/// Renders a step-by-step text trace: the header, one line per scenario step (events, advances,
/// and every `expect` marked `HELD` or `FAILED`), and a final verdict line. Deterministic — no
/// timestamps, so a CI job can diff two runs.
[[nodiscard]] std::string renderTraceText(const mdux::medui::CompiledScenario& scenario,
                                          const mdux::medui::ReplayReport&     report);

}  // namespace mdux::tools::scenario
