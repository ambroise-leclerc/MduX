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

// Include all headers before module imports to avoid GCC 15 conflicts
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "../TestRunner.h"

// Use module imports for MduX modules only (not import std; due to GCC 15 issues)
import mdux.vulkansc.objects;
import mdux.vulkansc.memory;

using namespace std;
using namespace mdux::vulkansc;
using mdux::test::TestRunner;

//=============================================================================
// ObjectReservationConfiguration Tests
//=============================================================================

void testObjectReservationConfigurationDefaults() {
    ObjectReservationConfiguration config;

    if (config.pipelineCacheCount != 1) {
        throw runtime_error("Default pipelineCacheCount not 1");
    }

    if (config.commandPoolCount != 3) {
        throw runtime_error("Default commandPoolCount not 3");
    }

    if (config.descriptorPoolCount != 3) {
        throw runtime_error("Default descriptorPoolCount not 3");
    }
}

void testObjectReservationConfigurationValidity() {
    ObjectReservationConfiguration validConfig;
    if (!validConfig.isValid()) {
        throw runtime_error("Default configuration should be valid");
    }

    ObjectReservationConfiguration invalidConfig;
    invalidConfig.pipelineCacheCount = 0;  // Invalid
    if (invalidConfig.isValid()) {
        throw runtime_error("Configuration with 0 pipeline cache should be invalid");
    }

    invalidConfig.pipelineCacheCount = 1;
    invalidConfig.commandPoolCount = 0;  // Invalid
    if (invalidConfig.isValid()) {
        throw runtime_error("Configuration with 0 command pools should be invalid");
    }
}

void testObjectReservationToVulkanStruct() {
    ObjectReservationConfiguration config;
    config.pipelineCacheCount = 2;
    config.commandPoolCount = 5;
    config.descriptorPoolCount = 4;
    config.bufferCount = 100;
    config.imageCount = 50;

    auto vulkanStruct = config.toVulkanStruct();

    if (vulkanStruct.sType != VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO) {
        throw runtime_error("Incorrect sType in Vulkan struct");
    }

    if (vulkanStruct.pipelineCacheCreateInfoCount != 2) {
        throw runtime_error("Pipeline cache count not transferred");
    }

    if (vulkanStruct.commandPoolRequestCount != 5) {
        throw runtime_error("Command pool count not transferred");
    }

    if (vulkanStruct.descriptorPoolRequestCount != 4) {
        throw runtime_error("Descriptor pool count not transferred");
    }

    if (vulkanStruct.bufferRequestCount != 100) {
        throw runtime_error("Buffer count not transferred");
    }

    if (vulkanStruct.imageRequestCount != 50) {
        throw runtime_error("Image count not transferred");
    }
}

void testObjectReservationMemorySettings() {
    ObjectReservationConfiguration config;
    config.deviceMemoryCount = 20;
    config.maxMemoryAllocationSize = 256 * 1024 * 1024;  // 256 MB

    auto vulkanStruct = config.toVulkanStruct();

    if (vulkanStruct.deviceMemoryRequestCount != 20) {
        throw runtime_error("Device memory count not transferred");
    }

    if (vulkanStruct.maxMemoryAllocationSize != 256 * 1024 * 1024) {
        throw runtime_error("Max memory allocation size not transferred");
    }
}

void testObjectReservationCalculatorBasic() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 2;
    profile.maxUIElementsPerScreen = 200;
    profile.maxTextureAtlases = 10;
    profile.safetyMarginMultiplier = 2.0f;

    auto config = ObjectReservationCalculator::calculate(profile);

    if (!config.isValid()) {
        throw runtime_error("Calculator produced invalid configuration");
    }

    // Should have reasonable pipeline counts
    if (config.graphicsPipelineCount < 5) {
        throw runtime_error("Graphics pipeline count too low");
    }

    // Should have buffer count for UI elements
    if (config.bufferCount < 10) {
        throw runtime_error("Buffer count too low for profile");
    }
}

