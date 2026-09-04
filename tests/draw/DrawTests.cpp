/**
 * @file DrawTests.cpp
 * @brief BDD scenarios for mdux.draw, converted from the Wave 3 MduXTest suite (issue #141).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 *
 * Three groups, and the boundaries are where the value is. The layout assertions are compile-time
 * and live in the module itself; what is tested here is the behaviour at the edge of a budget -
 * a frame that exactly fills it must succeed, and the very next primitive must fail with nothing
 * half-recorded - and the determinism a byte-compared frame depends on.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`). Sizes that guard indexing stay
 * hard, so a wrong size throws rather than letting the checks read out of bounds.
 */
// offsetof is a macro, so it comes from a header rather than `import std;` - and the header has
// to precede the import, exactly as the Vulkan SC tests include <vulkan/vulkan.h> first. Included
// afterwards, <cstddef> redefines std::byte against the module and the translation unit does not
// compile.
#include <cstddef>

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::draw;
namespace core = mdux::core;

/// Storage for a list, sized from a budget. Held by value in a test so nothing allocates.
template <std::uint32_t Vertices, std::uint32_t Indices, std::uint32_t Commands>
struct Storage {
    std::array<UiVertex, Vertices> vertices{};
    std::array<Index, Indices> indices{};
    std::array<DrawCommand, Commands> commands{};

    [[nodiscard]] static constexpr DrawBudget budget() noexcept {
        return DrawBudget{.maxVertices = Vertices, .maxIndices = Indices, .maxCommands = Commands};
    }

    [[nodiscard]] mdux::core::Result<DrawList, DrawError> list() noexcept {
        return DrawList::create(vertices, indices, commands, budget());
    }
};

/// Room for four rectangles under two clips.
using SmallStorage = Storage<16, 24, 2>;

constexpr core::Rect rect{.x = 10, .y = 20, .width = 30, .height = 40};
constexpr core::ColorRgba8 red{.r = 255, .g = 0, .b = 0, .a = 255};

/// Hard failure (REQUIRE-equivalent): a create() that was expected to succeed must have a list.
[[nodiscard]] DrawList requireCreated(core::Result<DrawList, DrawError> result,
                                      std::string_view what,
                                      std::source_location where =
                                          std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(
            std::format("{}: create() failed: {}", what, describe(result.error())),
            where);
    }
    return std::move(*result);
}

/// Hard failure (REQUIRE-equivalent): an add/set that was expected to succeed must have succeeded.
void requireAdded(core::ResultVoid<DrawError> result, std::string_view what,
                  std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(
            std::format("{}: {}", what, describe(result.error())),
            where);
    }
}

/// Hard failure (REQUIRE-equivalent): a create() that was expected to be rejected must fail.
[[nodiscard]] DrawError requireRejected(core::Result<DrawList, DrawError> result,
                                        std::string_view what,
                                        std::source_location where =
                                            std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(
            std::format("{}: expected a rejection but the list was created", what),
            where);
    }
    return result.error();
}

/// Hard failure (REQUIRE-equivalent): an add that was expected to be refused must be refused.
[[nodiscard]] DrawError requireRejectedAdd(core::ResultVoid<DrawError> result,
                                           std::string_view what,
                                           std::source_location where =
                                               std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(
            std::format("{}: expected a rejection but the primitive was recorded", what),
            where);
    }
    return result.error();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

