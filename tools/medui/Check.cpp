/**
 * @file Check.cpp
 * @brief Implementation of the single-file checker.
 */

module;

module mdux.tools.medui.check;

import std;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;

namespace mdux::tools::medui {

namespace {

namespace ms = mdux::medui;

/// Codes for what a run could not cover. Local to this tool, in the sense `SHE0NN` and `SCP0NN` are:
/// the `MEDUI-E0NN` registry is the shared contract for what a screen may *say*, and "this run did
/// not check X" is a statement about the run rather than about the screen.
constexpr std::string_view textNotChecked   = "MDC001";
constexpr std::string_view layoutNotChecked = "MDC002";

void note(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::string_view code, std::string message, std::string fixHint) {
    cli::Diagnostic entry;
    entry.file     = std::move(file);
    entry.code     = std::string{code};
    entry.severity = cli::Severity::Note;
    entry.message  = std::move(message);
    entry.fixHint  = std::move(fixHint);
    diagnostics.push_back(std::move(entry));
}

}  // namespace

CheckResult checkScreen(std::string_view source, std::string file) {
    CheckResult result;

    // 1. Parse. Everything after this reads an AST, so a file that does not parse stops here rather
    //    than producing a second opinion about a screen nobody could read.
    ParseResult parsed = parse(source, file);
    if (!parsed.diagnostics.empty() || !parsed.screen.has_value()) {
        result.diagnostics = std::move(parsed.diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(diagnose(Code::SourceUnreadable, file, 0, 0, "the source produced no screen and no diagnostic"));
        }
        return result;
    }
    const ast::Screen& screen = *parsed.screen;

    // 2. Semantic analysis, against the governed theme table and no approved locale. `analyze()`
    //    skips the key checks when it is given no packages, which is why this can run at all - see
    //    the note recorded below for what that leaves uncovered.
    std::vector<std::string_view> themeTokens;
    themeTokens.reserve(ms::themeColors.size());
    for (const ms::ThemeColor& colour : ms::themeColors) {
        themeTokens.push_back(colour.token);
    }

    // The one caller that asks for the partial mode, and the reason the mode is a request rather
    // than something inferred from an empty package list: this file may belong to no recipe at all.
    const SemanticResult semantic = analyze(screen, file, {.themeTokens = themeTokens, .textPackages = {}, .locales = LocalePolicy::Skipped});
    if (!semantic.ok()) {
        result.diagnostics = semantic.diagnostics;
        return result;
    }

    // 3. Annotations. Before layout, as the compiler runs them: the shared conformance suite pins
    //    MEDUI-E070 on a screen with no `surface:`, which the solver cannot resolve at all.
    const SafetyResult safety = validateSafetyAnnotations(screen, file);
    if (!safety.ok()) {
        result.diagnostics = safety.diagnostics;
        return result;
    }

    // 4. Layout and goldens, when the file says what surface it is drawn for. A screen without one
    //    is not malformed - the compiler takes the surface from the recipe - so this is a gap in
    //    what can be checked here rather than a finding about the file.
    if (screen.surface.has_value()) {
        const LayoutResult layout = resolveLayout(screen, file, {.surfaceWidth = screen.surface->x, .surfaceHeight = screen.surface->y});
        if (!layout.ok()) {
            result.diagnostics = layout.diagnostics;
            return result;
        }
        result.layoutChecked = true;

        // Called for its own sake: it throws if the annotation gate was bypassed, and running it
        // here is what makes a golden that cannot be derived show up in a check rather than in a
        // compile three commits later.
        static_cast<void>(collectGoldens(layout));
    } else {
        note(result.diagnostics,
             file,
             layoutNotChecked,
             "this screen declares no `surface:`, so layout, overflow and golden bounds were not checked",
             "add `surface: <width>px, <height>px;` to check it here, or compile it through a recipe that declares one");
    }

    // 5. What a file on its own cannot answer. Reported every time, including on a screen that draws
    //    no text: "there was nothing to check" and "the check did not run" are different statements,
    //    and only the second one is true here.
    note(result.diagnostics,
         file,
         textNotChecked,
         "text keys and text budgets were not checked: a single file names no approved locale",
         "compile through a recipe with a [text] table to check keys against every approved locale and "
         "boxes against the widest translation");

    return result;
}

}  // namespace mdux::tools::medui
