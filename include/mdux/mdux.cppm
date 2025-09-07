/**
 * @brief MduX C++23 Module Interface - Medical Device User eXperience Library
 * 
 * Pure Vulkan complement library for medical device user interfaces.
 * Integrates with existing Vulkan applications to provide compliant UI rendering
 * for Class B and Class C medical devices.
 */

module;

// Pure Vulkan integration - no windowing dependencies
#include <vulkan/vulkan.h>  // Use C API for maximum compatibility
#include <stdint.h>         // For uint32_t and other integer types

export module mdux;

// Import standard library modules (C++23 approach)
import std;

// Forward declarations for medical UI system
export namespace mdux {
    /**
     * @brief Medical UI content structure for HTML/CSS-based interfaces
     */
    struct MedicalUiContent {
        std::string identifier;        ///< Unique identifier for traceability
        std::string htmlContent;      ///< HTML interface definition
        std::string cssContent;       ///< CSS styling information
        std::vector<std::string> validationErrors; ///< Compliance validation errors
        std::string version;          ///< Content version for medical traceability
        
        bool isValid() const noexcept { return validationErrors.empty(); }
        bool hasContent() const noexcept { return !htmlContent.empty(); }
    };
    
    /**
     * @brief Hot-reload event for development workflows
     */
    struct UiReloadEvent {
        std::filesystem::path filePath;
        MedicalUiContent uiContent;
        bool contentChanged = false;
        std::string errorMessage;
        std::chrono::system_clock::time_point timestamp;
        
        bool isSuccess() const noexcept { return errorMessage.empty(); }
    };
    
    /**
     * @brief File change callback for hot-reload functionality
     */
    using UiChangeCallback = std::function<void(const UiReloadEvent&)>;
    
    /**
     * @brief Hot-reload file watcher for development
     */
    class UiFileWatcher {
    public:
        UiFileWatcher() = default;
        ~UiFileWatcher();
        
        // Non-copyable but movable
        UiFileWatcher(const UiFileWatcher&) = delete;
        UiFileWatcher& operator=(const UiFileWatcher&) = delete;
        UiFileWatcher(UiFileWatcher&&) noexcept;
        UiFileWatcher& operator=(UiFileWatcher&&) noexcept;
        
        bool startWatching(const std::filesystem::path& filePath, UiChangeCallback callback);
        void stopWatching();
        bool isWatching() const noexcept { return watching; }
        MedicalUiContent loadContent(const std::filesystem::path& filePath);
        
    private:
        std::filesystem::path watchedFile;
        UiChangeCallback changeCallback;
        std::atomic<bool> watching{false};
        std::atomic<bool> shouldStop{false};
        std::thread watchThread;
        std::filesystem::file_time_type lastWriteTime;
        
        void watchLoop();
        std::filesystem::file_time_type getFileTime(const std::filesystem::path& path);
    };
}

export namespace mdux {

//=============================================================================
// Version and Compliance Information
//=============================================================================

/**
 * @brief Version information for MduX library
 */
struct Version {
    static constexpr std::uint32_t major = MDUX_VERSION_MAJOR;
    static constexpr std::uint32_t minor = MDUX_VERSION_MINOR;
    static constexpr std::uint32_t patch = MDUX_VERSION_PATCH;

    /**
     * @brief Get version string in format "major.minor.patch"
     * @return Version string
     */
    static constexpr std::string_view getString() noexcept { return "0.1.0"; }
};

/**
 * @brief Medical device compliance information
 */
struct Compliance {
    static constexpr bool isMedicalDeviceCompliant = MDUX_MEDICAL_DEVICE_COMPLIANCE;
    static constexpr std::string_view standards = "IEC 62304, IEC 62366";
    static constexpr std::string_view safetyClass = "Class B/C Medical Device Software";
};

/**
 * @brief Vulkan integration capabilities
 */
struct VulkanSupport {
    static constexpr bool isAvailable = true;
    
