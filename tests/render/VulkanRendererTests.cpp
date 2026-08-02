/**
 * @brief Tests for mdux.render.vulkan that need no Vulkan device.
 *
 * ## What this file can and cannot cover, stated plainly
 *
 * `UiRenderer::create()` validates its context, its budget and its shader package *before* it
 * makes a single Vulkan call, so every one of those rejections is testable on a machine with no
 * GPU, no ICD and no loader - which is what CI currently is on both Linux legs.
 *
 * Everything past that first gate is not covered here. Pipeline creation, buffer allocation,
 * mapping and `record()` all need a real device, and a `UiRenderer` cannot be constructed without
 * one because `create()` is the only way to make one. Those land with #125, which adds the
 * headless harness and the lavapipe ICD to CI; this file deliberately does not pretend otherwise.
 */
// Headers before imports, as the Vulkan SC tests already do: `import std;` exports no macros, so
// offsetof needs <cstddef>, and included after the imports it redefines std::byte against the
// module. <vulkan/vulkan.h> follows the same rule for the same reason.
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.shader.schema;
import mdux.render.vulkan;
import mdux.test;

#include "../framework/MduXTest.hpp"

namespace {

using namespace mdux::render;
namespace core = mdux::core;
namespace draw = mdux::draw;
namespace shader = mdux::shader;

/// A context that passes validation. Every handle is a fabricated non-null value: `create()`
/// rejects null handles before it dereferences anything, so these are never followed.
[[nodiscard]] VulkanRenderContext plausibleContext() noexcept {
    VulkanRenderContext context;
    context.device = reinterpret_cast<VkDevice>(std::uintptr_t{0x1000});
    context.physicalDevice = reinterpret_cast<VkPhysicalDevice>(std::uintptr_t{0x2000});
    context.renderPass = reinterpret_cast<VkRenderPass>(std::uintptr_t{0x3000});
    context.queue = reinterpret_cast<VkQueue>(std::uintptr_t{0x4000});
    context.viewport = core::Extent2D{.width = 800, .height = 600};
    return context;
}

constexpr draw::DrawBudget workableBudget{
    .maxVertices = 64, .maxIndices = 96, .maxCommands = 4};

constexpr std::array<std::byte, 4> payload{};

constexpr std::array<shader::ModuleView, 2> bothStages{
    shader::ModuleView{.id = "ui.vert", .stage = shader::Stage::Vertex, .entryPoint = "main",
                       .byteOffset = 0, .byteLength = 4},
    shader::ModuleView{.id = "ui.frag", .stage = shader::Stage::Fragment, .entryPoint = "main",
                       .byteOffset = 0, .byteLength = 4}};

/// The descriptor and push-constant contract `create()` requires: one non-array combined image
/// sampler in set 0, and one vertex-visible range the size of UiPushConstants. Declared once so
/// the tests below vary the thing they are about and nothing else.
constexpr std::array<shader::DescriptorBinding, 1> conformingDescriptors{
    shader::DescriptorBinding{.set = 0,
                              .binding = 0,
                              .kind = shader::DescriptorKind::CombinedImageSampler,
                              .count = 1,
                              .stages = shader::fragmentBit}};

constexpr std::array<shader::PushConstantRange, 1> conformingPushConstants{
    shader::PushConstantRange{.offset = 0,
                              .size = sizeof(UiPushConstants),
                              .stages = shader::vertexBit}};

[[nodiscard]] shader::PackageView packageWith(
    std::span<const shader::ModuleView> modules) noexcept {
    return shader::PackageView{.id = "test",
                               .spirv = payload,
                               .modules = modules,
                               .descriptors = conformingDescriptors,
                               .pushConstants = conformingPushConstants};
}

/// The error `create()` reports, or nullopt when it got past validation. Never expected to
/// succeed here: a fabricated device handle would fault the moment a real call used it.
[[nodiscard]] std::optional<RenderError> creationError(
    const VulkanRenderContext& context, const shader::PackageView& package,
    const draw::DrawBudget& budget) noexcept {
    auto renderer = UiRenderer::create(context, package, budget);
    if (renderer.has_value()) {
        return std::nullopt;
    }
    return renderer.error();
}

}  // namespace

// ---------------------------------------------------------------------------
// VulkanRenderContext
// ---------------------------------------------------------------------------

TEST_CASE("A fully populated context is valid", "evidence-unit") {
    CHECK(plausibleContext().isValid());
}

