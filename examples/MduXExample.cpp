/**
 * @file MduXExample.cpp
 * @brief Comprehensive MduX example demonstrating all features
 * 
 * This unified example demonstrates:
 * 1. Traditional WindowConfig approach
 * 2. HTML/CSS file loading with static configuration
 * 3. HTML/CSS hot-reload with live updates
 * 4. Medical device compliance information
 * 5. System information display
 * 
 * Usage:
 *   ./MduXExample                    # Uses default configuration
 *   ./MduXExample ui.html            # Loads from HTML file (static)
 *   ./MduXExample ui.html --hot      # Enables hot-reload
 */

#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <string>
#include <memory>

#include <mdux/mdux.hpp>

class MduXDemo {
public:
    MduXDemo() = default;
    
    ~MduXDemo() {
        if (hotReloadLoader) {
            hotReloadLoader->stopWatching();
        }
    }
    
    /**
     * @brief Initialize and run the demonstration
     * @param argc Argument count
     * @param argv Argument vector
     * @return Exit code
     */
    int run(int argc, char* argv[]) {
        try {
            // Parse command line arguments
            parseArguments(argc, argv);
            
            // Initialize MduX library
            if (!mdux::initialize()) {
                std::cerr << "Failed to initialize MduX library" << std::endl;
                return -1;
            }
            
            // Display header
            displayHeader();
            
            // Create window based on mode
            if (!createWindow()) {
                return -1;
            }
            
            // Display system information
            displaySystemInfo();
            
            // Display current configuration info
            displayConfigInfo();
            
            // Run main loop
            runMainLoop();
            
            // Cleanup
            mdux::shutdown();
            std::cout << "\nApplication closed successfully." << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return -1;
        }
        
        return 0;
    }

private:
    enum class Mode {
        DEFAULT,        // Default configuration
        HTML_STATIC,    // Load HTML/CSS once
        HTML_HOT_RELOAD // Hot-reload HTML/CSS changes
    };
    
    Mode mode = Mode::DEFAULT;
    std::filesystem::path htmlPath;
    std::unique_ptr<mdux::Window> window;
    std::unique_ptr<mdux::HtmlCssLoader> hotReloadLoader;
    mdux::WindowConfig currentConfig;
    bool enableAnimation = true;
    
    /**
     * @brief Parse command line arguments
     */
    void parseArguments(int argc, char* argv[]) {
        if (argc >= 2) {
            htmlPath = argv[1];
            
            // Check if hot-reload is requested
            if (argc >= 3 && std::string(argv[2]) == "--hot") {
                mode = Mode::HTML_HOT_RELOAD;
            } else {
                mode = Mode::HTML_STATIC;
            }
        } else {
            mode = Mode::DEFAULT;
        }
    }
    
    /**
     * @brief Display application header
     */
    void displayHeader() {
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                    MduX Comprehensive Demo                   ║\n";
        std::cout << "║          Medical Device UI Library (C++23 + Vulkan)         ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
        
        switch (mode) {
            case Mode::DEFAULT:
                std::cout << "\n🎛️  Mode: Default Configuration (Traditional WindowConfig)\n";
                break;
            case Mode::HTML_STATIC:
                std::cout << "\n📄 Mode: HTML/CSS Static Loading\n";
                std::cout << "📁 HTML File: " << htmlPath << "\n";
                break;
            case Mode::HTML_HOT_RELOAD:
                std::cout << "\n🔥 Mode: HTML/CSS Hot-Reload\n";
                std::cout << "📁 HTML File: " << htmlPath << " (watching for changes)\n";
                break;
        }
    }
    
