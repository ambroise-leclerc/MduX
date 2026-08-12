/**
 * @file DiagnosticsTests.cpp
 * @brief BDD scenarios for the `.medui` diagnostic code registry (issue #191).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * The registry's whole value is that questions about the *set* of codes are answerable
 * mechanically. These are the questions, as tests. A registry nobody enumerates is a table of
 * string literals with extra steps, so if these scenarios were deleted the module below would be
 * worth deleting with them.
 */

import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md = mdux::tools::medui;
namespace cli = mdux::tools::cli;

/// Every enumerator, so a scenario can walk the enum rather than the table it is checking against.
///
/// Written out rather than derived: C++ has no reflection over an enum, and a `Count` sentinel
/// cast in a loop would assume contiguity this enum does not promise. The duplication is the point
/// - adding an enumerator without adding it here fails `every code is registered`, which is the
/// nudge that also gets it a table row.
constexpr std::array<md::Code, 22> allCodes{
    md::Code::RecipeUnreadable,
    md::Code::RecipeUnparsed,
    md::Code::RecipeMissingMember,
    md::Code::SourceUnreadable,
    md::Code::SourceNotUtf8,
    md::Code::UnexpectedToken,
    md::Code::UnknownComponent,
    md::Code::MissingRequiredField,
    md::Code::UnknownField,
    md::Code::DuplicateNodeId,
    md::Code::NestedRow,
    md::Code::ForbiddenConstruct,
    md::Code::HardcodedString,
    md::Code::UnknownColorToken,
    md::Code::UnknownTextKey,
    md::Code::TextKeyMissingForLocale,
    md::Code::TextBudgetExceeded,
    md::Code::LayoutOverflow,
    md::Code::SurfaceExceeded,
    md::Code::CharsetEscape,
    md::Code::SafetyCriticalWithoutRequirement,
    md::Code::UnknownCvCheck,
};