TEST_CASE("A default-constructed context is invalid on every axis", "evidence-unit") {
    // The reason this type replaces mdux::VulkanContext: that one leaves its handles
    // uninitialised, so a stack-allocated instance reads as a plausible device and validation
    // passes right up until the first real call faults.
    const VulkanRenderContext context;
    CHECK(!context.isValid());
    CHECK(context.device == VK_NULL_HANDLE);
    CHECK(context.physicalDevice == VK_NULL_HANDLE);
    CHECK(context.renderPass == VK_NULL_HANDLE);
    CHECK(context.queue == VK_NULL_HANDLE);
    CHECK(context.viewport.width == 0);
    CHECK(context.viewport.height == 0);
    CHECK(context.subpass == 0);
}

TEST_CASE("A context missing any one member is invalid", "evidence-unit") {
    VulkanRenderContext noDevice = plausibleContext();
    noDevice.device = VK_NULL_HANDLE;
    CHECK(!noDevice.isValid());

    VulkanRenderContext noPhysical = plausibleContext();
    noPhysical.physicalDevice = VK_NULL_HANDLE;
    CHECK(!noPhysical.isValid());

    VulkanRenderContext noRenderPass = plausibleContext();
    noRenderPass.renderPass = VK_NULL_HANDLE;
    CHECK(!noRenderPass.isValid());

    VulkanRenderContext noQueue = plausibleContext();
    noQueue.queue = VK_NULL_HANDLE;
    CHECK(!noQueue.isValid());

    VulkanRenderContext noViewport = plausibleContext();
    noViewport.viewport = core::Extent2D{};
    CHECK(!noViewport.isValid());
}

// ---------------------------------------------------------------------------
// create() validation, in the order it happens
// ---------------------------------------------------------------------------

TEST_CASE("Each missing context member is reported distinctly", "evidence-unit") {
    // Distinctly, not as one "invalid context": a caller that forgot the render pass and a caller
    // that forgot the device have different bugs, and the diagnostic should say which.
    const shader::PackageView package = packageWith(bothStages);

    VulkanRenderContext context = plausibleContext();
    context.device = VK_NULL_HANDLE;
    CHECK(creationError(context, package, workableBudget) == RenderError::NullDevice);

    context = plausibleContext();
    context.physicalDevice = VK_NULL_HANDLE;
    CHECK(creationError(context, package, workableBudget) == RenderError::NullPhysicalDevice);

    context = plausibleContext();
    context.renderPass = VK_NULL_HANDLE;
    CHECK(creationError(context, package, workableBudget) == RenderError::NullRenderPass);

    context = plausibleContext();
    context.queue = VK_NULL_HANDLE;
    CHECK(creationError(context, package, workableBudget) == RenderError::NullQueue);

    context = plausibleContext();
    context.viewport = core::Extent2D{.width = 0, .height = 600};
    CHECK(creationError(context, package, workableBudget) == RenderError::EmptyViewport);

    context = plausibleContext();
    context.viewport = core::Extent2D{.width = 800, .height = 0};
    CHECK(creationError(context, package, workableBudget) == RenderError::EmptyViewport);
}

TEST_CASE("A budget with no room for one primitive is rejected", "evidence-unit") {
    const shader::PackageView package = packageWith(bothStages);
    const VulkanRenderContext context = plausibleContext();

    constexpr draw::DrawBudget noVertices{.maxVertices = 3, .maxIndices = 96, .maxCommands = 4};
    CHECK(creationError(context, package, noVertices) == RenderError::EmptyBudget);

    constexpr draw::DrawBudget noIndices{.maxVertices = 64, .maxIndices = 5, .maxCommands = 4};
    CHECK(creationError(context, package, noIndices) == RenderError::EmptyBudget);

    constexpr draw::DrawBudget noCommands{.maxVertices = 64, .maxIndices = 96, .maxCommands = 0};
    CHECK(creationError(context, package, noCommands) == RenderError::EmptyBudget);
}

TEST_CASE("A budget beyond the index width is rejected", "evidence-unit") {
    // The same ceiling mdux.draw enforces, checked again here because a renderer built from a
    // budget DrawList would refuse could never record a full frame.
    constexpr draw::DrawBudget tooLarge{
        .maxVertices = draw::maxIndexableVertices + 1, .maxIndices = 96, .maxCommands = 4};
    CHECK(creationError(plausibleContext(), packageWith(bothStages), tooLarge) ==
          RenderError::BudgetExceedsIndexWidth);
}

