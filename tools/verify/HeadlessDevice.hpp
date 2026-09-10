/**
 * @file HeadlessDevice.hpp
 * @brief A Vulkan 1.3 device with no surface and no swapchain, for an offscreen verification render.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 *
 * Shared, as a plain header rather than a module, by `mdux.tools.verify.driver` (the screen driver,
 * #253) and `mdux.tools.verify.scenario.driver` (the scenario-capture driver, #321). It stays a
 * header because a module interface exporting it would have to expose `<vulkan/vulkan.h>` types, and
 * neither driver's module interface names a Vulkan type today - the device is a private
 * implementation detail of `run()`. Both including translation units put `#include
 * <vulkan/vulkan.h>` in their global module fragment before this header.
 *
 * `available()` is false with no loader, no ICD or no graphics queue; the driver maps that to
 * `RunState::NoRenderDevice` and exit 3 - an impossible run, never a skip. `backendName()` is the
 * verbatim `VkPhysicalDeviceProperties.deviceName`, host-dependent and never committed (ADR-007
 * decision 5), used only for a diagnostic line and the derived evidence envelope's `backend` token.
 *
 * ## Where to include it
 *
 * The class must land in the **global module** in every translation unit that uses it, so its one
 * inline definition ODR-merges rather than clashing. `Driver.cpp` (a module) includes it in the
 * global module fragment, after `<vulkan/vulkan.h>` and the `<vector>` / `<string>` / `<algorithm>`
 * headers it needs; `ScenarioRun.cpp` (not a module) includes it in ordinary code, after
 * `import std;` and `#include <vulkan/vulkan.h>`. This header itself names no standard-library
 * header - the includer supplies `std::vector`, `std::string`, `std::ranges`, `std::string_view`
 * either way, the same arrangement the `mdux_embed_blob` headers use.
 */
#pragma once

namespace mdux::tools::verify {

class HeadlessDevice {
public:
    HeadlessDevice() noexcept {
        initialise();
    }
    ~HeadlessDevice() {
        if (device_ != VK_NULL_HANDLE)
            vkDestroyDevice(device_, nullptr);
        if (instance_ != VK_NULL_HANDLE)
            vkDestroyInstance(instance_, nullptr);
    }
    HeadlessDevice(const HeadlessDevice&)            = delete;
    HeadlessDevice& operator=(const HeadlessDevice&) = delete;

    [[nodiscard]] bool available() const noexcept {
        return device_ != VK_NULL_HANDLE;
    }
    [[nodiscard]] std::string_view reason() const noexcept {
        return reason_;
    }
    [[nodiscard]] VkDevice device() const noexcept {
        return device_;
    }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept {
        return physicalDevice_;
    }
    /// `VkPhysicalDeviceProperties.deviceName`, verbatim - the exact producer-scoped backend
    /// identifier a derived evidence envelope records. Empty before `initialise()` runs.
    [[nodiscard]] std::string backendName() const noexcept {
        if (physicalDevice_ == VK_NULL_HANDLE) {
            return {};
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        return std::string{static_cast<const char*>(properties.deviceName)};
    }
    [[nodiscard]] VkQueue queue() const noexcept {
        return queue_;
    }
    [[nodiscard]] std::uint32_t family() const noexcept {
        return family_;
    }

private:
    [[nodiscard]] static bool hasInstanceExtension(std::string_view wanted) noexcept {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> values(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, values.data()) != VK_SUCCESS)
            return false;
        return std::ranges::any_of(values, [wanted](const auto& value) {
            return wanted == value.extensionName;
        });
    }

    void initialise() noexcept {
        const VkApplicationInfo  app{.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .pNext              = nullptr,
                                     .pApplicationName   = "mdux-verify",
                                     .applicationVersion = 1,
                                     .pEngineName        = "MduX",
                                     .engineVersion      = 1,
                                     .apiVersion         = VK_API_VERSION_1_3};
        std::vector<const char*> instanceExtensions;
        VkInstanceCreateFlags    flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
#endif
        const VkInstanceCreateInfo info{.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                        .pNext                   = nullptr,
                                        .flags                   = flags,
                                        .pApplicationInfo        = &app,
                                        .enabledLayerCount       = 0,
                                        .ppEnabledLayerNames     = nullptr,
                                        .enabledExtensionCount   = static_cast<std::uint32_t>(instanceExtensions.size()),
                                        .ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data()};
        if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
            reason_ = "vkCreateInstance failed: no Vulkan loader or usable ICD";
            return;
        }
        std::uint32_t deviceCount = 0;
        if (vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
            reason_ = "no Vulkan physical device";
            return;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()) != VK_SUCCESS) {
            reason_ = "physical-device enumeration failed";
            return;
        }
        physicalDevice_           = devices.front();
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());
        auto family = std::ranges::find_if(families, [](const auto& value) {
            return (value.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        });
        if (family == families.end()) {
            reason_ = "no graphics-capable Vulkan queue family";
            return;
        }
        family_                                     = static_cast<std::uint32_t>(std::distance(families.begin(), family));
        const float                        priority = 1.0F;
        const VkDeviceQueueCreateInfo      queueInfo{.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                     .pNext            = nullptr,
                                                     .flags            = 0,
                                                     .queueFamilyIndex = family_,
                                                     .queueCount       = 1,
                                                     .pQueuePriorities = &priority};
        std::vector<VkExtensionProperties> extensions;
        while (true) {
            std::uint32_t extensionCount = 0;
            if (vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
                reason_ = "device-extension count enumeration failed";
                return;
            }
            if (extensionCount == 0) {
                extensions.clear();
                break;
            }
            extensions.resize(extensionCount);
            const VkResult enumerated = vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, extensions.data());
            if (enumerated == VK_SUCCESS) {
                extensions.resize(extensionCount);
                break;
            }
            if (enumerated != VK_INCOMPLETE) {
                reason_ = "device-extension enumeration failed";
                return;
            }
        }
        std::vector<const char*> enabled;
        if (std::ranges::any_of(extensions, [](const auto& value) {
                return std::string_view{value.extensionName} == "VK_KHR_portability_subset";
            })) {
            enabled.push_back("VK_KHR_portability_subset");
        }
        const VkDeviceCreateInfo deviceInfo{.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            .pNext                   = nullptr,
                                            .flags                   = 0,
                                            .queueCreateInfoCount    = 1,
                                            .pQueueCreateInfos       = &queueInfo,
                                            .enabledLayerCount       = 0,
                                            .ppEnabledLayerNames     = nullptr,
                                            .enabledExtensionCount   = static_cast<std::uint32_t>(enabled.size()),
                                            .ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data(),
                                            .pEnabledFeatures        = nullptr};
        if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
            reason_ = "vkCreateDevice failed";
            return;
        }
        vkGetDeviceQueue(device_, family_, 0, &queue_);
    }

    VkInstance       instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice         device_{VK_NULL_HANDLE};
    VkQueue          queue_{VK_NULL_HANDLE};
    std::uint32_t    family_{0};
    std::string_view reason_{};
};

}  // namespace mdux::tools::verify
