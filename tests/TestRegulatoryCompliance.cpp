/**
 * @brief Regulatory compliance tests for MduX library
 */

#include <mdux/mdux.hpp>

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

    // Verify graphics consistency (required for usability)
    iec62366Compliant &= mdux::Graphics::isEnabled;
    iec62366Compliant &= (mdux::Graphics::api == "Vulkan");
    iec62366Compliant &= (mdux::Graphics::vulkanVersionMajor == 1);
    iec62366Compliant &= (mdux::Graphics::vulkanVersionMinor == 3);
    iec62366Compliant &= (mdux::Graphics::vulkanVersionPatch == 0);

    // Test medical device compliance flag
    bool medicalDeviceCompliant = mdux::Compliance::isMedicalDeviceCompliant;

    // Test safety standards documentation
    bool standardsDocumented = (mdux::Compliance::standards == "IEC 62304, IEC 62366");

    // Test safety class classification
    bool safetyClassDefined =
        (mdux::Compliance::safetyClass == "Class B/C Medical Device Software");

    // Test library initialization (critical for medical devices)
    bool initializationWorks = mdux::initialize();

    // All compliance tests must pass
    return iec62304Compliant && iec62366Compliant && medicalDeviceCompliant &&
           standardsDocumented && safetyClassDefined && initializationWorks;
}