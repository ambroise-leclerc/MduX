/**
 * @brief Scenarios for the governed compiled-screen schema (issue #197).
 * @file SchemaTests.cpp
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * The `static_assert` below is the point of the file. ADR-012 decision 3 puts
 * `static_assert(package.validate().has_value())` in every generated screen, so "this type can be
 * validated at compile time" is a promise the device build depends on rather than a nicety - and a
 * promise a runtime test cannot check, because a runtime test proves only that it also works at
 * run time. The scenarios that follow cover the rejections, which a `static_assert` cannot express.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.draw;
import mdux.evidence.report;
import mdux.medui.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms = mdux::medui;

// ---------------------------------------------------------------------------
// Compile-time evidence for the constexpr claim
// ---------------------------------------------------------------------------

// Payloads named separately, so the node list stays one line per node. Inline, clang-format's
// alignment of long designated initialisers pads the block into something no reviewer can scan -
// the formatter is satisfied and the reader is not, which is the wrong trade in a fixture whose
// job is to be read.
constexpr ms::PanelSpec          topbarPanel{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::LabelSpec          titleLabel{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};
constexpr ms::NumericDisplaySpec scoreDisplay{.requirement = "REQ-NS-001",
                                              .templateId  = "TPL-SEDATION-INDEX-160",
                                              .source      = "SEDATION_INDEX",
                                              .colorToken  = "Theme.Colors.ScoreDigits"};

constexpr std::array<ms::CompiledNode, 3> constNodes{
    ms::CompiledNode{.id = "topbar-background",    .bounds = {0, 0, 400, 40},  .payload = topbarPanel},
    ms::CompiledNode{            .id = "title",    .bounds = {8, 8, 200, 24},   .payload = titleLabel},
    ms::CompiledNode{            .id = "score", .bounds = {250, 60, 120, 60}, .payload = scoreDisplay}
};

constexpr ms::ScreenPackage constPackage{
    .id            = "neurosense",
    .schemaVersion = mdux::evidence::kSchemaVersion,
    .surfaceWidth  = 400,
    .surfaceHeight = 300,
    .nodes         = constNodes,
    .budget        = mdux::draw::DrawBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16}
};

// The module promises a generated screen can be validated at compile time and placed in read-only
// memory. This is that promise, mechanically checked - and it is what ADR-012's generated
// `static_assert` rests on.
static_assert(constPackage.validate().has_value(), "the reference screen must validate at compile time");
static_assert(constPackage.find("score") != nullptr, "find() resolves a node at compile time");
static_assert(constPackage.find("absent") == nullptr, "find() answers a miss without a runtime lookup");
static_assert(ms::kindName(constNodes[1]) == "Label", "a node names its component at compile time");
// `holds_alternative` for the claim, and a field read for the value. An earlier revision asserted
// `get_if(...) != nullptr`, which GCC rejected under -Waddress and was right to: for a variant whose
// alternative is known at compile time, the pointer can never be null, so the assertion asserted
// nothing.
static_assert(std::holds_alternative<ms::LabelSpec>(constNodes[1].payload), "a node's payload is its own component's type");
static_assert(std::get_if<ms::LabelSpec>(&constNodes[1].payload)->textKey == "STR-TITLE", "and its fields are readable at compile time, without std::get");
static_assert(ms::requirementOf(constNodes[2]) == "REQ-NS-001", "a traced node yields its requirement");
static_assert(ms::requirementOf(constNodes[1]).empty(), "and an untraced one yields nothing rather than a placeholder");

/**
 * @brief The palette transcribed from TrustSC's `THEME_COLORS`, by hand and independently.
 *
 * A second copy of data the module already holds, which this suite would normally refuse - and the
 * exception is the point. The parity claim is that a token renders the same colour in both projects,
 * so a check reading `themeColors` for both its input and its expectation would stay green while an
 * entry drifted, which is the one failure this table exists to prevent. Compared against a
 * transcription, a drift has to survive two edits in two files to go unnoticed.
 */
