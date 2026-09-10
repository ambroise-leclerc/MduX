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
import mdux.medui.screen;
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

/// The committed screen's topbar, in miniature: a control tinted `Theme.Colors.Fault` over a panel
/// in `Theme.Colors.TopbarBackground`.
///
/// That pair is not decoration. Its red channel spans ten units where green spans 163, and a narrow
/// span is what turns one UNORM step of device rounding into a wide interval of implied coverage -
/// which is the whole of what `verify-golden-two-coverage-rounding` is about. A fixture in
/// `ScoreDigits` over black could not exercise it, because every channel there is wide.
constexpr std::string_view haltToken = "Theme.Colors.Fault";

constexpr ms::CriticalButtonSpec haltButton{.requirement = "REQ-GC-001",
                                            .labelKey    = "STR-HALT",
                                            .colorToken  = haltToken,
                                            .onPress     = ms::SystemEvent::TriggerHalt};

constexpr std::array<ms::CompiledNode, 1> haltNodes{
    ms::CompiledNode{.id = "halt", .bounds = {4, 4, 8, 6}, .payload = haltButton}
};

constexpr ms::ScreenPackage haltScreen{.id                   = "halt",
                                       .schemaVersion        = mdux::evidence::kSchemaVersion,
                                       .surfaceWidth         = 16,
                                       .surfaceHeight        = 20,
                                       .approvedTextPackages = approvals,
                                       .nodes                = haltNodes,
                                       .budget               = budget};

static_assert(haltScreen.validate().has_value(), "and so must the one carrying a critical control");

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

