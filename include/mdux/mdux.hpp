/**
 * @brief Main header file for MduX - Medical Device User eXperience Library
 *
 * MduX is a modern C++23 header-only UI library designed for medical device software.
 * This library provides safe, compliant, and efficient user interface components
 * for Class B and Class C medical devices using Vulkan graphics API.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <optional>
#include <fstream>
#include <string>
#include <cstdlib>
#include <filesystem>

// Platform-specific includes for Vulkan surface creation
#ifdef MDUX_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #define VK_USE_PLATFORM_WIN32_KHR
#elif defined(MDUX_PLATFORM_LINUX)
    #define VK_USE_PLATFORM_XLIB_KHR
    #define VK_USE_PLATFORM_WAYLAND_KHR
#endif

// Vulkan headers (mandatory for UI library)
#include <vulkan/vulkan.hpp>

// GLFW for cross-platform window management (only when available)
#ifdef MDUX_GLFW_AVAILABLE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

// Forward declarations
namespace mdux {
    struct UiContent;
    struct ReloadEvent;
    class HtmlCssLoader;
}

// HTML/CSS support (include early to define types)
#include "FileWatcher.hpp"
#include "CssParser.hpp"
#include "HtmlCssLoader.hpp"

namespace mdux {

/**
 * @brief UI rendering modes for overlay/underlay support
 */
enum class UiRenderMode {
    OVERLAY,    ///< UI rendered on top of 3D content
    UNDERLAY,   ///< UI rendered behind 3D content  
    INTEGRATED ///< UI integrated with 3D rendering
};

/**
 * @brief UI rendering callback interface
 */
struct UiRenderer {
    /**
     * @brief Render UI content callback
     * @param uiContent Current UI content to render
     * @param renderMode How UI should be rendered relative to 3D content
     * @param deltaTime Time since last frame for animations
     */
    std::function<void(const UiContent&, UiRenderMode, float)> renderCallback;
    
    /**
     * @brief UI content update callback (optional)
     * @param uiContent New UI content loaded from file
     */
    std::function<void(const UiContent&)> contentUpdateCallback;
    
    /**
     * @brief Check if renderer is valid
     */
    bool isValid() const noexcept { 
        return static_cast<bool>(renderCallback); 
    }
};

/**
 * @brief UI integration configuration
 */
struct UiIntegration {
    UiRenderMode renderMode = UiRenderMode::OVERLAY;  ///< Default render mode
    bool enableHotReload = true;                      ///< Enable hot-reload by default
    std::filesystem::path htmlCssPath;                ///< Path to UI definition file
    UiRenderer renderer;                              ///< UI renderer callbacks
    
    /**
     * @brief Check if integration is properly configured
     */
    bool isConfigured() const noexcept {
        return !htmlCssPath.empty() && renderer.isValid();
    }
};

/**
 * @brief Version information for MduX library
 */
struct Version {
    static constexpr std::uint32_t major = MDUX_VERSION_MAJOR;
    static constexpr std::uint32_t minor = MDUX_VERSION_MINOR;
    static constexpr std::uint32_t patch = MDUX_VERSION_PATCH;

    /**
     * @brief Get version string in format "major.minor.patch"
     * @return Version string
     */
    static constexpr std::string_view getString() noexcept { return "0.1.0"; }
};

/**
 * @brief Medical device compliance information
 */
struct Compliance {
    static constexpr bool isMedicalDeviceCompliant = MDUX_MEDICAL_DEVICE_COMPLIANCE;
    static constexpr std::string_view standards = "IEC 62304, IEC 62366";
    static constexpr std::string_view safetyClass = "Class B/C Medical Device Software";
};

/**
 * @brief Graphics support information
 */
struct Graphics {
    static constexpr bool isEnabled = true;
    
    /**
     * @brief Get available Vulkan API version string
     * @return Vulkan version string (e.g., "Vulkan 1.4")
     */
    static std::string getApiVersion() noexcept {
        try {
            uint32_t apiVersion = 0;
            if (vk::enumerateInstanceVersion(&apiVersion) == vk::Result::eSuccess) {
                uint32_t major = VK_VERSION_MAJOR(apiVersion);
                uint32_t minor = VK_VERSION_MINOR(apiVersion);
                return "Vulkan " + std::to_string(major) + "." + std::to_string(minor);
            }
        } catch (...) {
            // Fallback if version enumeration fails
        }
        return "Vulkan 1.3"; // Fallback for older implementations
    }
    
