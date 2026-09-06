/**
 * @file TextPixelTests.cpp
 * @brief Rendered-truth tests for the coverage draw path (issue #162).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * The last link in the text pipeline, checked the only way that means anything: render it and look
 * at the pixels.
 *
 * ## Why a synthetic atlas rather than the committed font
 *
 * The expectations here are *derived*, not recorded. The atlas is two 4x4 blocks - one at coverage
 * 255, one at 128 - so every output pixel follows from the blend equation and can be written down
 * by hand:
 *
 *   background is opaque black, text is opaque white, blending is src-alpha over one-minus-src-alpha
 *   result = white * (1.0 * coverage) + black * (1 - coverage)
 *          = 255 * (coverage / 255)
 *
 * so coverage 255 renders 255, coverage 128 renders 128, coverage 0 renders the background. The
 * sampler is `VK_FILTER_NEAREST` and the quad is placed 1:1, so a pixel centre lands on a texel
 * centre and no filtering blurs the step.
 *
 * Rendering DejaVu instead would be more end-to-end and much weaker as a test: the expectation
 * would be a golden nobody can check by reading it, and #162 asks specifically that no golden be
 * silently rewritten. A test whose expected values are derivable cannot be "fixed" by re-recording
 * it, because the derivation would have to change too.
 *
 * The committed font is still exercised end to end - by `evidence.font.dejavu-ui`, which compares
 * its bytes, and by `font_spec`, which parses it. What is missing from neither is a check that the
 * *renderer* draws what the package describes, and that is what this file adds.
 */

#include <vulkan/vulkan.h>

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.schema;
import mdux.shader.generated.mdux_ui;
import mdux.test;
import mdux.text.draw;

#include "../framework/MduXTest.hpp"
#include "../framework/RunRecords.hpp"
#include "HeadlessDevice.hpp"
#include "PixelExpectation.hpp"

