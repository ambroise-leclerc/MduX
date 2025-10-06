# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Architecture Overview - PURE VULKAN COMPLEMENT LIBRARY

**Library Type:** Pure Vulkan complement library for medical device UI - NO windowing dependencies

**Core Design Principles:**
- **Pure Vulkan Integration**: Complements existing Vulkan applications without creating windows
- **Medical Device Compliance**: IEC 62304, IEC 62366 regulatory compliance built-in
- **C++23 Modules Architecture**: Ultra-clean module interface for modern development
- **Zero Windowing Dependencies**: Users provide their own VkDevice, VkRenderPass, VkCommandBuffer
- **Safety-Critical Design**: Deterministic performance and comprehensive validation

## Build System & Development Commands

**Prerequisites:**
- Vulkan SDK 1.3+ must be installed and findable by CMake
- CMake 4.0+ with C++23 modules support
- C++23 compliant compiler with modules support:
  - **MSVC 17.14+** (Visual Studio 2022 version 17.10+)
  - **GCC 15+** 
  - **Clang 20+**
- Windows 10+ or Linux only (macOS support discontinued)

**Modern C++23 Modules Build:**
```bash
# Configure with CMake (out-of-source build required)
cmake -B build -S .

# Build all targets
cmake --build build

# Build specific targets
cmake --build build --target MduX
cmake --build build --target MedicalUiExample  # NEW: Pure Vulkan integration example
cmake --build build --target MduXExample       # LEGACY: Will be deprecated
cmake --build build --target unit_tests
```

**Testing:**
```bash
# Run all tests
ctest --test-dir build

# Run specific test suites
./build/unit_tests
./build/compliance_tests
```

**Examples:**
```bash
# NEW: Pure Vulkan medical UI integration
./build/examples/MedicalUiExample medical_interface.html

# LEGACY: Window-based example (deprecated)
./build/examples/MduXExample examples/ui.html --hot
```

**Documentation:**
```bash
# Generate Doxygen documentation
cmake --build build --target doxygen-docs
```

**Build Options:**
- `MDUX_BUILD_EXAMPLES=ON/OFF` - Build example applications
- `MDUX_BUILD_TESTS=ON/OFF` - Build test suites  
- `MDUX_BUILD_DOCS=ON/OFF` - Generate documentation
- `MDUX_ENABLE_REGULATORY_DOCS=ON/OFF` - Enable regulatory compliance documentation

## NEW ARCHITECTURE - Pure Vulkan Integration

**Key Components:**

1. **MedicalUiRenderer** (Core Integration Class)
   - Integrates with existing Vulkan applications
   - Requires user-provided VkDevice, VkRenderPass, VkCommandBuffer
   - Medical device compliance validation built-in
   - Hot-reload support for development workflows

2. **Medical Device Features**
   - Built-in compliance metadata (IEC 62304, IEC 62366)
   - Audit trail and traceability support
   - Regulatory documentation generation
   - Safety-critical error handling patterns
   - Render statistics for compliance monitoring

3. **UI Content System**
   - HTML/CSS medical interface definitions
   - File watching for hot-reload during development
   - Medical device specific validation rules
   - Content versioning for regulatory traceability

**Integration Pattern:**
```cpp
// User's existing Vulkan setup
VkDevice device = /* user's device */;
VkRenderPass renderPass = /* user's render pass */;

// Setup medical compliance
mdux::ComplianceMetadata compliance;
compliance.deviceClass = "Class B";
compliance.standardsCompliance = "IEC 62304, IEC 62366";

// Initialize MduX
mdux::initialize(compliance);

// Create medical UI renderer
mdux::VulkanContext context{device, physicalDevice, commandBuffer, renderPass, /*...*/};
mdux::MedicalUiConfig config{uiPath, compliance, /*...*/};
mdux::MedicalUiRenderer renderer(context, config);

// In render loop
renderer.render(context);
```

**Dependencies:**
- **Required:** Vulkan 1.3+, Threads  
- **Removed:** GLFW (no windowing dependencies)
- **Build:** CMake 4.0+, C++23 compiler with modules support, Vulkan SDK

**Standards Compliance:**
- C++23 standard throughout
- C++ Core Guidelines coding standards  
- Medical device regulatory standards (IEC 62304, IEC 62366, FDA 21 CFR Part 820)
- Safety-critical design patterns

## Project Structure

```
include/mdux/           # Module interface and supporting headers
examples/               # Ultra-sleek C++23 modules examples
tests/                  # Unit and compliance test suites  
docs/adr/              # Architectural Decision Records
cmake/                 # CMake configuration modules
build/                 # Build output directory (out-of-source)
```

**Critical Files:**
- `include/mdux/mdux.cppm` - Ultra-clean C++23 module interface
- `CMakeLists.txt` - Sleek modules-only build configuration
- `examples/MduXExample.cpp` - Comprehensive C++23 modules demonstration
- `examples/UiIntegrationExample.cpp` - UI integration with modules
- `docs/adr/ADR-001-multiplatform-graphics-framework.md` - Graphics framework decision (revised for Vulkan)
- `docs/adr/ADR-002-testing-framework-selection.md` - BDD testing framework selection
- `CONTRIBUTING.md` - Development standards and documentation guidelines

## Development Guidelines

**Code Style:**
- Follow C++ Core Guidelines
- Classes: `UpperCamelCase`, Functions: `lowerCamelCase`, Variables: `lowerCamelCase`
- 4-space indentation, no tabs
- Doxygen documentation required for public APIs

**Modules Architecture:**
- Single `mdux.cppm` module interface with ultra-clean exports
- Categorized export sections for maximum readability
- No fallback header-only support - modules-only for modern development
- Traditional standard library includes until compiler support matures

**Medical Device Requirements:**
- All changes must maintain regulatory compliance
- Traceability required for requirements → implementation
- Version information embedded in library
- Safety-critical error handling patterns

**Testing Requirements:**
- Unit tests for all public APIs
- Compliance tests for regulatory requirements
- Cross-platform validation on Windows and Linux only
- Example applications serve as integration tests

**Graphics Programming Notes:**
- Vulkan 1.3 API only (no legacy graphics APIs)
- Platform-specific surface creation handled automatically
- GLFW integration for windowing and surface management
- Vulkan validation layers enabled in debug builds
- Deterministic resource management for medical device compliance