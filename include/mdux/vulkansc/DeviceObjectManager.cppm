/**
 * @file DeviceObjectManager.cppm
 * @brief Manager for device-lifetime objects in Vulkan SC
 *
 * Manages objects that cannot be destroyed until device destruction:
 * - VkDeviceMemory (cannot call vkFreeMemory)
 * - VkCommandPool (cannot call vkDestroyCommandPool)
 * - VkDescriptorPool (cannot call vkDestroyDescriptorPool)
 * - VkQueryPool (cannot call vkDestroyQueryPool)
 * - VkSwapchainKHR (cannot call vkDestroySwapchainKHR)
 *
 * @compliance IEC 62304 Class C - Object lifetime management
 * @compliance Vulkan SC 1.0 - Device-lifetime object requirement
 */

module;

#include <vulkan/vulkan.h>
#include <cstdint>

export module mdux.vulkansc.objects;

import std;

using namespace std;

export namespace mdux::vulkansc {

// Forward declaration of Vulkan SC types (not in standard Vulkan headers)
// TODO: Replace with actual Vulkan SC headers when available
inline constexpr VkStructureType VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO = static_cast<VkStructureType>(1000298000);

struct VkDeviceObjectReservationCreateInfo {
    VkStructureType sType;
    const void* pNext;
    uint32_t pipelineCacheCreateInfoCount;
    uint32_t pipelinePoolSizeCount;
    uint32_t semaphoreRequestCount;
    uint32_t commandBufferRequestCount;
    uint32_t fenceRequestCount;
    uint32_t deviceMemoryRequestCount;
    VkDeviceSize maxMemoryAllocationSize;
    uint32_t bufferRequestCount;
    uint32_t imageRequestCount;
    uint32_t bufferViewRequestCount;
    uint32_t imageViewRequestCount;
    uint32_t layeredImageViewRequestCount;
    uint32_t eventRequestCount;
    uint32_t queryPoolRequestCount;
    uint32_t descriptorSetLayoutRequestCount;
    uint32_t samplerRequestCount;
    uint32_t pipelineLayoutRequestCount;
    uint32_t renderPassRequestCount;
    uint32_t framebufferRequestCount;
    uint32_t graphicsPipelineRequestCount;
    uint32_t computePipelineRequestCount;
    uint32_t descriptorPoolRequestCount;
    uint32_t descriptorSetRequestCount;
    uint32_t commandPoolRequestCount;
    uint32_t descriptorSetLayoutBindingRequestCount;
    uint32_t descriptorSetLayoutBindingLimit;
    uint32_t maxImageViewMipLevels;
    uint32_t maxImageViewArrayLayers;
    uint32_t maxLayeredImageViewMipLevels;
    uint32_t maxOcclusionQueriesPerPool;
    uint32_t maxPipelineStatisticsQueriesPerPool;
    uint32_t maxTimestampQueriesPerPool;
    uint32_t maxImmutableSamplersPerDescriptorSetLayout;
};



//=============================================================================
// Object Reservation Configuration
//=============================================================================

/**
 * @brief Object reservation counts for Vulkan SC device creation
 *
 * All counts must be specified at device creation and cannot be exceeded.
 * Used to build VkDeviceObjectReservationCreateInfo.
 */
struct ObjectReservationConfiguration {
    // Pipeline reservations
    uint32_t pipelineCacheCount = 1;
    uint32_t pipelinePoolSize = 20;
    uint32_t graphicsPipelineCount = 15;
    uint32_t computePipelineCount = 5;

    // Memory reservations (device-lifetime!)
    uint32_t deviceMemoryCount = 15;
    VkDeviceSize maxMemoryAllocationSize = 128 * 1024 * 1024;  // 128 MB

    // Buffer reservations
    uint32_t bufferCount = 50;
    uint32_t bufferViewCount = 20;

    // Image reservations
    uint32_t imageCount = 30;
    uint32_t imageViewCount = 60;
    uint32_t layeredImageViewCount = 10;
    uint32_t samplerCount = 15;

