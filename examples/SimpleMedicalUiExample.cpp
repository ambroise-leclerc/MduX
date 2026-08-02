/**
 * @brief What MduX builds a frame out of, with no Vulkan device and no window.
 *
 * @compliance IEC 62304 Class B - Medical Device Example
 *
 * This example used to construct a `MedicalUiConfig`, a `MedicalUiContent` holding an HTML string,
 * and a `UiFileWatcher` - none of which did anything. `MedicalUiRenderer::render()` recorded no
 * Vulkan commands at all, and nothing ever parsed the HTML. Issue #127 deleted that path.
 *
 * What replaces it is worth showing precisely because it is unglamorous: a `DrawList` is a plain
 * description of a frame, built over storage the caller owns, with no Vulkan anywhere in it. That
 * is the property everything else rests on - it is why a frame can be built here with no device,
 * checked in a test with no GPU, and rendered offscreen in CI with no display server.
 *
 * For the rendering half see examples/VulkanSCTriangleExample.cpp, which owns a device and a
 * window, and tests/render/ for a frame going all the way to pixels.
 */

import std;
import mdux;
import mdux.core.units;
import mdux.draw;

namespace {

namespace core = mdux::core;
namespace draw = mdux::draw;

/// Storage for one screen's frame. On a device this is sized from a budget the `.medui` compiler
/// computed (issue #15) and lives in static storage; here it is a local, which is the same thing
/// with a shorter lifetime. Either way `DrawList` never allocates.
struct ScreenStorage {
    std::array<draw::UiVertex, 64> vertices{};
    std::array<draw::Index, 96> indices{};
    std::array<draw::DrawCommand, 8> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8};
    }
};

struct Element {
    core::Rect bounds;
    core::ColorRgba8 color;
    std::string_view name;
};

void describe(const draw::DrawList& list) {
    std::println("  vertices: {:>3} / {}", list.vertices().size(), list.budget().maxVertices);
    std::println("  indices:  {:>3} / {}", list.indices().size(), list.budget().maxIndices);
    std::println("  commands: {:>3} / {}", list.commands().size(), list.budget().maxCommands);
}

}  // namespace

int main() {
    std::println("MduX {} - {}", mdux::Version::getString(), mdux::Compliance::standards);
    std::println("Safety class: {}", mdux::Compliance::safetyClass);
    std::println("");

    if (!mdux::initialize()) {
        std::println(std::cerr, "mdux::initialize() failed");
        return 1;
    }

    ScreenStorage storage;
    auto list = draw::DrawList::create(storage.vertices, storage.indices, storage.commands,
                                       ScreenStorage::budget());
    if (!list.has_value()) {
        std::println(std::cerr, "could not build a draw list: {}", draw::describe(list.error()));
        mdux::shutdown();
        return 1;
    }

    // A panel, a header bar and a status block: three rectangles, in pixels, top-left origin.
    constexpr core::ColorRgba8 panel{.r = 32, .g = 38, .b = 45, .a = 255};
    constexpr core::ColorRgba8 header{.r = 11, .g = 110, .b = 119, .a = 255};
    constexpr core::ColorRgba8 ok{.r = 60, .g = 107, .b = 44, .a = 255};

    const std::array<Element, 3> screen{
        Element{.bounds = {.x = 0, .y = 0, .width = 800, .height = 600},
                .color = panel,
                .name = "panel"},
        Element{.bounds = {.x = 0, .y = 0, .width = 800, .height = 48},
                .color = header,
                .name = "header"},
        Element{.bounds = {.x = 16, .y = 64, .width = 120, .height = 24},
                .color = ok,
                .name = "status"}};

    for (const Element& element : screen) {
        if (auto added = list->addSolidRect(element.bounds, element.color); !added.has_value()) {
            // A frame that does not fit its budget is refused, not truncated. On a device that
            // refusal is the point: a UI which silently grew its buffers would have a per-frame
            // cost nobody bounded, and the first symptom is a missed deadline.
            std::println(std::cerr, "'{}' did not fit: {}", element.name,
                         draw::describe(added.error()));
            mdux::shutdown();
            return 1;
        }
    }

    std::println("Built a frame with no Vulkan device and no window:");
    describe(*list);
    std::println("");
    std::println("The same list is what mdux::render::UiRenderer records into a command buffer.");
    std::println("See examples/VulkanSCTriangleExample.cpp for the device half.");

    mdux::shutdown();
    return 0;
}
