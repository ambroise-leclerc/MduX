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
 * ## What is on screen
 *
 * The runtime draws a `Panel`, an authenticated `Image`, a `Label`, and the two fields #255 added. `EndoscopeMonitor`'s Row
 * declares a background, so the solver synthesised one, and that is what appears first: a 1280x72
 * bar in `Theme.Colors.TopbarBackground`, with the screen's title drawn over it from the committed
 * text package (#242). Below it the `NumericDisplay` and the `SignalTrace` paint the rectangles they
 * reserve, in the tokens their author gave them. Its video surface is still visited, counted as
 * deferred, and left undrawn because no test supplies a stream - and so is its status indicator,
 * which no test here binds a state to.
 *
 * Two authored-screen scenarios below, deliberately not one. The first renders without bindings and is the older
 * claim unchanged - the panel and the fields land where the compiler put them, every text node
 * deferred. The second binds the committed font and text packages and checks the glyphs. Keeping
 * them apart is what makes the unbound path a tested contract rather than a code path nobody
 * exercises once bindings exist. The bound scenario also compares every rendered image pixel to
 * its committed RGBA sidecar, so a colour-space mismatch cannot be accepted as a new expectation.
 *
 * What this test proves is not that MduX can draw a clinical screen; it is that a bar, a title and
 * two reserved fields on this display came from files an author wrote, through every stage, with
 * nothing hand-carried between them. The content grows as each component learns what to show inside
 * its field; the path does not have to be built again.
 *
 * ## What the golden sidecar has here
 *
 * `goldens.json` has two consumers in this file. The **static** one reads the committed sidecar and
 * cross-checks every entry against the compiled screen: it fails if the `@safety_critical`
 * annotation is removed, or if a golden's bounds stop matching the node it names.
 *
 * The **rendered** one is the consumer ADR-012 describes, and until #255 it could not exist. Both
 * golden nodes on this screen - a `NumericDisplay` and a `SignalTrace` - were deferred by the
 * runtime, so there were no pixels to check `Bounds` or `ColorHash` against, and the only node that
 * *was* drawn was the Row's synthetic `Panel`, which no golden can ever name: `collectGoldens()`
 * skips synthetic nodes, and a `Row` carries neither `requirement:` nor `position:`. The scenario
 * that stood in for it asserted the one rendered fact that was true - that the region the golden
 * named was empty - and said in its own comment that it was a tripwire which had to fail the day the
 * component drew. It did; this is its replacement.
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
import mdux.image.schema;
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
static_assert(compiled.approvedImagePackages.size() == 1, "the committed screen approves one image package");
static_assert(compiled.approvedImagePackages[0].packageId == "brand-mark", "the emitted image approval keeps its package id");

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
        auto made = medui::TextBinding::create(screen, font, text, textJson, runs);
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

struct BoundImage {
    std::vector<std::byte>    imageJson;
    std::vector<std::byte>    pixels;
    mdux::image::ImagePackage image;

    [[nodiscard]] medui::ImageBinding binding(const medui::ScreenPackage& screen) const {
        auto made = medui::ImageBinding::create(screen, image, imageJson, pixels);
        if (!made.has_value()) {
            throw std::runtime_error(std::format("the committed image was refused: {}", medui::describe(made.error())));
        }
        return *made;
    }
};

[[nodiscard]] BoundImage loadCommittedImage() {
    BoundImage bound;
    bound.imageJson = committed("image", "brand-mark", "package.json");
    bound.pixels    = committed("image", "brand-mark", "pixels.rgba");
    auto image      = mdux::image::ImagePackage::parse(asText(bound.imageJson));
    if (!image.has_value()) {
        throw std::runtime_error("the committed image package did not parse");
    }
    bound.image = std::move(*image);
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
    CHECK(package.nodes.size() == 7);

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
    // The Row's synthetic panel, and the two fields #255 taught the runtime to paint.
    CHECK(recorded->rects == 3);
    // Four of seven nodes are visited and left undrawn, and the frame says so rather than looking
    // complete. See this file's header for which, and why each - the status indicator joins them
    // because no test here binds a state, and an indicator with none is in no state to paint.
    CHECK(recorded->deferred == 4);

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    // Each rectangle comes from the package and each colour is the quantisation the runtime applies
    // - `Theme.Colors.TopbarBackground` is {0.82, 0.84, 0.86, 1.0} linear becoming {209, 214, 219,
    // 255}, and `ScoreDigits` and `Nominal` are both {0.13, 0.72, 0.42, 1.0} becoming {33, 184, 107,
    // 255}. Writing the results here rather than resolving the tokens makes this a check on the
    // whole conversion rather than on the geometry alone; an expectation that called the function
    // the runtime calls would agree with it whatever that function returned.
    //
    // The two fields share a value today, so this scenario would pass with them swapped. What tells
    // them apart is `verify.screen.endoscope-monitor`, where each is compared against the token its
    // own node names.
    ExpectedImage expected{surface, background};
    for (const auto& [id, tint] : std::initializer_list<std::pair<std::string_view, core::ColorRgba8>>{
             {    "topbar-background", core::ColorRgba8{.r = 209, .g = 214, .b = 219, .a = 255}},
             {"insufflation-pressure",  core::ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255}},
             {          "ecg-lead-ii",  core::ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255}}
    }) {
        const medui::CompiledNode* node = package.find(id);
        REQUIRE(node != nullptr);
        expected.paint(core::Rect{.x = node->bounds.x, .y = node->bounds.y, .width = node->bounds.width, .height = node->bounds.height}, tint);
    }

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

