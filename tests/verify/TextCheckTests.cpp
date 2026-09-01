/**
 * @file TextCheckTests.cpp
 * @brief BDD scenarios for the two mandatory text checks and the expectation that feeds them.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Three claims these scenarios exist to establish.
 *
 * **A compiled text node receives both mandatory checks whether or not it has a golden entry, and
 * whether or not it was positioned.** The screen below carries no golden sidecar at all, and its
 * label declares no `position:` - the layout gave it its box. Both obligations are still raised and
 * both are discharged, because ADR-014 decision 3 makes them a property of the `textKey`.
 *
 * **`LocalizedTextPresence` is a claim about the approved run's shape, not about ink in the right
 * boxes.** Two adversarial scenarios hold it to that: a frame painting one pixel inside each glyph
 * rectangle, and a frame painting a *different* glyph's shape at the right placement and bounds.
 * Both satisfy every check that knows only glyph metrics; both must fail this one.
 *
 * **A production expectation is authorised by artifacts, not by its arguments.** The provenance
 * scenario builds a real `TextBinding` from a self-consistent font package, text package, canonical
 * package bytes and sidecar, then shows that a screen whose manifest does not carry this package's
 * digest, a node from another screen, and a scope naming a locale the binding does not carry are
 * each refused.
 *
 * The font, atlas and run are synthetic. Two four-by-six glyphs with a space between them exercise
 * every branch and let a scenario state the ink box it expects in numbers a reader can check by
 * hand, which a real 12-pixel DejaVu run does not. `mdux.text.draw`'s record layout is the real one:
 * the fixture writes little-endian bytes rather than a struct, so a byte-order defect could not hide
 * behind a helper that shared the bug.
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
import mdux.text.schema;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "SyntheticFrame.hpp"

namespace {

namespace ms = mdux::medui;
namespace mv = mdux::verify;

using mdux::core::ColorRgba8;
using mdux::core::Px;
using mdux::test::verify::Canvas;
using mdux::test::verify::record;
using mdux::test::verify::runOf;
using mdux::test::verify::tintOf;

/// The panel the label is drawn over, so "painted" inside the node means "not the panel".
constexpr ColorRgba8 ground{.r = 40, .g = 40, .b = 40, .a = 255};

constexpr std::string_view titleToken = "Theme.Colors.Title";
constexpr std::string_view titleKey   = "STR-TITLE";
constexpr std::string_view fontId     = "synthetic-ui";
constexpr std::string_view packageId  = "labelled-en-us";
constexpr std::string_view localeTag  = "en-US";

constexpr ms::LabelSpec titleLabel{.textKey = titleKey, .colorToken = titleToken};
constexpr ms::PanelSpec backdrop{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::ClockSpec wallClock{.format = ms::ClockFormat::TimeSeconds};

constexpr std::array approvals{
    ms::TextPackageApproval{.locale = localeTag, .packageId = packageId, .packageSha256 = {3}}
};

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

constexpr std::uint32_t atlasEdge  = 16;
constexpr std::uint32_t glyphWidth = 4;
constexpr std::uint32_t glyphRows  = 6;

/**
 * @brief The coverage sheet the two glyphs paint from.
 *
 * `A` is solid: every texel of its slot is fully covered, which is the case where a covered pixel
 * must come out exactly the tint. `B` is deliberately *not* solid - a full-coverage left column and
 * a half-covered top row, the rest untouched - so that a scenario painting `A`'s shape at `B`'s
 * placement is painting a different letter of the same size, which is the substitution glyph metrics
 * alone cannot detect. The half-covered row also exercises the blend at a value that is neither
 * endpoint.
 */
[[nodiscard]] std::vector<std::byte> syntheticAtlas() {
    std::vector<std::byte> sheet(static_cast<std::size_t>(atlasEdge) * atlasEdge, std::byte{0});
    const auto             at = [&sheet](std::uint32_t x, std::uint32_t y) -> std::byte& {
        return sheet[static_cast<std::size_t>(y) * atlasEdge + x];
    };
    for (std::uint32_t y = 0; y < glyphRows; ++y) {
        for (std::uint32_t x = 0; x < glyphWidth; ++x) {
            at(x, y) = std::byte{255};
        }
        at(glyphWidth, y) = std::byte{255};
    }
    for (std::uint32_t x = 1; x < glyphWidth; ++x) {
        at(glyphWidth + x, 0) = std::byte{128};
    }
    return sheet;
}

