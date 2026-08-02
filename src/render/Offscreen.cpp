/**
 * @brief Implementation of the adapter-zone headless render target.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 */
module;

#include <vulkan/vulkan.h>

module mdux.render.offscreen;

import std;
import mdux.core.result;
import mdux.core.units;

namespace mdux::render {

using mdux::core::err;
using mdux::core::Result;

namespace {

/// The one format this target uses. Chosen so a readback row is already ColorRgba8.
constexpr VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

[[nodiscard]] std::optional<std::uint32_t> findMemoryType(VkPhysicalDevice physicalDevice,
                                                          std::uint32_t typeBits,
                                                          VkMemoryPropertyFlags properties) noexcept {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((typeBits & (1U << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view describe(OffscreenError error) noexcept {
    switch (error) {
    case OffscreenError::NullDevice:         return "device is null";
    case OffscreenError::NullPhysicalDevice: return "physical device is null";
    case OffscreenError::NullQueue:          return "queue is null";
    case OffscreenError::EmptyExtent:        return "extent has zero width or height";
    case OffscreenError::ExtentTooLarge:     return "extent exceeds the offscreen pixel ceiling";
    case OffscreenError::ImageCreationFailed:      return "vkCreateImage failed";
    case OffscreenError::NoSuitableMemoryType:
        return "no memory type on this device satisfies the required properties";
    case OffscreenError::MemoryAllocationFailed:   return "vkAllocateMemory failed";
    case OffscreenError::ImageViewCreationFailed:  return "vkCreateImageView failed";
    case OffscreenError::RenderPassCreationFailed: return "vkCreateRenderPass failed";
    case OffscreenError::FramebufferCreationFailed: return "vkCreateFramebuffer failed";
    case OffscreenError::BufferCreationFailed:     return "vkCreateBuffer failed";
    case OffscreenError::MemoryMapFailed:          return "vkMapMemory failed";
    case OffscreenError::CommandPoolCreationFailed: return "vkCreateCommandPool failed";
    case OffscreenError::CommandBufferAllocationFailed:
        return "vkAllocateCommandBuffers failed";
    case OffscreenError::BeginCommandBufferFailed: return "vkBeginCommandBuffer failed";
    case OffscreenError::EndCommandBufferFailed:   return "vkEndCommandBuffer failed";
    case OffscreenError::SubmitFailed:             return "vkQueueSubmit failed";
    case OffscreenError::WaitFailed:               return "vkQueueWaitIdle failed";
    }
    return "unknown offscreen error";
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

OffscreenTarget::~OffscreenTarget() {
    destroy();
}

OffscreenTarget::OffscreenTarget(OffscreenTarget&& other) noexcept {
    *this = std::move(other);
}

OffscreenTarget& OffscreenTarget::operator=(OffscreenTarget&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    imageMemory_ = std::exchange(other.imageMemory_, VK_NULL_HANDLE);
    imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
    renderPass_ = std::exchange(other.renderPass_, VK_NULL_HANDLE);
    framebuffer_ = std::exchange(other.framebuffer_, VK_NULL_HANDLE);
    readbackBuffer_ = std::exchange(other.readbackBuffer_, VK_NULL_HANDLE);
    readbackMemory_ = std::exchange(other.readbackMemory_, VK_NULL_HANDLE);
    readbackMapped_ = std::exchange(other.readbackMapped_, nullptr);
    commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
    commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
    extent_ = std::exchange(other.extent_, mdux::core::Extent2D{});
    return *this;
}

void OffscreenTarget::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    // Reverse creation order, every handle nulled, tolerant of any subset being null - which is
    // what lets create() return early on any failure and leave the local's destructor to clean up.
    if (commandPool_ != VK_NULL_HANDLE) {
        // Frees the command buffer allocated from it; no separate vkFreeCommandBuffers needed.
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }
    if (readbackMapped_ != nullptr) {
        vkUnmapMemory(device_, readbackMemory_);
        readbackMapped_ = nullptr;
    }
    if (readbackBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, readbackBuffer_, nullptr);
        readbackBuffer_ = VK_NULL_HANDLE;
    }
    if (readbackMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, readbackMemory_, nullptr);
        readbackMemory_ = VK_NULL_HANDLE;
    }
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (imageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (imageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, imageMemory_, nullptr);
        imageMemory_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

Result<OffscreenTarget, OffscreenError> OffscreenTarget::create(
    VkDevice device, VkPhysicalDevice physicalDevice, mdux::core::Extent2D extent,
    std::uint32_t queueFamilyIndex) noexcept {
    if (device == VK_NULL_HANDLE) {
        return err(OffscreenError::NullDevice);
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        return err(OffscreenError::NullPhysicalDevice);
    }
    if (extent.width <= 0 || extent.height <= 0) {
        return err(OffscreenError::EmptyExtent);
    }
    const auto pixels = static_cast<std::uint64_t>(extent.width) *
                        static_cast<std::uint64_t>(extent.height);
    if (pixels > maxPixels) {
        return err(OffscreenError::ExtentTooLarge);
    }

    OffscreenTarget target;
    target.device_ = device;
    target.extent_ = extent;

    const auto width = static_cast<std::uint32_t>(extent.width);
    const auto height = static_cast<std::uint32_t>(extent.height);

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = colorFormat,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(device, &imageInfo, nullptr, &target.image_) != VK_SUCCESS) {
        return err(OffscreenError::ImageCreationFailed);
    }

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device, target.image_, &imageRequirements);
    const auto imageType = findMemoryType(physicalDevice, imageRequirements.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!imageType.has_value()) {
        return err(OffscreenError::NoSuitableMemoryType);
    }
    const VkMemoryAllocateInfo imageAllocate{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                             .pNext = nullptr,
                                             .allocationSize = imageRequirements.size,
                                             .memoryTypeIndex = *imageType};
    if (vkAllocateMemory(device, &imageAllocate, nullptr, &target.imageMemory_) != VK_SUCCESS) {
        return err(OffscreenError::MemoryAllocationFailed);
    }
    if (vkBindImageMemory(device, target.image_, target.imageMemory_, 0) != VK_SUCCESS) {
        return err(OffscreenError::MemoryAllocationFailed);
    }

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = target.image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = colorFormat,
        .components = {},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(device, &viewInfo, nullptr, &target.imageView_) != VK_SUCCESS) {
        return err(OffscreenError::ImageViewCreationFailed);
    }

    // finalLayout is TRANSFER_SRC_OPTIMAL, so the render pass itself leaves the image ready to
    // copy. That removes a manual pipeline barrier, and with it the commonest way to get a
    // readback that races the rendering it is supposed to observe.
    const VkAttachmentDescription attachment{
        .flags = 0,
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    const VkAttachmentReference colorReference{
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{.flags = 0,
                                       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       .inputAttachmentCount = 0,
                                       .pInputAttachments = nullptr,
                                       .colorAttachmentCount = 1,
                                       .pColorAttachments = &colorReference,
                                       .pResolveAttachments = nullptr,
                                       .pDepthStencilAttachment = nullptr,
                                       .preserveAttachmentCount = 0,
                                       .pPreserveAttachments = nullptr};
    // Makes the copy that follows the render pass wait for the colour writes to complete.
    const VkSubpassDependency dependency{
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dependencyFlags = 0};
    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency};
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &target.renderPass_) != VK_SUCCESS) {
        return err(OffscreenError::RenderPassCreationFailed);
    }

    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = target.renderPass_,
        .attachmentCount = 1,
        .pAttachments = &target.imageView_,
        .width = width,
        .height = height,
        .layers = 1};
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &target.framebuffer_) !=
        VK_SUCCESS) {
        return err(OffscreenError::FramebufferCreationFailed);
    }

    const VkDeviceSize readbackBytes = pixels * 4;
    const VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .size = readbackBytes,
                                        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                        .queueFamilyIndexCount = 0,
                                        .pQueueFamilyIndices = nullptr};
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &target.readbackBuffer_) != VK_SUCCESS) {
        return err(OffscreenError::BufferCreationFailed);
    }
    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(device, target.readbackBuffer_, &bufferRequirements);
    const auto bufferType =
        findMemoryType(physicalDevice, bufferRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!bufferType.has_value()) {
        return err(OffscreenError::NoSuitableMemoryType);
    }
    const VkMemoryAllocateInfo bufferAllocate{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                              .pNext = nullptr,
                                              .allocationSize = bufferRequirements.size,
                                              .memoryTypeIndex = *bufferType};
    if (vkAllocateMemory(device, &bufferAllocate, nullptr, &target.readbackMemory_) !=
        VK_SUCCESS) {
        return err(OffscreenError::MemoryAllocationFailed);
    }
    if (vkBindBufferMemory(device, target.readbackBuffer_, target.readbackMemory_, 0) !=
        VK_SUCCESS) {
        return err(OffscreenError::MemoryAllocationFailed);
    }
    if (vkMapMemory(device, target.readbackMemory_, 0, VK_WHOLE_SIZE, 0,
                    &target.readbackMapped_) != VK_SUCCESS) {
        return err(OffscreenError::MemoryMapFailed);
    }

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex};
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &target.commandPool_) != VK_SUCCESS) {
        return err(OffscreenError::CommandPoolCreationFailed);
    }
    const VkCommandBufferAllocateInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = target.commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    if (vkAllocateCommandBuffers(device, &commandInfo, &target.commandBuffer_) != VK_SUCCESS) {
        return err(OffscreenError::CommandBufferAllocationFailed);
    }

    return target;
}

