/**
 * @file VulkanRenderer.cppm
 * @brief Adapter-zone fixed-budget Vulkan renderer: where a governed DrawList becomes commands.
 *
 * @compliance ADR-004 Trust zones in C++ (adapter zone: may name Vulkan, never named by MduXCore)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 *
 * Part of MduX, not MduXCore. This is the only module in the shader/draw/render chain that names
 * a Vulkan type, and the boundary is mechanical: `mdux.draw` describes a frame, this renders it,
 * and the governed side never learns that Vulkan exists.
 *
 * ## Everything is allocated once
 *
 * `create()` builds the pipeline, allocates the vertex and index buffers from the budget, and maps
 * them for the object's lifetime. `record()` copies a frame into those buffers and emits commands.
 * There is no allocation, no buffer growth, and no descriptor churn per frame - a frame whose
 * `DrawList` exceeds the budget the renderer was built with is refused, not accommodated.
 *
 * That refusal is the whole design. A renderer that grew its buffers would have a per-frame cost
 * nobody bounded and a worst case nobody measured, and on a device the first symptom is a missed
 * deadline. Here it is a `RenderError` at the point the frame was built.
 *
 * ## Caller-owned device, caller-owned render pass
 *
 * `VulkanRenderContext` is borrowed, never owned. MduX does not create instances, devices, queues,
 * surfaces or swapchains, and does not want to: those belong to the application, which knows what
 * else is running on the device. What this renderer owns is exactly what it created - the shader
 * modules, the descriptor set layout, the pipeline layout, the pipeline, the two buffers and their
 * memory - and it destroys those in reverse order in its destructor and nowhere else.
 *
 * ## The renderer owns a default atlas, so a solid rectangle needs no ceremony
 *
 * The fragment shader samples an atlas in two of its three modes, so the pipeline layout declares
 * a combined image sampler - and a draw recorded without a descriptor set bound is undefined
 * behaviour, whatever mode the vertices ask for. Rather than make every caller build a descriptor
 * set before it can draw a rectangle, the renderer creates and binds a 1x1 opaque white texture.
 *
 * That is not a placeholder to be removed. Sampling white and multiplying by the vertex colour is
 * the identity for the sampled-RGBA path and a full-coverage mask for the R8 path, so a default
 * atlas is the correct neutral value rather than a stand-in for a real one. #14 and #17 replace
 * the *contents* when they have glyphs and images to put there; the mechanism stays.
 *
 * This is why the context carries a queue: uploading one white pixel needs a layout transition,
 * and a layout transition needs a submitted command. It is used once, during `create()`, and
 * never touched per frame.
 *
 * ## No runtime shader I/O
 *
 * Shader bytes come from a `shader::PackageView`, which generated code supplies as `constexpr`
 * data (issue #121). There is no file path here, no working-directory assumption, and nothing to
 * go missing on a device - the failure mode `examples/VulkanSCTriangleExample.cpp` currently has,
 * and which #122 removes.
 */
module;

#include <vulkan/vulkan.h>

export module mdux.render.vulkan;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.shader.schema;

export namespace mdux::render {

/**
 * @brief The caller's Vulkan objects, borrowed for the renderer's lifetime.
 *
 * Every member is initialised, unlike the older `mdux::VulkanContext` this replaces - an
 * uninitialised `VkDevice` reads as a plausible handle rather than as a null one, so validation
 * would pass and the first real call would fault.
 */
struct VulkanRenderContext {
    VkDevice device{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::uint32_t subpass{0};

    /// Used once, during `create()`, to upload the default atlas. Never touched per frame.
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queueFamilyIndex{0};

    /// The area the vertex shader converts pixel coordinates against. Not a framebuffer size:
    /// a renderer drawing into a region of a larger target passes that region's extent.
    mdux::core::Extent2D viewport{};

    [[nodiscard]] bool isValid() const noexcept {
        return device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE &&
               renderPass != VK_NULL_HANDLE && queue != VK_NULL_HANDLE && viewport.width > 0 &&
               viewport.height > 0;
    }
};

enum class RenderError : std::uint8_t {
    NullDevice,
    NullPhysicalDevice,
    NullRenderPass,
    NullQueue,
    EmptyViewport,
    EmptyBudget,
    BudgetExceedsIndexWidth,
    MissingVertexModule,      ///< the package declares no vertex stage
    MissingFragmentModule,
    ShaderModuleCreationFailed,
    DescriptorSetLayoutCreationFailed,
    PipelineLayoutCreationFailed,
    PipelineCreationFailed,
    BufferCreationFailed,
    NoSuitableMemoryType,     ///< no host-visible, host-coherent memory type on this device
    MemoryAllocationFailed,
    MemoryMapFailed,
    ImageCreationFailed,      ///< the default atlas
    ImageViewCreationFailed,
    SamplerCreationFailed,
    DescriptorPoolCreationFailed,
    DescriptorSetAllocationFailed,
    CommandPoolCreationFailed,    ///< the one-shot pool used to upload the default atlas
    CommandBufferAllocationFailed,
    AtlasUploadFailed,
    AtlasExtentMismatch,      ///< the coverage byte count is not width * height, or an extent is zero
    SampledRgbaWithCoverageAtlas,  ///< the frame samples RGBA from a renderer holding an R8 sheet
    NullCommandBuffer,
    FrameExceedsBudget,       ///< the DrawList is larger than the renderer was built for

