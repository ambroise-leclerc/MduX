/**
 * @file VulkanRenderer.cpp
 * @brief Implementation of the adapter-zone fixed-budget renderer.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Creation is a sequence of steps, each of which can fail, and each failure has to leave the
 * partially-built object destroyed rather than leaked. That is handled by building into a local
 * `UiRenderer` and letting its destructor run on the error paths - `destroy()` is written to
 * tolerate any subset of the handles being null, so there is exactly one teardown routine rather
 * than one per failure point.
 */
module;

#include <vulkan/vulkan.h>

module mdux.render.vulkan;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.shader.schema;

namespace mdux::render {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace draw = mdux::draw;
namespace shader = mdux::shader;

namespace {

/// Host-visible and host-coherent, so a frame is a memcpy with no explicit flush.
///
/// Coherent rather than merely visible on purpose: a non-coherent mapping needs
/// vkFlushMappedMemoryRanges with correctly aligned ranges, and getting that alignment wrong is
/// a class of bug that shows as intermittently stale geometry rather than as an error.
constexpr VkMemoryPropertyFlags frameMemoryProperties =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

[[nodiscard]] std::optional<std::uint32_t> findMemoryType(VkPhysicalDevice physicalDevice,
                                                          std::uint32_t typeBits,
                                                          VkMemoryPropertyFlags properties) noexcept {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const bool allowed = (typeBits & (1U << i)) != 0;
        const bool suitable =
            (memory.memoryTypes[i].propertyFlags & properties) == properties;
        if (allowed && suitable) {
            return i;
        }
    }
    return std::nullopt;
}

/// Creates a buffer, backs it with mapped host-coherent memory, and hands back all three.
struct MappedBuffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    void* mapped{nullptr};
};

[[nodiscard]] Result<MappedBuffer, RenderError> createMappedBuffer(
    const VulkanRenderContext& context, VkDeviceSize size, VkBufferUsageFlags usage) noexcept {
    MappedBuffer result;

    const VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .size = size,
                                        .usage = usage,
                                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                        .queueFamilyIndexCount = 0,
                                        .pQueueFamilyIndices = nullptr};
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &result.buffer) != VK_SUCCESS) {
        return err(RenderError::BufferCreationFailed);
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, result.buffer, &requirements);

    const auto typeIndex = findMemoryType(context.physicalDevice, requirements.memoryTypeBits,
                                          frameMemoryProperties);
    if (!typeIndex.has_value()) {
        vkDestroyBuffer(context.device, result.buffer, nullptr);
        return err(RenderError::NoSuitableMemoryType);
    }

    const VkMemoryAllocateInfo allocateInfo{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                            .pNext = nullptr,
                                            .allocationSize = requirements.size,
                                            .memoryTypeIndex = *typeIndex};
    if (vkAllocateMemory(context.device, &allocateInfo, nullptr, &result.memory) != VK_SUCCESS) {
        vkDestroyBuffer(context.device, result.buffer, nullptr);
        return err(RenderError::MemoryAllocationFailed);
    }
    if (vkBindBufferMemory(context.device, result.buffer, result.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(context.device, result.memory, nullptr);
        vkDestroyBuffer(context.device, result.buffer, nullptr);
        return err(RenderError::MemoryAllocationFailed);
    }
    // Mapped once, for the renderer's lifetime. Mapping per frame would be a driver round trip
    // per frame for no benefit: the memory is host-coherent and never moves.
    if (vkMapMemory(context.device, result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped) !=
        VK_SUCCESS) {
        vkFreeMemory(context.device, result.memory, nullptr);
        vkDestroyBuffer(context.device, result.buffer, nullptr);
        return err(RenderError::MemoryMapFailed);
    }

    return result;
}

[[nodiscard]] Result<VkShaderModule, RenderError> createShaderModule(
    VkDevice device, std::span<const std::byte> spirv) noexcept {
    // vkCreateShaderModule wants 32-bit words and requires 4-byte alignment. The generated data
    // is a byte array whose alignment the compiler does not guarantee, so it is copied into a
    // word vector rather than reinterpret_cast in place - a misaligned load is undefined
    // behaviour on the way to being a driver crash.
    std::vector<std::uint32_t> words(spirv.size() / 4);
    std::memcpy(words.data(), spirv.data(), words.size() * 4);

    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = words.size() * 4,
                                        .pCode = words.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return err(RenderError::ShaderModuleCreationFailed);
    }
    return module;
}

