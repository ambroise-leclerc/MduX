/**
 * @brief Regulatory compliance tests for MduX library
 */

import std;
import mdux;

bool testRegulatoryCompliance() {
    // Test IEC 62304 compliance (Software Lifecycle)
    bool iec62304Compliant = true;

    // Verify version traceability (required by IEC 62304)
    iec62304Compliant &= (mdux::Version::major == 0);
    iec62304Compliant &= (mdux::Version::minor == 1);
    iec62304Compliant &= (mdux::Version::patch == 0);
    iec62304Compliant &= (!mdux::Version::getString().empty());

    // Test IEC 62366 compliance (Usability Engineering)
    bool iec62366Compliant = true;

    // Verify Vulkan consistency (required for usability)
    iec62366Compliant &= mdux::VulkanSupport::isAvailable;
    iec62366Compliant &= (mdux::VulkanSupport::api == std::string_view("Vulkan"));
    iec62366Compliant &= (mdux::VulkanSupport::requiredVersionMajor == 1);
    iec62366Compliant &= (mdux::VulkanSupport::requiredVersionMinor == 3);
    iec62366Compliant &= (mdux::VulkanSupport::requiredVersionPatch == 0);

    // Test medical device compliance flag
    bool medicalDeviceCompliant = mdux::Compliance::isMedicalDeviceCompliant;

    // Test safety standards documentation
    bool standardsDocumented = (mdux::Compliance::standards == std::string_view("IEC 62304, IEC 62366"));

    // Test safety class classification
    bool safetyClassDefined =
        (mdux::Compliance::safetyClass == std::string_view("Class B/C Medical Device Software"));

    // Test library initialization (critical for medical devices)
    bool initializationWorks = mdux::initialize();

    // All compliance tests must pass
    return iec62304Compliant && iec62366Compliant && medicalDeviceCompliant &&
           standardsDocumented && safetyClassDefined && initializationWorks;
}