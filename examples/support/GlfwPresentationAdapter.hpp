/**
 * @file GlfwPresentationAdapter.hpp
 * @brief The examples-zone windowed presentation and input adapter (#317, ADR-019).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone: GLFW and the swapchain live here, never in
 *             MduXCore or MduX — mdux_verify_trust_zones() enforces the governed side)
 * @compliance ADR-019 The windowed presentation and input adapter
 *
 * What this is: the reusable shell a windowed MduX example needs and the monitor (#318) will reuse —
 * a GLFW window, a host-owned Vulkan instance / device / swapchain, and the translation of native
 * pointer and keyboard events into the bounded `mdux.medui.input` vocabulary. `mdux::render::UiRenderer`
 * consumes the `VulkanRenderContext` this exposes without any change to its borrowed-context contract.
 *
 * What this is not: it opens no `.medui` screen, resolves no press and executes no action — that is
 * the example's job. It also does not assemble the input → update → render loop (ADR-018 clause 6,
 * #318). It hands the example a drained batch of events and a current `SurfaceMapping`, and the
 * example does the rest.
 *
 * ## Include order
 *
 * Include this header **after** `import std;`, `import mdux;`, `import mdux.core.units;`,
 * `import mdux.medui.input;` and `import mdux.render.vulkan;` — it names those without importing them,
 * the way `tests/framework/SpecLabBridge.hpp` does, so the module graph of the including translation
 * unit stays its own.
 *
 * ## Lifecycle outcomes (ADR-019 clause 4)
 *
 * Every failure is explicit. `create()` returns `std::nullopt` with a message on `std::cerr` for a
 * missing GLFW, window, instance, device, surface or swapchain — never a reduced-function object.
 * `beginFrame()` reports `PresentOutcome::OutOfDate` for a resize, `DeviceLost` for a lost device
 * (fatal), `Skipped` when no image was ready within the timeout. `recreateSwapchain()` idles the
 * device, tears the swapchain-derived objects down in reverse order and rebuilds. The destructor
 * idles the device and destroys everything in strict reverse construction order.
 */
#pragma once

#define GLFW_INCLUDE_VULKAN
#include "GlfwEventTranslation.hpp"

namespace mdux::examples {

// ===========================================================================
// A host-owned Vulkan instance + physical device + logical device
// ===========================================================================

struct VulkanBootError {
    std::string what;
};

/// Creates a `VkSurfaceKHR` on `instance` into `*out`, returning `VK_SUCCESS` or an error. `GlfwWindow`
/// passes a closure over `glfwCreateWindowSurface`; a headless boot passes nothing.
using SurfaceFactory = std::function<VkResult(VkInstance instance, VkSurfaceKHR* out)>;

/**
 * @brief A caller-owned Vulkan instance, physical device and logical device with a graphics queue.
 *
 * With a `SurfaceFactory` (a `GlfwWindow` passes one) it creates the surface between the instance
 * and the device — the only order that works, because the surface needs the instance and the
 * present-queue choice needs the surface — then also picks a present queue and requires
 * `VK_KHR_swapchain`. Without one it is a headless boot for the offscreen frame check. Every handle
 * is destroyed in the destructor, in reverse order, and nowhere else.
 */
class VulkanBoot {
public:
    VulkanBoot() = default;

    VulkanBoot(const VulkanBoot&)            = delete;
    VulkanBoot& operator=(const VulkanBoot&) = delete;

    VulkanBoot(VulkanBoot&& other) noexcept { *this = std::move(other); }
    VulkanBoot& operator=(VulkanBoot&& other) noexcept {
        if (this != &other) {
            destroy();
            instance_       = std::exchange(other.instance_, VK_NULL_HANDLE);
            surface_        = std::exchange(other.surface_, VK_NULL_HANDLE);
            physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
            device_         = std::exchange(other.device_, VK_NULL_HANDLE);
            graphicsQueue_  = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
            presentQueue_   = std::exchange(other.presentQueue_, VK_NULL_HANDLE);
            graphicsFamily_ = std::exchange(other.graphicsFamily_, UINT32_MAX);
            presentFamily_  = std::exchange(other.presentFamily_, UINT32_MAX);
        }
        return *this;
    }

    ~VulkanBoot() { destroy(); }

    /// A headless instance + device: no surface, graphics queue only. For `--headless-frame`.
    [[nodiscard]] static std::expected<VulkanBoot, VulkanBootError> headless() {
        return build({}, {});
    }