namespace {

using namespace mdux::render;
namespace core     = mdux::core;
namespace draw     = mdux::draw;
namespace font     = mdux::font;
namespace textdraw = mdux::text::draw;
using mdux::test::compare;
using mdux::test::ExpectedImage;
using mdux::test::sharedDevice;

constexpr core::Extent2D surface{.width = 48, .height = 32};

constexpr core::ColorRgba8 background{.r = 0, .g = 0, .b = 0, .a = 255};
constexpr core::ColorRgba8 white{.r = 255, .g = 255, .b = 255, .a = 255};

/// The colour a fully covered texel produces, and the colour a half-covered one does. Written as
/// the blend result rather than as constants so the derivation stays visible.
constexpr core::ColorRgba8 fullCoverage{.r = 255, .g = 255, .b = 255, .a = 255};
constexpr core::ColorRgba8 halfCoverage{.r = 128, .g = 128, .b = 128, .a = 255};

constexpr std::uint32_t atlasEdge  = 8;
constexpr std::uint32_t blockSize  = 4;
constexpr std::uint8_t  halfValue  = 128;

/// An 8x8 R8 sheet holding two 4x4 blocks: solid at (0,0), half-coverage at (4,0). The rest is
/// zero, so a quad that sampled the wrong slot would render background and fail loudly.
[[nodiscard]] std::vector<std::byte> syntheticAtlas() {
    std::vector<std::byte> sheet(static_cast<std::size_t>(atlasEdge) * atlasEdge, std::byte{0});
    for (std::uint32_t y = 0; y < blockSize; ++y) {
        for (std::uint32_t x = 0; x < blockSize; ++x) {
            sheet[static_cast<std::size_t>(y) * atlasEdge + x]             = std::byte{255};
            sheet[static_cast<std::size_t>(y) * atlasEdge + blockSize + x] = std::byte{halfValue};
        }
    }
    return sheet;
}

/// A package describing that sheet. Passes `validate()` - which is the point of building it here
/// rather than hand-writing JSON: the renderer is fed something the schema would accept.
[[nodiscard]] font::FontPackage syntheticPackage() {
    font::FontPackage package;
    package.id         = "pixel-fixture";
    package.unitsPerEm = 2048;
    package.pixelSize  = 4;
    package.locales    = {"en-US"};

    package.atlas.path             = "atlas.bin";
    package.atlas.width            = atlasEdge;
    package.atlas.height           = atlasEdge;
    package.atlas.byteLength       = static_cast<std::uint64_t>(atlasEdge) * atlasEdge;
    package.atlas.sha256           = std::string(64, 'a');
    package.atlas.occupancyPercent = 50;

    // 'A' is the solid block, 'B' the half one. bitmapOriginY is the height, so each glyph sits
    // with its bottom on the baseline - the ordinary case, and the one where an inverted origin
    // would be most visible.
    package.glyphs = {font::GlyphRecord{.codePoint       = U'A',
                                        .glyphIndex      = 1,
                                        .advanceWidth    = 1024,
                                        .leftSideBearing = 0,
                                        .x               = 0,
                                        .y               = 0,
                                        .width           = blockSize,
                                        .height          = blockSize,
                                        .bitmapOriginX   = 0,
                                        .bitmapOriginY   = static_cast<std::int32_t>(blockSize)},
                      font::GlyphRecord{.codePoint       = U'B',
                                        .glyphIndex      = 2,
                                        .advanceWidth    = 1024,
                                        .leftSideBearing = 0,
                                        .x               = blockSize,
                                        .y               = 0,
                                        .width           = blockSize,
                                        .height          = blockSize,
                                        .bitmapOriginX   = 0,
                                        .bitmapOriginY   = static_cast<std::int32_t>(blockSize)}};
    package.restrictedCharset = {font::CharsetRange{.first = U'A', .last = U'B'}};
    return package;
}

/// Two v1 records: glyph 0 at the run origin, glyph 1 ten pixels along. Written byte by byte in
/// little-endian rather than memcpy'd from a struct, because that is the format the sidecar
/// commits and a struct copy would inherit the host's padding and byte order.
[[nodiscard]] std::vector<std::byte> runRecords() {
    std::vector<std::byte> records;
    mdux::spec::appendRunRecord(records, 0, 0, 0);
    mdux::spec::appendRunRecord(records, 1, 10, 0);
    return records;
}

// Where the run is placed, and therefore where the ink must land. Derived once so the expectation
// and the assertion cannot drift apart.
constexpr core::Px originX   = 8;
constexpr core::Px baselineY = 12;
constexpr core::Rect solidRect{.x = originX,
                               .y = baselineY - static_cast<core::Px>(blockSize),
                               .width  = static_cast<core::Px>(blockSize),
                               .height = static_cast<core::Px>(blockSize)};
constexpr core::Rect halfRect{.x = originX + 10,
                              .y = baselineY - static_cast<core::Px>(blockSize),
                              .width  = static_cast<core::Px>(blockSize),
                              .height = static_cast<core::Px>(blockSize)};

struct Frame {
    std::array<draw::UiVertex, 32> vertices{};
    std::array<draw::Index, 48>    indices{};
    std::array<draw::DrawCommand, 4> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 32, .maxIndices = 48, .maxCommands = 4};
    }
};

struct RecordContext {
    UiRenderer*           renderer;
    const draw::DrawList* list;
};

/// Like `RecordContext`, but keeps what `record()` returned. The callback runs inside
/// `renderAndRead`, so a refusal has nowhere else to go - discarding it is what the ordinary
/// `recordFrame` does, and is why a test wanting the error needs its own callback.
struct Outcome {
    UiRenderer*                            renderer;
    const draw::DrawList*                  list;
    mdux::core::ResultVoid<RenderError>    result;
};

void captureRecord(VkCommandBuffer commandBuffer, void* context) {
    auto* outcome    = static_cast<Outcome*>(context);
    outcome->result = outcome->renderer->record(commandBuffer, *outcome->list);
}

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto* recording = static_cast<RecordContext*>(context);
    static_cast<void>(recording->renderer->record(commandBuffer, *recording->list));
}

/// A rendered frame, or the step that stopped it from being one.
///
/// Not `optional`: five different failures reach this - target, renderer, list, run and readback -
/// and collapsing them all to "no pixels" means a broken atlas upload and a rejected glyph run
/// report the same thing. The binary already skips wholesale when there is no device, so a failure
/// here is always a real one and deserves to say which.
using RenderedRun = std::expected<std::vector<core::ColorRgba8>, std::string>;

