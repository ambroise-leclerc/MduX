/**
 * @file Driver.cppm
 * @brief Host-only rendered-truth verification driver for committed screen bundles.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * The governed predicates live in `mdux.verify`; this module owns everything they deliberately do
 * not: artifact I/O, complete obligation enumeration, a headless Vulkan device, offscreen rendering
 * and actionable diagnostics. It is neither installed nor linked into a device target.
 */
module;

export module mdux.tools.verify.driver;

import std;
import mdux.core.units;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.verify;

export namespace mdux::tools::verify {

inline constexpr std::string_view toolName = "mdux-verify-ui";

/// Stable process outcomes. In particular, an impossible run is not a verification failure.
enum class RunState : std::uint8_t {
    Passed,
    ChecksFailed,
    CouldNotRun,
    /// The one impossibility a host may legitimately have: no Vulkan 1.3 device to render on.
    /// Every other impossibility is `CouldNotRun` and must not be mistaken for an absent GPU.
    NoRenderDevice,
};

enum class ObligationKind : std::uint8_t { Golden, Text };

/// One item in the complete plan, owned so it remains usable after artifact readers return.
struct Obligation {
    ObligationKind kind{ObligationKind::Golden};
    std::string    nodeId;
    std::string    scope;
    std::string    check;

    [[nodiscard]] bool operator==(const Obligation&) const = default;
};

struct PlanResult {
    std::vector<Obligation>                   obligations;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Enumerates the entire golden-by-scope and text-by-approved-locale obligation set.
 *
 * Pure and exposed for direct library tests. Production obtains `goldens` from the committed
 * sidecar; this function deliberately does not render or construct synthetic expectations.
 */
[[nodiscard]] PlanResult enumerate(const mdux::medui::ScreenPackage& screen, std::span<const mdux::verify::GoldenEntry> goldens);

/// An owning copy of the governed outcome, suitable for #254's later serialization stage.
struct Outcome {
    mdux::verify::Finding  finding{mdux::verify::Finding::Held};
    std::string            nodeId;
    std::string            scope;
    std::string            check;
    mdux::medui::NodeRect  expected{};
    mdux::medui::NodeRect  found{};
    bool                   foundValid{false};
    mdux::core::ColorRgba8 expectedColor{};
    mdux::core::ColorRgba8 foundColor{};
    bool                   foundColorValid{false};
    std::size_t            glyphIndex{0};

    [[nodiscard]] bool held() const noexcept {
        return finding == mdux::verify::Finding::Held;
    }
};

struct RunResult {
    RunState                                  state{RunState::CouldNotRun};
    std::size_t                               renderCount{0};
    std::vector<Obligation>                   obligations;
    std::vector<Outcome>                      outcomes;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;
};

/// Reads `<screenDirectory>/{package,goldens}.json`, resolves every referenced artifact and runs.
[[nodiscard]] RunResult run(const std::filesystem::path& screenDirectory);

/// Test/integration overload with an explicit committed-artifact root (`generated/`).
[[nodiscard]] RunResult run(const std::filesystem::path& screenDirectory, const std::filesystem::path& artifactRoot);

struct Invocation {
    std::filesystem::path    screenDirectory;
    mdux::tools::cli::Format format{mdux::tools::cli::Format::Text};
};

[[nodiscard]] std::string usage();
[[nodiscard]] Invocation  parseArguments(std::span<const std::string_view> arguments);
[[nodiscard]] Invocation  parseArguments(int argc, const char* const* argv);

/// 0 pass, 1 verification failure, 2 usage, 3 run impossible.
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

}  // namespace mdux::tools::verify
