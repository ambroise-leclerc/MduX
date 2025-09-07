/**
 * @file VulkanCubeRenderer.cppm
 * @brief Vulkan textured cube renderer for MduX integration demo
 * 
 * This class demonstrates how users can integrate their own Vulkan
 * rendering code with MduX's pre/post rendering pipeline.
 * Features a rotating cube textured with the IEC62304 logo.
 */

// Global module fragment - include C headers before modules
module;
#include <vulkan/vulkan.h>
#include <cstdint>

export module VulkanCubeRenderer;

import std;
import mdux;

export class VulkanCubeRenderer {
public:
    VulkanCubeRenderer() = default;
    ~VulkanCubeRenderer() { cleanup(); }
    
    // Non-copyable but movable
    VulkanCubeRenderer(const VulkanCubeRenderer&) = delete;
    VulkanCubeRenderer& operator=(const VulkanCubeRenderer&) = delete;
    VulkanCubeRenderer(VulkanCubeRenderer&&) = default;
    VulkanCubeRenderer& operator=(VulkanCubeRenderer&&) = default;
    
    /**
     * @brief Initialize Vulkan resources for cube rendering
     * @param vkInstance Vulkan instance
     * @param surface Vulkan surface  
     * @param logoPath Path to IEC62304 logo PNG file
     * @return true if initialization successful
     */
    bool initialize(VkInstance vkInstance, VkSurfaceKHR surface, const std::filesystem::path& logoPath);
    
    /**
     * @brief Render the textured rotating cube
     * @param context MduX user rendering context
     * @param userData Optional user data (unused in this demo)
     */
    void renderCube(const mdux::VulkanContext& context, void* userData);
    
    /**
     * @brief Update cube rotation animation
     * @param deltaTime Time since last frame
     */
    void updateAnimation(float deltaTime);
    
    /**
     * @brief Cleanup Vulkan resources
     */
    void cleanup();

private:
    // Vulkan core objects
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    uint32_t presentQueueFamily = 0;
    
    // Render pass and pipeline
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    // Vertex and index buffers
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    
    // Uniform buffer for MVP matrices
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;
    void* uniformBufferMapped = nullptr;
    
    // Texture resources
    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
    VkImageView textureImageView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    
    // Command buffer for setup
    VkCommandPool commandPool = VK_NULL_HANDLE;
    
    // Animation state
    float rotationAngle = 0.0f;
    
    // Vertex data structure
    struct Vertex {
        float pos[3];    // Position
        float texCoord[2]; // Texture coordinates
    };
    
    // Uniform buffer object for MVP matrices
    struct UniformBufferObject {
        float model[16];
        float view[16];
        float proj[16];
    };
    
    // Private helper methods
    bool createLogicalDevice();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createUniformBuffer();
    bool createDescriptorPool();
    bool createDescriptorSet();
    bool loadTexture(const std::filesystem::path& logoPath);
    bool createTextureImage(const std::filesystem::path& imagePath);
    bool createTextureImageView();
    bool createTextureSampler();
    bool createCommandPool();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    bool createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                    VkImageUsageFlags usage, VkMemoryPropertyFlags properties, 
                    VkImage& image, VkDeviceMemory& imageMemory);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    
    void updateUniformBuffer(const mdux::VulkanContext& context);
    void createModelMatrix(float* matrix, float angle);
    void createViewMatrix(float* matrix);
    void createProjectionMatrix(float* matrix, float aspect);
    void multiplyMatrices(const float* a, const float* b, float* result);
    void setIdentityMatrix(float* matrix);
    
    // Direct rendering when MduX doesn't provide command buffer
    bool renderDirectToSurface(const mdux::VulkanContext& context);
    
    // Cube geometry data
    static const std::vector<Vertex> vertices;
    static const std::vector<uint16_t> indices;
    
    // Shader code (SPIR-V bytecode will be embedded)
    static const std::vector<char> vertexShaderCode;
    static const std::vector<char> fragmentShaderCode;
};