constexpr std::array<ms::ThemeColor, 8> expectedPalette{
    ms::ThemeColor{.token = "Theme.Colors.TopbarBackground", .value = {0.82F, 0.84F, 0.86F, 1.0F}},
    ms::ThemeColor{           .token = "Theme.Colors.Title", .value = {0.10F, 0.12F, 0.16F, 1.0F}},
    ms::ThemeColor{     .token = "Theme.Colors.ScoreDigits", .value = {0.13F, 0.72F, 0.42F, 1.0F}},
    ms::ThemeColor{         .token = "Theme.Colors.Nominal", .value = {0.13F, 0.72F, 0.42F, 1.0F}},
    ms::ThemeColor{           .token = "Theme.Colors.Alert", .value = {0.95F, 0.65F, 0.15F, 1.0F}},
    ms::ThemeColor{           .token = "Theme.Colors.Fault", .value = {0.86F, 0.20F, 0.18F, 1.0F}},
    ms::ThemeColor{         .token = "Theme.Colors.Neutral", .value = {0.62F, 0.66F, 0.70F, 1.0F}},
    ms::ThemeColor{   .token = "Theme.Colors.PrimaryAction", .value = {0.16F, 0.44F, 0.86F, 1.0F}}
};

// The governed table is the module's own, so there is no fixture to build here - only the promise
// that a name resolves at compile time, which is what generated code and #16's verifier both rest on.
static_assert(ms::resolveColorToken("Theme.Colors.ScoreDigits").has_value(), "a name the governed table defines resolves at compile time");
static_assert(ms::resolveColorToken("Theme.Colors.ScoreDigits").value() == std::array<float, 4>{0.13F, 0.72F, 0.42F, 1.0F},
              "and resolves to the colour TrustSC's THEME_COLORS carries for it");

// ---------------------------------------------------------------------------
// A mutable equivalent, so each rejection can change exactly one field
// ---------------------------------------------------------------------------

/// Owns what the package's spans point at, so a mutated fixture stays self-consistent.
struct Fixture {
    std::vector<ms::CompiledNode> nodes{constNodes.begin(), constNodes.end()};
    std::string                   id{"neurosense"};
    std::uint64_t                 schemaVersion{mdux::evidence::kSchemaVersion};
    std::int32_t                  surfaceWidth{400};
    std::int32_t                  surfaceHeight{300};
    mdux::draw::DrawBudget        budget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

    [[nodiscard]] ms::ScreenPackage package() const noexcept {
        return ms::ScreenPackage{.id            = id,
                                 .schemaVersion = schemaVersion,
                                 .surfaceWidth  = surfaceWidth,
                                 .surfaceHeight = surfaceHeight,
                                 .nodes         = nodes,
                                 .budget        = budget};
    }
};

/// The error a mutated fixture reports, or nothing when it validates.
[[nodiscard]] std::optional<ms::SchemaError> errorOf(const Fixture& fixture) {
    const auto result = fixture.package().validate();
    return result.has_value() ? std::nullopt : std::optional{result.error()};
}

[[nodiscard]] std::string describe(std::optional<ms::SchemaError> error) {
    return error.has_value() ? std::string{ms::describe(*error)} : std::string{"accepted"};
}

}  // namespace

