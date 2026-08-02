/**
 * @brief Tests for mdux.draw.
 *
 * Three groups, and the boundaries are where the value is. The layout assertions are compile-time
 * and live in the module itself; what is tested here is the behaviour at the edge of a budget -
 * a frame that exactly fills it must succeed, and the very next primitive must fail with nothing
 * half-recorded - and the determinism a byte-compared frame depends on.
 */
// offsetof is a macro, so it comes from a header rather than `import std;` - and the header has
// to precede the import, exactly as the Vulkan SC tests include <vulkan/vulkan.h> first. Included
// afterwards, <cstddef> redefines std::byte against the module and the translation unit does not
// compile.
#include <cstddef>

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.test;

#include "../framework/MduXTest.hpp"

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

}  // namespace

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

TEST_CASE("UiVertex is the 24-byte layout the shader declares", "evidence-unit") {
    // The static_asserts in the module are the real gate - this test exists so the requirement is
    // visible to someone reading the suite, and so a reviewer sees the number rather than trusting
    // that an assertion somewhere covers it.
    CHECK(sizeof(UiVertex) == 24);
    CHECK(alignof(UiVertex) == 4);
    CHECK(offsetof(UiVertex, x) == 0);
    CHECK(offsetof(UiVertex, u) == 8);
    CHECK(offsetof(UiVertex, color) == 16);
    CHECK(offsetof(UiVertex, mode) == 20);
    CHECK(std::is_trivially_copyable_v<UiVertex>);
}

TEST_CASE("DrawMode values are the shader's constants", "evidence-unit") {
    // ui.frag switches on these. A renumbering here that is not matched there produces a
    // silently wrong fragment path rather than a diagnostic.
    CHECK(static_cast<std::uint32_t>(DrawMode::Solid) == 0);
    CHECK(static_cast<std::uint32_t>(DrawMode::CoverageR8) == 1);
    CHECK(static_cast<std::uint32_t>(DrawMode::SampledRgba) == 2);
}

TEST_CASE("packColor writes bytes in memory order R, G, B, A", "evidence-unit") {
    // The shader reads these four bytes as R8G8B8A8_UNORM in memory order, so the packing is
    // checked byte by byte rather than as a single integer - an integer comparison would pass on
    // a host whose byte order made the shader read them backwards.
    const std::uint32_t packed = packColor(core::ColorRgba8{.r = 1, .g = 2, .b = 3, .a = 4});
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&packed);
    CHECK(bytes[0] == 1);
    CHECK(bytes[1] == 2);
    CHECK(bytes[2] == 3);
    CHECK(bytes[3] == 4);
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

TEST_CASE("A list over adequate storage is created empty", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    CHECK(list->empty());
    CHECK(list->vertices().empty());
    CHECK(list->indices().empty());
    CHECK(list->commands().empty());
    CHECK(list->budget() == SmallStorage::budget());
}

TEST_CASE("A budget with no room for one primitive is rejected", "evidence-unit") {
    std::array<UiVertex, 4> vertices{};
    std::array<Index, 6> indices{};
    std::array<DrawCommand, 1> commands{};

    auto tooFewVertices = DrawList::create(
        vertices, indices, commands,
        DrawBudget{.maxVertices = 3, .maxIndices = 6, .maxCommands = 1});
    REQUIRE(!tooFewVertices.has_value());
    CHECK(tooFewVertices.error() == DrawError::EmptyBudget);

    auto tooFewIndices = DrawList::create(
        vertices, indices, commands,
        DrawBudget{.maxVertices = 4, .maxIndices = 5, .maxCommands = 1});
    REQUIRE(!tooFewIndices.has_value());
    CHECK(tooFewIndices.error() == DrawError::EmptyBudget);

    auto noCommands = DrawList::create(
        vertices, indices, commands,
        DrawBudget{.maxVertices = 4, .maxIndices = 6, .maxCommands = 0});
    REQUIRE(!noCommands.has_value());
    CHECK(noCommands.error() == DrawError::EmptyBudget);
}

