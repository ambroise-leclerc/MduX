

# MduX: Modern C++23 Vulkan-Based UI Library for Medical Devices


## Introduction
MduX is a modern, header-only C++23 user interface library purpose-built for medical device software using Vulkan graphics API. The name "MduX" stands for "Medical Device User eXperience," reflecting its mission to deliver exceptional, safe, and compliant user interfaces for medical technology.

MduX is designed to meet the highest standards of safety, usability, and regulatory compliance, empowering developers to create robust, maintainable, and visually consistent user interfaces for class B and class C medical devices with deterministic performance characteristics.

With a focus on modularity, traceability, and comprehensive documentation, MduX streamlines the development process while supporting the rigorous requirements of IEC 62304 (software lifecycle), IEC 62366 (usability engineering), and cybersecurity best practices. The library is engineered for reliability, testability, and ease of integration into Windows and Linux medical device platforms.

Key features include:
- Header-only, Vulkan-based architecture for future-proof graphics
- Deterministic performance for real-time medical applications
- Complete Doxygen documentation and regulatory traceability
- Built-in version and dependency management
- Cross-platform support (Windows and Linux only)
- Designed for safety-critical and regulated environments

MduX is the foundation for next-generation medical device interfaces, enabling innovation while ensuring compliance and patient safety.

---


## Implementation Status

| Feature/Module                | Status      | Notes |
|-------------------------------|-------------|-------|
| Core Library Structure        | Started     |       |
| Dependency Management         | Started     |       |
| Documentation Capabilities    | Started     |       |
| Regulatory Compliance         | Not started |       |
| UI Components                 | Not started |       |
| Testing Framework             | Not started |       |
| Build & Integration           | Not started |       |
| Future Extensions             | Not started |       |

Update this table as features are implemented.

---

## Regulatory Standards

### IEC 62304: Software Lifecycle Processes
- All development, verification, and maintenance activities will be documented to meet IEC 62304 requirements.
- Traceability matrix and risk management documentation will be maintained in the `regulatory/` folder.

### IEC 62366: Usability Engineering
- Usability engineering files, including user interface risk analysis and mitigation, will be provided in the `regulatory/` folder.
- UI components will be designed and tested to support safe and effective use.

### Cybersecurity
- The `regulatory/` folder will include documentation and processes for cybersecurity risk management, threat modeling, and mitigation strategies.
- All dependencies and code changes will be tracked for security impact.

---

## Implementation Plan

### 1. Core Library Structure
- Modular header-only design
- Strict adherence to C++23 standards
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
- Header-only UI testing utilities
- Screen capture and image verification tools
- Automated test runner for CI integration
- Example test cases for all UI components

### 7. Build & Integration
- CMake integration for easy inclusion in projects
- Single-header distribution option
- Platform abstraction for Windows, Linux, and embedded systems

### 8. Future Extensions
- Support for additional UI controls
- Advanced graphics and animations
- Integration with device communication protocols

---

## Getting Started

### Prerequisites
- C++23 compatible compiler (GCC 12+, Clang 15+, MSVC 2022+)
- Vulkan SDK 1.3+ installed
- CMake 4.0+
- Windows 10+ or Linux (Ubuntu 20.04+, RHEL 8+)

### Platform Support
- ✅ **Windows 10/11** - Full Vulkan support
- ✅ **Linux** - X11/Wayland with Vulkan support
- ❌ **macOS** - Support discontinued (see ADR-001)

### Quick Start
1. Clone the repository
2. Ensure Vulkan SDK is installed and findable by CMake
3. Include the main header in your project
4. Link against Vulkan and optionally GLFW for windowing
5. Follow the documentation for usage and compliance

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