    static constexpr std::string_view api = "Vulkan"; // Base API name

    static constexpr std::string_view surfaceType =
#ifdef MDUX_PLATFORM_WINDOWS
        "Win32 Surface";
#elif defined(MDUX_PLATFORM_LINUX)
        "X11/Wayland Surface";
#else
        "Unknown";
#endif

    static constexpr std::uint32_t vulkanVersionMajor = MDUX_VULKAN_VERSION_MAJOR;
    static constexpr std::uint32_t vulkanVersionMinor = MDUX_VULKAN_VERSION_MINOR;
    static constexpr std::uint32_t vulkanVersionPatch = MDUX_VULKAN_VERSION_PATCH;
    static constexpr bool validationLayersEnabled = MDUX_VULKAN_VALIDATION_LAYERS;
};

#ifdef MDUX_GLFW_AVAILABLE
/**
 * @brief Window configuration structure
 */
struct WindowConfig {
    std::uint32_t width = 800;
    std::uint32_t height = 600;
    std::string title = "MduX Medical Device Application";
    bool resizable = true;
    bool vsync = true;
    bool fullscreen = false;
    
    /**
     * @brief Create WindowConfig from HTML/CSS file
     * @param htmlCssPath Path to HTML or CSS file
     * @return WindowConfig with properties from file, or default values on error
     */
    static WindowConfig fromHtmlCss(const std::filesystem::path& htmlCssPath);
};

/**
 * @brief Cross-platform window class for medical device UI with Vulkan support
 */
// GLFW reference counter for proper lifecycle management
namespace detail {
    static std::atomic<int> glfwRefCount{0};
    static std::mutex glfwMutex;
}

class Window {
private:
    GLFWwindow* window = nullptr;
    WindowConfig config;
    vk::SurfaceKHR surface;
    vk::Instance instance;
    
    // UI integration support
    std::unique_ptr<UiIntegration> uiIntegration;
    std::unique_ptr<HtmlCssLoader> uiLoader;
    UiContent currentUiContent;
    std::chrono::steady_clock::time_point lastFrameTime;

public:
    /**
     * @brief Create a new window with configuration from HTML/CSS file
     * @param htmlCssPath Path to HTML or CSS file containing window configuration
     */
    explicit Window(const std::filesystem::path& htmlCssPath) 
        : Window(WindowConfig::fromHtmlCss(htmlCssPath)) {
    }
    
    /**
     * @brief Create a new window with specified configuration
     * @param config Window configuration parameters
     */
    explicit Window(const WindowConfig& windowConfig = {}) : config(windowConfig) {
        // Smart platform selection for cross-platform compatibility
        configurePlatformHints();
        
        // Thread-safe GLFW initialization with reference counting
        {
            std::lock_guard<std::mutex> lock(detail::glfwMutex);
            if (detail::glfwRefCount.fetch_add(1) == 0) {
                if (!glfwInit()) {
                    detail::glfwRefCount.fetch_sub(1);
                    throw std::runtime_error("Failed to initialize GLFW");
                }
            }
        }

        // Set Vulkan hints as per ADR-001 revision
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, windowConfig.resizable ? GLFW_TRUE : GLFW_FALSE);

        // Create window
        window = glfwCreateWindow(static_cast<int>(windowConfig.width), static_cast<int>(windowConfig.height),
                                    windowConfig.title.data(),
                                    windowConfig.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);

        if (!window) {
            // Decrement ref count if window creation failed
            {
                std::lock_guard<std::mutex> lock(detail::glfwMutex);
                if (detail::glfwRefCount.fetch_sub(1) == 1) {
                    glfwTerminate();
                }
            }
            throw std::runtime_error("Failed to create GLFW window");
        }

        // Set user pointer for callbacks
        glfwSetWindowUserPointer(window, this);

        // Show the window (required for visibility in some environments)
        glfwShowWindow(window);
        
