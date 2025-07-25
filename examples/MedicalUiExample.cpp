/**
 * @brief Medical device UI example demonstrating advanced window features with Vulkan
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <mdux/mdux.hpp>
#include <thread>

int main() {
    try {
        // Initialize MduX library
        if (!mdux::initialize()) {
            std::cerr << "Failed to initialize MduX library" << std::endl;
            return -1;
        }

        // Create window configuration for medical device application
        mdux::WindowConfig config;
        config.width = 1280;
        config.height = 800;
        config.title = "MduX Medical Device UI - Advanced Example";
        config.resizable = false; // Fixed size for medical device stability
        config.vsync = true;      // Prevent screen tearing for medical displays

        // Create window
        mdux::Window window(config);

        std::cout << "=== MduX Medical Device UI Library ===" << std::endl;
        std::cout << "Version: " << mdux::Version::getString() << std::endl;
        std::cout << "Graphics API: " << mdux::Graphics::api << std::endl;
        std::cout << "Surface Type: " << mdux::Graphics::surfaceType << std::endl;
        std::cout << "Vulkan Version: " << mdux::Graphics::vulkanVersionMajor << "." 
                  << mdux::Graphics::vulkanVersionMinor << "." << mdux::Graphics::vulkanVersionPatch << std::endl;
        std::cout << "Medical Device Compliant: "
                  << (mdux::Compliance::isMedicalDeviceCompliant ? "Yes" : "No") << std::endl;
        std::cout << "Safety Standards: " << mdux::Compliance::standards << std::endl;
        std::cout << "Safety Class: " << mdux::Compliance::safetyClass << std::endl;

        auto [width, height] = window.getSize();
        std::cout << "Window Size: " << width << "x" << height << std::endl;
        std::cout << "Medical UI initialized successfully!" << std::endl;

        // Animation variables for demonstration
        float colorPhase = 0.0f;
        auto lastTime = std::chrono::high_resolution_clock::now();

        // Main application loop
        while (!window.shouldClose()) {
            // Poll for window events
            window.pollEvents();

            // Calculate delta time for smooth animation
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Update animation
            colorPhase += deltaTime * 0.5f; // Slow, medical-appropriate animation
            if (colorPhase > 6.28318f)
                colorPhase -= 6.28318f; // 2*PI

            // Present rendered frame (Vulkan presentation)
            // Note: Actual rendering will be implemented with Vulkan in future versions
            // Medical-safe color scheme will be applied during rendering phase
            window.presentFrame();

            // Limit frame rate for medical device stability (60 FPS max)
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // Cleanup
        mdux::shutdown();
        std::cout << "Medical device application closed successfully." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Medical Device Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}