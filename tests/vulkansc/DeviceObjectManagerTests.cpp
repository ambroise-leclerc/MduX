/**
 * @file DeviceObjectManagerTests.cpp
 * @brief Test suite for Vulkan SC device-lifetime object manager
 *
 * @compliance IEC 62304 Software Unit Testing
 * @compliance Vulkan SC 1.0 Device-Lifetime Object Requirements
 */

module;

#include <vulkan/vulkan.h>
#include <cstdint>

import std;
import mdux.vulkansc.objects;
import mdux.vulkansc.memory;

using namespace std;
using namespace mdux::vulkansc;

//=============================================================================
// Test Framework
//=============================================================================

class TestRunner {
public:
    struct TestResult {
        string testName;
        bool passed;
        string errorMessage;
        chrono::microseconds duration;
    };

    void runTest(const string& name, function<void()> testFunc) {
        auto start = chrono::high_resolution_clock::now();

        try {
            testFunc();
            auto end = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(end - start);

            results.push_back({
                .testName = name,
                .passed = true,
                .errorMessage = "",
                .duration = duration
            });

            cout << "✅ PASS: " << name << " (" << duration.count() << " µs)\n";
        }
        catch (const exception& e) {
            auto end = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(end - start);

            results.push_back({
                .testName = name,
                .passed = false,
                .errorMessage = e.what(),
                .duration = duration
            });

            cout << "❌ FAIL: " << name << "\n";
            cout << "   Error: " << e.what() << "\n";
        }
    }

    void printSummary() const {
        size_t passed = count_if(results.begin(), results.end(),
                                [](const TestResult& r) { return r.passed; });
        size_t failed = results.size() - passed;

        cout << "\n=============================================================================\n";
        cout << "Test Summary\n";
        cout << "=============================================================================\n";
        cout << "Total:  " << results.size() << " tests\n";
        cout << "Passed: " << passed << " (" << (passed * 100 / results.size()) << "%)\n";
        cout << "Failed: " << failed << "\n";

        if (failed > 0) {
            cout << "\nFailed Tests:\n";
            for (const auto& result : results) {
                if (!result.passed) {
                    cout << "  - " << result.testName << ": " << result.errorMessage << "\n";
                }
            }
        }

        cout << "=============================================================================\n";
    }

    bool allTestsPassed() const {
        return all_of(results.begin(), results.end(),
                     [](const TestResult& r) { return r.passed; });
    }

private:
    vector<TestResult> results;
};

//=============================================================================
// Mock Vulkan Device
//=============================================================================

class MockVulkanDevice {
public:
    static VkDevice createMockDevice() {
        return reinterpret_cast<VkDevice>(0x1000);
    }

    static VkCommandPool createMockCommandPool(uint32_t index) {
        return reinterpret_cast<VkCommandPool>(0x2000 + index);
    }

    static VkDescriptorPool createMockDescriptorPool(uint32_t index) {
        return reinterpret_cast<VkDescriptorPool>(0x3000 + index);
    }

    static VkQueryPool createMockQueryPool(uint32_t index) {
        return reinterpret_cast<VkQueryPool>(0x4000 + index);
    }

    static VkBuffer createMockBuffer(uint32_t index) {
        return reinterpret_cast<VkBuffer>(0x5000 + index);
    }

    static VkImage createMockImage(uint32_t index) {
        return reinterpret_cast<VkImage>(0x6000 + index);
    }
};

//=============================================================================
// ObjectReservationConfiguration Tests
//=============================================================================

void testObjectReservationConfigurationDefaults() {
    ObjectReservationConfiguration config;

    // Verify sensible defaults
    if (config.pipelineCacheCount == 0) {
        throw runtime_error("Default pipelineCacheCount is zero");
    }

    if (config.commandPoolCount == 0) {
        throw runtime_error("Default commandPoolCount is zero");
    }

    if (config.descriptorPoolCount == 0) {
        throw runtime_error("Default descriptorPoolCount is zero");
    }

    if (config.bufferCount == 0) {
        throw runtime_error("Default bufferCount is zero");
    }

    if (config.imageCount == 0) {
        throw runtime_error("Default imageCount is zero");
    }
}

