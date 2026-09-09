/**
 * @file ScenarioScriptTests.cpp
 * @brief BDD scenarios for the `.scenario` line parser, the baker and the emitter (#319, ADR-020).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * The parser is driven as a call so a failing assertion names the `SCN0NN` code that failed rather
 * than an exit status. Every rejection scenario asserts the specific code, because #191's rule -
 * codes are the contract, messages are rewordable - applies to this tool too. The bake and the
 * emit are exercised over both a fixture screen and the committed one.
 */

import std;
import speclab;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.tools.cli;
import mdux.tools.scenario;
import mdux.tools.scenario.script;
import mdux.tools.scenarioemit;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace cli = mdux::tools::cli;
namespace sc  = mdux::tools::scenario;
namespace ms  = mdux::medui;

[[nodiscard]] std::filesystem::path repoRoot() { return std::filesystem::path{MDUX_REPO_ROOT}; }

/// A hand-built node set so the parser tests do not depend on the committed screen's exact layout.
[[nodiscard]] std::vector<sc::ScreenNode> fixtureNodes() {
    return {
        sc::ScreenNode{.id = "patient-id", .kind = "TextInput", .centreX = 100, .centreY = 20},
        sc::ScreenNode{.id = "freeze", .kind = "Button", .centreX = 200, .centreY = 20},
        sc::ScreenNode{.id = "emergency-halt", .kind = "CriticalButton", .centreX = 300, .centreY = 20},
        sc::ScreenNode{.id = "classifier-state", .kind = "StatusIndicator", .centreX = 400, .centreY = 20},
        sc::ScreenNode{.id = "insufflation-pressure", .kind = "NumericDisplay", .centreX = 500, .centreY = 120},
    };
}

/// The minimal valid header every step fixture prepends.
constexpr std::string_view header =
    "scenario s\nscreen screen-x\nversion 1\nclock 2026-01-02 03:04:05\n";

[[nodiscard]] std::optional<sc::Script> parse(std::string_view body, std::vector<cli::Diagnostic>& diags,
                                              std::string_view screen = "screen-x") {
    const std::string text = std::string{header} + std::string{body};
    return sc::parseScript(text, "fixture.scenario", screen, fixtureNodes(), diags);
}

[[nodiscard]] std::string firstCode(std::span<const cli::Diagnostic> diags) {
    return diags.empty() ? std::string{"<none>"} : diags.front().code;
}