/// The failure text, or empty when there was none. REQUIRE_MESSAGE takes its message by value, so
/// calling `.error()` directly at the assertion would read the error of a `RenderedRun` that holds
/// pixels - undefined, and on the passing path.
[[nodiscard]] std::string whyNot(const RenderedRun& run) {
    return run.has_value() ? std::string{} : run.error();
}

/// Renders one run. Shared by every case below so they differ only in what they draw and expect.
[[nodiscard]] RenderedRun renderRun(const font::FontPackage& package, std::span<const std::byte> records,
                                    core::Px penX, core::Px penY) {
    const auto& gpu    = sharedDevice();
    auto        target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface, gpu.queueFamilyIndex());
    if (!target.has_value()) {
        return std::unexpected(std::format("OffscreenTarget::create failed: {}", describe(target.error())));
    }

    VulkanRenderContext context;
    context.device           = gpu.device();
    context.physicalDevice   = gpu.physicalDevice();
    context.renderPass       = target->renderPass();
    context.queue            = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport         = surface;

    const auto sheet = syntheticAtlas();
    auto       renderer =
        UiRenderer::createWithCoverageAtlas(context, mdux::shader::generated::mdux_ui::package(), Frame::budget(),
                                            sheet, atlasEdge, atlasEdge);
    if (!renderer.has_value()) {
        return std::unexpected(
            std::format("createWithCoverageAtlas failed: {}", describe(renderer.error())));
    }

    Frame frame;
    auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, Frame::budget());
    if (!list.has_value()) {
        return std::unexpected(std::format("DrawList::create failed: {}", draw::describe(list.error())));
    }
    if (auto recorded = textdraw::recordRun(*list, package, records, penX, penY, white); !recorded.has_value()) {
        return std::unexpected(std::format("recordRun failed: {}", textdraw::describe(recorded.error())));
    }

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto          pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    if (!pixels.has_value()) {
        return std::unexpected(std::format("renderAndRead failed: {}", describe(pixels.error())));
    }
    // Copied, not returned as a span: the readback lives in `target`, which is local to this
    // function. Returning the view would hand every caller a dangling one.
    return std::vector<core::ColorRgba8>{pixels->begin(), pixels->end()};
}

}  // namespace

TEST_CASE("A baked glyph run renders at its recorded bounds, with its recorded coverage", "pixel") {
    const auto package = syntheticPackage();
    REQUIRE(package.validate().has_value());  // the fixture is one the schema would accept

    const auto records = runRecords();
    const auto pixels  = renderRun(package, records, originX, baselineY);
    REQUIRE_MESSAGE(pixels.has_value(), whyNot(pixels));

    ExpectedImage expected{surface, background};
    expected.paint(solidRect, fullCoverage);
    expected.paint(halfRect, halfCoverage);

    const auto diff = compare(expected, *pixels);
    CHECK_MESSAGE(diff.matched(), diff.message);
}

TEST_CASE("A run moved by one pixel no longer matches", "pixel") {
    // The sensitivity claim #162 asks for, demonstrated rather than asserted. If this passed, the
    // positive case above would be proving nothing about position.
    const auto package = syntheticPackage();
    const auto records = runRecords();
    const auto pixels  = renderRun(package, records, originX + 1, baselineY);
    REQUIRE_MESSAGE(pixels.has_value(), whyNot(pixels));

    ExpectedImage expected{surface, background};
    expected.paint(solidRect, fullCoverage);
    expected.paint(halfRect, halfCoverage);

    const auto diff = compare(expected, *pixels);
    CHECK(!diff.matched());
    // #162 asks that a deliberate change fail "with coordinates and expected/actual values", so
    // the message is asserted rather than assumed. The coordinate is derivable: shifting the run
    // one pixel right vacates the solid rect's leftmost column, so (8, 8) - its top-left - is the
    // first pixel that differs, expecting white and getting the background.
    CHECK(diff.message.find("(8, 8)") != std::string::npos);
    CHECK(diff.message.find("expected") != std::string::npos);
    CHECK(diff.message.find("actual") != std::string::npos);
}