void testObjectReservationConfigurationValidity() {
    ObjectReservationConfiguration validConfig;
    validConfig.pipelineCacheCount = 1;
    validConfig.commandPoolCount = 3;

    if (!validConfig.isValid()) {
        throw runtime_error("Valid configuration reported as invalid");
    }

    // Invalid: no pipeline cache
    ObjectReservationConfiguration invalidConfig1;
    invalidConfig1.pipelineCacheCount = 0;
    invalidConfig1.commandPoolCount = 3;

    if (invalidConfig1.isValid()) {
        throw runtime_error("Invalid configuration (no pipeline cache) reported as valid");
    }

    // Invalid: no command pools
    ObjectReservationConfiguration invalidConfig2;
    invalidConfig2.pipelineCacheCount = 1;
    invalidConfig2.commandPoolCount = 0;

    if (invalidConfig2.isValid()) {
        throw runtime_error("Invalid configuration (no command pools) reported as valid");
    }
}

void testObjectReservationConfigurationMemoryLimits() {
    ObjectReservationConfiguration config;
    config.deviceMemoryCount = 15;
    config.maxMemoryAllocationSize = 256 * 1024 * 1024;  // 256 MB

    if (config.deviceMemoryCount != 15) {
        throw runtime_error("Device memory count not correctly set");
    }

    if (config.maxMemoryAllocationSize != 256 * 1024 * 1024) {
        throw runtime_error("Max memory allocation size not correctly set");
    }
}

void testObjectReservationConfigurationDeviceLifetimeObjects() {
    ObjectReservationConfiguration config;

    // These are device-lifetime objects - cannot be destroyed
    config.commandPoolCount = 5;
    config.descriptorPoolCount = 4;
    config.queryPoolCount = 3;
    config.swapchainCount = 1;

    if (config.commandPoolCount != 5) {
        throw runtime_error("Command pool count not set");
    }

    if (config.descriptorPoolCount != 4) {
        throw runtime_error("Descriptor pool count not set");
    }

    if (config.queryPoolCount != 3) {
        throw runtime_error("Query pool count not set");
    }

    if (config.swapchainCount != 1) {
        throw runtime_error("Swapchain count not set");
    }
}

//=============================================================================
// DeviceObjectManager Basic Tests
//=============================================================================

void testDeviceObjectManagerInitialization() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 3;
    config.descriptorPoolCount = 2;
    config.bufferCount = 50;

    auto device = MockVulkanDevice::createMockDevice();

    // Should not throw
    manager.initialize(device, config);

    // Verify initialized state
    if (manager.getCommandPoolCount() != 0) {
        throw runtime_error("Command pools should not be pre-created");
    }
}

void testDeviceObjectManagerDoubleInitialization() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Second initialization should throw
    try {
        manager.initialize(device, config);
        throw runtime_error("Double initialization should throw");
    }
    catch (const runtime_error& e) {
        string msg = e.what();
        if (msg.find("already initialized") == string::npos) {
            throw runtime_error("Wrong error message for double initialization");
        }
    }
}

//=============================================================================
// Device-Lifetime Object Tests
//=============================================================================

void testCommandPoolCreation() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 3;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create command pools (device-lifetime objects)
    VkCommandPool pool1 = manager.createCommandPool(0, 0);
    VkCommandPool pool2 = manager.createCommandPool(1, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    if (pool1 == VK_NULL_HANDLE) {
        throw runtime_error("Command pool 1 creation failed");
    }

    if (pool2 == VK_NULL_HANDLE) {
        throw runtime_error("Command pool 2 creation failed");
    }

    if (manager.getCommandPoolCount() != 2) {
        throw runtime_error("Command pool count incorrect after creation");
    }
}