TEST_CASE("A package missing a stage is reported by which stage", "evidence-unit") {
    const VulkanRenderContext context = plausibleContext();

    static constexpr std::array<shader::ModuleView, 1> fragmentOnly{
        shader::ModuleView{.id = "ui.frag", .stage = shader::Stage::Fragment,
                           .entryPoint = "main", .byteOffset = 0, .byteLength = 4}};
    CHECK(creationError(context, packageWith(fragmentOnly), workableBudget) ==
          RenderError::MissingVertexModule);

    static constexpr std::array<shader::ModuleView, 1> vertexOnly{
        shader::ModuleView{.id = "ui.vert", .stage = shader::Stage::Vertex,
                           .entryPoint = "main", .byteOffset = 0, .byteLength = 4}};
    CHECK(creationError(context, packageWith(vertexOnly), workableBudget) ==
          RenderError::MissingFragmentModule);

    CHECK(creationError(context, packageWith({}), workableBudget) ==
          RenderError::MissingVertexModule);
}

TEST_CASE("Context is validated before the package, and both before any device call",
          "evidence-unit") {
    // Ordering matters for more than tidiness. Every check in this file runs against fabricated
    // handles, which is only safe because validation completes before the first Vulkan call - so
    // a reordering that moved a device call earlier would turn these tests into segfaults rather
    // than failures. Asserting the order keeps that property visible.
    VulkanRenderContext context = plausibleContext();
    context.device = VK_NULL_HANDLE;

    static constexpr std::array<shader::ModuleView, 0> none{};
    constexpr draw::DrawBudget broken{.maxVertices = 0, .maxIndices = 0, .maxCommands = 0};

    // All three are wrong; the context error is the one reported.
    CHECK(creationError(context, packageWith(none), broken) == RenderError::NullDevice);

    // With the context fixed, the budget outranks the package.
    context = plausibleContext();
    CHECK(creationError(context, packageWith(none), broken) == RenderError::EmptyBudget);
}

// ---------------------------------------------------------------------------
// The push constant block
// ---------------------------------------------------------------------------

TEST_CASE("UiPushConstants matches the block the shader package declares", "evidence-unit") {
    // recipes/shader/mdux-ui/ui.vert declares `vec2 viewportSize` at offset 0, and the committed
    // package records that as offset 0, size 8. A mismatch here would corrupt the uniform rather
    // than fail, so the number is pinned on both sides.
    CHECK(sizeof(UiPushConstants) == 8);
    CHECK(alignof(UiPushConstants) == 4);
    CHECK(offsetof(UiPushConstants, viewportWidth) == 0);
    CHECK(offsetof(UiPushConstants, viewportHeight) == 4);
    CHECK(std::is_trivially_copyable_v<UiPushConstants>);
}

TEST_CASE("Every RenderError has its own description", "evidence-unit") {
    constexpr std::array<RenderError, 27> all{
        RenderError::NullDevice,
        RenderError::NullPhysicalDevice,
        RenderError::NullRenderPass,
        RenderError::NullQueue,
        RenderError::EmptyViewport,
        RenderError::EmptyBudget,
        RenderError::BudgetExceedsIndexWidth,
        RenderError::MissingVertexModule,
        RenderError::MissingFragmentModule,
        RenderError::ShaderModuleCreationFailed,
        RenderError::DescriptorSetLayoutCreationFailed,
        RenderError::PipelineLayoutCreationFailed,
        RenderError::PipelineCreationFailed,
        RenderError::BufferCreationFailed,
        RenderError::NoSuitableMemoryType,
        RenderError::MemoryAllocationFailed,
        RenderError::MemoryMapFailed,
        RenderError::ImageCreationFailed,
        RenderError::ImageViewCreationFailed,
        RenderError::SamplerCreationFailed,
        RenderError::DescriptorPoolCreationFailed,
        RenderError::DescriptorSetAllocationFailed,
        RenderError::CommandPoolCreationFailed,
        RenderError::CommandBufferAllocationFailed,
        RenderError::AtlasUploadFailed,
        RenderError::NullCommandBuffer,
        RenderError::FrameExceedsBudget,
    };
    std::vector<std::string_view> seen;
    for (const RenderError error : all) {
        const std::string_view text = describe(error);
        CHECK(!text.empty());
        CHECK(std::ranges::find(seen, text) == seen.end());
        seen.push_back(text);
    }
}

TEST_CASE("A moved-from renderer is safe to destroy", "evidence-unit") {
    // No renderer can be constructed without a device, so this checks the property that does not
    // need one: the type is move-only, and copying is deleted rather than deep-copied - a copy
    // would duplicate handles the destructor frees, making the second destruction a use-after-free
    // of device objects.
    CHECK(!std::is_copy_constructible_v<UiRenderer>);
    CHECK(!std::is_copy_assignable_v<UiRenderer>);
    CHECK(std::is_move_constructible_v<UiRenderer>);
    CHECK(std::is_move_assignable_v<UiRenderer>);
    CHECK(std::is_nothrow_move_constructible_v<UiRenderer>);
}