TEST_CASE("Every golden region is painted where and in the tint the sidecar pins", "pixel") {
    // The rendered golden consumer this file's header used to say could not exist. It reads the
    // committed sidecar and compares the frame against the numbers *that file* carries - not
    // against the package the runtime drew from - so a sidecar that drifted from the screen fails
    // here even though both halves would still be internally consistent.
    //
    // It replaces the tripwire that asserted the region was empty. That scenario existed to fail the
    // day a golden-eligible component learned to draw, and #255 is that day.
    //
    // This is the same claim `mdux-verify-ui` makes and deliberately not the same mechanism: the
    // tool resolves expectations through `mdux.verify` and renders through a headless device it
    // creates itself. Two paths to one fact is the point - a runtime change that satisfied the
    // driver's own expectation builder would still have to satisfy the bytes on disk here.
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

    const std::filesystem::path sidecar = std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "screen" / "endoscope-monitor" / "goldens.json";
    std::ifstream               file{sidecar, std::ios::binary};
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto parsed = mdux::evidence::json::parse(buffer.str());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->kind() == mdux::evidence::json::Value::Kind::Array);

    const std::span<const mdux::evidence::json::Value> entries = parsed->elements();
    REQUIRE(!entries.empty());

    for (const mdux::evidence::json::Value& entry : entries) {
        const auto nodeId = entry.require("nodeId");
        REQUIRE(nodeId.has_value());
        const std::string name{(*nodeId)->asString().value_or("")};

        const auto boundsValue = entry.require("bounds");
        REQUIRE(boundsValue.has_value());
        const auto member = [&](std::string_view key) -> std::int64_t {
            const auto found = (*boundsValue)->require(key);
            REQUIRE(found.has_value());
            // `asInt()` for a negative coordinate, `asUInt()` for a non-negative one: the writer
            // emits whichever is exact, so a reader that took only one would reject half the
            // sidecars it is meant to read.
            if (const auto signedValue = (*found)->asInt(); signedValue.has_value()) {
                return *signedValue;
            }
            const auto unsignedValue = (*found)->asUInt();
            REQUIRE(unsignedValue.has_value());
            return static_cast<std::int64_t>(*unsignedValue);
        };
        const std::int64_t left   = member("x");
        const std::int64_t top    = member("y");
        const std::int64_t width  = member("width");
        const std::int64_t height = member("height");

        const auto tokenValue = entry.require("colorToken");
        REQUIRE(tokenValue.has_value());
        const auto resolved = medui::resolveColorToken((*tokenValue)->asString().value_or(""));
        REQUIRE(resolved.has_value());
        const core::ColorRgba8 tint = medui::quantise(*resolved);

        // Every pixel of the declared rectangle carries the declared tint. Deliberately not paired
        // with a scan of the ring just outside it: this screen packs `insufflation-pressure` and
        // `ecg-lead-ii` edge to edge and both tokens resolve to the same value today, so such a scan
        // reports the neighbour's pixels as this node's spill. That is the attribution limit
        // `mdux/verify/Verify.cppm` states for `goldenBounds()`, met here for the same reason. The
        // "not one pixel larger" half is the first scenario's job, and it does it better: it paints
        // the whole expected surface and compares every pixel of it.
        std::size_t wrongInside = 0;
        for (std::int64_t y = top; y < top + height; ++y) {
            for (std::int64_t x = left; x < left + width; ++x) {
                const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) + static_cast<std::size_t>(x);
                REQUIRE(index < pixels->size());
                wrongInside += static_cast<std::size_t>((*pixels)[index] != tint);
            }
        }
        CHECK_MESSAGE(wrongInside == 0, std::format("{}: {} pixels inside the golden rectangle are not its tint", name, wrongInside));
    }
}

