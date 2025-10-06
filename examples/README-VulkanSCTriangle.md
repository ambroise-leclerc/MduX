# Vulkan SC Pattern Demo - Triangle with Static Memory Management

**IEC 62304 Class B Compliance Example**

⚠️ **IMPORTANT**: This is **NOT** true Vulkan SC. This example demonstrates Vulkan SC patterns and constraints using standard Vulkan 1.3 API for development and testing purposes.

This example renders a simple colored triangle while showcasing all the critical Vulkan SC patterns required for medical device software, implemented on top of standard Vulkan infrastructure.

For information about migrating to true Vulkan SC, see [VulkanSC-vs-Vulkan.md](../docs/VulkanSC-vs-Vulkan.md).

---

## 📋 What This Example Demonstrates

### ✅ Vulkan SC Patterns Implemented:
- **Object Reservation Pre-calculation** - All Vulkan objects sized upfront
- **Static Memory Pool Management** - No runtime `vkFreeMemory()` calls
- **Device-Lifetime Object Management** - Command pools persist until device destruction
- **Medical Application Profiling** - Resource requirements calculated from medical context
- **Offline Shader Compilation** - SPIR-V bytecode compiled at build time
- **Deterministic Rendering** - Predictable behavior for safety-critical systems
- **Regulatory Compliance Patterns** - IEC 62304, ISO 13485 audit trails

### ❌ Standard Vulkan Used (NOT in true Vulkan SC):
- **VK_KHR_swapchain** - Doesn't exist in Vulkan SC (would use direct framebuffer)
- **Runtime Pipeline Creation** - True Vulkan SC requires offline Pipeline Cache Compiler
- **VK_API_VERSION_1_3** - True Vulkan SC uses variant=1 in version
- **Standard Vulkan Headers** - True Vulkan SC uses `<vulkan/vulkan_sc.h>`

---

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│         VulkanSCTriangleExample - Pattern Demo              │
├─────────────────────────────────────────────────────────────┤
│  Medical Compliance Framework (MduX)                        │
│  ├── ComplianceMetadata (IEC 62304 Class B)                │
│  ├── Medical Application Profiling                          │
│  └── Object Reservation Calculation                         │
├─────────────────────────────────────────────────────────────┤
│  Vulkan SC Pattern Infrastructure (MduX)                    │
│  ├── MemoryPoolManager - Static allocation                 │
│  ├── DeviceObjectManager - Device-lifetime tracking        │
│  └── ObjectReservationCalculator - Resource planning       │
├─────────────────────────────────────────────────────────────┤
│  Standard Vulkan 1.3 API (Development)                      │
│  ├── VK_KHR_swapchain (not in true SC)                     │
│  ├── Runtime pipeline creation (not in true SC)            │
│  └── Standard vulkan.h headers                              │
├─────────────────────────────────────────────────────────────┤
│  Future Migration → True Vulkan SC 1.0                      │
│  └── See docs/VulkanSC-vs-Vulkan.md                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 🚀 Building and Running

### Prerequisites
- Vulkan SDK 1.3+ (includes `glslangValidator`)
- CMake 4.0+
- C++23 compiler (MSVC 17.14+, GCC 15+, Clang 20+)
- Windows 10+ or Linux

### Build Instructions

```bash
# From MduX root directory
cd buildWindows  # or your build directory
cmake ..
cmake --build . --target VulkanSCTriangleExample

# Run the example
examples\VulkanSCTriangleExample.exe
```

### Expected Output