void testObjectReservationCalculatorSafetyMargin() {
    uint32_t baseCount = 100;
    float safetyMargin = 2.0f;

    uint32_t adjusted = ObjectReservationCalculator::applySafetyMargin(baseCount, safetyMargin);

    if (adjusted != 200) {
        throw runtime_error(
            "Safety margin calculation incorrect: expected 200, got " + to_string(adjusted)
        );
    }
}

void testObjectReservationCalculatorMedicalProfile() {
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
    if (config.bufferCount < 50) {
        throw runtime_error("Insufficient buffer count for complex medical UI");
    }

    if (config.imageCount < 20) {
        throw runtime_error("Insufficient image count for medical images");
    }

    if (config.descriptorSetCount < 50) {
        throw runtime_error("Insufficient descriptor sets");
    }

    if (config.commandPoolCount < 3) {
        throw runtime_error("Insufficient command pools");
    }
}

//=============================================================================
// Configuration Validation Tests (No Vulkan API Calls)
//=============================================================================

void testDeviceObjectManagerConfigurationValidation() {
    ObjectReservationConfiguration validConfig;
    // Default config should be valid
    if (!validConfig.isValid()) {
        throw runtime_error("Default configuration invalid");
    }

    ObjectReservationConfiguration invalidConfig1;
    invalidConfig1.pipelineCacheCount = 0;
    if (invalidConfig1.isValid()) {
        throw runtime_error("Zero pipeline cache should be invalid");
    }

    ObjectReservationConfiguration invalidConfig2;
    invalidConfig2.commandPoolCount = 0;
    if (invalidConfig2.isValid()) {
        throw runtime_error("Zero command pools should be invalid");
    }
}

void testStatisticsStructure() {
    DeviceObjectManager::ObjectStatistics stats;

    // Verify default initialization
    if (stats.commandPoolsCreated != 0) {
        throw runtime_error("Default commandPoolsCreated should be 0");
    }

    if (stats.buffersActive != 0) {
        throw runtime_error("Default buffersActive should be 0");
    }

    // Verify can update statistics
    stats.commandPoolsCreated = 3;
    stats.buffersActive = 25;
    stats.peakBuffers = 30;

    if (stats.commandPoolsCreated != 3) {
        throw runtime_error("Statistics update failed");
    }
}

//=============================================================================
// Main Test Suite
//=============================================================================

int main() {
    cout << "=============================================================================\n";
    cout << "Vulkan SC Device Object Manager Test Suite\n";
    cout << "IEC 62304 Software Unit Testing\n";
    cout << "=============================================================================\n\n";

    cout << "NOTE: Object creation tests skipped (require real Vulkan device)\n";
    cout << "Testing configuration validation and calculation logic only\n\n";

    TestRunner runner;

    // Configuration tests
    cout << "Running Object Reservation Configuration Tests...\n";
    runner.runTest("ObjectReservationConfiguration Defaults", testObjectReservationConfigurationDefaults);
    runner.runTest("ObjectReservationConfiguration Validity", testObjectReservationConfigurationValidity);
    runner.runTest("ObjectReservationConfiguration toVulkanStruct", testObjectReservationToVulkanStruct);
    runner.runTest("ObjectReservationConfiguration Memory Settings", testObjectReservationMemorySettings);

    // Calculator tests
    cout << "\nRunning Object Reservation Calculator Tests...\n";
    runner.runTest("ObjectReservationCalculator Basic", testObjectReservationCalculatorBasic);
    runner.runTest("ObjectReservationCalculator Safety Margin", testObjectReservationCalculatorSafetyMargin);
    runner.runTest("ObjectReservationCalculator Medical Profile", testObjectReservationCalculatorMedicalProfile);

    // Validation tests (no Vulkan calls)
    cout << "\nRunning DeviceObjectManager Validation Tests...\n";
    runner.runTest("DeviceObjectManager Configuration Validation", testDeviceObjectManagerConfigurationValidation);
    runner.runTest("ObjectStatistics Structure", testStatisticsStructure);

    runner.printSummary();

    return runner.allTestsPassed() ? 0 : 1;
}