/// A four-by-six glyph, twice, plus the blank the space is.
///
/// `bitmapOriginY` is measured *up* from the baseline, which is why a record's y is a baseline
/// rather than a top edge; the values here make the arithmetic legible - a glyph whose origin is six
/// pixels above a baseline at six lands its top edge on zero.
[[nodiscard]] mdux::font::FontPackage twoGlyphFont() {
    mdux::font::FontPackage font;
    font.id         = std::string{fontId};
    font.unitsPerEm = 1000;
    font.pixelSize  = glyphRows;
    font.atlas      = mdux::font::AtlasMetrics{.path             = "atlas.bin",
                                               .width            = atlasEdge,
                                               .height           = atlasEdge,
                                               .byteLength       = static_cast<std::uint64_t>(atlasEdge) * atlasEdge,
                                               .sha256           = {},
                                               .occupancyPercent = 0};
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
                                .bitmapOriginY   = 0                                   },
        mdux::font::GlyphRecord{.codePoint       = U'A',
                                .glyphIndex      = 4,
                                .advanceWidth    = 600,
                                .leftSideBearing = 0,
                                .x               = 0,
                                .y               = 0,
                                .width           = glyphWidth,
                                .height          = glyphRows,
                                .bitmapOriginX   = 0,
                                .bitmapOriginY   = static_cast<std::int32_t>(glyphRows)},
        mdux::font::GlyphRecord{.codePoint       = U'B',
                                .glyphIndex      = 5,
                                .advanceWidth    = 600,
                                .leftSideBearing = 0,
                                .x               = glyphWidth,
                                .y               = 0,
                                .width           = glyphWidth,
                                .height          = glyphRows,
                                .bitmapOriginX   = 0,
                                .bitmapOriginY   = static_cast<std::int32_t>(glyphRows)}
    };
    return font;
}

/// `A B` as the baker would have positioned it: ink at 0 and at 10, a blank between them.
[[nodiscard]] std::vector<std::byte> abRun() {
    const std::array<std::array<std::byte, 6>, 3> runs{record(1, 0, 6), record(0, 6, 6), record(2, 10, 6)};
    return runOf(runs);
}

/// The same string one glyph shorter: another locale's run over the same layout.
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

constexpr mv::RenderScope approvedLocale = mv::RenderScope::forLocale(localeTag);

/// Builds a synthetic expectation or fails the scenario naming the refusal.
///
/// `createSynthetic()` deliberately, and only here: what these scenarios exercise is the arithmetic
/// of four pure functions, and ADR-014 decision 1 admits exactly this path for that purpose. The
/// provenance-bearing `create()` has its own scenario at the end of this file.
[[nodiscard]] mv::TextExpectation
expect(const ms::CompiledNode& node, std::span<const std::byte> records, const mdux::font::FontPackage& font, std::span<const std::byte> atlas) {
    auto made = mv::TextExpectation::createSynthetic(node, approvedLocale, records, font, atlas, ground);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the text expectation was refused: {}", mv::describe(made.error())), std::source_location::current());
    }
    return *made;
}

/// Paints the run exactly as the coverage draw path would: every texel blended over the ground.
void paintRun(Canvas& canvas, const mv::TextExpectation& expectation, ColorRgba8 tint, Px shiftX = 0) {
    for (std::size_t index = 0; index < expectation.glyphCount(); ++index) {
        const std::optional<mv::PlacedGlyph> placed = expectation.glyph(index);
        if (!placed.has_value()) {
            continue;
        }
        for (Px dy = 0; dy < placed->rect.height; ++dy) {
            for (Px dx = 0; dx < placed->rect.width; ++dx) {
                canvas.set(placed->rect.x + dx + shiftX, placed->rect.y + dy, mv::blend(ground, tint, expectation.coverage(*placed, dx, dy)));
            }
        }
    }
}

/**
 * @brief A self-consistent font package, text package, canonical bytes and sidecar, plus the screen
 *        that approves them - everything `TextBinding::create()` insists on.
 *
 * Assembled rather than hand-written: the package's digests are computed from the bytes the fixture
 * actually holds, and the screen's approval from the package's own canonical form. A fixture with a
 * digest typed out by hand would be one that could only ever be wrong.
 */
struct BoundArtifacts {
    mdux::font::FontPackage                font{twoGlyphFont()};
    std::vector<std::byte>                 atlas{syntheticAtlas()};
    std::vector<std::byte>                 runs{abRun()};
    mdux::text::TextPackage                text{};
    std::string                            packageJson{};
    std::vector<std::byte>                 packageBytes{};
    std::array<ms::TextPackageApproval, 1> approval{};
    std::array<ms::CompiledNode, 3>        nodes{labelledNodes};
    ms::ScreenPackage                      screen{};

