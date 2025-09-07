# ADR-001: Graphics Framework Selection for Medical Device Software

## Status
Revised (v2.0 - 2025-07-25)

## Context
MduX is a modern C++23 header-only UI library designed specifically for medical device software. The library needs to support Windows and Linux platforms while maintaining high performance, reliability, and regulatory compliance for Class B and Class C medical devices.

The choice of graphics framework is critical as it affects:
- Cross-platform compatibility and deployment
- Performance characteristics for real-time medical applications
- Long-term maintenance and support (framework longevity)
- Regulatory compliance and validation requirements
- Integration complexity with existing medical device software stacks

Key requirements:
- Support for Windows and Linux desktop platforms only
- Potential future support for embedded medical device platforms
- Hardware acceleration capabilities with deterministic performance
- Modern, future-proof APIs with long-term industry support
- Low-level control for medical device real-time requirements
- Compatibility with medical device software validation processes

## Medical Device Considerations

### IEC 62304 Implications (Software Lifecycle)
- **Traceability**: The chosen framework must have clear version control and change documentation
- **Risk Management**: Graphics failures could impact patient safety in medical devices
- **Validation**: The framework must be validatable for medical device software
- **Maintenance**: Long-term support and security updates are critical

### IEC 62366 Implications (Usability Engineering)
- **Performance**: Consistent, responsive UI performance across platforms
- **Visual Consistency**: Identical rendering behavior across target platforms
- **Accessibility**: Support for accessibility features required in medical devices
- **Error Handling**: Graceful degradation when graphics hardware is unavailable

### Cybersecurity Impact
- **Attack Surface**: Graphics drivers and frameworks can be attack vectors
- **Supply Chain**: Third-party graphics libraries must be from trusted sources
- **Updates**: Security patches must be manageable within medical device lifecycles

### Risk Management Considerations
- **Hardware Dependency**: Reduced dependency on specific graphics hardware
- **Driver Stability**: Minimize impact of graphics driver issues
- **Fallback Mechanisms**: Software rendering capabilities for critical functions

## Decision
**Selected Framework: Vulkan API**

Primary choice: **Vulkan 1.3** with modern C++ bindings (vulkan.hpp)
Target platforms: **Windows and Linux only** (macOS support discontinued)

## Rationale for Architecture Change

### Vulkan Selection Drivers
1. **Future-Proofing**: Vulkan represents the long-term future of graphics APIs
2. **Performance Control**: Explicit control over GPU resources for deterministic medical device performance
3. **Industry Direction**: Major GPU vendors focusing development efforts on Vulkan
4. **Medical Device Benefits**: Predictable performance characteristics critical for real-time medical applications
5. **Reduced Platform Complexity**: Focusing on Windows/Linux simplifies validation and maintenance

### macOS Platform Removal Rationale
1. **Vulkan Complexity**: MoltenVK translation layer adds validation complexity for medical devices
2. **Apple's Direction**: Apple's focus on Metal conflicts with cross-platform medical device strategies
3. **Market Focus**: Medical device market primarily Windows/Linux-based embedded systems
4. **Maintenance Burden**: Reducing platform matrix complexity for better quality assurance

## Alternatives Considered

### 1. Vulkan 1.3 ✅ (Selected)
**Pros:**
- Modern, future-proof API with explicit resource control
- Excellent multi-threading support for complex medical UIs
- Predictable performance characteristics (critical for medical devices)
- Strong industry consortium backing with long-term support commitment
- Lower-level control enabling deterministic real-time behavior
- Cross-platform support on Windows and Linux without translation layers
- Better suited for medical device validation processes (no legacy baggage)
- Modern C++ bindings (vulkan.hpp) for type safety

**Cons:**
- Higher initial complexity and steeper learning curve
- More boilerplate code required for basic operations
- Requires team training and expertise development

### 2. OpenGL 4.6 Core Profile (Previous Choice)
**Pros:**
- Mature, stable API with extensive documentation
- Lower complexity barrier for development team
- Proven in existing medical device applications

