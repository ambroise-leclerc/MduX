/**
 * @file Diagnostics.cpp
 * @brief The `.medui` diagnostic registry table.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * The table is the interesting part of this file; everything else is lookup. Each row's `summary`
 * says what the code means rather than what one call site happened to print, because the summary is
 * what a reader consults when deciding whether an existing code already covers a new situation -
 * and reusing a code whose meaning nearly fits is how a stable identifier stops being stable.
 */
module;

module mdux.tools.medui.diagnostics;

import std;
import mdux.tools.cli;

namespace mdux::tools::medui {

namespace {

using cli::Severity;

// Numbers are assigned in blocks, with gaps left inside each block on purpose: a new grammar rule
// should land next to the grammar rules rather than at the end of the table, and it can only do
// that if its block has room. The blocks are documented on the enum in Diagnostics.cppm.
constexpr std::array<CodeInfo, 24> table{{
    {Code::RecipeUnreadable, "MEDUI-E000", Severity::Error,
     "the recipe file could not be opened",
     "check the path passed on the command line, and that the file is readable"},
    {Code::RecipeUnparsed, "MEDUI-E001", Severity::Error,
     "the recipe is not valid TOML in the subset mdux.tools.toml accepts",
     "see recipes/font/dejavu-ui.toml for the accepted shape; there is no [[table]] support"},
    {Code::RecipeMissingMember, "MEDUI-E002", Severity::Error,
     "the recipe is missing a member the compiler requires",
     "add the named key; every recipe knob is required because a defaulted one would not appear "
     "in report.json (ADR-007)"},
    {Code::SourceUnreadable, "MEDUI-E003", Severity::Error,
     "the .medui source named by the recipe could not be opened", "check the path in the recipe"},
    {Code::SourceNotUtf8, "MEDUI-E004", Severity::Error,
     "the .medui source is not valid UTF-8", "re-save the file as UTF-8 without a byte-order mark"},

    {Code::UnexpectedToken, "MEDUI-E010", Severity::Error,
     "the parser found a token that cannot appear here", ""},
    {Code::UnknownComponent, "MEDUI-E011", Severity::Error,
     "the component name is not in the dictionary",
     "see the component table in the medui-authoring skill; the dictionary is closed"},
    {Code::MissingRequiredField, "MEDUI-E012", Severity::Error,
     "a component is missing a field its dictionary entry requires", ""},
    {Code::UnknownField, "MEDUI-E013", Severity::Error,
     "a component carries a field its dictionary entry does not define",
     "check the spelling; unknown fields are rejected rather than ignored, so a typo cannot "
     "silently do nothing"},
    {Code::DuplicateNodeId, "MEDUI-E014", Severity::Error,
     "two nodes in one screen share an id",
     "ids address nodes in golden references and requirement traces, so they must be unique "
     "within a screen"},
    {Code::NestedRow, "MEDUI-E015", Severity::Error,
     "a Row contains another Row",
     "flatten the inner Row into its parent. Row is a single level so that layout stays one "
     "flattening pass (ADR-011 decision 5)"},
    {Code::ForbiddenConstruct, "MEDUI-E016", Severity::Error,
     "the source uses a loop, conditional, recursion or scripting construct",
     "express the screen at its maximum extent and hide what is unused. These make the primitive "
     "count depend on data the compiler cannot see, so DrawBudget could not be computed exactly "
     "(ADR-011 decision 5)"},
    {Code::HardcodedString, "MEDUI-E017", Severity::Error,
     "a literal string appears where a text key is required",
     "use t(\"STR-KEY\") against an approved text package. A hardcoded string cannot be validated "
     "against every approved locale, which is what the budget check needs"},

    {Code::UnknownColorToken, "MEDUI-E030", Severity::Error,
     "a Theme.Colors token is not in the governed table",
     "check the spelling against the theme token table. Unknown tokens are a compile error rather "
     "than a fallback colour, because a fallback would render something nobody approved"},
    {Code::UnknownTextKey, "MEDUI-E031", Severity::Error,
     "a text key is not in the approved text package at all", ""},
    {Code::TextKeyMissingForLocale, "MEDUI-E032", Severity::Error,
     "a text key exists but is missing from at least one approved locale",
     "add the translation, or remove the locale from the recipe's approved list. A key present in "
     "one locale and absent in another is a screen that renders blank on a shipped device"},
    {Code::FieldValueKind, "MEDUI-E033", Severity::Error,
     "a field value has the wrong semantic kind",
     "use the value form assigned to this field in the shared component model"},
    {Code::NamedValueOutsideSet, "MEDUI-E034", Severity::Error,
     "a named value is outside the closed set its field admits",
     "use one of the members the shared component model lists for this field. This is not "
     "MEDUI-E033: the value is a well-formed identifier, so its kind is right and only its "
     "membership is wrong"},

    {Code::TextBudgetExceeded, "MEDUI-E050", Severity::Error,
     "a component's bounds cannot contain the widest approved translation",
     "widen the component or shorten the translation. The check is against the widest approved "
     "locale, not the authoring one"},
    {Code::LayoutOverflow, "MEDUI-E051", Severity::Error,
     "a node does not fit the space its parent allows",
     "the solver reports overflow rather than clamping, because a clamped layout renders "
     "something the author did not describe"},
    {Code::SurfaceExceeded, "MEDUI-E052", Severity::Error,
     "a node falls outside the declared surface", ""},
    {Code::CharsetEscape, "MEDUI-E053", Severity::Error,
     "dynamic text could produce a character outside the restricted charset",
     "restrict the format, or extend the charset in the font recipe and re-bake. The charset is "
     "what makes \"no shaping on device\" checkable rather than conventional (ADR-010)"},

    {Code::SafetyCriticalWithoutRequirement, "MEDUI-E070", Severity::Error,
     "a @safety_critical node carries no requirement:",
     "add requirement:, or remove the annotation. A safety-critical node with no requirement "
     "cannot be traced, and tracing is what the annotation is for"},
    {Code::UnknownCvCheck, "MEDUI-E071", Severity::Error,
     "a cv_check names a verification this compiler does not emit", ""},
}};

}  // namespace

std::span<const CodeInfo> registry() noexcept { return table; }

const CodeInfo* tryInfo(Code code) noexcept {
    for (const CodeInfo& row : table) {
        if (row.code == code) {
            return &row;
        }
    }
    return nullptr;
}

const CodeInfo& info(Code code) {
    if (const CodeInfo* row = tryInfo(code)) {
        return *row;
    }
    // Fails loudly rather than returning the first row.
    //
    // An earlier revision returned `table.front()` on the argument that the completeness test makes
    // this unreachable. Two reviewers objected independently, and they were right: the completeness
    // test covers every *enumerator*, and `Code` has a fixed underlying type, so
    // `static_cast<Code>(200)` is a well-formed value of the type that no enumerator names. A cast,
    // a value read from data, or an uninitialised member would all have mapped silently to
    // MEDUI-E000 - "the recipe file could not be opened" - with that code's severity and fix hint
    // attached to an unrelated failure. A registry whose purpose is that a code means one thing
    // cannot have a path that quietly assigns the wrong one.
    //
    // Throwing is available here because this is the host-tools zone (ADR-004, ADR-005): the
    // compiler never reaches a device, and `tools/CMakeLists.txt` says as much. Callers that need
    // the non-throwing form have `tryInfo()`.
    throw std::logic_error{
        std::format("mdux::tools::medui::info: no registry row for Code value {}. Every enumerator "
                    "must have a row in Diagnostics.cpp's table; a value outside the enumerators "
                    "means a cast or an uninitialised Code reached this call.",
                    static_cast<unsigned>(code))};
}

std::string_view id(Code code) { return info(code).id; }

std::string legacyId(Code code) {
    const std::string_view canonical = id(code);
    return std::string{"MDX"} + std::string{canonical.substr(5)};
}

std::span<const std::string_view> retired() noexcept {
    static constexpr std::array<std::string_view, 0> none{};
    return none;
}

cli::Diagnostic diagnose(Code code, std::string file, std::size_t line, std::size_t column,
                         std::string message, std::string fixHint) {
    const CodeInfo& row = info(code);
    return cli::Diagnostic{.file = std::move(file),
                           .line = line,
                           .column = column,
                           .code = std::string{row.id},
                           .severity = row.severity,
                           .message = std::move(message),
                           .fixHint = fixHint.empty() ? std::string{row.fixHint} : std::move(fixHint)};
}

}  // namespace mdux::tools::medui
