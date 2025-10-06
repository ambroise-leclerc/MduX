/**
 * @file DeviceObjectManager.cpp
 * @brief Implementation of device-lifetime object manager for Vulkan SC
 */

module;

#include <vulkan/vulkan.h>
#include <cstdint>

module mdux.vulkansc.objects;

import std;
import mdux.vulkansc.memory;  // For MedicalApplicationProfile

using namespace std;

namespace mdux::vulkansc {

//=============================================================================
// ObjectReservationConfiguration Implementation
//=============================================================================

VkDeviceObjectReservationCreateInfo ObjectReservationConfiguration::toVulkanStruct() const {
    VkDeviceObjectReservationCreateInfo reservationInfo{};
    reservationInfo.sType = VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO;
    reservationInfo.pNext = nullptr;

    // Pipeline reservations
    reservationInfo.pipelineCacheCreateInfoCount = pipelineCacheCount;
    reservationInfo.pipelinePoolSizeCount = pipelinePoolSize;
    reservationInfo.graphicsPipelineRequestCount = graphicsPipelineCount;
    reservationInfo.computePipelineRequestCount = computePipelineCount;

    // Memory reservations
    reservationInfo.deviceMemoryRequestCount = deviceMemoryCount;
    reservationInfo.maxMemoryAllocationSize = maxMemoryAllocationSize;

    // Buffer reservations
    reservationInfo.bufferRequestCount = bufferCount;
    reservationInfo.bufferViewRequestCount = bufferViewCount;

    // Image reservations
    reservationInfo.imageRequestCount = imageCount;
    reservationInfo.imageViewRequestCount = imageViewCount;
    reservationInfo.layeredImageViewRequestCount = layeredImageViewCount;
    reservationInfo.samplerRequestCount = samplerCount;

    // Descriptor reservations
    reservationInfo.descriptorSetLayoutRequestCount = descriptorSetLayoutCount;
    reservationInfo.pipelineLayoutRequestCount = pipelineLayoutCount;
    reservationInfo.descriptorPoolRequestCount = descriptorPoolCount;
    reservationInfo.descriptorSetRequestCount = descriptorSetCount;

    // Render pass reservations
    reservationInfo.renderPassRequestCount = renderPassCount;
    reservationInfo.framebufferRequestCount = framebufferCount;

    // Command buffer reservations
    reservationInfo.commandPoolRequestCount = commandPoolCount;
    reservationInfo.commandBufferRequestCount = commandBufferCount;

    // Synchronization reservations
    reservationInfo.semaphoreRequestCount = semaphoreCount;
    reservationInfo.fenceRequestCount = fenceCount;
    reservationInfo.eventRequestCount = eventCount;
    reservationInfo.queryPoolRequestCount = queryPoolCount;

    return reservationInfo;
}

//=============================================================================
// DeviceObjectManager Implementation
//=============================================================================

DeviceObjectManager::~DeviceObjectManager() {
    cleanup();
}

DeviceObjectManager::DeviceObjectManager(DeviceObjectManager&& other) noexcept
    : device(other.device)
    , reservations(other.reservations)
    , initialized(other.initialized)
    , commandPools(move(other.commandPools))
    , descriptorPools(move(other.descriptorPools))
    , queryPools(move(other.queryPools))
    , buffers(move(other.buffers))
    , images(move(other.images))
    , imageViews(move(other.imageViews))
    , samplers(move(other.samplers))
    , renderPasses(move(other.renderPasses))
    , framebuffers(move(other.framebuffers))
    , objectAuditTrail(move(other.objectAuditTrail))
    , statistics(other.statistics)
{
    other.device = VK_NULL_HANDLE;
    other.initialized = false;
}

DeviceObjectManager& DeviceObjectManager::operator=(DeviceObjectManager&& other) noexcept {
    if (this != &other) {
        cleanup();

        device = other.device;
        reservations = other.reservations;
        initialized = other.initialized;
        commandPools = move(other.commandPools);
        descriptorPools = move(other.descriptorPools);
        queryPools = move(other.queryPools);
        buffers = move(other.buffers);
        images = move(other.images);
        imageViews = move(other.imageViews);
        samplers = move(other.samplers);
        renderPasses = move(other.renderPasses);
        framebuffers = move(other.framebuffers);
        objectAuditTrail = move(other.objectAuditTrail);
        statistics = other.statistics;

        other.device = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
}

void DeviceObjectManager::initialize(VkDevice dev,
                                    const ObjectReservationConfiguration& config) {
    if (initialized) {
        throw runtime_error("DeviceObjectManager already initialized");
    }

    if (!config.isValid()) {
        throw runtime_error("Invalid ObjectReservationConfiguration");
    }

    device = dev;
    reservations = config;

    // Pre-reserve capacity for vectors
    commandPools.reserve(config.commandPoolCount);
    descriptorPools.reserve(config.descriptorPoolCount);
    queryPools.reserve(config.queryPoolCount);
    buffers.reserve(config.bufferCount);
    images.reserve(config.imageCount);
    imageViews.reserve(config.imageViewCount);
    samplers.reserve(config.samplerCount);
    renderPasses.reserve(config.renderPassCount);
    framebuffers.reserve(config.framebufferCount);

    initialized = true;
}

//=============================================================================
// Device-Lifetime Object Creation (Cannot Destroy!)
//=============================================================================

VkCommandPool DeviceObjectManager::createCommandPool(uint32_t queueFamilyIndex,
                                                     VkCommandPoolCreateFlags flags) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    // Check reservation limit
    if (commandPools.size() >= reservations.commandPoolCount) {
        throw runtime_error(
            "Command pool count exceeded: " + to_string(commandPools.size()) +
            " >= " + to_string(reservations.commandPoolCount)
        );
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = flags;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateCommandPool failed with result " + to_string(result));
    }

    commandPools.push_back(commandPool);
    recordObjectCreation("VkCommandPool", reinterpret_cast<uint64_t>(commandPool), true);

    statistics.commandPoolsCreated++;

    return commandPool;
}

VkDescriptorPool DeviceObjectManager::createDescriptorPool(
    const vector<VkDescriptorPoolSize>& poolSizes,
    uint32_t maxSets,
    VkDescriptorPoolCreateFlags flags)
{
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    // Check reservation limit
    if (descriptorPools.size() >= reservations.descriptorPoolCount) {
        throw runtime_error(
            "Descriptor pool count exceeded: " + to_string(descriptorPools.size()) +
            " >= " + to_string(reservations.descriptorPoolCount)
        );
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    poolInfo.flags = flags;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateDescriptorPool failed with result " + to_string(result));
    }

    descriptorPools.push_back(descriptorPool);
    recordObjectCreation("VkDescriptorPool", reinterpret_cast<uint64_t>(descriptorPool), true);

    statistics.descriptorPoolsCreated++;

    return descriptorPool;
}

VkQueryPool DeviceObjectManager::createQueryPool(VkQueryType queryType, uint32_t queryCount) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    // Check reservation limit
    if (queryPools.size() >= reservations.queryPoolCount) {
        throw runtime_error(
            "Query pool count exceeded: " + to_string(queryPools.size()) +
            " >= " + to_string(reservations.queryPoolCount)
        );
    }

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = queryType;
    poolInfo.queryCount = queryCount;

