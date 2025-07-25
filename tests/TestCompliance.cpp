/**
 * @brief Compliance-related tests for MduX library
 */

#include <mdux/mdux.hpp>

bool testCompliance() {
    // Test medical device compliance flag
    bool complianceEnabled = mdux::Compliance::isMedicalDeviceCompliant;

    // Test standards information
    bool standardsValid = mdux::Compliance::standards == "IEC 62304, IEC 62366";

    // Test safety class information
    bool safetyClassValid = mdux::Compliance::safetyClass == "Class B/C Medical Device Software";

    // Test graphics support
    bool graphicsEnabled = mdux::Graphics::isEnabled;
    bool graphicsApiValid = mdux::Graphics::api == "Vulkan 1.3";

    return complianceEnabled && standardsValid && safetyClassValid && graphicsEnabled &&
           graphicsApiValid;
}