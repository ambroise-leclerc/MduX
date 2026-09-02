/**
 * @file Artifact.cppm
 * @brief `verification.json`, and the two members it adds to the screen bundle's existing report.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * This is #254's half of the screen bundle: #253's driver renders and evaluates, and this turns the
 * `RunResult` it returns into the fourth committed file. It enumerates nothing, resolves nothing and
 * re-applies no predicate - it serializes outcomes it was handed. ADR-014 decision 2's rule about
 * expectations is a rule about implementations, and a writer that recomputed any part of a run
 * would be the second implementation that rule exists to prevent.
 *
 * ## What the artifact says, and the sentence it must survive
 *
 * Each outcome names exactly one obligation and makes only that obligation's claim. A node carrying
 * only `Bounds` appears once, as a `Bounds` outcome, and nothing in the file can be read as saying
 * its tint was checked. The file names the screen package, the goldens sidecar, the shader package
 * and the per-locale font and text packages it used, by digest.
 *
 * What it does not say is that the screen is correct. The expectation and the frame come from one
 * source, so what holds is that the render agrees with the compiled screen - internal consistency,
 * not truth. ADR-014 decision 4 states this as a constraint on wording rather than a caveat, which
 * is why nothing here is named `passed`, `valid` or `correct`.
 *
 * ## Why there is no measurement in it
 *
 * `Outcome` carries `found` rectangles and `foundColor` samples so a driver can print a sentence a
 * reader can act on. None of that reaches this file. A measured pixel makes a byte-compared
 * artifact a property of the driver tuple that produced the frame rather than of its declared
 * inputs, so a rendering change would fail an evidence comparison while every check still held -
 * the same structural argument ADR-007 decision 5 makes about commit SHAs. The failure diff #255
 * attaches is a measurement for a human, and it lives outside this file.
 */
module;

export module mdux.tools.verify.artifact;

import std;
import mdux.core.result;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.tools.verify.driver;

export namespace mdux::tools::verify {

/// The file name this writer owns, fixed here so the CMake `OUTPUTS` entry and the writer cannot
/// drift into naming two different files.
inline constexpr std::string_view verificationFileName = "verification.json";

/// The tool name that appears in diagnostics from the bake front end.
inline constexpr std::string_view artifactToolName = "mdux-verify-bake";

enum class ArtifactError : std::uint8_t {
    NotRun,               ///< the run could not be made, so there is no outcome set to serialize
    NoObligations,        ///< a run that verified nothing is not evidence (ADR-014 decision 3)
    OutcomeMismatch,      ///< outcomes and obligations disagree; the driver's own invariant broke
    MalformedReport,      ///< the bundle's `report.json` did not parse as a bake report
    ReportRewriteFailed,  ///< the extended report failed its own validation
    SerializationFailed,  ///< canonical JSON refused a member this writer built
};

[[nodiscard]] std::string_view describe(ArtifactError error) noexcept;

/**
 * @brief The canonical `verification.json` text for a completed run, trailing newline included.
 *
 * Fails rather than writing a file for a run that could not be made: an artifact recording zero
 * outcomes because no device existed is indistinguishable, once committed, from one recording zero
 * outcomes because the screen had nothing to verify. ADR-014 decision 3 makes both a failure, and
 * this is where the first of them stops being writable.
 */
[[nodiscard]] mdux::core::Result<std::string, ArtifactError> writeVerification(const RunResult& result, std::string_view screenId);

/**
 * @brief The fully resolved verification options, as the screen's `report.json` records them.
 *
 * `--locales=all` resolved to the manifest's actual set is the option worth recording: it is the
 * one knob a caller could otherwise use to narrow what was verified, and ADR-007 decision 4 exists
 * so that a resolved set appears in the report rather than the name of one.
 */
[[nodiscard]] mdux::core::Result<evidence::json::Value, ArtifactError> verificationOptions(const RunResult& result, std::string_view artifactRoot);

/**
 * @brief Re-emits the bundle's `report.json` with the verification output and its resolved options.
 *
 * The screen bundle has one report, and this extends it rather than adding a second at the same
 * path - ADR-014 decision 4 forbids the second, and a reader holding two reports for one artifact
 * has to decide which is authoritative. `mdux-meduic` writes the report first, naming the two files
 * it produced; this adds the third, which cannot exist until a frame has been rendered.
 *
 * `verificationJson` is the exact text `writeVerification()` returned, so the digest recorded is of
 * the bytes that get committed rather than of a re-serialization that might differ.
 */
[[nodiscard]] mdux::core::Result<std::string, ArtifactError>
extendReport(std::string_view reportText, std::string_view verificationJson, const evidence::json::Value& options);

}  // namespace mdux::tools::verify
