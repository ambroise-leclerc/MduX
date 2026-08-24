/**
 * @file MemoryPoolManager.cpp
 * @brief Implementation of static memory pool manager for Vulkan SC
 */

module;

#include <vulkan/vulkan.h>
#include <ctime>  // For ctime_s/ctime_r

module mdux.vulkansc.memory;

import std;

using namespace std;

namespace mdux::vulkansc {

//=============================================================================
// MemoryPoolManager Implementation
//=============================================================================

MemoryPoolManager::~MemoryPoolManager() {
    cleanup();
}

MemoryPoolManager::MemoryPoolManager(MemoryPoolManager&& other) noexcept
    : device(other.device)
    , physicalDevice(other.physicalDevice)
    , config(std::move(other.config))
    , initialized(other.initialized)
    , pools(std::move(other.pools))
    , allocationHistory(std::move(other.allocationHistory))
    , statistics(other.statistics)
{
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
    other.initialized = false;
}

MemoryPoolManager& MemoryPoolManager::operator=(MemoryPoolManager&& other) noexcept {
    if (this != &other) {
        cleanup();

        device = other.device;
        physicalDevice = other.physicalDevice;
        config = std::move(other.config);
        initialized = other.initialized;
        pools = std::move(other.pools);
        allocationHistory = std::move(other.allocationHistory);
        statistics = other.statistics;

        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
}

void MemoryPoolManager::initialize(VkDevice dev,
                                   VkPhysicalDevice physDev,
                                   const MemoryPoolConfiguration& cfg) {
    if (initialized) {
        throw runtime_error("MemoryPoolManager already initialized");
    }

    if (!cfg.isValid()) {
        throw runtime_error("Invalid MemoryPoolConfiguration");
    }

    device = dev;
    physicalDevice = physDev;
    config = cfg;

    // Initialize memory pools based on configuration
    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        pools[i].totalReserved = config.poolSizes[i];
        pools[i].memoryTypeIndex = i;
        pools[i].allocations.reserve(config.maxAllocationsPerType[i]);
    }

    initialized = true;

    // Initialize statistics
    statistics.totalMemoryReserved = config.maxTotalMemory;
}

VkDeviceMemory MemoryPoolManager::allocate(VkDeviceSize size, uint32_t memoryTypeIndex) {
    lock_guard<mutex> lock(allocationMutex);

    if (!initialized) {
        throw runtime_error("MemoryPoolManager not initialized");
    }

    if (!isValidMemoryType(memoryTypeIndex)) {
        throw runtime_error("Invalid memory type index: " + to_string(memoryTypeIndex));
    }

    auto& pool = pools[memoryTypeIndex];

    // Check if allocation fits in pool
    if (pool.totalAllocated + size > pool.totalReserved) {
        throw runtime_error(
            "Memory pool exhausted for type " + to_string(memoryTypeIndex) +
            ": requested " + to_string(size) + " bytes, " +
            "available " + to_string(pool.getRemainingCapacity()) + " bytes"
        );
    }

    // Check allocation count limit
    if (pool.allocationCount >= config.maxAllocationsPerType[memoryTypeIndex]) {
        throw runtime_error(
            "Memory pool allocation count exceeded for type " + to_string(memoryTypeIndex) +
            ": max " + to_string(config.maxAllocationsPerType[memoryTypeIndex])
        );
    }

    // Allocate memory from Vulkan SC
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);

    if (result != VK_SUCCESS) {
        throw runtime_error(
            "vkAllocateMemory failed with result " + to_string(result)
        );
    }

    // Update pool tracking
    pool.allocations.push_back(memory);
    pool.totalAllocated += size;
    pool.allocationCount++;

    // Record allocation for audit trail
    AllocationRecord record{
        .memory = memory,
        .size = size,
        .memoryTypeIndex = memoryTypeIndex,
        .timestamp = chrono::system_clock::now(),
        .inUse = true,
        .allocatedFrom = "MemoryPoolManager::allocate"
    };
    allocationHistory.push_back(record);

    // Update statistics
    updateStatistics(size, memoryTypeIndex);

    return memory;
}

void MemoryPoolManager::markUnused(VkDeviceMemory memory) noexcept {
    lock_guard<mutex> lock(allocationMutex);

    // Find allocation record
    for (auto& record : allocationHistory) {
        if (record.memory == memory) {
            record.inUse = false;
            break;
        }
    }

    // NOTE: We do NOT call vkFreeMemory() - Vulkan SC prohibits this!
    // Memory remains allocated until device destruction
}

VkDeviceSize MemoryPoolManager::getRemainingCapacity(uint32_t memoryTypeIndex) const noexcept {
    if (!isValidMemoryType(memoryTypeIndex)) {
        return 0;
    }
    return pools[memoryTypeIndex].getRemainingCapacity();
}

