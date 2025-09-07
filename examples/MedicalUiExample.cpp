/**
 * @file MedicalUiExample.cpp
 * @brief Pure Vulkan medical UI integration example
 * 
 * This example demonstrates how to integrate MduX with an existing Vulkan application.
 * It shows the new architecture where MduX complements existing Vulkan setups
 * instead of creating its own windows.
 * 
 * Usage:
 *   ./MedicalUiExample medical_interface.html
 */

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h> // User's windowing solution
#define GLFW_INCLUDE_VULKAN

import std;
import mdux;

class ExistingVulkanApp {
public:
    ExistingVulkanApp() = default;
    ~ExistingVulkanApp() { cleanup(); }
    
    bool initialize() {
        // This represents a user's existing Vulkan application setup
        if (!initializeGLFW() || !initializeVulkan()) {
            return false;
        }
        
        std::cout << "✅ Existing Vulkan application initialized\n";
        return true;
    }
    
    bool initializeMedicalUi(const std::filesystem::path& uiPath) {
        // Setup medical device compliance
        mdux::ComplianceMetadata compliance;
        compliance.deviceClass = "Class B";
        compliance.standardsCompliance = "IEC 62304, IEC 62366, FDA 21 CFR Part 820";
        compliance.version = "1.0.0";
        compliance.buildId = "BUILD-2024-001";
        compliance.auditTrailEnabled = true;
        
        // Initialize MduX library
        if (!mdux::initialize()) {
            std::cerr << "❌ Failed to initialize MduX library\n";
            return false;
        }
        
        // Setup medical UI configuration
        mdux::MedicalUiConfig uiConfig;
        uiConfig.uiDefinitionPath = uiPath;
        uiConfig.compliance = compliance;
        uiConfig.enableHotReload = true; // For development
        uiConfig.enableValidation = true;
        uiConfig.rendererId = "medical-ui-main";
        
        // Create Vulkan context for MduX integration
        mdux::VulkanContext vulkanContext;
        vulkanContext.device = device;
        vulkanContext.physicalDevice = physicalDevice;
        vulkanContext.commandBuffer = commandBuffer; // Use our command buffer
        vulkanContext.renderPass = renderPass;
        vulkanContext.renderExtent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
        vulkanContext.currentFrame = 0;
        vulkanContext.deltaTime = 0.0f;
        
        // Create MduX medical UI renderer
        try {
            medicalUiRenderer = std::make_unique<mdux::MedicalUiRenderer>(vulkanContext, uiConfig);
            std::cout << "✅ Medical UI renderer initialized successfully\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "❌ Failed to create medical UI renderer: " << e.what() << "\n";
            return false;
        }
    }
    
    void run() {
        std::cout << "\n🚀 Running integrated Vulkan + Medical UI application...\n";
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        uint32_t frameIndex = 0;
        
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            // Calculate delta time
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            // Begin frame
            beginFrame();
            
            // Record user's existing Vulkan rendering
            recordExistingVulkanContent();
            
            // Integrate medical UI rendering
            if (medicalUiRenderer) {
                mdux::VulkanContext frameContext;
                frameContext.device = device;
                frameContext.physicalDevice = physicalDevice;
                frameContext.commandBuffer = commandBuffer;
                frameContext.renderPass = renderPass;
                frameContext.renderExtent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
                frameContext.currentFrame = frameIndex;
                frameContext.deltaTime = deltaTime;
                
                if (!medicalUiRenderer->render(frameContext)) {
                    std::cerr << "⚠️  Medical UI rendering failed\n";
                }
            }
            
            // End frame and present
            endFrame();
            frameIndex++;
        }
    }
    
    void displayMedicalUiInfo() {
        if (!medicalUiRenderer) {
            return;
        }
        
        std::cout << "\n╔═══════════════════ Medical UI Status ══════════════════╗\n";
        
        const auto& compliance = medicalUiRenderer->getCompliance();
        std::cout << "║ Device Class: " << compliance.deviceClass << "                                      ║\n";
        std::cout << "║ Standards: " << compliance.standardsCompliance.substr(0, 30) << "...║\n";
        std::cout << "║ Version: " << compliance.version << "                                           ║\n";
        std::cout << "║ Build ID: " << compliance.buildId << "                                  ║\n";
        
        const auto& stats = medicalUiRenderer->getStatistics();
        std::cout << "║ Frames Rendered: " << stats.frameCount << "                               ║\n";
        std::cout << "║ Avg Frame Time: " << std::fixed << std::setprecision(2) 
                  << stats.averageFrameTime << "ms                             ║\n";
        
        const auto& errors = medicalUiRenderer->getValidationErrors();
        std::cout << "║ Validation Errors: " << errors.size() << "                                ║\n";
        
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
        
        if (!errors.empty()) {
            std::cout << "\n⚠️  Validation Errors:\n";
            for (const auto& error : errors) {
                std::cout << "   • " << error << "\n";
            }
        }
    }
    