```
╔══════════════════════════════════════════════════╗
║  MduX Vulkan SC Medical Device Rendering Demo   ║
║  IEC 62304 Class B Compliance                    ║
╚══════════════════════════════════════════════════╝

Initializing window...
✓ Window created (800x600)

Initializing Vulkan SC...
  ✓ Vulkan instance created
  ✓ Window surface created
  ✓ Physical device: [Your GPU Name]
  ✓ Queue families: graphics=X, present=Y

  Creating Vulkan SC device with object reservations...
    - Graphics pipelines reserved: 6
    - Buffers reserved: 50
    - Memory reserved: XXX MB
  ✓ Logical device created
  ✓ Vulkan SC managers initialized
  ✓ Swapchain created (3 images)
  ✓ Image views created
  ✓ Render pass created
  ✓ Graphics pipeline created
  ✓ Framebuffers created
  ✓ Command pool created
  ✓ Command buffers allocated
  ✓ Synchronization objects created
✓ Vulkan SC initialized successfully

╔══════════════════════════════════════════════════╗
║  Medical Device Rendering Active                ║
║  Press ESC or close window to exit              ║
╚══════════════════════════════════════════════════╝

FPS: 60.0 | Memory: 0 KB | Objects: 0
```

You'll see a window with a **colored triangle**:
- **Red** vertex at top
- **Green** vertex at bottom-right
- **Blue** vertex at bottom-left
- Colors smoothly interpolated in between

---

## 🏥 Vulkan SC Infrastructure Usage

This example showcases the complete Vulkan SC infrastructure that makes medical device rendering safe and compliant.

### 1. **Medical Application Profiling**

**Location:** `VulkanSCTriangleExample.cpp:237-244`

```cpp
// Calculate object reservations for medical UI
MedicalApplicationProfile profile;
profile.maxConcurrentScreens = 1;
profile.maxUIElementsPerScreen = 50;
profile.maxTextureAtlases = 2;
profile.safetyMarginMultiplier = 2.0f;  // Class B safety
```

**What it does:**
- Defines the **maximum resource requirements** for your medical application
- `maxConcurrentScreens`: How many UI screens can be displayed simultaneously
- `maxUIElementsPerScreen`: How many buttons, labels, images per screen
- `maxTextureAtlases`: How many texture atlases for UI rendering
- `safetyMarginMultiplier`: Safety margin (2.0 = 100% extra capacity for Class B/C devices)

**Why it matters:**
- IEC 62304 requires **deterministic resource usage**
- Pre-calculating all resources prevents runtime failures
- Safety margins account for worst-case scenarios

---

### 2. **Object Reservation Calculator**

**Location:** `VulkanSCTriangleExample.cpp:245-246`

```cpp
auto objectReservations = ObjectReservationCalculator::calculate(profile);
auto memoryConfig = MemoryPoolCalculator::calculate(profile, physicalDevice);
```

**What it does:**
- **Analyzes** your medical application profile
- **Calculates** how many Vulkan objects you'll need:
  - Graphics pipelines
  - Buffers (vertex, index, uniform)
  - Images and textures
  - Descriptor sets
  - Command pools
  - Framebuffers
  - And ~30 other object types
- **Applies safety margins** based on IEC 62304 class

**Output Example:**
```
- Graphics pipelines reserved: 6
- Buffers reserved: 50
- Memory reserved: 287 MB
```

**Why it matters:**
- Vulkan SC requires **all object counts at device creation**
- No dynamic allocation at runtime (safety-critical requirement)
- Wrong estimates = application crashes or validation errors

**Implementation:** `src/vulkansc/DeviceObjectManager.cpp:728-778`

---

### 3. **Memory Pool Manager**

**Location:** `VulkanSCTriangleExample.cpp:284-285`

```cpp
memoryManager = make_unique<MemoryPoolManager>();
memoryManager->initialize(device, physicalDevice, memoryConfig);
```

**What it does:**
- **Reserves static memory pools** at device creation
- **Tracks all allocations** for audit trail (IEC 62304 requirement)
- **Prevents memory freeing** (Vulkan SC prohibits `vkFreeMemory()` at runtime)
- **Monitors capacity** to ensure you don't exceed reservations

**Key Constraint:**
```cpp
// In Vulkan SC, you CANNOT do this at runtime:
vkFreeMemory(device, memory, nullptr);  // ❌ FORBIDDEN!

// Instead, memory is marked as unused for tracking:
memoryManager->markUnused(memory);  // ✓ Tracking only
```

**Memory remains allocated until device destruction!**