**Cons:**
- Legacy API design with deprecated concepts
- Less predictable performance characteristics
- Uncertain long-term future (Khronos focus shifting to Vulkan)
- macOS deprecation creates platform instability
- Limited control over GPU resources for real-time requirements

### 3. DirectX 12 (Windows-only)
**Pros:**
- Excellent Windows performance
- Strong Microsoft support
- Modern low-level API design

**Cons:**
- Windows-only (fails cross-platform requirement)
- High complexity similar to Vulkan
- Not suitable for Linux-based medical devices
- Vendor lock-in concerns for medical device longevity

### 4. Software Rendering (CPU-based)
**Pros:**
- No hardware dependencies
- Completely predictable behavior
- Easier validation for medical devices

**Cons:**
- Poor performance for modern UI requirements
- Limited visual capabilities
- Not suitable for real-time medical applications
- Cannot meet modern medical device UI expectations

## Consequences

### Positive
- **Future-Proof Architecture**: Vulkan represents the long-term direction of graphics APIs
- **Performance Predictability**: Explicit control over GPU resources enables deterministic behavior
- **Medical Device Optimization**: Low-level control perfect for real-time medical applications
- **Platform Focus**: Windows/Linux support covers 95%+ of medical device deployment scenarios
- **Simplified Validation**: Reduced platform matrix simplifies regulatory validation processes
- **Industry Alignment**: Major medical device companies migrating to Vulkan-based solutions
- **Modern Development**: Latest C++ bindings and development practices

### Negative
- **Development Complexity**: Higher initial learning curve and development overhead
- **Team Training**: Requires significant investment in Vulkan expertise development
- **macOS Market Loss**: Loss of potential macOS medical device market (estimated <3% impact)
- **Migration Effort**: Requires complete rewrite of existing OpenGL-based prototypes

### Risks
- **Implementation Complexity**: Vulkan's complexity might slow initial development
  - *Mitigation*: Invest in team training and use high-level Vulkan abstractions
- **Driver Maturity**: Vulkan drivers may have issues on older medical device hardware
  - *Mitigation*: Maintain minimum driver version requirements, comprehensive testing matrix
- **Ecosystem Dependencies**: Fewer Vulkan medical device examples and libraries available
  - *Mitigation*: Leverage general Vulkan ecosystem, build medical device specific abstractions

## Implementation Notes

### Vulkan Abstraction Layer
Implement a medical device-optimized abstraction layer over Vulkan to:
- Simplify Vulkan complexity for UI development
- Provide deterministic resource management
- Enable comprehensive error handling and validation
- Support medical device-specific performance requirements
- Implement safety-critical rendering pipelines

### Platform-Specific Implementation
- **Windows**: Use Win32 surface creation with Vulkan WSI
- **Linux**: Use X11/Wayland surface creation with Vulkan WSI
- **Embedded**: Plan for direct framebuffer rendering on embedded platforms
- **GPU Support**: Target modern GPUs with Vulkan 1.3 support (2018+)

### Medical Device Integration
- Implement deterministic command buffer submission
- Provide explicit memory management for real-time constraints
- Include comprehensive validation layers during development
- Design for medical device performance testing and validation
- Implement software fallback for critical safety functions

### Development Roadmap
- **Phase 1**: Basic Vulkan infrastructure and window management
- **Phase 2**: Core rendering abstractions and resource management
- **Phase 3**: Medical device-specific features and optimizations
- **Phase 4**: Comprehensive validation and regulatory documentation

## References
- Vulkan 1.3 Specification
- Vulkan Memory Management Best Practices
- IEC 62304: Medical device software - Software life cycle processes
- IEC 62366: Medical devices - Application of usability engineering
- FDA Guidance on Cybersecurity in Medical Devices
- Khronos Group Vulkan Registry
- Medical Device Software Development Guidelines

## Approval
- **Original Decision Date**: 2025-07-15 (OpenGL)
- **Revised Decision Date**: 2025-07-25 (Vulkan)
- **Approved By**: Architecture Team, CTO
- **Review Date**: 2026-01-25 (6-month review for implementation progress)
- **Implementation Target**: Q2 2026