TEST_CASE("An authored screen's label and image reach the pixels the compiler approved", "pixel") {
    // The last link the chain was missing. The screen, the font package and the text package are all
    // committed artifacts; the runtime joins them; the glyphs land on the display. Nothing in this
    // scenario is hand-carried - the bytes are the ones `ctest -L evidence` byte-compares.
    const medui::ScreenPackage package = screen();
    const core::Extent2D       surface = surfaceOf(package);
    const BoundText            bound   = loadCommittedText();
    const BoundImage           image   = loadCommittedImage();

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
    auto renderer = UiRenderer::createWithAtlases(context,
                                                  mdux::shader::generated::mdux_ui::package(),
                                                  package.budget,
                                                  bound.atlas,
                                                  bound.font.atlas.width,
                                                  bound.font.atlas.height,
                                                  image.pixels,
                                                  image.image.width,
                                                  image.image.height);
    REQUIRE(renderer.has_value());

    Frame frame;
    auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, package.budget);
    REQUIRE(list.has_value());

    const auto recorded = medui::render(package, *list, bound.binding(package), image.binding(package));
    REQUIRE(recorded.has_value());

    // Two fewer deferred nodes than the unbound frame, and the difference is the label and the
    // image. Asserted as the count rather than as "the label was drawn" so that a future component
    // learning to draw cannot make this scenario pass for a reason it does not name. The status
    // indicator is one of the two that remain: this scenario binds text and an image, not a state.
    CHECK(recorded->deferred == 2);
    // The panel and the two fields, plus one rectangle per inked glyph. "Endoscope Monitor" is 17
    // characters of which the space paints nothing, so 16 glyphs and three filled rectangles.
    CHECK(recorded->rects == 20);

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

    const medui::CompiledNode* imageNode = package.find("brand-mark");
    REQUIRE(imageNode != nullptr);
    std::size_t mismatchedImagePixels = 0;
    for (std::int32_t y = 0; y < imageNode->bounds.height; ++y) {
        for (std::int32_t x = 0; x < imageNode->bounds.width; ++x) {
            const std::size_t source =
                (static_cast<std::size_t>(y) * image.image.width + static_cast<std::size_t>(x)) * 4;
            const core::ColorRgba8 expected{std::to_integer<std::uint8_t>(image.pixels[source]),
                                            std::to_integer<std::uint8_t>(image.pixels[source + 1]),
                                            std::to_integer<std::uint8_t>(image.pixels[source + 2]),
                                            std::to_integer<std::uint8_t>(image.pixels[source + 3])};
            const std::size_t      targetIndex = static_cast<std::size_t>(imageNode->bounds.y + y) * static_cast<std::size_t>(surface.width)
                                            + static_cast<std::size_t>(imageNode->bounds.x + x);
            const core::ColorRgba8 actual = (*pixels)[targetIndex];
            mismatchedImagePixels += static_cast<std::size_t>(actual != expected);
        }
    }
    CHECK_MESSAGE(mismatchedImagePixels == 0,
                  std::format("{} baked image pixels differ from the committed RGBA sidecar", mismatchedImagePixels));
}

