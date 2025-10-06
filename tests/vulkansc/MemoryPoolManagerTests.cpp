/**
 * @file MemoryPoolManagerTests.cpp
 * @brief Test suite for Vulkan SC static memory pool manager
 *
 * @compliance IEC 62304 Software Unit Testing
 * @compliance Vulkan SC 1.0 Conformance Testing
 */

#include <stdint.h>
#include <vulkan/vulkan.h>
#include "../TestRunner.h"

import std;
import mdux.vulkansc.memory;

using namespace std;
using namespace mdux::vulkansc;
using mdux::test::TestRunner;

//=============================================================================
// Note: Tests use VK_NULL_HANDLE for physical devices to avoid Vulkan API calls
// This tests the calculation logic without requiring real Vulkan hardware
//=============================================================================

//=============================================================================
// MemoryPoolManager Tests
//=============================================================================

void testMemoryPoolConfigurationValidity() {
    // Valid configuration
    MemoryPoolConfiguration validConfig;
    validConfig.maxTotalMemory = 128 * 1024 * 1024;  // 128 MB
    validConfig.maxTotalAllocations = 100;

    if (!validConfig.isValid()) {
        throw runtime_error("Valid configuration reported as invalid");
    }

    // Invalid configuration (no memory)
    MemoryPoolConfiguration invalidConfig1;
    invalidConfig1.maxTotalMemory = 0;
    invalidConfig1.maxTotalAllocations = 100;

    if (invalidConfig1.isValid()) {
        throw runtime_error("Invalid configuration (zero memory) reported as valid");
    }

    // Invalid configuration (no allocations)
    MemoryPoolConfiguration invalidConfig2;
    invalidConfig2.maxTotalMemory = 128 * 1024 * 1024;
    invalidConfig2.maxTotalAllocations = 0;

    if (invalidConfig2.isValid()) {
        throw runtime_error("Invalid configuration (zero allocations) reported as valid");
    }
}

void testMemoryPoolCalculatorBasic() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.maxTextureAtlases = 5;
    profile.maxConcurrentImages = 5;
    profile.maxImageResolution = 2048;
    profile.safetyMarginMultiplier = 2.0f;

    // Use VK_NULL_HANDLE to test calculation logic without Vulkan API calls
    auto config = MemoryPoolCalculator::calculate(profile);

    if (!config.isValid()) {
        throw runtime_error("Calculator produced invalid configuration");
    }

    if (config.maxTotalMemory == 0) {
        throw runtime_error("Calculator produced zero total memory");
    }

    if (config.maxTotalAllocations == 0) {
        throw runtime_error("Calculator produced zero allocations");
    }
}

void testMemoryPoolCalculatorWithSafetyMargin() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.safetyMarginMultiplier = 2.0f;  // 100% safety margin (Class B/C)

    auto config = MemoryPoolCalculator::calculate(profile);

    // Verify safety margin was applied
    if (config.safetyMargin != 2.0f) {
        throw runtime_error("Safety margin not correctly set");
    }
}

void testMemoryPoolCalculatorGeometryEstimation() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 2;
    profile.maxUIElementsPerScreen = 100;

    VkDeviceSize geometryMemory = MemoryPoolCalculator::estimateGeometryMemory(profile);

    // Should be non-zero
    if (geometryMemory == 0) {
        throw runtime_error("Geometry memory estimation returned zero");
    }

    // Rough sanity check (should be at least 1 KB for 200 elements)
    if (geometryMemory < 1024) {
        throw runtime_error("Geometry memory estimation too small");
    }
}

void testMemoryPoolCalculatorTextureEstimation() {
    MedicalApplicationProfile profile;
    profile.maxTextureAtlases = 5;
    profile.maxConcurrentImages = 10;
    profile.maxImageResolution = 2048;

    VkDeviceSize textureMemory = MemoryPoolCalculator::estimateTextureMemory(profile);

    if (textureMemory == 0) {
        throw runtime_error("Texture memory estimation returned zero");
    }

    // Should be substantial for 2048x2048 images
    if (textureMemory < 10 * 1024 * 1024) {  // At least 10 MB
        throw runtime_error("Texture memory estimation too small for resolution");
    }
}

void testMemoryPoolCalculatorSafetyMarginApplication() {
    MedicalApplicationProfile profile;
    profile.safetyMarginMultiplier = 2.5f;

    VkDeviceSize baseSize = 1000;
    VkDeviceSize withMargin = MemoryPoolCalculator::applySafetyMargin(baseSize, profile);

    if (withMargin != 2500) {
        throw runtime_error(
            "Safety margin calculation incorrect: expected 2500, got " + to_string(withMargin)
        );
    }
}

void testMemoryPoolCalculatorRedundancy() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 100;
    profile.safetyMarginMultiplier = 1.0f;
    profile.requiresRedundancy = true;

    auto config = MemoryPoolCalculator::calculate(profile);

    // With redundancy, memory should be doubled
    profile.requiresRedundancy = false;
    auto configNoRedundancy = MemoryPoolCalculator::calculate(profile);

    // Redundant config should have approximately 2× memory
    // (allowing for rounding differences)
    float ratio = static_cast<float>(config.maxTotalMemory) /
                 static_cast<float>(configNoRedundancy.maxTotalMemory);

    if (ratio < 1.8f || ratio > 2.2f) {
        throw runtime_error(
            "Redundancy not correctly applied: ratio = " + to_string(ratio)
        );
    }
}