    /**
     * @brief Get available Vulkan API version string
     * @return Vulkan version string (e.g., "Vulkan 1.3")
     */
    static std::string getApiVersion() noexcept {
        uint32_t apiVersion = 0;
        if (vkEnumerateInstanceVersion(&apiVersion) == VK_SUCCESS) {
            uint32_t major = VK_VERSION_MAJOR(apiVersion);
            uint32_t minor = VK_VERSION_MINOR(apiVersion);
            return "Vulkan " + std::to_string(major) + "." + std::to_string(minor);
        }
        return "Vulkan 1.3"; // Fallback for older implementations
    }
    
    static constexpr std::string_view api = "Vulkan";
    static constexpr std::uint32_t requiredVersionMajor = 1;
    static constexpr std::uint32_t requiredVersionMinor = 3;
    static constexpr std::uint32_t requiredVersionPatch = 0;
    
    /**
     * @brief Check if a Vulkan device supports MduX requirements
     * @param physicalDevice Vulkan physical device to check
     * @return true if device is suitable for medical UI rendering
     */
    static bool isDeviceSuitable(VkPhysicalDevice physicalDevice) noexcept;
};

//=============================================================================
// Vulkan Integration Types
//=============================================================================

/**
 * @brief Vulkan rendering context provided by the user application
 */
struct VulkanContext {
    VkDevice device;                   ///< Vulkan logical device
    VkPhysicalDevice physicalDevice;   ///< Vulkan physical device  
    VkCommandBuffer commandBuffer;     ///< Active command buffer for recording
    VkRenderPass renderPass;          ///< Compatible render pass
    VkExtent2D renderExtent;          ///< Render area dimensions
    uint32_t currentFrame;            ///< Current frame index
    float deltaTime;                  ///< Time since last frame (seconds)
    
    /**
     * @brief Validate that context has all required handles
     * @return true if context is valid for MduX rendering
     */
    bool isValid() const noexcept {
        return device != VK_NULL_HANDLE && 
               physicalDevice != VK_NULL_HANDLE &&
               commandBuffer != VK_NULL_HANDLE && 
               renderPass != VK_NULL_HANDLE;
    }
};

/**
 * @brief Medical device compliance metadata
 */
struct ComplianceMetadata {
    std::string deviceClass;           ///< Medical device class (A/B/C)
    std::string standardsCompliance;   ///< Standards compliance (e.g., "IEC 62304, IEC 62366")
    std::string version;              ///< Software version for traceability
    std::string buildId;              ///< Build identifier for regulatory tracking
    bool auditTrailEnabled = true;    ///< Enable audit trail logging
    
    /**
     * @brief Check if compliance metadata is complete
     */
    bool isComplete() const noexcept {
        return !deviceClass.empty() && !standardsCompliance.empty() && !version.empty();
    }
};

//=============================================================================
// Medical UI Rendering System
//=============================================================================

/**
 * @brief Medical UI rendering configuration
 */
struct MedicalUiConfig {
    std::filesystem::path uiDefinitionPath;   ///< Path to HTML/CSS UI definition
    ComplianceMetadata compliance;            ///< Medical device compliance metadata
    bool enableHotReload = false;             ///< Enable hot-reload for development
    bool enableValidation = true;             ///< Enable medical compliance validation
    std::string rendererId;                   ///< Unique renderer identifier for traceability
    
    /**
     * @brief Check if configuration is valid
     */
    bool isValid() const noexcept {
        return !uiDefinitionPath.empty() && 
               compliance.isComplete() && 
               !rendererId.empty();
    }
};

/**
 * @brief Medical UI render statistics for compliance monitoring
 */
struct RenderStatistics {
    uint64_t frameCount = 0;              ///< Total frames rendered
    float averageFrameTime = 0.0f;       ///< Average frame time in milliseconds
    uint64_t validationErrors = 0;       ///< Count of validation errors
    std::chrono::system_clock::time_point lastRender; ///< Timestamp of last render
    