const mdux::spec::Register vertexLayout{
    "UiVertex is the 24-byte layout the shader declares", "evidence-unit", [] {
        return speclab::Test("draw-vertex-layout")
            .Given("a UiVertex definition", [] {})
            .When("its layout is measured", [] {})
            .Then("it is the 24-byte layout the shader declares",
                  [] {
                      // The static_asserts in the module are the real gate - this scenario exists
                      // so the requirement is visible to someone reading the suite, and so a
                      // reviewer sees the number rather than trusting that an assertion somewhere
                      // covers it.
                      mdux::spec::Checks checks;
                      checks.expect(sizeof(UiVertex) == 24, "sizeof(UiVertex) is 24");
                      checks.expect(alignof(UiVertex) == 4, "alignof(UiVertex) is 4");
                      checks.expect(offsetof(UiVertex, x) == 0, "x at offset 0");
                      checks.expect(offsetof(UiVertex, u) == 8, "u at offset 8");
                      checks.expect(offsetof(UiVertex, color) == 16, "color at offset 16");
                      checks.expect(offsetof(UiVertex, mode) == 20, "mode at offset 20");
                      checks.expect(std::is_trivially_copyable_v<UiVertex>,
                                    "UiVertex is trivially copyable");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register drawModeConstants{
    "DrawMode values are the shader's constants", "evidence-unit", [] {
        return speclab::Test("draw-mode-constants")
            .Given("the DrawMode enumerators", [] {})
            .When("each is read as an integer", [] {})
            .Then("they are the shader's constants",
                  [] {
                      // ui.frag switches on these. A renumbering here that is not matched there
                      // produces a silently wrong fragment path rather than a diagnostic.
                      mdux::spec::Checks checks;
                      checks.expect(static_cast<std::uint32_t>(DrawMode::Solid) == 0, "Solid");
                      checks.expect(static_cast<std::uint32_t>(DrawMode::CoverageR8) == 1,
                                    "CoverageR8");
                      checks.expect(static_cast<std::uint32_t>(DrawMode::SampledRgba) == 2,
                                    "SampledRgba");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register packColorMemoryOrder{
    "packColor writes bytes in memory order R, G, B, A", "evidence-unit", [] {
        struct State {
            std::uint32_t packed{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-pack-color-memory-order")
            .Given("a colour with distinct channels",
                   [state] { state->packed = packColor(core::ColorRgba8{.r = 1, .g = 2, .b = 3, .a = 4}); })
            .When("its bytes are read back", [] {})
            .Then("they are R, G, B, A in memory order",
                  [state] {
                      // The shader reads these four bytes as R8G8B8A8_UNORM in memory order, so the
                      // packing is checked byte by byte rather than as a single integer - an integer
                      // comparison would pass on a host whose byte order made the shader read them
                      // backwards.
                      const auto* bytes =
                          reinterpret_cast<const std::uint8_t*>(&state->packed);
                      mdux::spec::Checks checks;
                      checks.expect(bytes[0] == 1, "byte 0 is red");
                      checks.expect(bytes[1] == 2, "byte 1 is green");
                      checks.expect(bytes[2] == 3, "byte 2 is blue");
                      checks.expect(bytes[3] == 4, "byte 3 is alpha");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

const mdux::spec::Register createdEmpty{
    "A list over adequate storage is created empty", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-created-empty")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("it is inspected", [] {})
            .Then("it is empty with its budget intact",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->empty(), "the list is empty");
                      checks.expect(state->list->vertices().empty(), "no vertices");
                      checks.expect(state->list->indices().empty(), "no indices");
                      checks.expect(state->list->commands().empty(), "no commands");
                      checks.expect(state->list->budget() == SmallStorage::budget(),
                                    "the budget survives construction");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register emptyBudgetRejected{
    "A budget with no room for one primitive is rejected", "evidence-unit", [] {
        struct State {
            std::array<UiVertex, 4> vertices{};
            std::array<Index, 6> indices{};
            std::array<DrawCommand, 1> commands{};
            DrawError tooFewVertices;
            DrawError tooFewIndices;
            DrawError noCommands;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-empty-budget-rejected")
            .Given("budgets with no room for one primitive",
                   [state] {
                       state->tooFewVertices = requireRejected(
                           DrawList::create(state->vertices, state->indices, state->commands,
                                            DrawBudget{.maxVertices = 3,
                                                       .maxIndices = 6,
                                                       .maxCommands = 1}),
                           "a budget with too few vertices");
                       state->tooFewIndices = requireRejected(
                           DrawList::create(state->vertices, state->indices, state->commands,
                                            DrawBudget{.maxVertices = 4,
                                                       .maxIndices = 5,
                                                       .maxCommands = 1}),
                           "a budget with too few indices");
                       state->noCommands = requireRejected(
                           DrawList::create(state->vertices, state->indices, state->commands,
                                            DrawBudget{.maxVertices = 4,
                                                       .maxIndices = 6,
                                                       .maxCommands = 0}),
                           "a budget with no command room");
                   })
            .When("each budget is offered to create()", [] {})
            .Then("every one is rejected as EmptyBudget",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->tooFewVertices == DrawError::EmptyBudget,
                                    "too few vertices");
                      checks.expect(state->tooFewIndices == DrawError::EmptyBudget,
                                    "too few indices");
                      checks.expect(state->noCommands == DrawError::EmptyBudget,
                                    "no command room");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register budgetBeyondIndexWidthRejected{
    "A budget beyond the index width is rejected", "evidence-unit", [] {
        struct State {
            std::array<UiVertex, 4> vertices{};
            std::array<Index, 6> indices{};
            std::array<DrawCommand, 1> commands{};
            DrawError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-budget-beyond-index-width-rejected")
            .Given("a budget one past the 16-bit index width",
                   [state] {
                       // 16-bit indices cap a list at 65536 vertices. Accepting a larger budget
                       // would produce wrapped indices - geometry pointing at the wrong vertices,
                       // with no error anywhere.
                       state->error = requireRejected(
                           DrawList::create(state->vertices, state->indices, state->commands,
                                            DrawBudget{.maxVertices = maxIndexableVertices + 1,
                                                       .maxIndices = 6,
                                                       .maxCommands = 1}),
                           "the oversize budget");
                   })
            .When("it is offered to create()", [] {})
            .Then("it is rejected as BudgetExceedsIndexWidth",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == DrawError::BudgetExceedsIndexWidth,
                                    "the error is BudgetExceedsIndexWidth");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register storageSmallerThanBudgetRejected{
    "Storage smaller than the budget is rejected at construction", "evidence-unit", [] {
        struct State {
            std::array<UiVertex, 4> vertices{};
            std::array<Index, 6> indices{};
            std::array<DrawCommand, 1> commands{};
            DrawError error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-storage-smaller-than-budget-rejected")
            .Given("storage smaller than the budget claims",
                   [state] {
                       // Rejected here rather than discovered on the frame that overflows it,
                       // which on a device would be a buffer overrun rather than an error.
                       const DrawBudget budget{.maxVertices = 8, .maxIndices = 6, .maxCommands = 1};
                       state->error = requireRejected(
                           DrawList::create(state->vertices, state->indices, state->commands,
                                            budget),
                           "the undersized storage");
                   })
            .When("it is offered to create()", [] {})
            .Then("it is rejected as StorageTooSmall",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == DrawError::StorageTooSmall,
                                    "the error is StorageTooSmall");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

const mdux::spec::Register solidRectGeometry{
    "A solid rectangle records four vertices and six indices", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-solid-rect-geometry")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a solid rectangle is added",
                  [state] { requireAdded(state->list->addSolidRect(rect, red), "addSolidRect"); })
            .Then("it records the corners, the diagonal and the mode",
                  [state] {
                      if (state->list->vertices().size() != 4) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 4 vertices, got {}",
                                          state->list->vertices().size()),
                              std::source_location::current());
                      }
                      if (state->list->indices().size() != 6) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 6 indices, got {}",
                                          state->list->indices().size()),
                              std::source_location::current());
                      }
                      if (state->list->commands().size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 command, got {}",
                                          state->list->commands().size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(!state->list->empty(), "the list is not empty");

                      // Corner order: top-left, top-right, bottom-right, bottom-left.
                      checks.expect(state->list->vertices()[0].x == 10.0F,
                                    "top-left x");
                      checks.expect(state->list->vertices()[0].y == 20.0F,
                                    "top-left y");
                      checks.expect(state->list->vertices()[1].x == 40.0F,
                                    "top-right x");
                      checks.expect(state->list->vertices()[1].y == 20.0F,
                                    "top-right y");
                      checks.expect(state->list->vertices()[2].x == 40.0F,
                                    "bottom-right x");
                      checks.expect(state->list->vertices()[2].y == 60.0F,
                                    "bottom-right y");
                      checks.expect(state->list->vertices()[3].x == 10.0F,
                                    "bottom-left x");
                      checks.expect(state->list->vertices()[3].y == 60.0F,
                                    "bottom-left y");

                      // Two triangles sharing the 0-2 diagonal.
                      const std::array<Index, 6> expected{0, 1, 2, 0, 2, 3};
                      checks.expect(std::ranges::equal(state->list->indices(), expected),
                                    "the index order");

                      for (const UiVertex& vertex : state->list->vertices()) {
                          checks.expect(
                              vertex.mode == static_cast<std::uint32_t>(DrawMode::Solid),
                              "vertex mode is Solid");
                          checks.expect(vertex.color == packColor(red),
                                        "vertex colour");
                      }

                      checks.expect(state->list->commands()[0].firstIndex == 0,
                                    "the command starts at index 0");
                      checks.expect(state->list->commands()[0].indexCount == 6,
                                    "the command spans 6 indices");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register solidRectUvZeroed{
    "A solid rectangle's uv is zeroed rather than left undefined", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-solid-rect-uv-zeroed")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a solid rectangle is added",
                  [state] { requireAdded(state->list->addSolidRect(rect, red), "addSolidRect"); })
            .Then("every vertex carries a zero uv",
                  [state] {
                      // An uninitialised float in a buffer that is hashed or compared would make a
                      // frame non-reproducible, which is exactly what #126's pixel test cannot
                      // tolerate.
                      mdux::spec::Checks checks;
                      for (const UiVertex& vertex : state->list->vertices()) {
                          checks.expect(vertex.u == 0.0F, "u is zero");
                          checks.expect(vertex.v == 0.0F, "v is zero");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register texturedRectUv{
    "A textured rectangle carries its uv corners", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-textured-rect-uv")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a rectangle is added in the sampled mode",
                  [state] {
                      const core::Rect uv{.x = 1, .y = 2, .width = 3, .height = 4};
                      requireAdded(state->list->addRect(rect, red, DrawMode::SampledRgba, uv),
                                   "addRect");
                  })
            .Then("it carries the uv corners",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->vertices()[0].u == 1.0F, "top-left u");
                      checks.expect(state->list->vertices()[0].v == 2.0F, "top-left v");
                      checks.expect(state->list->vertices()[2].u == 4.0F, "bottom-right u");
                      checks.expect(state->list->vertices()[2].v == 6.0F, "bottom-right v");
                      for (const UiVertex& vertex : state->list->vertices()) {
                          checks.expect(
                              vertex.mode == static_cast<std::uint32_t>(DrawMode::SampledRgba),
                              "vertex mode is SampledRgba");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register degenerateRectRefused{
    "A degenerate rectangle is refused and records nothing", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
            DrawError zeroWidth;
            DrawError negative;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-degenerate-rect-refused")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a zero-width and a negative rectangle are offered",
                  [state] {
                      state->zeroWidth = requireRejectedAdd(
                          state->list->addSolidRect(core::Rect{.x = 0,
                                                               .y = 0,
                                                               .width = 0,
                                                               .height = 5},
                                                    red),
                          "the zero-width rectangle");
                      state->negative = requireRejectedAdd(
                          state->list->addSolidRect(core::Rect{.x = 0,
                                                               .y = 0,
                                                               .width = 5,
                                                               .height = -1},
                                                    red),
                          "the negative-height rectangle");
                  })
            .Then("both are refused as DegenerateRect and nothing is recorded",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->zeroWidth == DrawError::DegenerateRect,
                                    "zero width is DegenerateRect");
                      checks.expect(state->negative == DrawError::DegenerateRect,
                                    "negative height is DegenerateRect");
                      checks.expect(state->list->vertices().empty(), "no vertices recorded");
                      checks.expect(state->list->commands().empty(), "no commands recorded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register indicesTrackGrowth{
    "Indices address the right vertices as a list grows", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-indices-track-growth")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("two solid rectangles are added",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                  })
            .Then("the second rectangle addresses vertices 4 through 7",
                  [state] {
                      const std::array<Index, 12> expected{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::equal(state->list->indices(), expected),
                                    "the running index order");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Commands and clipping
// ---------------------------------------------------------------------------

const mdux::spec::Register clipSharesCommand{
    "Primitives under one clip share a command", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-clip-shares-command")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("three solid rectangles are added under the default clip",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                      requireAdded(state->list->addSolidRect(rect, red), "third addSolidRect");
                  })
            .Then("they share a single command spanning all three",
                  [state] {
                      // What makes a command budget a meaningful number: the count is proportional
                      // to clip changes, not to the number of rectangles.
                      if (state->list->commands().size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 command, got {}",
                                          state->list->commands().size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands()[0].firstIndex == 0,
                                    "the command starts at index 0");
                      checks.expect(state->list->commands()[0].indexCount == 18,
                                    "the command spans 18 indices");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register clipChangeStartsCommand{
    "A clip change starts a new command", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
            core::Rect clip;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-clip-change-starts-command")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a rectangle is added and the clip changes before the next",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      state->clip = core::Rect{.x = 0, .y = 0, .width = 100, .height = 100};
                      state->list->setClip(state->clip);
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                  })
            .Then("the two rectangles are two commands, the second under the new clip",
                  [state] {
                      if (state->list->commands().size() != 2) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 2 commands, got {}",
                                          state->list->commands().size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands()[0].indexCount == 6,
                                    "the first command spans 6 indices");
                      checks.expect(state->list->commands()[1].firstIndex == 6,
                                    "the second command starts at index 6");
                      checks.expect(state->list->commands()[1].indexCount == 6,
                                    "the second command spans 6 indices");
                      checks.expect(state->list->commands()[1].clip == state->clip,
                                    "the second command carries the new clip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register clipWithNothingDrawn{
    "A clip set with nothing drawn under it produces no command", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
            core::Rect second;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-clip-with-nothing-drawn")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("two clips are set and then a rectangle is drawn",
                  [state] {
                      // setClip records rather than applies; emitting a command here would put an
                      // empty draw in the buffer and consume a budget slot for nothing.
                      // Named rather than written inline: a braced initialiser inside a macro would
                      // have its commas read as argument separators.
                      constexpr core::Rect first{.x = 1, .y = 1, .width = 2, .height = 2};
                      state->second = core::Rect{.x = 3, .y = 3, .width = 4, .height = 4};
                      state->list->setClip(first);
                      state->list->setClip(state->second);
                      requireAdded(state->list->addSolidRect(rect, red), "addSolidRect");
                  })
            .Then("only the rectangle's clip becomes a command",
                  [state] {
                      if (state->list->commands().size() != 1) {
                          throw speclab::core::AssertionFailure(
                              std::format("expected 1 command, got {}",
                                          state->list->commands().size()),
                              std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands()[0].clip == state->second,
                                    "the command carries the last clip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register returningClipStartsCommand{
    "Returning to a previous clip still starts a new command", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-returning-clip-starts-command")
            .Given("a list over adequate storage",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a rectangle is drawn, the clip changes, and another rectangle is drawn",
                  [state] {
                      // Commands are a sequence, not a set: merging with an earlier command would
                      // reorder the frame, drawing the third rectangle before the second.
                      const core::Rect a{.x = 0, .y = 0, .width = 10, .height = 10};
                      state->list->setClip(a);
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      state->list->setClip(
                          core::Rect{.x = 5, .y = 5, .width = 10, .height = 10});
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                  })
            .Then("there are two commands",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands().size() == 2,
                                    "two commands are recorded");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The budget boundary
// ---------------------------------------------------------------------------

const mdux::spec::Register frameFillsBudget{
    "A frame that exactly fills its budget succeeds", "evidence-unit", [] {
        struct State {
            Storage<8, 12, 1> storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-frame-fills-budget")
            .Given("a list whose budget holds exactly two rectangles",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("two solid rectangles are added",
                  [state] {
                      // The off-by-one that would make the budget one primitive smaller than it
                      // claims.
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                  })
            .Then("the buffers hold exactly the full budget",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->vertices().size() == 8, "all 8 vertices");
                      checks.expect(state->list->indices().size() == 12, "all 12 indices");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register primitivePastBudget{
    "The primitive past the budget fails and records nothing", "evidence-unit", [] {
        struct State {
            Storage<8, 12, 1> storage;
            std::optional<DrawList> list;
            DrawError overflow;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-primitive-past-budget")
            .Given("a list whose budget holds exactly two rectangles",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a third rectangle is offered",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                      state->overflow = requireRejectedAdd(
                          state->list->addSolidRect(rect, red), "the third rectangle");
                  })
            .Then("it is refused and nothing is half-recorded",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->overflow == DrawError::VertexBudgetExceeded,
                                    "the overflow is VertexBudgetExceeded");
                      // Nothing half-recorded: a partially written primitive would render as
                      // stray geometry.
                      checks.expect(state->list->vertices().size() == 8, "still 8 vertices");
                      checks.expect(state->list->indices().size() == 12, "still 12 indices");
                      checks.expect(state->list->commands().size() == 1, "still 1 command");
                      checks.expect(state->list->commands()[0].indexCount == 12,
                                    "the command still spans 12 indices");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register indexBudgetBinds{
    "An index budget can bind before the vertex budget", "evidence-unit", [] {
        struct State {
            Storage<64, 6, 4> storage;
            std::optional<DrawList> list;
            DrawError overflow;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-index-budget-binds")
            .Given("storage generous on vertices and tight on indices",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a second rectangle is offered",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      state->overflow = requireRejectedAdd(
                          state->list->addSolidRect(rect, red), "the second rectangle");
                  })
            .Then("the index check is what fires",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->overflow == DrawError::IndexBudgetExceeded,
                                    "the overflow is IndexBudgetExceeded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register commandBudgetBinds{
    "A command budget binds when clips change too often", "evidence-unit", [] {
        struct State {
            Storage<64, 96, 2> storage;
            std::optional<DrawList> list;
            DrawError overflow;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-command-budget-binds")
            .Given("a list whose command budget holds two commands",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a third clip forces a third command",
                  [state] {
                      state->list->setClip(
                          core::Rect{.x = 0, .y = 0, .width = 1, .height = 1});
                      requireAdded(state->list->addSolidRect(rect, red), "first addSolidRect");
                      state->list->setClip(
                          core::Rect{.x = 1, .y = 1, .width = 1, .height = 1});
                      requireAdded(state->list->addSolidRect(rect, red), "second addSolidRect");
                      state->list->setClip(
                          core::Rect{.x = 2, .y = 2, .width = 1, .height = 1});
                      state->overflow = requireRejectedAdd(
                          state->list->addSolidRect(rect, red), "the third rectangle");
                  })
            .Then("the third primitive is refused and left nothing behind",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->overflow == DrawError::CommandBudgetExceeded,
                                    "the overflow is CommandBudgetExceeded");
                      // The refused primitive left no vertices behind either.
                      checks.expect(state->list->vertices().size() == 8, "still 8 vertices");
                      checks.expect(state->list->commands().size() == 2, "still 2 commands");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register commandBudgetOfOne{
    "A command budget of one is not exceeded while the clip holds", "evidence-unit", [] {
        struct State {
            Storage<64, 96, 1> storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-command-budget-of-one")
            .Given("a list with room for a single command",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("eight rectangles are added without changing the clip",
                  [state] {
                      // Guards the command check against firing on every primitive rather than on
                      // every change.
                      for (int i = 0; i < 8; ++i) {
                          requireAdded(state->list->addSolidRect(rect, red),
                                       "addSolidRect in the loop");
                      }
                  })
            .Then("they all share the single command",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands().size() == 1, "one command");
                      checks.expect(state->list->commands()[0].indexCount == 48,
                                    "the command spans 48 indices");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// reset() and determinism
// ---------------------------------------------------------------------------

const mdux::spec::Register resetEmpties{
    "reset() empties the list and the budget survives", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-reset-empties")
            .Given("a list over adequate storage with a rectangle drawn",
                   [state] {
                       state->list = requireCreated(state->storage.list(), "the list");
                       requireAdded(state->list->addSolidRect(rect, red), "addSolidRect");
                   })
            .When("it is reset", [state] { state->list->reset(); })
            .Then("it is empty again, with its budget intact and reusable storage",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->empty(), "the list is empty");
                      checks.expect(state->list->vertices().empty(), "no vertices");
                      checks.expect(state->list->indices().empty(), "no indices");
                      checks.expect(state->list->commands().empty(), "no commands");
                      checks.expect(state->list->budget() == SmallStorage::budget(),
                                    "the budget survives reset");
                      checks.raise();

                      // And the list is usable again, from the start of its storage.
                      requireAdded(state->list->addSolidRect(rect, red), "addSolidRect after reset");
                      mdux::spec::Checks after;
                      after.expect(state->list->vertices().size() == 4,
                                   "the rectangle records 4 vertices");
                      after.expect(state->list->indices()[0] == 0,
                                   "the indices restart at 0");
                      after.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register resetClearsClip{
    "reset() clears the clip, so the next frame does not inherit one", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-reset-clears-clip")
            .Given("a list with a clip set and a rectangle drawn",
                   [state] {
                       state->list = requireCreated(state->storage.list(), "the list");
                       state->list->setClip(
                           core::Rect{.x = 7, .y = 7, .width = 7, .height = 7});
                       requireAdded(state->list->addSolidRect(rect, red), "addSolidRect");
                   })
            .When("it is reset and a rectangle is drawn",
                  [state] {
                      state->list->reset();
                      requireAdded(state->list->addSolidRect(rect, red), "addSolidRect after reset");
                  })
            .Then("the new frame records no clip",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->list->commands()[0].clip == core::Rect{},
                                    "the command carries the default clip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register byteIdenticalBuffers{
    "The same primitives produce byte-identical buffers", "evidence-unit", [] {
        struct State {
            Storage<16, 24, 2> first;
            Storage<64, 96, 8> second;
            std::optional<DrawList> a;
            std::optional<DrawList> b;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-byte-identical-buffers")
            .Given("two lists over different amounts of storage, one previously dirtied",
                   [state] {
                       // The property #126's pixel comparison rests on: a frame is a function of
                       // its primitives and nothing else - not of how much storage it was given,
                       // nor of what was there before.
                       state->a = requireCreated(state->first.list(), "the first list");
                       state->b = requireCreated(state->second.list(), "the second list");

                       // The second list has drawn a different frame before, so its storage is
                       // dirty.
                       constexpr core::Rect unit{.x = 1, .y = 1, .width = 1, .height = 1};
                       requireAdded(
                           state->b->addRect(unit, red, DrawMode::CoverageR8, core::Rect{}),
                           "the dirtied frame");
                       state->b->reset();
                   })
            .When("the same primitives are drawn into both",
                  [state] {
                      constexpr core::Rect unit{.x = 1, .y = 1, .width = 1, .height = 1};
                      constexpr core::Rect clip{.x = 2, .y = 2, .width = 5, .height = 5};
                      constexpr core::ColorRgba8 other{.r = 9, .g = 8, .b = 7, .a = 6};
                      for (DrawList* list : {&*state->a, &*state->b}) {
                          requireAdded(list->addSolidRect(rect, red), "addSolidRect");
                          list->setClip(clip);
                          requireAdded(list->addRect(rect, other, DrawMode::CoverageR8, unit),
                                       "addRect");
                      }
                  })
            .Then("the buffers are byte-identical",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::equal(state->a->vertices(), state->b->vertices()),
                                    "the vertex buffers match");
                      checks.expect(std::ranges::equal(state->a->indices(), state->b->indices()),
                                    "the index buffers match");
                      checks.expect(std::ranges::equal(state->a->commands(), state->b->commands()),
                                    "the command buffers match");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register drawErrorDescriptions{
    "Every DrawError has its own description", "evidence-unit", [] {
        return speclab::Test("draw-error-descriptions")
            .Given("every DrawError enumerator", [] {})
            .When("each is described", [] {})
            .Then("each has a unique, non-empty description",
                  [] {
                      // Every enumerator, not a subset. WrongList was absent from this list until
                      // #257 added DegenerateQuad beside it and the omission became visible - which
                      // is the failure mode a hand-maintained "all" array has, and the reason the
                      // scenario below counts them against the enumeration's own size.
                      constexpr std::array<DrawError, 9> all{
                          DrawError::EmptyBudget,
                          DrawError::BudgetExceedsIndexWidth,
                          DrawError::StorageTooSmall,
                          DrawError::VertexBudgetExceeded,
                          DrawError::IndexBudgetExceeded,
                          DrawError::CommandBudgetExceeded,
                          DrawError::DegenerateRect,
                          DrawError::WrongList,
                          DrawError::DegenerateQuad,
                      };
                      std::vector<std::string_view> seen;
                      mdux::spec::Checks checks;
                      for (const DrawError error : all) {
                          const std::string_view text = describe(error);
                          checks.expect(!text.empty(), "a description exists");
                          checks.expect(std::ranges::find(seen, text) == seen.end(),
                                        "the description is unique");
                          seen.push_back(text);
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// addSolidQuad() - the rotated primitive a polyline segment needs (#257)
// ---------------------------------------------------------------------------

/// The four corners of a unit-ish quad, wound as a ring. Not axis-aligned, so an implementation
/// that quietly took a bounding box would be visible in the vertices.
constexpr std::array<Point2F, 4> slantedQuad{
    Point2F{ .x = 10.5F, .y = 20.0F},
    Point2F{.x = 40.25F, .y = 24.0F},
    Point2F{.x = 40.25F, .y = 26.0F},
    Point2F{ .x = 10.5F, .y = 22.0F}
};

const mdux::spec::Register quadKeepsItsCorners{
    "A solid quad records its four corners unrounded", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-quad-keeps-corners")
            .Given("a list and a quad whose corners are not whole pixels",
                   [state] {
                       state->list = requireCreated(state->storage.list(), "the list");
                       requireAdded(state->list->addSolidQuad(slantedQuad, red), "the quad");
                   })
            .When("its vertices are read back", [] {})
            .Then("each corner survives exactly, in the order it was given",
                  [state] {
                      // The whole reason Point2F exists: an integer type would have rounded 10.5 and
                      // 40.25, and a 1px stroke would alternate between one and two pixels wide as
                      // its slope changed. Compared exactly rather than within a tolerance - these
                      // are copies, not computations.
                      const std::span<const UiVertex> vertices = state->list->vertices();
                      mdux::spec::Checks checks;
                      checks.expect(vertices.size() == 4, "a quad costs four vertices");
                      if (vertices.size() != 4) {
                          checks.raise();
                          return;
                      }
                      for (std::size_t i = 0; i < 4; ++i) {
                          checks.expect(vertices[i].x == slantedQuad[i].x,
                                        std::format("corner {} keeps its x", i));
                          checks.expect(vertices[i].y == slantedQuad[i].y,
                                        std::format("corner {} keeps its y", i));
                          checks.expect(vertices[i].mode == static_cast<std::uint32_t>(DrawMode::Solid),
                                        std::format("corner {} is Solid", i));
                          checks.expect(vertices[i].u == 0.0F && vertices[i].v == 0.0F,
                                        std::format("corner {} carries a zeroed uv", i));
                      }
                      checks.expect(state->list->indices().size() == 6, "a quad costs six indices");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register quadRefusesDegenerate{
    "A quad enclosing no area, or carrying a non-finite corner, is refused", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
            std::optional<DrawError> collinear;
            std::optional<DrawError> notANumber;
            std::optional<DrawError> infinite;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-quad-refuses-degenerate")
            .Given("a list",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("degenerate quads are offered",
                  [state] {
                      constexpr std::array<Point2F, 4> collinear{
                          Point2F{.x = 0.0F, .y = 0.0F},
                          Point2F{.x = 1.0F, .y = 1.0F},
                          Point2F{.x = 2.0F, .y = 2.0F},
                          Point2F{.x = 3.0F, .y = 3.0F}
                      };
                      state->collinear = requireRejectedAdd(state->list->addSolidQuad(collinear, red),
                                                            "a collinear quad");

                      std::array<Point2F, 4> withNan = slantedQuad;
                      withNan[2].y = std::numeric_limits<float>::quiet_NaN();
                      state->notANumber = requireRejectedAdd(state->list->addSolidQuad(withNan, red),
                                                              "a quad with a NaN corner");

                      std::array<Point2F, 4> withInf = slantedQuad;
                      withInf[0].x = std::numeric_limits<float>::infinity();
                      state->infinite = requireRejectedAdd(state->list->addSolidQuad(withInf, red),
                                                            "a quad with an infinite corner");
                  })
            .Then("each is DegenerateQuad and nothing was recorded",
                  [state] {
                      // The NaN case is the one that earns the finiteness test being first: every
                      // check after it is a comparison, and a NaN compares false against all of
                      // them - so an unchecked NaN would fall through the area test as "not
                      // degenerate" and reach the rasteriser, where it is undefined behaviour.
                      mdux::spec::Checks checks;
                      checks.expect(state->collinear == DrawError::DegenerateQuad,
                                    "a collinear quad is DegenerateQuad");
                      checks.expect(state->notANumber == DrawError::DegenerateQuad,
                                    "a NaN corner is DegenerateQuad");
                      checks.expect(state->infinite == DrawError::DegenerateQuad,
                                    "an infinite corner is DegenerateQuad");
                      checks.expect(state->list->vertices().empty(), "no vertex was recorded");
                      checks.expect(state->list->indices().empty(), "no index was recorded");
                      checks.expect(state->list->commands().empty(), "no command was started");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register quadSharesTheBudget{
    "A quad is held to the same budget a rectangle is", "evidence-unit", [] {
        struct State {
            Storage<8, 12, 2> storage;
            std::optional<DrawList> list;
            std::optional<DrawError> refused;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-quad-shares-budget")
            .Given("a list with room for exactly two primitives",
                   [state] { state->list = requireCreated(state->storage.list(), "the list"); })
            .When("a rectangle, a quad and one more quad are offered",
                  [state] {
                      requireAdded(state->list->addSolidRect(rect, red), "the rectangle");
                      requireAdded(state->list->addSolidQuad(slantedQuad, red), "the first quad");
                      state->refused = requireRejectedAdd(state->list->addSolidQuad(slantedQuad, red),
                                                          "the third primitive");
                  })
            .Then("the third is refused on the vertex budget and the list is intact",
                  [state] {
                      // A quad that bypassed the budget would be the whole fixed-budget property
                      // gone, since #257 makes a trace the primitive a screen records most of.
                      mdux::spec::Checks checks;
                      checks.expect(state->refused == DrawError::VertexBudgetExceeded,
                                    "the third primitive exceeds the vertex budget");
                      checks.expect(state->list->vertices().size() == 8, "two primitives were kept");
                      checks.expect(state->list->indices().size() == 12, "their indices were kept");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register quadExtendsTheCurrentCommand{
    "A quad under an unchanged clip extends the current command", "evidence-unit", [] {
        struct State {
            SmallStorage storage;
            std::optional<DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("draw-quad-extends-command")
            .Given("a list holding one rectangle",
                   [state] {
                       state->list = requireCreated(state->storage.list(), "the list");
                       requireAdded(state->list->addSolidRect(rect, red), "the rectangle");
                   })
            .When("a quad is added under the same clip",
                  [state] { requireAdded(state->list->addSolidQuad(slantedQuad, red), "the quad"); })
            .Then("one command covers both",
                  [state] {
                      // The property that keeps a command budget meaningful: a trace of 256 samples
                      // is 511 primitives and must not be 511 commands.
                      const std::span<const DrawCommand> commands = state->list->commands();
                      mdux::spec::Checks checks;
                      checks.expect(commands.size() == 1, "one command");
                      if (!commands.empty()) {
                          checks.expect(commands[0].indexCount == 12, "it claims both primitives' indices");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
