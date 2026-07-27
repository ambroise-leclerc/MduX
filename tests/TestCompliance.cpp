/**
 * @brief Compliance-related tests for MduX library
 */

import std;
import mdux;
import mdux.test;

#include "framework/MduXTest.hpp"

TEST_CASE("Compliance Test") {
    CHECK(mdux::Compliance::isMedicalDeviceCompliant);
    CHECK(mdux::Compliance::standards == std::string_view("IEC 62304, IEC 62366"));
    CHECK(mdux::Compliance::safetyClass == std::string_view("Class B/C Medical Device Software"));
    CHECK(mdux::VulkanSupport::isAvailable);
    CHECK(mdux::VulkanSupport::api == std::string_view("Vulkan"));
}
