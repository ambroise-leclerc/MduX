/**
 * @brief Compliance-related tests for MduX library
 */

import std;
import mdux;

bool testCompliance() {
    // Test medical device compliance flag
    bool complianceEnabled = mdux::Compliance::isMedicalDeviceCompliant;

    // Test standards information
    bool standardsValid = mdux::Compliance::standards == std::string_view("IEC 62304, IEC 62366");

    // Test safety class information
    bool safetyClassValid = mdux::Compliance::safetyClass == std::string_view("Class B/C Medical Device Software");

    // Test Vulkan support
    bool vulkanEnabled = mdux::VulkanSupport::isAvailable;
    bool vulkanApiValid = mdux::VulkanSupport::api == std::string_view("Vulkan");

    return complianceEnabled && standardsValid && safetyClassValid && vulkanEnabled &&
           vulkanApiValid;
}