TEST_CASE("A budget beyond the index width is rejected", "evidence-unit") {
    // 16-bit indices cap a list at 65536 vertices. Accepting a larger budget would produce
    // wrapped indices - geometry pointing at the wrong vertices, with no error anywhere.
    std::array<UiVertex, 4> vertices{};
    std::array<Index, 6> indices{};
    std::array<DrawCommand, 1> commands{};
    auto list = DrawList::create(
        vertices, indices, commands,
        DrawBudget{.maxVertices = maxIndexableVertices + 1, .maxIndices = 6, .maxCommands = 1});
    REQUIRE(!list.has_value());
    CHECK(list.error() == DrawError::BudgetExceedsIndexWidth);
}

TEST_CASE("Storage smaller than the budget is rejected at construction", "evidence-unit") {
    // Rejected here rather than discovered on the frame that overflows it, which on a device
    // would be a buffer overrun rather than an error.
    std::array<UiVertex, 4> vertices{};
    std::array<Index, 6> indices{};
    std::array<DrawCommand, 1> commands{};
    const DrawBudget budget{.maxVertices = 8, .maxIndices = 6, .maxCommands = 1};

    auto list = DrawList::create(vertices, indices, commands, budget);
    REQUIRE(!list.has_value());
    CHECK(list.error() == DrawError::StorageTooSmall);
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

TEST_CASE("A solid rectangle records four vertices and six indices", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    REQUIRE(list->vertices().size() == 4);
    REQUIRE(list->indices().size() == 6);
    REQUIRE(list->commands().size() == 1);
    CHECK(!list->empty());

    // Corner order: top-left, top-right, bottom-right, bottom-left.
    CHECK(list->vertices()[0].x == 10.0F);
    CHECK(list->vertices()[0].y == 20.0F);
    CHECK(list->vertices()[1].x == 40.0F);
    CHECK(list->vertices()[1].y == 20.0F);
    CHECK(list->vertices()[2].x == 40.0F);
    CHECK(list->vertices()[2].y == 60.0F);
    CHECK(list->vertices()[3].x == 10.0F);
    CHECK(list->vertices()[3].y == 60.0F);

    // Two triangles sharing the 0-2 diagonal.
    const std::array<Index, 6> expected{0, 1, 2, 0, 2, 3};
    CHECK(std::ranges::equal(list->indices(), expected));

    for (const UiVertex& vertex : list->vertices()) {
        CHECK(vertex.mode == static_cast<std::uint32_t>(DrawMode::Solid));
        CHECK(vertex.color == packColor(red));
    }

    CHECK(list->commands()[0].firstIndex == 0);
    CHECK(list->commands()[0].indexCount == 6);
}

TEST_CASE("A solid rectangle's uv is zeroed rather than left undefined", "evidence-unit") {
    // An uninitialised float in a buffer that is hashed or compared would make a frame
    // non-reproducible, which is exactly what #126's pixel test cannot tolerate.
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    for (const UiVertex& vertex : list->vertices()) {
        CHECK(vertex.u == 0.0F);
        CHECK(vertex.v == 0.0F);
    }
}

TEST_CASE("A textured rectangle carries its uv corners", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    const core::Rect uv{.x = 1, .y = 2, .width = 3, .height = 4};
    REQUIRE(list->addRect(rect, red, DrawMode::SampledRgba, uv).has_value());

    CHECK(list->vertices()[0].u == 1.0F);
    CHECK(list->vertices()[0].v == 2.0F);
    CHECK(list->vertices()[2].u == 4.0F);
    CHECK(list->vertices()[2].v == 6.0F);
    for (const UiVertex& vertex : list->vertices()) {
        CHECK(vertex.mode == static_cast<std::uint32_t>(DrawMode::SampledRgba));
    }
}

TEST_CASE("A degenerate rectangle is refused and records nothing", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());

    auto zeroWidth = list->addSolidRect(core::Rect{.x = 0, .y = 0, .width = 0, .height = 5}, red);
    REQUIRE(!zeroWidth.has_value());
    CHECK(zeroWidth.error() == DrawError::DegenerateRect);

    auto negative = list->addSolidRect(core::Rect{.x = 0, .y = 0, .width = 5, .height = -1}, red);
    REQUIRE(!negative.has_value());
    CHECK(negative.error() == DrawError::DegenerateRect);

    CHECK(list->vertices().empty());
    CHECK(list->commands().empty());
}

