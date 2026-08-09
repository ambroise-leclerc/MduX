/**
 * @file HeadlessDevice.hpp
 * @brief A Vulkan instance, device and queue created with no surface and no window.
 *
 * Test scaffolding, not library code: MduX never creates a device, because the application owns
 * one and knows what else is running on it. A test does have to create one, and this is the
 * smallest thing that does.
 *
 * ## Absence of a device is a skip, not a failure
 *
 * `available()` is false when there is no loader, no ICD, or no physical device. A contributor
 * without Vulkan installed should not see a red test they cannot act on, so the suite's `main`
 * returns 77 in that case and CTest reports it as *Skipped* - which is a distinct outcome from
 * passing, and therefore cannot be mistaken for coverage that did not happen.
 *
 * CI does have a device: the Linux legs install `mesa-vulkan-drivers` and select lavapipe. So on
 * the machines whose result gates a merge, these tests run.
 */
#pragma once

namespace mdux::test {

/// The CTest convention for "this test did not run, and that is expected".
inline constexpr int skipExitCode = 77;

/// Owns a headless Vulkan instance, device and queue. Move-disabled: a test creates one.
class HeadlessDevice {
public:
    HeadlessDevice() noexcept { initialise(); }

    ~HeadlessDevice() {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
        }
        if (messenger_ != VK_NULL_HANDLE) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy != nullptr) {
                destroy(instance_, messenger_, nullptr);
            }
            messenger_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    HeadlessDevice(const HeadlessDevice&) = delete;
    HeadlessDevice& operator=(const HeadlessDevice&) = delete;

    [[nodiscard]] bool available() const noexcept { return device_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept { return queueFamily_; }

    /// What went wrong, for a diagnostic when `available()` is false.
    [[nodiscard]] std::string_view reason() const noexcept { return reason_; }

    /// True when the validation layers are loaded, so a test can say "checked" rather than
    /// "assumed" - and can skip an assertion that would otherwise vacuously pass without them.
    [[nodiscard]] bool validationEnabled() const noexcept { return messenger_ != VK_NULL_HANDLE; }

    /// Every error- or warning-severity message the layers have produced since the device was
    /// created. Empty is the expected state; a test asserts on it directly.
    [[nodiscard]] static const std::vector<std::string>& validationMessages() noexcept {
        return messages();
    }

private:
    /// Collected by the debug messenger. A free function-local static rather than a member
    /// because the callback is a plain C function pointer with only a void* to work with, and a
    /// single device per test binary makes shared state the simpler correct choice.
    [[nodiscard]] static std::vector<std::string>& messages() noexcept {
        static std::vector<std::string> stored;
        return stored;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*) {
        // Errors only. The messenger subscribes to warnings as well so a developer watching the
        // output sees them, but a warning is an opinion - performance advice, a best-practice
        // note - and failing a pixel test on one would make this harness a source of unrelated
        // red. An error is a spec violation.
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 &&
            data != nullptr && data->pMessage != nullptr) {
            messages().emplace_back(data->pMessage);
        }
        // VK_FALSE: never abort the call that produced the message. The test decides what to do.
        return VK_FALSE;
    }