    // Descriptor reservations
    uint32_t descriptorSetLayoutCount = 10;
    uint32_t pipelineLayoutCount = 8;
    uint32_t descriptorPoolCount = 3;      // Device-lifetime!
    uint32_t descriptorSetCount = 50;

    // Render pass reservations
    uint32_t renderPassCount = 5;
    uint32_t framebufferCount = 15;

    // Command buffer reservations
    uint32_t commandPoolCount = 3;         // Device-lifetime!
    uint32_t commandBufferCount = 50;

    // Synchronization reservations
    uint32_t semaphoreCount = 20;
    uint32_t fenceCount = 20;
    uint32_t eventCount = 10;
    uint32_t queryPoolCount = 5;          // Device-lifetime!

    // Swapchain reservations (if applicable)
    uint32_t swapchainCount = 1;          // Device-lifetime!

    /**
     * @brief Validate configuration completeness
     */
    bool isValid() const noexcept {
        return pipelineCacheCount > 0 && commandPoolCount > 0;
    }

    /**
     * @brief Convert to VkDeviceObjectReservationCreateInfo
     */
    VkDeviceObjectReservationCreateInfo toVulkanStruct() const;
};

//=============================================================================
// Device Object Manager
//=============================================================================

/**
 * @brief Manager for device-lifetime objects in Vulkan SC
 *
 * Critical Vulkan SC Constraints:
 * - VkCommandPool: Cannot call vkDestroyCommandPool() at runtime
 * - VkDescriptorPool: Cannot call vkDestroyDescriptorPool() at runtime
 * - VkQueryPool: Cannot call vkDestroyQueryPool() at runtime
 * - VkSwapchainKHR: Cannot call vkDestroySwapchainKHR() at runtime
 * - All pools created once and persist until device destruction
 *
 * @warning Attempting to destroy these objects will result in validation errors
 */
class DeviceObjectManager {
public:
    DeviceObjectManager() = default;
    ~DeviceObjectManager();

    // Non-copyable, movable
    DeviceObjectManager(const DeviceObjectManager&) = delete;
    DeviceObjectManager& operator=(const DeviceObjectManager&) = delete;
    DeviceObjectManager(DeviceObjectManager&&) noexcept;
    DeviceObjectManager& operator=(DeviceObjectManager&&) noexcept;

    /**
     * @brief Initialize with object reservations
     *
     * CRITICAL: Must be called during device creation.
     * Object counts cannot be changed after initialization.
     *
     * @param device Vulkan SC device
     * @param reservations Object count reservations
     * @throws runtime_error if initialization fails
     */
    void initialize(VkDevice device, const ObjectReservationConfiguration& reservations);

    //=========================================================================
    // Device-Lifetime Object Creation (Cannot Destroy!)
    //=========================================================================

    /**
     * @brief Create command pool (device-lifetime)
     *
     * @param queueFamilyIndex Queue family for pool
     * @param flags Command pool creation flags
     * @return Command pool handle
     *
     * @warning Pool persists until device destruction - cannot call vkDestroyCommandPool()!
     */
    VkCommandPool createCommandPool(uint32_t queueFamilyIndex,
                                   VkCommandPoolCreateFlags flags = 0);

    /**
     * @brief Create descriptor pool (device-lifetime)
     *
     * @param poolSizes Descriptor pool sizes
     * @param maxSets Maximum descriptor sets
     * @param flags Descriptor pool creation flags
     * @return Descriptor pool handle
     *
     * @warning Pool persists until device destruction - cannot call vkDestroyDescriptorPool()!
     */
    VkDescriptorPool createDescriptorPool(
        const vector<VkDescriptorPoolSize>& poolSizes,
        uint32_t maxSets,
        VkDescriptorPoolCreateFlags flags = 0
    );

    /**
     * @brief Create query pool (device-lifetime)
     *
     * @param queryType Type of queries
     * @param queryCount Number of queries
     * @return Query pool handle
     *
     * @warning Pool persists until device destruction - cannot call vkDestroyQueryPool()!
     */
    VkQueryPool createQueryPool(VkQueryType queryType, uint32_t queryCount);

