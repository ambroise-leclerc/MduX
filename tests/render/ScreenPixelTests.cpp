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
 * The runtime draws a `Panel` and a `Label`. `EndoscopeMonitor`'s Row declares a background, so the
 * solver synthesised one, and that is what appears: a 1280x72 bar in `Theme.Colors.TopbarBackground`,
 * with the screen's title drawn over it from the committed text package (#242). Its image, its video
 * surface, its numeric readout and its waveform are visited, counted as deferred, and left undrawn,
 * because they have no geometry until a sample exists or a package this repository does not yet bake.
 *
 * Two scenarios below, deliberately not one. The first renders without a binding and is the older
 * claim unchanged - the panel lands where the compiler put it, every text node deferred. The second
 * binds the committed font and text packages and checks the glyphs. Keeping them apart is what makes
 * the unbound path a tested contract rather than a code path nobody exercises once a binding exists.
 *
 * That makes this test thin in content and complete in path, and the distinction is the point. What
 * it proves is not that MduX can draw a clinical screen; it is that a bar and a title on this display
 * came from files an author wrote, through every stage, with nothing hand-carried between them. The
 * content grows when the components learn their geometry; the path does not have to be built again.
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
import mdux.font.schema;
import mdux.shader.schema;
import mdux.text.draw;
import mdux.text.schema;
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
static_assert(compiled.validate().has_value(), "the committed screen's generated form validates");
static_assert(compiled.approvedTextPackages.size() == 1, "the committed screen approves one locale package");
static_assert(compiled.approvedTextPackages[0].locale == "en-US", "the emitted approval keeps its locale");
static_assert(compiled.approvedTextPackages[0].packageId == "endoscope-monitor-en-us", "the emitted approval keeps its package id");

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

