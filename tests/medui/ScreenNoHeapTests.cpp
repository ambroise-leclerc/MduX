/**
 * @file ScreenNoHeapTests.cpp
 * @brief Layer 1 of issue #199: render() makes no `operator new` call, proved by interposition.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no allocation)
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * "The draw path does not allocate" is easy to claim and easy to break silently - a `std::vector` in
 * a helper, a `std::string` on an error path, a `std::function` somebody found convenient. This
 * binary replaces the global `operator new` family with counting versions and measures instead.
 *
 * The interposition is shared with the ML suite; `tests/framework/CountingAllocations.hpp` says what
 * it catches and what it does not, and why the symbol scan is not redundant with it.
 */

import std;
import speclab;
import mdux.core.units;
import mdux.evidence.report;
import mdux.draw;
import mdux.medui.schema;
import mdux.medui.screen;

#include "../framework/SpecLabBridge.hpp"

// The interposition itself. Included by exactly one translation unit in this binary.
#include "../framework/CountingAllocations.hpp"

namespace {

namespace ms = mdux::medui;

constexpr mdux::draw::DrawBudget budget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

constexpr ms::PanelSpec panel{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::LabelSpec label{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};
constexpr ms::ClockSpec clock{.format = "TimeSeconds"};

// Every alternative the walk can take: one it draws, and two it defers. A screen of panels alone
// would leave the deferred branch - the one that touches a variant it does not draw - unmeasured.
constexpr std::array<ms::CompiledNode, 3> nodes{
    ms::CompiledNode{ .id = "back",  .bounds = {0, 0, 400, 40}, .payload = panel},
    ms::CompiledNode{.id = "title",  .bounds = {8, 8, 200, 24}, .payload = label},
    ms::CompiledNode{.id = "clock", .bounds = {8, 40, 120, 24}, .payload = clock}
};

constexpr ms::ScreenPackage screen{.id            = "noheap",
                                   .schemaVersion = mdux::evidence::kSchemaVersion,
                                   .surfaceWidth  = 400,
                                   .surfaceHeight = 300,
                                   .nodes         = nodes,
                                   .budget        = budget};

static_assert(screen.validate().has_value(), "the screen under measurement must be one a device could hold");

}  // namespace

const mdux::spec::Register theCounterMoves{"The allocation counter moves when something allocates", "noheap", [] {
                                               return speclab::Test("medui-screen-noheap-selftest")
                                                   .Given("the interposed operator new", [] {})
                                                   .When("a deliberate allocation is made", [] {})
                                                   .Then("the counter records it",
                                                         [] {
                                                             mdux::spec::Checks checks;

                                                             // Without this scenario the file would be a test that cannot fail: if the
                                                             // interposition ever stopped taking effect - a linker change, a static
                                                             // runtime, an inlined allocation - the counter would simply never move and
                                                             // every scenario below would pass.
                                                             const std::size_t before = allocations();
                                                             auto*             leaked = new std::array<std::byte, 64>{};
                                                             const std::size_t after  = allocations();
                                                             delete leaked;

                                                             checks.expect(after > before, std::format("the counter moved, {} then {}", before, after));
                                                             checks.raise();
                                                         })
                                                   .Execute();
                                           }};

const mdux::spec::Register renderingAllocatesNothing{
    "Rendering a frame allocates nothing",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render")
            .Given("a screen and storage sized once from its budget", [] {})
            .When("frames are recorded", [] {})
            .Then("the allocation counter does not move",
                  [] {
                      mdux::spec::Checks checks;

                      // Storage first, and outside the measurement: sizing it is the caller's job
                      // and may allocate on a real device. What must not allocate is the frame.
                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      const std::size_t before = allocations();
                      for (int frame = 0; frame < 8; ++frame) {
                          list.reset();
                          const auto recorded = ms::render(screen, list);
                          checks.expect(recorded.has_value(), "each frame is recorded");
                      }
                      const std::size_t after = allocations();

                      // Eight frames rather than one, because a first frame could allocate once and
                      // hide it as setup. A per-frame allocation would show up as eight.
                      checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register refusingAFrameAllocatesNothing{
    "Refusing a frame allocates nothing either",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-refusal")
            .Given("a screen naming a colour the governed table does not define", [] {})
            .When("the frame is refused and rolled back", [] {})
            .Then("the error path allocates nothing",
                  [] {
                      mdux::spec::Checks checks;

                      // The path review misses. An error type carrying a message, or a diagnostic
                      // assembled on the way out, allocates exactly where nobody is looking.
                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      constexpr ms::PanelSpec                   unknown{.colorToken = "Theme.Colors.NotInTheTable"};
                      constexpr std::array<ms::CompiledNode, 1> refusedNodes{
                          ms::CompiledNode{.id = "wrong", .bounds = {0, 0, 400, 40}, .payload = unknown}
                      };
                      const ms::ScreenPackage refused{.id            = "refused",
                                                      .schemaVersion = mdux::evidence::kSchemaVersion,
                                                      .surfaceWidth  = 400,
                                                      .surfaceHeight = 300,
                                                      .nodes         = refusedNodes,
                                                      .budget        = budget};

                      const std::size_t before = allocations();
                      const auto        frame  = ms::render(refused, list);
                      const std::size_t after  = allocations();

                      checks.expect(!frame.has_value(), "the frame is refused");
                      if (!frame.has_value()) {
                          // describe() is on the path a caller takes after a refusal, so it is
                          // measured too: a string_view over a literal allocates nothing.
                          checks.expect(!ms::describe(frame.error()).empty(), "the error describes itself");
                      }
                      checks.expect(after == before, std::format("the error path allocates nothing, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};
