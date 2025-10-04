/**
 * @file MemoryPoolManager.cppm
 * @brief Static memory pool manager for Vulkan SC medical device applications
 *
 * Manages pre-reserved memory pools that cannot be freed at runtime.
 * All memory is allocated at device creation and persists until device destruction.
 *
 * @compliance IEC 62304 Class C - Deterministic memory management
 * @compliance ISO 14971 - Risk control through static allocation
 * @compliance Vulkan SC 1.0 - Device-lifetime memory requirement
 */

module;

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>

export module mdux.vulkansc.memory;

import std;

using namespace std;

export namespace mdux::vulkansc {

//=============================================================================
// Memory Pool Configuration
//=============================================================================

/**
 * @brief Medical device application memory profile
 *
 * Defines memory requirements for different medical device use cases.
 * Used to calculate appropriate Vulkan SC memory reservations.
 */
struct MedicalApplicationProfile {
    // UI rendering requirements
    uint32_t maxConcurrentScreens = 1;        ///< Max simultaneous UI screens
    uint32_t maxUIElementsPerScreen = 100;    ///< Max UI elements per screen
    uint32_t maxTextureAtlases = 5;           ///< Max texture atlases

    // Medical imaging requirements
    uint32_t maxConcurrentImages = 5;         ///< Max concurrent medical images
    uint32_t maxImageResolution = 2048;       ///< Max image dimension
    uint32_t maxImageLayers = 1;              ///< Max image array layers

    // Performance requirements
    uint32_t targetFrameRate = 60;            ///< Target FPS
    uint32_t maxFramesInFlight = 2;           ///< Max frames in flight

    // Safety requirements
    float safetyMarginMultiplier = 2.0f;      ///< Safety margin for Class B/C (2.0 = 100%)
    bool requiresRedundancy = false;          ///< Redundant memory allocation
};

/**
 * @brief Memory pool configuration for Vulkan SC device creation
 *
 * Specifies memory reservations that cannot be exceeded at runtime.
 * All values must be calculated before device creation.
 */
struct MemoryPoolConfiguration {
    // Per-memory-type reservations (VK_MAX_MEMORY_TYPES = 32)
    array<VkDeviceSize, VK_MAX_MEMORY_TYPES> poolSizes{};

    // Maximum number of allocations per memory type
    array<uint32_t, VK_MAX_MEMORY_TYPES> maxAllocationsPerType{};

    // Overall limits
    VkDeviceSize maxTotalMemory = 0;          ///< Total memory across all types
    uint32_t maxTotalAllocations = 0;         ///< Total allocation count

    // Safety classification
    string safetyClass = "Class B";           ///< IEC 62304 safety class
    float safetyMargin = 2.0f;                ///< Safety margin multiplier

    /**
     * @brief Validate configuration completeness
     * @return true if configuration is valid
     */
    bool isValid() const noexcept {
        return maxTotalMemory > 0 && maxTotalAllocations > 0;
    }
};

//=============================================================================
// Memory Pool Manager
//=============================================================================

/**
 * @brief Static memory pool manager for Vulkan SC
 *
 * Key Vulkan SC Constraints:
 * - Cannot call vkFreeMemory() at runtime
 * - All memory pre-reserved at device creation
 * - Memory persists until device destruction
 * - Allocations from pre-reserved pools only
 *
 * @warning This class enforces Vulkan SC memory lifetime rules.
 *          Attempting to free memory will throw an exception.
 */
class MemoryPoolManager {
public:
    MemoryPoolManager() = default;
    ~MemoryPoolManager();

    // Non-copyable, movable
    MemoryPoolManager(const MemoryPoolManager&) = delete;
    MemoryPoolManager& operator=(const MemoryPoolManager&) = delete;
    MemoryPoolManager(MemoryPoolManager&&) noexcept;
    MemoryPoolManager& operator=(MemoryPoolManager&&) noexcept;

    /**
     * @brief Initialize memory pools at device creation
     *
     * CRITICAL: Must be called during device creation, before any memory allocations.
     * Memory pools cannot be resized after device creation.
     *
     * @param device Vulkan SC device
     * @param physicalDevice Vulkan SC physical device
     * @param config Memory pool configuration
     * @throws runtime_error if initialization fails
     *
     * @compliance Vulkan SC 1.0 - Device-lifetime memory requirement
     */
    void initialize(VkDevice device,
                   VkPhysicalDevice physicalDevice,
                   const MemoryPoolConfiguration& config);

    /**
     * @brief Allocate memory from pre-reserved pool
     *
     * Allocates from static memory pool. Memory CANNOT be freed.
     *
     * @param size Allocation size in bytes
     * @param memoryTypeIndex Memory type index from vkGetPhysicalDeviceMemoryProperties
     * @return VkDeviceMemory handle
     * @throws runtime_error if pool exhausted or allocation fails
     *
     * @note Memory persists until device destruction
     * @warning DO NOT call vkFreeMemory() on returned handle
     */
    VkDeviceMemory allocate(VkDeviceSize size, uint32_t memoryTypeIndex);

    /**
     * @brief Mark memory as unused (for tracking only)
     *
     * Does NOT free memory - Vulkan SC prohibits vkFreeMemory().
     * Only updates internal tracking for capacity monitoring.
     *
     * @param memory Memory to mark as unused
     * @note Memory remains allocated until device destruction
     */
    void markUnused(VkDeviceMemory memory) noexcept;

