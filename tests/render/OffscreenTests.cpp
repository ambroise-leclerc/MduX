/**
 * @brief Tests for the headless offscreen target: creation, clearing, rendering, readback.
 *
 * This is the first suite in the repository that puts anything on a GPU. What it proves is
 * narrow but load-bearing for everything after it: a `DrawList` built by governed code, handed
 * to the adapter renderer, becomes pixels whose values can be asserted - with no window, no
 * swapchain and no display server anywhere in the path.
 *
 * When no Vulkan device is present the suite's `main` exits 77 and CTest reports *Skipped*, which
 * is deliberately a different outcome from passing. See HeadlessDevice.hpp.
 */
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.schema;
import mdux.shader.generated.mdux_ui;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "HeadlessDevice.hpp"

namespace {

using namespace mdux::render;
namespace core = mdux::core;
namespace draw = mdux::draw;
using mdux::test::sharedDevice;

constexpr core::Extent2D surface{.width = 64, .height = 48};
constexpr core::ColorRgba8 black{.r = 0, .g = 0, .b = 0, .a = 255};
constexpr core::ColorRgba8 opaqueRed{.r = 255, .g = 0, .b = 0, .a = 255};

[[nodiscard]] mdux::core::Result<OffscreenTarget, OffscreenError> makeTarget(
    core::Extent2D extent = surface) noexcept {
    const auto& gpu = sharedDevice();
    return OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), extent,
                                   gpu.queueFamilyIndex());
}

/// Storage plus a DrawList, so a test can build a frame in one line.
struct Frame {
    std::array<draw::UiVertex, 64> vertices{};
    std::array<draw::Index, 96> indices{};
    std::array<draw::DrawCommand, 8> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8};
    }

    [[nodiscard]] mdux::core::Result<draw::DrawList, draw::DrawError> list() noexcept {
        return draw::DrawList::create(vertices, indices, commands, budget());
    }
};

/// Context for the record callback: the renderer and the frame it should emit.
struct RecordContext {
    UiRenderer* renderer;
    const draw::DrawList* list;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto* recording = static_cast<RecordContext*>(context);
    static_cast<void>(recording->renderer->record(commandBuffer, *recording->list));
}

}  // namespace

// ---------------------------------------------------------------------------
// Creation and teardown
// ---------------------------------------------------------------------------

TEST_CASE("A headless target is created without a surface", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    CHECK(target->renderPass() != VK_NULL_HANDLE);
    CHECK(target->extent() == surface);
}

TEST_CASE("Creation rejects a null device, physical device or extent", "pixel") {
    const auto& gpu = sharedDevice();

    auto noDevice = OffscreenTarget::create(VK_NULL_HANDLE, gpu.physicalDevice(), surface, 0);
    REQUIRE(!noDevice.has_value());
    CHECK(noDevice.error() == OffscreenError::NullDevice);

    auto noPhysical = OffscreenTarget::create(gpu.device(), VK_NULL_HANDLE, surface, 0);
    REQUIRE(!noPhysical.has_value());
    CHECK(noPhysical.error() == OffscreenError::NullPhysicalDevice);

    constexpr core::Extent2D empty{.width = 0, .height = 48};
    auto zero = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), empty,
                                        gpu.queueFamilyIndex());
    REQUIRE(!zero.has_value());
    CHECK(zero.error() == OffscreenError::EmptyExtent);
}

TEST_CASE("An implausibly large extent is refused rather than allocated", "pixel") {
    // A typo in an extent should be an error, not a multi-gigabyte device allocation.
    const auto& gpu = sharedDevice();
    constexpr core::Extent2D huge{.width = 40000, .height = 40000};
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), huge,
                                          gpu.queueFamilyIndex());
    REQUIRE(!target.has_value());
    CHECK(target.error() == OffscreenError::ExtentTooLarge);
}

TEST_CASE("Targets can be created and destroyed repeatedly", "pixel") {
    // Teardown correctness, checked the only way that is honest without a leak checker: if
    // destroy() left device objects behind, a loop like this would exhaust something.
    for (int i = 0; i < 8; ++i) {
        auto target = makeTarget();
        REQUIRE(target.has_value());
    }
    CHECK(true);
}