        // WSL debug: Force window to front and center
        glfwFocusWindow(window);
        
        // Center window on screen for better visibility
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                int xpos = (mode->width - static_cast<int>(windowConfig.width)) / 2;
                int ypos = (mode->height - static_cast<int>(windowConfig.height)) / 2;
                glfwSetWindowPos(window, xpos, ypos);
            }
        }

        // Initialize Vulkan instance (basic setup)
        initializeVulkan();
    }

    /**
     * @brief Destructor - cleanup window resources
     */
    ~Window() {
        stopUiIntegration();  // NEW: Clean up UI integration first
        cleanupVulkan();
        if (window) {
            glfwDestroyWindow(window);
        }
        // Thread-safe GLFW cleanup with reference counting
        {
            std::lock_guard<std::mutex> lock(detail::glfwMutex);
            if (detail::glfwRefCount.fetch_sub(1) == 1) {
                glfwTerminate();
            }
        }
    }

    // Non-copyable but movable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept 
        : window(other.window), config(other.config), surface(other.surface), instance(other.instance),
          uiIntegration(std::move(other.uiIntegration)), uiLoader(std::move(other.uiLoader)),
          currentUiContent(std::move(other.currentUiContent)), lastFrameTime(other.lastFrameTime) {
        other.window = nullptr;
        other.surface = nullptr;
        other.instance = nullptr;
        // No need to adjust ref count - just transferring ownership
    }
    Window& operator=(Window&& other) noexcept {
        if (this != &other) {
            // Clean up current resources without adjusting ref count (will be transferred)
            stopUiIntegration();  // NEW: Clean up UI integration
            cleanupVulkan();
            if (window) {
                glfwDestroyWindow(window);
            }
            // Take ownership of other's resources
            window = other.window;
            config = other.config;
            surface = other.surface;
            instance = other.instance;
            uiIntegration = std::move(other.uiIntegration);
            uiLoader = std::move(other.uiLoader);
            currentUiContent = std::move(other.currentUiContent);
            lastFrameTime = other.lastFrameTime;
            // Reset other's resources
            other.window = nullptr;
            other.surface = nullptr;
            other.instance = nullptr;
            // No ref count adjustment needed - just transferring ownership
        }
        return *this;
    }

    /**
     * @brief Check if window should close
     * @return true if window should close, false otherwise
     */
    bool shouldClose() const noexcept { return window ? glfwWindowShouldClose(window) : true; }

    /**
     * @brief Process window events
     */
    void pollEvents() noexcept { glfwPollEvents(); }

    /**
     * @brief Present rendered frame (Vulkan equivalent of swap buffers)
     */
    void presentFrame() noexcept {
        // Vulkan presentation will be implemented in future versions
        // Currently a placeholder for API compatibility
    }

    /**
     * @brief Get window size
     * @return pair of width and height
     */
    std::pair<int, int> getSize() const noexcept {
        int width, height;
        if (window) {
            glfwGetWindowSize(window, &width, &height);
            return {width, height};
        }
        return {0, 0};
    }

    /**
     * @brief Set window title
     * @param title New window title
     */
    void setTitle(std::string_view title) noexcept {
        if (window) {
            glfwSetWindowTitle(window, title.data());
        }
    }

    /**
     * @brief Set window size (resize existing window)
     * @param width New window width
     * @param height New window height
     */
    void setSize(int width, int height) noexcept {
        if (window) {
            glfwSetWindowSize(window, width, height);
            // Update internal config to reflect new size
            config.width = static_cast<std::uint32_t>(width);
            config.height = static_cast<std::uint32_t>(height);
        }
    }

    /**
     * @brief Apply window configuration without recreation
     * @param newConfig Configuration to apply
     * @return true if applied successfully, false if recreation needed
     */
    bool applyConfig(const WindowConfig& newConfig) noexcept {
        if (!window) return false;
        
        bool needsRecreation = false;
        
        // Check if any properties require window recreation
        if (config.fullscreen != newConfig.fullscreen ||
            config.resizable != newConfig.resizable ||
            config.vsync != newConfig.vsync) {
            needsRecreation = true;
        }
        
        if (needsRecreation) {
            return false; // Caller should recreate window
        }
        
        // Apply properties that can be changed dynamically
        if (config.width != newConfig.width || config.height != newConfig.height) {
            setSize(static_cast<int>(newConfig.width), static_cast<int>(newConfig.height));
        }
        
        if (config.title != newConfig.title) {
            setTitle(newConfig.title);
        }
        
        config = newConfig;
        return true;
    }

    /**
     * @brief Get native GLFW window handle
     * @return GLFWwindow pointer
     */
    GLFWwindow* getNativeHandle() const noexcept { return window; }

    /**
     * @brief Get Vulkan surface
     * @return Vulkan surface handle
     */
    vk::SurfaceKHR getSurface() const noexcept { return surface; }

    /**
     * @brief Get Vulkan instance
     * @return Vulkan instance handle
     */
    vk::Instance getInstance() const noexcept { return instance; }
    
    /**
     * @brief Configure UI integration for overlay/underlay rendering
     * @param integration UI integration configuration
     * @return true if UI integration was set up successfully
     */
    bool setupUiIntegration(UiIntegration integration);
    
    /**
     * @brief Update UI content manually (without hot-reload)
     * @param uiContent New UI content to display
     */
    void updateUiContent(const UiContent& uiContent);
    
    /**
     * @brief Get current UI content
     * @return Current UI content being displayed
     */
    const UiContent& getCurrentUiContent() const noexcept { return currentUiContent; }
    
    /**
     * @brief Check if UI integration is active
     * @return true if UI integration is configured and active
     */
    bool hasUiIntegration() const noexcept { return uiIntegration && uiIntegration->isConfigured(); }
    
    /**
     * @brief Get UI render mode
     * @return Current UI render mode (overlay/underlay/integrated)
     */
    UiRenderMode getUiRenderMode() const noexcept {
        return uiIntegration ? uiIntegration->renderMode : UiRenderMode::OVERLAY;
    }
    
    /**
     * @brief Render UI content (called during frame rendering)
     * @param deltaTime Time since last frame for animations
     * 
     * This method should be called by the user's rendering loop at the
     * appropriate point (before 3D content for underlay, after for overlay)
     */
    void renderUi(float deltaTime = 0.0f);
    
    /**
     * @brief Stop UI integration and hot-reload
     */
    void stopUiIntegration();

