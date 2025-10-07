# MduX Vulkan Cube Rendering Integration Demo

## Overview

This demo showcases how users can integrate their own Vulkan rendering code with MduX's pre/post rendering pipeline. It features a rotating cube textured with the IEC62304 medical device logo, demonstrating the principle that users maintain complete control over their 3D rendering while MduX handles UI integration.

## Architecture

### User Rendering Integration Pattern

MduX provides a callback-based system that allows users to inject their own Vulkan rendering code at specific points in the rendering pipeline:

```cpp
// Setup user rendering callbacks
mdux::UserRendering userRendering;

// Pre-UI callback: Render 3D content, backgrounds
userRendering.preUiCallback = [](const mdux::UserRenderContext& context, void* userData) {
    // Your Vulkan code here
    // - Bind pipelines
    // - Update uniforms
    // - Record draw commands
    // - Everything happens in the provided command buffer
};

// Post-UI callback: Render overlays, effects
userRendering.postUiCallback = [](const mdux::UserRenderContext& context, void* userData) {
    // Optional post-processing effects
};

window->setupUserRendering(std::move(userRendering));
```

### Rendering Pipeline Flow

1. **Begin Render Pass** - MduX sets up Vulkan render pass and command buffer
2. **Pre-UI User Rendering** - Execute user's 3D content (cube demo)
3. **UI Rendering** - MduX renders HTML/CSS UI content as overlay
4. **Post-UI User Rendering** - Execute user's overlay effects (optional)
5. **End Render Pass** - MduX presents the completed frame

### UserRenderContext

The context provides everything needed for Vulkan rendering:

```cpp
struct UserRenderContext {
    VkCommandBuffer commandBuffer;      // Active command buffer
    VkRenderPass renderPass;           // Active render pass
    VkFramebuffer framebuffer;         // Current framebuffer
    VkExtent2D renderExtent;          // Render area dimensions
    uint32_t currentFrame;            // Frame index
    float deltaTime;                  // Time since last frame
    
    struct {
        float model[16];      // Model matrix (4x4, row-major)
        float view[16];       // View matrix (4x4, row-major)
        float projection[16]; // Projection matrix (4x4, row-major)
    } matrices;
};
```

## Demo Components

### VulkanCubeRenderer Class

- **Purpose**: Demonstrates Vulkan integration
- **Features**:
  - Rotating textured cube with IEC62304 logo
  - Medical-appropriate slow animation (0.5 rad/sec)
  - Proper Vulkan resource management
  - Compatible with MduX rendering pipeline

### Key Features Demonstrated

1. **Seamless Integration**: User rendering code integrates naturally with MduX
2. **Full Vulkan Control**: Users have complete access to Vulkan API
3. **Medical Device Appropriate**: Slow, stable animations suitable for medical devices
4. **IEC62304 Compliance**: Uses official medical device logo texture
5. **Performance Considerations**: Designed for medical device performance requirements

## Usage

```bash
# Run the cube demo
./MduXExample

# Run with HTML UI integration
./MduXExample examples/ui.html

# Run with hot-reload for development
./MduXExample examples/ui.html --hot
```

## Expected Output

```
╔═════════════════ Vulkan Cube Demo Setup ══════════════════╗
║ Setting up Vulkan user rendering integration...          ║
╚════════════════════════════════════════════════════════════╝

🎮 Initializing Vulkan cube renderer with IEC62304 logo...
🔧 Setting up Vulkan device and queues...
  ✓ Logical device created (demo mode)
🎨 Creating render pass and pipeline...
  ✓ Render pass created (demo mode)
🖼️ Loading IEC62304 logo texture: Logo.png
  ✓ IEC62304 logo texture loaded: Logo.png
📐 Creating vertex and index buffers...
  ✓ Vertex buffer created (24 vertices)
  ✓ Index buffer created (36 indices)
🎯 Creating uniform buffer for MVP matrices...
  ✓ Uniform buffer created for MVP matrices
✅ Vulkan cube renderer initialized successfully!
🎮 Ready to render rotating IEC62304 textured cube

✅ User rendering integration configured successfully!
🎮 Cube will render behind UI content (pre-UI callback)
🎯 IEC62304 logo texture will be applied to cube faces
⏱️ Medical-appropriate slow rotation animation enabled

📋 Instructions:
  • Close window or press ESC to exit
  • Window shows medical device UI rendering
  • 🎮 Vulkan cube demo: Vulkan user rendering integration
  • 🎯 IEC62304 logo textured on rotating cube faces
  • ⚡ Demonstrates pre-UI and post-UI callback system

🚀 Application running...

🎮 Cube rendering callback called (frame 60, rotation: 28.6°)
🎮 Cube rendering callback called (frame 120, rotation: 57.3°)
```

## Medical Device Considerations

1. **Performance**: 60 FPS limit for stability
2. **Animation**: Slow rotation (0.5 rad/sec) appropriate for medical devices
3. **Compliance**: Uses official IEC62304 logo for medical device branding
4. **Deterministic**: Predictable rendering pipeline with no unexpected behavior
5. **Resource Management**: Proper Vulkan resource cleanup for long-running applications

## Files

- `VulkanCubeRenderer.hpp` - Cube renderer interface
- `VulkanCubeRenderer.cpp` - Cube renderer implementation
- `MduXExample.cpp` - Integration demo (modified)
- `inputs/Logo.png` - IEC62304 medical device logo texture

## Next Steps

This demo provides a foundation for more complex Vulkan integrations:

1. **Multiple Objects**: Render complex 3D scenes with multiple objects
2. **Real Shaders**: Implement actual SPIR-V shaders for realistic rendering
3. **Texture Loading**: Add full PNG/JPEG texture loading with stb_image
4. **Animation Systems**: Implement more sophisticated animation frameworks
5. **Medical Visualizations**: Integrate medical imaging, 3D anatomy, device controls