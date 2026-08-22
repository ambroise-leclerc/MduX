/**
 * @file Check.cppm
 * @brief Validating one `.medui` file on its own, and saying what that cannot cover.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * The surface an author and an agent both reach for first: point it at a file, read the
 * diagnostics, fix them. `mdux-meduic` compiles a screen from a recipe and writes artifacts; this
 * runs the same stages over a file that may not belong to a recipe at all, and writes nothing.
 *
 * ## What a single file cannot answer, and why that is said out loud
 *
 * Two stages need inputs a file does not carry.
 *
 * **Text keys** are checked against the approved locales a recipe names. With no recipe there are no
 * approved locales, and a checker that ran the check anyway would report every `t("STR-KEY")` as
 * absent from every locale - which is a vacuous truth dressed as a finding, and the fastest way to
 * teach an author that this tool's output is noise. So the check does not run, and the run says so.
 *
 * **Text budgets** need the font package those locales were baked into. Same reasoning, same
 * treatment.
 *
 * A third case is the file's own doing rather than the recipe's: a screen with no `surface:` cannot
 * be resolved to rectangles at all, so layout, overflow and golden bounds go unchecked. That is
 * reported the same way.
 *
 * Each of these is a `Severity::Note` carrying an `MDC0NN` code, travelling in the same envelope as
 * the findings. Two consequences worth having: a clean run is visibly *partial* rather than
 * silently so, and an agent can key off the code to decide whether to re-run through the compiler
 * with a recipe.
 *
 * ## What it does check
 *
 * Everything decidable from the file plus the governed tables: the grammar (#192), the component
 * dictionary, field value domains, hardcoded strings and theme tokens (#193), the `@safety_critical`
 * annotation rules (#196), and - when the file declares a surface - bounded layout with its overflow
 * and containment rules (#194) and the golden set that follows from it (#196).
 */
module;

export module mdux.tools.medui.check;

import std;
import mdux.tools.cli;

export namespace mdux::tools::medui {

/// The tool name that appears in every diagnostic envelope this produces.
inline constexpr std::string_view checkToolName = "mdux-medui-check";

/**
 * @brief One file's findings, and which checks were able to run.
 *
 * The booleans are not a substitute for the notes - both are produced, because a human reads the
 * notes and a caller reads the flags. They exist so a test can assert that a check was *skipped*
 * rather than merely that it reported nothing, which is the distinction that would otherwise be
 * invisible.
 */
struct CheckResult {
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;
    bool                                      layoutChecked{false};  ///< the file declared a surface
    bool                                      textChecked{false};    ///< approved locales were available

    /// Whether anything of error severity was reported. Notes and warnings do not fail a check.
    [[nodiscard]] bool ok() const noexcept {
        return std::ranges::none_of(diagnostics, [](const mdux::tools::cli::Diagnostic& diagnostic) {
            return diagnostic.severity == mdux::tools::cli::Severity::Error;
        });
    }
};

/**
 * @brief Runs every stage a single file admits, in the order the compiler runs them.
 *
 * @param source the file's text
 * @param file   the path to name in diagnostics
 *
 * Stops at the first stage that reports an error, for the reason the compiler driver stops: a later
 * stage reading a screen an earlier one rejected reports consequences rather than causes.
 */
[[nodiscard]] CheckResult checkScreen(std::string_view source, std::string file);

}  // namespace mdux::tools::medui