    VkQueryPool queryPool = VK_NULL_HANDLE;
    VkResult result = vkCreateQueryPool(device, &poolInfo, nullptr, &queryPool);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateQueryPool failed with result " + to_string(result));
    }

    queryPools.push_back(queryPool);
    recordObjectCreation("VkQueryPool", reinterpret_cast<uint64_t>(queryPool), true);

    statistics.queryPoolsCreated++;

    return queryPool;
}

//=============================================================================
// Regular Object Creation (Can Destroy)
//=============================================================================

VkBuffer DeviceObjectManager::createBuffer(VkDeviceSize size,
                                          VkBufferUsageFlags usage,
                                          VkSharingMode sharingMode) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    // Check reservation limit
    if (buffers.size() >= reservations.bufferCount) {
        throw runtime_error(
            "Buffer count exceeded: " + to_string(buffers.size()) +
            " >= " + to_string(reservations.bufferCount)
        );
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = sharingMode;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateBuffer failed with result " + to_string(result));
    }

    BufferRecord record{
        .handle = buffer,
        .size = size,
        .usage = usage,
        .creationTime = chrono::system_clock::now()
    };

    buffers.push_back(record);
    recordObjectCreation("VkBuffer", reinterpret_cast<uint64_t>(buffer), false);

    statistics.buffersActive++;
    if (statistics.buffersActive > statistics.peakBuffers) {
        statistics.peakBuffers = statistics.buffersActive;
    }

    return buffer;
}