private:
    // User's existing Vulkan setup
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    
    int windowWidth = 1024;
    int windowHeight = 768;
    
    // MduX integration
    std::unique_ptr<mdux::MedicalUiRenderer> medicalUiRenderer;
    
    bool initializeGLFW() {
        if (!glfwInit()) {
            std::cerr << "❌ Failed to initialize GLFW\n";
            return false;
        }
        
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        
        window = glfwCreateWindow(windowWidth, windowHeight, 
                                "Medical Device UI Integration Demo", nullptr, nullptr);
        if (!window) {
            std::cerr << "❌ Failed to create GLFW window\n";
            return false;
        }
        
        return true;
    }
    
    bool initializeVulkan() {
        // This is a simplified Vulkan initialization
        // Real applications would have more comprehensive setup
        
        // Create Vulkan instance
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Medical Device App";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Custom Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;
        
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;
        
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            std::cerr << "❌ Failed to create Vulkan instance\n";
            return false;
        }
        
        // Find suitable physical device
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        
        if (deviceCount == 0) {
            std::cerr << "❌ No Vulkan-capable GPUs found\n";
            return false;
        }
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        
        // Use first suitable device (simplified)
        for (const auto& dev : devices) {
            if (mdux::VulkanSupport::isDeviceSuitable(dev)) {
                physicalDevice = dev;
                break;
            }
        }
        
        if (physicalDevice == VK_NULL_HANDLE) {
            std::cerr << "❌ No suitable GPU found for medical UI requirements\n";
            return false;
        }
        
        // Create logical device (simplified)
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        
        uint32_t graphicsFamily = UINT32_MAX;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamily = i;
                break;
            }
        }
        
        if (graphicsFamily == UINT32_MAX) {
            std::cerr << "❌ No graphics queue family found\n";
            return false;
        }
        
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsFamily;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        
        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        
        if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
            std::cerr << "❌ Failed to create logical device\n";
            return false;
        }
        
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        presentQueue = graphicsQueue; // Simplified
        
        // Create basic render pass for UI integration
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB; // Simplified
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        
        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            std::cerr << "❌ Failed to create render pass\n";
            return false;
        }
        
        // Create command pool and buffer
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;
        
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            std::cerr << "❌ Failed to create command pool\n";
            return false;
        }
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            std::cerr << "❌ Failed to allocate command buffer\n";
            return false;
        }
        
        return true;
    }
    
    void beginFrame() {
        // Begin recording command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        // Begin render pass (simplified - no actual swapchain)
        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderPass;
        // Note: In real app, would use actual framebuffer
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
        
        VkClearValue clearColor = {{{0.0f, 0.2f, 0.4f, 1.0f}}}; // Medical blue background
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;
        
        // Note: This is simplified - real implementation would begin actual render pass
    }
    
    void recordExistingVulkanContent() {
        // This is where the user's existing Vulkan rendering would go
        // For example: 3D medical visualization, data plots, etc.
        
        // Placeholder for user's existing rendering
        std::cout << "📊 Recording existing medical visualization content...\n";
    }
    
    void endFrame() {
        // End command buffer recording
        vkEndCommandBuffer(commandBuffer);
        
        // Submit and present (simplified)
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue); // Simplified synchronization
        
        // Reset command buffer for next frame
        vkResetCommandBuffer(commandBuffer, 0);
    }
    
    void cleanup() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, commandPool, nullptr);
            }
            if (renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, renderPass, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
        
        if (window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
        
        mdux::shutdown();
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <medical_ui_definition.html>\n";
        std::cerr << "\nExample medical UI files:\n";
        std::cerr << "  • patient_monitor.html - Patient monitoring interface\n";
        std::cerr << "  • device_controls.html - Medical device control panel\n";
        std::cerr << "  • compliance_ui.html - Regulatory compliance interface\n";
        return -1;
    }
    
    std::filesystem::path uiPath = argv[1];
    
    if (!std::filesystem::exists(uiPath)) {
        std::cerr << "❌ UI definition file not found: " << uiPath.string() << "\n";
        return -1;
    }
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          MduX Medical UI Integration Example                 ║\n";
    std::cout << "║     Pure Vulkan Complement Library Demonstration            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\n📁 Loading medical UI from: " << uiPath.filename().string() << "\n";
    
    try {
        ExistingVulkanApp app;
        
        std::cout << "\n🔧 Initializing existing Vulkan application...\n";
        if (!app.initialize()) {
            std::cerr << "❌ Failed to initialize Vulkan application\n";
            return -1;
        }
        
        std::cout << "\n🏥 Integrating medical UI renderer...\n";
        if (!app.initializeMedicalUi(uiPath)) {
            std::cerr << "❌ Failed to initialize medical UI\n";
            return -1;
        }
        
        // Display medical UI information
        app.displayMedicalUiInfo();
        
        std::cout << "\n💡 Key Integration Points:\n";
        std::cout << "  • MduX uses your existing VkDevice and VkRenderPass\n";
        std::cout << "  • No window creation - works with your windowing solution\n";
        std::cout << "  • Medical compliance validation built-in\n";
        std::cout << "  • Hot-reload enabled for UI development\n";
        std::cout << "  • Renders into your existing command buffers\n";
        
        std::cout << "\n🚀 Starting integrated application...\n";
        app.run();
        
        std::cout << "\n✅ Application completed successfully\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Application error: " << e.what() << "\n";
        return -1;
    }
    
    return 0;
}