TEST_CASE("A moved-from target releases nothing twice", "pixel") {
    auto first = makeTarget();
    REQUIRE(first.has_value());
    const VkRenderPass borrowed = first->renderPass();

    OffscreenTarget second = std::move(*first);
    CHECK(second.renderPass() == borrowed);
    // The moved-from object must now own nothing, so its destructor at scope exit is a no-op
    // rather than a second free of the handles `second` holds.
    CHECK(first->renderPass() == VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Clearing
// ---------------------------------------------------------------------------

TEST_CASE("An empty frame reads back as the clear colour everywhere", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());

    auto pixels = target->renderAndRead(sharedDevice().queue(), opaqueRed, nullptr, nullptr);
    REQUIRE(pixels.has_value());
    REQUIRE(pixels->size() ==
            static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height));

    // Every pixel, not a sample: a clear that missed a region is exactly the defect a sampled
    // check would step over.
    std::size_t mismatches = 0;
    for (const core::ColorRgba8 pixel : *pixels) {
        if (pixel != opaqueRed) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

TEST_CASE("The clear colour round-trips exactly, with no gamma applied", "pixel") {
    // A UNORM target and a 0..1 clear value must come back as the byte that went in. If the
    // format were sRGB this would be off by the transfer function, and a colour assertion in #126
    // would fail for a reason that has nothing to do with what was drawn.
    auto target = makeTarget();
    REQUIRE(target.has_value());

    constexpr core::ColorRgba8 awkward{.r = 3, .g = 128, .b = 251, .a = 255};
    auto pixels = target->renderAndRead(sharedDevice().queue(), awkward, nullptr, nullptr);
    REQUIRE(pixels.has_value());
    REQUIRE(!pixels->empty());
    CHECK(pixels->front() == awkward);
}

TEST_CASE("renderAndRead rejects a null queue", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    auto pixels = target->renderAndRead(VK_NULL_HANDLE, black, nullptr, nullptr);
    REQUIRE(!pixels.has_value());
    CHECK(pixels.error() == OffscreenError::NullQueue);
}

TEST_CASE("pixelAt is bounds-checked and agrees with the returned span", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    auto pixels = target->renderAndRead(sharedDevice().queue(), opaqueRed, nullptr, nullptr);
    REQUIRE(pixels.has_value());

    CHECK(target->pixelAt(0, 0) == opaqueRed);
    CHECK(target->pixelAt(surface.width - 1, surface.height - 1) == opaqueRed);
    // Out of range reports nothing rather than comparing whatever was next in memory.
    CHECK(!target->pixelAt(surface.width, 0).has_value());
    CHECK(!target->pixelAt(0, surface.height).has_value());
    CHECK(!target->pixelAt(-1, 0).has_value());

    // The row-major indexing pixelAt assumes must match the packing the copy produced.
    constexpr core::Px x = 5;
    constexpr core::Px y = 7;
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) +
        static_cast<std::size_t>(x);
    CHECK(target->pixelAt(x, y) == (*pixels)[index]);
}

// ---------------------------------------------------------------------------
// Rendering a governed DrawList
// ---------------------------------------------------------------------------

TEST_CASE("A solid rectangle from a governed DrawList lands where it was asked to", "pixel") {
    // The whole chain, end to end: mdux.draw builds a frame with no knowledge of Vulkan, the
    // adapter renderer turns it into commands, and the result is pixels. This is the first time
    // in the repository that library code puts something on screen.
    auto target = makeTarget();
    REQUIRE(target.has_value());
    const auto& gpu = sharedDevice();

    VulkanRenderContext context;
    context.device = gpu.device();
    context.physicalDevice = gpu.physicalDevice();
    context.renderPass = target->renderPass();
    context.queue = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport = surface;

    auto renderer = UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(),
                                       Frame::budget());
    REQUIRE(renderer.has_value());

    Frame frame;
    auto list = frame.list();
    REQUIRE(list.has_value());
    constexpr core::Rect box{.x = 10, .y = 8, .width = 20, .height = 16};
    REQUIRE(list->addSolidRect(box, opaqueRed).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), black, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    // Inside the rectangle.
    CHECK(target->pixelAt(box.x + 1, box.y + 1) == opaqueRed);
    CHECK(target->pixelAt(box.right() - 2, box.bottom() - 2) == opaqueRed);
    CHECK(target->pixelAt(box.x + box.width / 2, box.y + box.height / 2) == opaqueRed);

    // Outside it, on all four sides - so a rectangle drawn at the wrong offset, or one that
    // filled the whole surface, both fail here.
    CHECK(target->pixelAt(box.x - 2, box.y + 1) == black);
    CHECK(target->pixelAt(box.right() + 1, box.y + 1) == black);
    CHECK(target->pixelAt(box.x + 1, box.y - 2) == black);
    CHECK(target->pixelAt(box.x + 1, box.bottom() + 1) == black);
    CHECK(target->pixelAt(0, 0) == black);
}

TEST_CASE("Two rectangles are both drawn, in the colours they were given", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    const auto& gpu = sharedDevice();

    VulkanRenderContext context;
    context.device = gpu.device();
    context.physicalDevice = gpu.physicalDevice();
    context.renderPass = target->renderPass();
    context.queue = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport = surface;

    auto renderer = UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(),
                                       Frame::budget());
    REQUIRE(renderer.has_value());

    Frame frame;
    auto list = frame.list();
    REQUIRE(list.has_value());
    constexpr core::Rect left{.x = 4, .y = 4, .width = 10, .height = 10};
    constexpr core::Rect right{.x = 40, .y = 30, .width = 12, .height = 12};
    constexpr core::ColorRgba8 green{.r = 0, .g = 255, .b = 0, .a = 255};
    REQUIRE(list->addSolidRect(left, opaqueRed).has_value());
    REQUIRE(list->addSolidRect(right, green).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), black, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    CHECK(target->pixelAt(left.x + 2, left.y + 2) == opaqueRed);
    CHECK(target->pixelAt(right.x + 2, right.y + 2) == green);
    // Between them, still background - so the two are separate primitives rather than one
    // bounding box covering both.
    CHECK(target->pixelAt(25, 20) == black);
}

