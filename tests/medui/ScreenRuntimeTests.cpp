/**
 * @file ScreenRuntimeTests.cpp
 * @brief BDD scenarios for the governed screen runtime (issue #199).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Two of these are #199's acceptance rather than ordinary coverage: the work a frame does must be a
 * pure function of the package, and it must scale with the node count. Both are read off
 * `FrameStats::steps`, which exists for them. A future component that looped until a condition, or
 * whose work grew with its data, breaks one or the other - which is the point, since the bound
 * otherwise holds only by argument.
 */

import std;
import speclab;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms = mdux::medui;

/// Storage a caller sizes once from a screen's budget, exactly as a device would.
///
/// A plain array rather than a vector: this suite links MduX::Core, and the scratch belongs where a
/// device would put it rather than on a heap the path under test must never touch.
struct Scratch {
    std::array<mdux::draw::UiVertex, 512>   vertices{};
    std::array<mdux::draw::Index, 768>      indices{};
    std::array<mdux::draw::DrawCommand, 16> commands{};

    [[nodiscard]] mdux::draw::DrawList list(const mdux::draw::DrawBudget& budget) {
        auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
        if (!created.has_value()) {
            throw speclab::core::AssertionFailure("the scratch does not satisfy the screen's budget", std::source_location::current());
        }
        return std::move(*created);
    }
};

constexpr mdux::draw::DrawBudget testBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

constexpr ms::PanelSpec topbar{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::PanelSpec footer{.colorToken = "Theme.Colors.Neutral"};
constexpr ms::LabelSpec title{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};

constexpr std::array<ms::CompiledNode, 3> mixedNodes{
    ms::CompiledNode{.id = "topbar",   .bounds = {0, 0, 400, 40}, .payload = topbar},
    ms::CompiledNode{ .id = "title",   .bounds = {8, 8, 200, 24},  .payload = title},
    ms::CompiledNode{.id = "footer", .bounds = {0, 260, 400, 40}, .payload = footer}
};

constexpr std::array defaultApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "runtime-text", .packageSha256 = {1}}
};

constexpr ms::ScreenPackage mixedScreen{.id                   = "mixed",
                                        .schemaVersion        = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth         = 400,
                                        .surfaceHeight        = 300,
                                        .approvedTextPackages = defaultApprovals,
                                        .nodes                = mixedNodes,
                                        .budget               = testBudget};

/// The same screen with twice the nodes, for the scaling half of the bounded-work acceptance.
constexpr std::array<ms::CompiledNode, 6> doubledNodes{
    ms::CompiledNode{ .id = "topbar",   .bounds = {0, 0, 400, 40}, .payload = topbar},
    ms::CompiledNode{  .id = "title",   .bounds = {8, 8, 200, 24},  .payload = title},
    ms::CompiledNode{ .id = "footer", .bounds = {0, 260, 400, 40}, .payload = footer},
    ms::CompiledNode{.id = "topbar2",  .bounds = {0, 40, 400, 40}, .payload = topbar},
    ms::CompiledNode{ .id = "title2",  .bounds = {8, 88, 200, 24},  .payload = title},
    ms::CompiledNode{.id = "footer2", .bounds = {0, 200, 400, 40}, .payload = footer}
};

constexpr ms::ScreenPackage doubledScreen{.id                   = "doubled",
                                          .schemaVersion        = mdux::evidence::kSchemaVersion,
                                          .surfaceWidth         = 400,
                                          .surfaceHeight        = 300,
                                          .approvedTextPackages = defaultApprovals,
                                          .nodes                = doubledNodes,
                                          .budget               = testBudget};

static_assert(mixedScreen.validate().has_value(), "the reference screen must be one a device could hold");
static_assert(doubledScreen.validate().has_value(), "and so must the doubled one");

}  // namespace