private:
    /**
     * @brief Configure GLFW platform hints based on environment detection
     */
    void configurePlatformHints() {
        // Check for manual override via environment variable
        const char* forcePlatform = getEnvironmentVariable("MDUX_FORCE_PLATFORM");
        if (forcePlatform) {
            if (std::string_view(forcePlatform) == "X11") {
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
                return;
            } else if (std::string_view(forcePlatform) == "WAYLAND") {
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
                return;
            }
            // If invalid value, fall through to auto-detection
        }
        
        // Detect WSL environment (needs X11 platform forcing)
        if (isWslEnvironment()) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
            return;
        }
        
        // For native Linux, let GLFW choose the best platform automatically
        // This allows Wayland systems to use Wayland, X11 systems to use X11
        // No platform hint = GLFW automatic selection
    }
    
    /**
     * @brief Secure cross-platform environment variable access
     * @param name Environment variable name
     * @return Environment variable value or nullptr if not found
     */
    const char* getEnvironmentVariable(const char* name) const noexcept {
#ifdef _WIN32
        // Windows: Use secure _dupenv_s when possible, fallback to getenv
        char* value = nullptr;
        size_t size = 0;
        if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
            // Note: This creates a memory leak, but it's acceptable for environment
            // variables that are typically accessed once during initialization
            return value;
        }
        return nullptr;
#else
        // Unix/Linux: getenv is safe on these platforms
        return std::getenv(name);
