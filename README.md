

# MduX: Medical Device Software Manufacturer Framework

> **⚠️ EXPERIMENTAL PROJECT WARNING**
> 
> This project is an **experimental early evaluation** of C++23 modules feasibility for cross-platform development with rich dependencies (Vulkan graphics, medical device compliance frameworks). It represents an attempt to leverage **C++23 and emerging C++26 safety evolutions** for medical device software development.
> 
> **Current Status:**
> - C++23 modules support requires cutting-edge toolchains (GCC 15+, MSVC 17.14+, Clang 20+)
> - CMake 4.x+ experimental support for `import std;` 
> - Cross-platform compatibility still evolving
> - Medical device compliance framework is conceptual/educational
> - Exploring modern C++ safety features for medical device reliability
> 
> **Not recommended for production use.** This project serves as a technical proof-of-concept for modern C++ module systems and safety evolutions in complex, regulated software environments.

## Introduction

MduX represents a comprehensive **Medical Device Software Manufacturer** framework built on modern C++23 modules and Vulkan graphics API. As a complete regulatory compliance architecture, MduX integrates three foundational medical device standards:

- **ISO 14971:2019** - Risk Management Framework for systematic hazard identification and risk control
- **ISO 13485:2016** - Quality Management System ensuring consistent medical device development
- **IEC 62304:2006** - Software Lifecycle Framework with safety classification and verification processes

The framework operates as a unified **Medical Device Software Manufacturer** platform, providing not just a UI library, but a complete regulatory infrastructure for developing Class A, B, and C medical device software with full traceability, risk management, and quality assurance.

### Core Medical Device Capabilities

**Regulatory Compliance Architecture:**
- Integrated risk management with hazard identification and control measures
- Quality management system with design controls and CAPA processes  
- Software lifecycle management with safety classification (Class A/B/C)
- Post-market surveillance and incident reporting systems
- Complete regulatory documentation generation (FDA 21 CFR Part 820, EU MDR 2017/745)

**Technical Excellence:**
- C++23 modules-based architecture with deterministic performance
- Vulkan-powered graphics for real-time medical applications
- Built-in compliance metadata and audit trail generation
- Comprehensive traceability from requirements to validation
- Cross-platform support (Windows and Linux medical device platforms)

**Safety-Critical Design:**
- IEC 62304 software safety classification integration
- Architectural risk controls (RAII patterns, memory safety)
- Real-time performance guarantees for critical medical functions
- Comprehensive validation and verification frameworks

MduX enables medical device manufacturers to accelerate development while maintaining the highest standards of patient safety, regulatory compliance, and technical excellence.

---

## Medical Device Compliance Architecture

### Integrated Framework Overview

The sections below describe the regulatory framing this repository is *organised around*. They
describe intent and documentation structure, not implemented capability - the code that exists is:

```cpp
// The modules that exist today. There is no mdux::risk, mdux::qms or mdux::lifecycle namespace,
// and no mdux/compliance/ headers - the regulatory work in this repository is documentation and
// governance records, not code. See "Implementation Status" below.
import mdux;                  // version, compliance metadata, initialize/shutdown
import mdux.core.units;       // Px, Rect, ColorRgba8, Extent2D
import mdux.draw;             // fixed-budget DrawList - governed, names no Vulkan type
import mdux.render.vulkan;    // UiRenderer - adapter zone, records into your command buffer
```

#### ISO 14971:2019 - Risk Management Framework
- **Risk Analysis Engine**: Systematic hazard identification and risk assessment
- **Risk Control Architecture**: Inherent safety, protective measures, information for safety
- **Residual Risk Evaluation**: Automated risk-benefit analysis and acceptability criteria
- **Post-Market Risk Management**: Continuous monitoring and risk profile updates

#### ISO 13485:2016 - Quality Management System  
- **Design Control Stages**: Requirements → Design → Verification → Validation → Transfer
- **CAPA System**: Corrective and Preventive Action management with root cause analysis
- **Document Control**: Version management, change control, and regulatory submission support
- **Management Review**: Executive dashboards and quality metrics reporting

#### IEC 62304:2006 - Software Lifecycle Framework
- **Safety Classification**: Class A (non-injury), Class B (non-fatal injury), Class C (fatal injury)
- **Development Planning**: Risk-based development with safety requirements integration
- **Architecture Design**: Safety-critical component identification and segregation
- **Verification & Validation**: Class-specific testing requirements and coverage metrics

### Medical Device Software Manufacturer Role

As a **Medical Device Software Manufacturer**, MduX provides:

- **Regulatory Infrastructure**: Complete compliance framework with built-in documentation
- **Safety Engineering**: Risk-based design with architectural safety controls
- **Quality Assurance**: Integrated QMS with design controls and validation processes
- **Post-Market Operations**: Incident reporting, adverse event management, and continuous improvement