    /// A windowed instance + surface (via `makeSurface`) + device with a present queue.
    [[nodiscard]] static std::expected<VulkanBoot, VulkanBootError> forSurface(
        std::span<const char* const> instanceExtensions, const SurfaceFactory& makeSurface) {
        return build(instanceExtensions, makeSurface);
    }

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] std::uint32_t graphicsFamily() const noexcept { return graphicsFamily_; }
    [[nodiscard]] std::uint32_t presentFamily() const noexcept { return presentFamily_; }

private:
    void destroy() noexcept {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] static std::unexpected<VulkanBootError> fail(std::string what) {
        return std::unexpected(VulkanBootError{.what = std::move(what)});
    }

    [[nodiscard]] static std::expected<VulkanBoot, VulkanBootError> build(
        std::span<const char* const> requestedInstanceExtensions, const SurfaceFactory& makeSurface) {
        VulkanBoot   boot;
        const bool   windowed = static_cast<bool>(makeSurface);

        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "MduX Medical Screen Monitor";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "MduX";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        std::vector<const char*> instanceExtensions(requestedInstanceExtensions.begin(),
                                                    requestedInstanceExtensions.end());
        VkInstanceCreateFlags instanceFlags = 0;

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        std::uint32_t availableCount = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr) == VK_SUCCESS) {
            std::vector<VkExtensionProperties> available(availableCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
            const bool hasPortability = std::ranges::any_of(available, [](const VkExtensionProperties& e) {
                return std::string_view{e.extensionName} == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
            });
            if (hasPortability) {
                instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            }
        }
#endif

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.flags                   = instanceFlags;
        instanceInfo.pApplicationInfo        = &appInfo;
        instanceInfo.enabledExtensionCount   = static_cast<std::uint32_t>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

        if (vkCreateInstance(&instanceInfo, nullptr, &boot.instance_) != VK_SUCCESS) {
            return fail("could not create the Vulkan instance");
        }

        // The surface must exist before the present-queue choice, and it needs the instance.
        if (windowed) {
            if (makeSurface(boot.instance_, &boot.surface_) != VK_SUCCESS || boot.surface_ == VK_NULL_HANDLE) {
                return fail("could not create the window surface");
            }
        }

        std::uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(boot.instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            return fail("no Vulkan-capable device is available");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(boot.instance_, &deviceCount, devices.data());
        boot.physicalDevice_ = devices.front();

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(boot.physicalDevice_, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(boot.physicalDevice_, &familyCount, families.data());
        for (std::uint32_t i = 0; i < familyCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                boot.graphicsFamily_ = i;
            }
            if (windowed) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(boot.physicalDevice_, i, boot.surface_, &present);
                if (present == VK_TRUE) {
                    boot.presentFamily_ = i;
                }
            }
        }
        if (boot.graphicsFamily_ == UINT32_MAX) {
            return fail("the device has no graphics queue family");
        }
        if (windowed && boot.presentFamily_ == UINT32_MAX) {
            return fail("the device cannot present to the window surface");
        }

        std::vector<const char*> deviceExtensions;
        std::uint32_t            deviceExtCount = 0;
        vkEnumerateDeviceExtensionProperties(boot.physicalDevice_, nullptr, &deviceExtCount, nullptr);
        std::vector<VkExtensionProperties> deviceExts(deviceExtCount);
        vkEnumerateDeviceExtensionProperties(boot.physicalDevice_, nullptr, &deviceExtCount, deviceExts.data());
        const auto hasDeviceExt = [&](std::string_view wanted) {
            return std::ranges::any_of(deviceExts, [wanted](const VkExtensionProperties& e) {
                return wanted == e.extensionName;
            });
        };
        if (windowed) {
            if (!hasDeviceExt(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                return fail("the device does not provide VK_KHR_swapchain");
            }
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }
        // Spelled out, not via the macro, for tests/render/HeadlessDevice.hpp's reason: the name
        // lives in vulkan_beta.h and is only defined under VK_ENABLE_BETA_EXTENSIONS.
        if (hasDeviceExt("VK_KHR_portability_subset")) {
            deviceExtensions.push_back("VK_KHR_portability_subset");
        }

        std::set<std::uint32_t> uniqueFamilies{boot.graphicsFamily_};
        if (windowed) {
            uniqueFamilies.insert(boot.presentFamily_);
        }
        const float                          priority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (std::uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo q{};
            q.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            q.queueFamilyIndex = family;
            q.queueCount       = 1;
            q.pQueuePriorities = &priority;
            queueInfos.push_back(q);
        }

        VkPhysicalDeviceFeatures features{};
        VkDeviceCreateInfo       deviceInfo{};
        deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount    = static_cast<std::uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos       = queueInfos.data();
        deviceInfo.pEnabledFeatures        = &features;
        deviceInfo.enabledExtensionCount   = static_cast<std::uint32_t>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

        if (vkCreateDevice(boot.physicalDevice_, &deviceInfo, nullptr, &boot.device_) != VK_SUCCESS) {
            return fail("could not create the logical device");
        }
        vkGetDeviceQueue(boot.device_, boot.graphicsFamily_, 0, &boot.graphicsQueue_);
        if (windowed) {
            vkGetDeviceQueue(boot.device_, boot.presentFamily_, 0, &boot.presentQueue_);
        }
        return boot;
    }

    VkInstance       instance_{VK_NULL_HANDLE};
    VkSurfaceKHR     surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice         device_{VK_NULL_HANDLE};
    VkQueue          graphicsQueue_{VK_NULL_HANDLE};
    VkQueue          presentQueue_{VK_NULL_HANDLE};
    std::uint32_t    graphicsFamily_{UINT32_MAX};
    std::uint32_t    presentFamily_{UINT32_MAX};
};