**Memory Pool Structure:**
```cpp
struct MemoryPool {
    vector<VkDeviceMemory> allocations;  // All allocations (never freed!)
    VkDeviceSize totalReserved;          // Capacity reserved at creation
    VkDeviceSize totalAllocated;         // Current usage
    uint32_t memoryTypeIndex;            // GPU memory type (device-local, host-visible, etc.)
};
```

**Allocation Flow:**
```
1. Initialize pools with reserved capacity
   ↓
2. Allocate from pool (vkAllocateMemory)
   ↓
3. Track allocation in audit trail
   ↓
4. Memory persists until device destruction
   ↓
5. All memory freed at once during cleanup()
```

**Why it matters:**
- Medical devices must have **deterministic memory behavior**
- No fragmentation, no allocation failures at runtime
- Complete audit trail for regulatory compliance

**Implementation:** `src/vulkansc/MemoryPoolManager.cpp`

---

### 4. **Device Object Manager**

**Location:** `VulkanSCTriangleExample.cpp:287-288`

```cpp
objectManager = make_unique<DeviceObjectManager>();
objectManager->initialize(device, objectReservations);
```

**What it does:**
- Manages **device-lifetime objects** that cannot be destroyed:
  - `VkCommandPool` - Cannot call `vkDestroyCommandPool()` ❌
  - `VkDescriptorPool` - Cannot call `vkDestroyDescriptorPool()` ❌
  - `VkQueryPool` - Cannot call `vkDestroyQueryPool()` ❌
- Tracks **regular objects** that can be destroyed:
  - `VkBuffer`, `VkImage`, `VkImageView`, etc. ✅
- **Validates** you don't exceed object count reservations
- **Records** all object creation for audit trail

**Critical Vulkan SC Constraint:**

```cpp
// Device-lifetime objects - CANNOT be destroyed!
VkCommandPool pool = objectManager->createCommandPool(queueFamily);
// ❌ vkDestroyCommandPool(device, pool, nullptr);  // FORBIDDEN in Vulkan SC!

// These persist until device destruction
```

**Object Categories:**

**Device-Lifetime (Cannot Destroy):**
- Command pools
- Descriptor pools
- Query pools
- Swapchains (if using)

**Regular Objects (Can Destroy):**
- Buffers
- Images
- Image views
- Samplers
- Render passes
- Framebuffers
- Pipelines

**Object Tracking:**
```cpp
struct ObjectStatistics {
    // Device-lifetime (permanent)
    uint32_t commandPoolsCreated;
    uint32_t descriptorPoolsCreated;

    // Regular objects (current count)
    uint32_t buffersActive;
    uint32_t imagesActive;

    // Peak usage (for compliance reporting)
    uint32_t peakBuffers;
    uint32_t peakImages;
};
```

**Why it matters:**
- Vulkan SC enforces **object lifetime rules** for safety
- Prevents resource leaks in safety-critical systems
- Audit trail proves compliance with IEC 62304

**Implementation:** `src/vulkansc/DeviceObjectManager.cpp`

---

### 5. **Audit Trail and Compliance**

Both managers provide audit reporting:

```cpp
// Generate IEC 62304 compliance report
string memoryAudit = memoryManager->generateAuditReport();
string objectAudit = objectManager->generateAuditReport();
```

**Sample Audit Output:**
```json
{
  "memoryPoolManager": {
    "safetyClass": "Class B",
    "totalMemoryReserved": 301989888,
    "totalMemoryAllocated": 0,
    "utilizationPercentage": 0.0,
    "totalAllocations": 0,
    "peakAllocations": 0,
    "memoryPools": [
      {
        "memoryType": 0,
        "reserved": 241591910,
        "allocated": 0,
        "utilization": 0.0,
        "allocationCount": 0
      }
    ]
  }
}
```

**Regulatory Value:**
- Proves **worst-case resource usage**
- Demonstrates **deterministic behavior**
- Provides **traceability** for audits
- Satisfies IEC 62304 Section 5.5 (Software Unit Implementation and Verification)

---

## 🎓 Medical Device Patterns Demonstrated