---

## Implementation Status

| Area | Status | What is actually there |
|------|--------|------------------------|
| **Governed core** (`MduXCore`, no Vulkan) | | |
| `mdux.core.result`, `mdux.core.units` | Implemented | `Result` over `std::expected`; `Px`, `Rect`, `ColorRgba8`, `Extent2D` |
| `mdux.draw` | Implemented | 24-byte `UiVertex`, fixed-budget `DrawList`, explicit refusal on overflow |
| `mdux.shader.schema` | Implemented | canonical shader package types; names no Vulkan type |
| `mdux.evidence.*` | Implemented | SHA-256, canonical JSON, `BakeReport` |
| `mdux.governance*` | Implemented | governance records and compliance program types |
| **Adapter zone** (Vulkan) | | |
| `mdux.render.vulkan` | Implemented | pipeline built from a baked package, fixed-budget `record()` |
| `mdux.render.offscreen` | Implemented | headless target and CPU readback, used by the pixel tests |
| `mdux.vulkansc.*` | Partial | memory-pool and device-object patterns; not true Vulkan SC |
| **Host tools** (never linked into a device target) | | |
| `mdux-shaderbake`, `mdux-shaderemit` | Implemented | SPIR-V reflection, byte-verified packages, generated C++ |
| `mdux-docs-lint`, `mdux-evidence-lint` | Implemented | run in CI |
| **Regulatory material** | | |
| Standards corpus under `docs/` | Documentation only | structured notes and indexes; establishes no compliance |
| Software Development File | Documentation only | templates and records under `software_development_file/` |
| Risk management, QMS, lifecycle *code* | **Not started** | no `mdux::risk`, `mdux::qms` or `mdux::lifecycle` exists |
| **Not yet started** | | |
| `.medui` compiler, text and glyph rendering, ML inference | Planned | see issues #15, #17, #18 |

This repository is experimental. It establishes no certification, validation, production readiness
or regulatory compliance, and nothing in it has been assessed by a notified body. The documentation
under `docs/` records how such work would be organised; it is not evidence that it has been done.


---

## Framework Integration and Usage

### Building and recording a frame

Every symbol below exists. `mdux::initialize()` takes no arguments; there is no
`MedicalDeviceContext`, and a module is consumed with `import`, not `#include` of a `.cppm`.

```cpp
import std;
import mdux;
import mdux.core.units;
import mdux.draw;
import mdux.render.vulkan;

mdux::initialize();

// Build a frame. Governed: no Vulkan types, no allocation, storage supplied by the caller.
static std::array<mdux::draw::UiVertex, 64> vertices;
static std::array<mdux::draw::Index, 96> indices;
static std::array<mdux::draw::DrawCommand, 8> commands;
constexpr mdux::draw::DrawBudget budget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8};

auto list = mdux::draw::DrawList::create(vertices, indices, commands, budget);
if (!list.has_value()) {
    return handleError(mdux::draw::describe(list.error()));
}

// Every add* either records the primitive completely or records nothing and returns an error.
// A frame that does not fit its budget is refused, never truncated.
constexpr mdux::core::ColorRgba8 statusGreen{.r = 60, .g = 107, .b = 44, .a = 255};
if (auto added = list->addSolidRect({.x = 16, .y = 64, .width = 120, .height = 24}, statusGreen);
    !added.has_value()) {
    return handleError(mdux::draw::describe(added.error()));
}

// Record it. Adapter zone: the caller owns the device, render pass and command buffer.
renderer.record(commandBuffer, *list);
```

See `examples/SimpleMedicalUiExample.cpp` for the complete version of the governed half, which
needs no device and no window, and `examples/VulkanSCTriangleExample.cpp` for the device half.

### What is not here

Earlier revisions of this file showed `mdux::risk::RiskAnalysis`, `mdux::qms::DesignControl`,
`mdux::lifecycle::SoftwareItem` and `mdux::compliance::DocumentationGenerator`. **None of those
types, namespaces or headers exist**, and no code in this repository generates a Design History
File, a Risk Management File or an audit trail. The regulatory material here is documentation and
governance records under `docs/` and `software_development_file/`.

---

## Regulatory Standards Compliance

## Implementation Plan

### 1. Core Library Structure
- Modular C++23 modules-based design
- Strict adherence to C++23 standards with modules support
- Clear separation of UI components, utilities, and platform abstractions

### 2. Dependency Management & Version Tracing
- Built-in mechanisms for tracking third-party dependencies
- Version metadata embedded in the library
- Automated generation of dependency and version reports

### 3. Documentation Capabilities
- Doxygen-compatible comments for all public APIs
- Automated documentation generation scripts
- Documentation templates for regulatory compliance (class B & C)
- Examples and usage guides

