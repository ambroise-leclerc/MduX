/**
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

private:
    void initialise() noexcept {
        const VkApplicationInfo application{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                            .pNext = nullptr,
                                            .pApplicationName = "MduX offscreen tests",
                                            .applicationVersion = 1,
                                            .pEngineName = "MduX",
                                            .engineVersion = 1,
                                            .apiVersion = VK_API_VERSION_1_3};

        // MoltenVK is a portability driver and is invisible without this flag and extension, so a
        // developer on macOS would otherwise see "no device" on a machine that has one.
        //
        // Guarded because the macro is not in every Vulkan SDK: the Windows CI leg's headers
        // predate it, and an unguarded reference does not compile there. Where it is absent the
        // platform has no portability driver either, so nothing is lost.
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        const std::array<const char*, 1> portability{
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
        VkInstanceCreateInfo instanceInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
            .pApplicationInfo = &application,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<std::uint32_t>(portability.size()),
            .ppEnabledExtensionNames = portability.data()};
#else
        VkInstanceCreateInfo instanceInfo{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                          .pNext = nullptr,
                                          .flags = 0,
                                          .pApplicationInfo = &application,
                                          .enabledLayerCount = 0,
                                          .ppEnabledLayerNames = nullptr,
                                          .enabledExtensionCount = 0,
                                          .ppEnabledExtensionNames = nullptr};
#endif

        if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
            instanceInfo.flags = 0;
            instanceInfo.enabledExtensionCount = 0;
            instanceInfo.ppEnabledExtensionNames = nullptr;
            if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
                reason_ = "vkCreateInstance failed: no Vulkan loader or no ICD";
                instance_ = VK_NULL_HANDLE;
                return;
            }
        }

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
};

/// The one device every test in the suite shares. Creating a device per test would multiply the
/// slowest part of the suite by its case count for no additional coverage.
inline HeadlessDevice& sharedDevice() {
    static HeadlessDevice device;
    return device;
}

}  // namespace mdux::test