/// Creates the 1x1 opaque white default atlas, uploads it, and leaves it sampleable.
///
/// White is the neutral value rather than a placeholder: multiplying the vertex colour by white is
/// the identity for the sampled-RGBA path, and a red channel of 1.0 is full coverage for the R8
/// path. A renderer with no glyphs or images yet therefore behaves correctly rather than
/// approximately.
struct DefaultAtlas {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

/// Builds the sampled image, in whichever format and extent the caller needs.
///
/// One function for both the 1x1 white default and a baked R8 coverage sheet, because they differ
/// only in format, extent and the bytes staged into them - the image, memory, view, staging
/// buffer, layout transitions and one-shot command buffer are identical. Two copies of that
/// sequence would be two places for a barrier to go wrong.
[[nodiscard]] Result<DefaultAtlas, RenderError> createAtlasImage(const VulkanRenderContext& context, VkFormat format,
                                                                 std::uint32_t width, std::uint32_t height,
                                                                 std::span<const std::byte> pixels) noexcept {
    DefaultAtlas atlas;

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(context.device, &imageInfo, nullptr, &atlas.image) != VK_SUCCESS) {
        return err(RenderError::ImageCreationFailed);
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device, atlas.image, &requirements);
    const auto typeIndex = findMemoryType(context.physicalDevice, requirements.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!typeIndex.has_value()) {
        vkDestroyImage(context.device, atlas.image, nullptr);
        return err(RenderError::NoSuitableMemoryType);
    }
    const VkMemoryAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                        .pNext = nullptr,
                                        .allocationSize = requirements.size,
                                        .memoryTypeIndex = *typeIndex};
    if (vkAllocateMemory(context.device, &allocate, nullptr, &atlas.memory) != VK_SUCCESS ||
        vkBindImageMemory(context.device, atlas.image, atlas.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(context.device, atlas.memory, nullptr);
        vkDestroyImage(context.device, atlas.image, nullptr);
        return err(RenderError::MemoryAllocationFailed);
    }

    // The pixels through a staging buffer and a one-shot command buffer. Everything created here
    // is destroyed before returning: the atlas outlives this function, the machinery does not.
    auto staging = createMappedBuffer(context, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging.has_value()) {
        vkFreeMemory(context.device, atlas.memory, nullptr);
        vkDestroyImage(context.device, atlas.image, nullptr);
        return err(staging.error());
    }
    std::memcpy(staging->mapped, pixels.data(), pixels.size());

    const VkCommandPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                           .queueFamilyIndex = context.queueFamilyIndex};
    VkCommandPool pool = VK_NULL_HANDLE;
    RenderError failure = RenderError::AtlasUploadFailed;
    bool ok = vkCreateCommandPool(context.device, &poolInfo, nullptr, &pool) == VK_SUCCESS;
    if (!ok) {
        failure = RenderError::CommandPoolCreationFailed;
    }

    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (ok) {
        const VkCommandBufferAllocateInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1};
        ok = vkAllocateCommandBuffers(context.device, &commandInfo, &commands) == VK_SUCCESS;
        if (!ok) {
            failure = RenderError::CommandBufferAllocationFailed;
        }
    }

    if (ok) {
        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr};
        ok = vkBeginCommandBuffer(commands, &begin) == VK_SUCCESS;
    }

    if (ok) {
        VkImageMemoryBarrier toTransfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = atlas.image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toTransfer);

        const VkBufferImageCopy copy{.bufferOffset = 0,
                                     .bufferRowLength = 0,
                                     .bufferImageHeight = 0,
                                     .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                     .imageOffset = {0, 0, 0},
                                     .imageExtent = {width, height, 1}};
        vkCmdCopyBufferToImage(commands, staging->buffer, atlas.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toShader{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = atlas.image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toShader);

        ok = vkEndCommandBuffer(commands) == VK_SUCCESS;
    }

    if (ok) {
        const VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                  .pNext = nullptr,
                                  .waitSemaphoreCount = 0,
                                  .pWaitSemaphores = nullptr,
                                  .pWaitDstStageMask = nullptr,
                                  .commandBufferCount = 1,
                                  .pCommandBuffers = &commands,
                                  .signalSemaphoreCount = 0,
                                  .pSignalSemaphores = nullptr};
        ok = vkQueueSubmit(context.queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
             vkQueueWaitIdle(context.queue) == VK_SUCCESS;
    }

    if (pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context.device, pool, nullptr);
    }
    vkUnmapMemory(context.device, staging->memory);
    vkDestroyBuffer(context.device, staging->buffer, nullptr);
    vkFreeMemory(context.device, staging->memory, nullptr);

    if (!ok) {
        vkFreeMemory(context.device, atlas.memory, nullptr);
        vkDestroyImage(context.device, atlas.image, nullptr);
        return err(failure);
    }

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = atlas.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &atlas.view) != VK_SUCCESS) {
        vkFreeMemory(context.device, atlas.memory, nullptr);
        vkDestroyImage(context.device, atlas.image, nullptr);
        return err(RenderError::ImageViewCreationFailed);
    }

    return atlas;
}