/// The control's entry, opted into both checks exactly as the committed screen's is.
constexpr mv::GoldenEntry haltGolden{
    .nodeId     = "halt",
    .bounds     = {4, 4, 8, 6},
    .textKey    = "STR-HALT",
    .colorToken = haltToken,
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

const mdux::spec::Register regionPaintedSeesContentButNotItsValue{
    "RegionPainted holds when a scene-driven node drew anything and fails only when it went blank",
    "evidence-unit",
    [] {
        return speclab::Test("verify-region-painted")
            .Given("the rectangle of a live NumericDisplay and the driver's resolved ground", [] {})
            .When("the node draws its value, then a different value, then nothing, then off the frame", [] {})
            .Then("only the blank frame and the off-frame rectangle fail - a wrong value is the scenario's binding obligation to catch",
                  [] {
                      mdux::spec::Checks checks;

                      constexpr ms::NodeRect rect{4, 4, 8, 6};
                      const mv::RenderScope  scope = mv::RenderScope::forLocale("en-US");

                      // "120 mmHg": some glyph pixels land inside the box.
                      Canvas reading120{16, 20, ground};
                      reading120.fill({5, 5, 2, 1}, ColorRgba8{.r = 200, .g = 210, .b = 220, .a = 255});
                      const mv::CheckOutcome shown = mv::regionPainted(reading120.view(), rect, ground, "insufflation-pressure", scope);
                      checks.expect(shown.held(), "a node that painted its value satisfies the check");
                      checks.expect(shown.profile == mv::regionPaintedProfile, "the outcome is tagged with the region-painted profile");

                      // A different value still paints *something* in the box: RegionPainted cannot
                      // tell 999 from 120, and ADR-021 does not ask it to - `expect reading` does.
                      Canvas reading999{16, 20, ground};
                      reading999.fill({7, 5, 3, 1}, ColorRgba8{.r = 200, .g = 210, .b = 220, .a = 255});
                      checks.expect(mv::regionPainted(reading999.view(), rect, ground, "insufflation-pressure", scope).held(),
                                    "a wrong-but-present value is not this check's to reject");

                      Canvas blank{16, 20, ground};
                      const mv::CheckOutcome gone = mv::regionPainted(blank.view(), rect, ground, "insufflation-pressure", scope);
                      checks.expect(gone.finding == mv::Finding::NothingPainted, "a node that went blank is caught");
                      checks.expect(!gone.held(), "which is a failure, not a skip");

                      Canvas                 small{8, 8, ground};
                      const mv::CheckOutcome off = mv::regionPainted(small.view(), rect, ground, "insufflation-pressure", scope);
                      checks.expect(off.finding == mv::Finding::RegionOutsideFrame, "a rectangle off the frame fails rather than reading past it");
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

const mdux::spec::Register aDimmedFieldUnderAFullTintStrokeIsAdmitted{
    "A field at reduced coverage under a full-tint stroke discharges both checks",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-two-coverage-composition")
            .Given("the composition a bound SignalTrace paints (#257)", [] {})
            .When("Bounds and ColorHash are run over it", [] {})
            .Then("both hold, which is what makes that composition available at all",
                  [] {
                      // The check behind an argument `mdux.medui.screen` makes rather than proves in
                      // its own suite. A `SignalTraceSpec` carries one colour token, an additive draw
                      // list cannot knock a stroke back to the ground, and a third colour fails
                      // ColorHash - so the only composition left is one tint at two coverages, and
                      // whether that is admissible is this module's answer to give, not that one's.
                      //
                      // The dimmed field is what keeps Bounds true: a stroke alone paints a
                      // data-dependent box, and `goldenBounds()` asks for the node's whole rectangle
                      // edge for edge. The full-tint stroke is what keeps ColorHash from reporting
                      // TintAbsent, which a dimmed field alone would earn.
                      mdux::spec::Checks checks;

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const ColorRgba8            tint        = tintOf(readoutToken);

                      // `mdux::medui::boundFieldCoverage` as the runtime quantises it. Written
                      // as the same arithmetic rather than as 64, so a change to the constant moves
                      // this scenario with it instead of leaving it testing a number nothing paints.
                      const auto dimmed = static_cast<std::uint8_t>((255.0F * mdux::medui::boundFieldCoverage) + 0.5F);

                      Canvas canvas{16, 20, ground};
                      canvas.fill({4, 4, 8, 6}, mv::blend(ground, tint, dimmed));
                      // A stroke through the field: full tint, and nowhere near filling the box.
                      for (mdux::core::Px x = 4; x < 12; ++x) {
                          canvas.set(x, 6 + (x % 3), tint);
                      }

                      const mv::CheckOutcome bounds = mv::goldenBounds(canvas.view(), expectation);
                      checks.expect(bounds.held(), std::format("the dimmed field keeps the node's whole rectangle painted: {}", mv::describe(bounds.finding)));

                      const mv::CheckOutcome colour = mv::colorHash(canvas.view(), expectation);
                      checks.expect(colour.held(), std::format("both coverages are blends of one tint: {}", mv::describe(colour.finding)));

                      // And the half that keeps this from being a scenario that cannot fail: the
                      // same field with no stroke over it reaches full coverage nowhere, which is
                      // exactly the TintAbsent the stroke is there to answer.
                      Canvas fieldOnly{16, 20, ground};
                      fieldOnly.fill({4, 4, 8, 6}, mv::blend(ground, tint, dimmed));
                      checks.expect(mv::colorHash(fieldOnly.view(), expectation).finding == mv::Finding::TintAbsent,
                                    "a dimmed field on its own does not carry its tint");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aTwoLayerCompositeIsAllowedTwoStepsOfRounding{
    "A pixel two composites deep is admitted at two steps and refused at one",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-two-coverage-rounding")
            .Given("the pixel three CI legs actually rendered for the committed screen's halt control", [] {})
            .When("ColorHash is run over it at one composite and at two", [] {})
            .Then("one step reports ForeignColour and two admits it, which is what a device's own precision costs",
                  [] {
                      // The scenario above is right in exact arithmetic and passed while saying
                      // nothing about a GPU: it paints `blend()`'s own output, so no rounding ever
                      // disagrees with it. This one paints what lavapipe produced under #261 -
                      // identical on the GCC, Clang and MSVC legs, so a measurement rather than a
                      // driver's noise - and it is the case that argument did not cover.
                      //
                      // A bound button paints its field and then its word: two composites, each
                      // quantised to eight bits. `Theme.Colors.Fault` over
                      // `Theme.Colors.TopbarBackground` spans ten units of red against 163 of green,
                      // so one step of rounding on red implies a tenth of coverage while green and
                      // blue pin it to a hundredth - and the intervals miss.
                      mdux::spec::Checks checks;

                      constexpr ColorRgba8 topbar{.r = 209, .g = 214, .b = 219, .a = 255};
                      constexpr ColorRgba8 fault{.r = 219, .g = 51, .b = 46, .a = 255};
                      constexpr ColorRgba8 rendered{.r = 215, .g = 134, .b = 135, .a = 255};

                      // Ideal red at the coverage green and blue agree on is 213.88, so the frame's
                      // 215 is one step above what a single composite could explain. Stated as an
                      // assertion rather than a comment, because the whole scenario turns on it.
                      checks.expect(mv::blend(topbar, fault, 124).r == 214, "the ideal red at this coverage rounds to 214");

                      const auto colourAt = [&](std::size_t composites) {
                          const auto expectation = mv::GoldenExpectation::create(haltGolden, haltScreen, mv::RenderScope::localeFree(), topbar, composites);
                          if (!expectation.has_value()) {
                              throw speclab::core::AssertionFailure("the fixture golden must resolve", std::source_location::current());
                          }
                          Canvas canvas{16, 20, topbar};
                          canvas.fill({4, 4, 8, 6}, rendered);
                          // One fully covered pixel, so the check is answering ForeignColour or Held
                          // rather than TintAbsent - which would pass this scenario for the wrong
                          // reason at both depths.
                          canvas.set(5, 5, fault);
                          return mv::colorHash(canvas.view(), *expectation).finding;
                      };

                      checks.expect(colourAt(1) == mv::Finding::ForeignColour,
                                    "one step is what the check allowed before #261, and it refuses a frame three CI legs produced");
                      checks.expect(colourAt(2) == mv::Finding::Held, "one step per composite admits it");

                      // And the half that keeps the slack from being a licence: a genuinely foreign
                      // colour misses by far more than two steps on the channels that carry the
                      // coverage, so two composites do not admit it either.
                      const auto foreign = mv::GoldenExpectation::create(haltGolden, haltScreen, mv::RenderScope::localeFree(), topbar, 2);
                      if (!foreign.has_value()) {
                          throw speclab::core::AssertionFailure("the fixture golden must resolve", std::source_location::current());
                      }
                      Canvas wrong{16, 20, topbar};
                      wrong.fill({4, 4, 8, 6}, ColorRgba8{.r = 215, .g = 20, .b = 135, .a = 255});
                      wrong.set(5, 5, fault);
                      checks.expect(mv::colorHash(wrong.view(), *foreign).finding == mv::Finding::ForeignColour,
                                    "a colour no coverage of this tint produces is still refused at two steps");
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

const mdux::spec::Register eachGoldenCheckNamesItsOwnObservation{
    "Bounds and ColorHash carry distinct, versioned observation profiles",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-observation-profile")
            .Given("a golden opted into both checks, and a frame that draws its node", [] {})
            .When("goldenBounds and colorHash run over it", [] {})
            .Then("each outcome names the profile its check reports under, and the two differ",
                  [] {
                      mdux::spec::Checks checks;

                      // ADR-015 decision 2: MduX's `ColorHash` is a tint-composition predicate, not
                      // the raw-pixel digest TrustSC's `ColorHash` is. The profile on the outcome is
                      // what keeps the two from being compared as one observation.
                      checks.expect(mv::profileOf(mv::CvCheck::Bounds) == mv::extentEqualityProfile, "Bounds maps to extent-equality");
                      checks.expect(mv::profileOf(mv::CvCheck::ColorHash) == mv::tintCompositionProfile, "ColorHash maps to tint-composition");
                      checks.expect(mv::profileOf(mv::CvCheck::Bounds) != mv::profileOf(mv::CvCheck::ColorHash), "and the two are not the same observation");
                      checks.expect(mv::extentEqualityProfile.id().starts_with(mv::localProfilePrefix), "the ids are implementation-local (ADR-016)");
                      checks.expect(mv::tintCompositionProfile.version() == 1, "at version 1");

                      Canvas canvas{16, 20, ground};
                      canvas.fill({4, 4, 8, 6}, tintOf(readoutToken));

                      const mv::GoldenExpectation expectation = expect(readoutGolden, textlessScreen, mv::RenderScope::localeFree());
                      const mv::CheckOutcome      bounds      = mv::goldenBounds(canvas.view(), expectation);
                      const mv::CheckOutcome      colour      = mv::colorHash(canvas.view(), expectation);

                      checks.expect(bounds.profile == mv::extentEqualityProfile, "the goldenBounds outcome carries extent-equality");
                      checks.expect(colour.profile == mv::tintCompositionProfile, "the colorHash outcome carries tint-composition");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register eachLocalProfileMapsToItsSharedRenderedCheck{
    "canonicalRenderedCheckFor pairs a local profile with its shared check only when the predicates agree",
    "evidence-unit",
    [] {
        return speclab::Test("verify-golden-canonical-rendered-mapping")
            .Given("the five mdux.local observation profiles and the pinned MEDUI-PROFILE-RENDERED check ids", [] {})
            .When("canonicalRenderedCheckFor is asked for each", [] {})
            .Then("extent-equality, tint-composition and raw-image-digest map to R01/R03/R04; ink-containment and ink-coverage map to nothing",
                  [] {
                      mdux::spec::Checks checks;

                      // The candidate half of the ADR-016 §4 / ADR-017 §3 migration. `spec/profiles.md`
                      // at the pin defines MEDUI-PROFILE-RENDERED/1 with checks extent-equality/1
                      // (R01), ink-containment/1 (R02), tint-composition/1 (R03) and rgba8-sha256/1
                      // (R04). A local profile maps only where the local predicate computes that
                      // shared check's observation.
                      const auto namesRenderedProfile = [](const std::optional<mv::CanonicalRenderedCheck>& mapped) {
                          return mapped.has_value() && mapped->profileId == mv::renderedProfileId && mapped->profileVersion == mv::renderedProfileVersion;
                      };

                      const auto extent = mv::canonicalRenderedCheckFor(mv::extentEqualityProfile);
                      checks.expect(namesRenderedProfile(extent) && extent->checkId == "extent-equality" && extent->checkVersion == 1,
                                    "extent-equality -> MEDUI-PROFILE-RENDERED/1 extent-equality/1 (R01)");

                      const auto tint = mv::canonicalRenderedCheckFor(mv::tintCompositionProfile);
                      checks.expect(namesRenderedProfile(tint) && tint->checkId == "tint-composition" && tint->checkVersion == 1,
                                    "tint-composition -> MEDUI-PROFILE-RENDERED/1 tint-composition/1 (R03)");

                      const auto digest = mv::canonicalRenderedCheckFor(mv::rawImageDigestProfile);
                      checks.expect(namesRenderedProfile(digest) && digest->checkId == "rgba8-sha256" && digest->checkVersion == 1,
                                    "raw-image-digest -> MEDUI-PROFILE-RENDERED/1 rgba8-sha256/1 (R04), even though it commits no baseline");

                      // `inkContainment()` is NOT R02: R02 is a containment test that passes empty ink
                      // and tests against an inflated golden, while `inkContainment()` is a compound
                      // predicate (containment vs the node, predicted-vs-measured extent, non-empty)
                      // that is strictly stronger. A clipped glyph fails locally but passes R02, so
                      // the local finding cannot stand in for an R02 result - it stays local, and this
                      // assertion is the guard against reinstating the mapping without a real R02
                      // obligation (ADR-016 §4).
                      checks.expect(!mv::canonicalRenderedCheckFor(mv::inkContainmentProfile).has_value(),
                                    "ink-containment does not map - inkContainment() is stronger than R02");

                      // `LocalizedTextPresence` is implementation-local: MEDUI-PROFILE-RENDERED has
                      // no localized-text-presence predicate.
                      checks.expect(!mv::canonicalRenderedCheckFor(mv::inkCoverageProfile).has_value(),
                                    "ink-coverage has no shared rendered-check equivalent");

                      // A local profile at a version this build does not produce must not silently
                      // pair with an unchanged shared id - the mapping is keyed on the whole profile.
                      checks.expect(!mv::canonicalRenderedCheckFor(mv::ObservationProfile{"mdux.local/tint-composition", 2}).has_value(),
                                    "a hypothetical tint-composition v2 does not map until the pairing is re-reviewed");
                      checks.expect(!mv::canonicalRenderedCheckFor(mv::ObservationProfile{}).has_value(), "and a default-constructed profile maps to nothing");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theExportedRenderedLeavesHoldTheirContracts{
    "The rendered-check leaves exported for the shared corpus decide as documented",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rendered-leaves")
            .Given("rectContainedBy, inflate and couldBeBlend, now on the module interface", [] {})
            .When("they are exercised at the edges rule R02/R03 turn on", [] {})
            .Then("containment allows the touching edge, inflate widens each side, and a real "
                  "half-coverage blend lies on the line couldBeBlend accepts",
                  [] {
                      mdux::spec::Checks checks;
                      using ms::NodeRect;

                      // rectContainedBy: touching the outer edge is inside; a pixel past it is not.
                      checks.expect(mv::rectContainedBy(NodeRect{2, 2, 4, 4}, NodeRect{2, 2, 4, 4}), "an exact fit is contained");
                      checks.expect(!mv::rectContainedBy(NodeRect{1, 2, 4, 4}, NodeRect{2, 2, 4, 4}), "one pixel left of the box is not");

                      // inflate: R02's margin grows every side, and the containment then holds.
                      const NodeRect grown = mv::inflate(NodeRect{2, 2, 4, 4}, 1);
                      checks.expect(grown == NodeRect{1, 1, 6, 6}, "inflate(_, 1) moves the origin out and adds 2 to each extent");
                      checks.expect(mv::rectContainedBy(NodeRect{1, 1, 6, 6}, grown), "ink flush with the inflated edge is contained (R02)");

                      // couldBeBlend over the precondition both callers meet: opaque ground and
                      // opaque tint. A real half-coverage blend() output is on the line; a channel
                      // no single coverage explains is not.
                      const mdux::core::ColorRgba8 opaqueGround{10, 20, 30, 255};
                      const mdux::core::ColorRgba8 opaqueTint{200, 60, 60, 255};
                      const mdux::core::ColorRgba8 painted = mv::blend(opaqueGround, opaqueTint, 128);
                      checks.expect(mv::couldBeBlend(painted, opaqueGround, opaqueTint, 1), "a real half-coverage blend is on the line");
                      const mdux::core::ColorRgba8 offLine{painted.r, static_cast<std::uint8_t>(painted.g + 40), painted.b, 255};
                      checks.expect(!mv::couldBeBlend(offLine, opaqueGround, opaqueTint, 1), "a channel 40 units off the line is not");
                      checks.raise();
                  })
            .Execute();
    }};