VkImage DeviceObjectManager::createImage(const VkImageCreateInfo& createInfo) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    if (images.size() >= reservations.imageCount) {
        throw runtime_error(
            "Image count exceeded: " + to_string(images.size()) +
            " >= " + to_string(reservations.imageCount)
        );
    }

    VkImage image = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(device, &createInfo, nullptr, &image);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateImage failed with result " + to_string(result));
    }

    ImageRecord record{
        .handle = image,
        .extent = createInfo.extent,
        .format = createInfo.format,
        .creationTime = chrono::system_clock::now()
    };

    images.push_back(record);
    recordObjectCreation("VkImage", reinterpret_cast<uint64_t>(image), false);

    statistics.imagesActive++;
    if (statistics.imagesActive > statistics.peakImages) {
        statistics.peakImages = statistics.imagesActive;
    }

    return image;
}

VkImageView DeviceObjectManager::createImageView(const VkImageViewCreateInfo& createInfo) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    if (imageViews.size() >= reservations.imageViewCount) {
        throw runtime_error(
            "Image view count exceeded: " + to_string(imageViews.size()) +
            " >= " + to_string(reservations.imageViewCount)
        );
    }

    VkImageView imageView = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(device, &createInfo, nullptr, &imageView);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateImageView failed with result " + to_string(result));
    }

    imageViews.push_back(imageView);
    recordObjectCreation("VkImageView", reinterpret_cast<uint64_t>(imageView), false);

    statistics.imageViewsActive++;
    if (statistics.imageViewsActive > statistics.peakImageViews) {
        statistics.peakImageViews = statistics.imageViewsActive;
    }

    return imageView;
}

VkSampler DeviceObjectManager::createSampler(const VkSamplerCreateInfo& createInfo) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    if (samplers.size() >= reservations.samplerCount) {
        throw runtime_error(
            "Sampler count exceeded: " + to_string(samplers.size()) +
            " >= " + to_string(reservations.samplerCount)
        );
    }

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device, &createInfo, nullptr, &sampler);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateSampler failed with result " + to_string(result));
    }

    samplers.push_back(sampler);
    recordObjectCreation("VkSampler", reinterpret_cast<uint64_t>(sampler), false);

    statistics.samplersActive++;

    return sampler;
}

VkRenderPass DeviceObjectManager::createRenderPass(const VkRenderPassCreateInfo& createInfo) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    if (renderPasses.size() >= reservations.renderPassCount) {
        throw runtime_error(
            "Render pass count exceeded: " + to_string(renderPasses.size()) +
            " >= " + to_string(reservations.renderPassCount)
        );
    }

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkResult result = vkCreateRenderPass(device, &createInfo, nullptr, &renderPass);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateRenderPass failed with result " + to_string(result));
    }

    renderPasses.push_back(renderPass);
    recordObjectCreation("VkRenderPass", reinterpret_cast<uint64_t>(renderPass), false);

    statistics.renderPassesActive++;

    return renderPass;
}

VkFramebuffer DeviceObjectManager::createFramebuffer(const VkFramebufferCreateInfo& createInfo) {
    lock_guard<mutex> lock(objectMutex);

    if (!initialized) {
        throw runtime_error("DeviceObjectManager not initialized");
    }

    if (framebuffers.size() >= reservations.framebufferCount) {
        throw runtime_error(
            "Framebuffer count exceeded: " + to_string(framebuffers.size()) +
            " >= " + to_string(reservations.framebufferCount)
        );
    }

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkResult result = vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffer);

    if (result != VK_SUCCESS) {
        throw runtime_error("vkCreateFramebuffer failed with result " + to_string(result));
    }

    framebuffers.push_back(framebuffer);
    recordObjectCreation("VkFramebuffer", reinterpret_cast<uint64_t>(framebuffer), false);

    statistics.framebuffersActive++;

    return framebuffer;
}

//=============================================================================
// Object Destruction (Regular Objects Only!)
//=============================================================================

void DeviceObjectManager::destroyBuffer(VkBuffer buffer) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        removeBuffer(buffer);
        if (statistics.buffersActive > 0) {
            statistics.buffersActive--;
        }
    }
}

void DeviceObjectManager::destroyImage(VkImage image) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        removeImage(image);
        if (statistics.imagesActive > 0) {
            statistics.imagesActive--;
        }
    }
}

