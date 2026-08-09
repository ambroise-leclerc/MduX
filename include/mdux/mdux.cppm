/**
 * @file mdux.cppm
 * @brief MduX facade: version, compliance metadata and Vulkan capability reporting.
 *
 * What is left here after issue #127 retired the HTML/CSS runtime path. `MedicalUiRenderer`,
 * `MedicalUiConfig`, `MedicalUiContent`, `UiFileWatcher`, `UiReloadEvent` and `RenderStatistics`
 * are gone: the renderer recorded no Vulkan commands and never created a pipeline, and the
 * "UI definition" was an HTML string nothing parsed. Deleting them is the point of #13 rather
 * than a side effect of it - there is now a renderer that actually draws.
 *
 * Rendering lives in `mdux.draw` (governed, no Vulkan) and `mdux.render.vulkan` (adapter).
 * `VulkanContext` went with them, superseded by `mdux::render::VulkanRenderContext`, which
 * initialises its members - the old one did not, so an uninitialised instance read as a plausible
 * device and passed validation right up until the first real call faulted.
 *
 * This module keeps only what an application asks MduX *about* rather than asks it *to do*.
 */

module;

// Pure Vulkan integration - no windowing dependencies
#include <vulkan/vulkan.h>  // Use C API for maximum compatibility
#include <stdint.h>         // For uint32_t and other integer types

export module mdux;

// Import standard library modules (C++23 approach)
import std;

// Builds Version::getString() from the same MDUX_VERSION_* definitions that back
// major/minor/patch (see configure_medical_compliance() in cmake/CompilerSettings.cmake),
// so the string can never drift from those values the way a separately hardcoded
// literal did.
#define MDUX_STRINGIFY_DETAIL(x) #x
#define MDUX_STRINGIFY(x) MDUX_STRINGIFY_DETAIL(x)
#define MDUX_VERSION_STRING \
    MDUX_STRINGIFY(MDUX_VERSION_MAJOR) "." MDUX_STRINGIFY(MDUX_VERSION_MINOR) "." MDUX_STRINGIFY(MDUX_VERSION_PATCH)

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
    static constexpr std::string_view getString() noexcept { return MDUX_VERSION_STRING; }
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
 * @brief Initialize MduX library
 * @return true if initialization successful, false otherwise
 */
bool initialize() noexcept;

/**
 * @brief Shutdown MduX library
 */
void shutdown() noexcept;

} // export namespace mdux