### Pattern 1: **Pre-Reservation Before Use**

```cpp
// ❌ WRONG - Vulkan SC forbids this:
VkDevice device = createDevice();
VkBuffer buffer = createBuffer();  // No reservation!

// ✅ CORRECT - Pre-reserve, then create:
MedicalApplicationProfile profile;
profile.maxUIElementsPerScreen = 100;

auto reservations = ObjectReservationCalculator::calculate(profile);
VkDevice device = createDeviceWithReservations(reservations);

// Now buffer creation is guaranteed to succeed (or fail deterministically)
VkBuffer buffer = createBuffer();
```

---

### Pattern 2: **Static Memory, No Runtime Freeing**

```cpp
// ❌ WRONG - Dynamic allocation pattern:
VkDeviceMemory memory = allocateMemory(size);
// ... use memory ...
vkFreeMemory(device, memory, nullptr);  // FORBIDDEN in Vulkan SC!

// ✅ CORRECT - Static allocation pattern:
MemoryPoolManager poolManager;
poolManager.initialize(device, physicalDevice, config);

VkDeviceMemory memory = poolManager.allocate(size, memoryType);
// ... use memory ...
poolManager.markUnused(memory);  // Track only, don't free

// All memory freed at once during device destruction
poolManager.cleanup();  // Called in destructor
```

---

### Pattern 3: **Device-Lifetime Objects**

```cpp
// ❌ WRONG - Destroying device-lifetime objects:
VkCommandPool pool = createCommandPool();
// ... use pool ...
vkDestroyCommandPool(device, pool, nullptr);  // FORBIDDEN!

// ✅ CORRECT - Pools persist until device destruction:
DeviceObjectManager objectManager;
objectManager.initialize(device, reservations);

VkCommandPool pool = objectManager.createCommandPool(queueFamily);
// ... use pool forever ...

// Pools automatically cleaned up when device is destroyed
objectManager.cleanup();  // Called during device teardown
```

---

### Pattern 4: **Safety Margins**

```cpp
// ❌ WRONG - Exact capacity, no margin:
profile.maxUIElementsPerScreen = 100;  // Exactly 100 elements
profile.safetyMarginMultiplier = 1.0f;  // No margin!

// ✅ CORRECT - IEC 62304 Class B/C requires safety margins:
profile.maxUIElementsPerScreen = 100;
profile.safetyMarginMultiplier = 2.0f;  // 100% safety margin

// Calculator reserves capacity for 200 elements!
// Handles worst-case scenarios and prevents runtime failures
```

---

## 🔧 Shader Compilation Pipeline

This example uses **offline shader compilation**, a Vulkan SC requirement.

### Source Shaders

**`examples/shaders/triangle.vert`** - Vertex shader (positions and colors)
**`examples/shaders/triangle.frag`** - Fragment shader (pixel colors)

### CMake Build Process

```cmake
# Find Vulkan SDK shader compiler
find_program(GLSL_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/Bin)

# Compile shaders to SPIR-V bytecode
add_custom_command(
    OUTPUT triangle.vert.spv
    COMMAND ${GLSL_VALIDATOR} -V triangle.vert -o triangle.vert.spv
)

add_custom_command(
    OUTPUT triangle.frag.spv
    COMMAND ${GLSL_VALIDATOR} -V triangle.frag -o triangle.frag.spv
)
```

### Runtime Loading

```cpp
// Load pre-compiled SPIR-V bytecode
auto vertShaderCode = readFile("examples/shaders/triangle.vert.spv");
auto fragShaderCode = readFile("examples/shaders/triangle.frag.spv");

// Create shader modules
VkShaderModule vertModule = createShaderModule(vertShaderCode);
VkShaderModule fragModule = createShaderModule(fragShaderCode);
```

**Why offline compilation?**
- **Deterministic**: Same shaders always produce same bytecode
- **Fast**: No runtime compilation delays
- **Secure**: Shaders validated before deployment
- **Required**: Vulkan SC mandates offline shader compilation

---

## 📊 Resource Flow Diagram