void testMedicalApplicationProfileDefaults() {
    MedicalApplicationProfile profile;

    // Verify sensible defaults
    if (profile.maxConcurrentScreens == 0) {
        throw runtime_error("Default maxConcurrentScreens is zero");
    }

    if (profile.maxUIElementsPerScreen == 0) {
        throw runtime_error("Default maxUIElementsPerScreen is zero");
    }

    if (profile.safetyMarginMultiplier < 1.0f) {
        throw runtime_error("Default safety margin less than 1.0");
    }

    if (profile.targetFrameRate == 0) {
        throw runtime_error("Default target frame rate is zero");
    }
}

void testMemoryPoolConfigurationSafetyClass() {
    MemoryPoolConfiguration config;
    config.maxTotalMemory = 128 * 1024 * 1024;
    config.maxTotalAllocations = 100;
    config.safetyClass = "Class B";

    if (config.safetyClass != "Class B") {
        throw runtime_error("Safety class not correctly set");
    }

    // Verify can change safety class
    config.safetyClass = "Class C";
    if (config.safetyClass != "Class C") {
        throw runtime_error("Safety class cannot be changed");
    }
}

void testMemoryPoolArrayInitialization() {
    MemoryPoolConfiguration config;

    // All pool sizes should default to zero
    for (size_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        if (config.poolSizes[i] != 0) {
            throw runtime_error("Pool size at index " + to_string(i) + " not zero-initialized");
        }
        if (config.maxAllocationsPerType[i] != 0) {
            throw runtime_error(
                "Max allocations at index " + to_string(i) + " not zero-initialized"
            );
        }
    }

    // Can set individual pool sizes
    config.poolSizes[0] = 64 * 1024 * 1024;
    config.maxAllocationsPerType[0] = 50;

    if (config.poolSizes[0] != 64 * 1024 * 1024) {
        throw runtime_error("Pool size not correctly set");
    }
}

void testCalculatorLargeApplicationProfile() {
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
    if (config.maxTotalMemory < 100 * 1024 * 1024) {  // At least 100 MB
        throw runtime_error(
            "Large profile produced insufficient memory: " +
            to_string(config.maxTotalMemory / (1024 * 1024)) + " MB"
        );
    }

    // Should request substantial allocations
    if (config.maxTotalAllocations < 50) {
        throw runtime_error("Large profile produced insufficient allocation count");
    }
}

void testCalculatorMinimalApplicationProfile() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 1;
    profile.maxUIElementsPerScreen = 10;
    profile.maxTextureAtlases = 1;
    profile.maxConcurrentImages = 1;
    profile.maxImageResolution = 512;
    profile.maxFramesInFlight = 1;
    profile.safetyMarginMultiplier = 1.0f;     // Minimal margin

    auto config = MemoryPoolCalculator::calculate(profile);

    if (!config.isValid()) {
        throw runtime_error("Minimal profile produced invalid configuration");
    }

    // Should still produce reasonable requirements
    if (config.maxTotalMemory == 0) {
        throw runtime_error("Minimal profile produced zero memory");
    }
}

//=============================================================================
// Main Test Suite
//=============================================================================

int main() {
    cout << "=============================================================================\n";
    cout << "Vulkan SC Memory Pool Manager Test Suite\n";
    cout << "IEC 62304 Software Unit Testing\n";
    cout << "=============================================================================\n\n";

    TestRunner runner;

    // Configuration tests
    cout << "Running Configuration Tests...\n";
    runner.runTest("MemoryPoolConfiguration Validity", testMemoryPoolConfigurationValidity);
    runner.runTest("MemoryPoolConfiguration Safety Class", testMemoryPoolConfigurationSafetyClass);
    runner.runTest("MemoryPoolConfiguration Array Initialization", testMemoryPoolArrayInitialization);
    runner.runTest("MedicalApplicationProfile Defaults", testMedicalApplicationProfileDefaults);

    // Calculator basic tests
    cout << "\nRunning Memory Pool Calculator Tests...\n";
    runner.runTest("MemoryPoolCalculator Basic", testMemoryPoolCalculatorBasic);
    runner.runTest("MemoryPoolCalculator Safety Margin", testMemoryPoolCalculatorWithSafetyMargin);
    runner.runTest("MemoryPoolCalculator Geometry Estimation", testMemoryPoolCalculatorGeometryEstimation);
    runner.runTest("MemoryPoolCalculator Texture Estimation", testMemoryPoolCalculatorTextureEstimation);
    runner.runTest("MemoryPoolCalculator Safety Margin Application", testMemoryPoolCalculatorSafetyMarginApplication);
    runner.runTest("MemoryPoolCalculator Redundancy", testMemoryPoolCalculatorRedundancy);

    // Calculator profile tests
    cout << "\nRunning Application Profile Tests...\n";
    runner.runTest("Calculator Large Application Profile", testCalculatorLargeApplicationProfile);
    runner.runTest("Calculator Minimal Application Profile", testCalculatorMinimalApplicationProfile);

    runner.printSummary();

    return runner.allTestsPassed() ? 0 : 1;
}
