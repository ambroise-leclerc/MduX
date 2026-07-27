/**
 * @brief Regulatory compliance tests for MduX library
 */

import std;
import mdux;
import mdux.test;

#include "framework/MduXTest.hpp"

TEST_CASE("IEC 62304 Version Traceability", "regulatory") {
    // Verify version traceability (required by IEC 62304)
    CHECK(mdux::Version::major == 0);
    CHECK(mdux::Version::minor == 2);
    CHECK(mdux::Version::patch == 0);
    CHECK(!mdux::Version::getString().empty());
}

TEST_CASE("IEC 62366 Vulkan Consistency", "regulatory") {
    // Verify Vulkan consistency (required for usability)
    CHECK(mdux::VulkanSupport::isAvailable);
    CHECK(mdux::VulkanSupport::api == std::string_view("Vulkan"));
    CHECK(mdux::VulkanSupport::requiredVersionMajor == 1);
    CHECK(mdux::VulkanSupport::requiredVersionMinor == 3);
    CHECK(mdux::VulkanSupport::requiredVersionPatch == 0);
}

TEST_CASE("Medical Device Compliance Flag", "regulatory") {
    CHECK(mdux::Compliance::isMedicalDeviceCompliant);
    CHECK(mdux::Compliance::standards == std::string_view("IEC 62304, IEC 62366"));
    CHECK(mdux::Compliance::safetyClass == std::string_view("Class B/C Medical Device Software"));
}

TEST_CASE("Library Initialization", "regulatory") {
    // Critical for medical devices: initialization must succeed.
    REQUIRE(mdux::initialize());
}
