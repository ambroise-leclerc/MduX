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
import mdux.draw;
import mdux.medui.schema;
import mdux.medui.screen;

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

constexpr ms::ScreenPackage mixedScreen{.id            = "mixed",
                                        .schemaVersion = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth  = 400,
                                        .surfaceHeight = 300,
                                        .nodes         = mixedNodes,
                                        .budget        = testBudget};

/// The same screen with twice the nodes, for the scaling half of the bounded-work acceptance.
constexpr std::array<ms::CompiledNode, 6> doubledNodes{
    ms::CompiledNode{ .id = "topbar",   .bounds = {0, 0, 400, 40}, .payload = topbar},
    ms::CompiledNode{  .id = "title",   .bounds = {8, 8, 200, 24},  .payload = title},
    ms::CompiledNode{ .id = "footer", .bounds = {0, 260, 400, 40}, .payload = footer},
    ms::CompiledNode{.id = "topbar2",  .bounds = {0, 40, 400, 40}, .payload = topbar},
    ms::CompiledNode{ .id = "title2",  .bounds = {8, 88, 200, 24},  .payload = title},
    ms::CompiledNode{.id = "footer2", .bounds = {0, 200, 400, 40}, .payload = footer}
};

constexpr ms::ScreenPackage doubledScreen{.id            = "doubled",
                                          .schemaVersion = mdux::evidence::kSchemaVersion,
                                          .surfaceWidth  = 400,
                                          .surfaceHeight = 300,
                                          .nodes         = doubledNodes,
                                          .budget        = testBudget};

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
                      const ms::ScreenPackage screen{.id            = "unknown-token",
                                                     .schemaVersion = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth  = 400,
                                                     .surfaceHeight = 300,
                                                     .nodes         = nodes,
                                                     .budget        = testBudget};

                      const auto frame = ms::render(screen, list);
                      checks.expect(!frame.has_value(), "a screen naming an unknown colour is refused");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::UnknownColorToken,
                                        std::format("reported as UnknownColorToken, got '{}'", ms::describe(frame.error())));
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
                      const ms::ScreenPackage screen{.id            = "too-tight",
                                                     .schemaVersion = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth  = 400,
                                                     .surfaceHeight = 300,
                                                     .nodes         = nodes,
                                                     .budget        = tight};

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