TEST_CASE("An authored screen's bound status state reaches the pixels", "pixel") {
    // The claim #259 made and this scenario is what makes true: the state on screen is drawn from
    // the committed text package, in that state's own tint, at the corner the compiler measured its
    // box against. Everything here is a committed artifact - the screen, the font, the text package
    // and its sidecar - and the only thing this test supplies is a *position* in a closed list.
    //
    // Kept apart from the label/image scenario for that scenario's own reason: an unbound indicator
    // is a tested contract (it is what the first scenario renders), and folding a state into the
    // scenario next door would leave the unbound path unexercised the moment a binding exists.
    const medui::ScreenPackage package = screen();
    const core::Extent2D       surface = surfaceOf(package);
    const BoundText            bound   = loadCommittedText();
    const BoundImage           image   = loadCommittedImage();

    const medui::CompiledNode* indicator = package.find("classifier-state");
    REQUIRE(indicator != nullptr);
    const auto* spec = std::get_if<medui::StatusIndicatorSpec>(&indicator->payload);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->stateKeys.size() == 4);

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

    auto renderer = UiRenderer::createWithAtlases(context,
                                                  mdux::shader::generated::mdux_ui::package(),
                                                  package.budget,
                                                  bound.atlas,
                                                  bound.font.atlas.width,
                                                  bound.font.atlas.height,
                                                  image.pixels,
                                                  image.image.width,
                                                  image.image.height);
    REQUIRE(renderer.has_value());

    /// One frame of the committed screen with `state` bound to the indicator, read back as pixels.
    ///
    /// A lambda rather than two copies, because the point of this scenario is the *difference*
    /// between two states and a difference needs both halves produced the same way.
    const auto frameFor = [&](std::uint32_t state) {
        const std::array<medui::StatusSlot, 1> slots{
            medui::StatusSlot{.nodeId = "classifier-state", .state = state}
        };
        auto status = medui::StatusBinding::create(package, slots);
        REQUIRE(status.has_value());

        Frame frame;
        auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, package.budget);
        REQUIRE(list.has_value());

        const auto recorded = medui::render(package, *list, bound.binding(package), image.binding(package), {}, {}, *status);
        REQUIRE(recorded.has_value());
        CHECK(recorded->states == 1);
        // Only the video surface is left: the panel, the image, the label, the two fields and now
        // the indicator all draw. Asserted as the count for the label scenario's reason - a future
        // component learning to draw must not be able to make this pass for a reason it does not
        // name.
        CHECK(recorded->deferred == 1);

        RecordContext recording{.renderer = &*renderer, .list = &*list};
        auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
        REQUIRE(pixels.has_value());
        // Copied out, and that is not incidental: the span `renderAndRead()` returns is the target's
        // own staging buffer and is valid only until the next call. Holding two of them would leave
        // this scenario comparing one frame against itself and reporting the two states as
        // indistinguishable - which is the failure it exists to detect.
        return std::vector<core::ColorRgba8>{pixels->begin(), pixels->end()};
    };

    // `Class 2`, whose tint is `Theme.Colors.Alert`.
    const std::vector<core::ColorRgba8> alarmed = frameFor(2);

    // The field's colour, read out of the frame at a corner the word cannot reach rather than
    // predicted: the field is one tint at `boundFieldCoverage` composited over the topbar panel, and
    // an expectation carrying that blend would be testing this file's arithmetic against the
    // renderer's. What is under test here is where the *glyphs* landed, and for that the field is
    // simply the ground they are drawn on - which is exactly what `paintedWithin()` needs.
    const auto fieldPixel = [&](const std::vector<core::ColorRgba8>& pixels) {
        const auto x = static_cast<std::size_t>(indicator->bounds.x + indicator->bounds.width - 1);
        const auto y = static_cast<std::size_t>(indicator->bounds.y + indicator->bounds.height - 1);
        return pixels[y * static_cast<std::size_t>(surface.width) + x];
    };

    const core::ColorRgba8 field = fieldPixel(alarmed);

    // The field is painted, and painted over the topbar rather than left as it: an indicator that
    // drew only its word would leave the panel's colour here and every assertion below would still
    // hold.
    constexpr core::ColorRgba8 topbar{.r = 209, .g = 214, .b = 219, .a = 255};
    CHECK(field != topbar);

    // The state's word, from the committed text package, where the compiler measured it.
    const InkBox derived = inkOfRun(bound, spec->stateKeys[2]);
    REQUIRE(derived.found);
    const InkBox painted = paintedWithin(alarmed, surface, indicator->bounds, field);
    REQUIRE(painted.found);

    CHECK(painted.left == indicator->bounds.x);
    CHECK(painted.top == indicator->bounds.y);
    CHECK(painted.right - painted.left == derived.right - derived.left);
    CHECK(painted.bottom - painted.top == derived.bottom - derived.top);
    CHECK(painted.right <= indicator->bounds.x + indicator->bounds.width);
    CHECK(painted.bottom <= indicator->bounds.y + indicator->bounds.height);

    // And the state is what decides the tint, not the node. `Class 0` and `Class 2` are the same
    // length, so their ink boxes are identical and only the colour tells the two frames apart -
    // which is the property `StatusBinding` refuses an untinted indicator to protect.
    const std::vector<core::ColorRgba8> nominal = frameFor(0);
    CHECK(fieldPixel(nominal) != field);

    // And the *word* is the state's own, not the first state's. Every state on this screen reads
    // `Class N`, so the two words differ in one glyph and nothing else - and whether that is visible
    // to the extent checks above is an accident of the font: `0` and `2` happen to have different
    // ink widths at this size, so today they would catch it.
    //
    // This does not depend on that accident. The ink *mask* - which pixels differ from their own
    // frame's field, taken per frame so the colour difference cancels out - separates two words of
    // identical extent as long as their glyphs do not cover the same pixels, which is what makes
    // them different glyphs. A mask that matched would mean one word was drawn for both states.
    const auto inkMask = [&](const std::vector<core::ColorRgba8>& pixels, core::ColorRgba8 ground) {
        std::vector<bool> mask;
        mask.reserve(static_cast<std::size_t>(indicator->bounds.width) * static_cast<std::size_t>(indicator->bounds.height));
        for (std::int32_t y = 0; y < indicator->bounds.height; ++y) {
            for (std::int32_t x = 0; x < indicator->bounds.width; ++x) {
                const auto index = static_cast<std::size_t>(indicator->bounds.y + y) * static_cast<std::size_t>(surface.width)
                                   + static_cast<std::size_t>(indicator->bounds.x + x);
                mask.push_back(pixels[index] != ground);
            }
        }
        return mask;
    };

    CHECK(inkMask(alarmed, field) != inkMask(nominal, fieldPixel(nominal)));
}
