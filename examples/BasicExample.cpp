/**
 * @file BasicExample.cpp
 * @brief Basic example demonstrating MduX window creation with traditional config
 */

#include <iostream>
#include <mdux/mdux.hpp>

int main() {
    try {
        // Initialize MduX library
        if (!mdux::initialize()) {
            std::cerr << "Failed to initialize MduX library" << std::endl;
            return -1;
        }

        std::cout << "=== MduX Basic Example - Traditional Configuration ===" << std::endl;

        // Create window configuration for medical device application
        mdux::WindowConfig config;
        config.width = 1024;
        config.height = 768;
        config.title = "MduX Medical Device UI - Basic Example";
        config.resizable = true;
        config.vsync = true;

        // Create window with traditional configuration approach
        mdux::Window window(config);

        std::cout << "MduX Medical Device UI Library v" << mdux::Version::getString() << std::endl;
        std::cout << "Graphics API: " << mdux::Graphics::api << std::endl;
        std::cout << "Surface Type: " << mdux::Graphics::surfaceType << std::endl;
        std::cout << "Vulkan Version: " << mdux::Graphics::vulkanVersionMajor << "." 
                  << mdux::Graphics::vulkanVersionMinor << "." << mdux::Graphics::vulkanVersionPatch << std::endl;
        std::cout << "Medical Device Compliant: "
                  << (mdux::Compliance::isMedicalDeviceCompliant ? "Yes" : "No") << std::endl;
        std::cout << "Window created successfully!" << std::endl;

        // Main application loop
        while (!window.shouldClose()) {
            // Poll for window events
            window.pollEvents();

            // Present rendered frame (Vulkan presentation)
            // Note: Actual Vulkan rendering will be implemented in future versions
            window.presentFrame();
        }

        // Cleanup
        mdux::shutdown();
        std::cout << "Application closed successfully." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}