    /**
     * @brief Get remaining capacity in memory pool
     *
     * @param memoryTypeIndex Memory type index
     * @return Remaining bytes available in pool
     */
    VkDeviceSize getRemainingCapacity(uint32_t memoryTypeIndex) const noexcept;

    /**
     * @brief Get total allocated memory
     *
     * @param memoryTypeIndex Memory type index
     * @return Total bytes allocated from pool
     */
    VkDeviceSize getTotalAllocated(uint32_t memoryTypeIndex) const noexcept;

    /**
     * @brief Validate all allocations fit within reservations
     *
     * @return true if all allocations within capacity
     */
    bool validateCapacity() const noexcept;

    /**
     * @brief Get allocation statistics for medical device audit
     *
     * @return Allocation statistics for regulatory compliance
     */
    struct AllocationStatistics {
        uint32_t totalAllocations = 0;
        VkDeviceSize totalMemoryAllocated = 0;
        VkDeviceSize totalMemoryReserved = 0;
        float utilizationPercentage = 0.0f;
        uint32_t peakAllocations = 0;
        chrono::system_clock::time_point lastAllocation;
    };

    AllocationStatistics getStatistics() const noexcept;

    /**
     * @brief Generate regulatory audit report
     *
     * @return JSON-formatted audit trail for IEC 62304 compliance
     */
    string generateAuditReport() const;

    /**
     * @brief Cleanup all device-lifetime memory
     *
     * Called automatically at device destruction.
     * This is the ONLY time memory can be freed in Vulkan SC.
     */
    void cleanup() noexcept;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    MemoryPoolConfiguration config;
    bool initialized = false;

    /**
     * @brief Memory pool for a specific memory type
     */
    struct MemoryPool {
        vector<VkDeviceMemory> allocations;  ///< All allocations (device-lifetime)
        VkDeviceSize totalReserved = 0;      ///< Total reserved capacity
        VkDeviceSize totalAllocated = 0;     ///< Total allocated so far
        uint32_t memoryTypeIndex = 0;        ///< Memory type index
        uint32_t allocationCount = 0;        ///< Number of allocations

        /**
         * @brief Get remaining capacity
         */
        VkDeviceSize getRemainingCapacity() const noexcept {
            return totalReserved > totalAllocated ?
                   totalReserved - totalAllocated : 0;
        }

        /**
         * @brief Get utilization percentage
         */
        float getUtilization() const noexcept {
            return totalReserved > 0 ?
                   (static_cast<float>(totalAllocated) / static_cast<float>(totalReserved)) * 100.0f :
                   0.0f;
        }
    };

    array<MemoryPool, VK_MAX_MEMORY_TYPES> pools;

    /**
     * @brief Allocation record for audit trail (IEC 62304)
     */
    struct AllocationRecord {
        VkDeviceMemory memory;
        VkDeviceSize size;
        uint32_t memoryTypeIndex;
        chrono::system_clock::time_point timestamp;
        bool inUse;
        string allocatedFrom;  // Function/location for traceability
    };

    vector<AllocationRecord> allocationHistory;
    mutable mutex allocationMutex;  // Thread-safety for allocations

    // Statistics
    mutable AllocationStatistics statistics;

    /**
     * @brief Update statistics after allocation
     */
    void updateStatistics(VkDeviceSize size, uint32_t memoryTypeIndex) noexcept;

    /**
     * @brief Validate memory type index
     */
    bool isValidMemoryType(uint32_t memoryTypeIndex) const noexcept {
        return memoryTypeIndex < VK_MAX_MEMORY_TYPES;
    }
};

//=============================================================================
// Memory Pool Calculator
//=============================================================================

/**
 * @brief Calculate memory pool configuration from application profile
 *
 * Analyzes medical device requirements and generates appropriate
 * memory reservations for Vulkan SC device creation.
 */
class MemoryPoolCalculator {
public:
    /**
     * @brief Calculate memory configuration from application profile
     *
     * @param profile Medical application profile
     * @param physicalDevice Vulkan physical device for memory properties
     * @return Memory pool configuration for device creation
     */
    static MemoryPoolConfiguration calculate(
        const MedicalApplicationProfile& profile,
        VkPhysicalDevice physicalDevice
    );

    /**
     * @brief Estimate vertex/index buffer memory
     */
    static VkDeviceSize estimateGeometryMemory(
        const MedicalApplicationProfile& profile
    ) noexcept;

    /**
     * @brief Estimate uniform buffer memory
     */
    static VkDeviceSize estimateUniformMemory(
        const MedicalApplicationProfile& profile
    ) noexcept;

    /**
     * @brief Estimate texture memory
     */
    static VkDeviceSize estimateTextureMemory(
        const MedicalApplicationProfile& profile
    ) noexcept;

    /**
     * @brief Estimate framebuffer memory
     */
    static VkDeviceSize estimateFramebufferMemory(
        const MedicalApplicationProfile& profile
    ) noexcept;

    /**
     * @brief Apply safety margin based on IEC 62304 class
     */
    static VkDeviceSize applySafetyMargin(
        VkDeviceSize baseSize,
        const MedicalApplicationProfile& profile
    ) noexcept;

private:
    static constexpr VkDeviceSize BYTES_PER_VERTEX = 32;  // Position + Normal + UV
    static constexpr VkDeviceSize BYTES_PER_INDEX = 2;     // uint16_t
    static constexpr VkDeviceSize UNIFORM_BUFFER_SIZE = 256; // MVP matrices
};

} // namespace mdux::vulkansc