// ===========================================================================
// The GLFW window + swapchain
// ===========================================================================

/// What one `beginFrame()` produced.
enum class PresentOutcome : std::uint8_t {
    Presented,   ///< a frame was acquired, recorded and queued for presentation
    Skipped,     ///< no swapchain image was ready within the timeout; try again next frame
    OutOfDate,   ///< the swapchain must be recreated (a resize) before the next frame
    DeviceLost,  ///< fatal — the caller must tear down
};

/// The command buffer the caller records its `UiRenderer::record()` into, already inside the render
/// pass, plus the extent it is drawing against.
struct FrameContext {
    VkCommandBuffer     commandBuffer{VK_NULL_HANDLE};
    mdux::core::Extent2D framebufferExtent{};
};

/**
 * @brief A GLFW window, its Vulkan surface, and a single-colour-attachment swapchain.
 *
 * Extracted from `examples/VulkanSCTriangleExample.cpp`'s window/swapchain/present code so the
 * monitor (#318) can reuse it. The triangle itself is not refactored onto this in this change.
 */
class GlfwWindow {
public:
    GlfwWindow(const GlfwWindow&)            = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    ~GlfwWindow() { destroy(); }

    /// Opens a window of `width` x `height` screen coordinates titled `title`, resizable, and brings
    /// up an instance / device / surface / swapchain. `std::nullopt` (with a `std::cerr` line) on any
    /// failure — never a partially-initialised window.
    [[nodiscard]] static std::optional<GlfwWindow> create(int width, int height, const char* title) {
        if (glfwInit() != GLFW_TRUE) {
            std::cerr << "adapter: GLFW would not initialise\n";
            return std::nullopt;
        }
        if (glfwVulkanSupported() != GLFW_TRUE) {
            std::cerr << "adapter: GLFW reports no Vulkan loader\n";
            glfwTerminate();
            return std::nullopt;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        GlfwWindow self;
        self.glfwOwned_ = true;
        self.window_    = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (self.window_ == nullptr) {
            std::cerr << "adapter: could not create the window\n";
            return std::nullopt;
        }

        std::uint32_t      glfwExtCount = 0;
        const char* const* glfwExts     = glfwGetRequiredInstanceExtensions(&glfwExtCount);
        if (glfwExts == nullptr) {
            std::cerr << "adapter: GLFW did not report the required instance extensions\n";
            return std::nullopt;
        }

        GLFWwindow* const window = self.window_;
        auto        boot         = VulkanBoot::forSurface(
            {glfwExts, glfwExtCount}, [window](VkInstance instance, VkSurfaceKHR* out) {
                return glfwCreateWindowSurface(instance, window, nullptr, out);
            });
        if (!boot) {
            std::cerr << "adapter: " << boot.error().what << '\n';
            return std::nullopt;
        }
        self.boot_ = std::move(*boot);

        if (!self.createRenderPass() || !self.createSwapchain() || !self.createSync()) {
            return std::nullopt;
        }
        return self;
    }

    [[nodiscard]] GLFWwindow* handle() const noexcept { return window_; }
    [[nodiscard]] bool        shouldClose() const noexcept { return glfwWindowShouldClose(window_) == GLFW_TRUE; }

    [[nodiscard]] mdux::core::Extent2D framebufferExtent() const noexcept {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        return {w, h};
    }
    [[nodiscard]] mdux::core::Extent2D windowExtent() const noexcept {
        int w = 0;
        int h = 0;
        glfwGetWindowSize(window_, &w, &h);
        return {w, h};
    }

    /// The context a `UiRenderer` is created against. `viewport` is the caller's to set — it is the
    /// authored surface extent, not the framebuffer (ADR-019 clause 2).
    [[nodiscard]] mdux::render::VulkanRenderContext renderContext() const noexcept {
        mdux::render::VulkanRenderContext context;
        context.device           = boot_.device();
        context.physicalDevice   = boot_.physicalDevice();
        context.renderPass       = renderPass_;
        context.queue            = boot_.graphicsQueue();
        context.queueFamilyIndex = boot_.graphicsFamily();
        return context;
    }

    /// Waits for the previous use of this frame slot, acquires an image, and begins a command buffer
    /// inside a render pass cleared to `clear`. On `Presented` the caller records into
    /// `frame.commandBuffer`, then calls `endFrame()`.
    [[nodiscard]] PresentOutcome beginFrame(mdux::core::ColorRgba8 clear, std::uint64_t timeoutNanos,
                                            FrameContext& frame) {
        const VkResult fence = vkWaitForFences(boot_.device(), 1, &inFlight_[slot_], VK_TRUE, timeoutNanos);
        if (fence == VK_TIMEOUT) {
            return PresentOutcome::Skipped;
        }
        if (fence != VK_SUCCESS) {
            // Device loss, or a host/device allocation failure. Not a skipped frame — every one of
            // these is fatal, and continuing would submit over work the GPU has not finished.
            return PresentOutcome::DeviceLost;
        }

        const VkResult acquired = vkAcquireNextImageKHR(boot_.device(), swapchain_, timeoutNanos,
                                                        imageAvailable_[slot_], VK_NULL_HANDLE, &imageIndex_);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            return PresentOutcome::OutOfDate;
        }
        if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
            return PresentOutcome::Skipped;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            // VK_ERROR_DEVICE_LOST and every other failure — fatal.
            return PresentOutcome::DeviceLost;
        }