TEST_CASE("Indices address the right vertices as a list grows", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    const std::array<Index, 12> expected{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    CHECK(std::ranges::equal(list->indices(), expected));
}

// ---------------------------------------------------------------------------
// Commands and clipping
// ---------------------------------------------------------------------------

TEST_CASE("Primitives under one clip share a command", "evidence-unit") {
    // What makes a command budget a meaningful number: the count is proportional to clip changes,
    // not to the number of rectangles.
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    REQUIRE(list->commands().size() == 1);
    CHECK(list->commands()[0].firstIndex == 0);
    CHECK(list->commands()[0].indexCount == 18);
}

TEST_CASE("A clip change starts a new command", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    const core::Rect clip{.x = 0, .y = 0, .width = 100, .height = 100};
    list->setClip(clip);
    REQUIRE(list->addSolidRect(rect, red).has_value());

    REQUIRE(list->commands().size() == 2);
    CHECK(list->commands()[0].indexCount == 6);
    CHECK(list->commands()[1].firstIndex == 6);
    CHECK(list->commands()[1].indexCount == 6);
    CHECK(list->commands()[1].clip == clip);
}

TEST_CASE("A clip set with nothing drawn under it produces no command", "evidence-unit") {
    // setClip records rather than applies; emitting a command here would put an empty draw in
    // the buffer and consume a budget slot for nothing.
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    // Named rather than written inline: a braced initialiser inside CHECK() would have its
    // commas read as macro argument separators.
    constexpr core::Rect first{.x = 1, .y = 1, .width = 2, .height = 2};
    constexpr core::Rect second{.x = 3, .y = 3, .width = 4, .height = 4};
    list->setClip(first);
    list->setClip(second);
    CHECK(list->commands().empty());

    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->commands().size() == 1);
    CHECK(list->commands()[0].clip == second);
}

TEST_CASE("Returning to a previous clip still starts a new command", "evidence-unit") {
    // Commands are a sequence, not a set: merging with an earlier command would reorder the
    // frame, drawing the third rectangle before the second.
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    const core::Rect a{.x = 0, .y = 0, .width = 10, .height = 10};

    list->setClip(a);
    REQUIRE(list->addSolidRect(rect, red).has_value());
    list->setClip(core::Rect{.x = 5, .y = 5, .width = 10, .height = 10});
    REQUIRE(list->addSolidRect(rect, red).has_value());

    CHECK(list->commands().size() == 2);
}

// ---------------------------------------------------------------------------
// The budget boundary
// ---------------------------------------------------------------------------

TEST_CASE("A frame that exactly fills its budget succeeds", "evidence-unit") {
    // The off-by-one that would make the budget one primitive smaller than it claims.
    Storage<8, 12, 1> storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    CHECK(list->vertices().size() == 8);
    CHECK(list->indices().size() == 12);
}

TEST_CASE("The primitive past the budget fails and records nothing", "evidence-unit") {
    Storage<8, 12, 1> storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    auto overflow = list->addSolidRect(rect, red);
    REQUIRE(!overflow.has_value());
    CHECK(overflow.error() == DrawError::VertexBudgetExceeded);

    // Nothing half-recorded: a partially written primitive would render as stray geometry.
    CHECK(list->vertices().size() == 8);
    CHECK(list->indices().size() == 12);
    CHECK(list->commands().size() == 1);
    CHECK(list->commands()[0].indexCount == 12);
}

TEST_CASE("An index budget can bind before the vertex budget", "evidence-unit") {
    // Storage is generous on vertices and tight on indices, so the index check is what fires.
    Storage<64, 6, 4> storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());

    auto overflow = list->addSolidRect(rect, red);
    REQUIRE(!overflow.has_value());
    CHECK(overflow.error() == DrawError::IndexBudgetExceeded);
}