/// Reads a committed file, or fails the scenario naming it.
[[nodiscard]] std::vector<std::byte> committed(std::string_view kind, std::string_view id, std::string_view name) {
    const std::filesystem::path path = std::filesystem::path{MDUX_REPO_ROOT} / "generated" / kind / id / name;
    std::ifstream               file{path, std::ios::binary};
    if (!file.is_open()) {
        throw std::runtime_error(std::format("cannot open {}", path.generic_string()));
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    const auto*       data = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>{data, data + text.size()};
}

[[nodiscard]] std::string_view asText(const std::vector<std::byte>& bytes) noexcept {
    return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// The three committed artifacts a device joins a locale-free screen to, held together so a
/// scenario cannot bind two of them and forget the third.
struct BoundText {
    std::vector<std::byte>  fontJson;
    std::vector<std::byte>  textJson;
    std::vector<std::byte>  runs;
    std::vector<std::byte>  atlas;
    mdux::font::FontPackage font;
    mdux::text::TextPackage text;

    /// Through `create()`, which is the only way to obtain one - and which proves the three
    /// committed artifacts describe each other before a frame reads any of them.
    [[nodiscard]] medui::TextBinding binding(const medui::ScreenPackage& screen) const {
        auto made = medui::TextBinding::create(screen, font, text, runs);
        if (!made.has_value()) {
            throw std::runtime_error(std::format("the committed artifacts were refused: {}", medui::describe(made.error())));
        }
        return *made;
    }
};

/// Loads them from `generated/`, which is the point: the pixels below come from the same bytes
/// `ctest -L evidence` compares, not from a fixture written to make this scenario pass.
[[nodiscard]] BoundText loadCommittedText() {
    BoundText bound;
    bound.fontJson = committed("font", "dejavu-ui", "package.json");
    bound.textJson = committed("text", "endoscope-monitor-en-us", "package.json");
    bound.runs     = committed("text", "endoscope-monitor-en-us", "runs.bin");
    bound.atlas    = committed("font", "dejavu-ui", "atlas.bin");

    auto font = mdux::font::FontPackage::parse(asText(bound.fontJson));
    if (!font.has_value()) {
        throw std::runtime_error("the committed font package did not parse");
    }
    auto text = mdux::text::TextPackage::parse(asText(bound.textJson));
    if (!text.has_value()) {
        throw std::runtime_error("the committed text package did not parse");
    }
    bound.font = std::move(*font);
    bound.text = std::move(*text);
    return bound;
}

/// A rectangle in pixels, or nothing.
struct InkBox {
    bool     found{false};
    core::Px left{0};
    core::Px top{0};
    core::Px right{0};   ///< exclusive
    core::Px bottom{0};  ///< exclusive
};

/**
 * @brief The ink one committed run paints, measured from the packages rather than from the frame.
 *
 * This is the *independent* half of the scenario below. The runtime derives the same quantity to
 * decide where to put the run; deriving it here from the committed bytes - and then finding the same
 * box in the rendered pixels - is what makes the assertion about the placement rule rather than a
 * restatement of the code that implements it.
 */
[[nodiscard]] InkBox inkOfRun(const BoundText& bound, std::string_view key) {
    const mdux::text::TextRun* run = nullptr;
    for (const mdux::text::TextRun& candidate : bound.text.runs) {
        if (candidate.id == key) {
            run = &candidate;
        }
    }
    if (run == nullptr) {
        return InkBox{};
    }
    // Bounds and alignment before the span exists, not after. This helper reads committed bytes,
    // and a corrupt artifact should make it return "no ink" - which fails the REQUIRE below with the
    // run named - rather than form an out-of-range span and take the process with it. Subtraction
    // rather than `byteOffset + byteLength`, which wraps.
    if (run->byteOffset > bound.runs.size() || run->byteLength > bound.runs.size() - run->byteOffset || run->byteLength % mdux::text::draw::recordSize != 0) {
        return InkBox{};
    }
    const std::span<const std::byte> records{bound.runs.data() + run->byteOffset, static_cast<std::size_t>(run->byteLength)};

    InkBox box;
    for (std::size_t offset = 0; offset < records.size(); offset += mdux::text::draw::recordSize) {
        const auto placement = mdux::text::draw::decodeRecord(records.subspan(offset, mdux::text::draw::recordSize));
        if (!placement.has_value()) {
            return InkBox{};
        }
        if (placement->packageIndex >= bound.font.glyphs.size()) {
            return InkBox{};
        }
        const mdux::font::GlyphRecord& glyph = bound.font.glyphs[placement->packageIndex];
        if (glyph.isBlank()) {
            continue;
        }
        const auto left   = static_cast<core::Px>(placement->x + glyph.bitmapOriginX);
        const auto top    = static_cast<core::Px>(placement->y - glyph.bitmapOriginY);
        const auto right  = static_cast<core::Px>(left + static_cast<core::Px>(glyph.width));
        const auto bottom = static_cast<core::Px>(top + static_cast<core::Px>(glyph.height));
        if (!box.found) {
            box = InkBox{.found = true, .left = left, .top = top, .right = right, .bottom = bottom};
            continue;
        }
        box.left   = std::min(box.left, left);
        box.top    = std::min(box.top, top);
        box.right  = std::max(box.right, right);
        box.bottom = std::max(box.bottom, bottom);
    }
    return box;
}

/// The bounding box of every pixel inside `within` that is not `ground`.
///
/// The label is drawn over the topbar panel, so "painted" means "differs from the panel's colour" -
/// which also makes the assertion insensitive to the anti-aliased coverage values themselves. What
/// is under test is where the glyphs landed, not what shade each edge pixel came out.
[[nodiscard]] InkBox paintedWithin(std::span<const core::ColorRgba8> pixels, core::Extent2D surface, const medui::NodeRect& within, core::ColorRgba8 ground) {
    InkBox box;
    for (core::Px y = static_cast<core::Px>(within.y); y < static_cast<core::Px>(within.y + within.height); ++y) {
        for (core::Px x = static_cast<core::Px>(within.x); x < static_cast<core::Px>(within.x + within.width); ++x) {
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) + static_cast<std::size_t>(x);
            if (pixels[index] == ground) {
                continue;
            }
            if (!box.found) {
                box = InkBox{.found = true, .left = x, .top = y, .right = static_cast<core::Px>(x + 1), .bottom = static_cast<core::Px>(y + 1)};
                continue;
            }
            box.left   = std::min(box.left, x);
            box.top    = std::min(box.top, y);
            box.right  = std::max(box.right, static_cast<core::Px>(x + 1));
            box.bottom = std::max(box.bottom, static_cast<core::Px>(y + 1));
        }
    }
    return box;
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
    CHECK(package.nodes.size() == 6);

    // The safety-critical node #201 asks for, reached through the function a traceability export
    // walks rather than by index.
    const medui::CompiledNode* traced = package.find("insufflation-pressure");
    REQUIRE(traced != nullptr);
    CHECK(medui::requirementOf(*traced) == "REQ-EM-001");

    // The label carries the *key*, never the words. This is ADR-011 (as amended by #203) made
    // checkable at the one place it could be violated without anything else noticing: a compiler
    // that resolved `t("STR-EM-TITLE")` into "Endoscope Monitor" here would produce a screen that
    // still validates, still renders identically today, and is wrong in every locale but one.
    const medui::CompiledNode* label = package.find("screen-title");
    REQUIRE(label != nullptr);
    const auto* text = std::get_if<medui::LabelSpec>(&label->payload);
    REQUIRE(text != nullptr);
    CHECK(text->textKey == "STR-EM-TITLE");
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
    // Five of six nodes are visited and left undrawn, and the frame says so rather than looking
    // complete. See this file's header for which, and why each.
    CHECK(recorded->deferred == 5);

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

TEST_CASE("An authored screen's label reaches pixels where the compiler measured it", "pixel") {
    // The last link the chain was missing. The screen, the font package and the text package are all
    // committed artifacts; the runtime joins them; the glyphs land on the display. Nothing in this
    // scenario is hand-carried - the bytes are the ones `ctest -L evidence` byte-compares.
    const medui::ScreenPackage package = screen();
    const core::Extent2D       surface = surfaceOf(package);
    const BoundText            bound   = loadCommittedText();

    const medui::CompiledNode* label = package.find("screen-title");
    REQUIRE(label != nullptr);

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

    // The coverage overload, with the committed atlas. A default renderer would sample a white
    // 1x1 default and every glyph would come out a filled rectangle - which would still "draw
    // text" in the loosest sense and would prove nothing about the atlas.
    auto renderer = UiRenderer::createWithCoverageAtlas(context,
                                                        mdux::shader::generated::mdux_ui::package(),
                                                        package.budget,
                                                        bound.atlas,
                                                        bound.font.atlas.width,
                                                        bound.font.atlas.height);
    REQUIRE(renderer.has_value());

    Frame frame;
    auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, package.budget);
    REQUIRE(list.has_value());

    const auto recorded = medui::render(package, *list, bound.binding(package));
    REQUIRE(recorded.has_value());

    // One fewer deferred node than the unbound frame, and the difference is the label. Asserted as
    // the count rather than as "the label was drawn" so that a future component learning to draw
    // cannot make this scenario pass for a reason it does not name.
    CHECK(recorded->deferred == 4);
    // The panel, plus one rectangle per inked glyph. "Endoscope Monitor" is 17 characters of which
    // the space paints nothing, so 16 glyphs and the panel.
    CHECK(recorded->rects == 17);

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    // The panel's colour, which is what the label is drawn over and therefore what "not painted"
    // means inside the topbar.
    constexpr core::ColorRgba8 topbar{.r = 209, .g = 214, .b = 219, .a = 255};

    const InkBox derived = inkOfRun(bound, "STR-EM-TITLE");
    REQUIRE(derived.found);
    const InkBox painted = paintedWithin(*pixels, surface, label->bounds, topbar);
    REQUIRE(painted.found);

    // The placement rule, stated as an assertion: the ink box's top-left corner is the node's
    // top-left corner. This is the whole of what Screen.cppm decides, and the reason it decides it -
    // that #195 measured this box against this rectangle - is only true if these two agree.
    CHECK(painted.left == label->bounds.x);
    CHECK(painted.top == label->bounds.y);

    // ...and the extent is the one measured from the committed packages, so the glyphs are the run's
    // rather than some subset that happened to land in the right corner.
    CHECK(painted.right - painted.left == derived.right - derived.left);
    CHECK(painted.bottom - painted.top == derived.bottom - derived.top);

    // The build-time promise, now observable: the text fits the box it was measured against.
    CHECK(painted.right <= label->bounds.x + label->bounds.width);
    CHECK(painted.bottom <= label->bounds.y + label->bounds.height);
}