VkDeviceSize MemoryPoolManager::getTotalAllocated(uint32_t memoryTypeIndex) const noexcept {
    if (!isValidMemoryType(memoryTypeIndex)) {
        return 0;
    }
    return pools[memoryTypeIndex].totalAllocated;
}

bool MemoryPoolManager::validateCapacity() const noexcept {
    for (const auto& pool : pools) {
        if (pool.totalAllocated > pool.totalReserved) {
            return false;
        }
    }
    return true;
}

MemoryPoolManager::AllocationStatistics MemoryPoolManager::getStatistics() const noexcept {
    lock_guard<mutex> lock(allocationMutex);
    return statistics;
}

string MemoryPoolManager::generateAuditReport() const {
    lock_guard<mutex> lock(allocationMutex);

    ostringstream report;
    report << "{\n";
    report << "  \"memoryPoolManager\": {\n";
    report << "    \"safetyClass\": \"" << config.safetyClass << "\",\n";
    report << "    \"totalMemoryReserved\": " << config.maxTotalMemory << ",\n";
    report << "    \"totalMemoryAllocated\": " << statistics.totalMemoryAllocated << ",\n";
    report << "    \"utilizationPercentage\": " << statistics.utilizationPercentage << ",\n";
    report << "    \"totalAllocations\": " << statistics.totalAllocations << ",\n";
    report << "    \"peakAllocations\": " << statistics.peakAllocations << ",\n";
    report << "    \"memoryPools\": [\n";

    bool firstPool = true;
    for (size_t i = 0; i < pools.size(); ++i) {
        const auto& pool = pools[i];
        if (pool.totalReserved > 0) {
            if (!firstPool) report << ",\n";
            firstPool = false;

            report << "      {\n";
            report << "        \"memoryType\": " << i << ",\n";
            report << "        \"reserved\": " << pool.totalReserved << ",\n";
            report << "        \"allocated\": " << pool.totalAllocated << ",\n";
            report << "        \"utilization\": " << pool.getUtilization() << ",\n";
            report << "        \"allocationCount\": " << pool.allocationCount << "\n";
            report << "      }";
        }
    }

    report << "\n    ],\n";
    report << "    \"allocationHistory\": [\n";

    bool firstAlloc = true;
    for (const auto& record : allocationHistory) {
        if (!firstAlloc) report << ",\n";
        firstAlloc = false;

        report << "      {\n";
        report << "        \"size\": " << record.size << ",\n";
        report << "        \"memoryType\": " << record.memoryTypeIndex << ",\n";
        report << "        \"inUse\": " << (record.inUse ? "true" : "false") << ",\n";

        auto timestamp = chrono::system_clock::to_time_t(record.timestamp);
        char timeStr[26];
        #ifdef _WIN32
        ctime_s(timeStr, sizeof(timeStr), &timestamp);
        #else
        ctime_r(&timestamp, timeStr);
        #endif
        report << "        \"timestamp\": \"" << timeStr << "\",\n";
        report << "        \"source\": \"" << record.allocatedFrom << "\"\n";
        report << "      }";
    }

    report << "\n    ]\n";
    report << "  }\n";
    report << "}\n";

    return report.str();
}

void MemoryPoolManager::cleanup() noexcept {
    if (!initialized || device == VK_NULL_HANDLE) {
        return;
    }

    // Wait for device to be idle before cleanup
    vkDeviceWaitIdle(device);

    // NOW we can free memory - this is the ONLY time in Vulkan SC
    for (auto& pool : pools) {
        for (auto memory : pool.allocations) {
            vkFreeMemory(device, memory, nullptr);
        }
        pool.allocations.clear();
    }

    allocationHistory.clear();
    initialized = false;
}

void MemoryPoolManager::updateStatistics(VkDeviceSize size, uint32_t /*memoryTypeIndex*/) noexcept {
    statistics.totalAllocations++;
    statistics.totalMemoryAllocated += size;
    statistics.lastAllocation = chrono::system_clock::now();

    if (statistics.totalAllocations > statistics.peakAllocations) {
        statistics.peakAllocations = statistics.totalAllocations;
    }

    if (statistics.totalMemoryReserved > 0) {
        statistics.utilizationPercentage =
            (static_cast<float>(statistics.totalMemoryAllocated) /
             static_cast<float>(statistics.totalMemoryReserved)) * 100.0f;
    }
}

//=============================================================================
// MemoryPoolCalculator Implementation
//=============================================================================

