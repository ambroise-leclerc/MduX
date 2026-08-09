/**
 * @file PixelTests.cpp
 * @brief The project's first pixel test: a rendered frame compared against an expectation,
 *        every pixel, with actionable output when it differs.
 *
 * #125 proved a rectangle lands where it was asked to by sampling a handful of points. This goes
 * further in the way that matters: it compares **every** pixel, so stray geometry in a corner
 * nobody thought to sample fails here rather than surviving to a device.
 *
 * ## The comparator is tested too
 *
 * Half of this file exercises `compare()` itself, against frames that were never rendered. That
 * is not padding. A pixel test whose comparator cannot detect a one-pixel change is a test that
 * passes whatever the renderer does, and the only way to know it can is to hand it a frame that
 * is wrong by exactly one pixel and watch it say so - with the coordinate and both values, which
 * is the difference between a diagnostic and a notification.
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
#include "PixelExpectation.hpp"

namespace {

using namespace mdux::render;
namespace core = mdux::core;
namespace draw = mdux::draw;
using mdux::test::compare;
using mdux::test::ExpectedImage;
using mdux::test::sharedDevice;

constexpr core::Extent2D surface{.width = 48, .height = 32};

/// The index of (x, y) in a readback row-major buffer of `surface`.
///
/// Derived rather than written out: a readback is a flat vector, so the stride is the one number
/// that silently converts a change of `surface` into a test that pokes the wrong pixel and still
/// passes - it would just be asserting about somewhere else.
[[nodiscard]] constexpr std::size_t indexOf(core::Px x, core::Px y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) +
           static_cast<std::size_t>(x);
}

/// How many pixels a full-surface comparison reports on, for the message assertions below.
constexpr std::size_t surfacePixels =
    static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height);
constexpr core::ColorRgba8 background{.r = 0, .g = 0, .b = 0, .a = 255};
constexpr core::ColorRgba8 red{.r = 255, .g = 0, .b = 0, .a = 255};
constexpr core::ColorRgba8 blue{.r = 0, .g = 0, .b = 255, .a = 255};

struct Frame {
    std::array<draw::UiVertex, 32> vertices{};
    std::array<draw::Index, 48> indices{};
    std::array<draw::DrawCommand, 4> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 32, .maxIndices = 48, .maxCommands = 4};
    }
};

struct RecordContext {
    UiRenderer* renderer;
    const draw::DrawList* list;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto* recording = static_cast<RecordContext*>(context);
    static_cast<void>(recording->renderer->record(commandBuffer, *recording->list));
}

/// A synthetic frame matching `expected` exactly, for testing the comparator without a GPU.
[[nodiscard]] std::vector<core::ColorRgba8> renderExpected(const ExpectedImage& expected) {
    std::vector<core::ColorRgba8> pixels;
    const core::Extent2D extent = expected.extent();
    pixels.reserve(static_cast<std::size_t>(extent.width) *
                   static_cast<std::size_t>(extent.height));
    for (core::Px y = 0; y < extent.height; ++y) {
        for (core::Px x = 0; x < extent.width; ++x) {
            pixels.push_back(expected.at(x, y));
        }
    }
    return pixels;
}

}  // namespace

// ---------------------------------------------------------------------------
// The comparator, checked before anything relies on it
// ---------------------------------------------------------------------------

TEST_CASE("An exact match reports no differences", "pixel") {
    ExpectedImage expected{surface, background};
    expected.paint(core::Rect{.x = 4, .y = 4, .width = 8, .height = 6}, red);

    const auto diff = compare(expected, renderExpected(expected));
    CHECK(diff.matched());
    CHECK(diff.differing == 0);
    CHECK(diff.message.empty());
    CHECK(diff.total == surfacePixels);
}

TEST_CASE("One wrong pixel is detected, and reported with its coordinate", "pixel") {
    // The case that decides whether any of this is worth running. A comparator that misses a
    // single-pixel change makes every test in this file pass regardless of what was drawn.
    ExpectedImage expected{surface, background};
    expected.paint(core::Rect{.x = 4, .y = 4, .width = 8, .height = 6}, red);

    std::vector<core::ColorRgba8> actual = renderExpected(expected);
    constexpr core::Px x = 17;
    constexpr core::Px y = 9;
    actual[indexOf(x, y)] = blue;

    const auto diff = compare(expected, actual);
    CHECK(!diff.matched());
    CHECK(diff.differing == 1);
    CHECK(diff.message.find("(17, 9)") != std::string::npos);
    CHECK(diff.message.find("expected #000000ff") != std::string::npos);
    CHECK(diff.message.find("actual #0000ffff") != std::string::npos);
    CHECK(diff.message.find(std::format("1 of {} pixels differ", surfacePixels)) !=
          std::string::npos);
}

TEST_CASE("A one-channel difference is detected", "pixel") {
    // Off by one in a single channel: the smallest wrong answer a renderer can give, and the one
    // a tolerance-based comparison would wave through.
    ExpectedImage expected{surface, background};
    expected.paint(core::Rect{.x = 2, .y = 2, .width = 4, .height = 4}, red);

    std::vector<core::ColorRgba8> actual = renderExpected(expected);
    constexpr std::size_t index = indexOf(3, 3);
    actual[index] = core::ColorRgba8{.r = 254, .g = 0, .b = 0, .a = 255};

    const auto diff = compare(expected, actual);
    CHECK(!diff.matched());
    CHECK(diff.differing == 1);
    CHECK(diff.message.find("expected #ff0000ff") != std::string::npos);
    CHECK(diff.message.find("actual #fe0000ff") != std::string::npos);
}

TEST_CASE("An alpha-only difference is detected", "pixel") {
    ExpectedImage expected{surface, background};
    std::vector<core::ColorRgba8> actual = renderExpected(expected);
    actual[0] = core::ColorRgba8{.r = 0, .g = 0, .b = 0, .a = 254};

    const auto diff = compare(expected, actual);
    CHECK(diff.differing == 1);
    CHECK(diff.message.find("(0, 0)") != std::string::npos);
}

TEST_CASE("A wholly wrong frame reports the count and truncates the list", "pixel") {
    // Thousands of identical lines would bury the count, which is the useful part.
    ExpectedImage expected{surface, background};
    const std::vector<core::ColorRgba8> actual(surfacePixels, blue);

    const auto diff = compare(expected, actual);
    CHECK(diff.differing == surfacePixels);
    CHECK(diff.message.find(std::format("{0} of {0} pixels differ", surfacePixels)) !=
          std::string::npos);
    CHECK(diff.message.find(std::format("and {} more", surfacePixels - 8)) !=
          std::string::npos);
}

TEST_CASE("A frame of the wrong size is reported as such, not as every pixel", "pixel") {
    ExpectedImage expected{surface, background};
    const std::vector<core::ColorRgba8> actual(10, background);

    const auto diff = compare(expected, actual);
    CHECK(!diff.matched());
    CHECK(diff.message.find("size mismatch") != std::string::npos);
    CHECK(diff.message.find(std::format("{}x{}", surface.width, surface.height)) !=
          std::string::npos);
}

TEST_CASE("Later rectangles paint over earlier ones", "pixel") {
    // The expectation follows the same painter's model the renderer does, so an overlap is
    // described the way it was drawn rather than as a transformation of it.
    ExpectedImage expected{surface, background};
    expected.paint(core::Rect{.x = 0, .y = 0, .width = 10, .height = 10}, red);
    expected.paint(core::Rect{.x = 5, .y = 5, .width = 10, .height = 10}, blue);

    CHECK(expected.at(2, 2) == red);
    CHECK(expected.at(7, 7) == blue);
    CHECK(expected.at(12, 12) == blue);
    CHECK(expected.at(20, 20) == background);
}

// ---------------------------------------------------------------------------
// The pixel test proper
// ---------------------------------------------------------------------------

TEST_CASE("A rendered frame matches its expectation exactly, pixel for pixel", "pixel") {
    const auto& gpu = sharedDevice();
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface,
                                          gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

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
    auto list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands,
                                       Frame::budget());
    REQUIRE(list.has_value());

    constexpr core::Rect first{.x = 4, .y = 4, .width = 12, .height = 9};
    constexpr core::Rect second{.x = 28, .y = 18, .width = 15, .height = 10};
    REQUIRE(list->addSolidRect(first, red).has_value());
    REQUIRE(list->addSolidRect(second, blue).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    ExpectedImage expected{surface, background};
    expected.paint(first, red);
    expected.paint(second, blue);

    const auto diff = compare(expected, *pixels);
    CHECK_MESSAGE(diff.matched(), diff.message);
}

TEST_CASE("A frame moved by one pixel no longer matches", "pixel") {
    // The sensitivity claim, demonstrated rather than asserted: the same scene drawn one pixel
    // across must fail against the original expectation. If it did not, the comparison would be
    // measuring nothing and every pixel test in this file would be theatre.
    const auto& gpu = sharedDevice();
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface,
                                          gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

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
    auto list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands,
                                       Frame::budget());
    REQUIRE(list.has_value());

    constexpr core::Rect asExpected{.x = 10, .y = 10, .width = 8, .height = 8};
    constexpr core::Rect shifted{.x = 11, .y = 10, .width = 8, .height = 8};
    REQUIRE(list->addSolidRect(shifted, red).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    ExpectedImage expected{surface, background};
    expected.paint(asExpected, red);

    const auto diff = compare(expected, *pixels);
    CHECK(!diff.matched());
    // A one-pixel shift of an 8x8 rectangle changes two columns of eight: the one it left and the
    // one it moved into.
    CHECK(diff.differing == 16);
    CHECK(diff.message.find("(10, 10)") != std::string::npos);
}

TEST_CASE("A frame drawn in the wrong colour no longer matches", "pixel") {
    const auto& gpu = sharedDevice();
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface,
                                          gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

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
    auto list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands,
                                       Frame::budget());
    REQUIRE(list.has_value());

    constexpr core::Rect box{.x = 6, .y = 6, .width = 6, .height = 6};
    // One step off in a single channel - the smallest wrong answer the renderer can give.
    constexpr core::ColorRgba8 almostRed{.r = 254, .g = 0, .b = 0, .a = 255};
    REQUIRE(list->addSolidRect(box, almostRed).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    ExpectedImage expected{surface, background};
    expected.paint(box, red);

    const auto diff = compare(expected, *pixels);
    CHECK(!diff.matched());
    CHECK(diff.differing == 36);
    CHECK(diff.message.find("expected #ff0000ff") != std::string::npos);
    CHECK(diff.message.find("actual #fe0000ff") != std::string::npos);
}

TEST_CASE("The vertex colour reaches the pixel unmodified", "pixel") {
    // Every channel at an awkward value, so a swizzle, a premultiply or a gamma step anywhere in
    // the path shows up. A test using pure red would pass through all three.
    const auto& gpu = sharedDevice();
    auto target = OffscreenTarget::create(gpu.device(), gpu.physicalDevice(), surface,
                                          gpu.queueFamilyIndex());
    REQUIRE(target.has_value());

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
    auto list = draw::DrawList::create(frame.vertices, frame.indices, frame.commands,
                                       Frame::budget());
    REQUIRE(list.has_value());

    constexpr core::Rect box{.x = 8, .y = 8, .width = 10, .height = 10};
    constexpr core::ColorRgba8 awkward{.r = 17, .g = 129, .b = 240, .a = 255};
    REQUIRE(list->addSolidRect(box, awkward).has_value());

    RecordContext recording{.renderer = &*renderer, .list = &*list};
    auto pixels = target->renderAndRead(gpu.queue(), background, recordFrame, &recording);
    REQUIRE(pixels.has_value());

    ExpectedImage expected{surface, background};
    expected.paint(box, awkward);

    const auto diff = compare(expected, *pixels);
    CHECK_MESSAGE(diff.matched(), diff.message);
}