#endif
    }
    
    /**
     * @brief Detect if running in WSL environment
     * @return true if WSL detected, false otherwise
     */
    bool isWslEnvironment() const noexcept {
        // Method 1: Check /proc/version for Microsoft signature
        std::ifstream procVersion("/proc/version");
        if (procVersion.is_open()) {
            std::string line;
            if (std::getline(procVersion, line)) {
                if (line.find("Microsoft") != std::string::npos || 
                    line.find("WSL") != std::string::npos) {
                    return true;
                }
            }
        }
        
        // Method 2: Check WSL-specific environment variables
        if (getEnvironmentVariable("WSL_DISTRO_NAME") || getEnvironmentVariable("WSLENV")) {
            return true;
        }
        
        // Method 3: Check for WSL filesystem signature
        std::ifstream wslInterop("/proc/sys/fs/binfmt_misc/WSLInterop");
        if (wslInterop.is_open()) {
            return true;
        }
        
        return false;
    }

    /**
     * @brief Initialize Vulkan instance and surface
     */
    void initializeVulkan() {
        // Determine the highest available Vulkan API version
        uint32_t apiVersion = VK_API_VERSION_1_3; // Fallback to 1.3
        try {
            if (vk::enumerateInstanceVersion(&apiVersion) != vk::Result::eSuccess) {
                apiVersion = VK_API_VERSION_1_3; // Use fallback if enumeration fails
            }
        } catch (...) {
            apiVersion = VK_API_VERSION_1_3; // Use fallback on exception
        }
        
        // Basic Vulkan initialization
        // Full implementation will be added in future versions
        vk::ApplicationInfo appInfo(
            "MduX Medical Device Application",    // pApplicationName
            VK_MAKE_VERSION(0, 1, 0),           // applicationVersion
            "MduX",                             // pEngineName
            VK_MAKE_VERSION(0, 1, 0),           // engineVersion
            apiVersion                          // Use detected API version
        );

        std::vector<const char*> extensions;
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);

        vk::InstanceCreateInfo createInfo(
            {},                                 // flags
            &appInfo,                          // pApplicationInfo
            0,                                 // enabledLayerCount
            nullptr,                           // ppEnabledLayerNames
            static_cast<uint32_t>(extensions.size()), // enabledExtensionCount
            extensions.data()                  // ppEnabledExtensionNames
        );

        instance = vk::createInstance(createInfo);

        // Create surface
        VkSurfaceKHR surfaceHandle;
        if (glfwCreateWindowSurface(instance, window, nullptr, &surfaceHandle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan surface");
        }
        surface = vk::SurfaceKHR(surfaceHandle);
    }

    /**
     * @brief Cleanup Vulkan resources
     */
    void cleanupVulkan() {
        try {
            if (surface && instance) {
                instance.destroySurfaceKHR(surface);
                surface = nullptr;
            }
            if (instance) {
                instance.destroy();
                instance = nullptr;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Vulkan cleanup error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Warning: Unknown Vulkan cleanup error" << std::endl;
        }
    }
    
    /**
     * @brief Handle UI hot-reload events
     * @param event Reload event with updated content
     */
    void onUiReload(const ReloadEvent& event);
};
#endif // MDUX_GLFW_AVAILABLE

/**
 * @brief Initialize MduX library
 * @return true if initialization successful, false otherwise
 */
inline bool initialize() noexcept {
    // Vulkan initialization is handled per-window for better resource management
    // This function can be used for global library initialization if needed
    return true;
}

/**
 * @brief Shutdown MduX library
 */
inline void shutdown() noexcept {
    // Global cleanup if needed
    // Individual windows handle their own Vulkan cleanup
}

} // namespace mdux

