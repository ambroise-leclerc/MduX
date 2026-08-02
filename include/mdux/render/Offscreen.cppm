/**
 * @brief Adapter-zone headless render target: a fixed-size colour image, no window, no swapchain.
 *
 * @compliance ADR-004 Trust zones in C++ (adapter zone: may name Vulkan, never named by MduXCore)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 *
 * Part of MduX. This is what makes rendered output checkable: a frame is drawn into memory this
 * object owns, copied back to the host, and returned as plain `ColorRgba8` pixels a test can
 * assert on. No surface, no swapchain, no presentation engine, and therefore no display server -
 * which is the only reason a pixel test can run in CI at all.
 *
 * ## Why this is library code rather than test-only scaffolding
 *
 * Offscreen rendering is not exclusively a testing concern. A device that renders to a buffer it
 * then hands to a display controller, or that captures a frame for an audit record, wants exactly
 * this. Putting it in the adapter zone means #126's pixel test and a manufacturer's capture path
 * are the same code, so the thing CI exercises is the thing that ships.
 *
 * ## The image never leaves the device's control
 *
 * The colour image, its memory, the view, the render pass, the framebuffer and the readback
 * buffer are all owned here and destroyed in reverse order. The caller supplies a device, a
 * physical device and a queue family, and gets pixels back; it never sees a `VkImage`. That keeps
 * the ownership question out of the test harness, where a leak would be attributed to whichever
 * test happened to run last.
 *
 * ## Format
 *
 * `VK_FORMAT_R8G8B8A8_UNORM`, so a readback row maps onto `mdux::core::ColorRgba8` with no
 * conversion and no channel swizzle. A test comparing an expected colour compares the bytes the
 * shader wrote, not a reinterpretation of them.
 */
module;

#include <vulkan/vulkan.h>

export module mdux.render.offscreen;

import std;
import mdux.core.result;
import mdux.core.units;

export namespace mdux::render {

enum class OffscreenError : std::uint8_t {
    NullDevice,
    NullPhysicalDevice,
    NullQueue,
    EmptyExtent,
    ExtentTooLarge,               ///< width * height * 4 would not fit a readback buffer
    ImageCreationFailed,
    NoSuitableMemoryType,
    MemoryAllocationFailed,
    ImageViewCreationFailed,
    RenderPassCreationFailed,
    FramebufferCreationFailed,
    BufferCreationFailed,
    MemoryMapFailed,
    CommandPoolCreationFailed,
    CommandBufferAllocationFailed,
    ResetCommandBufferFailed,  ///< the buffer is left invalid; recording on would be UB
    BeginCommandBufferFailed,
    EndCommandBufferFailed,
    SubmitFailed,
    WaitFailed,
};

[[nodiscard]] std::string_view describe(OffscreenError error) noexcept;

/// Records draw commands into a command buffer that is already inside the render pass.
///
/// A plain function pointer with a context, rather than `std::function`: this is called on a path
/// that must not allocate, and the harness that supplies it always has a stable context object.
using RecordCommands = void (*)(VkCommandBuffer commandBuffer, void* context);

/**
 * @brief A fixed-size offscreen colour target and its readback path.
 *
 * Move-only, for the same reason `UiRenderer` is: a copy would duplicate handles the destructor
 * frees, and the second destruction would be a use-after-free of device objects.
 */
class OffscreenTarget {
public:
    /// The largest surface this will allocate. A pixel test wants tens of thousands of pixels,
    /// not tens of millions, and an accidental extent typo should be an error rather than a
    /// multi-gigabyte allocation on a device.
    static constexpr std::uint64_t maxPixels = 1u << 22;  // 4 Mpx, e.g. 2048x2048

    [[nodiscard]] static mdux::core::Result<OffscreenTarget, OffscreenError> create(
        VkDevice device, VkPhysicalDevice physicalDevice, mdux::core::Extent2D extent,
        std::uint32_t queueFamilyIndex) noexcept;

    ~OffscreenTarget();

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;
    OffscreenTarget(OffscreenTarget&& other) noexcept;
    OffscreenTarget& operator=(OffscreenTarget&& other) noexcept;

    /// The render pass a `UiRenderer` must be created against to draw into this target.
    [[nodiscard]] VkRenderPass renderPass() const noexcept { return renderPass_; }
    [[nodiscard]] mdux::core::Extent2D extent() const noexcept { return extent_; }

    /**
     * @brief Clears to `clear`, runs `record` inside the render pass, and reads the result back.
     *
     * Submits, waits for the queue to idle, and copies the image to host memory. Synchronous by
     * design: a test that had to poll a fence would be a test that can hang, and there is no
     * frame rate to keep up with here.
     *
     * The returned span is owned by this object and stays valid until the next call or until the
     * object is destroyed. Rows are tightly packed, `extent.width` pixels each.
     */
    [[nodiscard]] mdux::core::Result<std::span<const mdux::core::ColorRgba8>, OffscreenError>
    renderAndRead(VkQueue queue, mdux::core::ColorRgba8 clear, RecordCommands record,
                  void* context) noexcept;

    /// The pixel at (x, y) from the last `renderAndRead`, or nullopt when out of bounds.
    ///
    /// Bounds-checked and returning an optional rather than indexing: an out-of-range read in a
    /// pixel test is a mistake in the expectation, and it should say so rather than compare
    /// whatever was next in memory.
    ///
    /// `std::optional` is safe here despite ColorRgba8 being imported from `mdux.core.units`. The
    /// GCC 15 defect recorded in tools/shader/ShaderBake.cppm needs a type that is *not* trivially
    /// destructible - it is the implicit destructor's exception specification that is computed
    /// inconsistently across the module boundary. ColorRgba8 is an aggregate of four
    /// `std::uint8_t`, so `std::optional` gives it a trivial destructor and there is nothing to
    /// disagree about. Both GCC legs compile this today; a value type with a `std::string` in it
    /// would need `mdux::core::Result` instead.
    [[nodiscard]] std::optional<mdux::core::ColorRgba8> pixelAt(mdux::core::Px x,
                                                                mdux::core::Px y) const noexcept;

private:
    OffscreenTarget() noexcept = default;
    void destroy() noexcept;

    VkDevice device_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VkDeviceMemory imageMemory_{VK_NULL_HANDLE};
    VkImageView imageView_{VK_NULL_HANDLE};
    VkRenderPass renderPass_{VK_NULL_HANDLE};
    VkFramebuffer framebuffer_{VK_NULL_HANDLE};
    VkBuffer readbackBuffer_{VK_NULL_HANDLE};
    VkDeviceMemory readbackMemory_{VK_NULL_HANDLE};
    void* readbackMapped_{nullptr};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    mdux::core::Extent2D extent_{};
};

}  // namespace mdux::render
