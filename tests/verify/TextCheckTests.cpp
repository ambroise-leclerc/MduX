/**
 * @file TextCheckTests.cpp
 * @brief BDD scenarios for the two mandatory text checks and the expectation that feeds them.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * The claim these scenarios exist to establish, and #252's acceptance criterion that is easiest to
 * satisfy by accident: **a compiled text node receives both mandatory checks whether or not it has a
 * golden entry, and whether or not it was positioned.** The screen below carries no golden sidecar
 * at all, and its label declares no `position:` - the layout gave it its box. Both obligations are
 * still raised, and both are discharged, because ADR-014 decision 3 makes them a property of the
 * `textKey` rather than of an annotation.
 *
 * The font package and the run are synthetic, and deliberately so. Two glyphs six pixels tall with a
 * space between them are enough to exercise every branch, and they let a scenario state the ink box
 * it expects in numbers a reader can check by hand - which a real 12-pixel DejaVu run does not.
 * `mdux.text.draw`'s record layout is the real one: the fixture writes little-endian bytes rather
 * than a struct, so a byte-order defect could not hide behind a helper that shared the bug.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.draw;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "SyntheticFrame.hpp"

namespace {

namespace ms = mdux::medui;
namespace mv = mdux::verify;

using mdux::core::ColorRgba8;
using mdux::test::verify::Canvas;
using mdux::test::verify::record;
using mdux::test::verify::runOf;
using mdux::test::verify::tintOf;

/// The panel the label is drawn over, so "painted" inside the node means "not the panel".
constexpr ColorRgba8 ground{.r = 40, .g = 40, .b = 40, .a = 255};

constexpr std::string_view titleToken = "Theme.Colors.Title";

constexpr std::array approvals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "labelled-en-us", .packageSha256 = {3}}
};

constexpr ms::LabelSpec titleLabel{.textKey = "STR-TITLE", .colorToken = titleToken};
constexpr ms::PanelSpec backdrop{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::ClockSpec wallClock{.format = ms::ClockFormat::TimeSeconds};

/// A label the layout placed, with no `position:` of its own and no golden entry anywhere.
constexpr std::array<ms::CompiledNode, 3> labelledNodes{
    ms::CompiledNode{.id = "backdrop",   .bounds = {0, 0, 64, 48},   .payload = backdrop},
    ms::CompiledNode{   .id = "title", .bounds = {10, 20, 40, 20}, .payload = titleLabel},
    ms::CompiledNode{   .id = "clock",   .bounds = {0, 40, 20, 8},  .payload = wallClock}
};

constexpr mdux::draw::DrawBudget budget{.maxVertices = 256, .maxIndices = 384, .maxCommands = 32};

constexpr ms::ScreenPackage labelledScreen{.id                   = "labelled",
                                           .schemaVersion        = mdux::evidence::kSchemaVersion,
                                           .surfaceWidth         = 64,
                                           .surfaceHeight        = 48,
                                           .approvedTextPackages = approvals,
                                           .nodes                = labelledNodes,
                                           .budget               = budget};

static_assert(labelledScreen.validate().has_value(), "the labelled fixture must be a screen a device could hold");

/// A four-by-six glyph, twice, plus the blank the space is.
///
/// `bitmapOriginY` is measured *up* from the baseline, which is why a record's y is a baseline
/// rather than a top edge; the values here make the arithmetic legible - a glyph whose origin is six
/// pixels above a baseline at six lands its top edge on zero.
[[nodiscard]] mdux::font::FontPackage twoGlyphFont() {
    mdux::font::FontPackage font;
    font.id         = "synthetic-ui";
    font.unitsPerEm = 1000;
    font.pixelSize  = 6;
    font.atlas      = mdux::font::AtlasMetrics{.path = "atlas.bin", .width = 16, .height = 16, .byteLength = 256, .sha256 = {}, .occupancyPercent = 0};
    font.glyphs     = {
        mdux::font::GlyphRecord{.codePoint       = U' ',
                                .glyphIndex      = 3,
                                .advanceWidth    = 600,
                                .leftSideBearing = 0,
                                .x               = 0,
                                .y               = 0,
                                .width           = 0,
                                .height          = 0,
                                .bitmapOriginX   = 0,
                                .bitmapOriginY   = 0},
        mdux::font::GlyphRecord{.codePoint       = U'A',
                                .glyphIndex      = 4,
                                .advanceWidth    = 600,
                                .leftSideBearing = 0,
                                .x               = 0,
                                .y               = 0,
                                .width           = 4,
                                .height          = 6,
                                .bitmapOriginX   = 0,
                                .bitmapOriginY   = 6},
        mdux::font::GlyphRecord{.codePoint       = U'B',
                                .glyphIndex      = 5,
                                .advanceWidth    = 600,
                                .leftSideBearing = 0,
                                .x               = 4,
                                .y               = 0,
                                .width           = 4,
                                .height          = 6,
                                .bitmapOriginX   = 0,
                                .bitmapOriginY   = 6}
    };
    return font;
}

/// `A B` as the baker would have positioned it: ink at 0 and at 10, a blank between them.
[[nodiscard]] std::vector<std::byte> abRun() {
    const std::array<std::array<std::byte, 6>, 3> runs{record(1, 0, 6), record(0, 6, 6), record(2, 10, 6)};
    return runOf(runs);
}

/// The same string one glyph shorter: a different locale's run over the same layout.
[[nodiscard]] std::vector<std::byte> aOnlyRun() {
    const std::array<std::array<std::byte, 6>, 1> runs{record(1, 0, 6)};
    return runOf(runs);
}

/// A run of nothing but the space.
[[nodiscard]] std::vector<std::byte> blankRun() {
    const std::array<std::array<std::byte, 6>, 1> runs{record(0, 0, 6)};
    return runOf(runs);
}

[[nodiscard]] const ms::CompiledNode& titleNode() {
    const ms::CompiledNode* node = labelledScreen.find("title");
    if (node == nullptr) {
        throw speclab::core::AssertionFailure("the fixture screen lost its label", std::source_location::current());
    }
    return *node;
}

constexpr mv::RenderScope approvedLocale = mv::RenderScope::forLocale("en-US");

/// Builds the expectation or fails the scenario naming the refusal.
[[nodiscard]] mv::TextExpectation expect(const ms::CompiledNode& node, std::span<const std::byte> records, const mdux::font::FontPackage& font) {
    auto made = mv::TextExpectation::create(node, approvedLocale, records, font, ground);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the text expectation was refused: {}", mv::describe(made.error())), std::source_location::current());
    }
    return *made;
}

/// Paints the run where the runtime's placement rule puts it.
void paintRun(Canvas& canvas, const mv::TextExpectation& expectation, ColorRgba8 colour, mdux::core::Px shiftX = 0) {
    for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
        const std::optional<ms::NodeRect> glyph = expectation.glyphRect(index);
        if (!glyph.has_value()) {
            continue;
        }
        canvas.fill(ms::NodeRect{.x = glyph->x + shiftX, .y = glyph->y, .width = glyph->width, .height = glyph->height}, colour);
    }
}

}  // namespace

const mdux::spec::Register anUnpositionedTextNodeStillGetsBothChecks{
    "An unpositioned text node with no golden still receives both mandatory checks",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-mandatory-without-a-golden")
            .Given("a label the layout placed, with no position: and no golden entry", [] {})
            .When("its approved locale's run is drawn where the runtime would draw it", [] {})
            .Then("both InkContainment and LocalizedTextPresence are raised and both hold",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font);

                      // The placement rule, restated as the number it produces: the run's ink box
                      // is fourteen wide and six tall, and its corner is the node's corner.
                      checks.expect(title.ink() == ms::NodeRect{10, 20, 14, 6}, "the ink lands at the node's top-left corner");
                      checks.expect(title.locale() == "en-US", "the obligation names the approved locale it runs in");

                      Canvas canvas{64, 48, ground};
                      paintRun(canvas, title, tintOf(titleToken));

                      const mv::CheckOutcome containment = mv::inkContainment(canvas.view(), title);
                      const mv::CheckOutcome presence    = mv::localizedTextPresence(canvas.view(), title);

                      checks.expect(containment.held(), std::format("InkContainment holds: {}", mv::describe(containment.finding)));
                      checks.expect(presence.held(), std::format("LocalizedTextPresence holds: {}", mv::describe(presence.finding)));
                      checks.expect(containment.check == "InkContainment", "the outcome names its check");
                      checks.expect(presence.check == "LocalizedTextPresence", "and so does the other");
                      checks.expect(presence.scope == "en-US", "and both name the locale rather than a locale-free scope");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRunThatLeavesItsNodeIsCaught{
    "A run whose ink does not fit its node fails before a pixel is read",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-ink-leaves-the-node")
            .Given("the same run bound to a node only eight pixels wide", [] {})
            .When("InkContainment runs", [] {})
            .Then("it reports the box that left the node, beside the node it left",
                  [] {
                      mdux::spec::Checks checks;

                      // #195 proved the ink fits the box for the package it measured. This is the
                      // same claim for the package that was actually bound.
                      constexpr ms::CompiledNode narrow{
                          .id      = "title",
                          .bounds  = {10, 20, 8, 20},
                          .payload = titleLabel
                      };

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(narrow, records, font);

                      Canvas canvas{64, 48, ground};
                      paintRun(canvas, title, tintOf(titleToken));

                      const mv::CheckOutcome containment = mv::inkContainment(canvas.view(), title);
                      checks.expect(containment.finding == mv::Finding::InkLeftItsNode,
                                    std::format("the overflow is caught: {}", mv::describe(containment.finding)));
                      checks.expect(containment.expected == ms::NodeRect{10, 20, 8, 20}, "the outcome carries the node's rectangle");
                      checks.expect(containment.found == ms::NodeRect{10, 20, 14, 6}, "and the ink box that would not fit it");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register inkInTheWrongPlaceIsCaught{
    "Ink drawn one pixel across, and ink absent altogether, are different findings",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-ink-extent")
            .Given("a label whose run the frame drew one pixel to the right, and then not at all", [] {})
            .When("InkContainment runs on each", [] {})
            .Then("the first reports the extent it measured and the second reports nothing painted",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font);

                      Canvas shifted{64, 48, ground};
                      paintRun(shifted, title, tintOf(titleToken), 1);
                      const mv::CheckOutcome moved = mv::inkContainment(shifted.view(), title);
                      checks.expect(moved.finding == mv::Finding::InkExtentDiffers, std::format("displaced ink is caught: {}", mv::describe(moved.finding)));
                      checks.expect(moved.expected == ms::NodeRect{10, 20, 14, 6}, "against the box the committed run predicts");
                      checks.expect(moved.found == ms::NodeRect{11, 20, 14, 6}, "and the box the frame actually shows");

                      Canvas                 empty{64, 48, ground};
                      const mv::CheckOutcome absent = mv::inkContainment(empty.view(), title);
                      checks.expect(absent.finding == mv::Finding::NothingPainted, "a label that drew nothing is a different fact from one that drew badly");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anotherLocalesRunIsNotThisLocalesRun{
    "A frame carrying a different locale's run does not satisfy this locale's presence check",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-presence-is-per-locale")
            .Given("a node whose approved run is two glyphs, and a frame showing only one", [] {})
            .When("LocalizedTextPresence runs", [] {})
            .Then("it names the glyph that painted nothing rather than passing on the rest",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font     = twoGlyphFont();
                      const std::vector<std::byte>  approved = abRun();
                      const std::vector<std::byte>  shorter  = aOnlyRun();

                      const mv::TextExpectation expected = expect(titleNode(), approved, font);
                      const mv::TextExpectation other    = expect(titleNode(), shorter, font);

                      // The frame drew the *other* locale's run. Its single glyph lands where this
                      // locale's first one does, so half of the expectation is satisfied - which is
                      // exactly why a per-glyph check is what distinguishes them.
                      Canvas canvas{64, 48, ground};
                      paintRun(canvas, other, tintOf(titleToken));

                      const mv::CheckOutcome presence = mv::localizedTextPresence(canvas.view(), expected);
                      checks.expect(presence.finding == mv::Finding::GlyphMissing,
                                    std::format("the missing glyph is named: {}", mv::describe(presence.finding)));
                      checks.expect(presence.glyphIndex == 2, std::format("and it is the third record, got {}", presence.glyphIndex));
                      checks.expect(presence.expected == ms::NodeRect{20, 20, 4, 6}, "with the rectangle it should have painted");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register inkOutsideTheRunIsCaught{
    "Ink the approved run does not account for fails, and so does ink in a foreign colour",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-presence-rejects-stray-ink")
            .Given("a correctly drawn run", [] {})
            .When("one extra pixel is painted in the gap the space leaves, and then a glyph is recoloured", [] {})
            .Then("the first is ink outside the run and the second is a foreign colour",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font);

                      // The gap the space leaves is where a run from another package, or a stale
                      // frame, would show through. The atlas slot is exactly the glyph's bitmap, so
                      // a correct frame paints nothing here.
                      Canvas stray{64, 48, ground};
                      paintRun(stray, title, tintOf(titleToken));
                      stray.set(16, 22, tintOf(titleToken));
                      const mv::CheckOutcome intruder = mv::localizedTextPresence(stray.view(), title);
                      checks.expect(intruder.finding == mv::Finding::InkOutsideTheRun, std::format("stray ink is caught: {}", mv::describe(intruder.finding)));
                      checks.expect(intruder.found == ms::NodeRect{16, 22, 1, 1}, "and the outcome names the pixel");

                      Canvas recoloured{64, 48, ground};
                      paintRun(recoloured, title, ColorRgba8{.r = 255, .g = 0, .b = 0, .a = 255});
                      const mv::CheckOutcome wrongTint = mv::localizedTextPresence(recoloured.view(), title);
                      checks.expect(wrongTint.finding == mv::Finding::ForeignColour,
                                    std::format("a run in the wrong tint is caught: {}", mv::describe(wrongTint.finding)));
                      checks.expect(wrongTint.expectedColor == tintOf(titleToken), "against the tint the node's token resolves to");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aTextExpectationRefusesWhatItCannotCheck{
    "A text expectation refuses a node, a scope or a run it could not discharge an obligation over",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-expectation-fails-closed")
            .Given("a node with no text key, a locale-free scope, a partial run and a blank one", [] {})
            .When("an expectation is built from each", [] {})
            .Then("each is refused, rather than leaving an obligation nobody could discharge",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  records = abRun();

                      const auto refusal = [&](const ms::CompiledNode& node, mv::RenderScope scope, std::span<const std::byte> bytes) {
                          auto made = mv::TextExpectation::create(node, scope, bytes, font, ground);
                          return made.has_value() ? std::optional<mv::VerifyError>{} : std::optional<mv::VerifyError>{made.error()};
                      };

                      const ms::CompiledNode* backdropNode = labelledScreen.find("backdrop");
                      checks.expect(backdropNode != nullptr, "the fixture keeps its panel");
                      if (backdropNode != nullptr) {
                          checks.expect(refusal(*backdropNode, approvedLocale, records) == mv::VerifyError::NodeCarriesNoTextKey,
                                        "a panel raises no text obligation");
                      }

                      // The locale-free scope exists so a textless screen keeps its geometric
                      // obligations, not so a text obligation can lose its locale.
                      checks.expect(refusal(titleNode(), mv::RenderScope::localeFree(), records) == mv::VerifyError::ScopeIsLocaleFree,
                                    "a text obligation must name its locale");

                      const std::vector<std::byte> truncated{records.begin(), records.end() - 1};
                      checks.expect(refusal(titleNode(), approvedLocale, truncated) == mv::VerifyError::MalformedRun, "a partial record is refused");

                      const std::vector<std::byte> blank = blankRun();
                      checks.expect(refusal(titleNode(), approvedLocale, blank) == mv::VerifyError::EmptyRun,
                                    "and a run that paints nothing cannot discharge a rendered-truth obligation");

                      const std::array<std::array<std::byte, 6>, 1> unknownGlyph{record(9, 0, 6)};
                      const std::vector<std::byte>                  dangling = runOf(unknownGlyph);
                      checks.expect(refusal(titleNode(), approvedLocale, dangling) == mv::VerifyError::GlyphIndexOutOfRange,
                                    "a record naming a glyph the package does not hold is refused");

                      const std::vector<std::byte> tooLong(mdux::text::draw::recordSize * (mdux::medui::maxGlyphsPerRun + 1), std::byte{0});
                      checks.expect(refusal(titleNode(), approvedLocale, tooLong) == mv::VerifyError::RunTooLong,
                                    "and so is a run the runtime itself would refuse to draw");
                      checks.raise();
                  })
            .Execute();
    }};
