/**
 * @brief MduX C++23 Module Interface - Medical Device User eXperience Library
 * 
 * Ultra-sleek C++23 modules-based medical device UI library with Vulkan graphics.
 * This module provides safe, compliant, and efficient user interface components
 * for Class B and Class C medical devices.
 */

module;

// Platform-specific includes for Vulkan surface creation (minimal C includes only)
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

// Only include C headers that don't conflict with import std
#include <vulkan/vulkan.h>  // Use C API instead of C++ API
#include <stdint.h>         // For uint32_t and other integer types

// GLFW C API (doesn't conflict with import std)
#ifdef MDUX_GLFW_AVAILABLE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

export module mdux;

// Import standard library modules (C++23 approach)
import std;

// Forward declarations and minimal definitions for UI types
export namespace mdux {
    // UI content structure (minimal definition)
    struct UiContent {
        std::string title;
        std::string htmlContent;
        std::string cssContent;
        std::vector<std::string> errors;
        
        bool isValid() const noexcept { return errors.empty(); }
    };
    
    // Reload event structure (minimal definition)
    struct ReloadEvent {
        std::filesystem::path filePath;
        UiContent uiContent;
        bool windowConfigChanged = false;
        bool uiContentChanged = false;
        std::string errorMessage;
        
        struct WindowStyle {
            std::optional<std::uint32_t> width;
            std::optional<std::uint32_t> height;
            std::optional<std::string> title;
            std::optional<bool> resizable;
            std::optional<bool> vsync;
            std::optional<bool> fullscreen;
        } windowStyle;
        
        bool isSuccess() const noexcept { return errorMessage.empty(); }
        bool isUiOnlyChange() const noexcept { return uiContentChanged && !windowConfigChanged; }
    };
    
    // File watcher callback type
    using FileChangeCallback = std::function<void(const ReloadEvent&)>;
    
    // Simple file loader class (defined inline to avoid header dependencies)
    class HtmlCssLoader {
    public:
        HtmlCssLoader() = default;
        ~HtmlCssLoader() = default;
        
        // Non-copyable but movable
        HtmlCssLoader(const HtmlCssLoader&) = delete;
        HtmlCssLoader& operator=(const HtmlCssLoader&) = delete;
        HtmlCssLoader(HtmlCssLoader&&) = default;
        HtmlCssLoader& operator=(HtmlCssLoader&&) = default;
        
        ReloadEvent loadFile(const std::filesystem::path& filePath);
        bool startWatching(const std::filesystem::path& filePath, FileChangeCallback callback);
        void stopWatching();
        bool isWatching() const noexcept { return watching; }
        
    private:
        std::filesystem::path watchedFile;
        FileChangeCallback changeCallback;
        std::atomic<bool> watching{false};
        std::atomic<bool> shouldStop{false};
        std::thread watchThread;
        std::filesystem::file_time_type lastWriteTime;
        
        void watchLoop();
        std::filesystem::file_time_type getFileTime(const std::filesystem::path& path);
    };
}

export namespace mdux {

//=============================================================================
// Version and Compliance Information
//=============================================================================

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
        uint32_t apiVersion = 0;
        if (vkEnumerateInstanceVersion(&apiVersion) == VK_SUCCESS) {
            uint32_t major = VK_VERSION_MAJOR(apiVersion);
            uint32_t minor = VK_VERSION_MINOR(apiVersion);
            return "Vulkan " + std::to_string(major) + "." + std::to_string(minor);
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

//=============================================================================
// UI System Types
//=============================================================================

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

//=============================================================================
// Window System
//=============================================================================

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

// GLFW reference counter for proper lifecycle management (when available)
#ifdef MDUX_GLFW_AVAILABLE
namespace detail {
    extern std::atomic<int> glfwRefCount;
    extern std::mutex glfwMutex;
}
#endif

/**
 * @brief Cross-platform window class for medical device UI with Vulkan support
 */
class Window {
private:
#ifdef MDUX_GLFW_AVAILABLE
    GLFWwindow* window = nullptr;
#else
    void* window = nullptr;  // Placeholder when GLFW not available
#endif
    WindowConfig config;
    VkSurfaceKHR surface;
    VkInstance instance;
    
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
    explicit Window(const std::filesystem::path& htmlCssPath);
    
    /**
     * @brief Create a new window with specified configuration
     * @param config Window configuration parameters
     */
    explicit Window(const WindowConfig& windowConfig = {});

    /**
     * @brief Destructor - cleanup window resources
     */
    ~Window();

    // Non-copyable but movable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    /**
     * @brief Check if window should close
     * @return true if window should close, false otherwise
     */
    bool shouldClose() const noexcept;

    /**
     * @brief Process window events
     */
    void pollEvents() noexcept;

    /**
     * @brief Present rendered frame (Vulkan equivalent of swap buffers)
     */
    void presentFrame() noexcept;

    /**
     * @brief Get window size
     * @return pair of width and height
     */
    std::pair<int, int> getSize() const noexcept;

    /**
     * @brief Set window title
     * @param title New window title
     */
    void setTitle(std::string_view title) noexcept;

    /**
     * @brief Set window size (resize existing window)
     * @param width New window width
     * @param height New window height
     */
    void setSize(int width, int height) noexcept;

    /**
     * @brief Apply window configuration without recreation
     * @param newConfig Configuration to apply
     * @return true if applied successfully, false if recreation needed
     */
    bool applyConfig(const WindowConfig& newConfig) noexcept;

    /**
     * @brief Get native window handle
     * @return Native window handle (GLFWwindow* when GLFW available, nullptr otherwise)
     */
#ifdef MDUX_GLFW_AVAILABLE
    GLFWwindow* getNativeHandle() const noexcept;
#else
    void* getNativeHandle() const noexcept;
#endif

    /**
     * @brief Get Vulkan surface
     * @return Vulkan surface handle
     */
    VkSurfaceKHR getSurface() const noexcept;

    /**
     * @brief Get Vulkan instance
     * @return Vulkan instance handle
     */
    VkInstance getInstance() const noexcept;
    
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
    const UiContent& getCurrentUiContent() const noexcept;
    
    /**
     * @brief Check if UI integration is active
     * @return true if UI integration is configured and active
     */
    bool hasUiIntegration() const noexcept;
    
    /**
     * @brief Get UI render mode
     * @return Current UI render mode (overlay/underlay/integrated)
     */
    UiRenderMode getUiRenderMode() const noexcept;
    
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
    void configurePlatformHints();
    
    /**
     * @brief Secure cross-platform environment variable access
     * @param name Environment variable name
     * @return Environment variable value or nullptr if not found
     */
    const char* getEnvironmentVariable(const char* name) const noexcept;
    
    /**
     * @brief Detect if running in WSL environment
     * @return true if WSL detected, false otherwise
     */
    bool isWslEnvironment() const noexcept;

    /**
     * @brief Initialize Vulkan instance and surface
     */
    void initializeVulkan();

    /**
     * @brief Cleanup Vulkan resources
     */
    void cleanupVulkan();
    
    /**
     * @brief Handle UI hot-reload events
     * @param event Reload event with updated content
     */
    void onUiReload(const ReloadEvent& event);
};

//=============================================================================
// Library Management Functions
//=============================================================================

/**
 * @brief Initialize MduX library
 * @return true if initialization successful, false otherwise
 */
bool initialize() noexcept;

/**
 * @brief Shutdown MduX library
 */
void shutdown() noexcept;

} // export namespace mdux