    /**
     * @brief Create window based on selected mode
     */
    bool createWindow() {
        try {
            switch (mode) {
                case Mode::DEFAULT:
                    return createDefaultWindow();
                case Mode::HTML_STATIC:
                    return createHtmlStaticWindow();
                case Mode::HTML_HOT_RELOAD:
                    return createHtmlHotReloadWindow();
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to create window: " << e.what() << std::endl;
            return false;
        }
        return false;
    }
    
    /**
     * @brief Create window with default configuration
     */
    bool createDefaultWindow() {
        std::cout << "\n🔧 Creating window with default configuration...\n";
        
        currentConfig.width = 1024;
        currentConfig.height = 768;
        currentConfig.title = "MduX Medical Device UI - Default Config";
        currentConfig.resizable = true;
        currentConfig.vsync = true;
        currentConfig.fullscreen = false;
        
        window = std::make_unique<mdux::Window>(currentConfig);
        std::cout << "✅ Window created successfully\n";
        return true;
    }
    
    /**
     * @brief Create window from HTML/CSS file (static)
     */
    bool createHtmlStaticWindow() {
        if (!std::filesystem::exists(htmlPath)) {
            std::cerr << "❌ HTML file not found: " << htmlPath << std::endl;
            std::cout << "🔄 Falling back to default configuration...\n";
            return createDefaultWindow();
        }
        
        std::cout << "\n📄 Loading window configuration from HTML/CSS file...\n";
        
        // Method 1: Direct constructor with HTML path
        window = std::make_unique<mdux::Window>(htmlPath);
        
        // Get the actual config that was applied
        auto [width, height] = window->getSize();
        std::cout << "✅ Window created from HTML/CSS file\n";
        std::cout << "📐 Parsed dimensions: " << width << "x" << height << "\n";
        
        return true;
    }
    
    /**
     * @brief Create window with HTML/CSS hot-reload
     */
    bool createHtmlHotReloadWindow() {
        if (!std::filesystem::exists(htmlPath)) {
            std::cerr << "❌ HTML file not found: " << htmlPath << std::endl;
            std::cout << "🔄 Falling back to default configuration...\n";
            return createDefaultWindow();
        }
        
        std::cout << "\n🔥 Setting up HTML/CSS hot-reload...\n";
        
        // Create initial window from HTML
        window = std::make_unique<mdux::Window>(htmlPath);
        
        // Set up hot-reload
        hotReloadLoader = std::make_unique<mdux::HtmlCssLoader>();
        
        auto reloadCallback = [this](const mdux::ReloadEvent& event) {
            this->onHtmlCssReload(event);
        };
        
        if (hotReloadLoader->startWatching(htmlPath, reloadCallback)) {
            std::cout << "✅ Hot-reload enabled for: " << htmlPath.filename() << "\n";
            std::cout << "💡 Modify the HTML file to see live updates!\n";
            return true;
        } else {
            std::cerr << "❌ Failed to start hot-reload\n";
            hotReloadLoader.reset();
            return true; // Continue without hot-reload
        }
    }
    
    /**
     * @brief Handle HTML/CSS reload events
     */
    void onHtmlCssReload(const mdux::ReloadEvent& event) {
        std::cout << "\n🔄 File change detected: " << event.filePath.filename() << "\n";
        
        if (!event.isSuccess()) {
            std::cerr << "❌ Reload failed: " << event.errorMessage << std::endl;
            return;
        }
        
        std::cout << "✅ Configuration reloaded successfully\n";
        
        // NEW: Check what type of change occurred
        if (event.isUiOnlyChange()) {
            std::cout << "🎨 UI content changed - updating without window recreation\n";
            updateUiContent(event.uiContent);
        } else if (event.windowConfigChanged) {
            std::cout << "🪟 Window configuration changed\n";
            auto newConfig = windowStyleToConfig(event.windowStyle);
            
            if (needsWindowRecreation(currentConfig, newConfig)) {
                std::cout << "🔄 Recreating window with new configuration...\n";
                recreateWindow(newConfig);
            } else {
                updateWindowProperties(newConfig);
            }
            
            currentConfig = newConfig;
            
            // Display updated configuration
            auto [width, height] = window->getSize();
            std::cout << "📐 New dimensions: " << width << "x" << height << "\n";
            if (event.windowStyle.title) {
                std::cout << "🏷️  New title: " << *event.windowStyle.title << "\n";
            }
        }
        
        // Update UI content if it changed
        if (event.uiContentChanged) {
            updateUiContent(event.uiContent);
        }
    }
    
    /**
     * @brief Convert WindowStyle to WindowConfig
     */
    mdux::WindowConfig windowStyleToConfig(const mdux::WindowStyle& style) {
        mdux::WindowConfig config = currentConfig;
        
        if (style.width) config.width = *style.width;
        if (style.height) config.height = *style.height;
        if (style.title) config.title = *style.title;
        if (style.resizable) config.resizable = *style.resizable;
        if (style.vsync) config.vsync = *style.vsync;
        if (style.fullscreen) config.fullscreen = *style.fullscreen;
        
        return config;
    }
    
    /**
     * @brief Check if window recreation is needed
     */
    bool needsWindowRecreation(const mdux::WindowConfig& oldConfig, 
                              const mdux::WindowConfig& newConfig) {
        return oldConfig.fullscreen != newConfig.fullscreen ||
               oldConfig.resizable != newConfig.resizable ||
               oldConfig.vsync != newConfig.vsync ||
               oldConfig.width != newConfig.width ||
               oldConfig.height != newConfig.height;
    }
    
    /**
     * @brief Recreate window with new configuration
     */
    void recreateWindow(const mdux::WindowConfig& newConfig) {
        std::cout << "🔄 Non-disruptive window update approach..." << std::endl;
        
        try {
            // NEW APPROACH: Try to apply config without recreation first
            std::cout << "🔄 Step 1: Attempting dynamic window resize..." << std::endl;
            
            if (window && window->applyConfig(newConfig)) {
                std::cout << "✅ Window resized dynamically without recreation!" << std::endl;
                currentConfig = newConfig;
                return;
            }
            
            // If dynamic update failed, fall back to recreation
            std::cout << "🔄 Dynamic resize not possible, creating new window..." << std::endl;
            
            std::cout << "🔄 Step 2: Creating new window..." << std::endl;
            auto newWindow = std::make_unique<mdux::Window>(newConfig);
            std::cout << "🔄 Step 3: New window created successfully" << std::endl;
            
            std::cout << "🔄 Step 4: Replacing old window..." << std::endl;
            window = std::move(newWindow);
            std::cout << "✅ Window replacement completed!" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Exception during window update: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "❌ Unknown exception during window update" << std::endl;
            throw;
        }
    }
    
    /**
     * @brief Update window properties without recreation
     */
    void updateWindowProperties(const mdux::WindowConfig& newConfig) {
        if (currentConfig.title != newConfig.title) {
            window->setTitle(newConfig.title);
        }
    }
    
    /**
     * @brief Update UI content without window recreation (NEW)
     */
    void updateUiContent(const mdux::UiContent& uiContent) {
        if (uiContent.isValid()) {
            std::cout << "🎨 Updating UI content: " << uiContent.title << "\n";
            std::cout << "📄 HTML content: " << uiContent.htmlContent.length() << " characters\n";
            std::cout << "🎯 CSS content: " << uiContent.cssContent.length() << " characters\n";
            
            // Future: This is where we would update the actual UI rendering
            // For now, just log the content update
            std::cout << "✅ UI content updated (rendering integration coming next)\n";
        } else {
            std::cerr << "❌ Invalid UI content, cannot update\n";
            for (const auto& error : uiContent.errors) {
                std::cerr << "   Error: " << error << "\n";
            }
        }
    }
    
    /**
     * @brief Display system information
     */
    void displaySystemInfo() {
        std::cout << "\n╔═══════════════════ System Information ════════════════════╗\n";
        std::cout << "║ Library Version: " << mdux::Version::getString() << "                                      ║\n";
        std::cout << "║ Graphics API: " << mdux::Graphics::api << "                                         ║\n";
        std::cout << "║ Vulkan Version: " << mdux::Graphics::vulkanVersionMajor << "." 
                  << mdux::Graphics::vulkanVersionMinor << "." 
                  << mdux::Graphics::vulkanVersionPatch << "                                          ║\n";
        std::cout << "║ Surface Type: " << mdux::Graphics::surfaceType << "                        ║\n";
        std::cout << "║ Medical Device Compliant: " 
                  << (mdux::Compliance::isMedicalDeviceCompliant ? "Yes" : "No") << "                              ║\n";
        std::cout << "║ Safety Standards: " << mdux::Compliance::standards << "                    ║\n";
        std::cout << "║ Safety Class: " << mdux::Compliance::safetyClass << " ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    }
    
    /**
     * @brief Display current window configuration
     */
    void displayConfigInfo() {
        auto [width, height] = window->getSize();
        
        std::cout << "\n╔═══════════════════ Window Configuration ══════════════════╗\n";
        std::cout << "║ Dimensions: " << width << "x" << height << " pixels";
        
        // Pad the line to 60 characters
        auto padding = static_cast<int>(60 - 13 - std::to_string(width).length() - 1 - std::to_string(height).length() - 7);
        for (int i = 0; i < padding; i++) std::cout << " ";
        std::cout << "║\n";
        
        if (mode == Mode::HTML_HOT_RELOAD) {
            std::cout << "║ Hot-reload: ENABLED (" << htmlPath.filename() << ")";
            auto pathPadding = static_cast<int>(60 - 17 - htmlPath.filename().string().length() - 1);
            for (int i = 0; i < pathPadding; i++) std::cout << " ";
            std::cout << "║\n";
        } else if (mode == Mode::HTML_STATIC) {
            std::cout << "║ Loaded from: " << htmlPath.filename();
            auto pathPadding = static_cast<int>(60 - 14 - htmlPath.filename().string().length());
            for (int i = 0; i < pathPadding; i++) std::cout << " ";
            std::cout << "║\n";
        } else {
            std::cout << "║ Configuration: Default (programmatic)                     ║\n";
        }
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    }
    
    /**
     * @brief Run the main application loop
     */
    void runMainLoop() {
        std::cout << "\n📋 Instructions:\n";
        std::cout << "  • Close window or press ESC to exit\n";
        std::cout << "  • Window shows medical device UI rendering\n";
        
        if (mode == Mode::HTML_HOT_RELOAD) {
            std::cout << "  • Modify " << htmlPath.filename() << " to see live updates\n";
            std::cout << "  • Watch the console for reload notifications\n";
        }
        
        if (enableAnimation) {
            std::cout << "  • Smooth animation for medical device stability\n";
        }
        
        std::cout << "\n🚀 Application running...\n\n";
        
        // Animation variables for medical-appropriate demonstration
        float animationPhase = 0.0f;
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        
        // Main application loop
        while (!window->shouldClose()) {
            // Poll for window events
            window->pollEvents();
            
            // Calculate delta time for smooth animation
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            // Update animation (medical-safe, slow animation)
            if (enableAnimation) {
                animationPhase += deltaTime * 0.5f; // Slow animation for medical devices
                if (animationPhase > 6.28318f) {
                    animationPhase -= 6.28318f; // 2*PI
                }
            }
            
            // Present rendered frame (Vulkan presentation)
            // Note: Actual rendering will be implemented in future versions
            window->presentFrame();
            
            // Medical device stability: Limit to 60 FPS maximum
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            
            // Progress indicator every 5 seconds
            frameCount++;
            if (frameCount % 300 == 0) { // ~5 seconds at 60fps
                std::cout << "⏱️  Runtime: " << (frameCount / 60) << " seconds";
                if (mode == Mode::HTML_HOT_RELOAD) {
                    std::cout << " (watching " << htmlPath.filename() << ")";
                }
                std::cout << "\n";
            }
        }
    }
};

int main(int argc, char* argv[]) {
    MduXDemo demo;
    return demo.run(argc, argv);
}