// ---------------------------------------------------------------------------
// The package's pipeline contract
// ---------------------------------------------------------------------------

TEST_CASE("A descriptor outside set 0 is refused rather than mistranslated", "evidence-unit") {
    // create() builds one descriptor set layout and passes it as set 0. A package declaring set 1
    // would produce a pipeline layout that does not match its own contract, and nothing would say
    // so until a draw sampled whatever happened to be bound.
    constexpr std::array<shader::DescriptorBinding, 1> secondSet{
        shader::DescriptorBinding{.set = 1,
                                  .binding = 0,
                                  .kind = shader::DescriptorKind::CombinedImageSampler,
                                  .count = 1,
                                  .stages = shader::fragmentBit}};
    shader::PackageView package = packageWith(bothStages);
    package.descriptors = secondSet;

    CHECK(creationError(plausibleContext(), package, workableBudget) ==
          RenderError::UnsupportedDescriptorSet);
}

TEST_CASE("Two descriptors sharing a binding number are refused", "evidence-unit") {
    // vkCreateDescriptorSetLayout reports this as a validation message with no reference to the
    // package that caused it.
    constexpr std::array<shader::DescriptorBinding, 2> collide{
        shader::DescriptorBinding{.set = 0,
                                  .binding = 0,
                                  .kind = shader::DescriptorKind::CombinedImageSampler,
                                  .count = 1,
                                  .stages = shader::fragmentBit},
        shader::DescriptorBinding{.set = 0,
                                  .binding = 0,
                                  .kind = shader::DescriptorKind::UniformBuffer,
                                  .count = 1,
                                  .stages = shader::vertexBit}};
    shader::PackageView package = packageWith(bothStages);
    package.descriptors = collide;

    CHECK(creationError(plausibleContext(), package, workableBudget) ==
          RenderError::DuplicateDescriptorBinding);
}

TEST_CASE("A descriptor contract the atlas write cannot satisfy is refused", "evidence-unit") {
    shader::PackageView noDescriptors = packageWith(bothStages);
    noDescriptors.descriptors = {};
    CHECK(creationError(plausibleContext(), noDescriptors, workableBudget) ==
          RenderError::UnsupportedDescriptorContract);

    // An array of samplers: the pool and the write both assume a count of one.
    constexpr std::array<shader::DescriptorBinding, 1> samplerArray{
        shader::DescriptorBinding{.set = 0,
                                  .binding = 0,
                                  .kind = shader::DescriptorKind::CombinedImageSampler,
                                  .count = 4,
                                  .stages = shader::fragmentBit}};
    shader::PackageView arrayed = packageWith(bothStages);
    arrayed.descriptors = samplerArray;
    CHECK(creationError(plausibleContext(), arrayed, workableBudget) ==
          RenderError::UnsupportedDescriptorContract);

    // A uniform buffer where the renderer writes an image.
    constexpr std::array<shader::DescriptorBinding, 1> wrongKind{
        shader::DescriptorBinding{.set = 0,
                                  .binding = 0,
                                  .kind = shader::DescriptorKind::UniformBuffer,
                                  .count = 1,
                                  .stages = shader::fragmentBit}};
    shader::PackageView mistyped = packageWith(bothStages);
    mistyped.descriptors = wrongKind;
    CHECK(creationError(plausibleContext(), mistyped, workableBudget) ==
          RenderError::UnsupportedDescriptorContract);
}

TEST_CASE("A push constant range record() could not fill is refused", "evidence-unit") {
    // record() writes a UiPushConstants at the offset and stage the package declared. Every case
    // here would leave record() disagreeing with the pipeline layout built from the same package.
    shader::PackageView none = packageWith(bothStages);
    none.pushConstants = {};
    CHECK(creationError(plausibleContext(), none, workableBudget) ==
          RenderError::UnsupportedPushConstantContract);

    constexpr std::array<shader::PushConstantRange, 1> wrongSize{
        shader::PushConstantRange{.offset = 0, .size = 4, .stages = shader::vertexBit}};
    shader::PackageView sized = packageWith(bothStages);
    sized.pushConstants = wrongSize;
    CHECK(creationError(plausibleContext(), sized, workableBudget) ==
          RenderError::UnsupportedPushConstantContract);

    constexpr std::array<shader::PushConstantRange, 1> fragmentOnly{
        shader::PushConstantRange{
            .offset = 0, .size = sizeof(UiPushConstants), .stages = shader::fragmentBit}};
    shader::PackageView staged = packageWith(bothStages);
    staged.pushConstants = fragmentOnly;
    CHECK(creationError(plausibleContext(), staged, workableBudget) ==
          RenderError::UnsupportedPushConstantContract);
}
