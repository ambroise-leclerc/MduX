/**
 * @file MemoryPoolManagerTests.cpp
 * @brief Test suite for Vulkan SC static memory pool manager
 *
 * @compliance IEC 62304 Software Unit Testing
 * @compliance Vulkan SC 1.0 Conformance Testing
 */

#include <stdint.h>
#include <vulkan/vulkan.h>

import std;
import mdux.vulkansc.memory;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace std;
using namespace mdux::vulkansc;

//=============================================================================
// Note: Tests use VK_NULL_HANDLE for physical devices to avoid Vulkan API calls
// This tests the calculation logic without requiring real Vulkan hardware
//=============================================================================

//=============================================================================
// MemoryPoolManager Tests
//=============================================================================

TEST_CASE("MemoryPoolConfiguration Validity") {
    // Valid configuration
    MemoryPoolConfiguration validConfig;
    validConfig.maxTotalMemory = 128 * 1024 * 1024;  // 128 MB
    validConfig.maxTotalAllocations = 100;
    CHECK(validConfig.isValid());

    // Invalid configuration (no memory)
    MemoryPoolConfiguration invalidConfig1;
    invalidConfig1.maxTotalMemory = 0;
    invalidConfig1.maxTotalAllocations = 100;
    CHECK(!invalidConfig1.isValid());

    // Invalid configuration (no allocations)
    MemoryPoolConfiguration invalidConfig2;
    invalidConfig2.maxTotalMemory = 128 * 1024 * 1024;
    invalidConfig2.maxTotalAllocations = 0;
    CHECK(!invalidConfig2.isValid());
}

TEST_CASE("MemoryPoolCalculator Basic") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.maxTextureAtlases = 5;
    profile.maxConcurrentImages = 5;
    profile.maxImageResolution = 2048;
    profile.safetyMarginMultiplier = 2.0f;

    // Use VK_NULL_HANDLE to test calculation logic without Vulkan API calls
    auto config = MemoryPoolCalculator::calculate(profile);

    CHECK(config.isValid());
    CHECK(config.maxTotalMemory != 0);
    CHECK(config.maxTotalAllocations != 0);
}

TEST_CASE("MemoryPoolCalculator Safety Margin") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.safetyMarginMultiplier = 2.0f;  // 100% safety margin (Class B/C)

    auto config = MemoryPoolCalculator::calculate(profile);

    // Verify safety margin was applied
    CHECK(config.safetyMargin == 2.0f);
}

TEST_CASE("MemoryPoolCalculator Geometry Estimation") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 2;
    profile.maxUIElementsPerScreen = 100;

    VkDeviceSize geometryMemory = MemoryPoolCalculator::estimateGeometryMemory(profile);

    CHECK(geometryMemory != 0);
    // Rough sanity check (should be at least 1 KB for 200 elements)
    CHECK(geometryMemory >= 1024);
}

TEST_CASE("MemoryPoolCalculator Texture Estimation") {
    MedicalApplicationProfile profile;
    profile.maxTextureAtlases = 5;
    profile.maxConcurrentImages = 10;
    profile.maxImageResolution = 2048;

    VkDeviceSize textureMemory = MemoryPoolCalculator::estimateTextureMemory(profile);

    CHECK(textureMemory != 0);
    // Should be substantial for 2048x2048 images
    CHECK(textureMemory >= 10 * 1024 * 1024);  // At least 10 MB
}

TEST_CASE("MemoryPoolCalculator Safety Margin Application") {
    MedicalApplicationProfile profile;
    profile.safetyMarginMultiplier = 2.5f;

    VkDeviceSize baseSize = 1000;
    VkDeviceSize withMargin = MemoryPoolCalculator::applySafetyMargin(baseSize, profile);

    CHECK_MESSAGE(withMargin == 2500,
                  "expected 2500, got " + to_string(withMargin));
}

TEST_CASE("MemoryPoolCalculator Redundancy") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.safetyMarginMultiplier = 1.0f;
    profile.requiresRedundancy = true;

    auto config = MemoryPoolCalculator::calculate(profile);

    // With redundancy, memory should be doubled
    profile.requiresRedundancy = false;
    auto configNoRedundancy = MemoryPoolCalculator::calculate(profile);

    // Redundant config should have approximately 2x memory (allowing for rounding)
    float ratio = static_cast<float>(config.maxTotalMemory) /
                 static_cast<float>(configNoRedundancy.maxTotalMemory);

    CHECK_MESSAGE(ratio >= 1.8f && ratio <= 2.2f,
                  "redundancy not correctly applied: ratio = " + to_string(ratio));
}

TEST_CASE("MedicalApplicationProfile Defaults") {
    MedicalApplicationProfile profile;

    // Verify sensible defaults
    CHECK(profile.maxConcurrentScreens != 0);
    CHECK(profile.maxUIElementsPerScreen != 0);
    CHECK(profile.safetyMarginMultiplier >= 1.0f);
    CHECK(profile.targetFrameRate != 0);
}

TEST_CASE("MemoryPoolConfiguration Safety Class") {
    MemoryPoolConfiguration config;
    config.maxTotalMemory = 128 * 1024 * 1024;
    config.maxTotalAllocations = 100;
    config.safetyClass = "Class B";

    CHECK(config.safetyClass == "Class B");

    // Verify can change safety class
    config.safetyClass = "Class C";
    CHECK(config.safetyClass == "Class C");
}

TEST_CASE("MemoryPoolConfiguration Array Initialization") {
    MemoryPoolConfiguration config;

    // All pool sizes should default to zero
    for (size_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        CHECK_MESSAGE(config.poolSizes[i] == 0,
                      "pool size at index " + to_string(i) + " not zero-initialized");
        CHECK_MESSAGE(config.maxAllocationsPerType[i] == 0,
                      "max allocations at index " + to_string(i) + " not zero-initialized");
    }

    // Can set individual pool sizes
    config.poolSizes[0] = 64 * 1024 * 1024;
    config.maxAllocationsPerType[0] = 50;

    CHECK(config.poolSizes[0] == 64 * 1024 * 1024);
}

TEST_CASE("Calculator Large Application Profile") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 10;         // Many screens
    profile.maxUIElementsPerScreen = 500;      // Complex UIs
    profile.maxTextureAtlases = 20;            // Many textures
    profile.maxConcurrentImages = 50;          // Many medical images
    profile.maxImageResolution = 4096;         // High resolution
    profile.maxFramesInFlight = 3;             // Triple buffering
    profile.safetyMarginMultiplier = 2.0f;     // Class B/C safety

    auto config = MemoryPoolCalculator::calculate(profile);

    // Should produce large memory requirements
    CHECK_MESSAGE(config.maxTotalMemory >= 100 * 1024 * 1024,  // At least 100 MB
                  "large profile produced insufficient memory: " +
                      to_string(config.maxTotalMemory / (1024 * 1024)) + " MB");

    // Should request substantial allocations
    CHECK(config.maxTotalAllocations >= 50);
}

TEST_CASE("Calculator Minimal Application Profile") {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 10;
    profile.maxTextureAtlases = 1;
    profile.maxConcurrentImages = 1;
    profile.maxImageResolution = 512;
    profile.maxFramesInFlight = 1;
    profile.safetyMarginMultiplier = 1.0f;     // Minimal margin

    auto config = MemoryPoolCalculator::calculate(profile);

    CHECK(config.isValid());
    // Should still produce reasonable requirements
    CHECK(config.maxTotalMemory != 0);
}

MDUX_TEST_MAIN("Vulkan SC Memory Pool Manager Test Suite")
