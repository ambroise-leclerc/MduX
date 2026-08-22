/**
 * @file CheckTests.cpp
 * @brief BDD scenarios for the single-file checker (issue #200).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Driven over the rejection corpus the parser and analyzer suites already use, which is the point of
 * that corpus being real files rather than string literals: the tool #200 adds is pointed at exactly
 * what those suites check by call, so a code that stops surfacing here has stopped surfacing to the
 * author too.
 *
 * The scenarios that matter most are the two about what the checker *cannot* do. A checker whose
 * clean run is silently partial is worse than no checker, because it converts "I did not look" into
 * "nothing is wrong".
 */

import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.medui.check;
import mdux.tools.medui.diagnostics;

#include "../framework/SpecLabBridge.hpp"
#include "../framework/TemporaryDirectory.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace cli = mdux::tools::cli;

[[nodiscard]] std::filesystem::path fixturePath(std::string_view name) {
    return std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
}

[[nodiscard]] std::string fixture(std::string_view name) {
    std::ifstream in{fixturePath(name), std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("fixture {} could not be opened", name), std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] bool carries(const md::CheckResult& result, std::string_view code) {
    return std::ranges::any_of(result.diagnostics, [code](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

[[nodiscard]] std::string codes(const md::CheckResult& result) {
    std::string rendered;
    for (const cli::Diagnostic& diagnostic : result.diagnostics) {
        if (!rendered.empty()) {
            rendered += ", ";
        }
        rendered += diagnostic.code.empty() ? std::string{"<none>"} : diagnostic.code;
    }
    return rendered.empty() ? std::string{"<nothing>"} : rendered;
}


/// One shell command line, spelled the way the platform's shell will actually read it.
///
/// `cmd.exe` strips the outer pair of quotes when a command line begins with one, and reads a
/// leading `/` as a switch. Getting this wrong does not fail honestly - the process never starts and
/// the exit status is non-zero for the wrong reason.
[[nodiscard]] std::string shellCommand(std::string_view program, std::string_view arguments) {
#ifdef _WIN32
    std::string native{program};
    std::ranges::replace(native, '/', '\\');
    return std::format(R"(""{}" {}")", native, arguments);
#else
    return std::format(R"("{}" {})", program, arguments);
#endif
}

[[nodiscard]] std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

const mdux::spec::Register aGoodScreenPassesWithItsGapsNamed{
    "A screen that checks out says which checks did not run",
    "evidence-unit",
    [] {
        return speclab::Test("medui-check-accepts")
            .Given("a screen the compiler accepts", [] {})
            .When("it is checked on its own", [] {})
            .Then("nothing of error severity is reported, and the uncovered checks are named",
                  [] {
                      mdux::spec::Checks    checks;
                      const md::CheckResult result = md::checkScreen(fixture("accepted-textless.medui"), "accepted-textless.medui");

                      checks.expect(result.ok(), std::format("a good screen passes, got {}", codes(result)));
                      checks.expect(result.layoutChecked, "and its layout was resolved, since it declares a surface");

                      // The property that makes a clean run trustworthy: it is visibly partial. A
                      // checker that stayed silent here would convert "I did not look" into
                      // "nothing is wrong", which is worse than not existing.
                      checks.expect(!result.textChecked, "text keys were not checked without a recipe");
                      checks.expect(carries(result, "MDC001"), std::format("and the run says so, got {}", codes(result)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyRejectionCodeSurfaces{"The rejection corpus surfaces through the checker", "evidence-unit", [] {
                                                          return speclab::Test("medui-check-rejects")
                                                              .Given("the fixtures the parser and analyzer suites drive by call", [] {})
                                                              .When("the checker is pointed at each of them", [] {})
                                                              .Then("each reports the code its name promises",
                                                                    [] {
                                                                        mdux::spec::Checks checks;

                                                                        // Pointed at the same corpus rather than at strings of its own: a code that
                                                                        // stops surfacing here has stopped surfacing to an author, and that is what
                                                                        // the shared fixtures exist to make checkable.
                                                                        const std::array<std::pair<std::string_view, std::string_view>, 4> cases{
                                                                            {{"rejected-unterminated-string.medui", "MEDUI-E010"},
                                                                             {"rejected-nested-row.medui", "MEDUI-E015"},
                                                                             {"rejected-duplicate-id.medui", "MEDUI-E014"},
                                                                             {"rejected-safety-without-requirement.medui", "MEDUI-E070"}}
                                                                        };

                                                                        for (const auto& [name, code] : cases) {
                                                                            const md::CheckResult result = md::checkScreen(fixture(name), std::string{name});
                                                                            checks.expect(!result.ok(), std::format("{} is rejected", name));
                                                                            checks.expect(carries(result, code),
                                                                                          std::format("{} reports {}, got {}", name, code, codes(result)));
                                                                        }
                                                                        checks.raise();
                                                                    })
                                                              .Execute();
                                                      }};

const mdux::spec::Register aScreenWithoutASurfaceSaysWhatWasSkipped{
    "A screen with no surface is checked as far as it can be, and says where that stopped",
    "evidence-unit",
    [] {
        return speclab::Test("medui-check-no-surface")
            .Given("a screen that declares no `surface:`", [] {})
            .When("it is checked", [] {})
            .Then("it is not called malformed, and layout is reported as unchecked",
                  [] {
                      mdux::spec::Checks checks;

                      // Not a finding about the file: the compiler takes the surface from the
                      // recipe, so a screen without one is legitimate and simply cannot be resolved
                      // to rectangles here.
                      const std::string source = "Screen NoSurface {\n"
                                                 "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
                                                 "\n"
                                                 "    Clock {\n"
                                                 "        id: wall-clock;\n"
                                                 "        width: 200px;\n"
                                                 "        height: 40px;\n"
                                                 "        format: TimeSeconds;\n"
                                                 "    }\n"
                                                 "}\n";

                      const md::CheckResult result = md::checkScreen(source, "no-surface.medui");
                      checks.expect(result.ok(), std::format("a screen without a surface is not an error, got {}", codes(result)));
                      checks.expect(!result.layoutChecked, "and its layout was not resolved");
                      checks.expect(carries(result, "MDC002"), std::format("with the gap named, got {}", codes(result)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theCheckerStopsAtTheCause{"The checker stops at the first stage that rejects the screen", "evidence-unit", [] {
                                                         return speclab::Test("medui-check-stops-at-the-cause")
                                                             .Given("a file that does not parse", [] {})
                                                             .When("it is checked", [] {})
                                                             .Then("the parse error is reported alone, without consequences from later stages",
                                                                   [] {
                                                                       mdux::spec::Checks    checks;
                                                                       const md::CheckResult result = md::checkScreen(
                                                                           fixture("rejected-unterminated-string.medui"),
                                                                           "rejected-unterminated-string.medui");

                                                                       checks.expect(!result.ok(), "the file is rejected");
                                                                       // A later stage reading a screen an earlier one rejected reports consequences
                                                                       // rather than causes - and the notes about uncovered checks are not emitted
                                                                       // either, since the run did not get far enough to have gaps worth naming.
                                                                       checks.expect(!carries(result, "MDC001") && !carries(result, "MDC002"),
                                                                                     std::format("no notes about later stages, got {}", codes(result)));
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};

const mdux::spec::Register theToolExitsAndSpeaksTheSharedEnvelope{
    "The tool exits non-zero on an error and prints the shared envelope",
    "evidence-unit",  // the one scenario here that spawns mdux-medui-check rather than calling into it
    [] {
        return speclab::Test("medui-check-cli")
            .Given("mdux-medui-check and a broken screen", [] {})
            .When("it is run with --format=json", [] {})
            .Then("it exits non-zero having printed the envelope every MduX tool emits",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-medui-check-cli"};

                      const std::filesystem::path log     = scratch.path() / "stdout.txt";
                      const std::string           broken  = fixturePath("rejected-safety-without-requirement.medui").generic_string();
                      const std::string           command = shellCommand(MDUX_MEDUI_CHECK_PATH,
                                                               std::format(R"("{}" --format=json > "{}")", broken, log.generic_string()));
                      const int                   status  = std::system(command.c_str());
                      checks.expect(status != 0, "a broken screen exits non-zero");

                      const std::string envelope = contentsOf(log);
                      // Asserted before the contents: an empty log means the process never ran, and
                      // the status above was non-zero for a reason unrelated to the screen.
                      checks.expect(!envelope.empty(), "the checker ran and printed something");
                      checks.expect(envelope.contains("mdux-medui-check"), "the envelope names the tool");
                      checks.expect(envelope.contains("MEDUI-E070"), std::format("and carries the finding, got:\n{}", envelope));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aCleanFileExitsZero{
    "A file that checks out exits zero, notes and all",
    "evidence-unit",
    [] {
        return speclab::Test("medui-check-cli-clean")
            .Given("a screen the compiler accepts", [] {})
            .When("the tool is run over it", [] {})
            .Then("it exits zero, because a note is not a failure",
                  [] {
                      mdux::spec::Checks             checks;
                      mdux::test::TemporaryDirectory scratch{"mdux-medui-check-clean"};

                      // The half that would be easy to get wrong: the notes about uncovered checks
                      // travel in the same envelope as findings, and an exit status keyed off the
                      // presence of diagnostics rather than their severity would fail every clean
                      // run.
                      const std::filesystem::path log  = scratch.path() / "stdout.txt";
                      const std::string           good = fixturePath("accepted-textless.medui").generic_string();
                      const std::string command = shellCommand(MDUX_MEDUI_CHECK_PATH, std::format(R"("{}" --format=json > "{}")", good, log.generic_string()));
                      const int         status  = std::system(command.c_str());
                      checks.expect(status == 0, "a clean screen exits zero");

                      const std::string envelope = contentsOf(log);
                      checks.expect(envelope.contains("MDC001"), std::format("and still reports what it could not check, got:\n{}", envelope));
                      checks.raise();
                  })
            .Execute();
    }};