TEST_CASE("A frame larger than the renderer's budget is refused, not truncated", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    const auto& gpu = sharedDevice();

    VulkanRenderContext context;
    context.device = gpu.device();
    context.physicalDevice = gpu.physicalDevice();
    context.renderPass = target->renderPass();
    context.queue = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport = surface;

    // A renderer built for one rectangle, handed a list holding two.
    constexpr draw::DrawBudget tight{.maxVertices = 4, .maxIndices = 6, .maxCommands = 1};
    auto renderer =
        UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(), tight);
    REQUIRE(renderer.has_value());

    Frame frame;
    auto list = frame.list();
    REQUIRE(list.has_value());
    constexpr core::Rect box{.x = 1, .y = 1, .width = 4, .height = 4};
    REQUIRE(list->addSolidRect(box, opaqueRed).has_value());
    REQUIRE(list->addSolidRect(box, opaqueRed).has_value());

    // Refused before anything is copied: writing the larger frame into the smaller mapped buffers
    // would be an overrun, and a silently truncated frame is a wrong frame.
    // A fabricated command buffer is safe here: the budget check runs before record() touches it.
    auto* const neverUsed = reinterpret_cast<VkCommandBuffer>(std::uintptr_t{0x1000});
    auto recorded = renderer->record(neverUsed, *list);
    REQUIRE(!recorded.has_value());
    CHECK(recorded.error() == RenderError::FrameExceedsBudget);
}

TEST_CASE("record() rejects a null command buffer", "pixel") {
    auto target = makeTarget();
    REQUIRE(target.has_value());
    const auto& gpu = sharedDevice();

    VulkanRenderContext context;
    context.device = gpu.device();
    context.physicalDevice = gpu.physicalDevice();
    context.renderPass = target->renderPass();
    context.queue = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport = surface;

    auto renderer = UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(),
                                       Frame::budget());
    REQUIRE(renderer.has_value());

    Frame frame;
    auto list = frame.list();
    REQUIRE(list.has_value());

    auto recorded = renderer->record(VK_NULL_HANDLE, *list);
    REQUIRE(!recorded.has_value());
    CHECK(recorded.error() == RenderError::NullCommandBuffer);
}

TEST_CASE("Rendering the same frame twice produces identical pixels", "pixel") {
    // Determinism, which #126's comparison against expected values depends on entirely.
    auto target = makeTarget();
    REQUIRE(target.has_value());
    const auto& gpu = sharedDevice();

    VulkanRenderContext context;
    context.device = gpu.device();
    context.physicalDevice = gpu.physicalDevice();
    context.renderPass = target->renderPass();
    context.queue = gpu.queue();
    context.queueFamilyIndex = gpu.queueFamilyIndex();
    context.viewport = surface;

    auto renderer = UiRenderer::create(context, mdux::shader::generated::mdux_ui::package(),
                                       Frame::budget());
    REQUIRE(renderer.has_value());

    Frame frame;
    auto list = frame.list();
    REQUIRE(list.has_value());
    constexpr core::Rect box{.x = 6, .y = 6, .width = 15, .height = 11};
    REQUIRE(list->addSolidRect(box, opaqueRed).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};

    auto first = target->renderAndRead(gpu.queue(), black, recordFrame, &recording);
    REQUIRE(first.has_value());
    const std::vector<core::ColorRgba8> captured{first->begin(), first->end()};

    auto second = target->renderAndRead(gpu.queue(), black, recordFrame, &recording);
    REQUIRE(second.has_value());

    CHECK(std::ranges::equal(captured, *second));
}

TEST_CASE("Every OffscreenError has its own description", "pixel") {
    constexpr std::array<OffscreenError, 19> all{
        OffscreenError::NullDevice,
        OffscreenError::NullPhysicalDevice,
        OffscreenError::NullQueue,
        OffscreenError::EmptyExtent,
        OffscreenError::ExtentTooLarge,
        OffscreenError::ImageCreationFailed,
        OffscreenError::NoSuitableMemoryType,
        OffscreenError::MemoryAllocationFailed,
        OffscreenError::ImageViewCreationFailed,
        OffscreenError::RenderPassCreationFailed,
        OffscreenError::FramebufferCreationFailed,
        OffscreenError::BufferCreationFailed,
        OffscreenError::MemoryMapFailed,
        OffscreenError::CommandPoolCreationFailed,
        OffscreenError::CommandBufferAllocationFailed,
        OffscreenError::BeginCommandBufferFailed,
        OffscreenError::EndCommandBufferFailed,
        OffscreenError::SubmitFailed,
        OffscreenError::WaitFailed,
    };
    std::vector<std::string_view> seen;
    for (const OffscreenError error : all) {
        const std::string_view text = describe(error);
        CHECK(!text.empty());
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}