        vkResetFences(boot_.device(), 1, &inFlight_[slot_]);
        vkResetCommandBuffer(commandBuffers_[slot_], 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(commandBuffers_[slot_], &begin) != VK_SUCCESS) {
            // The buffer is left invalid; recording a render pass onto it would be undefined.
            return PresentOutcome::DeviceLost;
        }

        VkClearValue clearValue{};
        clearValue.color.float32[0] = static_cast<float>(clear.r) / 255.0F;
        clearValue.color.float32[1] = static_cast<float>(clear.g) / 255.0F;
        clearValue.color.float32[2] = static_cast<float>(clear.b) / 255.0F;
        clearValue.color.float32[3] = static_cast<float>(clear.a) / 255.0F;
        VkRenderPassBeginInfo pass{};
        pass.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass        = renderPass_;
        pass.framebuffer       = framebuffers_[imageIndex_];
        pass.renderArea.extent = extent_;
        pass.clearValueCount   = 1;
        pass.pClearValues      = &clearValue;
        vkCmdBeginRenderPass(commandBuffers_[slot_], &pass, VK_SUBPASS_CONTENTS_INLINE);

        frame.commandBuffer     = commandBuffers_[slot_];
        frame.framebufferExtent = {static_cast<mdux::core::Px>(extent_.width),
                                   static_cast<mdux::core::Px>(extent_.height)};
        return PresentOutcome::Presented;
    }

    /// Ends the render pass and command buffer, submits, and presents. `OutOfDate` when the
    /// swapchain went stale during present (a resize); `Presented` otherwise.
    ///
    /// The submit signals `renderFinished_[imageIndex_]` — a semaphore **per swapchain image**, not
    /// per frame slot. A binary semaphore cannot be re-signalled until the previous wait on it has
    /// completed, and there is no fence for the *presentation* wait; under FIFO, an image is not
    /// re-acquired until its previous present finished, so keying the semaphore to the image is
    /// what makes the reuse safe (Khronos WSI guidance).
    [[nodiscard]] PresentOutcome endFrame() {
        vkCmdEndRenderPass(commandBuffers_[slot_]);
        vkEndCommandBuffer(commandBuffers_[slot_]);

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo               submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &imageAvailable_[slot_];
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &commandBuffers_[slot_];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &renderFinished_[imageIndex_];
        if (vkQueueSubmit(boot_.graphicsQueue(), 1, &submit, inFlight_[slot_]) != VK_SUCCESS) {
            return PresentOutcome::DeviceLost;
        }

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &renderFinished_[imageIndex_];
        present.swapchainCount     = 1;
        present.pSwapchains        = &swapchain_;
        present.pImageIndices      = &imageIndex_;

        const VkResult presented = vkQueuePresentKHR(boot_.presentQueue(), &present);
        slot_                    = (slot_ + 1) % kFramesInFlight;
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            return PresentOutcome::OutOfDate;
        }
        if (presented != VK_SUCCESS) {
            return PresentOutcome::DeviceLost;
        }
        return PresentOutcome::Presented;
    }

    /// Blocks until the device has finished every submitted frame. The caller runs this before it
    /// destroys a `UiRenderer` built against this window — the renderer frees buffers, descriptors
    /// and a pipeline the last in-flight frame may still be reading, and only this window's own
    /// teardown waits for the device otherwise.
    void waitIdle() const noexcept {
        if (boot_.device() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(boot_.device());
        }
    }

    /// Idles the device, tears the swapchain-derived objects down in reverse order, and rebuilds
    /// from the current framebuffer size. `false` (with a `std::cerr` line) on failure. A zero
    /// framebuffer — a minimised window — returns `true` without rebuilding; the caller retries.
    [[nodiscard]] bool recreateSwapchain() {
        const mdux::core::Extent2D fb = framebufferExtent();
        if (fb.width == 0 || fb.height == 0) {
            return true;
        }
        vkDeviceWaitIdle(boot_.device());
        destroySwapchain();
        return createSwapchain();
    }