    /**
     * @brief Update statistics with new frame timing
     */
    void updateFrame(float frameTime) noexcept {
        frameCount++;
        averageFrameTime = (averageFrameTime * static_cast<float>(frameCount - 1) + frameTime) / static_cast<float>(frameCount);
        lastRender = std::chrono::system_clock::now();
    }
};

//=============================================================================
// Medical UI Renderer Core
//=============================================================================

class MedicalUiRenderer {
private:
    // Core Vulkan resources (provided by user)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // MduX-managed resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    
    // UI content and configuration
    MedicalUiConfig config;
    MedicalUiContent currentContent;
    std::unique_ptr<UiFileWatcher> fileWatcher;
    RenderStatistics statistics;
    
    // Compliance and validation
    std::vector<std::string> validationErrors;
    bool complianceValidated = false;
    
public:
    /**
     * @brief Create medical UI renderer for existing Vulkan application
     * @param vulkanContext User's Vulkan context
     * @param config Medical UI configuration
     */
    explicit MedicalUiRenderer(const VulkanContext& vulkanContext, const MedicalUiConfig& config);
    
    /**
     * @brief Destructor - cleanup MduX resources
     */
    ~MedicalUiRenderer();
    
    // Non-copyable but movable
    MedicalUiRenderer(const MedicalUiRenderer&) = delete;
    MedicalUiRenderer& operator=(const MedicalUiRenderer&) = delete;
    MedicalUiRenderer(MedicalUiRenderer&& other) noexcept;
    MedicalUiRenderer& operator=(MedicalUiRenderer&& other) noexcept;
    
    /**
     * @brief Load UI definition from file
     * @param filePath Path to HTML/CSS UI definition
     * @return true if loaded successfully
     */
    bool loadUiDefinition(const std::filesystem::path& filePath);
    
    /**
     * @brief Render medical UI into user's command buffer
     * @param context Current Vulkan rendering context
     * @return true if rendered successfully
     */
    bool render(const VulkanContext& context);
    
    /**
     * @brief Update UI content programmatically
     * @param content New UI content
     * @return true if updated successfully
     */
    bool updateContent(const MedicalUiContent& content);
    
    /**
     * @brief Enable/disable hot-reload for development
     * @param enable Enable hot-reload functionality
     * @return true if hot-reload state changed successfully
     */
    bool setHotReloadEnabled(bool enable);
    
    /**
     * @brief Get current UI content
     * @return Current medical UI content
     */
    const MedicalUiContent& getCurrentContent() const noexcept { return currentContent; }
    
    /**
     * @brief Get render statistics for compliance monitoring
     * @return Current render statistics
     */
    const RenderStatistics& getStatistics() const noexcept { return statistics; }
    
    /**
     * @brief Get compliance metadata
     * @return Medical device compliance metadata
     */
    const ComplianceMetadata& getCompliance() const noexcept { return config.compliance; }
    
    /**
     * @brief Validate medical compliance
     * @return true if all compliance requirements are met
     */
    bool validateCompliance();
    
    /**
     * @brief Get validation errors
     * @return List of current validation errors
     */
    const std::vector<std::string>& getValidationErrors() const noexcept { return validationErrors; }
    
private:
    /**
     * @brief Initialize Vulkan resources for UI rendering
     * @param renderPass Compatible render pass from user
     * @return true if initialization successful
     */
    bool initializeVulkanResources(VkRenderPass renderPass);
    
    /**
     * @brief Cleanup MduX Vulkan resources
     */
    void cleanupVulkanResources();
    
    /**
     * @brief Create descriptor set layout for UI rendering
     * @return true if successful
     */
    bool createDescriptorSetLayout();
    
    /**
     * @brief Create graphics pipeline for UI rendering
     * @param renderPass Compatible render pass
     * @return true if successful
     */
    bool createGraphicsPipeline(VkRenderPass renderPass);
    
    /**
     * @brief Handle hot-reload events
     * @param event UI reload event
     */
    void onHotReload(const UiReloadEvent& event);
    
    /**
     * @brief Validate medical device compliance requirements
     * @return true if compliant
     */
    bool performComplianceValidation();
};

//=============================================================================
// Library Management Functions
//=============================================================================

/**
 * @brief Initialize MduX library
 * @return true if initialization successful, false otherwise
 */
bool initialize() noexcept;

/**
 * @brief Shutdown MduX library
 */
void shutdown() noexcept;

} // export namespace mdux