[[nodiscard]] bool wellFormedId(std::string_view id) {
    // `MDX-E` followed by exactly three digits. The schema's `code` pattern is looser - it admits
    // the bakers' `TXT001` too - so this checks the narrower shape this tool committed to, which
    // the schema alone would not catch.
    if (id.size() != 8 || !id.starts_with("MDX-E")) {
        return false;
    }
    return std::all_of(id.begin() + 5, id.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

// ---------------------------------------------------------------------------
// The questions a registry exists to answer.
// ---------------------------------------------------------------------------

const mdux::spec::Register everyCodeIsRegistered{
    "Every enumerator has exactly one registry row",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-complete")
            .Given("the registry and the full enumerator list", [] {})
            .When("each enumerator is looked up", [] {})
            .Then("each resolves to a row carrying its own code, and the table has no extras",
                  [] {
                      mdux::spec::Checks checks;
                      for (md::Code code : allCodes) {
                          const md::CodeInfo& row = md::info(code);
                          // info() falls back to the first row for an unregistered enumerator, so
                          // comparing the round-trip is what actually detects a missing row.
                          checks.expect(row.code == code,
                                        std::format("enumerator {} has its own row",
                                                    static_cast<int>(code)));
                      }
                      checks.expect(md::registry().size() == allCodes.size(),
                                    std::format("table has {} rows, one per enumerator (has {})",
                                                allCodes.size(), md::registry().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyIdIsUnique{
    "No two codes share an identifier",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-unique")
            .Given("the registry", [] {})
            .When("its identifiers are collected", [] {})
            .Then("each appears once", [] {
                mdux::spec::Checks checks;
                std::vector<std::string_view> ids;
                for (const md::CodeInfo& row : md::registry()) {
                    ids.push_back(row.id);
                }
                std::ranges::sort(ids);
                const auto duplicate = std::ranges::adjacent_find(ids);
                checks.expect(duplicate == ids.end(),
                              duplicate == ids.end()
                                  ? "identifiers are unique"
                                  : std::format("'{}' is used twice", *duplicate));
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register everyIdIsWellFormed{
    "Every identifier is MDX-E followed by three digits",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-shape")
            .Given("the registry", [] {})
            .When("each identifier is inspected", [] {})
            .Then("it matches the published prefix and width", [] {
                mdux::spec::Checks checks;
                for (const md::CodeInfo& row : md::registry()) {
                    checks.expect(wellFormedId(row.id),
                                  std::format("'{}' is well formed", row.id));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register everyRowSaysWhatItMeans{
    "Every row carries a non-empty summary",
    "evidence-unit",
    [] {
        // fixHint is deliberately *not* required. The schema calls an empty hint "honest and
        // common", and an invented one worse than none - so this asserts the summary, which is
        // what a reader consults before reusing a code, and leaves the hint optional.
        return speclab::Test("medui-diagnostics-summaries")
            .Given("the registry", [] {})
            .When("each row is inspected", [] {})
            .Then("its summary is present", [] {
                mdux::spec::Checks checks;
                for (const md::CodeInfo& row : md::registry()) {
                    checks.expect(!row.summary.empty(),
                                  std::format("{} has a summary", row.id));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register idsAreOrderedAndUnretired{
    "Identifiers ascend, and none collides with a retired number",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-order")
            .Given("the registry and the retired list", [] {})
            .When("the identifiers are read in table order", [] {})
            .Then("they ascend, and no live code reuses a retired number", [] {
                mdux::spec::Checks checks;
                std::string_view previous;
                for (const md::CodeInfo& row : md::registry()) {
                    if (!previous.empty()) {
                        checks.expect(previous < row.id,
                                      std::format("{} follows {}", row.id, previous));
                    }
                    previous = row.id;
                    const bool reused = std::ranges::find(md::retired(), row.id) != md::retired().end();
                    checks.expect(!reused, std::format("{} is not a retired number", row.id));
                }
                checks.raise();
            })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The envelope the codes travel in.
// ---------------------------------------------------------------------------

const mdux::spec::Register diagnoseCarriesRegisteredIdentity{
    "diagnose() stamps the registered id and severity onto the shared envelope",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-diagnose")
            .Given("a code with a registered fix hint", [] {})
            .When("a diagnostic is built without an explicit hint", [] {})
            .Then("the registry's id, severity and hint are used", [] {
                mdux::spec::Checks checks;
                const cli::Diagnostic d = md::diagnose(md::Code::UnknownColorToken, "screen.medui",
                                                       12, 5, "Theme.Colors.Wrong is not a token");
                checks.expect(d.code == "MDX-E030", std::format("code is MDX-E030, got '{}'", d.code));
                checks.expect(d.severity == cli::Severity::Error, "severity comes from the registry");
                checks.expect(d.line == 12 && d.column == 5, "position is carried through");
                checks.expect(!d.fixHint.empty(), "the registry's fix hint is used when none is given");
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register callerHintWins{
    "A caller-supplied fix hint overrides the registry's",
    "evidence-unit",
    [] {
        return speclab::Test("medui-diagnostics-hint-override")
            .Given("a code whose registry hint is generic", [] {})
            .When("a diagnostic is built with a specific hint", [] {})
            .Then("the specific one survives", [] {
                mdux::spec::Checks checks;
                const cli::Diagnostic d =
                    md::diagnose(md::Code::UnknownColorToken, "screen.medui", 1, 1, "unknown token",
                                 "did you mean Theme.Colors.ScoreDigits?");
                checks.expect(d.fixHint == "did you mean Theme.Colors.ScoreDigits?",
                              std::format("caller hint kept, got '{}'", d.fixHint));
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register positionsMayBeAbsent{
    "A finding about the whole file carries no position",
    "evidence-unit",
    [] {
        // The envelope defines line and column as independent, 0 meaning "no precise position on
        // this axis". A recipe that cannot be opened has neither, and inventing 1:1 would point a
        // reader at a line that says nothing.
        return speclab::Test("medui-diagnostics-no-position")
            .Given("a whole-file failure", [] {})
            .When("it is reported", [] {})
            .Then("line and column are both zero", [] {
                mdux::spec::Checks checks;
                const cli::Diagnostic d = md::diagnose(md::Code::RecipeUnreadable, "recipe.toml", 0,
                                                       0, "could not open recipe.toml");
                checks.expect(d.line == 0 && d.column == 0, "no position invented");
                checks.expect(d.code == "MDX-E000", "code is carried");
                checks.raise();
            })
            .Execute();
    }};
