/**
 * @file DriverTests.cpp
 * @brief Direct-library tests for complete obligation planning and CLI policy.
 */
import std;
import speclab;
import mdux.draw;
import mdux.evidence.report;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.verify.driver;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"

namespace {
namespace ms = mdux::medui;
namespace mv = mdux::verify;
namespace vu = mdux::tools::verify;

constexpr mdux::draw::DrawBudget budget{.maxVertices = 8, .maxIndices = 12, .maxCommands = 2};
constexpr ms::PanelSpec          panel{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr std::array             textlessNodes{
    ms::CompiledNode{.id = "background", .bounds = {0, 0, 16, 16}, .payload = panel}
};
constexpr ms::ScreenPackage textless{.id                   = "textless",
                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                     .surfaceWidth         = 16,
                                     .surfaceHeight        = 16,
                                     .approvedTextPackages = {},
                                     .nodes                = textlessNodes,
                                     .budget               = budget};
constexpr std::array        goldenChecks{mv::CvCheck::Bounds, mv::CvCheck::ColorHash};
constexpr std::array        textlessGoldens{
    mv::GoldenEntry{.nodeId = "background", .bounds = {0, 0, 16, 16}, .textKey = {}, .colorToken = "Theme.Colors.TopbarBackground", .cvChecks = goldenChecks}
};

constexpr std::array approvals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "screen-en-us", .packageSha256 = {1}},
    ms::TextPackageApproval{.locale = "fr-FR", .packageId = "screen-fr-fr", .packageSha256 = {2}}
};
constexpr ms::LabelSpec label{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};
constexpr std::array    localizedNodes{
    ms::CompiledNode{.id = "title", .bounds = {0, 0, 16, 8}, .payload = label}
};
constexpr ms::ScreenPackage localized{.id                   = "localized",
                                      .schemaVersion        = mdux::evidence::kSchemaVersion,
                                      .surfaceWidth         = 16,
                                      .surfaceHeight        = 16,
                                      .approvedTextPackages = approvals,
                                      .nodes                = localizedNodes,
                                      .budget               = budget};