TEST_CASE("A run drawn from the wrong atlas slot no longer matches", "pixel") {
    // Both glyphs pointed at the solid block: the half-coverage rectangle would render white.
    // This is what catches a uv normalisation error, which is the arithmetic most likely to be
    // wrong in the coverage path and the one that looks plausible when it is.
    auto package             = syntheticPackage();
    package.glyphs[1].x      = 0;  // 'B' now samples 'A''s slot

    const auto records = runRecords();
    const auto pixels  = renderRun(package, records, originX, baselineY);
    REQUIRE_MESSAGE(pixels.has_value(), whyNot(pixels));

    ExpectedImage expected{surface, background};
    expected.paint(solidRect, fullCoverage);
    expected.paint(halfRect, halfCoverage);

    const auto diff = compare(expected, *pixels);
    CHECK(!diff.matched());
    CHECK(diff.differing == static_cast<std::size_t>(blockSize) * blockSize);
}

TEST_CASE("A blank glyph occupies no pixels but does not fail the run", "pixel") {
    // The space. It has an advance and no coverage, so it must be skipped rather than recorded as
    // a degenerate rectangle - which DrawList would refuse, failing the whole run.
    auto package = syntheticPackage();
    package.glyphs[1].width  = 0;
    package.glyphs[1].height = 0;

    const auto records = runRecords();
    const auto pixels  = renderRun(package, records, originX, baselineY);
    REQUIRE_MESSAGE(pixels.has_value(), whyNot(pixels));

    ExpectedImage expected{surface, background};
    expected.paint(solidRect, fullCoverage);  // only the solid glyph; the blank paints nothing

    const auto diff = compare(expected, *pixels);
    CHECK_MESSAGE(diff.matched(), diff.message);
}

TEST_CASE("A coverage renderer refuses a frame that samples RGBA", "pixel") {
    // The neutral RGBA texture keeps the fixed descriptor set valid; it is not authored image
    // content. Refusal prevents an unbound Image from becoming a plausible white rectangle.
    const auto& gpu = sharedDevice();
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface, gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

    VulkanRenderContext context;
    context.device           = gpu.device();
    context.physicalDevice   = gpu.physicalDevice();
    context.renderPass       = target->renderPass();
    context.queue            = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport         = surface;

    const auto sheet    = syntheticAtlas();
    auto       renderer = UiRenderer::createWithCoverageAtlas(
        context, mdux::shader::generated::mdux_ui::package(), Frame::budget(), sheet, atlasEdge, atlasEdge);
    REQUIRE(renderer.has_value());

    Frame frame;
    auto  list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands, Frame::budget());
    REQUIRE(list.has_value());

    constexpr core::Rect rect{.x = 0, .y = 0, .width = 4, .height = 4};
    constexpr draw::UvRect unit{.u0 = 0.0F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F};
    REQUIRE(list->addRect(rect, white, draw::DrawMode::SampledRgba, unit).has_value());

    // Through the real recording path rather than a synthesised command buffer, so what is being
    // checked is the call a frame actually makes. The refusal happens before any vkCmd, so the
    // pass still completes - it just draws nothing.
    Outcome    outcome{.renderer = &*renderer, .list = &*list, .result = {}};
    const auto rejected = target->renderAndRead(gpu.queue(), background, captureRecord, &outcome);
    REQUIRE_MESSAGE(rejected.has_value(), rejected.has_value() ? std::string{} : std::string{describe(rejected.error())});
    REQUIRE(!outcome.result.has_value());
    CHECK(outcome.result.error() == RenderError::SampledRgbaWithCoverageAtlas);

    // And the same renderer still accepts an ordinary coverage frame, so what the guard rejects is
    // the mode rather than the renderer.
    const auto package = syntheticPackage();
    list->reset();
    REQUIRE(textdraw::recordRun(*list, package, runRecords(), originX, baselineY, white).has_value());
    const auto accepted = target->renderAndRead(gpu.queue(), background, captureRecord, &outcome);
    REQUIRE_MESSAGE(accepted.has_value(), accepted.has_value() ? std::string{} : std::string{describe(accepted.error())});
    CHECK_MESSAGE(outcome.result.has_value(),
                  outcome.result.has_value() ? std::string{} : std::string{describe(outcome.result.error())});
}