    BoundArtifacts() {
        text.header.schemaVersion = mdux::evidence::kSchemaVersion;
        text.header.id            = std::string{packageId};
        text.header.kind          = std::string{mdux::text::packageKind};
        text.atlasId              = std::string{fontId};
        text.locale               = std::string{localeTag};
        text.sidecarPath          = "runs.bin";
        text.sidecarByteLength    = runs.size();
        text.sidecarSha256        = mdux::evidence::sha256(runs);
        text.runs                 = {
            mdux::text::TextRun{.id = std::string{titleKey}, .byteOffset = 0, .byteLength = runs.size(), .sha256 = mdux::evidence::sha256(runs)}
        };

        auto written = text.write();
        if (!written.has_value()) {
            throw speclab::core::AssertionFailure("the fixture text package would not serialise", std::source_location::current());
        }
        packageJson = std::move(*written);
        packageBytes.assign(std::as_bytes(std::span{packageJson}).begin(), std::as_bytes(std::span{packageJson}).end());

        approval[0] = ms::TextPackageApproval{.locale = localeTag, .packageId = packageId, .packageSha256 = mdux::evidence::sha256(packageBytes)};
        screen      = ms::ScreenPackage{.id                   = "labelled",
                                        .schemaVersion        = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth         = 64,
                                        .surfaceHeight        = 48,
                                        .approvedTextPackages = approval,
                                        .nodes                = nodes,
                                        .budget               = budget};
    }

    [[nodiscard]] const ms::CompiledNode& title() const {
        const ms::CompiledNode* node = screen.find("title");
        if (node == nullptr) {
            throw speclab::core::AssertionFailure("the fixture screen lost its label", std::source_location::current());
        }
        return *node;
    }