const mdux::spec::Register aFrameDrawsWhatItCanAndCountsTheRest{
    "A frame draws the panels and counts what it cannot decide",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-frame")
            .Given("a screen with two panels and a label", [] {})
            .When("one frame is recorded", [] {})
            .Then("the panels are drawn and the label is deferred, not silently skipped",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;
                      auto               list = scratch.list(testBudget);

                      const auto frame = ms::render(mixedScreen, list);
                      checks.expect(frame.has_value(), "the frame is recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(frame->nodes == 3, std::format("three nodes visited, got {}", frame->nodes));
                      checks.expect(frame->rects == 2, std::format("two rectangles, got {}", frame->rects));
                      // The honest half: a Label carries a text key, not glyphs, so drawing it is a
                      // join with a baked text package this repository does not have yet. Counted
                      // rather than skipped, so an integrator sees what was left undone.
                      checks.expect(frame->deferred == 1, std::format("one node deferred, got {}", frame->deferred));
                      // The per-node cap this implementation actually has, asserted rather than
                      // described: no node contributes more than one rectangle.
                      checks.expect(frame->rects <= frame->nodes, "no node records more than one rectangle");
                      checks.expect(list.vertices().size() == 8, std::format("two quads worth of vertices, got {}", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theWorkIsAPureFunctionOfTheScreen{
    "Rendering the same screen twice does the same work",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-work-is-pure")
            .Given("one screen and two lists over separate storage", [] {})
            .When("it is rendered twice", [] {})
            .Then("the iteration count and the recorded bytes are identical",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            first;
                      Scratch            second;

                      auto       listA  = first.list(testBudget);
                      auto       listB  = second.list(testBudget);
                      const auto frameA = ms::render(mixedScreen, listA);
                      const auto frameB = ms::render(mixedScreen, listB);
                      if (!frameA.has_value() || !frameB.has_value()) {
                          checks.expect(false, "both frames are recorded");
                          checks.raise();
                          return;
                      }

                      // #199's first acceptance criterion: the work is a function of the package and
                      // of nothing ambient. A component that looped until a condition would show up
                      // here as a step count that moved between two identical renders.
                      checks.expect(*frameA == *frameB, std::format("identical statistics, got steps {} then {}", frameA->steps, frameB->steps));

                      const std::span<const mdux::draw::UiVertex> a = listA.vertices();
                      const std::span<const mdux::draw::UiVertex> b = listB.vertices();
                      checks.expect(a.size() == b.size(), "the same number of vertices");
                      const bool sameBytes = a.size() == b.size()
                                             && std::equal(reinterpret_cast<const std::byte*>(a.data()),
                                                           reinterpret_cast<const std::byte*>(a.data()) + a.size_bytes(),
                                                           reinterpret_cast<const std::byte*>(b.data()));
                      checks.expect(sameBytes, "and the same bytes, which is what a cross-toolchain comparison rests on");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theWorkScalesWithTheNodeCount{"The work scales with the node count", "evidence-unit", [] {
                                                             return speclab::Test("medui-screen-work-scales")
                                                                 .Given("a screen and a second one with twice the nodes", [] {})
                                                                 .When("both are rendered", [] {})
                                                                 .Then("the iteration count doubles, so data-dependent work would be visible",
                                                                       [] {
                                                                           mdux::spec::Checks checks;
                                                                           Scratch            small;
                                                                           Scratch            large;

                                                                           auto       listSmall = small.list(testBudget);
                                                                           auto       listLarge = large.list(testBudget);
                                                                           const auto n         = ms::render(mixedScreen, listSmall);
                                                                           const auto twoN      = ms::render(doubledScreen, listLarge);
                                                                           if (!n.has_value() || !twoN.has_value()) {
                                                                               checks.expect(false, "both frames are recorded");
                                                                               checks.raise();
                                                                               return;
                                                                           }

                                                                           // #199's second criterion. An equality rather than a bound: work that grew
                                                                           // with something other than the node count would land between the two, and a
                                                                           // bound would let it pass.
                                                                           checks.expect(
                                                                               twoN->steps == n->steps * 2,
                                                                               std::format("{} steps for twice the nodes of {}", twoN->steps, n->steps));
                                                                           checks.expect(twoN->nodes == n->nodes * 2, "and twice the nodes visited");
                                                                           checks.raise();
                                                                       })
                                                                 .Execute();
                                                         }};

const mdux::spec::Register aRefusedFrameLeavesNothingBehind{
    "A refused frame leaves the list as it was found",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-fails-closed")
            .Given("a screen whose second panel names a colour the governed table does not define", [] {})
            .When("a frame is recorded after something else was already drawn", [] {})
            .Then("the frame is refused and the earlier drawing survives untouched",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;
                      auto               list = scratch.list(testBudget);

                      // Something the caller drew before the screen: a frame that rolled back too
                      // far would take this with it.
                      const auto before = list.addSolidRect(mdux::core::Rect{.x = 0, .y = 0, .width = 10, .height = 10},
                                                            mdux::core::ColorRgba8{.r = 1, .g = 2, .b = 3, .a = 4});
                      checks.expect(before.has_value(), "the caller's own rectangle is recorded");
                      const std::size_t verticesBefore = list.vertices().size();

                      constexpr ms::PanelSpec                   unknown{.colorToken = "Theme.Colors.NotInTheTable"};
                      constexpr std::array<ms::CompiledNode, 2> nodes{
                          ms::CompiledNode{ .id = "good",  .bounds = {0, 0, 400, 40},  .payload = topbar},
                          ms::CompiledNode{.id = "wrong", .bounds = {0, 60, 400, 40}, .payload = unknown}
                      };
                      const ms::ScreenPackage screen{.id                   = "unknown-token",
                                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth         = 400,
                                                     .surfaceHeight        = 300,
                                                     .approvedTextPackages = {},
                                                     .nodes                = nodes,
                                                     .budget               = testBudget};

                      const auto frame = ms::render(screen, list);
                      checks.expect(!frame.has_value(), "a screen naming an unknown colour is refused");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::UnknownColorToken,
                                        std::format("reported as UnknownColorToken, got '{}'", ms::describe(frame.error())));
                      }

                      // A malformed name is a different failure from an absent one, and the schema
                      // keeps them apart for a reason worth preserving here: one says the emitter is
                      // broken, the other says the table does not define this colour.
                      constexpr ms::PanelSpec                   malformed{.colorToken = "NotEvenAToken"};
                      constexpr std::array<ms::CompiledNode, 1> malformedNodes{
                          ms::CompiledNode{.id = "bad", .bounds = {0, 0, 40, 40}, .payload = malformed}
                      };
                      const ms::ScreenPackage malformedScreen{.id                   = "malformed-token",
                                                              .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                              .surfaceWidth         = 400,
                                                              .surfaceHeight        = 300,
                                                              .approvedTextPackages = {},
                                                              .nodes                = malformedNodes,
                                                              .budget               = testBudget};
                      const auto              malformedFrame = ms::render(malformedScreen, list);
                      checks.expect(!malformedFrame.has_value(), "a malformed colour is refused too");
                      if (!malformedFrame.has_value()) {
                          checks.expect(malformedFrame.error() == ms::ScreenError::MalformedColorToken,
                                        std::format("and told apart from an unknown one, got '{}'", ms::describe(malformedFrame.error())));
                      }
                      // The first panel had already been recorded when the second failed. A frame is
                      // whole or absent: a half-drawn one on a medical display looks like a reading.
                      checks.expect(list.vertices().size() == verticesBefore,
                                    std::format("the partial frame is gone, {} vertices remain of {}", list.vertices().size(), verticesBefore));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBudgetTooSmallIsRefusedNotTruncated{
    "A budget too small for the screen is refused rather than drawn in part",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-budget-exhausted")
            .Given("a list created against a budget with room for one rectangle", [] {})
            .When("a screen with two panels is rendered", [] {})
            .Then("the frame is refused and nothing is left recorded",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;

                      constexpr mdux::draw::DrawBudget tight{.maxVertices = 4, .maxIndices = 6, .maxCommands = 1};
                      auto                             list = scratch.list(tight);

                      constexpr std::array<ms::CompiledNode, 2> nodes{
                          ms::CompiledNode{.id = "one",  .bounds = {0, 0, 400, 40}, .payload = topbar},
                          ms::CompiledNode{.id = "two", .bounds = {0, 60, 400, 40}, .payload = footer}
                      };
                      const ms::ScreenPackage screen{.id                   = "too-tight",
                                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth         = 400,
                                                     .surfaceHeight        = 300,
                                                     .approvedTextPackages = {},
                                                     .nodes                = nodes,
                                                     .budget               = tight};

                      const auto frame = ms::render(screen, list);
                      checks.expect(!frame.has_value(), "the frame is refused");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::BudgetExhausted,
                                        std::format("reported as BudgetExhausted, got '{}'", ms::describe(frame.error())));
                      }
                      checks.expect(list.vertices().empty(), std::format("nothing is left recorded, got {} vertices", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theWorkDoesNotVaryWithTheData{
    "Two screens with the same node count and different data do the same work",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-work-is-data-independent")
            .Given("two screens of three nodes whose rectangles and colours differ entirely", [] {})
            .When("both are rendered", [] {})
            .Then("the iteration count is the same, so work proportional to the data would show",
                  [] {
                      mdux::spec::Checks checks;

                      // The case that carries the weight. Rendering one screen twice proves only
                      // determinism, and doubling identical nodes doubles any per-node cost whatever
                      // it depends on - so neither would catch work proportional to a rectangle's
                      // width. These two screens have the same node count and share nothing else.
                      constexpr ms::PanelSpec alert{.colorToken = "Theme.Colors.Alert"};
                      constexpr ms::PanelSpec fault{.colorToken = "Theme.Colors.Fault"};
                      constexpr ms::LabelSpec other{.textKey = "STR-OTHER", .colorToken = "Theme.Colors.ScoreDigits"};

                      constexpr std::array<ms::CompiledNode, 3> tinyNodes{
                          ms::CompiledNode{.id = "a", .bounds = {0, 0, 2, 2}, .payload = alert},
                          ms::CompiledNode{.id = "b", .bounds = {4, 4, 2, 2}, .payload = other},
                          ms::CompiledNode{.id = "c", .bounds = {8, 8, 2, 2}, .payload = fault}
                      };
                      constexpr std::array<ms::CompiledNode, 3> hugeNodes{
                          ms::CompiledNode{.id = "a",   .bounds = {0, 0, 400, 150}, .payload = fault},
                          ms::CompiledNode{.id = "b", .bounds = {0, 150, 400, 100}, .payload = other},
                          ms::CompiledNode{.id = "c",  .bounds = {0, 250, 400, 50}, .payload = alert}
                      };

                      const ms::ScreenPackage tiny{.id                   = "tiny",
                                                   .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                   .surfaceWidth         = 400,
                                                   .surfaceHeight        = 300,
                                                   .approvedTextPackages = {},
                                                   .nodes                = tinyNodes,
                                                   .budget               = testBudget};
                      const ms::ScreenPackage huge{.id                   = "huge",
                                                   .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                   .surfaceWidth         = 400,
                                                   .surfaceHeight        = 300,
                                                   .approvedTextPackages = {},
                                                   .nodes                = hugeNodes,
                                                   .budget               = testBudget};

                      Scratch small;
                      Scratch large;
                      auto    listTiny = small.list(testBudget);
                      auto    listHuge = large.list(testBudget);

                      const auto a = ms::render(tiny, listTiny);
                      const auto b = ms::render(huge, listHuge);
                      if (!a.has_value() || !b.has_value()) {
                          checks.expect(false, "both frames are recorded");
                          checks.raise();
                          return;
                      }
                      checks.expect(a->steps == b->steps, std::format("the same work for the same node count, got {} and {}", a->steps, b->steps));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theEmittedBytesArePinned{
    "One panel emits exactly these vertices on every toolchain",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-vertex-bytes")
            .Given("a screen of one panel at a known rectangle in a known colour", [] {})
            .When("a frame is recorded", [] {})
            .Then("every field of every vertex is the value frozen here",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;
                      auto               list = scratch.list(testBudget);

                      // Comparing two renders proves repeatability inside one binary, and both CI
                      // legs would pass while emitting different bytes. The constants below are what
                      // makes this a cross-toolchain comparison: MSVC and GCC check their output
                      // against the same numbers, so a float conversion or a colour quantisation
                      // that differed would fail on the leg that differed.
                      constexpr std::array<ms::CompiledNode, 1> pinned{
                          ms::CompiledNode{.id = "topbar", .bounds = {0, 0, 400, 40}, .payload = topbar}
                      };
                      const ms::ScreenPackage screen{.id                   = "pinned",
                                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth         = 400,
                                                     .surfaceHeight        = 300,
                                                     .approvedTextPackages = {},
                                                     .nodes                = pinned,
                                                     .budget               = testBudget};

                      const auto frame = ms::render(screen, list);
                      if (!frame.has_value()) {
                          checks.expect(false, "the frame is recorded");
                          checks.raise();
                          return;
                      }

                      // `Theme.Colors.TopbarBackground` is {0.82, 0.84, 0.86, 1.0} linear, and this
                      // runtime quantises without a transfer function - so these four bytes are the
                      // quantisation, pinned. A change to either the table or the rounding shows up
                      // here rather than as a slightly different frame nobody compares.
                      const std::uint32_t expectedColor = mdux::draw::packColor(mdux::core::ColorRgba8{.r = 209, .g = 214, .b = 219, .a = 255});

                      const std::span<const mdux::draw::UiVertex> vertices = list.vertices();
                      checks.expect(vertices.size() == 4, std::format("one quad, got {} vertices", vertices.size()));
                      if (vertices.size() != 4) {
                          checks.raise();
                          return;
                      }

                      // Corner order is top-left, top-right, bottom-right, bottom-left. Every field
                      // of all four is named, and `UiVertex` is statically asserted to be exactly
                      // 24 bytes with no padding, so "every field" is "every byte".
                      const std::array<mdux::draw::UiVertex, 4> expected{
                          mdux::draw::UiVertex{  .x = 0.0F,  .y = 0.0F, .u = 0.0F, .v = 0.0F, .color = expectedColor, .mode = 0},
                          mdux::draw::UiVertex{.x = 400.0F,  .y = 0.0F, .u = 0.0F, .v = 0.0F, .color = expectedColor, .mode = 0},
                          mdux::draw::UiVertex{.x = 400.0F, .y = 40.0F, .u = 0.0F, .v = 0.0F, .color = expectedColor, .mode = 0},
                          mdux::draw::UiVertex{  .x = 0.0F, .y = 40.0F, .u = 0.0F, .v = 0.0F, .color = expectedColor, .mode = 0}
                      };
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(vertices[index] == expected[index],
                                        std::format("vertex {} matches the frozen value, got x={} y={} colour={}",
                                                    index,
                                                    vertices[index].x,
                                                    vertices[index].y,
                                                    vertices[index].color));
                      }

                      const std::array<mdux::draw::Index, 6>   expectedIndices{0, 1, 2, 0, 2, 3};
                      const std::span<const mdux::draw::Index> indices = list.indices();
                      checks.expect(std::ranges::equal(indices, expectedIndices), "and the two triangles are wound as pinned");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theScreensOwnBudgetBoundsTheFrame{
    "The screen's declared budget bounds the frame, whatever the list allows",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-own-budget")
            .Given("a two-panel screen declaring room for one rectangle, and a list far larger", [] {})
            .When("it is rendered", [] {})
            .Then("the frame is refused and rolled back rather than drawn to the list's limit",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;

                      // The inverse of the exhausted-list case, and the one that showed the declared
                      // budget was decorative: `DrawList` can only enforce the budget it was created
                      // with, so a roomier list let a screen draw past the ceiling it declares - and
                      // a mistake in a baked budget would have been bypassed rather than observed.
                      auto list = scratch.list(testBudget);

                      constexpr mdux::draw::DrawBudget          oneRect{.maxVertices = 4, .maxIndices = 6, .maxCommands = 1};
                      constexpr std::array<ms::CompiledNode, 2> nodes{
                          ms::CompiledNode{.id = "one",  .bounds = {0, 0, 400, 40}, .payload = topbar},
                          ms::CompiledNode{.id = "two", .bounds = {0, 60, 400, 40}, .payload = footer}
                      };
                      const ms::ScreenPackage screen{.id                   = "declares-one-rect",
                                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth         = 400,
                                                     .surfaceHeight        = 300,
                                                     .approvedTextPackages = {},
                                                     .nodes                = nodes,
                                                     .budget               = oneRect};

                      const auto frame = ms::render(screen, list);
                      checks.expect(!frame.has_value(), "the screen's own budget refuses the second rectangle");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::BudgetExhausted,
                                        std::format("reported as BudgetExhausted, got '{}'", ms::describe(frame.error())));
                      }
                      checks.expect(list.vertices().empty(), std::format("and nothing is left recorded, got {} vertices", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Drawing a Label: the join, its refusals, and the cap (#242)
// ---------------------------------------------------------------------------

namespace {

/// A one-glyph font whose only character is a 4x6 block with its origin six pixels above the
/// baseline, so an ink box computed from it is checkable by hand.
[[nodiscard]] mdux::font::FontPackage textFixtureFont() {
    mdux::font::FontPackage font;
    font.id                     = "runtime-ui";
    font.unitsPerEm             = 1000;
    font.pixelSize              = 10;
    font.locales                = {"en-US"};
    font.atlas.path             = "atlas.bin";
    font.atlas.width            = 8;
    font.atlas.height           = 8;
    font.atlas.byteLength       = 64;
    font.atlas.sha256           = std::string(64, 'a');
    font.atlas.occupancyPercent = 25;
    font.glyphs                 = {
        // A blank, so a run made only of these can be measured as painting nothing.
        {.codePoint       = U' ',
         .glyphIndex      = 3,
         .advanceWidth    = 250,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 0,
         .height          = 0,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 0},
        {.codePoint       = U'A',
         .glyphIndex      = 4,
         .advanceWidth    = 700,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 4,
         .height          = 6,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 6},
    };
    font.restrictedCharset = {
        {.first = U' ', .last = U' '},
        {.first = U'A', .last = U'A'}
    };
    return font;
}

/// One v1 record, little-endian, as `mdux::text::draw::decodeRecord()` reads it.
void appendRecord(std::vector<std::byte>& out, std::uint16_t index, std::int16_t x, std::int16_t y) {
    const auto emit = [&out](std::uint16_t value) {
        out.push_back(static_cast<std::byte>(value & 0xFFu));
        out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    };
    emit(index);
    emit(std::bit_cast<std::uint16_t>(x));
    emit(std::bit_cast<std::uint16_t>(y));
}

/// A text package naming one run over the whole of `records`, with the digests `create()` checks.
///
/// The digests are computed from the bytes rather than written out: a fixture carrying a stale one
/// would exercise the rejection path while claiming to be the accepted case, which is the way a
/// suite quietly stops testing what it says it does.
[[nodiscard]] mdux::text::TextPackage textFixturePackage(std::string_view key, std::span<const std::byte> records) {
    mdux::text::TextPackage package;
    package.header.id         = "runtime-text";
    package.header.kind       = std::string{mdux::text::packageKind};
    package.atlasId           = "runtime-ui";
    package.locale            = "en-US";
    package.sidecarPath       = "runs.bin";
    package.sidecarByteLength = records.size();
    package.sidecarSha256     = mdux::evidence::sha256(records);
    package.runs.push_back(
        mdux::text::TextRun{.id = std::string{key}, .byteOffset = 0, .byteLength = records.size(), .sha256 = mdux::evidence::sha256(records)});
    return package;
}

constexpr ms::LabelSpec                   fixtureLabel{.textKey = "STR-A", .colorToken = "Theme.Colors.Title"};
constexpr std::array<ms::CompiledNode, 1> labelOnly{
    ms::CompiledNode{.id = "title", .bounds = {20, 30, 100, 40}, .payload = fixtureLabel}
};
constexpr ms::ScreenPackage labelScreen{.id                   = "label",
                                        .schemaVersion        = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth         = 200,
                                        .surfaceHeight        = 100,
                                        .approvedTextPackages = {},
                                        .nodes                = labelOnly,
                                        .budget               = testBudget};

[[nodiscard]] ms::TextPackageApproval approvalFor(const mdux::text::TextPackage& text) {
    const auto canonical = text.write();
    if (!canonical.has_value()) {
        throw speclab::core::AssertionFailure("the fixture text package did not serialize", std::source_location::current());
    }
    return ms::TextPackageApproval{.locale        = text.locale,
                                   .packageId     = text.header.id,
                                   .packageSha256 = mdux::evidence::sha256(std::as_bytes(std::span{canonical->data(), canonical->size()}))};
}

[[nodiscard]] std::string packageJsonFor(const mdux::text::TextPackage& text) {
    const auto canonical = text.write();
    if (!canonical.has_value()) {
        throw speclab::core::AssertionFailure("the fixture text package did not serialize", std::source_location::current());
    }
    return *canonical;
}

[[nodiscard]] std::span<const std::byte> bytesOf(std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] ms::ScreenPackage screenWith(std::span<const ms::TextPackageApproval> approvals) noexcept {
    ms::ScreenPackage screen    = labelScreen;
    screen.approvedTextPackages = approvals;
    return screen;
}

/// A binding built from a fixture, or a scenario failure naming what `create()` refused.
[[nodiscard]] ms::TextBinding
bindOrThrow(const ms::ScreenPackage& screen, const mdux::font::FontPackage& font, const mdux::text::TextPackage& text, std::span<const std::byte> records) {
    const std::string packageJson = packageJsonFor(text);
    auto              made        = ms::TextBinding::create(screen, font, text, bytesOf(packageJson), records);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture binding was refused: {}", ms::describe(made.error())), std::source_location::current());
    }
    return *made;
}

}  // namespace

const mdux::spec::Register labelInkLandsOnTheNodeCorner{
    "A label's ink box is placed at the node's corner, which is what #195 measured",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-label-placement")
            .Given("a run whose single glyph sits six pixels above its baseline", [] {})
            .When("the screen is rendered with the run bound", [] {})
            .Then("the glyph's rectangle starts exactly at the node's origin",
                  [] {
                      mdux::spec::Checks checks;

                      const mdux::font::FontPackage font = textFixtureFont();
                      std::vector<std::byte>        records;
                      appendRecord(records, 1, 0, 0);  // the 'A', at the run's own origin
                      const mdux::text::TextPackage text = textFixturePackage("STR-A", records);
                      const std::array              approvals{approvalFor(text)};
                      const ms::ScreenPackage       screen = screenWith(approvals);

                      Scratch              scratch;
                      mdux::draw::DrawList list  = scratch.list(testBudget);
                      const auto           frame = ms::render(screen, list, bindOrThrow(screen, font, text, records));

                      checks.expect(frame.has_value(), "the frame was recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(frame->rects == 1, std::format("one rectangle, got {}", frame->rects));
                      checks.expect(frame->deferred == 0, std::format("nothing deferred, got {}", frame->deferred));

                      // The ink box in run coordinates is (0, -6): x from the pen, y six pixels above
                      // the baseline. Placing it at the node's corner therefore means the rectangle
                      // lands at the node's own (20, 30) - not at (20, 24), which is where a baseline
                      // placed on the node's top edge would have put it.
                      const std::span<const mdux::draw::UiVertex> vertices = list.vertices();
                      checks.expect(vertices.size() == 4, std::format("four vertices, got {}", vertices.size()));
                      if (vertices.size() == 4) {
                          checks.expect(vertices[0].x == 20.0F, std::format("left edge at 20, got {}", vertices[0].x));
                          checks.expect(vertices[0].y == 30.0F, std::format("top edge at 30, got {}", vertices[0].y));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bindingRefusals{
    "Three artifacts that do not describe each other are refused once, not per frame",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-binding-refusals")
            .Given("bindings that are wrong in one way each", [] {})
            .When("each is offered to create()", [] {})
            .Then("each reports its own error",
                  [] {
                      mdux::spec::Checks            checks;
                      const mdux::font::FontPackage font = textFixtureFont();

                      std::vector<std::byte> records;
                      appendRecord(records, 1, 0, 0);
                      const mdux::text::TextPackage approvedText = textFixturePackage("STR-A", records);
                      const std::array              approvals{approvalFor(approvedText)};
                      const ms::ScreenPackage       screen       = screenWith(approvals);
                      const std::string             approvedJson = packageJsonFor(approvedText);

                      const auto errorOf = [&](const mdux::text::TextPackage& text, std::span<const std::byte> runs) {
                          auto made = ms::TextBinding::create(screen, font, text, bytesOf(approvedJson), runs);
                          return made.has_value() ? std::optional<ms::ScreenError>{} : std::optional{made.error()};
                      };

                      // A package baked against another font. Every glyph index would still be in
                      // range here, so nothing downstream could detect it - the frame would draw a
                      // plausible sentence made of the wrong letters.
                      mdux::text::TextPackage otherAtlas = textFixturePackage("STR-A", records);
                      otherAtlas.atlasId                 = "some-other-font";
                      checks.expect(errorOf(otherAtlas, records) == ms::ScreenError::AtlasMismatch, "another font is AtlasMismatch");

                      // The right length, the wrong bytes. This is the case a structural check
                      // cannot reach and the digest exists for: it renders different words.
                      std::vector<std::byte> impostor;
                      appendRecord(impostor, 0, 0, 0);
                      checks.expect(errorOf(textFixturePackage("STR-A", records), impostor) == ms::ScreenError::SidecarMismatch,
                                    "a same-length wrong sidecar is SidecarMismatch");

                      // A sidecar of the wrong length, which the cheap check catches first.
                      checks.expect(errorOf(textFixturePackage("STR-A", records), std::span{records}.first(0)) == ms::ScreenError::SidecarMismatch,
                                    "a short sidecar is SidecarMismatch");

                      // The wraparound. `byteOffset + byteLength` sums to zero for these, so an
                      // addition-form range check would accept them and hand `subspan()` an offset
                      // past its size. Six bytes is one record, so the cap does not catch it either.
                      mdux::text::TextPackage wrapped = textFixturePackage("STR-A", records);
                      wrapped.runs[0].byteOffset      = std::numeric_limits<std::uint64_t>::max() - 5;
                      wrapped.runs[0].byteLength      = 6;
                      checks.expect(errorOf(wrapped, records) == ms::ScreenError::MalformedTextRun, "a range that wraps is MalformedTextRun");

                      // Past the cap. Built rather than described, so the constant and the refusal
                      // cannot drift apart: one record more than the runtime will draw.
                      std::vector<std::byte> tooMany;
                      for (std::size_t i = 0; i <= ms::maxGlyphsPerRun; ++i) {
                          appendRecord(tooMany, 1, static_cast<std::int16_t>(i), 0);
                      }
                      checks.expect(errorOf(textFixturePackage("STR-A", tooMany), tooMany) == ms::ScreenError::RunTooLong,
                                    std::format("{} records is RunTooLong", ms::maxGlyphsPerRun + 1));

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unapprovedPackageIsRefused{
    "A valid text package the screen was not compiled against is refused by identity",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-package-not-approved")
            .Given("a screen approving one fitting package, and two other valid packages for the same locale, font and key", [] {})
            .When("each other package is offered to create()", [] {})
            .Then("both are PackageNotApproved, including the package that reuses the approved id",
                  [] {
                      mdux::spec::Checks            checks;
                      const mdux::font::FontPackage font = textFixtureFont();

                      std::vector<std::byte> approvedRuns;
                      appendRecord(approvedRuns, 1, 0, 0);
                      const mdux::text::TextPackage approvedText = textFixturePackage("STR-A", approvedRuns);
                      const std::array              approvals{approvalFor(approvedText)};
                      const ms::ScreenPackage       screen = screenWith(approvals);

                      mdux::text::TextPackage otherId = approvedText;
                      otherId.header.id               = "other-runtime-text";
                      const std::string otherIdJson   = packageJsonFor(otherId);
                      const auto        idResult      = ms::TextBinding::create(screen, font, otherId, bytesOf(otherIdJson), approvedRuns);
                      checks.expect(!idResult.has_value() && idResult.error() == ms::ScreenError::PackageNotApproved,
                                    "a different package id is PackageNotApproved");

                      // A blank glyph is still a fitting run, and the package keeps the approved id.
                      // Only its canonical package digest distinguishes it from what the compiler
                      // measured, so this assertion proves the digest participates in identity.
                      std::vector<std::byte> changedRuns;
                      appendRecord(changedRuns, 0, 0, 0);
                      const mdux::text::TextPackage changedText  = textFixturePackage("STR-A", changedRuns);
                      const std::string             changedJson  = packageJsonFor(changedText);
                      const auto                    digestResult = ms::TextBinding::create(screen, font, changedText, bytesOf(changedJson), changedRuns);
                      checks.expect(!digestResult.has_value() && digestResult.error() == ms::ScreenError::PackageNotApproved,
                                    "same id and locale with different reviewed bytes is PackageNotApproved");

                      // The approved bytes and parsed package are one authenticated input. Supplying
                      // A's approved JSON alongside B's internally valid text and sidecar must not
                      // let B's unreviewed wording through merely because its id and locale match.
                      const std::string approvedJson = packageJsonFor(approvedText);
                      const auto        splitResult  = ms::TextBinding::create(screen, font, changedText, bytesOf(approvedJson), changedRuns);
                      checks.expect(!splitResult.has_value() && splitResult.error() == ms::ScreenError::PackageNotApproved,
                                    "approved JSON paired with different parsed text is PackageNotApproved");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bindingCannotCrossScreens{"A binding approved for one screen cannot render another screen", "evidence-unit", [] {
                                                         return speclab::Test("medui-screen-binding-target")
                                                             .Given("two screens approving different fitting packages with the same font and key", [] {})
                                                             .When("the first screen's binding is offered to the second screen", [] {})
                                                             .Then("the frame is PackageNotApproved and records nothing",
                                                                   [] {
                                                                       mdux::spec::Checks            checks;
                                                                       const mdux::font::FontPackage font = textFixtureFont();

                                                                       std::vector<std::byte> firstRuns;
                                                                       appendRecord(firstRuns, 1, 0, 0);
                                                                       const mdux::text::TextPackage firstText = textFixturePackage("STR-A", firstRuns);
                                                                       const std::array              firstApprovals{approvalFor(firstText)};
                                                                       const ms::ScreenPackage       firstScreen = screenWith(firstApprovals);
                                                                       const ms::TextBinding binding = bindOrThrow(firstScreen, font, firstText, firstRuns);

                                                                       std::vector<std::byte> secondRuns;
                                                                       appendRecord(secondRuns, 0, 0, 0);
                                                                       const mdux::text::TextPackage secondText = textFixturePackage("STR-A", secondRuns);
                                                                       const std::array              secondApprovals{approvalFor(secondText)};
                                                                       const ms::ScreenPackage       secondScreen = screenWith(secondApprovals);

                                                                       Scratch              scratch;
                                                                       mdux::draw::DrawList list  = scratch.list(testBudget);
                                                                       const auto           frame = ms::render(secondScreen, list, binding);
                                                                       checks.expect(!frame.has_value() && frame.error() == ms::ScreenError::PackageNotApproved,
                                                                                     "the second screen refuses the first screen's binding by identity");
                                                                       checks.expect(list.vertices().empty() && list.indices().empty()
                                                                                         && list.commands().empty(),
                                                                                     "the refused frame records nothing");
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};

const mdux::spec::Register frameRefusals{"A valid binding that does not serve this screen is refused, and the list is left as found", "evidence-unit", [] {
                                             return speclab::Test("medui-screen-frame-refusals")
                                                 .Given("bindings create() accepts but this screen cannot use", [] {})
                                                 .When("the screen is rendered with each", [] {})
                                                 .Then("each reports its own error and records nothing",
                                                       [] {
                                                           mdux::spec::Checks            checks;
                                                           const mdux::font::FontPackage font = textFixtureFont();

                                                           std::vector<std::byte> records;
                                                           appendRecord(records, 1, 0, 0);

                                                           const auto renderWith = [&](const mdux::text::TextPackage& text, std::span<const std::byte> runs) {
                                                               const std::array        approvals{approvalFor(text)};
                                                               const ms::ScreenPackage screen  = screenWith(approvals);
                                                               const ms::TextBinding   binding = bindOrThrow(screen, font, text, runs);
                                                               Scratch                 scratch;
                                                               mdux::draw::DrawList    list  = scratch.list(testBudget);
                                                               const auto              frame = ms::render(screen, list, binding);
                                                               return std::pair{frame.has_value() ? std::optional<ms::ScreenError>{}
                                                                                                  : std::optional{frame.error()},
                                                                                list.vertices().size()};
                                                           };

                                                           // A package for another screen: it is internally consistent, it passes
                                                           // `create()`, and it does not carry this node's key.
                                                           const auto wrongKey = renderWith(textFixturePackage("STR-SOMETHING-ELSE", records), records);
                                                           checks.expect(wrongKey.first == ms::ScreenError::UnknownTextKey, "an absent key is UnknownTextKey");
                                                           checks.expect(wrongKey.second == 0, "and records nothing");

                                                           // The finding this check exists for: a second valid package, same font, same
                                                           // key, wider text. Nothing in the artifacts says which package the compiler
                                                           // measured, so the runtime measures the one it was given - and refuses it
                                                           // rather than drawing over the node's neighbours. The node is 100 wide; this
                                                           // run is fifty glyphs at ten pixels of advance.
                                                           std::vector<std::byte> wide;
                                                           for (std::int16_t i = 0; i < 50; ++i) {
                                                               appendRecord(wide, 1, static_cast<std::int16_t>(i * 10), 0);
                                                           }
                                                           const auto overflowing = renderWith(textFixturePackage("STR-A", wide), wide);
                                                           checks.expect(overflowing.first == ms::ScreenError::TextOverflowsNode,
                                                                         "text wider than its node is TextOverflowsNode");
                                                           checks.expect(overflowing.second == 0, "and the list is left as it was found");

                                                           checks.raise();
                                                       })
                                                 .Execute();
                                         }};

const mdux::spec::Register aRunOfBlanksDrawsNothing{"A run that paints nothing is drawn, not deferred", "evidence-unit", [] {
                                                        return speclab::Test("medui-screen-label-blank-run")
                                                            .Given("a run made only of blank glyphs", [] {})
                                                            .When("the screen is rendered with it bound", [] {})
                                                            .Then("no rectangle is recorded and the node is not counted as deferred",
                                                                  [] {
                                                                      mdux::spec::Checks checks;

                                                                      const mdux::font::FontPackage font = textFixtureFont();
                                                                      std::vector<std::byte>        records;
                                                                      appendRecord(records, 0, 0, 0);  // the space
                                                                      const mdux::text::TextPackage text = textFixturePackage("STR-A", records);
                                                                      const std::array              approvals{approvalFor(text)};
                                                                      const ms::ScreenPackage       screen = screenWith(approvals);

                                                                      Scratch              scratch;
                                                                      mdux::draw::DrawList list = scratch.list(testBudget);
                                                                      const auto frame = ms::render(screen, list, bindOrThrow(screen, font, text, records));

                                                                      checks.expect(frame.has_value(), "the frame was recorded");
                                                                      if (frame.has_value()) {
                                                                          checks.expect(frame->rects == 0, std::format("no rectangle, got {}", frame->rects));
                                                                          // The distinction the module documents: joined and found to paint nothing
                                                                          // is a different fact from having no package to join to.
                                                                          checks.expect(frame->deferred == 0,
                                                                                        std::format("nothing deferred, got {}", frame->deferred));
                                                                      }
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register anUnboundLabelIsDeferred{"Without a binding a label is deferred exactly as it was before #242", "evidence-unit", [] {
                                                        return speclab::Test("medui-screen-label-unbound")
                                                            .Given("no text binding", [] {})
                                                            .When("the screen is rendered", [] {})
                                                            .Then("the label is deferred and the frame succeeds",
                                                                  [] {
                                                                      mdux::spec::Checks   checks;
                                                                      Scratch              scratch;
                                                                      mdux::draw::DrawList list  = scratch.list(testBudget);
                                                                      const auto           frame = ms::render(labelScreen, list);

                                                                      checks.expect(frame.has_value(), "the frame was recorded");
                                                                      if (frame.has_value()) {
                                                                          checks.expect(frame->deferred == 1,
                                                                                        std::format("one deferred node, got {}", frame->deferred));
                                                                          checks.expect(frame->rects == 0, std::format("no rectangle, got {}", frame->rects));
                                                                      }
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};