    [[nodiscard]] static bool hasLayer(std::string_view wanted) noexcept {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
            return false;
        }
        std::vector<VkLayerProperties> properties(count);
        if (vkEnumerateInstanceLayerProperties(&count, properties.data()) != VK_SUCCESS) {
            return false;
        }
        return std::any_of(properties.begin(), properties.end(),
                           [wanted](const VkLayerProperties& layer) {
                               return wanted == layer.layerName;
                           });
    }

    [[nodiscard]] static bool hasExtension(std::string_view wanted) noexcept {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS ||
            count == 0) {
            return false;
        }
        std::vector<VkExtensionProperties> properties(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data()) !=
            VK_SUCCESS) {
            return false;
        }
        return std::any_of(properties.begin(), properties.end(),
                           [wanted](const VkExtensionProperties& extension) {
                               return wanted == extension.extensionName;
                           });
    }

    /// Attaches the messenger once the instance exists. Failure here is not fatal: the tests still
    /// run, they just cannot claim the layers approved of them.
    void attachMessenger() noexcept {
        if (!validationRequested_) {
            return;
        }
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create == nullptr) {
            return;
        }
        const VkDebugUtilsMessengerCreateInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = &onValidationMessage,
            .pUserData = nullptr};
        if (create(instance_, &info, nullptr, &messenger_) != VK_SUCCESS) {
            messenger_ = VK_NULL_HANDLE;
        }
    }

    void initialise() noexcept {
        const VkApplicationInfo application{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                            .pNext = nullptr,
                                            .pApplicationName = "MduX offscreen tests",
                                            .applicationVersion = 1,
                                            .pEngineName = "MduX",
                                            .engineVersion = 1,
                                            .apiVersion = VK_API_VERSION_1_3};

        std::vector<const char*> layers;
        std::vector<const char*> instanceExtensions;
        VkInstanceCreateFlags flags = 0;

        // MoltenVK is a portability driver and is invisible without this flag and extension, so a
        // developer on macOS would otherwise see "no device" on a machine that has one.
        //
        // Guarded because the macro is not in every Vulkan SDK: the Windows CI leg's headers
        // predate it, and an unguarded reference does not compile there. Where it is absent the
        // platform has no portability driver either, so nothing is lost.
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        // The validation layers, when the machine has them. Without these a pixel test proves the
        // pixels are right and says nothing about whether the commands that produced them were
        // legal - image layouts, synchronisation and descriptor state can all be wrong in ways
        // that happen to render correctly on one driver. Enabled opportunistically rather than
        // required, because a contributor without the SDK still gets the pixel coverage.
        if (hasLayer("VK_LAYER_KHRONOS_validation") && hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validationRequested_ = true;
        }

        VkInstanceCreateInfo instanceInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = flags,
            .pApplicationInfo = &application,
            .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
            .enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size()),
            .ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data()};

        if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
            instanceInfo.flags = 0;
            instanceInfo.enabledExtensionCount = 0;
            instanceInfo.ppEnabledExtensionNames = nullptr;
            instanceInfo.enabledLayerCount = 0;
            instanceInfo.ppEnabledLayerNames = nullptr;
            validationRequested_ = false;
            if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
                reason_ = "vkCreateInstance failed: no Vulkan loader or no ICD";
                instance_ = VK_NULL_HANDLE;
                return;
            }
        }

        attachMessenger();

        std::uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            reason_ = "no physical device: a loader is present but no ICD reported a device";
            return;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
        physicalDevice_ = devices.front();

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());

        // Graphics only. No presentation support is asked for, which is the whole point: nothing
        // here needs a surface, so nothing here needs a display server.
        bool found = false;
        for (std::uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                queueFamily_ = i;
                found = true;
                break;
            }
        }
        if (!found) {
            reason_ = "no graphics-capable queue family";
            return;
        }

        const float priority = 1.0F;
        const VkDeviceQueueCreateInfo queueInfo{.sType =
                                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                .pNext = nullptr,
                                                .flags = 0,
                                                .queueFamilyIndex = queueFamily_,
                                                .queueCount = 1,
                                                .pQueuePriorities = &priority};

        // A portability driver requires its extension to be enabled on the device too.
        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount,
                                             extensions.data());
        std::vector<const char*> enabled;
        for (const VkExtensionProperties& extension : extensions) {
            if (std::string_view{extension.extensionName} == "VK_KHR_portability_subset") {
                enabled.push_back("VK_KHR_portability_subset");
            }
        }

        const VkDeviceCreateInfo deviceInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<std::uint32_t>(enabled.size()),
            .ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data(),
            .pEnabledFeatures = nullptr};
        if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
            reason_ = "vkCreateDevice failed";
            device_ = VK_NULL_HANDLE;
            return;
        }
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    }

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamily_{0};
    std::string_view reason_{};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    bool validationRequested_{false};
};

/// The one device every test in the suite shares. Creating a device per test would multiply the
/// slowest part of the suite by its case count for no additional coverage.
inline HeadlessDevice& sharedDevice() {
    static HeadlessDevice device;
    return device;
}

}  // namespace mdux::test
