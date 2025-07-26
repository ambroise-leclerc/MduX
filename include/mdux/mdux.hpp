/**
 * @brief Main header file for MduX - Medical Device User eXperience Library
 *
 * MduX is a modern C++23 header-only UI library designed for medical device software.
 * This library provides safe, compliant, and efficient user interface components
 * for Class B and Class C medical devices using Vulkan graphics API.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <optional>
#include <fstream>
#include <string>
#include <cstdlib>

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

namespace mdux {

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
    std::string_view title = "MduX Medical Device Application";
    bool resizable = true;
    bool vsync = true;
    bool fullscreen = false;
};

/**
 * @brief Cross-platform window class for medical device UI with Vulkan support
 */
class Window {
private:
    GLFWwindow* window = nullptr;
    WindowConfig config;
    bool shouldCloseFlag = false;
    vk::SurfaceKHR surface;
    vk::Instance instance;

public:
    /**
     * @brief Create a new window with specified configuration
     * @param config Window configuration parameters
     */
    explicit Window(const WindowConfig& windowConfig = {}) : config(windowConfig) {
        // Smart platform selection for cross-platform compatibility
        configurePlatformHints();
        
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        // Set Vulkan hints as per ADR-001 revision
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, windowConfig.resizable ? GLFW_TRUE : GLFW_FALSE);

        // Create window
        window = glfwCreateWindow(static_cast<int>(windowConfig.width), static_cast<int>(windowConfig.height),
                                    windowConfig.title.data(),
                                    windowConfig.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);

        if (!window) {
            glfwTerminate();
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
        cleanupVulkan();
        if (window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    // Non-copyable but movable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept : window(other.window), config(other.config) {
        other.window = nullptr;
    }
    Window& operator=(Window&& other) noexcept {
        if (this != &other) {
            if (window) {
                glfwDestroyWindow(window);
            }
            window = other.window;
            config = other.config;
            other.window = nullptr;
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
        if (surface) {
            instance.destroySurfaceKHR(surface);
        }
        if (instance) {
            instance.destroy();
        }
    }
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