constexpr std::array        localizedGoldens{
    mv::GoldenEntry{.nodeId = "title", .bounds = {0, 0, 16, 8}, .textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title", .cvChecks = goldenChecks}
};

static_assert(textless.validate().has_value());
static_assert(localized.validate().has_value());

const mdux::spec::Register textlessPlan{"A textless screen keeps one locale-free render scope", "evidence-unit", [] {
                                            return speclab::Test("verify-ui-textless-plan")
                                                .Given("one golden declaring both governed checks", [] {})
                                                .When("the library builds the plan", [] {})
                                                .Then("both obligations survive the empty locale manifest",
                                                      [] {
                                                          const vu::PlanResult plan = vu::enumerate(textless, textlessGoldens);
                                                          mdux::spec::Checks   assertions;
                                                          assertions.expect(plan.ok(), "the plan is valid");
                                                          assertions.expect(plan.obligations.size() == 2, "both golden checks are present");
                                                          assertions.expect(std::ranges::all_of(plan.obligations,
                                                                                                [](const vu::Obligation& item) {
                                                                                                    return item.scope == mv::localeFreeScopeName;
                                                                                                }),
                                                                            "both name the explicit locale-free scope");
                                                          assertions.raise();
                                                      })
                                                .Execute();
                                        }};

const mdux::spec::Register localizedPlan{"An unpositioned text node receives two checks per approved locale", "evidence-unit", [] {
                                             return speclab::Test("verify-ui-localized-plan")
                                                 .Given("one text node, no golden, and two approvals", [] {})
                                                 .When("the library builds the plan", [] {})
                                                 .Then("four mandatory text obligations are present",
                                                       [] {
                                                           const vu::PlanResult plan = vu::enumerate(localized, {});
                                                           mdux::spec::Checks   assertions;
                                                           assertions.expect(plan.ok(), "no golden is required for a text obligation");
                                                           assertions.expect(plan.obligations.size() == 4, "two checks times two locales");
                                                           assertions.expect(std::ranges::count_if(plan.obligations,
                                                                                                   [](const vu::Obligation& item) {
                                                                                                       return item.check
                                                                                                              == mv::spell(mv::TextCheck::InkContainment);
                                                                                                   })
                                                                                 == 2,
                                                                             "InkContainment occurs once per locale");
                                                           assertions.expect(
                                                               std::ranges::count_if(plan.obligations,
                                                                                     [](const vu::Obligation& item) {
                                                                                         return item.check == mv::spell(mv::TextCheck::LocalizedTextPresence);
                                                                                     })
                                                                   == 2,
                                                               "LocalizedTextPresence occurs once per locale");
                                                           assertions.raise();
                                                       })
                                                 .Execute();
                                         }};

const mdux::spec::Register goldenLocaleProduct{"Every golden check is repeated in every approved render scope", "evidence-unit", [] {
                                                   return speclab::Test("verify-ui-golden-locale-product")
                                                       .Given("one two-check golden and two approved locales", [] {})
                                                       .When("the library builds the complete plan", [] {})
                                                       .Then(
                                                           "golden and mandatory text obligations both form their full products",
                                                           [] {
                                                               const vu::PlanResult plan = vu::enumerate(localized, localizedGoldens);
                                                               const auto goldenCount = std::ranges::count_if(plan.obligations, [](const vu::Obligation& item) {
                                                                   return item.kind == vu::ObligationKind::Golden;
                                                               });
                                                               const auto textCount   = std::ranges::count_if(plan.obligations, [](const vu::Obligation& item) {
                                                                   return item.kind == vu::ObligationKind::Text;
                                                               });
                                                               mdux::spec::Checks assertions;
                                                               assertions.expect(plan.ok(), "the plan is valid");
                                                               assertions.expect(goldenCount == 4, "two golden checks times two scopes");
                                                               assertions.expect(textCount == 4, "two mandatory text checks times two locales");
                                                               assertions.expect(plan.obligations.size() == 8, "the products are accumulated, not substituted");
                                                               assertions.raise();
                                                           })
                                                       .Execute();
                                               }};

const mdux::spec::Register zeroPlan{"A zero-obligation screen is not a successful plan", "evidence-unit", [] {
                                        return speclab::Test("verify-ui-zero-plan")
                                            .Given("a textless screen and no golden", [] {})
                                            .When("the library builds the plan", [] {})
                                            .Then("it fails closed",
                                                  [] {
                                                      const vu::PlanResult plan = vu::enumerate(textless, {});
                                                      mdux::spec::Checks   assertions;
                                                      assertions.expect(!plan.ok(), "the empty plan is rejected");
                                                      assertions.expect(plan.obligations.empty(), "nothing was invented to make it pass");
                                                      assertions.raise();
                                                  })
                                            .Execute();
                                    }};

const mdux::spec::Register zeroRun{"A zero-obligation run is a verification failure", "evidence-unit", [] {
                                       return speclab::Test("verify-ui-zero-run")
                                           .Given("a canonical textless bundle with no golden", [] {})
                                           .When("the production driver plans the run", [] {})
                                           .Then("it returns status one without creating a Vulkan device",
                                                 [] {
                                                     const std::filesystem::path root{MDUX_REPO_ROOT};
                                                     const vu::RunResult         result = vu::run(root / "tests/verify_ui/fixtures/zero", root / "generated");
                                                     mdux::spec::Checks          assertions;
                                                     assertions.expect(result.state == vu::RunState::ChecksFailed,
                                                                       "an empty verification is a verification failure");
                                                     assertions.expect(vu::exitStatus(result.state) == 1, "CI receives the failure status");
                                                     assertions.expect(result.renderCount == 0, "planning fails before rendering");
                                                     assertions.expect(result.outcomes.empty(), "no outcome is invented");
                                                     assertions.expect(std::ranges::any_of(result.diagnostics,
                                                                                           [](const auto& diagnostic) {
                                                                                               return diagnostic.code == "VUI004";
                                                                                           }),
                                                                       "the zero-obligation diagnostic is retained");
                                                     assertions.raise();
                                                 })
                                           .Execute();
                                   }};

const mdux::spec::Register allLocalesCli{"The exact all-locales invocation is accepted", "evidence-unit", [] {
                                             return speclab::Test("verify-ui-all-locales-cli")
                                                 .Given("a committed screen path", [] {})
                                                 .When("the CLI boundary parses it", [] {})
                                                 .Then("the path and format are retained",
                                                       [] {
                                                           constexpr std::array arguments{std::string_view{"--screen=generated/screen/demo"},
                                                                                          std::string_view{"--locales=all"},
                                                                                          std::string_view{"--format=json"}};
                                                           const auto           invocation = vu::parseArguments(arguments);
                                                           mdux::spec::Checks   assertions;
                                                           assertions.expect(invocation.screenDirectory == "generated/screen/demo", "the screen is selected");
                                                           assertions.expect(invocation.format == mdux::tools::cli::Format::Json,
                                                                             "the shared JSON envelope is selected");
                                                           assertions.raise();
                                                       })
                                                 .Execute();
                                         }};

const mdux::spec::Register trailingSeparatorCli{"A trailing screen separator is normalized", "evidence-unit", [] {
                                                    return speclab::Test("verify-ui-trailing-screen-separator")
                                                        .Given("the path form commonly produced by shell completion", [] {})
                                                        .When("the CLI boundary parses it", [] {})
                                                        .Then("identity and artifact-root derivation see the bundle directory",
                                                              [] {
                                                                  constexpr std::array arguments{std::string_view{"--screen=generated/screen/demo/"},
                                                                                                 std::string_view{"--locales=all"}};
                                                                  const auto           invocation = vu::parseArguments(arguments);
                                                                  mdux::spec::Checks   assertions;
                                                                  assertions.expect(invocation.screenDirectory == "generated/screen/demo",
                                                                                    "the trailing separator is removed");
                                                                  assertions.raise();
                                                              })
                                                        .Execute();
                                                }};

const mdux::spec::Register subsetCli{"A locale subset is rejected", "evidence-unit", [] {
                                         return speclab::Test("verify-ui-subset-cli")
                                             .Given("a request for one locale", [] {})
                                             .When("the CLI boundary parses it", [] {})
                                             .Then("it is a usage error",
                                                   [] {
                                                       constexpr std::array arguments{std::string_view{"--screen=generated/screen/demo"},
                                                                                      std::string_view{"--locales=en-US"}};
                                                       bool                 rejected = false;
                                                       try {
                                                           static_cast<void>(vu::parseArguments(arguments));
                                                       } catch (const mdux::tools::cli::UsageError&) {
                                                           rejected = true;
                                                       }
                                                       mdux::spec::Checks assertions;
                                                       assertions.expect(rejected, "approved locales cannot be narrowed");
                                                       assertions.raise();
                                                   })
                                             .Execute();
                                     }};

const mdux::spec::Register distinctExitStatuses{"Check failure and impossible execution have different statuses", "evidence-unit", [] {
                                                    return speclab::Test("verify-ui-exit-statuses")
                                                        .Given("the three driver outcomes", [] {})
                                                        .When("they cross the process boundary", [] {})
                                                        .Then("none is confused with another",
                                                              [] {
                                                                  mdux::spec::Checks assertions;
                                                                  assertions.expect(vu::exitStatus(vu::RunState::Passed) == 0, "pass is zero");
                                                                  assertions.expect(vu::exitStatus(vu::RunState::ChecksFailed) == 1, "a made check failed");
                                                                  assertions.expect(vu::exitStatus(vu::RunState::CouldNotRun) == 3,
                                                                                    "the run could not be made");
                                                                  assertions.raise();
                                                              })
                                                        .Execute();
                                                }};

}  // namespace