    /// The binding, proved by the module that owns that proof.
    [[nodiscard]] ms::TextBinding binding() const {
        auto made = ms::TextBinding::create(screen, font, text, packageBytes, runs);
        if (!made.has_value()) {
            throw speclab::core::AssertionFailure(std::format("the fixture artifacts were refused: {}", ms::describe(made.error())),
                                                  std::source_location::current());
        }
        return *made;
    }
};

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
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font, atlas);

                      // The placement rule, restated as the number it produces: the run's ink box is
                      // fourteen wide and six tall, and its corner is the node's corner.
                      checks.expect(title.ink() == ms::NodeRect{10, 20, 14, 6}, "the ink lands at the node's top-left corner");
                      checks.expect(title.locale() == localeTag, "the obligation names the approved locale it runs in");

                      Canvas canvas{64, 48, ground};
                      paintRun(canvas, title, tintOf(titleToken));

                      const mv::CheckOutcome containment = mv::inkContainment(canvas.view(), title);
                      const mv::CheckOutcome presence    = mv::localizedTextPresence(canvas.view(), title);

                      checks.expect(containment.held(), std::format("InkContainment holds: {}", mv::describe(containment.finding)));
                      checks.expect(presence.held(), std::format("LocalizedTextPresence holds: {}", mv::describe(presence.finding)));
                      checks.expect(containment.check == "InkContainment", "the outcome names its check");
                      checks.expect(presence.check == "LocalizedTextPresence", "and so does the other");
                      checks.expect(presence.scope == localeTag, "and both name the locale rather than a locale-free scope");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sparseInkDoesNotSatisfyPresence{
    "Ink that merely occupies the glyph rectangles does not satisfy LocalizedTextPresence",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-presence-rejects-sparse-ink")
            .Given("a frame painting one pixel inside each of the run's glyph rectangles", [] {})
            .When("LocalizedTextPresence runs", [] {})
            .Then("it fails, because the claim is about the run's shape and not about occupancy",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font, atlas);

                      // Every check that knows only glyph metrics passes this frame: each non-blank
                      // record's rectangle contains ink, and nothing is painted outside them.
                      Canvas sparse{64, 48, ground};
                      for (std::size_t index = 0; index < title.glyphCount(); ++index) {
                          const std::optional<mv::PlacedGlyph> placed = title.glyph(index);
                          if (placed.has_value()) {
                              sparse.set(placed->rect.x, placed->rect.y, tintOf(titleToken));
                          }
                      }

                      const mv::CheckOutcome presence = mv::localizedTextPresence(sparse.view(), title);
                      checks.expect(presence.finding == mv::Finding::CoverageDiffers, std::format("sparse ink is caught: {}", mv::describe(presence.finding)));
                      checks.expect(presence.foundColorValid && presence.foundColor == ground, "and the outcome names the pixel that was left unpainted");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aDifferentGlyphOfTheSameSizeIsCaught{
    "A different glyph at the same placement and bounds does not satisfy LocalizedTextPresence",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-presence-rejects-substituted-glyph")
            .Given("a frame drawing the solid glyph's shape where the sparse one belongs", [] {})
            .When("LocalizedTextPresence runs", [] {})
            .Then("the substitution is caught, because the coverage is compared and not the box",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font, atlas);

                      // The second glyph's rectangle, filled the way the *first* glyph is covered.
                      // Same placement, same bounds, same tint, different letter - which is what a
                      // run drawn from another package's slots looks like.
                      Canvas swapped{64, 48, ground};
                      paintRun(swapped, title, tintOf(titleToken));
                      const std::optional<mv::PlacedGlyph> second = title.glyph(2);
                      checks.expect(second.has_value(), "the fixture keeps its second glyph");
                      if (!second.has_value()) {
                          checks.raise();
                          return;
                      }
                      swapped.fill(second->rect, tintOf(titleToken));

                      const mv::CheckOutcome presence = mv::localizedTextPresence(swapped.view(), title);
                      checks.expect(presence.finding == mv::Finding::CoverageDiffers,
                                    std::format("the substituted glyph is caught: {}", mv::describe(presence.finding)));
                      checks.expect(presence.glyphIndex == 2, std::format("and it is the third record, got {}", presence.glyphIndex));
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
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(narrow, records, font, atlas);

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
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font, atlas);

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
                      const std::vector<std::byte>  atlas    = syntheticAtlas();
                      const std::vector<std::byte>  approved = abRun();
                      const std::vector<std::byte>  shorter  = aOnlyRun();

                      const mv::TextExpectation expected = expect(titleNode(), approved, font, atlas);
                      const mv::TextExpectation other    = expect(titleNode(), shorter, font, atlas);

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
    "Ink the approved run does not account for fails, and so does a run in the wrong tint",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-presence-rejects-stray-ink")
            .Given("a correctly drawn run", [] {})
            .When("one extra pixel is painted in the gap the space leaves, and then the run is recoloured", [] {})
            .Then("the first is ink outside the run and the second is a coverage disagreement",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();
                      const mv::TextExpectation     title   = expect(titleNode(), records, font, atlas);

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
                      checks.expect(wrongTint.finding == mv::Finding::CoverageDiffers,
                                    std::format("a run in the wrong tint is caught: {}", mv::describe(wrongTint.finding)));
                      checks.expect(wrongTint.foundColorValid && wrongTint.foundColor.r == 255, "and the outcome carries the pixel it found");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aTextExpectationRefusesWhatItCannotCheck{
    "A text expectation refuses a node, a scope, a run or a sheet it could not check against",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-expectation-fails-closed")
            .Given("a node with no text key, a locale-free scope, a partial run, a blank one and a wrong sheet", [] {})
            .When("an expectation is built from each", [] {})
            .Then("each is refused, rather than leaving an obligation nobody could discharge",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font    = twoGlyphFont();
                      const std::vector<std::byte>  atlas   = syntheticAtlas();
                      const std::vector<std::byte>  records = abRun();

                      const auto refusal =
                          [&](const ms::CompiledNode& node, mv::RenderScope scope, std::span<const std::byte> bytes, std::span<const std::byte> sheet) {
                              auto made = mv::TextExpectation::createSynthetic(node, scope, bytes, font, sheet, ground);
                              return made.has_value() ? std::optional<mv::VerifyError>{} : std::optional<mv::VerifyError>{made.error()};
                          };

                      const ms::CompiledNode* backdropNode = labelledScreen.find("backdrop");
                      checks.expect(backdropNode != nullptr, "the fixture keeps its panel");
                      if (backdropNode != nullptr) {
                          checks.expect(refusal(*backdropNode, approvedLocale, records, atlas) == mv::VerifyError::NodeCarriesNoTextKey,
                                        "a panel raises no text obligation");
                      }

                      // The locale-free scope exists so a textless screen keeps its geometric
                      // obligations, not so a text obligation can lose its locale.
                      checks.expect(refusal(titleNode(), mv::RenderScope::localeFree(), records, atlas) == mv::VerifyError::ScopeIsLocaleFree,
                                    "a text obligation must name its locale");

                      const std::vector<std::byte> truncated{records.begin(), records.end() - 1};
                      checks.expect(refusal(titleNode(), approvedLocale, truncated, atlas) == mv::VerifyError::MalformedRun, "a partial record is refused");

                      const std::vector<std::byte> blank = blankRun();
                      checks.expect(refusal(titleNode(), approvedLocale, blank, atlas) == mv::VerifyError::EmptyRun,
                                    "and a run that paints nothing cannot discharge a rendered-truth obligation");

                      const std::array<std::array<std::byte, 6>, 1> unknownGlyph{record(9, 0, 6)};
                      const std::vector<std::byte>                  dangling = runOf(unknownGlyph);
                      checks.expect(refusal(titleNode(), approvedLocale, dangling, atlas) == mv::VerifyError::GlyphIndexOutOfRange,
                                    "a record naming a glyph the package does not hold is refused");

                      const std::vector<std::byte> tooLong(mdux::text::draw::recordSize * (mdux::medui::maxGlyphsPerRun + 1), std::byte{0});
                      checks.expect(refusal(titleNode(), approvedLocale, tooLong, atlas) == mv::VerifyError::RunTooLong,
                                    "and so is a run the runtime itself would refuse to draw");

                      // A sheet of the wrong size is a sheet from another font, and sampling it
                      // would find the run present in the wrong letters.
                      const std::vector<std::byte> shortSheet(atlas.size() - 1, std::byte{0});
                      checks.expect(refusal(titleNode(), approvedLocale, records, shortSheet) == mv::VerifyError::AtlasSizeMismatch,
                                    "and a coverage sheet that is not the one the font package describes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aProductionExpectationIsAuthorisedByArtifacts{
    "A production text expectation comes from an authenticated binding, or not at all",
    "evidence-unit",
    [] {
        return speclab::Test("verify-text-expectation-provenance")
            .Given("a screen, a font package, a text package, its canonical bytes and its sidecar, all agreeing", [] {})
            .When("expectations are built from the binding, and from things that authorise none", [] {})
            .Then(
                "only the authorised one is admitted, and each refusal names what was missing",
                [] {
                    mdux::spec::Checks checks;

                    const BoundArtifacts  fixture;
                    const ms::TextBinding binding = fixture.binding();
                    const ms::TextBinding unbound;

                    const auto build = [&](const ms::ScreenPackage& screen, const ms::CompiledNode& node, const ms::TextBinding& bound, mv::RenderScope scope) {
                        return mv::TextExpectation::create(screen, node, bound, fixture.atlas, scope, ground);
                    };

                    // The authorised case. Nothing about the locale, the run bytes or the font was
                    // supplied by this scenario: the run was looked up by the node's own textKey in
                    // the package the screen's manifest approves.
                    auto authorised = build(fixture.screen, fixture.title(), binding, approvedLocale);
                    checks.expect(authorised.has_value(),
                                  authorised.has_value() ? "the authorised expectation is admitted"
                                                         : std::format("the authorised expectation was refused: {}", mv::describe(authorised.error())));
                    if (authorised.has_value()) {
                        checks.expect(authorised->records().size() == fixture.runs.size(), "and it carries the run the package addresses");
                        checks.expect(authorised->ink() == ms::NodeRect{10, 20, 14, 6}, "placed by the runtime's own rule");

                        Canvas canvas{64, 48, ground};
                        paintRun(canvas, *authorised, tintOf(titleToken));
                        checks.expect(mv::localizedTextPresence(canvas.view(), *authorised).held(), "and it discharges its obligation against a correct frame");
                    }

                    const auto error = [](auto&& made) {
                        return made.has_value() ? std::optional<mv::VerifyError>{} : std::optional<mv::VerifyError>{made.error()};
                    };

                    checks.expect(error(build(fixture.screen, fixture.title(), unbound, approvedLocale)) == mv::VerifyError::TextBindingUnbound,
                                  "a binding carrying no packages authorises nothing");

                    // `labelledScreen` approves the same locale and package id with a digest that
                    // is not this package's, which is the case a locale-and-id comparison alone
                    // would wave through.
                    checks.expect(error(build(labelledScreen, titleNode(), binding, approvedLocale)) == mv::VerifyError::BindingNotApproved,
                                  "a screen whose manifest does not carry this package's digest");

                    checks.expect(error(build(fixture.screen, titleNode(), binding, approvedLocale)) == mv::VerifyError::NodeNotInScreen,
                                  "a node that is not this screen's, even with the right id");

                    checks.expect(error(build(fixture.screen, fixture.title(), binding, mv::RenderScope::forLocale("de-DE")))
                                      == mv::VerifyError::ScopeIsNotTheBoundLocale,
                                  "and a scope that would report an English frame as a German outcome");
                    checks.raise();
                })
            .Execute();
    }};