const mdux::spec::Register referenceScreenValidates{"The reference screen validates, so every rejection below changes exactly one thing", "evidence-unit", [] {
                                                        return speclab::Test("medui-schema-reference-validates")
                                                            .Given("a three-node screen on a 400x300 surface", [] {})
                                                            .When("it is validated", [] {})
                                                            .Then("it is accepted, at run time as well as at compile time",
                                                                  [] {
                                                                      mdux::spec::Checks checks;
                                                                      checks.expect(!errorOf(Fixture{}).has_value(),
                                                                                    std::format("the fixture validates, got {}", describe(errorOf(Fixture{}))));
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register identityIsRequired{"A screen with no id and one with an unreadable version are both refused", "evidence-unit", [] {
                                                  return speclab::Test("medui-schema-identity")
                                                      .Given("the reference screen", [] {})
                                                      .When("its id is emptied, and its schemaVersion moved past what this module reads", [] {})
                                                      .Then("each is refused with the error that names it",
                                                            [] {
                                                                mdux::spec::Checks checks;

                                                                Fixture unnamed;
                                                                unnamed.id.clear();
                                                                checks.expect(errorOf(unnamed) == ms::SchemaError::EmptyId,
                                                                              std::format("an unnamed screen is refused, got {}", describe(errorOf(unnamed))));

                                                                Fixture future;
                                                                future.schemaVersion = mdux::evidence::kSchemaVersion + 1;
                                                                checks.expect(
                                                                    errorOf(future) == ms::SchemaError::UnsupportedSchemaVersion,
                                                                    std::format("a version this module cannot read is refused rather than guessed at, got {}",
                                                                                describe(errorOf(future))));
                                                                checks.raise();
                                                            })
                                                      .Execute();
                                              }};

const mdux::spec::Register nodesAreAddressable{"Every node must be addressable, and addressable by exactly one id", "evidence-unit", [] {
                                                   return speclab::Test("medui-schema-node-ids")
                                                       .Given("the reference screen", [] {})
                                                       .When("a node loses its id, and two nodes share one", [] {})
                                                       .Then("both are refused, because a golden or a requirement could name either",
                                                             [] {
                                                                 mdux::spec::Checks checks;

                                                                 Fixture anonymous;
                                                                 anonymous.nodes[1].id = {};
                                                                 checks.expect(errorOf(anonymous) == ms::SchemaError::EmptyNodeId,
                                                                               std::format("an unnamed node is refused, got {}", describe(errorOf(anonymous))));

                                                                 Fixture duplicated;
                                                                 duplicated.nodes[2].id = duplicated.nodes[1].id;
                                                                 checks.expect(errorOf(duplicated) == ms::SchemaError::DuplicateNodeId,
                                                                               std::format("a shared id is refused, got {}", describe(errorOf(duplicated))));
                                                                 checks.raise();
                                                             })
                                                       .Execute();
                                               }};

const mdux::spec::Register rectanglesStayInsideTheSurface{
    "A rectangle with no extent, or one that leaves the surface, is refused",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-bounds")
            .Given("the reference screen", [] {})
            .When("a node is collapsed, moved past the right edge, and given a negative origin", [] {})
            .Then("each is refused rather than clamped",
                  [] {
                      mdux::spec::Checks checks;

                      Fixture collapsed;
                      collapsed.nodes[1].bounds.width = 0;
                      checks.expect(errorOf(collapsed) == ms::SchemaError::DegenerateBounds,
                                    std::format("a zero-width rectangle is refused, got {}", describe(errorOf(collapsed))));

                      Fixture overflowing;
                      overflowing.nodes[1].bounds.x = 201;  // 201 + 200 > 400
                      checks.expect(errorOf(overflowing) == ms::SchemaError::BoundsOutsideSurface,
                                    std::format("a rectangle past the right edge is refused, got {}", describe(errorOf(overflowing))));

                      Fixture negative;
                      negative.nodes[1].bounds.y = -1;
                      checks.expect(errorOf(negative) == ms::SchemaError::BoundsOutsideSurface,
                                    std::format("a negative origin is refused, got {}", describe(errorOf(negative))));

                      // The containment arithmetic is 64-bit on purpose: at the type's extreme,
                      // x + width wraps, and a wrapped comparison would admit exactly this.
                      Fixture wrapping;
                      wrapping.nodes[1].bounds.x     = std::numeric_limits<std::int32_t>::max();
                      wrapping.nodes[1].bounds.width = 16;
                      checks.expect(errorOf(wrapping) == ms::SchemaError::BoundsOutsideSurface,
                                    std::format("a rectangle whose right edge overflows int32 is refused, got {}", describe(errorOf(wrapping))));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register surfaceMustHaveExtent{"A surface with no extent contains nothing", "evidence-unit", [] {
                                                     return speclab::Test("medui-schema-surface")
                                                         .Given("the reference screen", [] {})
                                                         .When("its surface height is zeroed", [] {})
                                                         .Then("the screen is refused before any rectangle is considered",
                                                               [] {
                                                                   mdux::spec::Checks checks;

                                                                   Fixture flat;
                                                                   flat.surfaceHeight = 0;
                                                                   checks.expect(
                                                                       errorOf(flat) == ms::SchemaError::NonPositiveSurface,
                                                                       std::format("a surface with no extent is refused, got {}", describe(errorOf(flat))));
                                                                   checks.raise();
                                                               })
                                                         .Execute();
                                                 }};

const mdux::spec::Register coloursAreNamesNotValues{
    "A colour a node draws with is a governed name, never a value",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-colour-tokens")
            .Given("the reference screen", [] {})
            .When("a node carries an RGBA literal where its token belongs, and another carries none", [] {})
            .Then("the literal is refused and the absent one is not",
                  [] {
                      mdux::spec::Checks checks;

                      // ADR-011 keeps values off the device side of the boundary: a package that
                      // carried `[33, 184, 107, 255]` would have performed the substitution the
                      // governed table exists to perform, and a reviewer would read numbers.
                      Fixture literal;
                      literal.nodes[1].payload = ms::LabelSpec{.textKey = "STR-TITLE", .colorToken = "#21B86B"};
                      checks.expect(errorOf(literal) == ms::SchemaError::MalformedColorToken,
                                    std::format("a colour value is refused, got {}", describe(errorOf(literal))));

                      // The prefix on its own resolves to nothing in the
                      // governed table, so a screen carrying it would validate
                      // at compile time and fail its lookup on the device.
                      Fixture bare;
                      bare.nodes[1].payload = ms::LabelSpec{.textKey = "STR-TITLE", .colorToken = ms::colorTokenPrefix};
                      checks.expect(errorOf(bare) == ms::SchemaError::MalformedColorToken,
                                    std::format("a bare Theme.Colors. prefix names nothing and is refused, got {}", describe(errorOf(bare))));

                      // The case the governed table makes checkable, and the reason this is worth
                      // a rejection rather than a device-time miss: the name is well-formed, so
                      // shape alone accepted it, and the generated `static_assert` would have
                      // certified a screen whose colour lookup then fails on the device.
                      Fixture undefined;
                      undefined.nodes[1].payload = ms::LabelSpec{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.DoesNotExist"};
                      checks.expect(errorOf(undefined) == ms::SchemaError::UnknownColorToken,
                                    std::format("a name the governed table does not define is refused, got {}", describe(errorOf(undefined))));

                      // The typed payload makes this checkable where the flat node could not: the
                      // dictionary makes `color` required on a Label, and one shared field cannot be
                      // required for some components and absent for others.
                      Fixture untinted;
                      untinted.nodes[1].payload = ms::LabelSpec{.textKey = "STR-TITLE", .colorToken = {}};
                      checks.expect(errorOf(untinted) == ms::SchemaError::EmptyRequiredName,
                                    std::format("a Label with no tint is refused, got {}", describe(errorOf(untinted))));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register budgetMustBeIndexableAndNonEmpty{
    "The budget must be addressable, and must not be empty for a screen that draws",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-budget")
            .Given("the reference screen", [] {})
            .When("its budget is emptied, and then set past the 16-bit index width", [] {})
            .Then("both are refused, while an empty screen with an empty budget is not",
                  [] {
                      mdux::spec::Checks checks;

                      Fixture empty;
                      empty.budget = mdux::draw::DrawBudget{};
                      checks.expect(errorOf(empty) == ms::SchemaError::EmptyBudget,
                                    std::format("a screen with nodes and no budget draws nothing, got {}", describe(errorOf(empty))));

                      Fixture unindexable;
                      unindexable.budget.maxVertices = mdux::draw::maxIndexableVertices + 1;
                      checks.expect(errorOf(unindexable) == ms::SchemaError::BudgetExceedsIndexWidth,
                                    std::format("more vertices than a 16-bit index can address is refused, got {}", describe(errorOf(unindexable))));

                      // The one screen for which an empty budget is honest: it draws nothing.
                      Fixture blank;
                      blank.nodes.clear();
                      blank.budget = mdux::draw::DrawBudget{};
                      checks.expect(!errorOf(blank).has_value(), std::format("a screen with no nodes is legal, got {}", describe(errorOf(blank))));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sizingIsNotThisModulesClaim{
    "The schema carries the budget without claiming it is large enough",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-budget-sizing")
            .Given("a three-node screen whose budget holds one rectangle", [] {})
            .When("it is validated", [] {})
            .Then("it is accepted, because sizing is the compiler's claim and not this type's",
                  [] {
                      mdux::spec::Checks checks;

                      // Not an oversight, and worth a scenario so that nobody later "fixes" it into
                      // a rule: the number of rectangles a screen draws depends on the widest
                      // approved translation and on what each component draws, and this package
                      // carries neither by design. A rule invented here would be a second, weaker
                      // opinion about a number the compiler computes from the text packages (#195).
                      Fixture tiny;
                      tiny.budget = mdux::draw::DrawBudget{.maxVertices = 4, .maxIndices = 6, .maxCommands = 1};
                      checks.expect(!errorOf(tiny).has_value(),
                                    std::format("an undersized budget is not this module's to refuse, got {}", describe(errorOf(tiny))));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register colourTokensResolveAgainstTheGovernedTable{
    "Every governed token resolves, and everything else is refused with the error that fits",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-colour-resolution")
            .Given("the governed token to RGBA table", [] {})
            .When("every entry, an undefined name and two malformed ones are resolved", [] {})
            .Then("each answers with its colour or with the error that says whose defect it is",
                  [] {
                      mdux::spec::Checks checks;

                      // The shape of TrustSC's own
                      // `theme_color_table_resolves_every_entry_and_rejects_unknown_tokens`: walk
                      // the table rather than spot-check it, so an entry that stops resolving -
                      // a malformed token added to the table, say - fails here rather than on a
                      // device.
                      // Two different claims, and the second is the parity one. The first says the
                      // resolver reaches every entry the table holds - a malformed token added to
                      // the table would fail `isColorToken()` and be unreachable, and this catches
                      // it. The second says the table holds what TrustSC holds, which reading the
                      // table for both sides could never show.
                      bool everyEntryResolves = true;
                      for (const ms::ThemeColor& entry : ms::themeColors) {
                          const auto resolved = ms::resolveColorToken(entry.token);
                          everyEntryResolves  = everyEntryResolves && resolved.has_value() && *resolved == entry.value;
                      }
                      checks.expect(everyEntryResolves, "the resolver reaches every entry the table holds");

                      checks.expect(ms::themeColors.size() == expectedPalette.size(),
                                    std::format("the table carries TrustSC's eight entries, got {}", ms::themeColors.size()));
                      checks.expect(std::ranges::equal(ms::themeColors, expectedPalette), "and carries them token for token, value for value");

                      bool everyExpectedTokenResolves = true;
                      for (const ms::ThemeColor& expected : expectedPalette) {
                          const auto resolved        = ms::resolveColorToken(expected.token);
                          everyExpectedTokenResolves = everyExpectedTokenResolves && resolved.has_value() && *resolved == expected.value;
                      }
                      checks.expect(everyExpectedTokenResolves, "and every name TrustSC defines resolves here to the colour it defines");

                      // A miss is not a colour. ADR-011 keeps the lookup bounded and fallible on
                      // purpose: a fallback tint would render something nobody approved.
                      const auto miss = ms::resolveColorToken("Theme.Colors.DoesNotExist");
                      checks.expect(!miss.has_value() && miss.error() == ms::ThemeError::UnknownToken,
                                    "a name the table does not define is a miss, not a default");

                      // The error matters as much as the failure: a malformed name is an emitter
                      // defect, while an absent one is a screen compiled against another palette.
                      const auto malformed = ms::resolveColorToken("Theme.Colors.#");
                      checks.expect(!malformed.has_value() && malformed.error() == ms::ThemeError::MalformedToken,
                                    "a name that is not a name is distinguished from one that is merely absent");

                      const auto value = ms::resolveColorToken("#21B86B");
                      checks.expect(!value.has_value() && value.error() == ms::ThemeError::MalformedToken, "a colour value is refused where a name belongs");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register tokenShapeMatchesWhatTheParserAccepts{
    "The shape rule admits what the language can write, and refuses what it cannot",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-colour-shape")
            .Given("names the parser produces and names it cannot", [] {})
            .When("each is checked for shape", [] {})
            .Then("the rule tracks the grammar rather than a stricter guess at it",
                  [] {
                      mdux::spec::Checks checks;

                      // The parser builds a dotted path from identifier tokens, and an identifier
                      // may contain digits, `_` and `-`. A stricter rule here would reject screens
                      // the compiler accepted, which is the worse direction to be wrong in.
                      checks.expect(ms::isColorToken("Theme.Colors.ScoreDigits"), "the ordinary form");
                      checks.expect(ms::isColorToken("Theme.Colors.Status-Ok"), "a hyphenated name");
                      checks.expect(ms::isColorToken("Theme.Colors.Alarm_2"), "digits and an underscore");
                      checks.expect(ms::isColorToken("Theme.Colors.Status.Ok"), "a nested path, which the parser can produce");

                      checks.expect(!ms::isColorToken("Theme.Colors."), "the bare prefix names nothing");
                      checks.expect(!ms::isColorToken("Theme.Colors.Status."), "a trailing dot leaves an empty segment");
                      checks.expect(!ms::isColorToken("Theme.Colors..Ok"), "so does a doubled one");
                      checks.expect(!ms::isColorToken("Theme.Colors.#"), "punctuation is not an identifier character");
                      checks.expect(!ms::isColorToken("Palette.Title"), "another prefix is another table");
                      checks.expect(!ms::isColorToken(""), "and nothing at all is not a name");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyComponentNamesItself{
    "Every payload names its component, and only the traced ones carry a requirement",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-payload-kinds")
            .Given("one node of each of the eleven kinds", [] {})
            .When("each is asked for its component name and its requirement", [] {})
            .Then("the names are the dictionary's, and an untraced component yields nothing",
                  [] {
                      mdux::spec::Checks checks;

                      const std::array<std::string_view, 2>                              states{"STR-OK", "STR-ALARM"};
                      const std::array<std::pair<ms::NodePayload, std::string_view>, 11> cases{
                          std::pair{          ms::NodePayload{ms::PanelSpec{}},           std::string_view{"Panel"}},
                          std::pair{          ms::NodePayload{ms::LabelSpec{}},           std::string_view{"Label"}},
                          std::pair{          ms::NodePayload{ms::ClockSpec{}},           std::string_view{"Clock"}},
                          std::pair{          ms::NodePayload{ms::ImageSpec{}},           std::string_view{"Image"}},
                          std::pair{ ms::NodePayload{ms::VulkanViewportSpec{}},  std::string_view{"VulkanViewport"}},
                          std::pair{    ms::NodePayload{ms::SignalTraceSpec{}},     std::string_view{"SignalTrace"}},
                          std::pair{         ms::NodePayload{ms::ButtonSpec{}},          std::string_view{"Button"}},
                          std::pair{ ms::NodePayload{ms::CriticalButtonSpec{}},  std::string_view{"CriticalButton"}},
                          std::pair{ ms::NodePayload{ms::NumericDisplaySpec{}},  std::string_view{"NumericDisplay"}},
                          std::pair{ms::NodePayload{ms::StatusIndicatorSpec{}}, std::string_view{"StatusIndicator"}},
                          std::pair{      ms::NodePayload{ms::TextInputSpec{}},       std::string_view{"TextInput"}}
                      };

                      bool everyKindNamed = true;
                      for (const auto& [payload, expected] : cases) {
                          const ms::CompiledNode node{
                              .id      = "n",
                              .bounds  = {0, 0, 1, 1},
                              .payload = payload
                          };
                          everyKindNamed = everyKindNamed && ms::kindName(node) == expected;
                      }
                      checks.expect(everyKindNamed, "every alternative names its component");
                      checks.expect(cases.size() == std::variant_size_v<ms::NodePayload>,
                                    "and the case list covers every alternative, so a new one cannot be added unnoticed");

                      // Five components can be traced; the rest carry no requirement field at all,
                      // which is the distinction the flat node had to fake with an empty string.
                      const ms::CompiledNode traced{
                          .id      = "score",
                          .bounds  = {                     0,                 0,                   1,                 1},
                          .payload = ms::StatusIndicatorSpec{.requirement = "REQ-1", .source = "STATE", .stateKeys = states, .colorTokens = {}}
                      };
                      const ms::CompiledNode untraced{
                          .id      = "now",
                          .bounds  = {0, 0, 1, 1},
                          .payload = ms::ClockSpec{.format = "HH_MM"}
                      };
                      checks.expect(ms::requirementOf(traced) == "REQ-1", "a status indicator yields its requirement");
                      checks.expect(ms::requirementOf(untraced).empty(), "a clock has none to yield");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register statesAndTheirTintsPairUp{
    "A status indicator must have states, and per-state tints must pair with them",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-status-states")
            .Given("a status indicator with two states", [] {})
            .When("its states are emptied, and its tint list given the wrong length", [] {})
            .Then("each is refused, while no tints at all is legal",
                  [] {
                      mdux::spec::Checks checks;

                      const std::array<std::string_view, 2> states{"STR-OK", "STR-ALARM"};
                      const std::array<std::string_view, 2> tints{"Theme.Colors.Nominal", "Theme.Colors.Fault"};
                      const std::array<std::string_view, 1> oneTint{"Theme.Colors.Nominal"};

                      const auto packageWith = [](std::span<const ms::CompiledNode> storage) {
                          return ms::ScreenPackage{
                              .id            = "screen",
                              .schemaVersion = mdux::evidence::kSchemaVersion,
                              .surfaceWidth  = 400,
                              .surfaceHeight = 300,
                              .nodes         = storage,
                              .budget        = mdux::draw::DrawBudget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 4}
                          };
                      };

                      const auto errorFor = [&](ms::NodePayload payload) -> std::optional<ms::SchemaError> {
                          const std::array<ms::CompiledNode, 1> nodes{
                              ms::CompiledNode{.id = "state", .bounds = {0, 0, 120, 40}, .payload = std::move(payload)}
                          };
                          const auto result = packageWith(nodes).validate();
                          return result.has_value() ? std::nullopt : std::optional{result.error()};
                      };

                      checks.expect(
                          !errorFor(ms::StatusIndicatorSpec{.requirement = "REQ-1", .source = "S", .stateKeys = states, .colorTokens = tints}).has_value(),
                          "two states and two tints validate");
                      checks.expect(
                          !errorFor(ms::StatusIndicatorSpec{.requirement = "REQ-1", .source = "S", .stateKeys = states, .colorTokens = {}}).has_value(),
                          "and declaring no tint at all is legal");
                      checks.expect(errorFor(ms::StatusIndicatorSpec{.requirement = "REQ-1", .source = "S", .stateKeys = {}, .colorTokens = {}})
                                        == ms::SchemaError::NoStates,
                                    "an indicator that can show nothing is refused");

                      // The failure this pairing rule exists for: one tint short leaves a state with
                      // no colour and no way to say which state that is.
                      checks.expect(errorFor(ms::StatusIndicatorSpec{.requirement = "REQ-1", .source = "S", .stateKeys = states, .colorTokens = oneTint})
                                        == ms::SchemaError::StateColorCountMismatch,
                                    "a short tint list is refused rather than padded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unknownPayloadsAreRefused{
    "A payload this module cannot name is refused rather than accepted",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-unknown-payload")
            .Given("the closed set of payload alternatives", [] {})
            .When("a node is validated and asked for its component name", [] {})
            .Then("every alternative names itself, and the residual case fails closed",
                  [] {
                      mdux::spec::Checks checks;

                      // Exhaustiveness is a build failure rather than a test: `variant_size_v` is
                      // static_asserted in the module, so an alternative added without teaching
                      // kindName() and validatePayload() about it stops the build at the change.
                      // What a test can still show is that no alternative is silently misnamed.
                      checks.expect(std::variant_size_v<ms::NodePayload> == 11, "the alternative set is the eleven components");

                      const ms::CompiledNode textInput{
                          .id      = "entry",
                          .bounds  = {0, 0, 100, 20},
                          .payload = ms::TextInputSpec{.source = "NOTE", .colorToken = "Theme.Colors.Title", .maxLength = 16}
                      };
                      checks.expect(ms::kindName(textInput) == "TextInput",
                                    "the last alternative is named because it is that alternative, not because it is last");

                      const ms::CompiledNode noSource{
                          .id      = "entry",
                          .bounds  = {0, 0, 100, 20},
                          .payload = ms::TextInputSpec{.source = {}, .colorToken = "Theme.Colors.Title", .maxLength = 16}
                      };
                      const std::array<ms::CompiledNode, 1> nodes{noSource};
                      const ms::ScreenPackage               package{
                                        .id            = "screen",
                                        .schemaVersion = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth  = 400,
                                        .surfaceHeight = 300,
                                        .nodes         = nodes,
                                        .budget        = mdux::draw::DrawBudget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 4}
                      };
                      const auto result = package.validate();
                      checks.expect(!result.has_value() && result.error() == ms::SchemaError::EmptyRequiredName,
                                    "and a required name it does declare is still enforced");
                      checks.raise();
                  })
            .Execute();
    }};