```
Medical Application Profile
         ↓
    ┌────────────────────────┐
    │ Resource Calculators   │
    ├────────────────────────┤
    │ • Object Reservation   │
    │ • Memory Pool Sizing   │
    │ • Safety Margins       │
    └────────────────────────┘
         ↓
    ┌────────────────────────┐
    │ Vulkan SC Device       │
    │ Creation               │
    ├────────────────────────┤
    │ VkDeviceObjectReserv.. │
    └────────────────────────┘
         ↓
    ┌──────────────┬──────────────┐
    │              │              │
    ▼              ▼              ▼
┌─────────┐  ┌──────────┐  ┌─────────┐
│ Memory  │  │  Object  │  │ Command │
│ Pools   │  │ Tracking │  │  Pools  │
└─────────┘  └──────────┘  └─────────┘
    │              │              │
    └──────────────┴──────────────┘
                   ↓
         Rendering Application
```

---

## 🧪 Testing and Validation

Run the example and verify:

1. **No validation errors** (enable Vulkan validation layers for dev)
2. **Consistent frame rate** (~60 FPS)
3. **Zero memory allocations** after initialization
4. **Clean shutdown** with all resources freed

### Enable Vulkan Validation (Development Only)

In `createInstance()`, add:

```cpp
const vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
createInfo.ppEnabledLayerNames = validationLayers.data();
```

---

## 📚 Related Documentation

- **Vulkan SC Migration Plan**: `docs/Vulkan-SC-Only-Migration-Plan.md`
- **Memory Pool Manager**: `include/mdux/vulkansc/MemoryPoolManager.cppm`
- **Device Object Manager**: `include/mdux/vulkansc/DeviceObjectManager.cppm`
- **IEC 62304 Reference**: `docs/MduX-ISO-13485-AI-Reference.md`

---

## 🎯 Next Steps

### Extend This Example

1. **Add more UI elements** (buttons, text, medical images)
2. **Implement vertex buffers** (instead of hardcoded positions)
3. **Add texture mapping** (medical imaging display)
4. **Create multiple render passes** (multi-layer UI)
5. **Integrate medical data visualization**

### Modify the Triangle

Edit `examples/shaders/triangle.vert`:

```glsl
// Make it a square
vec2 positions[6] = vec2[](
    vec2(-0.5, -0.5),  // Triangle 1
    vec2(0.5, -0.5),
    vec2(-0.5, 0.5),
    vec2(0.5, -0.5),   // Triangle 2
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);
```

Then in `recordCommandBuffer()`:
```cpp
vkCmdDraw(commandBuffer, 6, 1, 0, 0);  // Draw 6 vertices (2 triangles)
```

---

## ⚠️ Important Notes for Medical Device Development

1. **Always use safety margins** (2.0× for Class B, 3.0× for Class C)
2. **Never exceed object reservations** (causes validation errors)
3. **Don't free memory at runtime** (Vulkan SC restriction)
4. **Keep audit trails** for regulatory compliance
5. **Test worst-case scenarios** (max UI elements, max textures)
6. **Validate deterministic behavior** (same input = same output)

---

## 🏆 Compliance Checklist

- ✅ IEC 62304 Section 5.3: Software Detailed Design
- ✅ IEC 62304 Section 5.5: Software Unit Implementation
- ✅ IEC 62304 Section 5.6: Software Integration and Testing
- ✅ ISO 14971: Risk Management (safety margins, deterministic behavior)
- ✅ ISO 13485: Quality Management (audit trails, traceability)
- ✅ Vulkan SC 1.0: Safety-Critical API Compliance

---

## 📞 Support

For questions about:
- **Vulkan SC**: See `docs/Vulkan-SC-Only-Migration-Plan.md`
- **Medical compliance**: See `docs/MduX-ISO-13485-AI-Reference.md`
- **Build issues**: Check CMakeLists.txt configuration
- **Shader errors**: Review `examples/shaders/` source files

---

**Congratulations!** You now understand how to build safety-critical medical device graphics applications with Vulkan SC! 🏥🎨✨
