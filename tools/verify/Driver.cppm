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

/**
 * @brief One committed artifact the run bound, named by role rather than by filesystem path.
 *
 * #254 writes these into `verification.json`, which is byte-compared, and a path is exactly what
 * ADR-007 decision 5 keeps out of such a file: during a bake the screen bundle is read from
 * `${CMAKE_BINARY_DIR}/mdux_bake/screen/<id>/`, so a recorded path would name the machine that
 * produced the artifact. A role, an id and a digest identify the same input without that.
 *
 * The driver fills these where it already resolves and digests each artifact, so the writer has no
 * second resolution path to disagree with - the rule ADR-014 decision 2 states for expectations
 * applies just as much to the inputs the artifact names.
 */
struct BoundArtifact {
    std::string role;    ///< `screenPackage`, `goldens`, `shaderPackage`, `fontPackage`, `textPackage`
    std::string id;      ///< the artifact id; the screen's own id for its package and goldens
    std::string locale;  ///< the approved locale a text package carries; empty otherwise
    std::string sha256;  ///< lowercase hex, as every other evidence record spells a digest

    [[nodiscard]] bool operator==(const BoundArtifact&) const = default;
};

struct RunResult {
    RunState                                  state{RunState::CouldNotRun};
    std::size_t                               renderCount{0};
    std::vector<Obligation>                   obligations;
    std::vector<Outcome>                      outcomes;
    std::vector<BoundArtifact>                inputs;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    /// Diff images this run wrote, one per render scope that had a failure. Empty when nothing
    /// failed, and empty when no destination was configured.
    ///
    /// Paths rather than digests, and it is worth saying why that is not the inconsistency it looks
    /// like beside `inputs`. `BoundArtifact` names inputs by digest because #254 commits them to a
    /// byte-compared file, where a path would name the machine that baked it. Nothing here is
    /// committed: these are attachments for a person, and what a person needs from one is where it
    /// is. See `mdux.tools.verify.diff` for why the image stays outside the artifact entirely.
    std::vector<std::filesystem::path> diffImages;
};

/**
 * @brief What a run is allowed to choose, which is locations and never expectations.
 *
 * Epic #16 states the rule this struct is held to - "the caller chooses locations, not expectations"
 * - and ADR-014 decision 2 is why: a verifier whose scope is an argument reports on whatever it was
 * asked about, in a file that reads as though it reported on the screen. So there is no member here
 * that could narrow, widen or substitute what is checked. Both of these move bytes; neither moves a
 * claim.
 */
struct RunOptions {
    /// Where the committed artifacts other than the screen bundle live - the `generated/` tree the
    /// shader, font and text packages sit under. The single-argument `run()` derives it from the
    /// bundle's own path.
    ///
    /// Empty is legal and means the current directory, which is what that derivation yields for a
    /// relative bundle like `screen/<id>`. So this is deliberately not checked for emptiness: an
    /// artifact that cannot be found is reported as the unreadable path it is (VUI005, VUI006),
    /// which names the file and is more use than a guess about why the root was wrong.
    std::filesystem::path artifactRoot;

    /// Where to write a diff image for each render scope that fails. Empty means write none, which
    /// is what a run that only wants a verdict asks for. Created if it does not exist.
    std::filesystem::path diffImageDirectory;
};

/// Reads `<screenDirectory>/{package,goldens}.json`, resolves every referenced artifact and runs.
[[nodiscard]] RunResult run(const std::filesystem::path& screenDirectory);

/// Test/integration overload with an explicit committed-artifact root (`generated/`).
[[nodiscard]] RunResult run(const std::filesystem::path& screenDirectory, const std::filesystem::path& artifactRoot);

/// The full form. `options.artifactRoot` must be set; the other members may be empty.
[[nodiscard]] RunResult run(const std::filesystem::path& screenDirectory, const RunOptions& options);

struct Invocation {
    std::filesystem::path    screenDirectory;
    std::filesystem::path    diffImageDirectory;
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