    //=========================================================================
    // Regular Object Creation (Can Destroy)
    //=========================================================================

    /**
     * @brief Create buffer (can be destroyed)
     *
     * @param size Buffer size
     * @param usage Buffer usage flags
     * @param sharingMode Sharing mode
     * @return Buffer handle
     */
    VkBuffer createBuffer(VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);

    /**
     * @brief Create image (can be destroyed)
     *
     * @param createInfo Image creation info
     * @return Image handle
     */
    VkImage createImage(const VkImageCreateInfo& createInfo);

    /**
     * @brief Create image view (can be destroyed)
     *
     * @param createInfo Image view creation info
     * @return Image view handle
     */
    VkImageView createImageView(const VkImageViewCreateInfo& createInfo);

    /**
     * @brief Create sampler (can be destroyed)
     *
     * @param createInfo Sampler creation info
     * @return Sampler handle
     */
    VkSampler createSampler(const VkSamplerCreateInfo& createInfo);

    /**
     * @brief Create render pass (can be destroyed)
     *
     * @param createInfo Render pass creation info
     * @return Render pass handle
     */
    VkRenderPass createRenderPass(const VkRenderPassCreateInfo& createInfo);

    /**
     * @brief Create framebuffer (can be destroyed)
     *
     * @param createInfo Framebuffer creation info
     * @return Framebuffer handle
     */
    VkFramebuffer createFramebuffer(const VkFramebufferCreateInfo& createInfo);

    //=========================================================================
    // Object Destruction (Regular Objects Only!)
    //=========================================================================

    /**
     * @brief Destroy buffer
     *
     * @param buffer Buffer to destroy
     * @note Can only destroy regular objects, not device-lifetime pools
     */
    void destroyBuffer(VkBuffer buffer) noexcept;

    /**
     * @brief Destroy image
     *
     * @param image Image to destroy
     */
    void destroyImage(VkImage image) noexcept;

    /**
     * @brief Destroy image view
     *
     * @param imageView Image view to destroy
     */
    void destroyImageView(VkImageView imageView) noexcept;

    /**
     * @brief Destroy sampler
     *
     * @param sampler Sampler to destroy
     */
    void destroySampler(VkSampler sampler) noexcept;

    /**
     * @brief Destroy render pass
     *
     * @param renderPass Render pass to destroy
     */
    void destroyRenderPass(VkRenderPass renderPass) noexcept;

    /**
     * @brief Destroy framebuffer
     *
     * @param framebuffer Framebuffer to destroy
     */
    void destroyFramebuffer(VkFramebuffer framebuffer) noexcept;

    //=========================================================================
    // Query and Validation
    //=========================================================================

    /**
     * @brief Get command pool by index
     *
     * @param index Pool index (0 to commandPoolCount-1)
     * @return Command pool handle or VK_NULL_HANDLE if invalid
     */
    VkCommandPool getCommandPool(uint32_t index) const noexcept;

    /**
     * @brief Get descriptor pool by index
     *
     * @param index Pool index (0 to descriptorPoolCount-1)
     * @return Descriptor pool handle or VK_NULL_HANDLE if invalid
     */
    VkDescriptorPool getDescriptorPool(uint32_t index) const noexcept;

    /**
     * @brief Get number of command pools created
     */
    uint32_t getCommandPoolCount() const noexcept {
        return static_cast<uint32_t>(commandPools.size());
    }

    /**
     * @brief Get number of descriptor pools created
     */
    uint32_t getDescriptorPoolCount() const noexcept {
        return static_cast<uint32_t>(descriptorPools.size());
    }

    /**
     * @brief Validate object counts within reservations
     *
     * @return true if all object counts within limits
     */
    bool validateObjectCounts() const noexcept;