MemoryPoolConfiguration MemoryPoolCalculator::calculate(
    const MedicalApplicationProfile& profile,
    VkPhysicalDevice physicalDevice)
{
    MemoryPoolConfiguration config;
    config.safetyClass = "Class B"; // Default to Class B
    config.safetyMargin = profile.safetyMarginMultiplier;

    // Calculate total memory requirements
    VkDeviceSize geometryMemory = estimateGeometryMemory(profile);
    VkDeviceSize uniformMemory = estimateUniformMemory(profile);
    VkDeviceSize textureMemory = estimateTextureMemory(profile);
    VkDeviceSize framebufferMemory = estimateFramebufferMemory(profile);

    VkDeviceSize totalRequired = geometryMemory + uniformMemory +
                                 textureMemory + framebufferMemory;

    // Apply safety margin
    totalRequired = applySafetyMargin(totalRequired, profile);

    // Apply redundancy if required
    if (profile.requiresRedundancy) {
        totalRequired *= 2;
    }

    config.maxTotalMemory = totalRequired;
    config.maxTotalAllocations = 100; // Conservative estimate

    // Distribute memory across memory types
    if (physicalDevice != VK_NULL_HANDLE) {
        // Use actual physical device memory properties
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            VkMemoryPropertyFlags props = memProperties.memoryTypes[i].propertyFlags;

            if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                // Device-local memory for textures and framebuffers
                config.poolSizes[i] = static_cast<VkDeviceSize>(static_cast<double>(totalRequired) * 0.8);
                config.maxAllocationsPerType[i] = 50;
            }
            else if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                // Host-visible for staging and uniform buffers
                config.poolSizes[i] = static_cast<VkDeviceSize>(static_cast<double>(totalRequired) * 0.2);
                config.maxAllocationsPerType[i] = 30;
            }
            else {
                config.poolSizes[i] = 0;
                config.maxAllocationsPerType[i] = 0;
            }
        }
    } else {
        // Testing mode: Use default distribution without querying device
        // Assume 2 memory types: device-local (index 0) and host-visible (index 1)
        config.poolSizes[0] = static_cast<VkDeviceSize>(static_cast<double>(totalRequired) * 0.8);  // Device-local
        config.maxAllocationsPerType[0] = 50;

        config.poolSizes[1] = static_cast<VkDeviceSize>(static_cast<double>(totalRequired) * 0.2);  // Host-visible
        config.maxAllocationsPerType[1] = 30;
    }

    return config;
}

VkDeviceSize MemoryPoolCalculator::estimateGeometryMemory(
    const MedicalApplicationProfile& profile) noexcept
{
    // Estimate vertex and index buffer memory
    VkDeviceSize verticesPerScreen = profile.maxUIElementsPerScreen * 4; // Quad per element
    VkDeviceSize indicesPerScreen = profile.maxUIElementsPerScreen * 6;  // Two triangles per quad

    VkDeviceSize vertexMemory = verticesPerScreen * BYTES_PER_VERTEX * profile.maxConcurrentScreens;
    VkDeviceSize indexMemory = indicesPerScreen * BYTES_PER_INDEX * profile.maxConcurrentScreens;

    return vertexMemory + indexMemory;
}

VkDeviceSize MemoryPoolCalculator::estimateUniformMemory(
    const MedicalApplicationProfile& profile) noexcept
{
    // Uniform buffer per frame in flight
    return UNIFORM_BUFFER_SIZE * profile.maxFramesInFlight;
}

VkDeviceSize MemoryPoolCalculator::estimateTextureMemory(
    const MedicalApplicationProfile& profile) noexcept
{
    // Texture atlases
    VkDeviceSize atlasSize = 2048 * 2048 * 4; // 2K RGBA atlas
    VkDeviceSize atlasMemory = atlasSize * profile.maxTextureAtlases;

    // Medical images
    VkDeviceSize imageSize = profile.maxImageResolution * profile.maxImageResolution * 4;
    VkDeviceSize imageMemory = imageSize * profile.maxConcurrentImages * profile.maxImageLayers;

    return atlasMemory + imageMemory;
}

VkDeviceSize MemoryPoolCalculator::estimateFramebufferMemory(
    const MedicalApplicationProfile& profile) noexcept
{
    // Assume 1920x1080 framebuffers
    VkDeviceSize framebufferSize = 1920 * 1080 * 4; // RGBA
    return framebufferSize * profile.maxFramesInFlight * 2; // Double-buffered
}

VkDeviceSize MemoryPoolCalculator::applySafetyMargin(
    VkDeviceSize baseSize,
    const MedicalApplicationProfile& profile) noexcept
{
    return static_cast<VkDeviceSize>(static_cast<double>(baseSize) * static_cast<double>(profile.safetyMarginMultiplier));
}

} // namespace mdux::vulkansc
