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
import mdux.core.units;
import mdux.draw;
import mdux.evidence.report;
import mdux.medui.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms = mdux::medui;

// ---------------------------------------------------------------------------
// Compile-time evidence for the constexpr claim
// ---------------------------------------------------------------------------

constexpr std::array<ms::CompiledNode, 3> constNodes{
    ms::CompiledNode{.id          = "topbar-background",
                     .component   = "Panel",
                     .bounds      = {0, 0, 400, 40},
                     .textKey     = {},
                     .colorToken  = "Theme.Colors.Surface",
                     .requirement = {}          },
    ms::CompiledNode{            .id          = "title",
                     .component   = "Label",
                     .bounds      = {8, 8, 200, 24},
                     .textKey     = "STR-TITLE",
                     .colorToken  = "Theme.Colors.Title",
                     .requirement = {}          },
    ms::CompiledNode{            .id          = "score",
                     .component   = "NumericDisplay",
                     .bounds      = {250, 60, 120, 60},
                     .textKey     = {},
                     .colorToken  = "Theme.Colors.ScoreDigits",
                     .requirement = "REQ-NS-001"}
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

// The governed table a product supplies. Two entries are enough to tell a hit from a miss, and
// `constexpr` so the resolution below happens at compile time rather than at start-up.
constexpr std::array<ms::ThemeColor, 2> constTheme{
    ms::ThemeColor{      .token = "Theme.Colors.Title",   .value = {.r = 12, .g = 24, .b = 36, .a = 255}},
    ms::ThemeColor{.token = "Theme.Colors.ScoreDigits", .value = {.r = 33, .g = 184, .b = 107, .a = 255}}
};

static_assert(ms::resolveColorToken(constTheme, "Theme.Colors.ScoreDigits").has_value(), "a name the table defines resolves at compile time");
static_assert(ms::resolveColorToken(constTheme, "Theme.Colors.ScoreDigits").value() == mdux::core::ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255},
              "and resolves to the colour the table carries");

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
                      literal.nodes[1].colorToken = "#21B86B";
                      checks.expect(errorOf(literal) == ms::SchemaError::MalformedColorToken,
                                    std::format("a colour value is refused, got {}", describe(errorOf(literal))));

                      // The prefix on its own resolves to nothing in the
                      // governed table, so a screen carrying it would validate
                      // at compile time and fail its lookup on the device.
                      Fixture bare;
                      bare.nodes[1].colorToken = ms::colorTokenPrefix;
                      checks.expect(errorOf(bare) == ms::SchemaError::MalformedColorToken,
                                    std::format("a bare Theme.Colors. prefix names nothing and is refused, got {}", describe(errorOf(bare))));

                      Fixture untinted;
                      untinted.nodes[1].colorToken = {};
                      checks.expect(!errorOf(untinted).has_value(), "a node that declares no tint is legal");
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

const mdux::spec::Register colourTokensResolveAgainstAGovernedTable{
    "A carried name resolves against the product's table, and a miss is a Result rather than a guess",
    "evidence-unit",
    [] {
        return speclab::Test("medui-schema-colour-resolution")
            .Given("a two-entry governed colour table", [] {})
            .When("a defined name, an undefined one, and two malformed ones are resolved", [] {})
            .Then("each answers with the colour or with the error that says which kind of failure it is",
                  [] {
                      mdux::spec::Checks checks;

                      const auto hit = ms::resolveColorToken(constTheme, "Theme.Colors.Title");
                      checks.expect(hit.has_value() && *hit == mdux::core::ColorRgba8{.r = 12, .g = 24, .b = 36, .a = 255},
                                    "a defined name resolves to its colour");

                      // A miss is not a colour. ADR-011 keeps the lookup bounded and fallible on
                      // purpose: a fallback tint would render something nobody approved, which is
                      // the failure mode a governed table exists to prevent.
                      const auto miss = ms::resolveColorToken(constTheme, "Theme.Colors.Undefined");
                      checks.expect(!miss.has_value() && miss.error() == ms::ThemeError::UnknownToken,
                                    "a name the table does not define is a miss, not a default");

                      const auto malformed = ms::resolveColorToken(constTheme, "Theme.Colors.#");
                      checks.expect(!malformed.has_value() && malformed.error() == ms::ThemeError::MalformedToken,
                                    "a name that is not a name is distinguished from one that is merely absent");

                      const auto value = ms::resolveColorToken(constTheme, "#21B86B");
                      checks.expect(!value.has_value() && value.error() == ms::ThemeError::MalformedToken, "a colour value is refused where a name belongs");

                      // The error matters as much as the failure: a well-formed name against an
                      // empty table is *absent*, not malformed. Asserting only `!has_value()` would
                      // pass if the resolver ever confused the two, which is the distinction this
                      // scenario exists to draw.
                      const auto emptyTable = ms::resolveColorToken({}, "Theme.Colors.Title");
                      checks.expect(!emptyTable.has_value() && emptyTable.error() == ms::ThemeError::UnknownToken,
                                    "an empty table answers a well-formed name with a miss, not a colour nobody approved");
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
