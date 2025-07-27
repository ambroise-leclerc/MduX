/**
 * @brief MduX C++23 Module Implementation - Medical Device User eXperience Library
 * 
 * Implementation file for ultra-sleek C++23 modules-based medical device UI library.
 * This implementation provides the actual functionality for all exported interfaces.
 */

// Global module fragment for implementation
module;

// Module implementation needs headers in global fragment too
#ifdef MDUX_GLFW_AVAILABLE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif
#include <vulkan/vulkan.h>

// Module declaration
module mdux;

// Import standard library modules (C++23 approach)
import std;

namespace mdux {

#ifdef MDUX_GLFW_AVAILABLE
// Define GLFW reference counter variables
namespace detail {
    std::atomic<int> glfwRefCount{0};
    std::mutex glfwMutex;
}
#endif

//=============================================================================
// HtmlCssLoader Implementation
//=============================================================================

ReloadEvent HtmlCssLoader::loadFile(const std::filesystem::path& filePath) {
    ReloadEvent event;
    event.filePath = filePath;
    
    try {
        if (!std::filesystem::exists(filePath)) {
            event.errorMessage = "File does not exist: " + filePath.string();
            return event;
        }
        
        // Read file content
        std::ifstream file(filePath);
        if (!file.is_open()) {
            event.errorMessage = "Cannot open file: " + filePath.string();
            return event;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        // Simple parsing for HTML and CSS content
        event.uiContent.htmlContent = content;
        event.uiContent.title = "MduX Application";
        
        // Extract window configuration from CSS
        auto extractCssValue = [&](const std::string& property) -> std::string {
            auto pos = content.find(property + ":");
            if (pos != std::string::npos) {
                auto start = content.find_first_not_of(" \t", pos + property.length() + 1);
                auto end = content.find_first_of(";\n}", start);
                if (start != std::string::npos && end != std::string::npos) {
                    return content.substr(start, end - start);
                }
            }
            return {};
        };
        
        // Parse window properties
        auto width = extractCssValue("width");
        auto height = extractCssValue("height");
        auto title = extractCssValue("title");
        
        if (!width.empty() && width.back() == 'x') width.pop_back();
        if (!height.empty() && height.back() == 'x') height.pop_back();
        if (!title.empty() && title.front() == '"' && title.back() == '"') {
            title = title.substr(1, title.length() - 2);
            event.uiContent.title = title;
        }
        
        if (!width.empty()) {
            event.windowStyle.width = static_cast<std::uint32_t>(std::stoi(width));
            event.windowConfigChanged = true;
        }
        if (!height.empty()) {
            event.windowStyle.height = static_cast<std::uint32_t>(std::stoi(height));
            event.windowConfigChanged = true;
        }
        if (!title.empty()) {
            event.windowStyle.title = title;
            event.windowConfigChanged = true;
        }
        
        event.uiContentChanged = true;
        
    } catch (const std::exception& e) {
        event.errorMessage = "Error loading file: " + std::string(e.what());
    }
    
    return event;
}

bool HtmlCssLoader::startWatching(const std::filesystem::path& filePath, FileChangeCallback callback) {
    if (watching.load()) {
        return false; // Already watching
    }
    
    watchedFile = filePath;
    changeCallback = callback;
    shouldStop.store(false);
    lastWriteTime = getFileTime(filePath);
    
    // Start watch thread
    watchThread = std::thread(&HtmlCssLoader::watchLoop, this);
    watching.store(true);
    
    return true;
}

void HtmlCssLoader::stopWatching() {
    if (watching.load()) {
        shouldStop.store(true);
        if (watchThread.joinable()) {
            watchThread.join();
        }
        watching.store(false);
    }
}

void HtmlCssLoader::watchLoop() {
    while (!shouldStop.load()) {
        try {
            auto currentTime = getFileTime(watchedFile);
            if (currentTime != lastWriteTime && currentTime != std::filesystem::file_time_type::min()) {
                lastWriteTime = currentTime;
                
                // File changed, reload and notify
                auto event = loadFile(watchedFile);
                if (changeCallback) {
                    changeCallback(event);
                }
            }
        } catch (const std::exception&) {
            // Ignore file system errors during watching
        }
        
        // Poll every 500ms
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

std::filesystem::file_time_type HtmlCssLoader::getFileTime(const std::filesystem::path& path) {
    try {
        if (std::filesystem::exists(path)) {
            return std::filesystem::last_write_time(path);
        }
    } catch (const std::exception&) {
        // Return min time on error
    }
    return std::filesystem::file_time_type::min();
}

//=============================================================================
// WindowConfig Implementation
//=============================================================================

WindowConfig WindowConfig::fromHtmlCss(const std::filesystem::path& htmlCssPath) {
    WindowConfig config;
    
    // Minimal CSS parsing for window configuration
    try {
        if (std::filesystem::exists(htmlCssPath)) {
            std::ifstream file(htmlCssPath);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            
            // Simple CSS property extraction
            auto extractValue = [&](const std::string& property) -> std::string {
                auto pos = content.find(property + ":");
                if (pos != std::string::npos) {
                    auto start = content.find_first_not_of(" \t", pos + property.length() + 1);
                    auto end = content.find_first_of(";\n}", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        return content.substr(start, end - start);
                    }
                }
                return {};
            };
            
            // Extract window properties
            auto width = extractValue("width");
            auto height = extractValue("height");
            auto title = extractValue("title");
            
            if (!width.empty() && width.back() == 'x') width.pop_back();
            if (!height.empty() && height.back() == 'x') height.pop_back();
            if (!title.empty() && title.front() == '"' && title.back() == '"') {
                title = title.substr(1, title.length() - 2);
            }
            
            if (!width.empty()) config.width = static_cast<std::uint32_t>(std::stoi(width));
            if (!height.empty()) config.height = static_cast<std::uint32_t>(std::stoi(height));
            if (!title.empty()) config.title = title;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to load window config from " << htmlCssPath.string() 
                  << ": " << e.what() << ". Using defaults." << std::endl;
    }
    
    return config;
}

//=============================================================================
// Window Implementation
//=============================================================================

#ifdef MDUX_GLFW_AVAILABLE

Window::Window(const std::filesystem::path& htmlCssPath) 
    : Window(WindowConfig::fromHtmlCss(htmlCssPath)) {
}

Window::Window(const WindowConfig& windowConfig) : config(windowConfig) {
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

Window::~Window() {
    stopUiIntegration();  // Clean up UI integration first
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

Window::Window(Window&& other) noexcept 
    : window(other.window), config(other.config), surface(other.surface), instance(other.instance),
      uiIntegration(std::move(other.uiIntegration)), uiLoader(std::move(other.uiLoader)),
      currentUiContent(std::move(other.currentUiContent)), lastFrameTime(other.lastFrameTime) {
    other.window = nullptr;
    other.surface = VK_NULL_HANDLE;
    other.instance = VK_NULL_HANDLE;
    // No need to adjust ref count - just transferring ownership
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        // Clean up current resources without adjusting ref count (will be transferred)
        stopUiIntegration();  // Clean up UI integration
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
        other.surface = VK_NULL_HANDLE;
        other.instance = VK_NULL_HANDLE;
        // No ref count adjustment needed - just transferring ownership
    }
    return *this;
}

bool Window::shouldClose() const noexcept { 
    return window ? glfwWindowShouldClose(window) : true; 
}

void Window::pollEvents() noexcept { 
    glfwPollEvents(); 
}

void Window::presentFrame() noexcept {
    // Vulkan presentation will be implemented in future versions
    // Currently a placeholder for API compatibility
}

std::pair<int, int> Window::getSize() const noexcept {
    int width, height;
    if (window) {
        glfwGetWindowSize(window, &width, &height);
        return {width, height};
    }
    return {0, 0};
}

void Window::setTitle(std::string_view title) noexcept {
    if (window) {
        glfwSetWindowTitle(window, title.data());
    }
}

void Window::setSize(int width, int height) noexcept {
    if (window) {
        glfwSetWindowSize(window, width, height);
        // Update internal config to reflect new size
        config.width = static_cast<std::uint32_t>(width);
        config.height = static_cast<std::uint32_t>(height);
    }
}

bool Window::applyConfig(const WindowConfig& newConfig) noexcept {
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

#ifdef MDUX_GLFW_AVAILABLE
GLFWwindow* Window::getNativeHandle() const noexcept { 
    return window; 
}
#else
void* Window::getNativeHandle() const noexcept { 
    return window; 
}
#endif

VkSurfaceKHR Window::getSurface() const noexcept { 
    return surface; 
}

VkInstance Window::getInstance() const noexcept { 
    return instance; 
}

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
            std::cerr << "Window: Failed to start UI hot-reload for " << uiIntegration->htmlCssPath.string() << std::endl;
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
    
    std::cout << "Window: UI integration setup complete for " << uiIntegration->htmlCssPath.filename().string() << std::endl;
    return true;
}

void Window::updateUiContent(const UiContent& uiContent) {
    currentUiContent = uiContent;
    
    // Notify renderer of content update
    if (uiIntegration && uiIntegration->renderer.contentUpdateCallback) {
        uiIntegration->renderer.contentUpdateCallback(currentUiContent);
    }
}

const UiContent& Window::getCurrentUiContent() const noexcept { 
    return currentUiContent; 
}

bool Window::hasUiIntegration() const noexcept { 
    return uiIntegration && uiIntegration->isConfigured(); 
}

UiRenderMode Window::getUiRenderMode() const noexcept {
    return uiIntegration ? uiIntegration->renderMode : UiRenderMode::OVERLAY;
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

void Window::configurePlatformHints() {
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

const char* Window::getEnvironmentVariable(const char* name) const noexcept {
#ifdef _WIN32
    // Windows: Use getenv for compatibility with import std
    // Suppress deprecation warning as we need compatibility with import std
    #pragma warning(push)
    #pragma warning(disable: 4996)
    return std::getenv(name);
    #pragma warning(pop)
#else
    // Unix/Linux: getenv is safe on these platforms
    return std::getenv(name);
#endif
}

bool Window::isWslEnvironment() const noexcept {
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

void Window::initializeVulkan() {
    // Determine the highest available Vulkan API version
    uint32_t apiVersion = VK_API_VERSION_1_3; // Fallback to 1.3
    if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) {
        apiVersion = VK_API_VERSION_1_3; // Use fallback if enumeration fails
    }
    
    // Basic Vulkan initialization using C API
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MduX Medical Device Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "MduX";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = apiVersion;

    std::vector<const char*> extensions;
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    // Create surface
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
}

void Window::cleanupVulkan() {
    if (surface && instance) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
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

#else // !MDUX_GLFW_AVAILABLE

// Stub implementations when GLFW is not available
Window::Window(const std::filesystem::path& htmlCssPath) 
    : config(WindowConfig::fromHtmlCss(htmlCssPath)) {
    // Stub implementation - no actual window creation
}

Window::Window(const WindowConfig& windowConfig) : config(windowConfig) {
    // Stub implementation - no actual window creation
}

Window::~Window() {
    // Stub implementation - no cleanup needed
}

Window::Window(Window&& other) noexcept 
    : config(other.config), surface(other.surface), instance(other.instance) {
    other.surface = VK_NULL_HANDLE;
    other.instance = VK_NULL_HANDLE;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        config = other.config;
        surface = other.surface;
        instance = other.instance;
        other.surface = VK_NULL_HANDLE;
        other.instance = VK_NULL_HANDLE;
    }
    return *this;
}

bool Window::shouldClose() const noexcept { 
    return false; // Stub - never closes
}

void Window::pollEvents() noexcept { 
    // Stub implementation
}

void Window::presentFrame() noexcept {
    // Stub implementation
}

std::pair<int, int> Window::getSize() const noexcept {
    return {static_cast<int>(config.width), static_cast<int>(config.height)};
}

void Window::setTitle(std::string_view title) noexcept {
    config.title = title;
}

void Window::setSize(int width, int height) noexcept {
    config.width = static_cast<std::uint32_t>(width);
    config.height = static_cast<std::uint32_t>(height);
}

bool Window::applyConfig(const WindowConfig& newConfig) noexcept {
    config = newConfig;
    return true;
}

VkSurfaceKHR Window::getSurface() const noexcept { 
    return surface; 
}

VkInstance Window::getInstance() const noexcept { 
    return instance; 
}

bool Window::setupUiIntegration(UiIntegration /*integration*/) {
    // Stub implementation
    return false;
}

void Window::updateUiContent(const UiContent& /*uiContent*/) {
    // Stub implementation
}

const UiContent& Window::getCurrentUiContent() const noexcept { 
    static UiContent empty;
    return empty; 
}

bool Window::hasUiIntegration() const noexcept { 
    return false; 
}

UiRenderMode Window::getUiRenderMode() const noexcept {
    return UiRenderMode::OVERLAY;
}

void Window::renderUi(float /*deltaTime*/) {
    // Stub implementation
}

void Window::stopUiIntegration() {
    // Stub implementation
}

void Window::configurePlatformHints() {
    // Stub implementation
}

const char* Window::getEnvironmentVariable(const char* /*name*/) const noexcept {
    return nullptr; // Stub implementation
}

bool Window::isWslEnvironment() const noexcept {
    return false; // Stub implementation
}

void Window::initializeVulkan() {
    // Stub implementation
}

void Window::cleanupVulkan() {
    // Stub implementation
}

void Window::onUiReload(const ReloadEvent& /*event*/) {
    // Stub implementation
}

#endif // MDUX_GLFW_AVAILABLE

//=============================================================================
// Library Management Functions
//=============================================================================

bool initialize() noexcept {
    // Vulkan initialization is handled per-window for better resource management
    // This function can be used for global library initialization if needed
    return true;
}

void shutdown() noexcept {
    // Global cleanup if needed
    // Individual windows handle their own Vulkan cleanup
}

} // namespace mdux