void testCommandPoolReservationLimit() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 2;  // Limit to 2 pools
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create up to limit
    manager.createCommandPool(0);
    manager.createCommandPool(1);

    // Exceeding limit should throw
    try {
        manager.createCommandPool(2);
        throw runtime_error("Should not exceed command pool reservation");
    }
    catch (const runtime_error& e) {
        string msg = e.what();
        if (msg.find("reservation") == string::npos && msg.find("exceeded") == string::npos) {
            throw runtime_error("Wrong error for exceeding reservation");
        }
    }
}

void testDescriptorPoolCreation() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.descriptorPoolCount = 3;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create descriptor pool
    vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20 }
    };

    VkDescriptorPool pool = manager.createDescriptorPool(poolSizes, 30);

    if (pool == VK_NULL_HANDLE) {
        throw runtime_error("Descriptor pool creation failed");
    }

    if (manager.getDescriptorPoolCount() != 1) {
        throw runtime_error("Descriptor pool count incorrect");
    }
}

void testQueryPoolCreation() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.queryPoolCount = 5;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create query pool
    VkQueryPool pool = manager.createQueryPool(VK_QUERY_TYPE_OCCLUSION, 16);

    if (pool == VK_NULL_HANDLE) {
        throw runtime_error("Query pool creation failed");
    }
}

//=============================================================================
// Regular Object Tests (Can Destroy)
//=============================================================================

void testBufferCreationAndDestruction() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.bufferCount = 50;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create buffer
    VkBuffer buffer = manager.createBuffer(1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    if (buffer == VK_NULL_HANDLE) {
        throw runtime_error("Buffer creation failed");
    }

    // Should be able to destroy regular objects
    manager.destroyBuffer(buffer);  // Should not throw
}

void testImageCreationAndDestruction() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.imageCount = 30;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { 256, 256, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImage image = manager.createImage(imageInfo);

    if (image == VK_NULL_HANDLE) {
        throw runtime_error("Image creation failed");
    }

    manager.destroyImage(image);  // Should not throw
}

//=============================================================================
// Object Statistics and Validation Tests
//=============================================================================