[[nodiscard]] VkDescriptorType toVulkan(shader::DescriptorKind kind) noexcept {
    switch (kind) {
    case shader::DescriptorKind::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case shader::DescriptorKind::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case shader::DescriptorKind::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case shader::DescriptorKind::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case shader::DescriptorKind::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

[[nodiscard]] VkShaderStageFlags toVulkan(shader::StageMask mask) noexcept {
    VkShaderStageFlags flags = 0;
    if ((mask & shader::vertexBit) != 0) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((mask & shader::fragmentBit) != 0) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return flags;
}

}  // namespace

std::string_view describe(RenderError error) noexcept {
    switch (error) {
    case RenderError::NullDevice:          return "context has no device";
    case RenderError::NullPhysicalDevice:  return "context has no physical device";
    case RenderError::NullRenderPass:      return "context has no render pass";
    case RenderError::NullQueue:           return "context has no queue";
    case RenderError::EmptyViewport:       return "context viewport has zero width or height";
    case RenderError::EmptyBudget:         return "budget has no room for a primitive";
    case RenderError::BudgetExceedsIndexWidth:
        return "budget exceeds what a 16-bit index can address";
    case RenderError::MissingVertexModule: return "shader package declares no vertex stage";
    case RenderError::MissingFragmentModule:
        return "shader package declares no fragment stage";
    case RenderError::ShaderModuleCreationFailed: return "vkCreateShaderModule failed";
    case RenderError::DescriptorSetLayoutCreationFailed:
        return "vkCreateDescriptorSetLayout failed";
    case RenderError::PipelineLayoutCreationFailed: return "vkCreatePipelineLayout failed";
    case RenderError::PipelineCreationFailed: return "vkCreateGraphicsPipelines failed";
    case RenderError::BufferCreationFailed:   return "vkCreateBuffer failed";
    case RenderError::NoSuitableMemoryType:
        return "no host-visible, host-coherent memory type on this device";
    case RenderError::MemoryAllocationFailed: return "vkAllocateMemory failed";
    case RenderError::MemoryMapFailed:        return "vkMapMemory failed";
    case RenderError::ImageCreationFailed:    return "vkCreateImage failed for the default atlas";
    case RenderError::ImageViewCreationFailed:
        return "vkCreateImageView failed for the default atlas";
    case RenderError::SamplerCreationFailed:  return "vkCreateSampler failed";
    case RenderError::DescriptorPoolCreationFailed: return "vkCreateDescriptorPool failed";
    case RenderError::DescriptorSetAllocationFailed:
        return "vkAllocateDescriptorSets failed";
    case RenderError::CommandPoolCreationFailed:
        return "vkCreateCommandPool failed for the atlas upload";
    case RenderError::CommandBufferAllocationFailed:
        return "vkAllocateCommandBuffers failed for the atlas upload";
    case RenderError::AtlasUploadFailed:      return "uploading the atlas failed";
    case RenderError::AtlasExtentMismatch:
        return "the coverage atlas has a zero extent, or its byte count is not width * height";
    case RenderError::SampledRgbaWithCoverageAtlas:
        return "the frame samples an RGBA atlas, but this renderer holds an R8 coverage sheet";
    case RenderError::NullCommandBuffer:      return "command buffer is null";
    case RenderError::FrameExceedsBudget:
        return "draw list is larger than the renderer's budget";
    case RenderError::UnsupportedDescriptorSet:
        return "package declares a descriptor outside set 0; this renderer builds one set layout";
    case RenderError::DuplicateDescriptorBinding:
        return "package declares two descriptors with the same binding number";
    case RenderError::UnsupportedDescriptorContract:
        return "package must declare exactly one non-array combined image sampler";
    case RenderError::UnsupportedPushConstantContract:
        return "package must declare one vertex-visible push constant range matching "
               "UiPushConstants";
    }
    return "unknown render error";
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UiRenderer::~UiRenderer() {
    destroy();
}

UiRenderer::UiRenderer(UiRenderer&& other) noexcept {
    *this = std::move(other);
}

UiRenderer& UiRenderer::operator=(UiRenderer&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    // Destroy what this object already owns before taking the other's handles, or they leak.
    destroy();

    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    vertexModule_ = std::exchange(other.vertexModule_, VK_NULL_HANDLE);
    fragmentModule_ = std::exchange(other.fragmentModule_, VK_NULL_HANDLE);
    descriptorSetLayout_ = std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE);
    pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
    pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
    vertexBuffer_ = std::exchange(other.vertexBuffer_, VK_NULL_HANDLE);
    vertexMemory_ = std::exchange(other.vertexMemory_, VK_NULL_HANDLE);
    indexBuffer_ = std::exchange(other.indexBuffer_, VK_NULL_HANDLE);
    indexMemory_ = std::exchange(other.indexMemory_, VK_NULL_HANDLE);
    atlasImage_ = std::exchange(other.atlasImage_, VK_NULL_HANDLE);
    atlasMemory_ = std::exchange(other.atlasMemory_, VK_NULL_HANDLE);
    atlasView_ = std::exchange(other.atlasView_, VK_NULL_HANDLE);
    atlasSampler_ = std::exchange(other.atlasSampler_, VK_NULL_HANDLE);
    descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
    // The set is owned by the pool and freed with it, so it is carried but never freed directly.
    descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
    vertexMapped_ = std::exchange(other.vertexMapped_, nullptr);
    indexMapped_ = std::exchange(other.indexMapped_, nullptr);
    vertexBytes_ = std::exchange(other.vertexBytes_, 0);
    indexBytes_ = std::exchange(other.indexBytes_, 0);
    budget_ = std::exchange(other.budget_, draw::DrawBudget{});
    viewport_ = std::exchange(other.viewport_, mdux::core::Extent2D{});
    // The validated package contract. Not handles, but just as load-bearing: record() pushes
    // constants using pushSize_, so a member left behind here means a moved-from renderer pushes
    // nothing and every vertex reads a zero viewport. create() returns by value, so *every*
    // renderer is a moved one - this list is not an edge case.
    atlasBinding_ = std::exchange(other.atlasBinding_, 0);
    pushStages_ = std::exchange(other.pushStages_, 0);
    pushOffset_ = std::exchange(other.pushOffset_, 0);
    pushSize_ = std::exchange(other.pushSize_, 0);
    // Left out of this list on the first attempt, and the guard then silently never fired: create()
    // returns by value, so the flag was set on a renderer that was immediately moved from. Exactly
    // what the note above describes, which is why the test that exercises the guard is what caught
    // it rather than review.
    atlasIsCoverageOnly_ = std::exchange(other.atlasIsCoverageOnly_, false);
    return *this;
}

void UiRenderer::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    // Reverse order of creation, and every handle nulled so a second call is a no-op. The
    // partially-built object on a create() error path relies on exactly that tolerance.
    if (indexMapped_ != nullptr) {
        vkUnmapMemory(device_, indexMemory_);
        indexMapped_ = nullptr;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, indexBuffer_, nullptr);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (indexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, indexMemory_, nullptr);
        indexMemory_ = VK_NULL_HANDLE;
    }
    if (vertexMapped_ != nullptr) {
        vkUnmapMemory(device_, vertexMemory_);
        vertexMapped_ = nullptr;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertexMemory_, nullptr);
        vertexMemory_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        // Frees the set allocated from it; no separate vkFreeDescriptorSets is needed or allowed
        // without VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT.
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (atlasSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, atlasSampler_, nullptr);
        atlasSampler_ = VK_NULL_HANDLE;
    }
    if (atlasView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, atlasView_, nullptr);
        atlasView_ = VK_NULL_HANDLE;
    }
    if (atlasImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, atlasImage_, nullptr);
        atlasImage_ = VK_NULL_HANDLE;
    }
    if (atlasMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, atlasMemory_, nullptr);
        atlasMemory_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (fragmentModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragmentModule_, nullptr);
        fragmentModule_ = VK_NULL_HANDLE;
    }
    if (vertexModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertexModule_, nullptr);
        vertexModule_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

