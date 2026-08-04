/**
 * @brief MduX C++23 Module Implementation - Medical Device User eXperience Library
 * 
 * Pure Vulkan complement library implementation for medical device UI.
 * Integrates with existing Vulkan applications without windowing dependencies.
 */

// Global module fragment for implementation
module;

// Pure Vulkan integration - no windowing dependencies
#include <vulkan/vulkan.h>
#include <cstring>  // For strcmp and memset

// Module declaration
module mdux;

// Import standard library modules (C++23 approach)
import std;

namespace mdux {

//=============================================================================
// VulkanSupport Implementation
//=============================================================================

bool VulkanSupport::isDeviceSuitable(VkPhysicalDevice physicalDevice) noexcept {
    if (physicalDevice == VK_NULL_HANDLE) {
        return false;
    }
    
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    
    // Check API version compatibility
    uint32_t apiVersion = deviceProperties.apiVersion;
    uint32_t major = VK_VERSION_MAJOR(apiVersion);
    uint32_t minor = VK_VERSION_MINOR(apiVersion);
    
    if (major < requiredVersionMajor || 
        (major == requiredVersionMajor && minor < requiredVersionMinor)) {
        return false;
    }
    
    // Check for required features for UI rendering
    if (!deviceFeatures.samplerAnisotropy || !deviceFeatures.fillModeNonSolid) {
        return false;
    }
    
    return true;
}

//=============================================================================
// Library lifecycle
//=============================================================================

static ComplianceMetadata globalCompliance;
static bool libraryInitialized = false;

bool initialize() noexcept {
    if (libraryInitialized) {
        return true;
    }
    
    // Use default compliance metadata
    ComplianceMetadata defaultCompliance;
    defaultCompliance.deviceClass = "Class B";
    defaultCompliance.standardsCompliance = "IEC 62304, IEC 62366";
    defaultCompliance.version = "1.0.0";
    defaultCompliance.auditTrailEnabled = true;
    
    globalCompliance = defaultCompliance;
    libraryInitialized = true;
    
    return true;
}

void shutdown() noexcept {
    libraryInitialized = false;
    globalCompliance = ComplianceMetadata{};
}

} // namespace mdux