TEST_CASE("A command budget binds when clips change too often", "evidence-unit") {
    Storage<64, 96, 2> storage;
    auto list = storage.list();
    REQUIRE(list.has_value());

    list->setClip(core::Rect{.x = 0, .y = 0, .width = 1, .height = 1});
    REQUIRE(list->addSolidRect(rect, red).has_value());
    list->setClip(core::Rect{.x = 1, .y = 1, .width = 1, .height = 1});
    REQUIRE(list->addSolidRect(rect, red).has_value());

    list->setClip(core::Rect{.x = 2, .y = 2, .width = 1, .height = 1});
    auto overflow = list->addSolidRect(rect, red);
    REQUIRE(!overflow.has_value());
    CHECK(overflow.error() == DrawError::CommandBudgetExceeded);

    // The refused primitive left no vertices behind either.
    CHECK(list->vertices().size() == 8);
    CHECK(list->commands().size() == 2);
}

TEST_CASE("A command budget of one is not exceeded while the clip holds", "evidence-unit") {
    // Guards the command check against firing on every primitive rather than on every change.
    Storage<64, 96, 1> storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    for (int i = 0; i < 8; ++i) {
        REQUIRE(list->addSolidRect(rect, red).has_value());
    }
    CHECK(list->commands().size() == 1);
    CHECK(list->commands()[0].indexCount == 48);
}

// ---------------------------------------------------------------------------
// reset() and determinism
// ---------------------------------------------------------------------------

TEST_CASE("reset() empties the list and the budget survives", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    REQUIRE(list->addSolidRect(rect, red).has_value());
    list->reset();

    CHECK(list->empty());
    CHECK(list->vertices().empty());
    CHECK(list->indices().empty());
    CHECK(list->commands().empty());
    CHECK(list->budget() == SmallStorage::budget());

    // And the list is usable again, from the start of its storage.
    REQUIRE(list->addSolidRect(rect, red).has_value());
    CHECK(list->vertices().size() == 4);
    CHECK(list->indices()[0] == 0);
}

TEST_CASE("reset() clears the clip, so the next frame does not inherit one", "evidence-unit") {
    SmallStorage storage;
    auto list = storage.list();
    REQUIRE(list.has_value());
    list->setClip(core::Rect{.x = 7, .y = 7, .width = 7, .height = 7});
    REQUIRE(list->addSolidRect(rect, red).has_value());
    list->reset();
    REQUIRE(list->addSolidRect(rect, red).has_value());

    CHECK(list->commands()[0].clip == core::Rect{});
}

TEST_CASE("The same primitives produce byte-identical buffers", "evidence-unit") {
    // The property #126's pixel comparison rests on: a frame is a function of its primitives and
    // nothing else - not of how much storage it was given, nor of what was there before.
    Storage<16, 24, 2> first;
    Storage<64, 96, 8> second;

    auto a = first.list();
    auto b = second.list();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    constexpr core::Rect unit{.x = 1, .y = 1, .width = 1, .height = 1};
    constexpr core::Rect clip{.x = 2, .y = 2, .width = 5, .height = 5};
    constexpr core::ColorRgba8 other{.r = 9, .g = 8, .b = 7, .a = 6};

    // The second list has drawn a different frame before, so its storage is dirty.
    REQUIRE(b->addRect(unit, red, DrawMode::CoverageR8, core::Rect{}).has_value());
    b->reset();

    for (DrawList* list : {&*a, &*b}) {
        REQUIRE(list->addSolidRect(rect, red).has_value());
        list->setClip(clip);
        REQUIRE(list->addRect(rect, other, DrawMode::CoverageR8, unit).has_value());
    }

    CHECK(std::ranges::equal(a->vertices(), b->vertices()));
    CHECK(std::ranges::equal(a->indices(), b->indices()));
    CHECK(std::ranges::equal(a->commands(), b->commands()));
}

TEST_CASE("Every DrawError has its own description", "evidence-unit") {
    constexpr std::array<DrawError, 7> all{
        DrawError::EmptyBudget,          DrawError::BudgetExceedsIndexWidth,
        DrawError::StorageTooSmall,      DrawError::VertexBudgetExceeded,
        DrawError::IndexBudgetExceeded,  DrawError::CommandBudgetExceeded,
        DrawError::DegenerateRect,
    };
    std::vector<std::string_view> seen;
    for (const DrawError error : all) {
        const std::string_view text = describe(error);
        CHECK(!text.empty());
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}