void DeviceObjectManager::destroyImageView(VkImageView imageView) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
        removeImageView(imageView);
        if (statistics.imageViewsActive > 0) {
            statistics.imageViewsActive--;
        }
    }
}

void DeviceObjectManager::destroySampler(VkSampler sampler) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        samplers.erase(remove(samplers.begin(), samplers.end(), sampler), samplers.end());
        if (statistics.samplersActive > 0) {
            statistics.samplersActive--;
        }
    }
}

void DeviceObjectManager::destroyRenderPass(VkRenderPass renderPass) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPasses.erase(remove(renderPasses.begin(), renderPasses.end(), renderPass),
                          renderPasses.end());
        if (statistics.renderPassesActive > 0) {
            statistics.renderPassesActive--;
        }
    }
}

void DeviceObjectManager::destroyFramebuffer(VkFramebuffer framebuffer) noexcept {
    lock_guard<mutex> lock(objectMutex);

    if (device != VK_NULL_HANDLE && framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffers.erase(remove(framebuffers.begin(), framebuffers.end(), framebuffer),
                          framebuffers.end());
        if (statistics.framebuffersActive > 0) {
            statistics.framebuffersActive--;
        }
    }
}

//=============================================================================
// Query and Validation
//=============================================================================

VkCommandPool DeviceObjectManager::getCommandPool(uint32_t index) const noexcept {
    if (index < commandPools.size()) {
        return commandPools[index];
    }
    return VK_NULL_HANDLE;
}

VkDescriptorPool DeviceObjectManager::getDescriptorPool(uint32_t index) const noexcept {
    if (index < descriptorPools.size()) {
        return descriptorPools[index];
    }
    return VK_NULL_HANDLE;
}

bool DeviceObjectManager::validateObjectCounts() const noexcept {
    return commandPools.size() <= reservations.commandPoolCount &&
           descriptorPools.size() <= reservations.descriptorPoolCount &&
           queryPools.size() <= reservations.queryPoolCount &&
           buffers.size() <= reservations.bufferCount &&
           images.size() <= reservations.imageCount &&
           imageViews.size() <= reservations.imageViewCount &&
           samplers.size() <= reservations.samplerCount &&
           renderPasses.size() <= reservations.renderPassCount &&
           framebuffers.size() <= reservations.framebufferCount;
}

DeviceObjectManager::ObjectStatistics DeviceObjectManager::getStatistics() const noexcept {
    lock_guard<mutex> lock(objectMutex);
    return statistics;
}

string DeviceObjectManager::generateAuditReport() const {
    lock_guard<mutex> lock(objectMutex);

    ostringstream report;
    report << "{\n";
    report << "  \"deviceObjectManager\": {\n";
    report << "    \"deviceLifetimeObjects\": {\n";
    report << "      \"commandPools\": " << commandPools.size() << ",\n";
    report << "      \"descriptorPools\": " << descriptorPools.size() << ",\n";
    report << "      \"queryPools\": " << queryPools.size() << "\n";
    report << "    },\n";
    report << "    \"regularObjects\": {\n";
    report << "      \"buffersActive\": " << statistics.buffersActive << ",\n";
    report << "      \"imagesActive\": " << statistics.imagesActive << ",\n";
    report << "      \"imageViewsActive\": " << statistics.imageViewsActive << ",\n";
    report << "      \"samplersActive\": " << statistics.samplersActive << ",\n";
    report << "      \"renderPassesActive\": " << statistics.renderPassesActive << ",\n";
    report << "      \"framebuffersActive\": " << statistics.framebuffersActive << "\n";
    report << "    },\n";
    report << "    \"peakCounts\": {\n";
    report << "      \"peakBuffers\": " << statistics.peakBuffers << ",\n";
    report << "      \"peakImages\": " << statistics.peakImages << ",\n";
    report << "      \"peakImageViews\": " << statistics.peakImageViews << "\n";
    report << "    }\n";
    report << "  }\n";
    report << "}\n";

    return report.str();
}