### 4. Regulatory Compliance
- Documentation and code comments tailored for class B and class C medical devices
- Traceability matrix linking requirements to implementation
- Guidelines for risk management and mitigation

### 5. UI Components
- Core widgets: buttons, sliders, displays, input fields
- Medical device-specific controls (alarms, status indicators, etc.)
- Theming and accessibility support

### 6. Testing Framework
- C++23 modules-based UI testing utilities
- Screen capture and image verification tools
- Automated test runner for CI integration
- Example test cases for all UI components

### 7. Build & Integration
- CMake integration for easy inclusion in projects
- C++23 modules distribution
- Platform abstraction for Windows, Linux, and embedded systems

### 8. Future Extensions
- Support for additional UI controls
- Advanced graphics and animations
- Integration with device communication protocols

---

## Getting Started

### Medical Device Software Development Prerequisites

**Regulatory Preparation:**
- Understanding of ISO 14971 (Risk Management), ISO 13485 (Quality Management), IEC 62304 (Software Lifecycle)
- Medical device regulatory strategy and compliance planning
- Risk management and hazard analysis training
- Quality management system processes and documentation

**Technical Prerequisites:**
- **C++23 compatible compiler** with modules support:
  - **MSVC 17.14+** (Visual Studio 2022 version 17.10+)
  - **GCC 15+** 
  - **Clang 20+**
- **Vulkan SDK 1.3+** installed and findable by CMake
- **CMake 4.0+** with C++23 modules support  
- **Medical Device Platform Support:** Windows 10+ or Linux (Ubuntu 20.04+, RHEL 8+)

**Medical Device Development Environment:**
- Document management system for regulatory documentation
- Version control system with audit trail capabilities
- Test management and validation tracking system
- Risk management database and traceability tools

#### Linux Installation
```bash
# Ubuntu/Debian - Install Vulkan development tools
sudo apt update
sudo apt install vulkan-tools libvulkan-dev glslc glslang-tools

# Install Vulkan SDK from LunarG (recommended for development)
wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-$(lsb_release -cs).list \
  https://packages.lunarg.com/vulkan/$(lsb_release -cs)/lunarg-vulkan-$(lsb_release -cs).list
sudo apt update
sudo apt install vulkan-sdk

# Verify Vulkan installation
vulkaninfo --summary
```

#### Windows Installation
1. Download and install [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) from LunarG
2. Ensure the SDK is added to your system PATH
3. Verify installation: `vulkaninfo.exe` in Command Prompt

### Platform Support
- **Windows 10/11** - Full Vulkan support
- **Linux** - X11/Wayland with Vulkan support
- **MacOS** - MoltenVK + GCC15 will be evaluated for future support

### Quick Start for Medical Device Development

**Medical Device Software Manufacturer Setup:**

1. **Regulatory Planning Phase**
   ```bash
   # Clone the medical device framework
   git clone https://github.com/your-org/MduX.git
   cd MduX
   
   # Review the regulatory corpus - one directory per standard, clause by clause
   ls docs/iec62304/ docs/iso13485/ docs/iso14971/ docs/iec62366/ docs/iec81001/

   # Each has a per-clause index that points at the module covering it
   cat docs/iec62304/AI-Reference.md

   # ...and the scope limits this project does and does not claim
   cat docs/regulatory-compliance.md
   ```

2. **Development Environment Setup**
   ```bash
   # Ensure Vulkan SDK is installed and findable by CMake
   vulkaninfo --summary
   
   # MDUX_ENABLE_REGULATORY_DOCS selects the Doxygen theme; it does not turn on any
   # compliance behaviour, because there is none to turn on.
   cmake -B build -S . -DMDUX_ENABLE_REGULATORY_DOCS=ON
   cmake --build build
   
   # Run compliance validation
   ./build/compliance_tests
   ```

3. **Using the library**
   ```cpp
   import mdux;

   // Takes no arguments. The compliance metadata below is a constant describing what this
   // build declares, not a context you populate - there is no MedicalDeviceContext.
   if (!mdux::initialize()) {
       return 1;
   }
   std::println("{} - {}", mdux::Version::getString(), mdux::Compliance::standards);
   ```

4. **Development Workflow**
   - Read `AGENTS.md` and `CONTRIBUTING.md` before the first change
   - The trust zones in ADR-004 are enforced at configure time; a governed module that names a
     Vulkan type fails the build rather than review
   - Committed artifacts under `generated/` are byte-compared by `ctest -L evidence`
   - Regulatory documentation is written and reviewed by hand; nothing generates it

---

## Contributing
- Follow the coding and documentation standards
- Submit tests and documentation with all new features
- Ensure regulatory compliance for all contributions

---

## License
To be determined based on regulatory requirements and intended distribution.

---

## Contact
For questions or contributions, please open an issue or contact the maintainers.