namespace mdux {

#ifdef MDUX_GLFW_AVAILABLE
// WindowConfig implementation (needs to be after includes)
inline WindowConfig WindowConfig::fromHtmlCss(const std::filesystem::path& htmlCssPath) {
    WindowConfig config;
    
    try {
        auto windowStyle = loadWindowStyleFromFile(htmlCssPath);
        
        if (windowStyle.width) config.width = *windowStyle.width;
        if (windowStyle.height) config.height = *windowStyle.height;
        if (windowStyle.title) config.title = *windowStyle.title;
        if (windowStyle.resizable) config.resizable = *windowStyle.resizable;
        if (windowStyle.vsync) config.vsync = *windowStyle.vsync;
        if (windowStyle.fullscreen) config.fullscreen = *windowStyle.fullscreen;
        
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to load window config from " << htmlCssPath 
                  << ": " << e.what() << ". Using defaults." << std::endl;
    }
    
    return config;
}

// UI Integration implementation
bool Window::setupUiIntegration(UiIntegration integration) {
    if (!integration.isConfigured()) {
        std::cerr << "Window: UI integration is not properly configured" << std::endl;
        return false;
    }
    
    // Stop any existing integration
    stopUiIntegration();
    
    // Store integration config
    uiIntegration = std::make_unique<UiIntegration>(std::move(integration));
    
    // Initialize UI loader if hot-reload is enabled
    if (uiIntegration->enableHotReload) {
        uiLoader = std::make_unique<HtmlCssLoader>();
        
        auto reloadCallback = [this](const ReloadEvent& event) {
            this->onUiReload(event);
        };
        
        if (!uiLoader->startWatching(uiIntegration->htmlCssPath, reloadCallback)) {
            std::cerr << "Window: Failed to start UI hot-reload for " << uiIntegration->htmlCssPath << std::endl;
            return false;
        }
    } else {
        // Load UI content once without hot-reload
        HtmlCssLoader loader;
        auto event = loader.loadFile(uiIntegration->htmlCssPath);
        if (event.isSuccess()) {
            currentUiContent = event.uiContent;
        } else {
            std::cerr << "Window: Failed to load UI content: " << event.errorMessage << std::endl;
            return false;
        }
    }
    
    // Initialize frame timing
    lastFrameTime = std::chrono::steady_clock::now();
    
    std::cout << "Window: UI integration setup complete for " << uiIntegration->htmlCssPath.filename() << std::endl;
    return true;
}

void Window::updateUiContent(const UiContent& uiContent) {
    currentUiContent = uiContent;
    
    // Notify renderer of content update
    if (uiIntegration && uiIntegration->renderer.contentUpdateCallback) {
        uiIntegration->renderer.contentUpdateCallback(currentUiContent);
    }
}

void Window::renderUi(float deltaTime) {
    if (!hasUiIntegration() || !uiIntegration->renderer.renderCallback) {
        return;  // No UI integration or renderer configured
    }
    
    // Calculate delta time if not provided
    if (deltaTime <= 0.0f) {
        auto currentTime = std::chrono::steady_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;
    }
    
    // Call user's UI render callback
    uiIntegration->renderer.renderCallback(currentUiContent, uiIntegration->renderMode, deltaTime);
}

void Window::stopUiIntegration() {
    if (uiLoader) {
        uiLoader->stopWatching();
        uiLoader.reset();
    }
    uiIntegration.reset();
    currentUiContent = UiContent{};  // Clear UI content
}

void Window::onUiReload(const ReloadEvent& event) {
    if (!event.isSuccess()) {
        std::cerr << "Window: UI reload failed: " << event.errorMessage << std::endl;
        return;
    }
    
    // Check if window configuration changed
    if (event.windowConfigChanged) {
        std::cout << "Window: Window configuration changed, applying dynamic resize..." << std::endl;
        
        // Convert to WindowConfig and apply
        WindowConfig newConfig = config;  // Start with current config
        if (event.windowStyle.width) newConfig.width = *event.windowStyle.width;
        if (event.windowStyle.height) newConfig.height = *event.windowStyle.height;
        if (event.windowStyle.title) newConfig.title = *event.windowStyle.title;
        if (event.windowStyle.resizable) newConfig.resizable = *event.windowStyle.resizable;
        if (event.windowStyle.vsync) newConfig.vsync = *event.windowStyle.vsync;
        if (event.windowStyle.fullscreen) newConfig.fullscreen = *event.windowStyle.fullscreen;
        
        // Apply configuration without recreation when possible
        if (!applyConfig(newConfig)) {
            std::cout << "Window: Window recreation required for configuration changes" << std::endl;
            // Note: For full hot-reload, window recreation would need to be handled
            // by the application, not the Window class itself
        }
    }
    
    // Update UI content if it changed
    if (event.uiContentChanged) {
        std::cout << "Window: UI content changed - updating without interrupting rendering" << std::endl;
        updateUiContent(event.uiContent);
    }
}

#endif // MDUX_GLFW_AVAILABLE

} // namespace mdux