void DeviceObjectManager::cleanup() noexcept {
    if (!initialized || device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device);

    // Destroy regular objects first
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();

    for (auto renderPass : renderPasses) {
        vkDestroyRenderPass(device, renderPass, nullptr);
    }
    renderPasses.clear();

    for (auto sampler : samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    samplers.clear();

    for (auto imageView : imageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    imageViews.clear();

    for (const auto& imageRecord : images) {
        vkDestroyImage(device, imageRecord.handle, nullptr);
    }
    images.clear();

    for (const auto& bufferRecord : buffers) {
        vkDestroyBuffer(device, bufferRecord.handle, nullptr);
    }
    buffers.clear();

    // NOW destroy device-lifetime objects - only time allowed in Vulkan SC!
    for (auto queryPool : queryPools) {
        vkDestroyQueryPool(device, queryPool, nullptr);
    }
    queryPools.clear();

    for (auto descriptorPool : descriptorPools) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    descriptorPools.clear();

    for (auto commandPool : commandPools) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }
    commandPools.clear();

    objectAuditTrail.clear();
    initialized = false;
}

//=============================================================================
// Private Helper Methods
//=============================================================================

void DeviceObjectManager::recordObjectCreation(const string& objectType,
                                              uint64_t handle,
                                              bool isDeviceLifetime) {
    ObjectCreationRecord record{
        .objectType = objectType,
        .handle = handle,
        .creationTime = chrono::system_clock::now(),
        .isDeviceLifetime = isDeviceLifetime
    };

    objectAuditTrail.push_back(record);
}

void DeviceObjectManager::updateStatistics() {
    // Statistics updated in creation/destruction methods
}

void DeviceObjectManager::removeBuffer(VkBuffer buffer) noexcept {
    buffers.erase(
        remove_if(buffers.begin(), buffers.end(),
                 [buffer](const BufferRecord& r) { return r.handle == buffer; }),
        buffers.end()
    );
}

void DeviceObjectManager::removeImage(VkImage image) noexcept {
    images.erase(
        remove_if(images.begin(), images.end(),
                 [image](const ImageRecord& r) { return r.handle == image; }),
        images.end()
    );
}

void DeviceObjectManager::removeImageView(VkImageView imageView) noexcept {
    imageViews.erase(remove(imageViews.begin(), imageViews.end(), imageView), imageViews.end());
}

//=============================================================================
// ObjectReservationCalculator Implementation
//=============================================================================

ObjectReservationConfiguration ObjectReservationCalculator::calculate(
    const MedicalApplicationProfile& profile)
{
    ObjectReservationConfiguration config;

    // Apply safety margin to all counts
    float margin = profile.safetyMarginMultiplier;

    // Pipeline reservations (conservative estimates)
    config.pipelineCacheCount = 1;
    config.pipelinePoolSize = applySafetyMargin(20, margin);
    config.graphicsPipelineCount = applySafetyMargin(
        profile.maxConcurrentScreens * 3, margin);  // ~3 pipelines per screen
    config.computePipelineCount = applySafetyMargin(5, margin);

    // Memory reservations
    config.deviceMemoryCount = applySafetyMargin(15, margin);
    config.maxMemoryAllocationSize = 256 * 1024 * 1024;  // 256 MB

    // Buffer reservations
    config.bufferCount = applySafetyMargin(
        profile.maxUIElementsPerScreen * profile.maxConcurrentScreens / 2, margin);
    config.bufferViewCount = applySafetyMargin(20, margin);

    // Image reservations
    config.imageCount = applySafetyMargin(
        profile.maxConcurrentImages + profile.maxTextureAtlases, margin);
    config.imageViewCount = applySafetyMargin(config.imageCount * 2, margin);
    config.layeredImageViewCount = applySafetyMargin(10, margin);
    config.samplerCount = applySafetyMargin(profile.maxTextureAtlases + 5, margin);

    // Descriptor reservations
    config.descriptorSetLayoutCount = applySafetyMargin(10, margin);
    config.pipelineLayoutCount = applySafetyMargin(8, margin);
    config.descriptorPoolCount = 3;  // Device-lifetime, keep minimal
    config.descriptorSetCount = applySafetyMargin(50, margin);

    // Render pass reservations
    config.renderPassCount = applySafetyMargin(5, margin);
    config.framebufferCount = applySafetyMargin(profile.maxFramesInFlight * 3, margin);

    // Command buffer reservations
    config.commandPoolCount = 3;  // Device-lifetime, keep minimal
    config.commandBufferCount = applySafetyMargin(50, margin);

    // Synchronization
    config.semaphoreCount = applySafetyMargin(profile.maxFramesInFlight * 4, margin);
    config.fenceCount = applySafetyMargin(profile.maxFramesInFlight * 4, margin);
    config.eventCount = applySafetyMargin(10, margin);
    config.queryPoolCount = applySafetyMargin(5, margin);

    return config;
}

} // namespace mdux::vulkansc
