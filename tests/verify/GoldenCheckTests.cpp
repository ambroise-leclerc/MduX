/**
 * @file GoldenCheckTests.cpp
 * @brief BDD scenarios for `GoldenBounds`, `ColorHash`, and the golden expectation that feeds them.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Two halves, and the split is #252's acceptance rather than a filing convention.
 *
 * The first half is the checks themselves, on images a scenario paints: a rectangle in the right
 * place passes, the same rectangle one pixel to the right fails, an empty region fails, and a region
 * painted in a colour no coverage of the tint could produce fails differently from one painted in a
 * blend that never reaches it. No GPU is involved anywhere, which is ADR-014 decision 1's first
 * consequence made mechanical.
 *
 * The second half is `GoldenExpectation::create()`, which is where ADR-014 decision 2 lives: a
 * golden naming a node the screen does not have, and *every* field the sidecar duplicates
 * disagreeing with the node it names. Those are the failures the ownership table assigns to the
 * verifier rather than to the baker's re-derivation, so they are the ones this file has to cover
 * exhaustively rather than representatively.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.medui.schema;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "SyntheticFrame.hpp"

namespace {

namespace ms = mdux::medui;
namespace mv = mdux::verify;

using mdux::core::ColorRgba8;
using mdux::test::verify::Canvas;
using mdux::test::verify::tintOf;

/// What the driver clears to, and therefore what "this node painted nothing" looks like.
constexpr ColorRgba8 ground{.r = 0, .g = 0, .b = 0, .a = 255};

constexpr std::string_view readoutToken = "Theme.Colors.ScoreDigits";

constexpr ms::PanelSpec          readoutPanel{.colorToken = readoutToken};
constexpr ms::VulkanViewportSpec endoscopeFeed{.streamSource = "ENDOSCOPE_PRIMARY"};

/// A screen with no text at all: exactly the case ADR-014 decision 3 gives one explicit locale-free
/// render scope, so its geometric and chromatic obligations survive an empty locale manifest.
constexpr std::array<ms::CompiledNode, 2> textlessNodes{
    ms::CompiledNode{.id = "readout",   .bounds = {4, 4, 8, 6},  .payload = readoutPanel},
    ms::CompiledNode{   .id = "feed", .bounds = {0, 12, 16, 8}, .payload = endoscopeFeed}
};

constexpr mdux::draw::DrawBudget budget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8};

constexpr ms::ScreenPackage textlessScreen{.id                   = "textless",
                                           .schemaVersion        = mdux::evidence::kSchemaVersion,
                                           .surfaceWidth         = 16,
                                           .surfaceHeight        = 20,
                                           .approvedTextPackages = {},
                                           .nodes                = textlessNodes,
                                           .budget               = budget};

static_assert(textlessScreen.validate().has_value(), "the textless fixture must be a screen a device could hold");

/// A screen that does carry text, for the golden fields a textless one cannot exercise.
constexpr std::array approvals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "titled-en-us", .packageSha256 = {7}}
};

constexpr ms::LabelSpec titleLabel{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};

constexpr std::array<ms::CompiledNode, 1> titledNodes{
    ms::CompiledNode{.id = "screen-title", .bounds = {0, 0, 16, 8}, .payload = titleLabel}
};

constexpr ms::ScreenPackage titledScreen{.id                   = "titled",
                                         .schemaVersion        = mdux::evidence::kSchemaVersion,
                                         .surfaceWidth         = 16,
                                         .surfaceHeight        = 20,
                                         .approvedTextPackages = approvals,
                                         .nodes                = titledNodes,
                                         .budget               = budget};

static_assert(titledScreen.validate().has_value(), "and so must the text-bearing one");

/// A screen naming a colour the governed table does not define.
///
/// Deliberately *not* `static_assert`ed: `validate()` refuses it, which is the point. A screen like
/// this cannot come out of the compiler, and `GoldenExpectation::create()` is where one built by
/// hand at run time is stopped instead of being resolved against a table that has no such entry.
constexpr ms::PanelSpec unknownTintPanel{.colorToken = "Theme.Colors.NotInTheTable"};

constexpr std::array<ms::CompiledNode, 1> unknownTintNodes{
    ms::CompiledNode{.id = "readout", .bounds = {4, 4, 8, 6}, .payload = unknownTintPanel}
};

constexpr ms::ScreenPackage unknownTintScreen{.id                   = "unknown-tint",
                                              .schemaVersion        = mdux::evidence::kSchemaVersion,
                                              .surfaceWidth         = 16,
                                              .surfaceHeight        = 20,
                                              .approvedTextPackages = {},
                                              .nodes                = unknownTintNodes,
                                              .budget               = budget};

constexpr std::array bothChecks{mv::CvCheck::Bounds, mv::CvCheck::ColorHash};
constexpr std::array boundsOnly{mv::CvCheck::Bounds};
constexpr std::array colorHashOnly{mv::CvCheck::ColorHash};
constexpr std::array reversedChecks{mv::CvCheck::ColorHash, mv::CvCheck::Bounds};
constexpr std::array repeatedChecks{mv::CvCheck::Bounds, mv::CvCheck::Bounds};

/// The committed sidecar's entry for the panel: bounds and tint duplicated from the node, as
/// `collectGoldens()` writes them.
constexpr mv::GoldenEntry readoutGolden{
    .nodeId     = "readout",
    .bounds     = {4, 4, 8, 6},
    .textKey    = {},
    .colorToken = readoutToken,
    .cvChecks   = bothChecks
};

/// A positioned node with no single tint: `Bounds` is claimable, `ColorHash` is not.
constexpr mv::GoldenEntry feedGolden{
    .nodeId     = "feed",
    .bounds     = {0, 12, 16, 8},
    .textKey    = {},
    .colorToken = {},
    .cvChecks   = boundsOnly
};

/// Builds the expectation or fails the scenario, so a `Then` reads as the claim rather than as
/// error handling.
[[nodiscard]] mv::GoldenExpectation expect(const mv::GoldenEntry& entry, const ms::ScreenPackage& screen, mv::RenderScope scope) {
    auto made = mv::GoldenExpectation::create(entry, screen, scope, ground);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::string{"the golden was refused: "} + std::string{mv::describe(made.error())},
                                              std::source_location::current());
    }
    return *made;
}

/// The error `create()` reports, or nothing when it unexpectedly succeeded.
[[nodiscard]] std::optional<mv::VerifyError> refusal(const mv::GoldenEntry& entry, const ms::ScreenPackage& screen) {
    auto made = mv::GoldenExpectation::create(entry, screen, mv::RenderScope::localeFree(), ground);
    if (made.has_value()) {
        return std::nullopt;
    }
    return made.error();
}

}  // namespace

const mdux::spec::Register aTextlessGoldenIsCheckedInOneLocaleFreeScope{
    "A textless screen's golden is discharged in its one locale-free render scope",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-locale-free-scope")
            .Given("a screen with no text, a golden over its panel, and a frame that drew it", [] {})
            .When("both of the golden's checks run in the locale-free scope", [] {})
            .Then("they hold, and each outcome names that scope rather than a locale",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas canvas{16, 20, ground};
                      canvas.fill({4, 4, 8, 6}, tintOf(readoutToken));

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const mv::CheckOutcome      bounds      = mv::goldenBounds(canvas.view(), expectation);
                      const mv::CheckOutcome      colour      = mv::colorHash(canvas.view(), expectation);

                      // ADR-014 decision 3's textless case: the obligations do not disappear in a
                      // Cartesian product with an empty locale set, because there is one explicit
                      // scope rather than none.
                      checks.expect(bounds.held(), std::format("Bounds holds: {}", mv::describe(bounds.finding)));
                      checks.expect(colour.held(), std::format("ColorHash holds: {}", mv::describe(colour.finding)));
                      checks.expect(bounds.scope == mv::localeFreeScopeName, std::format("the scope is named, got '{}'", bounds.scope));
                      checks.expect(colour.scope == mv::localeFreeScopeName, std::format("and on the colour outcome too, got '{}'", colour.scope));
                      checks.expect(bounds.nodeId == "readout", "the outcome names the node");
                      checks.expect(bounds.check == "Bounds", "and the check");
                      checks.expect(colour.check == "ColorHash", "and so does the other one");
                      checks.expect(bounds.found == ms::NodeRect{4, 4, 8, 6}, "the content measured is the rectangle pinned");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRectangleMovedByOnePixelFails{
    "A golden rectangle moved by one pixel fails, and an empty one fails differently",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-bounds-moved")
            .Given("the same screen and golden", [] {})
            .When("the frame draws the panel one pixel right, and then not at all", [] {})
            .Then("Bounds reports the extent it found, and then that nothing was drawn",
                  [] {
                      mdux::spec::Checks checks;

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());

                      // ADR-014's own worked example. A containment would pass this: the moved
                      // rectangle still overlaps seven eighths of its declared box.
                      Canvas moved{16, 20, ground};
                      moved.fill({5, 4, 8, 6}, tintOf(readoutToken));
                      const mv::CheckOutcome shifted = mv::goldenBounds(moved.view(), expectation);
                      checks.expect(shifted.finding == mv::Finding::BoundsDiffer, std::format("the move is caught: {}", mv::describe(shifted.finding)));
                      checks.expect(shifted.expected == ms::NodeRect{4, 4, 8, 6}, "the outcome carries the expected rectangle");
                      checks.expect(shifted.found == ms::NodeRect{5, 4, 7, 6}, "and the extent actually measured inside it");

                      // The state `tests/render/ScreenPixelTests.cpp` asserts as a tripwire today,
                      // because both of the committed screen's golden nodes are still deferred.
                      Canvas                 empty{16, 20, ground};
                      const mv::CheckOutcome absent = mv::goldenBounds(empty.view(), expectation);
                      checks.expect(absent.finding == mv::Finding::NothingPainted, "an undrawn node is a different finding from a moved one");
                      checks.expect(!absent.foundValid, "and it measured nothing to report");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRegionOffTheFrameIsNotAPass{
    "A golden rectangle that is not inside the frame fails rather than reading past it",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-region-outside-frame")
            .Given("a golden pinning a rectangle at x=4 width 8", [] {})
            .When("the frame handed in is only eight pixels wide", [] {})
            .Then("the check fails as a region outside the frame",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas                      small{8, 8, ground};
                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const mv::CheckOutcome      outcome     = mv::goldenBounds(small.view(), expectation);

                      checks.expect(outcome.finding == mv::Finding::RegionOutsideFrame, "the rectangle leaves the frame");
                      checks.expect(!outcome.held(), "which is a failure, not a skip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theTintIsComparedExactly{
    "ColorHash separates the wrong colour from a colour that never reaches its tint",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-color-hash")
            .Given("a golden that opted into ColorHash", [] {})
            .When("the panel is drawn in its tint, then in a foreign colour, then in a blend", [] {})
            .Then("only the first holds, and the other two report different findings",
                  [] {
                      mdux::spec::Checks checks;

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const ColorRgba8            tint        = tintOf(readoutToken);

                      Canvas correct{16, 20, ground};
                      correct.fill({4, 4, 8, 6}, tint);
                      checks.expect(mv::colorHash(correct.view(), expectation).held(), "the tint the token resolves to holds");

                      // Red is outside the closed interval every channel of a blend has to lie in,
                      // so no coverage of this tint over this ground could have produced it.
                      Canvas foreign{16, 20, ground};
                      foreign.fill({4, 4, 8, 6}, ColorRgba8{.r = 255, .g = 0, .b = 0, .a = 255});
                      const mv::CheckOutcome wrong = mv::colorHash(foreign.view(), expectation);
                      checks.expect(wrong.finding == mv::Finding::ForeignColour, std::format("a foreign colour: {}", mv::describe(wrong.finding)));
                      checks.expect(wrong.foundColorValid && wrong.foundColor.r == 255, "and the outcome carries the pixel it found");
                      checks.expect(wrong.expectedColor == tint, "beside the tint it expected");

                      // A legitimate blend of the tint over the ground at half coverage: a possible
                      // pixel everywhere, and equal to the tint nowhere. Through `blend()` rather
                      // than `tint / 2`, which is not the same number when a channel is odd and
                      // would make this scenario fail as ForeignColour for a reason it is not about.
                      Canvas partial{16, 20, ground};
                      partial.fill({4, 4, 8, 6}, mv::blend(ground, tint, 128));
                      const mv::CheckOutcome faint = mv::colorHash(partial.view(), expectation);
                      checks.expect(faint.finding == mv::Finding::TintAbsent,
                                    std::format("content that never reaches its tint: {}", mv::describe(faint.finding)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyChannelMustAgreeOnOneCoverage{
    "A pixel inside every channel's range still fails when no single coverage produces it",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-color-hash-common-factor")
            .Given("a region holding one exact-tint pixel and one impossible channel combination", [] {})
            .When("ColorHash runs", [] {})
            .Then("the impossible pixel is reported rather than excused by the exact one",
                  [] {
                      mdux::spec::Checks checks;

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const ColorRgba8            tint        = tintOf(readoutToken);

                      // Over a black ground, this pixel takes red and blue from the tint and green
                      // from the ground. Every channel is inside its own interval; no coverage
                      // produces all three, because red and blue demand full coverage and green
                      // demands none. A per-channel test accepts it, and the exact-tint pixels
                      // around it would then satisfy the "carries its tint" half and return Held.
                      const ColorRgba8 impossible{.r = tint.r, .g = ground.g, .b = tint.b, .a = tint.a};

                      Canvas canvas{16, 20, ground};
                      canvas.fill({4, 4, 8, 6}, tint);
                      canvas.set(6, 6, impossible);

                      const mv::CheckOutcome outcome = mv::colorHash(canvas.view(), expectation);
                      checks.expect(outcome.finding == mv::Finding::ForeignColour,
                                    std::format("no single coverage produces it: {}", mv::describe(outcome.finding)));
                      checks.expect(outcome.found == ms::NodeRect{6, 6, 1, 1}, "and the outcome names the pixel");
                      checks.expect(outcome.foundColorValid && outcome.foundColor == impossible, "with the colour it actually found");

                      // ...while a real half-coverage blend, where every channel agrees on the same
                      // factor, is still admitted. Without this half the assertion above could be
                      // satisfied by a rule that rejected everything but the tint itself.
                      Canvas honest{16, 20, ground};
                      honest.fill({4, 4, 8, 6}, tint);
                      honest.set(6, 6, mv::blend(ground, tint, 128));
                      checks.expect(mv::colorHash(honest.view(), expectation).held(), "a genuine half-coverage pixel is still a blend");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aGoldenWithNoTintCannotDischargeColorHash{
    "ColorHash over a golden that resolved no tint fails rather than passing",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-no-tint-to-compare")
            .Given("a positioned node with no colour token, and a golden asking only for Bounds", [] {})
            .When("ColorHash is nevertheless run against it", [] {})
            .Then("it reports that there was no tint to compare, and Bounds still holds",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas canvas{16, 20, ground};
                      canvas.fill({0, 12, 16, 8}, ColorRgba8{.r = 12, .g = 12, .b = 12, .a = 255});

                      const mv::GoldenExpectation expectation = expect(feedGolden, textlessScreen, mv::RenderScope::localeFree());
                      checks.expect(!expectation.hasTint(), "the expectation resolved no tint");
                      checks.expect(expectation.declares(mv::CvCheck::Bounds), "and it declares the check it can discharge");
                      checks.expect(!expectation.declares(mv::CvCheck::ColorHash), "and not the one it cannot");

                      checks.expect(mv::goldenBounds(canvas.view(), expectation).held(), "Bounds holds over content that fills the box");
                      const mv::CheckOutcome colour = mv::colorHash(canvas.view(), expectation);
                      checks.expect(colour.finding == mv::Finding::NoTintToCompare, "and ColorHash refuses rather than reporting a pass it did not establish");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aDanglingGoldenIdIsRefused{"A golden naming a node the screen does not have is refused on the first lookup", "evidence-unit", [] {
                                                          return speclab::Test("verify-golden-dangling-id")
                                                              .Given("a golden entry naming a node id no compiled node carries", [] {})
                                                              .When("an expectation is built from it", [] {})
                                                              .Then("it is refused, which is the failure ADR-014 assigns to the verifier",
                                                                    [] {
                                                                        mdux::spec::Checks checks;

                                                                        constexpr mv::GoldenEntry dangling{
                                                                            .nodeId     = "no-such-node",
                                                                            .bounds     = {4, 4, 8, 6},
                                                                            .textKey    = {},
                                                                            .colorToken = readoutToken,
                                                                            .cvChecks   = bothChecks
                                                                        };

                                                                        const std::optional<mv::VerifyError> error = refusal(dangling, textlessScreen);
                                                                        checks.expect(error.has_value(), "the expectation was refused");
                                                                        checks.expect(error == mv::VerifyError::DanglingGoldenId, "as a dangling golden id");
                                                                        checks.raise();
                                                                    })
                                                              .Execute();
                                                      }};

const mdux::spec::Register everyDuplicatedGoldenFieldMustAgree{
    "Every field a golden duplicates has to agree with the node it names",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-fields-agree")
            .Given("goldens whose bounds, text key and colour token each disagree in turn", [] {})
            .When("each is resolved against the screen it was emitted beside", [] {})
            .Then("each is refused, and refused for the field that disagreed",
                  [] {
                      mdux::spec::Checks checks;

                      // A sidecar that drifted from its package addresses content no verifier could
                      // find. Each field is exercised on its own so the diagnostic cannot be right
                      // by accident.
                      constexpr mv::GoldenEntry movedBounds{
                          .nodeId     = "readout",
                          .bounds     = {4, 5, 8, 6},
                          .textKey    = {},
                          .colorToken = readoutToken,
                          .cvChecks   = bothChecks
                      };
                      checks.expect(refusal(movedBounds, textlessScreen) == mv::VerifyError::GoldenBoundsDisagree, "a rectangle that is not the node's");

                      constexpr mv::GoldenEntry wrongToken{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = {},
                          .colorToken = "Theme.Colors.Alert",
                          .cvChecks   = bothChecks
                      };
                      checks.expect(refusal(wrongToken, textlessScreen) == mv::VerifyError::GoldenColorTokenDisagrees, "a tint that is not the node's");

                      // The node draws no text, so a golden claiming a key for it is one drift a
                      // rectangle comparison would never notice.
                      constexpr mv::GoldenEntry inventedKey{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = "STR-INVENTED",
                          .colorToken = readoutToken,
                          .cvChecks   = bothChecks
                      };
                      checks.expect(refusal(inventedKey, textlessScreen) == mv::VerifyError::GoldenTextKeyDisagrees, "a text key the node does not carry");

                      // ...and the other direction, on a node that does carry one: a golden that
                      // dropped the key still has to be refused, or a screen could be verified
                      // against a sidecar that forgot what the node draws.
                      constexpr mv::GoldenEntry droppedKey{
                          .nodeId     = "screen-title",
                          .bounds     = {0, 0, 16, 8},
                          .textKey    = {},
                          .colorToken = "Theme.Colors.Title",
                          .cvChecks   = boundsOnly
                      };
                      checks.expect(refusal(droppedKey, titledScreen) == mv::VerifyError::GoldenTextKeyDisagrees,
                                    "a text key the node carries and the golden lost");

                      constexpr mv::GoldenEntry agreeing{
                          .nodeId     = "screen-title",
                          .bounds     = {0, 0, 16, 8},
                          .textKey    = "STR-TITLE",
                          .colorToken = "Theme.Colors.Title",
                          .cvChecks   = boundsOnly
                      };
                      checks.expect(!refusal(agreeing, titledScreen).has_value(), "and the entry that agrees with all three is admitted");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aGoldenMustDeclareCanonicalChecks{
    "A golden that declares nothing, or declares it out of canonical order, is refused",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-checks-canonical")
            .Given("entries with no cvChecks, reversed cvChecks and a repeated one", [] {})
            .When("each is resolved", [] {})
            .Then("each is refused, because none of them came from the compiler that writes them",
                  [] {
                      mdux::spec::Checks checks;

                      constexpr mv::GoldenEntry silent{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = {},
                          .colorToken = readoutToken,
                          .cvChecks   = {}
                      };
                      checks.expect(refusal(silent, textlessScreen) == mv::VerifyError::NoChecksDeclared, "an entry that can discharge nothing");

                      constexpr mv::GoldenEntry reversed{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = {},
                          .colorToken = readoutToken,
                          .cvChecks   = reversedChecks
                      };
                      checks.expect(refusal(reversed, textlessScreen) == mv::VerifyError::ChecksNotCanonical, "an entry that is not in the sidecar's order");

                      constexpr mv::GoldenEntry repeated{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = {},
                          .colorToken = readoutToken,
                          .cvChecks   = repeatedChecks
                      };
                      checks.expect(refusal(repeated, textlessScreen) == mv::VerifyError::ChecksNotCanonical, "and one that repeats a check");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aTintClaimNeedsATintToMake{
    "ColorHash without a token, and a token the table does not define, are both refused",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-tint-must-resolve")
            .Given("a golden asking for ColorHash over a node with no tint, and one naming an unknown token", [] {})
            .When("each is resolved", [] {})
            .Then("each is refused, rather than leaving a check with nothing to compare against",
                  [] {
                      mdux::spec::Checks checks;

                      // The two halves of one claim, as `collectGoldens()` treats them: a verifier
                      // asked to compare a tint has to be told which tint.
                      constexpr mv::GoldenEntry untinted{
                          .nodeId     = "feed",
                          .bounds     = {0, 12, 16, 8},
                          .textKey    = {},
                          .colorToken = {},
                          .cvChecks   = colorHashOnly
                      };
                      checks.expect(refusal(untinted, textlessScreen) == mv::VerifyError::ColorHashWithoutTint, "ColorHash with no colour token");

                      constexpr mv::GoldenEntry unknown{
                          .nodeId     = "readout",
                          .bounds     = {4, 4, 8, 6},
                          .textKey    = {},
                          .colorToken = "Theme.Colors.NotInTheTable",
                          .cvChecks   = bothChecks
                      };
                      checks.expect(refusal(unknown, unknownTintScreen) == mv::VerifyError::UnresolvedColorToken, "a token the governed table does not define");
                      checks.raise();
                  })
            .Execute();
    }};