Result<UiRenderer, RenderError> UiRenderer::create(const VulkanRenderContext& context,
                                                   const shader::PackageView& package,
                                                   const draw::DrawBudget& budget) noexcept {
    // The 1x1 white default. White is the neutral value: it is the identity for the sampled-RGBA
    // path and full coverage for the R8 one, so a renderer with no atlas yet behaves correctly
    // rather than approximately.
    static constexpr std::array<std::byte, 4> white{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    return createInternal(context, package, budget, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, white);
}

Result<UiRenderer, RenderError> UiRenderer::createWithCoverageAtlas(const VulkanRenderContext& context,
                                                                    const shader::PackageView& package,
                                                                    const draw::DrawBudget& budget,
                                                                    std::span<const std::byte> atlas,
                                                                    std::uint32_t width,
                                                                    std::uint32_t height) noexcept {
    // Checked here rather than trusted: a sheet whose byte count disagrees with its extent would
    // stage the wrong number of bytes and read uninitialised memory into a texture, which renders
    // as plausible noise rather than failing.
    if (width == 0 || height == 0
        || atlas.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return err(RenderError::AtlasExtentMismatch);
    }
    return createInternal(context, package, budget, VK_FORMAT_R8_UNORM, width, height, atlas);
}

Result<UiRenderer, RenderError> UiRenderer::createInternal(const VulkanRenderContext& context,
                                                           const shader::PackageView& package,
                                                           const draw::DrawBudget& budget, VkFormat atlasFormat,
                                                           std::uint32_t atlasWidth, std::uint32_t atlasHeight,
                                                           std::span<const std::byte> atlasPixels) noexcept {
    // Context and budget first: both are cheap to check and neither needs a device call, so a
    // caller's mistake is reported before anything is created.
    if (context.device == VK_NULL_HANDLE) {
        return err(RenderError::NullDevice);
    }
    if (context.physicalDevice == VK_NULL_HANDLE) {
        return err(RenderError::NullPhysicalDevice);
    }
    if (context.renderPass == VK_NULL_HANDLE) {
        return err(RenderError::NullRenderPass);
    }
    if (context.queue == VK_NULL_HANDLE) {
        return err(RenderError::NullQueue);
    }
    if (context.viewport.width <= 0 || context.viewport.height <= 0) {
        return err(RenderError::EmptyViewport);
    }
    if (budget.maxVertices < 4 || budget.maxIndices < 6 || budget.maxCommands == 0) {
        return err(RenderError::EmptyBudget);
    }
    if (budget.maxVertices > draw::maxIndexableVertices) {
        return err(RenderError::BudgetExceedsIndexWidth);
    }

    const shader::ModuleView* vertex = nullptr;
    const shader::ModuleView* fragment = nullptr;
    for (const shader::ModuleView& module : package.modules) {
        if (module.stage == shader::Stage::Vertex && vertex == nullptr) {
            vertex = &module;
        } else if (module.stage == shader::Stage::Fragment && fragment == nullptr) {
            fragment = &module;
        }
    }
    if (vertex == nullptr) {
        return err(RenderError::MissingVertexModule);
    }
    if (fragment == nullptr) {
        return err(RenderError::MissingFragmentModule);
    }

    // Check the package declares the contract this renderer implements, before any of it is
    // translated into Vulkan objects.
    //
    // The layout below is built from `package.descriptors`, but everything downstream of it - one
    // set layout, a pool sized for one combined image sampler, a write to one binding, a push
    // constant recorded with a fixed stage and size - assumes a specific shape. Where the package
    // could disagree, it has to be refused here: `vkCreateDescriptorSetLayout` reports duplicate
    // bindings as a validation message far from this code, and a descriptor in set 1 would
    // silently build a pipeline layout that does not match the package at all.
    //
    // Refusing rather than generalising is deliberate. Multi-set support is machinery for a case
    // no package in this repository has, and #124's budget argument applies to descriptor sets as
    // much as to vertices: the renderer should do one predictable thing.
    for (std::size_t i = 0; i < package.descriptors.size(); ++i) {
        const shader::DescriptorBinding& descriptor = package.descriptors[i];
        if (descriptor.set != 0) {
            return err(RenderError::UnsupportedDescriptorSet);
        }
        for (std::size_t j = i + 1; j < package.descriptors.size(); ++j) {
            if (package.descriptors[j].binding == descriptor.binding) {
                return err(RenderError::DuplicateDescriptorBinding);
            }
        }
    }

    // Exactly one combined image sampler, not an array: that is what the atlas write below
    // performs and what the pool is sized for. Its binding number comes from the package rather
    // than being assumed to be 0.
    if (package.descriptors.size() != 1 ||
        package.descriptors.front().kind != shader::DescriptorKind::CombinedImageSampler ||
        package.descriptors.front().count != 1) {
        return err(RenderError::UnsupportedDescriptorContract);
    }
    const std::uint32_t atlasBinding = package.descriptors.front().binding;

    // One push-constant range, matching UiPushConstants exactly and visible to the vertex stage,
    // because record() writes that struct. A range of a different size would have record()
    // disagreeing with the pipeline layout it was built from.
    if (package.pushConstants.size() != 1) {
        return err(RenderError::UnsupportedPushConstantContract);
    }
    const shader::PushConstantRange& declaredPush = package.pushConstants.front();
    if (declaredPush.offset != 0 || declaredPush.size != sizeof(UiPushConstants) ||
        (declaredPush.stages & shader::vertexBit) == 0) {
        return err(RenderError::UnsupportedPushConstantContract);
    }
    const VkShaderStageFlags pushStages = toVulkan(declaredPush.stages);
    const std::uint32_t pushOffset = declaredPush.offset;
    const std::uint32_t pushSize = declaredPush.size;


    // Built into a local whose destructor cleans up on every error path below, so there is one
    // teardown routine rather than one per failure point.
    UiRenderer renderer;
    renderer.device_ = context.device;
    renderer.budget_ = budget;
    renderer.viewport_ = context.viewport;
    // Derived from the format rather than passed as a flag, so the two cannot disagree: whatever
    // image this renderer ends up holding is what record() judges a frame against.
    renderer.atlasIsCoverageOnly_ = (atlasFormat == VK_FORMAT_R8_UNORM);
    renderer.atlasBinding_ = atlasBinding;
    renderer.pushStages_ = pushStages;
    renderer.pushOffset_ = pushOffset;
    renderer.pushSize_ = pushSize;

    auto vertexModule = createShaderModule(context.device, package.moduleSpirv(vertex->id));
    if (!vertexModule.has_value()) {
        return err(vertexModule.error());
    }
    renderer.vertexModule_ = *vertexModule;

    auto fragmentModule = createShaderModule(context.device, package.moduleSpirv(fragment->id));
    if (!fragmentModule.has_value()) {
        return err(fragmentModule.error());
    }
    renderer.fragmentModule_ = *fragmentModule;

    // The descriptor set layout is the package's contract, translated. Nothing here is
    // hand-written: a shader that changes its bindings changes this layout through the baked
    // package, which is the point of #119's reflection.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(package.descriptors.size());
    for (const shader::DescriptorBinding& descriptor : package.descriptors) {
        bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = descriptor.binding,
            .descriptorType = toVulkan(descriptor.kind),
            .descriptorCount = descriptor.count,
            .stageFlags = toVulkan(descriptor.stages),
            .pImmutableSamplers = nullptr});
    }
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.empty() ? nullptr : bindings.data()};
    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr,
                                    &renderer.descriptorSetLayout_) != VK_SUCCESS) {
        return err(RenderError::DescriptorSetLayoutCreationFailed);
    }

    std::vector<VkPushConstantRange> pushRanges;
    pushRanges.reserve(package.pushConstants.size());
    for (const shader::PushConstantRange& range : package.pushConstants) {
        pushRanges.push_back(VkPushConstantRange{.stageFlags = toVulkan(range.stages),
                                                 .offset = range.offset,
                                                 .size = range.size});
    }
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &renderer.descriptorSetLayout_,
        .pushConstantRangeCount = static_cast<std::uint32_t>(pushRanges.size()),
        .pPushConstantRanges = pushRanges.empty() ? nullptr : pushRanges.data()};
    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr,
                               &renderer.pipelineLayout_) != VK_SUCCESS) {
        return err(RenderError::PipelineLayoutCreationFailed);
    }

    // The vertex layout, matching mdux::draw::UiVertex exactly. Its static_asserts pin the
    // offsets on the governed side; these attributes are the same numbers on this side.
    const VkVertexInputBindingDescription vertexBinding{
        .binding = 0,
        .stride = sizeof(draw::UiVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 4> attributes{
        VkVertexInputAttributeDescription{
            .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
        VkVertexInputAttributeDescription{
            .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 8},
        VkVertexInputAttributeDescription{
            .location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 16},
        VkVertexInputAttributeDescription{
            .location = 3, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = 20}};

    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()};

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE};

    // Viewport and scissor are dynamic: a renderer whose pipeline baked in a size would need
    // rebuilding whenever the surface resized, which is an allocation on a resize and exactly the
    // unbounded cost a fixed-budget design exists to avoid.
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                      VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr};

    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        // No culling: UI quads are emitted in one winding, and a culled quad is a blank screen
        // whose cause is invisible. Nothing here benefits from culling.
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0F,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = 0.0F,
        .lineWidth = 1.0F};

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};

    // Straight alpha blending, matching the non-premultiplied ColorRgba8 the governed side uses.
    const VkPipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F}};

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = renderer.vertexModule_,
            .pName = vertex->entryPoint.data(),
            .pSpecializationInfo = nullptr},
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = renderer.fragmentModule_,
            .pName = fragment->entryPoint.data(),
            .pSpecializationInfo = nullptr}};

    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamic,
        .layout = renderer.pipelineLayout_,
        .renderPass = context.renderPass,
        .subpass = context.subpass,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1};

    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &renderer.pipeline_) != VK_SUCCESS) {
        return err(RenderError::PipelineCreationFailed);
    }

    renderer.vertexBytes_ = static_cast<VkDeviceSize>(budget.maxVertices) * sizeof(draw::UiVertex);
    renderer.indexBytes_ = static_cast<VkDeviceSize>(budget.maxIndices) * sizeof(draw::Index);

    auto vertexBuffer = createMappedBuffer(context, renderer.vertexBytes_,
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vertexBuffer.has_value()) {
        return err(vertexBuffer.error());
    }
    renderer.vertexBuffer_ = vertexBuffer->buffer;
    renderer.vertexMemory_ = vertexBuffer->memory;
    renderer.vertexMapped_ = vertexBuffer->mapped;

    auto indexBuffer =
        createMappedBuffer(context, renderer.indexBytes_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!indexBuffer.has_value()) {
        return err(indexBuffer.error());
    }
    renderer.indexBuffer_ = indexBuffer->buffer;
    renderer.indexMemory_ = indexBuffer->memory;
    renderer.indexMapped_ = indexBuffer->mapped;

    // The default atlas, and the descriptor set that binds it. Without these a draw is undefined
    // behaviour whatever mode its vertices carry, because the pipeline layout declares a sampler.
    auto atlas = createAtlasImage(context, atlasFormat, atlasWidth, atlasHeight, atlasPixels);
    if (!atlas.has_value()) {
        return err(atlas.error());
    }
    renderer.atlasImage_ = atlas->image;
    renderer.atlasMemory_ = atlas->memory;
    renderer.atlasView_ = atlas->view;

    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        // Nearest, not linear: a coverage atlas is sampled at texel centres by construction, and
        // filtering it would blur glyph edges that the rasteriser already antialiased.
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0F,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0F,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0F,
        .maxLod = 0.0F,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE};
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &renderer.atlasSampler_) !=
        VK_SUCCESS) {
        return err(RenderError::SamplerCreationFailed);
    }

    // Sized from the package rather than from a literal. The contract check above already
    // guarantees this is one combined image sampler with count 1, so today these agree - but a
    // package that changes descriptor type is then refused by that check instead of allocating a
    // pool of the wrong type here and failing at vkAllocateDescriptorSets.
    const VkDescriptorPoolSize poolSize{
        .type = toVulkan(package.descriptors.front().kind),
        .descriptorCount = package.descriptors.front().count};
    const VkDescriptorPoolCreateInfo descriptorPoolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        // No FREE_DESCRIPTOR_SET_BIT: the set lives as long as the renderer, and destroying the
        // pool frees it. A pool that permitted individual frees would allow per-frame descriptor
        // churn, which is the cost this design exists to not have.
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize};
    if (vkCreateDescriptorPool(context.device, &descriptorPoolInfo, nullptr,
                               &renderer.descriptorPool_) != VK_SUCCESS) {
        return err(RenderError::DescriptorPoolCreationFailed);
    }

    const VkDescriptorSetAllocateInfo setInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = renderer.descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &renderer.descriptorSetLayout_};
    if (vkAllocateDescriptorSets(context.device, &setInfo, &renderer.descriptorSet_) !=
        VK_SUCCESS) {
        return err(RenderError::DescriptorSetAllocationFailed);
    }

    const VkDescriptorImageInfo imageInfo{
        .sampler = renderer.atlasSampler_,
        .imageView = renderer.atlasView_,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .pNext = nullptr,
                                     .dstSet = renderer.descriptorSet_,
                                     .dstBinding = renderer.atlasBinding_,
                                     .dstArrayElement = 0,
                                     .descriptorCount = package.descriptors.front().count,
                                     .descriptorType = toVulkan(package.descriptors.front().kind),
                                     .pImageInfo = &imageInfo,
                                     .pBufferInfo = nullptr,
                                     .pTexelBufferView = nullptr};
    vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

    return renderer;
}