    /**
     * @brief Get object creation statistics
     */
    struct ObjectStatistics {
        // Device-lifetime objects
        uint32_t commandPoolsCreated = 0;
        uint32_t descriptorPoolsCreated = 0;
        uint32_t queryPoolsCreated = 0;

        // Regular objects (current count)
        uint32_t buffersActive = 0;
        uint32_t imagesActive = 0;
        uint32_t imageViewsActive = 0;
        uint32_t samplersActive = 0;
        uint32_t renderPassesActive = 0;
        uint32_t framebuffersActive = 0;

        // Peak counts
        uint32_t peakBuffers = 0;
        uint32_t peakImages = 0;
        uint32_t peakImageViews = 0;
    };

    ObjectStatistics getStatistics() const noexcept;

    /**
     * @brief Generate audit report for IEC 62304 compliance
     *
     * @return JSON-formatted object lifecycle audit trail
     */
    string generateAuditReport() const;

    /**
     * @brief Cleanup all device-lifetime objects
     *
     * Called automatically at device destruction.
     * This is the ONLY time pools can be destroyed in Vulkan SC.
     */
    void cleanup() noexcept;

private:
    VkDevice device = VK_NULL_HANDLE;
    ObjectReservationConfiguration reservations;
    bool initialized = false;

    //=========================================================================
    // Device-Lifetime Objects (Cannot Destroy!)
    //=========================================================================

    vector<VkCommandPool> commandPools;
    vector<VkDescriptorPool> descriptorPools;
    vector<VkQueryPool> queryPools;

    //=========================================================================
    // Regular Objects (Can Destroy)
    //=========================================================================

    struct BufferRecord {
        VkBuffer handle;
        VkDeviceSize size;
        VkBufferUsageFlags usage;
        chrono::system_clock::time_point creationTime;
    };

    struct ImageRecord {
        VkImage handle;
        VkExtent3D extent;
        VkFormat format;
        chrono::system_clock::time_point creationTime;
    };

    vector<BufferRecord> buffers;
    vector<ImageRecord> images;
    vector<VkImageView> imageViews;
    vector<VkSampler> samplers;
    vector<VkRenderPass> renderPasses;
    vector<VkFramebuffer> framebuffers;

    //=========================================================================
    // Audit Trail
    //=========================================================================

    struct ObjectCreationRecord {
        string objectType;
        uint64_t handle;
        chrono::system_clock::time_point creationTime;
        bool isDeviceLifetime;  // true if cannot be destroyed
    };

    vector<ObjectCreationRecord> objectAuditTrail;
    mutable mutex objectMutex;  // Thread-safety

    // Statistics
    mutable ObjectStatistics statistics;

    /**
     * @brief Record object creation for audit trail
     */
    void recordObjectCreation(const string& objectType,
                             uint64_t handle,
                             bool isDeviceLifetime);

    /**
     * @brief Update statistics after object creation
     */
    void updateStatistics();

    /**
     * @brief Remove buffer from tracking
     */
    void removeBuffer(VkBuffer buffer) noexcept;

    /**
     * @brief Remove image from tracking
     */
    void removeImage(VkImage image) noexcept;

    /**
     * @brief Remove image view from tracking
     */
    void removeImageView(VkImageView imageView) noexcept;
};

//=============================================================================
// Object Reservation Calculator
//=============================================================================

/**
 * @brief Calculate object reservations from medical application profile
 */
class ObjectReservationCalculator {
public:
    /**
     * @brief Calculate reservations from medical application profile
     *
     * @param profile Medical application profile
     * @return Object reservation configuration
     */
    static ObjectReservationConfiguration calculate(
        const struct MedicalApplicationProfile& profile
    );

    /**
     * @brief Apply safety margin to object counts
     *
     * @param baseCount Base object count
     * @param safetyMargin Safety margin multiplier
     * @return Adjusted count with safety margin
     */
    static uint32_t applySafetyMargin(uint32_t baseCount, float safetyMargin) noexcept {
        return static_cast<uint32_t>(static_cast<float>(baseCount) * safetyMargin);
    }
};

} // namespace mdux::vulkansc