void testObjectStatistics() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 3;
    config.descriptorPoolCount = 2;
    config.bufferCount = 50;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create some objects
    manager.createCommandPool(0);
    manager.createCommandPool(1);

    vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 }
    };
    manager.createDescriptorPool(poolSizes, 10);

    VkBuffer buffer1 = manager.createBuffer(1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VkBuffer buffer2 = manager.createBuffer(2048, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    auto stats = manager.getStatistics();

    if (stats.commandPoolsCreated != 2) {
        throw runtime_error("Command pool statistics incorrect");
    }

    if (stats.descriptorPoolsCreated != 1) {
        throw runtime_error("Descriptor pool statistics incorrect");
    }

    if (stats.buffersActive != 2) {
        throw runtime_error("Buffer statistics incorrect");
    }

    // Destroy one buffer
    manager.destroyBuffer(buffer1);
    stats = manager.getStatistics();

    if (stats.buffersActive != 1) {
        throw runtime_error("Buffer count not updated after destruction");
    }
}

void testObjectCountValidation() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 3;
    config.bufferCount = 50;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create within limits
    manager.createCommandPool(0);
    manager.createBuffer(1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    if (!manager.validateObjectCounts()) {
        throw runtime_error("Validation failed for counts within limits");
    }
}

void testObjectReservationCalculator() {
    MedicalApplicationProfile profile;
    profile.maxConcurrentScreens = 2;
    profile.maxUIElementsPerScreen = 100;
    profile.maxTextureAtlases = 5;
    profile.maxConcurrentImages = 10;
    profile.safetyMarginMultiplier = 2.0f;

    auto config = ObjectReservationCalculator::calculate(profile);

    if (!config.isValid()) {
        throw runtime_error("Calculator produced invalid configuration");
    }

    if (config.commandPoolCount == 0) {
        throw runtime_error("Calculator produced zero command pools");
    }

    if (config.descriptorPoolCount == 0) {
        throw runtime_error("Calculator produced zero descriptor pools");
    }

    if (config.bufferCount == 0) {
        throw runtime_error("Calculator produced zero buffers");
    }
}

void testObjectReservationCalculatorSafetyMargin() {
    uint32_t baseCount = 10;
    float safetyMargin = 2.5f;

    uint32_t adjusted = ObjectReservationCalculator::applySafetyMargin(baseCount, safetyMargin);

    if (adjusted != 25) {
        throw runtime_error(
            "Safety margin calculation incorrect: expected 25, got " + to_string(adjusted)
        );
    }
}

//=============================================================================
// Audit Trail Tests
//=============================================================================

void testAuditReportGeneration() {
    DeviceObjectManager manager;

    ObjectReservationConfiguration config;
    config.commandPoolCount = 3;
    config.bufferCount = 50;
    auto device = MockVulkanDevice::createMockDevice();

    manager.initialize(device, config);

    // Create some objects
    manager.createCommandPool(0);
    manager.createBuffer(1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    string report = manager.generateAuditReport();

    // Verify report contains expected information
    if (report.empty()) {
        throw runtime_error("Audit report is empty");
    }

    if (report.find("deviceObjectManager") == string::npos) {
        throw runtime_error("Audit report missing manager section");
    }

    if (report.find("commandPoolsCreated") == string::npos) {
        throw runtime_error("Audit report missing command pool data");
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

    TestRunner runner;

    // Configuration tests
    cout << "Running Object Reservation Configuration Tests...\n";
    runner.runTest("ObjectReservationConfiguration Defaults", testObjectReservationConfigurationDefaults);
    runner.runTest("ObjectReservationConfiguration Validity", testObjectReservationConfigurationValidity);
    runner.runTest("ObjectReservationConfiguration Memory Limits", testObjectReservationConfigurationMemoryLimits);
    runner.runTest("ObjectReservationConfiguration Device-Lifetime Objects", testObjectReservationConfigurationDeviceLifetimeObjects);

    // Manager initialization tests
    cout << "\nRunning DeviceObjectManager Initialization Tests...\n";
    runner.runTest("DeviceObjectManager Initialization", testDeviceObjectManagerInitialization);
    runner.runTest("DeviceObjectManager Double Initialization", testDeviceObjectManagerDoubleInitialization);

    // Device-lifetime object tests
    cout << "\nRunning Device-Lifetime Object Tests...\n";
    runner.runTest("Command Pool Creation", testCommandPoolCreation);
    runner.runTest("Command Pool Reservation Limit", testCommandPoolReservationLimit);
    runner.runTest("Descriptor Pool Creation", testDescriptorPoolCreation);
    runner.runTest("Query Pool Creation", testQueryPoolCreation);

    // Regular object tests
    cout << "\nRunning Regular Object Tests...\n";
    runner.runTest("Buffer Creation and Destruction", testBufferCreationAndDestruction);
    runner.runTest("Image Creation and Destruction", testImageCreationAndDestruction);

    // Statistics and validation tests
    cout << "\nRunning Object Statistics Tests...\n";
    runner.runTest("Object Statistics", testObjectStatistics);
    runner.runTest("Object Count Validation", testObjectCountValidation);
    runner.runTest("Object Reservation Calculator", testObjectReservationCalculator);
    runner.runTest("Object Reservation Calculator Safety Margin", testObjectReservationCalculatorSafetyMargin);

    // Audit trail tests
    cout << "\nRunning Audit Trail Tests...\n";
    runner.runTest("Audit Report Generation", testAuditReportGeneration);

    runner.printSummary();

    return runner.allTestsPassed() ? 0 : 1;
}