// ---------------------------------------------------------------------------
// record()
// ---------------------------------------------------------------------------

ResultVoid<RenderError> UiRenderer::record(VkCommandBuffer commandBuffer,
                                           const draw::DrawList& list) noexcept {
    if (commandBuffer == VK_NULL_HANDLE) {
        return err(RenderError::NullCommandBuffer);
    }
    // A list built against a larger budget than this renderer was created for would overrun the
    // mapped buffers. Refused rather than clamped: a silently truncated frame is a wrong frame.
    if (list.vertices().size() > budget_.maxVertices ||
        list.indices().size() > budget_.maxIndices ||
        list.commands().size() > budget_.maxCommands) {
        return err(RenderError::FrameExceedsBudget);
    }
    if (atlasIsCoverageOnly_) {
        // An R8 image sampled as RGBA returns (coverage, 0, 0, 1): a picture in the wrong colours
        // rather than a black frame or a crash, so it survives review. Refused here instead.
        //
        // The scan is per frame because the mode is per vertex, and there is nowhere earlier to
        // put it - the list is rebuilt every frame. It is a pass over the same vertices the memcpy
        // below already touches, so it costs the frame nothing measurable, and it buys a refusal
        // in place of a screenshot nobody questions.
        const auto samplesRgba = [](const draw::UiVertex& vertex) noexcept {
            return vertex.mode == static_cast<std::uint32_t>(draw::DrawMode::SampledRgba);
        };
        if (std::ranges::any_of(list.vertices(), samplesRgba)) {
            return err(RenderError::SampledRgbaWithCoverageAtlas);
        }
    }

    if (!list.empty()) {
        std::memcpy(vertexMapped_, list.vertices().data(),
                    list.vertices().size() * sizeof(draw::UiVertex));
        std::memcpy(indexMapped_, list.indices().data(),
                    list.indices().size() * sizeof(draw::Index));
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    // Bound for every frame, including one that draws nothing but solid rectangles: the pipeline
    // layout declares the sampler, so a draw without a set bound is undefined behaviour whatever
    // the vertices' mode says. Found the hard way - it faults inside the driver, not at a
    // validation message.
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descriptorSet_, 0, nullptr);

    const VkViewport viewport{.x = 0.0F,
                              .y = 0.0F,
                              .width = static_cast<float>(viewport_.width),
                              .height = static_cast<float>(viewport_.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    const UiPushConstants push{.viewportWidth = static_cast<float>(viewport_.width),
                               .viewportHeight = static_cast<float>(viewport_.height)};
    // Stage, offset and size come from what create() validated the package declares, not from
    // literals here: record() must agree with the pipeline layout that was actually built.
    vkCmdPushConstants(commandBuffer, pipelineLayout_, pushStages_, pushOffset_, pushSize_, &push);

    // An empty frame still binds and sets state, so a caller that records every frame the same
    // way gets the same command stream shape whether or not anything was drawn.
    if (list.empty()) {
        const VkRect2D scissor{.offset = {0, 0},
                               .extent = {static_cast<std::uint32_t>(viewport_.width),
                                          static_cast<std::uint32_t>(viewport_.height)}};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        return {};
    }

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);

    for (const draw::DrawCommand& command : list.commands()) {
        // A zero-sized clip means "no clip", which is what a DrawList carries until setClip is
        // called. Scissoring to a zero rectangle would discard the whole command silently.
        const bool clipped = command.clip.width > 0 && command.clip.height > 0;
        const VkRect2D scissor{
            .offset = {clipped ? command.clip.x : 0, clipped ? command.clip.y : 0},
            .extent = {clipped ? static_cast<std::uint32_t>(command.clip.width)
                               : static_cast<std::uint32_t>(viewport_.width),
                       clipped ? static_cast<std::uint32_t>(command.clip.height)
                               : static_cast<std::uint32_t>(viewport_.height)}};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdDrawIndexed(commandBuffer, command.indexCount, 1, command.firstIndex, 0, 0);
    }

    return {};
}

}  // namespace mdux::render