// ---------------------------------------------------------------------------
// renderAndRead()
// ---------------------------------------------------------------------------

Result<std::span<const mdux::core::ColorRgba8>, OffscreenError> OffscreenTarget::renderAndRead(
    VkQueue queue, mdux::core::ColorRgba8 clear, RecordCommands record, void* context) noexcept {
    if (queue == VK_NULL_HANDLE) {
        return err(OffscreenError::NullQueue);
    }

    vkResetCommandBuffer(commandBuffer_, 0);
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr};
    if (vkBeginCommandBuffer(commandBuffer_, &begin) != VK_SUCCESS) {
        return err(OffscreenError::BeginCommandBufferFailed);
    }

    // The clear colour goes through the same 0..1 normalisation the shader's outputs do, so an
    // expected colour written as ColorRgba8 compares equal to a cleared pixel exactly.
    const VkClearValue clearValue{.color = {{static_cast<float>(clear.r) / 255.0F,
                                             static_cast<float>(clear.g) / 255.0F,
                                             static_cast<float>(clear.b) / 255.0F,
                                             static_cast<float>(clear.a) / 255.0F}}};
    const VkRenderPassBeginInfo renderPassBegin{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = renderPass_,
        .framebuffer = framebuffer_,
        .renderArea = {{0, 0},
                       {static_cast<std::uint32_t>(extent_.width),
                        static_cast<std::uint32_t>(extent_.height)}},
        .clearValueCount = 1,
        .pClearValues = &clearValue};
    vkCmdBeginRenderPass(commandBuffer_, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    if (record != nullptr) {
        record(commandBuffer_, context);
    }

    vkCmdEndRenderPass(commandBuffer_);

    const VkBufferImageCopy copy{
        .bufferOffset = 0,
        // Zero means tightly packed at the image's width, which is what pixelAt() assumes.
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {static_cast<std::uint32_t>(extent_.width),
                        static_cast<std::uint32_t>(extent_.height), 1}};
    vkCmdCopyImageToBuffer(commandBuffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_, 1, &copy);

    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        return err(OffscreenError::EndCommandBufferFailed);
    }

    const VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .waitSemaphoreCount = 0,
                              .pWaitSemaphores = nullptr,
                              .pWaitDstStageMask = nullptr,
                              .commandBufferCount = 1,
                              .pCommandBuffers = &commandBuffer_,
                              .signalSemaphoreCount = 0,
                              .pSignalSemaphores = nullptr};
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        return err(OffscreenError::SubmitFailed);
    }
    // Wait rather than fence-and-poll: a test that polls is a test that can hang, and there is no
    // frame rate to keep up with here.
    if (vkQueueWaitIdle(queue) != VK_SUCCESS) {
        return err(OffscreenError::WaitFailed);
    }

    const auto pixels = static_cast<std::size_t>(extent_.width) *
                        static_cast<std::size_t>(extent_.height);
    return std::span<const mdux::core::ColorRgba8>{
        reinterpret_cast<const mdux::core::ColorRgba8*>(readbackMapped_), pixels};
}

std::optional<mdux::core::ColorRgba8> OffscreenTarget::pixelAt(mdux::core::Px x,
                                                               mdux::core::Px y) const noexcept {
    if (readbackMapped_ == nullptr || x < 0 || y < 0 || x >= extent_.width || y >= extent_.height) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(extent_.width) +
                       static_cast<std::size_t>(x);
    return reinterpret_cast<const mdux::core::ColorRgba8*>(readbackMapped_)[index];
}

}  // namespace mdux::render