    // The package declares a pipeline contract this renderer does not implement. Refused at
    // create() rather than mistranslated, because every one of these becomes either a
    // validation-layer message far from its cause or a set bound at the wrong index.
    UnsupportedDescriptorSet,     ///< a descriptor outside set 0; only one set layout is built
    DuplicateDescriptorBinding,   ///< two descriptors share a binding number within a set
    UnsupportedDescriptorContract,///< not exactly one non-array combined image sampler
    UnsupportedPushConstantContract, ///< not exactly one vertex-visible UiPushConstants range
};

[[nodiscard]] std::string_view describe(RenderError error) noexcept;

/**
 * @brief Records a governed `DrawList` into a caller-supplied command buffer.
 *
 * Move-only. A copy would duplicate handles this object destroys, and the second destruction
 * would be a use-after-free of device objects - so copying is deleted rather than deep-copied,
 * which for Vulkan handles is not a meaningful operation anyway.
 */
class UiRenderer {
public:
    /**
     * @brief Builds the pipeline and allocates the frame buffers.
     *
     * @param context  caller-owned device, physical device and render pass; borrowed
     * @param package  the shader package, from generated `constexpr` data
     * @param budget   the ceiling every frame this renderer records must fit within
     *
     * Fails rather than adapts: an invalid context, an empty budget, or a package missing a stage
     * are all errors here, where they are attributable, rather than a device loss later.
     */
    [[nodiscard]] static mdux::core::Result<UiRenderer, RenderError> create(
        const VulkanRenderContext& context, const mdux::shader::PackageView& package,
        const mdux::draw::DrawBudget& budget) noexcept;

    /**
     * @brief Creates a renderer whose atlas is a baked R8 coverage sheet rather than the default.
     *
     * Replaces the atlas' *contents*, not the mechanism: #13 already created the image, sampler
     * and descriptor, and this overload uploads different bytes into the same arrangement. There
     * is no second binding, no second pipeline and no branch at record time - the shader's
     * `CoverageR8` mode was always reading whatever this image held.
     *
     * Taken at construction rather than as a later `setAtlas()` because the descriptor is written
     * once during `create()`. A renderer that could swap atlases mid-life would need either a
     * descriptor rewrite between frames or a second set, and neither is worth carrying for a font
     * that is chosen at build time.
     *
     * @param atlas   `width * height` bytes of coverage, row-major, top row first - exactly the
     *                sidecar `mdux-textbake` commits
     * @param width   sheet width in pixels; must be non-zero and match `atlas.size()`
     * @param height  sheet height in pixels
     *
     * ## This renderer can then draw text and solids, but not images
     *
     * The atlas is `VK_FORMAT_R8_UNORM`, and there is one of them. `DrawMode::SampledRgba` reads
     * the same image expecting four channels, so it would sample red-only and return
     * `(coverage, 0, 0, 1)` - a plausible picture in the wrong colours, which is the kind of
     * failure nobody notices in review.
     *
     * So `record()` refuses a list containing any `SampledRgba` vertex on a coverage renderer,
     * with `SampledRgbaWithCoverageAtlas`. Mixing baked text and images in one frame needs two
     * atlas bindings, which is #17's problem rather than something to leave as a trap here.
     */
    [[nodiscard]] static mdux::core::Result<UiRenderer, RenderError> createWithCoverageAtlas(
        const VulkanRenderContext& context, const mdux::shader::PackageView& package,
        const mdux::draw::DrawBudget& budget, std::span<const std::byte> atlas,
        std::uint32_t width, std::uint32_t height) noexcept;

