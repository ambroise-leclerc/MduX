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
 * ## What the golden sidecar has here, and what it does not
 *
 * `goldens.json` gets its first consumer in this file, and it is a **static** one: the last scenario
 * reads the committed sidecar and cross-checks every entry against the compiled screen. That is a
 * real check - it fails if the `@safety_critical` annotation is removed, or if a golden's bounds stop
 * matching the node it names - and it is not the consumer ADR-012 describes.
 *
 * The rendered one cannot exist yet, and the reason is structural rather than an omission here.
 * Both golden nodes on this screen are deferred - a `NumericDisplay` and a `SignalTrace` - so there
 * are no pixels to check `Bounds` or `ColorHash` against, and the only node that *is* drawn is the
 * Row's synthetic `Panel`, which no golden can ever name: `collectGoldens()` skips synthetic nodes,
 * and a `Row` carries neither `requirement:` nor `position:`. So no screen in this repository can
 * have a rendered golden consumer until a golden-eligible component learns to draw, which is #17,
 * and the verifier that would consume it is #16.
 *
 * The scenario below therefore asserts the one rendered fact that is true: the region the golden
 * names is empty today. That is a tripwire rather than a goal - the day the `NumericDisplay` draws,
 * it fails and has to be replaced by the check ADR-012 actually wants.
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
import mdux.evidence.json;
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

/// The screen as a constant expression, so the storage below can be sized from the budget the
/// artifact bakes rather than from three numbers copied out of it.
constexpr medui::ScreenPackage compiled = medui::generated::screen_endoscope_monitor::package();

/// Storage sized once from the screen's own budget, as a device would size it - and sized *by* it,
/// so the two cannot drift. Hard-coding the three numbers would have left this test claiming a
/// contract it did not exercise the moment a recipe changed one.
struct Frame {
    std::array<draw::UiVertex, compiled.budget.maxVertices>    vertices{};
    std::array<draw::Index, compiled.budget.maxIndices>        indices{};
    std::array<draw::DrawCommand, compiled.budget.maxCommands> commands{};
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

TEST_CASE("The golden sidecar names this screen's safety-critical content", "pixel") {
    // The sidecar's first consumer in this tree, and a static one: it reads the committed file and
    // checks it against the compiled screen. See this file's header for why the rendered consumer
    // ADR-012 describes cannot exist yet, and what would have to change for it to.
    const medui::ScreenPackage package = screen();

    const std::filesystem::path sidecar = std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "screen" / "endoscope-monitor" / "goldens.json";
    std::ifstream               file{sidecar, std::ios::binary};
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const auto parsed = mdux::evidence::json::parse(buffer.str());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->kind() == mdux::evidence::json::Value::Kind::Array);

    // Two entries, one per rule ADR-011 fixes: the annotated node, and the positioned one.
    const std::span<const mdux::evidence::json::Value> entries = parsed->elements();
    CHECK(entries.size() == 2);

    bool sawAnnotated = false;
    for (const mdux::evidence::json::Value& entry : entries) {
        const auto nodeId = entry.require("nodeId");
        REQUIRE(nodeId.has_value());
        const auto name = (*nodeId)->asString();
        REQUIRE(name.has_value());

        // Every golden names a node this screen actually has, and pins the rectangle that node
        // actually occupies. A sidecar that drifted from the package would address content the
        // verifier could never find.
        const medui::CompiledNode* node = package.find(*name);
        REQUIRE(node != nullptr);

        const auto bounds = entry.require("bounds");
        REQUIRE(bounds.has_value());
        const auto x      = (*bounds)->require("x");
        const auto y      = (*bounds)->require("y");
        const auto width  = (*bounds)->require("width");
        const auto height = (*bounds)->require("height");
        REQUIRE(x.has_value());
        REQUIRE(y.has_value());
        REQUIRE(width.has_value());
        REQUIRE(height.has_value());
        CHECK((*x)->asInt().value_or(-1) == node->bounds.x);
        CHECK((*y)->asInt().value_or(-1) == node->bounds.y);
        CHECK((*width)->asInt().value_or(-1) == node->bounds.width);
        CHECK((*height)->asInt().value_or(-1) == node->bounds.height);

        if (*name == "insufflation-pressure") {
            sawAnnotated = true;
            // The two checks the annotation asked for, and the tint the ColorHash one would compare
            // against. Removing `@safety_critical` from the screen drops this entry entirely, which
            // is what makes this scenario a check on the authored safety marking rather than on the
            // serialiser.
            const auto checks = entry.require("cvChecks");
            REQUIRE(checks.has_value());
            const std::span<const mdux::evidence::json::Value> named = (*checks)->elements();
            REQUIRE(named.size() == 2);
            CHECK(named[0].asString().value_or("") == "Bounds");
            CHECK(named[1].asString().value_or("") == "ColorHash");

            const auto tint = entry.require("colorToken");
            REQUIRE(tint.has_value());
            CHECK((*tint)->asString().value_or("") == "Theme.Colors.ScoreDigits");
        }
    }
    CHECK(sawAnnotated);
}

TEST_CASE("The safety-critical region is empty, which is what the golden cannot yet check", "pixel") {
    // A tripwire, not a goal. `insufflation-pressure` is deferred, so its golden's Bounds and
    // ColorHash have nothing to compare against - and saying that out loud, in a test that fails the
    // day the component draws, is more honest than a comment nobody re-reads. When #17 gives a
    // NumericDisplay its geometry, this scenario must be replaced by the check ADR-012 wants.
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
    REQUIRE(medui::render(package, *list).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    const medui::CompiledNode* traced = package.find("insufflation-pressure");
    REQUIRE(traced != nullptr);

    // Sampled at the centre of the region the golden pins, which is where content would appear.
    const auto        centreX = static_cast<std::size_t>(traced->bounds.x + traced->bounds.width / 2);
    const auto        centreY = static_cast<std::size_t>(traced->bounds.y + traced->bounds.height / 2);
    const std::size_t index   = centreY * static_cast<std::size_t>(surface.width) + centreX;
    REQUIRE(index < pixels->size());
    CHECK((*pixels)[index] == background);
}
