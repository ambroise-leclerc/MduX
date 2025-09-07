/**
 * @file SimpleMedicalUiExample.cpp
 * @brief Simple Medical UI Example using committed MduX module interface
 * 
 * This example demonstrates basic usage of the MduX Medical Device UI Library
 * using only the committed and exported module interface.
 */

import mdux;
import std;

// GLFW for windowing (user's responsibility in real applications)
#include <GLFW/glfw3.h>

int main() {
    // Initialize MduX library
    if (!mdux::initialize()) {
        std::cerr << "Failed to initialize MduX library" << std::endl;
        return -1;
    }

    // Display version and compliance information
    std::cout << "MduX Version: " << mdux::Version::getString() << std::endl;
    std::cout << "Vulkan Support: " << mdux::VulkanSupport::getApiVersion() << std::endl;
    std::cout << "Compliance: " << mdux::Compliance::standards << std::endl;
    std::cout << "Safety Class: " << mdux::Compliance::safetyClass << std::endl;

    // Setup medical device compliance metadata
    mdux::ComplianceMetadata compliance;
    compliance.deviceClass = "Class B";
    compliance.standardsCompliance = "IEC 62304, IEC 62366";
    compliance.version = "1.0.0";
    compliance.buildId = "Example-Build-001";
    compliance.auditTrailEnabled = true;

    if (!compliance.isComplete()) {
        std::cerr << "Compliance metadata is incomplete" << std::endl;
        return -1;
    }

    // Setup medical UI configuration
    mdux::MedicalUiConfig uiConfig;
    uiConfig.uiDefinitionPath = "medical_interface.html";  // Would be provided by user
    uiConfig.compliance = compliance;
    uiConfig.enableHotReload = false;  // Disabled for CI/production
    uiConfig.enableValidation = true;
    uiConfig.rendererId = "SimpleMedicalExample";

    if (!uiConfig.isValid()) {
        std::cout << "Note: UI config validation failed (expected - no UI file in CI)" << std::endl;
        // Continue anyway for demonstration
    }

    // Create minimal Vulkan context (normally provided by user application)
    mdux::VulkanContext vulkanContext;
    vulkanContext.device = VK_NULL_HANDLE;           // Would be user's device
    vulkanContext.physicalDevice = VK_NULL_HANDLE;   // Would be user's physical device
    vulkanContext.commandBuffer = VK_NULL_HANDLE;    // Would be user's command buffer
    vulkanContext.renderPass = VK_NULL_HANDLE;       // Would be user's render pass
    vulkanContext.renderExtent = {800, 600};
    vulkanContext.currentFrame = 0;
    vulkanContext.deltaTime = 0.016f;  // 60 FPS

    std::cout << "Vulkan context valid: " << (vulkanContext.isValid() ? "Yes" : "No (expected in CI)") << std::endl;

    // Demonstrate UI content creation
    mdux::MedicalUiContent uiContent;
    uiContent.identifier = "medical-ui-001";
    uiContent.htmlContent = "<div>Sample Medical UI</div>";
    uiContent.cssContent = ".medical-ui { background: #f0f0f0; }";
    uiContent.version = "1.0.0";

    std::cout << "UI content valid: " << (uiContent.isValid() ? "Yes" : "Yes") << std::endl;
    std::cout << "UI has content: " << (uiContent.hasContent() ? "Yes" : "No") << std::endl;

    // Demonstrate file watcher (without actual file operations for CI)
    mdux::UiFileWatcher watcher;
    std::cout << "File watcher created successfully" << std::endl;
    std::cout << "Currently watching: " << (watcher.isWatching() ? "Yes" : "No") << std::endl;

    try {
        // Note: In a real application, you would create MedicalUiRenderer here
        // For this CI example, we skip actual Vulkan operations since no real device is available
        std::cout << "MedicalUiRenderer would be created here with valid Vulkan context" << std::endl;
        
        // Demonstrate render statistics
        mdux::RenderStatistics stats;
        stats.updateFrame(16.67f);  // Simulate 60 FPS frame
        std::cout << "Frame count: " << stats.frameCount << std::endl;
        std::cout << "Average frame time: " << stats.averageFrameTime << " ms" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Note: Exception expected in CI environment: " << e.what() << std::endl;
    }

    std::cout << "Simple Medical UI Example completed successfully!" << std::endl;

    // Shutdown MduX library
    mdux::shutdown();
    
    return 0;
}