    ~UiRenderer();

private:
    /// The shared body of both create() overloads. They differ only in the atlas they upload, so
    /// the pipeline, buffers, sampler and descriptor arrangement are built once here.
    [[nodiscard]] static mdux::core::Result<UiRenderer, RenderError> createInternal(
        const VulkanRenderContext& context, const mdux::shader::PackageView& package,
        const mdux::draw::DrawBudget& budget, VkFormat atlasFormat, std::uint32_t atlasWidth,
        std::uint32_t atlasHeight, std::span<const std::byte> atlasPixels) noexcept;

public:

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    UiRenderer(UiRenderer&& other) noexcept;
    UiRenderer& operator=(UiRenderer&& other) noexcept;

    /**
     * @brief Copies `list` into the mapped buffers and records its commands.
     *
     * The command buffer must already be recording, inside a render pass compatible with the one
     * the renderer was created against. The atlas descriptor set is bound here, so a caller that
     * only draws solid rectangles needs no descriptor plumbing of its own.
     */
    [[nodiscard]] mdux::core::ResultVoid<RenderError> record(
        VkCommandBuffer commandBuffer, const mdux::draw::DrawList& list) noexcept;

    [[nodiscard]] const mdux::draw::DrawBudget& budget() const noexcept { return budget_; }
    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const noexcept {
        return descriptorSetLayout_;
    }

    /// Bytes reserved for vertices and for indices. Fixed at construction; reported so a caller
    /// can record what a screen's budget actually cost on the device.
    [[nodiscard]] VkDeviceSize vertexBufferSize() const noexcept { return vertexBytes_; }
    [[nodiscard]] VkDeviceSize indexBufferSize() const noexcept { return indexBytes_; }

private:
    UiRenderer() noexcept = default;

    /// Destroys everything this object created, in reverse order of creation, and leaves every
    /// handle null. Called by the destructor and by move-assignment; safe to call twice.
    void destroy() noexcept;

    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule vertexModule_{VK_NULL_HANDLE};
    VkShaderModule fragmentModule_{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorSetLayout_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkBuffer vertexBuffer_{VK_NULL_HANDLE};
    VkDeviceMemory vertexMemory_{VK_NULL_HANDLE};
    VkBuffer indexBuffer_{VK_NULL_HANDLE};
    VkDeviceMemory indexMemory_{VK_NULL_HANDLE};
    VkImage atlasImage_{VK_NULL_HANDLE};
    VkDeviceMemory atlasMemory_{VK_NULL_HANDLE};
    VkImageView atlasView_{VK_NULL_HANDLE};
    VkSampler atlasSampler_{VK_NULL_HANDLE};
    /// True when the atlas is an R8 coverage sheet, so `record()` can refuse a frame that samples
    /// it as RGBA. Cheaper to carry than to query, and the answer never changes after create().
    bool atlasIsCoverageOnly_{false};
    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
    void* vertexMapped_{nullptr};
    void* indexMapped_{nullptr};
    VkDeviceSize vertexBytes_{0};
    VkDeviceSize indexBytes_{0};
    mdux::draw::DrawBudget budget_{};
    mdux::core::Extent2D viewport_{};

    // What create() validated the package declares, so record() and the descriptor write use the
    // package's numbers rather than repeating literals that were only ever true for the current
    // shader. If the contract changes, create() refuses; it does not silently disagree with the
    // pipeline layout it built.
    std::uint32_t atlasBinding_{0};
    VkShaderStageFlags pushStages_{0};
    std::uint32_t pushOffset_{0};
    std::uint32_t pushSize_{0};
};

/// The push-constant block the UI vertex shader declares: the viewport size, in pixels.
///
/// Declared here rather than assembled inline so its size can be checked against what the shader
/// package records - a mismatch is a compile-time-visible number rather than a corrupted uniform.
struct alignas(4) UiPushConstants {
    float viewportWidth{0.0F};
    float viewportHeight{0.0F};
};

static_assert(sizeof(UiPushConstants) == 8, "must match the shader's push constant block");

}  // namespace mdux::render
