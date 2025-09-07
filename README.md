

# MduX: Medical Device Software Manufacturer Framework

> **⚠️ EXPERIMENTAL PROJECT WARNING**
> 
> This project is an **experimental early evaluation** of C++23 modules feasibility for cross-platform development with rich dependencies (Vulkan graphics, medical device compliance frameworks). 
> 
> **Current Status:**
> - C++23 modules support requires cutting-edge toolchains (GCC 15+, MSVC 17.14+, Clang 20+)
> - CMake 4.x+ experimental support for `import std;` 
> - Cross-platform compatibility still evolving
> - Medical device compliance framework is conceptual/educational
> 
> **Not recommended for production use.** This project serves as a technical proof-of-concept for modern C++ module systems in complex, regulated software environments.

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

MduX operates as a **Medical Device Software Manufacturer** with three interconnected compliance frameworks:

```cpp
// Core compliance integration
#include "mdux/compliance/RiskManagement.cppm"     // ISO 14971
#include "mdux/compliance/QualityManagement.cppm"  // ISO 13485  
#include "mdux/compliance/SoftwareLifecycle.cppm"  // IEC 62304

namespace mdux {
    struct MedicalDeviceContext {
        risk::RiskManagementSystem riskSystem;
        qms::QualityManagementSystem qualitySystem;
        lifecycle::SoftwareLifecycleFramework lifecycleFramework;
        ComplianceMetadata metadata;
    };
}
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

| Framework/Module              | Status      | Regulatory Standard | Safety Class | Notes |
|-------------------------------|-------------|--------------------|--------------| ------|
| **Medical Device Compliance** |             |                    |              |       |
| Risk Management System        | Completed   | ISO 14971:2019     | A/B/C        | Risk analysis, hazard ID, controls |
| Quality Management System     | Completed   | ISO 13485:2016     | A/B/C        | Design controls, CAPA, documentation |
| Software Lifecycle Framework  | Completed   | IEC 62304:2006     | A/B/C        | Development planning, safety classification |
| **Core Technical Framework**  |             |                    |              |       |
| C++23 Modules Architecture    | Completed   | -                  | A/B/C        | Modern module system with compliance |
| Vulkan Graphics Integration   | Completed   | -                  | A/B/C        | Deterministic rendering performance |
| Medical Compliance Metadata   | Completed   | ISO 14971/13485    | A/B/C        | Audit trails, traceability |
| **Regulatory Documentation**  |             |                    |              |       |
| Design History File (DHF)     | Completed   | ISO 13485          | B/C          | Complete development documentation |
| Risk Management File (RMF)    | Completed   | ISO 14971          | A/B/C        | Risk analysis and control records |
| Software Documentation        | Completed   | IEC 62304          | A/B/C        | Lifecycle documentation |
| **Development Infrastructure** |            |                    |              |       |
| UI Components Library         | Started     | IEC 62366          | A/B/C        | Medical device specific controls |
| Testing & Validation Framework| Started     | IEC 62304          | A/B/C        | Class-specific test requirements |
| Build & Integration Tools     | Started     | -                  | A/B/C        | CMake with compliance validation |
| Post-Market Surveillance      | Started     | ISO 14971          | B/C          | Incident reporting and monitoring |

Update this table as features are implemented.

---

## Framework Integration and Usage

### Basic Medical Device Integration

```cpp
#include "mdux/mdux.cppm"
#include "mdux/compliance/MedicalDevice.cppm"

// Initialize medical device context
mdux::MedicalDeviceContext context;
context.metadata.deviceClass = "Class B";
context.metadata.standardsCompliance = "ISO 14971, ISO 13485, IEC 62304";
context.metadata.manufacturerId = "YOUR_MANUFACTURER_ID";
context.metadata.deviceId = "YOUR_DEVICE_ID";

// Initialize compliance systems
mdux::initialize(context);

// Create UI with compliance integration
mdux::VulkanContext vulkanContext{device, physicalDevice, commandBuffer, renderPass};
mdux::MedicalUiConfig uiConfig{uiPath, context.metadata};
mdux::MedicalUiRenderer renderer(vulkanContext, uiConfig);

// Render with automatic compliance logging
renderer.render(vulkanContext);
```

### Risk Management Integration (ISO 14971)

```cpp
// Risk assessment and control
mdux::risk::RiskAnalysis riskAnalysis;
riskAnalysis.addHazard("UI_FREEZE", mdux::risk::SeverityLevel::Major, 
                      mdux::risk::ProbabilityLevel::Remote);
riskAnalysis.addRiskControl("WATCHDOG_TIMER", 
                           mdux::risk::ControlType::ProtectiveMeasure);

// Integration with medical device context
context.riskSystem.registerRiskAnalysis(riskAnalysis);
```

### Quality Management Integration (ISO 13485)

```cpp
// Design control and documentation
mdux::qms::DesignControl designControl;
designControl.setDesignInput("User interface requirements specification");
designControl.setDesignOutput("MduX medical UI implementation");
designControl.addVerificationRecord("UI component unit tests passed");
designControl.addValidationRecord("Clinical usability validation completed");

context.qualitySystem.registerDesignControl(designControl);
```

### Software Lifecycle Integration (IEC 62304)

```cpp
// Software safety classification
mdux::lifecycle::SoftwareItem softwareItem;
softwareItem.setName("Medical UI Renderer");
softwareItem.setSafetyClassification(mdux::lifecycle::SafetyClass::ClassB);
softwareItem.addRequirement("REQ-UI-001", "Display patient data accurately");

// Integration with validation framework
context.lifecycleFramework.registerSoftwareItem(softwareItem);
```

### Regulatory Documentation Generation

```cpp
// Generate compliance documentation
mdux::compliance::DocumentationGenerator docGen(context);

// Generate regulatory submission documents
docGen.generateDesignHistoryFile("docs/dhf/");
docGen.generateRiskManagementFile("docs/rmf/");
docGen.generateSoftwareDocumentation("docs/lifecycle/");

// Generate audit trails
docGen.generateAuditTrail("audit/audit_trail.json");
```

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
   
   # Review regulatory documentation
   cat docs/MduX_ISO-14971-Risk-Management-Framework.md
   cat docs/MduX_ISO-13485-Quality-Management-Framework.md  
   cat docs/MduX_IEC-62304-Software-Lifecycle-Framework.md
   ```

2. **Development Environment Setup**
   ```bash
   # Ensure Vulkan SDK is installed and findable by CMake
   vulkaninfo --summary
   
   # Build with medical device compliance
   cmake -B build -S . -DMDUX_ENABLE_REGULATORY_DOCS=ON
   cmake --build build
   
   # Run compliance validation
   ./build/compliance_tests
   ```

3. **Medical Device Integration**
   ```cpp
   // Initialize medical device framework
   #include "mdux/mdux.cppm"
   #include "mdux/compliance/MedicalDevice.cppm"
   
   mdux::MedicalDeviceContext context;
   context.metadata.deviceClass = "Class B";  // or Class A/C
   context.metadata.standardsCompliance = "ISO 14971, ISO 13485, IEC 62304";
   
   mdux::initialize(context);
   ```

4. **Development Workflow**
   - Follow IEC 62304 software lifecycle processes
   - Integrate with ISO 13485 design controls
   - Maintain ISO 14971 risk management throughout development
   - Generate regulatory documentation automatically

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
