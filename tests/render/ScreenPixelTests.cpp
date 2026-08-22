/**
 * @file ScreenPixelTests.cpp
 * @brief The first pixel drawn from an authored screen (issue #201).
 *
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Every link in the chain this epic set out to build, exercised end to end in one test: an authored
 * `.medui` file becomes a committed, byte-compared artifact (#198), the artifact becomes `constexpr`
 * C++ this translation unit links (#197), the governed runtime turns that into draw commands without
 * allocating (#199), and the Vulkan renderer draws them into an offscreen target that is read back
 * and compared pixel by pixel (#125, #126).
 *
 * ## What is on screen, and why it is one rectangle
 *
 * The runtime draws a `Panel`. `EndoscopeMonitor`'s Row declares a background, so the solver
 * synthesised one, and that is what appears: a 1280x72 bar in `Theme.Colors.TopbarBackground`. Its
 * image, its video surface, its numeric readout and its waveform are visited, counted as deferred,
 * and left undrawn - because a compiled screen carries a `textKey` rather than glyphs and no text
 * package is baked in this tree, and because live-data components have no geometry until a sample
 * exists.
 *
 * That makes this test thin in content and complete in path, and the distinction is the point. What
 * it proves is not that MduX can draw a clinical screen; it is that a rectangle on this display came
 * from a file an author wrote, through every stage, with nothing hand-carried between them. The
 * content grows when the text package lands and the remaining components learn their geometry; the
 * path does not have to be built again.
 *
 * ## Compared against the compiled screen, not against a copy of it
 *
 * The expectation is painted from the package's own node bounds and the governed colour table -
 * `screen.find("topbar-background")->bounds` - rather than from four numbers written here. A test
 * carrying its own copy of the rectangle would pass while the compiler moved it, which is the one
 * regression this test exists to catch.
 */
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.schema;
import mdux.shader.generated.mdux_ui;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "HeadlessDevice.hpp"
#include "PixelExpectation.hpp"

namespace {

using namespace mdux::render;
namespace core  = mdux::core;
namespace draw  = mdux::draw;
namespace medui = mdux::medui;
using mdux::test::compare;
using mdux::test::ExpectedImage;
using mdux::test::sharedDevice;

/// The screen as generated code holds it: read-only data, validated at compile time by the
/// `static_assert` the emitter wrote beside it.
[[nodiscard]] medui::ScreenPackage screen() noexcept {
    return medui::generated::screen_endoscope_monitor::package();
}

/// The offscreen surface is the screen's own, so a node lands where the compiler said it would.
/// Taken from the package rather than written out: a test that fixed its own extent would keep
/// passing while the screen was drawn for a different panel.
[[nodiscard]] core::Extent2D surfaceOf(const medui::ScreenPackage& package) noexcept {
    return core::Extent2D{.width = package.surfaceWidth, .height = package.surfaceHeight};
}

constexpr core::ColorRgba8 background{.r = 0, .g = 0, .b = 0, .a = 255};

/// Storage sized once from the screen's own budget, as a device would size it.
struct Frame {
    std::array<draw::UiVertex, 4096>  vertices{};
    std::array<draw::Index, 6144>     indices{};
    std::array<draw::DrawCommand, 64> commands{};
};

struct RecordContext {
    UiRenderer*           renderer;
    const draw::DrawList* list;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto* recording = static_cast<RecordContext*>(context);
    static_cast<void>(recording->renderer->record(commandBuffer, *recording->list));
}

}  // namespace

TEST_CASE("The compiled screen is the one the compiler produced", "pixel") {
    // Checked before any GPU is involved, so a mismatch here reports the compiler rather than the
    // renderer. This is also the assertion that would fail if the committed artifact and the
    // generated source ever stopped agreeing.
    const medui::ScreenPackage package = screen();

    CHECK(package.id == "endoscope-monitor");
    CHECK(package.validate().has_value());
    CHECK(package.surfaceWidth == 1280);
    CHECK(package.surfaceHeight == 720);
    CHECK(package.nodes.size() == 5);

    // The safety-critical node #201 asks for, reached through the function a traceability export
    // walks rather than by index.
    const medui::CompiledNode* traced = package.find("insufflation-pressure");
    REQUIRE(traced != nullptr);
    CHECK(medui::requirementOf(*traced) == "REQ-EM-001");
}

TEST_CASE("An authored screen draws its panel where the compiler put it", "pixel") {
    const medui::ScreenPackage package = screen();
    const core::Extent2D       surface = surfaceOf(package);

    const auto& gpu    = sharedDevice();
    auto        target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface, gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

    VulkanRenderContext context;
    context.device           = gpu.device();
    context.physicalDevice   = gpu.physicalDevice();
    context.renderPass       = target->renderPass();
    context.queue            = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport         = surface;

    auto renderer = UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(), package.budget);
    REQUIRE(renderer.has_value());

    Frame frame;
    auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, package.budget);
    REQUIRE(list.has_value());

    // The governed runtime, doing the only work between a compiled screen and a frame.
    const auto recorded = medui::render(package, *list);
    REQUIRE(recorded.has_value());
    CHECK(recorded->rects == 1);
    // Four of five nodes are visited and left undrawn, and the frame says so rather than looking
    // complete. See this file's header for which, and why each.
    CHECK(recorded->deferred == 4);

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    // Painted from the package and the governed table, so the expectation moves when the screen
    // does. The colour is the quantisation the runtime applies - {0.82, 0.84, 0.86, 1.0} linear
    // becoming {209, 214, 219, 255} - which makes this a check on the whole conversion rather than
    // on the geometry alone.
    const medui::CompiledNode* panel = package.find("topbar-background");
    REQUIRE(panel != nullptr);

    ExpectedImage expected{surface, background};
    expected.paint(core::Rect{.x = panel->bounds.x, .y = panel->bounds.y, .width = panel->bounds.width, .height = panel->bounds.height},
                   core::ColorRgba8{.r = 209, .g = 214, .b = 219, .a = 255});

    const auto diff = compare(expected, *pixels);
    CHECK_MESSAGE(diff.matched(), diff.message);
}