public:
    GlfwWindow(GlfwWindow&& other) noexcept { moveFrom(other); }
    GlfwWindow& operator=(GlfwWindow&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

private:
    // One frame in flight. There is a single `UiRenderer` with one vertex/index buffer pair, and
    // `UiRenderer::record()` overwrites it in place — so a second in-flight frame would let the CPU
    // rewrite geometry the GPU is still reading for the previous one. Serialising to one frame is
    // the correct, small fix for a demonstrator; a real monitor would carry a renderer (or a buffer
    // ring) per slot. `beginFrame()` waits this slot's fence before it hands back a command buffer,
    // so `record()` only ever touches the buffer once the previous frame's GPU work has completed.
    static constexpr std::uint32_t kFramesInFlight = 1;

    GlfwWindow() = default;

    void moveFrom(GlfwWindow& other) noexcept {
        boot_           = std::move(other.boot_);
        window_         = std::exchange(other.window_, nullptr);
        swapchain_      = std::exchange(other.swapchain_, VK_NULL_HANDLE);
        renderPass_     = std::exchange(other.renderPass_, VK_NULL_HANDLE);
        commandPool_    = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        extent_         = std::exchange(other.extent_, VkExtent2D{});
        images_         = std::move(other.images_);
        imageViews_     = std::move(other.imageViews_);
        framebuffers_   = std::move(other.framebuffers_);
        commandBuffers_ = std::move(other.commandBuffers_);
        imageAvailable_ = std::move(other.imageAvailable_);
        renderFinished_ = std::move(other.renderFinished_);
        inFlight_       = std::move(other.inFlight_);
        slot_           = std::exchange(other.slot_, 0);
        imageIndex_     = std::exchange(other.imageIndex_, 0);
        glfwOwned_      = std::exchange(other.glfwOwned_, false);
    }

    [[nodiscard]] bool createRenderPass() {
        // A single-colour-attachment pass for B8G8R8A8_SRGB — the format createSwapchain() requires
        // the surface to offer. Built once and kept across resizes.
        VkAttachmentDescription color{};
        color.format         = VK_FORMAT_B8G8R8A8_SRGB;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription  subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments    = &color;
        info.subpassCount    = 1;
        info.pSubpasses      = &subpass;
        info.dependencyCount = 1;
        info.pDependencies   = &dep;
        if (vkCreateRenderPass(boot_.device(), &info, nullptr, &renderPass_) != VK_SUCCESS) {
            std::cerr << "adapter: could not create the render pass\n";
            return false;
        }

        VkCommandPoolCreateInfo pool{};
        pool.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = boot_.graphicsFamily();
        if (vkCreateCommandPool(boot_.device(), &pool, nullptr, &commandPool_) != VK_SUCCESS) {
            std::cerr << "adapter: could not create the command pool\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool createSwapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(boot_.physicalDevice(), boot_.surface(), &caps);

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(boot_.physicalDevice(), boot_.surface(), &formatCount, nullptr);
        if (formatCount == 0) {
            std::cerr << "adapter: the surface reports no formats\n";
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(boot_.physicalDevice(), boot_.surface(), &formatCount, formats.data());
        // The render pass was built for B8G8R8A8_SRGB. Take that pairing or fail — presenting through
        // an incompatible pass is undefined.
        const bool hasSrgb = std::ranges::any_of(formats, [](const VkSurfaceFormatKHR& f) {
            return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (!hasSrgb) {
            std::cerr << "adapter: the surface does not offer B8G8R8A8_SRGB / SRGB_NONLINEAR\n";
            return false;
        }
        const VkSurfaceFormatKHR chosen{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

        extent_ = caps.currentExtent;
        if (extent_.width == UINT32_MAX) {
            const mdux::core::Extent2D fb = framebufferExtent();
            extent_.width  = std::clamp(static_cast<std::uint32_t>(fb.width), caps.minImageExtent.width,
                                        caps.maxImageExtent.width);
            extent_.height = std::clamp(static_cast<std::uint32_t>(fb.height), caps.minImageExtent.height,
                                        caps.maxImageExtent.height);
        }

        std::uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface          = boot_.surface();
        info.minImageCount    = imageCount;
        info.imageFormat      = chosen.format;
        info.imageColorSpace  = chosen.colorSpace;
        info.imageExtent      = extent_;
        info.imageArrayLayers = 1;
        info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::uint32_t families[]{boot_.graphicsFamily(), boot_.presentFamily()};
        if (boot_.graphicsFamily() != boot_.presentFamily()) {
            info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices   = families;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform   = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode    = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped        = VK_TRUE;
        if (vkCreateSwapchainKHR(boot_.device(), &info, nullptr, &swapchain_) != VK_SUCCESS) {
            std::cerr << "adapter: could not create the swapchain\n";
            return false;
        }

        std::uint32_t actual = 0;
        vkGetSwapchainImagesKHR(boot_.device(), swapchain_, &actual, nullptr);
        images_.resize(actual);
        vkGetSwapchainImagesKHR(boot_.device(), swapchain_, &actual, images_.data());

        imageViews_.resize(actual);
        framebuffers_.resize(actual);
        for (std::uint32_t i = 0; i < actual; ++i) {
            VkImageViewCreateInfo view{};
            view.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image                       = images_[i];
            view.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
            view.format                      = chosen.format;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            if (vkCreateImageView(boot_.device(), &view, nullptr, &imageViews_[i]) != VK_SUCCESS) {
                std::cerr << "adapter: could not create a swapchain image view\n";
                return false;
            }
            VkFramebufferCreateInfo fb{};
            fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass      = renderPass_;
            fb.attachmentCount = 1;
            fb.pAttachments    = &imageViews_[i];
            fb.width           = extent_.width;
            fb.height          = extent_.height;
            fb.layers          = 1;
            if (vkCreateFramebuffer(boot_.device(), &fb, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
                std::cerr << "adapter: could not create a framebuffer\n";
                return false;
            }
        }

        if (commandBuffers_.empty()) {
            commandBuffers_.resize(kFramesInFlight);
            VkCommandBufferAllocateInfo alloc{};
            alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool        = commandPool_;
            alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = kFramesInFlight;
            if (vkAllocateCommandBuffers(boot_.device(), &alloc, commandBuffers_.data()) != VK_SUCCESS) {
                std::cerr << "adapter: could not allocate command buffers\n";
                return false;
            }
        }

        // One `renderFinished` semaphore per swapchain image — see `endFrame()`. Rebuilt with the
        // swapchain because the image count can change on a resize.
        renderFinished_.resize(actual);
        const VkSemaphoreCreateInfo sem{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (std::uint32_t i = 0; i < actual; ++i) {
            if (vkCreateSemaphore(boot_.device(), &sem, nullptr, &renderFinished_[i]) != VK_SUCCESS) {
                std::cerr << "adapter: could not create a presentation semaphore\n";
                return false;
            }
        }
        return true;
    }

    /// The per-frame-slot fence and image-available semaphore — `kFramesInFlight` of each, built
    /// once. `renderFinished` lives with the swapchain instead (`createSwapchain`).
    [[nodiscard]] bool createSync() {
        imageAvailable_.resize(kFramesInFlight);
        inFlight_.resize(kFramesInFlight);
        const VkSemaphoreCreateInfo sem{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        const VkFenceCreateInfo     fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                          .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (vkCreateSemaphore(boot_.device(), &sem, nullptr, &imageAvailable_[i]) != VK_SUCCESS ||
                vkCreateFence(boot_.device(), &fence, nullptr, &inFlight_[i]) != VK_SUCCESS) {
                std::cerr << "adapter: could not create the frame synchronisation objects\n";
                return false;
            }
        }
        return true;
    }

    void destroySwapchain() noexcept {
        for (VkSemaphore s : renderFinished_) {
            vkDestroySemaphore(boot_.device(), s, nullptr);
        }
        renderFinished_.clear();
        for (VkFramebuffer fb : framebuffers_) {
            vkDestroyFramebuffer(boot_.device(), fb, nullptr);
        }
        framebuffers_.clear();
        for (VkImageView view : imageViews_) {
            vkDestroyImageView(boot_.device(), view, nullptr);
        }
        imageViews_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(boot_.device(), swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void destroy() noexcept {
        // Order matters twice over. First the device children in reverse creation order, while the
        // device is alive. Then `boot_` — the device, the surface and the instance — **before**
        // GLFW is torn down: `vkDestroySurfaceKHR` for a Wayland surface reaches into the display
        // connection `glfwTerminate()` frees, so destroying the surface after it is a use-after-free.
        if (boot_.device() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(boot_.device());
            destroySwapchain();  // also destroys the per-image renderFinished_ semaphores
            for (VkSemaphore s : imageAvailable_) {
                vkDestroySemaphore(boot_.device(), s, nullptr);
            }
            for (VkFence f : inFlight_) {
                vkDestroyFence(boot_.device(), f, nullptr);
            }
            imageAvailable_.clear();
            inFlight_.clear();
            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(boot_.device(), commandPool_, nullptr);
                commandPool_ = VK_NULL_HANDLE;
            }
            if (renderPass_ != VK_NULL_HANDLE) {
                vkDestroyRenderPass(boot_.device(), renderPass_, nullptr);
                renderPass_ = VK_NULL_HANDLE;
            }
        }
        // The device, surface and instance, before GLFW — see the note above.
        boot_ = VulkanBoot{};
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwOwned_) {
            glfwTerminate();
            glfwOwned_ = false;
        }
    }

    VulkanBoot                   boot_{};
    GLFWwindow*                  window_{nullptr};
    VkSwapchainKHR               swapchain_{VK_NULL_HANDLE};
    VkRenderPass                 renderPass_{VK_NULL_HANDLE};
    VkCommandPool                commandPool_{VK_NULL_HANDLE};
    VkExtent2D                   extent_{};
    std::vector<VkImage>         images_;
    std::vector<VkImageView>     imageViews_;
    std::vector<VkFramebuffer>   framebuffers_;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore>     imageAvailable_;
    std::vector<VkSemaphore>     renderFinished_;
    std::vector<VkFence>         inFlight_;
    std::uint32_t                slot_{0};
    std::uint32_t                imageIndex_{0};
    bool                         glfwOwned_{false};
};

// ===========================================================================
// The event pump: native callbacks -> a caller-owned EventQueue
// ===========================================================================

/**
 * @brief Installs GLFW callbacks that translate native input into `mdux.medui.input` events and
 *        push them into a caller-owned `EventQueue`, and tracks the three flags a caller reads once
 *        per frame: the surface must be remapped (`takeSurfaceDirty`), the window lost focus
 *        (`takeFocusLost`), or a `push()` overflowed (`takeOverflow`) — each a `PressLatch::cancel()`
 *        signal.
 *
 * The queue, and everything it stores, is the caller's. This object holds a pointer to it, the
 * current `SurfaceMapping`, and the three flags. One per window; the user pointer its callbacks read
 * is cleared on destruction, so a callback that fires afterwards is a no-op.
 */
class WindowEventPump {
public:
    WindowEventPump(GLFWwindow* window, mdux::medui::EventQueue& queue, const mdux::medui::SurfaceMapping& mapping)
        : window_{window}, queue_{&queue}, mapping_{mapping} {
        glfwSetWindowUserPointer(window_, this);
        glfwSetCursorPosCallback(window_, &WindowEventPump::onCursorPos);
        glfwSetMouseButtonCallback(window_, &WindowEventPump::onMouseButton);
        glfwSetKeyCallback(window_, &WindowEventPump::onKey);
        glfwSetCharCallback(window_, &WindowEventPump::onChar);
        glfwSetWindowFocusCallback(window_, &WindowEventPump::onFocus);
        glfwSetCursorEnterCallback(window_, &WindowEventPump::onCursorEnter);
        glfwSetFramebufferSizeCallback(window_, &WindowEventPump::onFramebufferSize);
    }

    WindowEventPump(const WindowEventPump&)            = delete;
    WindowEventPump& operator=(const WindowEventPump&) = delete;

    ~WindowEventPump() {
        if (window_ != nullptr && glfwGetWindowUserPointer(window_) == this) {
            glfwSetWindowUserPointer(window_, nullptr);
        }
    }

    /// The mapping the pump applies to pointer coordinates. Rebuild it on the caller's side after a
    /// resize/scale change and hand the new one in here (ADR-019 clause 2).
    void setMapping(const mdux::medui::SurfaceMapping& mapping) noexcept { mapping_ = mapping; }

    /// `true` once a framebuffer-size change has been seen; the caller reads and clears it, rebuilds
    /// the swapchain and the `SurfaceMapping`, and `PressLatch::cancel()`s.
    [[nodiscard]] bool takeSurfaceDirty() noexcept { return std::exchange(surfaceDirty_, false); }

    /// `true` once the window has lost focus since the last check; the caller `PressLatch::cancel()`s.
    [[nodiscard]] bool takeFocusLost() noexcept { return std::exchange(focusLost_, false); }

    /// `true` while any queue `push()` since the last check was dropped — the batch is incomplete
    /// and the caller must `PressLatch::cancel()` (ADR-018 clause 2).
    [[nodiscard]] bool takeOverflow() noexcept { return std::exchange(overflowed_, false); }

private:
    [[nodiscard]] static WindowEventPump* self(GLFWwindow* window) noexcept {
        return static_cast<WindowEventPump*>(glfwGetWindowUserPointer(window));
    }

    void enqueue(const mdux::medui::InputEvent& event) noexcept {
        if (queue_->push(event) == mdux::medui::PushOutcome::DroppedNewest) {
            overflowed_ = true;
        }
    }

    static void onCursorPos(GLFWwindow* window, double x, double y) {
        if (auto* pump = self(window)) {
            pump->pointerAt(mdux::medui::PointerKind::Move, x, y);
        }
    }
    static void onMouseButton(GLFWwindow* window, int button, int action, int /*mods*/) {
        auto* pump = self(window);
        if (pump == nullptr || button != GLFW_MOUSE_BUTTON_LEFT) {
            return;
        }
        double x = 0;
        double y = 0;
        glfwGetCursorPos(window, &x, &y);
        pump->pointerAt(glfwButtonAction(action), x, y);
    }
    static void onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
        auto* pump = self(window);
        if (pump == nullptr || action == GLFW_REPEAT) {
            return;  // ADR-018 clause 1: MduX does not synthesise key-repeat; a repeat is the
                     // platform's to deliver, and GLFW_REPEAT here would double every caret move.
        }
        if (const auto code = glfwKeyToKeyCode(key, mods)) {
            pump->enqueue(mdux::medui::KeyEvent{
                .kind = action == GLFW_PRESS ? mdux::medui::KeyKind::Down : mdux::medui::KeyKind::Up,
                .key  = *code});
        }
    }
    static void onChar(GLFWwindow* window, unsigned int codepoint) {
        if (auto* pump = self(window)) {
            pump->enqueue(charToTextEvent(codepoint));
        }
    }
    static void onFocus(GLFWwindow* window, int focused) {
        if (auto* pump = self(window); pump != nullptr && focused == GLFW_FALSE) {
            pump->focusLost_ = true;
            pump->enqueue(mdux::medui::PointerEvent{.kind = mdux::medui::PointerKind::Cancel});
        }
    }
    static void onCursorEnter(GLFWwindow* window, int entered) {
        // The pointer left the window (ADR-019 clause 3: a lost pointer-capture gesture is void).
        // Cancel so a press cannot stay armed while the cursor is somewhere the operator can no
        // longer see the control.
        if (auto* pump = self(window); pump != nullptr && entered == GLFW_FALSE) {
            pump->enqueue(mdux::medui::PointerEvent{.kind = mdux::medui::PointerKind::Cancel});
        }
    }
    static void onFramebufferSize(GLFWwindow* window, int /*w*/, int /*h*/) {
        if (auto* pump = self(window)) {
            pump->surfaceDirty_ = true;
        }
    }

    void pointerAt(mdux::medui::PointerKind kind, double x, double y) noexcept {
        // Scale first, then floor toward −∞ — the same rule `mdux::medui::normalizeSurfacePoint()`
        // applies, done here because GLFW hands us fractional coordinates and the governed module is
        // integer-only. Rounding `x`/`y` before scaling (an earlier `std::lround`) pushed a
        // sub-pixel position onto the adjacent authored cell at a non-1:1 device-pixel ratio: window
        // 10.75 at 2× became authored 22 rather than ⌊21.5⌋ = 21.
        const double ratio     = static_cast<double>(mapping_.scaleNum) / mapping_.scaleDen;
        const double authoredX = std::floor((x - mapping_.originX) * ratio);
        const double authoredY = std::floor((y - mapping_.originY) * ratio);
        // `core::Px` is `std::int32_t`. −2³¹ and 2³¹ are both exactly representable as `double`,
        // INT32_MAX is not — so bound with the half-open `[−2³¹, 2³¹)` and the cast is always in
        // range. Fail-closed, as `normalizeSurfacePoint`'s `CoordinateOutOfRange`: never clamp a
        // wrapped coordinate onto a real control.
        constexpr double lo = -2147483648.0;
        constexpr double hi = 2147483648.0;
        if (!(authoredX >= lo && authoredX < hi && authoredY >= lo && authoredY < hi)) {
            return;
        }
        enqueue(mdux::medui::PointerEvent{.kind = kind,
                                          .x    = static_cast<mdux::core::Px>(authoredX),
                                          .y    = static_cast<mdux::core::Px>(authoredY)});
    }

    GLFWwindow*                 window_{nullptr};
    mdux::medui::EventQueue*    queue_{nullptr};
    mdux::medui::SurfaceMapping mapping_{};
    bool                        surfaceDirty_{false};
    bool                        focusLost_{false};
    bool                        overflowed_{false};
};

}  // namespace mdux::examples
