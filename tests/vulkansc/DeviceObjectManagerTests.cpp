/**
 * @file DeviceObjectManagerTests.cpp
 * @brief Test suite for Vulkan SC device-lifetime object manager
 *
 * @compliance IEC 62304 Software Unit Testing
 * @compliance Vulkan SC 1.0 Conformance Testing
 *
 * @note Object creation tests require real Vulkan device and are skipped
 *       These tests focus on configuration validation without Vulkan API calls
 */

#include <stdint.h>
#include <vulkan/vulkan.h>

import std;
import mdux.vulkansc.objects;
import mdux.vulkansc.memory;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace std;
using namespace mdux::vulkansc;

//=============================================================================
// ObjectReservationConfiguration Tests
//=============================================================================

TEST_CASE("ObjectReservationConfiguration Defaults") {
    ObjectReservationConfiguration config;

    CHECK(config.pipelineCacheCount == 1);
    CHECK(config.commandPoolCount == 3);
    CHECK(config.descriptorPoolCount == 3);
}

TEST_CASE("ObjectReservationConfiguration Validity") {
    ObjectReservationConfiguration validConfig;
    CHECK(validConfig.isValid());

    ObjectReservationConfiguration invalidConfig;
    invalidConfig.pipelineCacheCount = 0;  // Invalid
    CHECK(!invalidConfig.isValid());

    invalidConfig.pipelineCacheCount = 1;
    invalidConfig.commandPoolCount = 0;  // Invalid
    CHECK(!invalidConfig.isValid());
}

TEST_CASE("ObjectReservationConfiguration toVulkanStruct") {
    ObjectReservationConfiguration config;
    config.pipelineCacheCount = 2;
    config.commandPoolCount = 5;
    config.descriptorPoolCount = 4;
    config.bufferCount = 100;
    config.imageCount = 50;

    auto vulkanStruct = config.toVulkanStruct();

    CHECK(vulkanStruct.sType == VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO);
    CHECK(vulkanStruct.pipelineCacheCreateInfoCount == 2);
    CHECK(vulkanStruct.commandPoolRequestCount == 5);
    CHECK(vulkanStruct.descriptorPoolRequestCount == 4);
    CHECK(vulkanStruct.bufferRequestCount == 100);
    CHECK(vulkanStruct.imageRequestCount == 50);
}

TEST_CASE("ObjectReservationConfiguration Memory Settings") {
    ObjectReservationConfiguration config;
    config.deviceMemoryCount = 20;
    config.maxMemoryAllocationSize = 256 * 1024 * 1024;  // 256 MB

    auto vulkanStruct = config.toVulkanStruct();

    CHECK(vulkanStruct.deviceMemoryRequestCount == 20);
    CHECK(vulkanStruct.maxMemoryAllocationSize == 256 * 1024 * 1024);
}

//=============================================================================
// ObjectReservationCalculator Tests
//=============================================================================

TEST_CASE("ObjectReservationCalculator Basic") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 2;
    profile.maxUIElementsPerScreen = 200;
    profile.maxTextureAtlases = 10;
    profile.safetyMarginMultiplier = 2.0f;

    auto config = ObjectReservationCalculator::calculate(profile);

    CHECK(config.isValid());
    // Should have reasonable pipeline counts
    CHECK(config.graphicsPipelineCount >= 5);
    // Should have buffer count for UI elements
    CHECK(config.bufferCount >= 10);
}

TEST_CASE("ObjectReservationCalculator Safety Margin") {
    uint32_t baseCount = 100;
    float safetyMargin = 2.0f;

    uint32_t adjusted = ObjectReservationCalculator::applySafetyMargin(baseCount, safetyMargin);

    CHECK_MESSAGE(adjusted == 200, "expected 200, got " + to_string(adjusted));
}

TEST_CASE("ObjectReservationCalculator Medical Profile") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 3;
    profile.maxUIElementsPerScreen = 500;
    profile.maxTextureAtlases = 15;
    profile.maxConcurrentImages = 20;
    profile.maxImageResolution = 2048;
    profile.maxFramesInFlight = 3;
    profile.safetyMarginMultiplier = 2.0f;  // Class B/C

    auto config = ObjectReservationCalculator::calculate(profile);

    // Verify comprehensive configuration
    CHECK(config.bufferCount >= 50);
    CHECK(config.imageCount >= 20);
    CHECK(config.descriptorSetCount >= 50);
    CHECK(config.commandPoolCount >= 3);
}

//=============================================================================
// Configuration Validation Tests (No Vulkan API Calls)
//=============================================================================

TEST_CASE("DeviceObjectManager Configuration Validation") {
    ObjectReservationConfiguration validConfig;
    // Default config should be valid
    CHECK(validConfig.isValid());

    ObjectReservationConfiguration invalidConfig1;
    invalidConfig1.pipelineCacheCount = 0;
    CHECK(!invalidConfig1.isValid());

    ObjectReservationConfiguration invalidConfig2;
    invalidConfig2.commandPoolCount = 0;
    CHECK(!invalidConfig2.isValid());
}

TEST_CASE("ObjectStatistics Structure") {
    DeviceObjectManager::ObjectStatistics stats;

    // Verify default initialization
    CHECK(stats.commandPoolsCreated == 0);
    CHECK(stats.buffersActive == 0);

    // Verify can update statistics
    stats.commandPoolsCreated = 3;
    stats.buffersActive = 25;
    stats.peakBuffers = 30;

    CHECK(stats.commandPoolsCreated == 3);
}

MDUX_TEST_MAIN("Vulkan SC Device Object Manager Test Suite")