const mdux::spec::Register aValidScriptParses{
    "A well-formed .scenario parses into ordered steps with the header resolved",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-script-valid")
            .Given("a header, one event, an advance and two expectations", [] {})
            .When("parseScript() runs", [] {})
            .Then("the steps come out in order and the clock is pinned",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> diags;
                      auto script = parse("pointer down freeze\nadvance\nexpect button freeze FREEZE\ncapture c\n", diags);
                      checks.expect(script.has_value(), std::format("it parsed, first code {}", firstCode(diags)));
                      if (script) {
                          checks.expect(script->id == "s" && script->screenId == "screen-x", "identity");
                          checks.expect(script->clock.year == 2026 && script->clock.minute == 4, "clock pinned");
                          checks.expect(script->steps.size() == 4, std::format("4 steps, got {}", script->steps.size()));
                          checks.expect(script->steps[0].kind == ms::StepKind::Pointer, "first is the pointer");
                          checks.expect(script->steps[1].kind == ms::StepKind::Advance, "then the advance");
                          checks.expect(script->captureNames.size() == 1 && script->captureNames[0] == "c", "capture recorded");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

struct RejectionCase {
    std::string_view what;
    std::string_view code;
    std::string_view body;
    std::string_view screen{"screen-x"};
};

const mdux::spec::Register everyRejectionPathHasItsCode{
    "The .scenario parser emits the right stable SCN0NN code per failure mode",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-script-rejections")
            .Given("one malformed script per code", [] {})
            .When("parseScript() runs on each", [] {})
            .Then("each is refused with its documented code",
                  [] {
                      const std::vector<RejectionCase> cases{
                          {"an unknown directive", "SCN001", "wobble\n"},
                          {"an unknown expect kind", "SCN001", "advance\nexpect nonsense 1\n"},
                          {"an unknown KeyCode", "SCN001", "key Frobnicate\nadvance\n"},
                          {"a repeated header line", "SCN002", "advance\nscenario again\n"},
                          {"more requirements than the bound", "SCN003",
                           "requirement A-1 A-2 A-3 A-4 A-5 A-6 A-7 A-8 A-9 A-10 A-11 A-12 A-13 A-14 A-15 A-16 A-17\nadvance\n"},
                          {"a mismatched screen line", "SCN005", "advance\n", "another-screen"},
                          {"a pointer at a missing node", "SCN005", "pointer down nope\nadvance\n"},
                          {"a malformed clock in the header", "SCN006", ""},  // replaced below
                          {"a bad expect arity", "SCN007", "advance\nexpect refused\n"},
                          {"a reused capture name", "SCN007", "advance\ncapture c\ncapture c\n"},
                          {"events after the last advance", "SCN008", "advance\ntext A\n"},
                          {"no advance at all", "SCN008", "pointer down freeze\n"},
                      };

                      mdux::spec::Checks checks;
                      for (const RejectionCase& c : cases) {
                          if (c.code == "SCN006") {
                              std::vector<cli::Diagnostic> diags;
                              auto script = sc::parseScript("scenario s\nscreen screen-x\nversion 1\nclock 2026-13-40 99:99:99\nadvance\n",
                                                            "fixture.scenario", "screen-x", fixtureNodes(), diags);
                              checks.expect(!script.has_value() && firstCode(diags) == "SCN006",
                                            std::format("{} -> SCN006, got {}", c.what, firstCode(diags)));
                              continue;
                          }
                          std::vector<cli::Diagnostic> diags;
                          auto script = parse(c.body, diags, c.screen);
                          checks.expect(!script.has_value() && firstCode(diags) == c.code,
                                        std::format("{} -> {}, got {}", c.what, c.code, firstCode(diags)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theCommittedScenarioBakesAndVerifies{
    "The committed endoscope-monitor-basics scenario compiles, and re-baking it is byte-identical",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-committed-bake")
            .Given("the committed recipe and .scenario source", [] {})
            .When("run() produces scenario.json twice and verify() compares the committed bundle", [] {})
            .Then("both bakes are byte-identical and the committed bundle verifies",
                  [] {
                      mdux::spec::Checks checks;
                      const std::filesystem::path recipePath = repoRoot() / "recipes/scenario/endoscope-monitor-basics.toml";
                      std::ifstream               in{recipePath, std::ios::binary};
                      const std::string recipeText{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                      const auto        recipeBytes = std::as_bytes(std::span{recipeText.data(), recipeText.size()});

                      std::vector<cli::Diagnostic> d1;
                      auto recipe = sc::parseRecipe(recipeText, "recipes/scenario/endoscope-monitor-basics.toml", d1);
                      checks.expect(recipe.has_value(), std::format("the recipe parses, first {}", firstCode(d1)));
                      if (!recipe) {
                          checks.raise();
                          return;
                      }

                      std::vector<cli::Diagnostic> d2;
                      auto first  = sc::run(*recipe, "recipes/scenario/endoscope-monitor-basics.toml", recipeBytes, repoRoot(), d2);
                      std::vector<cli::Diagnostic> d3;
                      auto second = sc::run(*recipe, "recipes/scenario/endoscope-monitor-basics.toml", recipeBytes, repoRoot(), d3);
                      checks.expect(first.has_value() && second.has_value(),
                                    std::format("both bakes succeed, first {}", firstCode(d2)));
                      if (first && second) {
                          checks.expect(first->scenarioJson == second->scenarioJson, "two bakes produce one scenario.json");
                          checks.expect(first->reportJson == second->reportJson, "two bakes produce one report.json");
                          checks.expect(first->stepCount == 28, std::format("28 steps, got {}", first->stepCount));

                          std::vector<cli::Diagnostic> dv;
                          const bool verified = sc::verify(*first, repoRoot() / "generated/scenario/endoscope-monitor-basics/scenario.json",
                                                           repoRoot() / "generated/scenario/endoscope-monitor-basics/report.json", dv);
                          checks.expect(verified, std::format("the committed bundle matches the fresh bake, first {}", firstCode(dv)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theEmitterRendersAValidModule{
    "mdux-scenarioemit renders a module whose scenario re-reads and re-emits identically",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-emit-roundtrip")
            .Given("the committed scenario.json", [] {})
            .When("renderScenario() runs twice", [] {})
            .Then("both renderings are byte-identical and the module names the scenario",
                  [] {
                      mdux::spec::Checks           checks;
                      std::vector<cli::Diagnostic> d1;
                      auto a = sc::renderScenario(repoRoot() / "generated/scenario/endoscope-monitor-basics/scenario.json", d1);
                      std::vector<cli::Diagnostic> d2;
                      auto b = sc::renderScenario(repoRoot() / "generated/scenario/endoscope-monitor-basics/scenario.json", d2);
                      checks.expect(a.has_value() && b.has_value(), std::format("both render, first {}", firstCode(d1)));
                      if (a && b) {
                          checks.expect(a->moduleSource == b->moduleSource, "two renderings produce one module");
                          checks.expect(a->moduleName == "mdux.medui.generated.scenario_endoscope_monitor_basics", "module name");
                          checks.expect(a->moduleSource.find("static_assert(scenario.validate()") != std::string::npos,
                                        "the module carries the validation gate");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register identifierParity{
    "identifierForScenario maps an id the same way the CMake helper does",
    "evidence-unit",
    [] {
        return speclab::Test("scenario-identifier")
            .Given("a set of ids", [] {})
            .When("identifierForScenario() maps each", [] {})
            .Then("the mapping is prefixed and injective on separators",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(sc::identifierForScenario("endoscope-monitor-basics") == "scenario_endoscope_monitor_basics", "hyphens");
                      checks.expect(sc::identifierForScenario("a.b") == "scenario_a_b", "dots");
                      checks.expect(sc::identifierForScenario("class") == "scenario_class", "a keyword id is still an identifier");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
