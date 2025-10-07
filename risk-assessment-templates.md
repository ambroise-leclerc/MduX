# Risk Assessment Templates and Tools for MduX Medical Device Library

This document provides practical templates and tools for conducting ISO 14971 risk assessments specifically for the MduX medical device UI library.

---

## 1. Quick Risk Assessment Tool

### 1.1 MduX Component Risk Screener

Use this tool to quickly assess potential risks for any MduX component:

```cpp
// Risk Assessment Integration Example
namespace mdux::risk_assessment {
    
    struct ComponentRiskProfile {
        std::string componentName;
        SafetyCriticality criticality;
        std::vector<IdentifiedHazard> hazards;
        RiskLevel overallRisk;
        bool requiresDetailedAnalysis;
    };
    
    // Quick screening questionnaire
    class RiskScreener {
    public:
        ComponentRiskProfile assessComponent(const std::string& componentName) {
            ComponentRiskProfile profile;
            profile.componentName = componentName;
            
            // Automated screening questions
            if (handlesPatientData(componentName)) {
                profile.criticality = SafetyCriticality::High;
                profile.hazards.emplace_back("Data corruption", HazardType::PatientSafety);
            }
            
            if (controlsCriticalFunctions(componentName)) {
                profile.criticality = SafetyCriticality::High;
                profile.hazards.emplace_back("Function failure", HazardType::CriticalOperation);
            }
            
            if (hasUserInterface(componentName)) {
                profile.hazards.emplace_back("User error", HazardType::UsabilityError);
            }
            
            profile.overallRisk = calculateOverallRisk(profile.hazards);
            profile.requiresDetailedAnalysis = (profile.overallRisk >= RiskLevel::Medium);
            
            return profile;
        }
    };
}
```

### 1.2 Component Classification Matrix

| Component Type | Data Sensitivity | User Interaction | Safety Impact | Initial Risk Level |
|----------------|------------------|------------------|---------------|-------------------|
| Data Renderer | High | Medium | High | **High** |
| UI Controls | Medium | High | Medium | **Medium** |
| Configuration | High | Low | Medium | **Medium** |
| Logging | Low | None | Low | **Low** |
| Graphics Engine | Low | Medium | Medium | **Medium** |

---

## 2. Detailed Risk Assessment Templates

### 2.1 Software Hazard Analysis Worksheet

```markdown
# MduX Software Hazard Analysis
**Component**: [Component Name]
**Version**: [Version Number]
**Date**: [Assessment Date]
**Analyst**: [Your Name]

## Component Description
**Purpose**: [What does this component do?]
**Interfaces**: [What other components does it interact with?]
**Data Handled**: [What type of data does it process?]
**User Interaction**: [How do users interact with it?]

## Hazard Identification

### Hazard 1: [Hazard Name]
**ID**: HZ-[Component]-001
**Description**: [Detailed description of what could go wrong]
**Triggering Events**: 
- [Event 1]
- [Event 2]
- [Event 3]

**Potential Harm**: [What harm could result?]
**Affected Parties**: [Who could be harmed?]

**Probability Assessment**:
- Historical data: [Any known incidents?]
- Expert judgment: [Team consensus on likelihood]
- Similar systems: [Experience from similar components]
- **Probability Score**: [1-5]

**Severity Assessment**:
- Direct harm: [Immediate impact on patient/user]
- Indirect consequences: [Secondary effects]
- Recovery time: [How long to resolve?]
- **Severity Score**: [1-5]

**Risk Level**: [P × S = Risk Score]
```

### 2.2 FMEA (Failure Mode and Effects Analysis) Template

```markdown
# MduX Component FMEA
**Component**: [Name]
**Function**: [Primary function]
**Date**: [Date]

| Failure Mode | Potential Effect | Severity | Potential Cause | Occurrence | Current Controls | Detection | RPN | Recommended Actions |
|--------------|------------------|----------|-----------------|------------|------------------|-----------|-----|-------------------|
| Memory leak | System slowdown | 7 | Improper cleanup | 3 | RAII patterns | Automated tests | 63 | Add memory monitoring |
| Null pointer | Application crash | 9 | Uninitialized data | 2 | Smart pointers | Static analysis | 36 | Strengthen null checks |
| Data corruption | Wrong display | 8 | Race condition | 4 | Mutex protection | Integration tests | 96 | Improve synchronization |

**RPN Calculation**: Severity × Occurrence × Detection
**Priority Levels**: 
- RPN 1-100: Low priority
- RPN 101-200: Medium priority  
- RPN 201-300: High priority
```

### 2.3 Use-Case Based Risk Analysis

```markdown
# Use Case Risk Analysis: [Scenario Name]

## Scenario Description
**Use Case**: [e.g., "Display Patient Vital Signs"]
**Actor**: [e.g., "ICU Nurse"]
**Context**: [e.g., "Emergency patient monitoring"]
**Prerequisites**: [What must be true before this scenario?]

## Normal Flow
1. [Step 1]
2. [Step 2]
3. [Step 3]
...

## Risk Analysis for Each Step

### Step 1: [Description]
**Potential Failures**:
- What if the data is missing?
- What if the display fails?
- What if the user misinterprets the information?

**Risk Controls**:
- Input validation
- Error messages
- Clear visual design

### Step 2: [Description]
[Continue for each step...]

## Alternative Flows (Error Scenarios)
### Alt Flow 1: [Error condition]
**Trigger**: [What causes this error?]
**System Response**: [How should system react?]
**Risk Assessment**: [What are the risks if this isn't handled properly?]
```

---

## 3. MduX-Specific Risk Scenarios

### 3.1 Vulkan Rendering Risks

```cpp
// Example: Risk-aware Vulkan rendering
class SafeVulkanRenderer {
public:
    struct RenderRisk {
        enum Type { 
            DriverCrash, 
            MemoryExhaustion, 
            CommandTimeout,
            InvalidState 
        } type;
        
        std::string description;
        MitigationStrategy mitigation;
    };
    
    RenderResult renderWithRiskAssessment(const UIElement& element) {
        // Pre-render risk checks
        auto risks = assessRenderingRisks(element);
        
        for (const auto& risk : risks) {
            if (risk.type == RenderRisk::MemoryExhaustion) {
                // Risk mitigation: Use lower quality rendering
                return renderLowQuality(element);
            }
            
            if (risk.type == RenderRisk::CommandTimeout) {
                // Risk mitigation: Split into smaller commands
                return renderInChunks(element);
            }
        }
        
        return performNormalRender(element);
    }
    
private:
    std::vector<RenderRisk> assessRenderingRisks(const UIElement& element) {
        std::vector<RenderRisk> risks;
        
        // Check available GPU memory
        auto availableMemory = getAvailableGpuMemory();
        auto requiredMemory = estimateMemoryRequirements(element);
        
        if (requiredMemory > availableMemory * 0.8) {
            risks.emplace_back(RenderRisk{
                .type = RenderRisk::MemoryExhaustion,
                .description = "Insufficient GPU memory for high-quality rendering",
                .mitigation = MitigationStrategy::ReduceQuality
            });
        }
        
        return risks;
    }
};
```

### 3.2 Medical Data Display Risks

```markdown
# Risk Analysis: Medical Data Display

## Identified Risks

### Risk 1: Incorrect Patient Data Display
**Risk ID**: RK-UI-001
**Component**: PatientDataRenderer
**Scenario**: Wrong patient's data shown on display

**Causes**:
- Database query error
- UI component receives wrong patient ID
- Cache contains stale data
- Race condition in data loading

**Current Controls**:
```cpp
class PatientDataValidator {
public:
    ValidationResult validate(const PatientData& data, const PatientID& expectedId) {
        ValidationResult result;
        
        // Verify patient ID matches
        if (data.patientId != expectedId) {
            result.addCriticalError("Patient ID mismatch detected");
            return result;
        }
        
        // Check data freshness
        auto age = std::chrono::system_clock::now() - data.timestamp;
        if (age > std::chrono::minutes(5)) {
            result.addWarning("Patient data may be stale");
        }
        
        // Verify data integrity
        if (!verifyDataIntegrity(data)) {
            result.addError("Data integrity check failed");
        }
        
        return result;
    }
};
```

**Risk Level**: High (P=3, S=9, Risk=27)
**Acceptability**: Unacceptable without additional controls

### Risk 2: UI Responsiveness Failure
**Risk ID**: RK-UI-002
**Component**: RenderingEngine
**Scenario**: UI becomes unresponsive during critical operations

**Risk Controls**:
```cpp
class ResponsiveRenderer {
    static constexpr auto MAX_RENDER_TIME = std::chrono::milliseconds(16); // 60 FPS
    
public:
    RenderResult renderWithTimeout(const Scene& scene) {
        std::promise<RenderResult> promise;
        auto future = promise.get_future();
        
        std::thread renderThread([&promise, this, &scene]() {
            try {
                auto result = performRender(scene);
                promise.set_value(result);
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        });
        
        if (future.wait_for(MAX_RENDER_TIME) == std::future_status::timeout) {
            // Risk mitigation: Force thread termination and use cached frame
            renderThread.detach(); // Note: In production, use safer thread management
            return RenderResult::UseCachedFrame();
        }
        
        renderThread.join();
        return future.get();
    }
};
```
```

---

## 4. Automated Risk Assessment Tools

### 4.1 Static Analysis Integration

```cpp
// Risk-based static analysis configuration
namespace mdux::static_analysis {
    
    struct RiskBasedRule {
        std::string ruleId;
        std::string description;
        RiskCategory category;
        SeverityLevel severity;
        bool isBlockingForRelease;
    };
    
    // High-risk rules for medical device software
    const std::vector<RiskBasedRule> MEDICAL_DEVICE_RULES = {
        {
            .ruleId = "MDUX-001",
            .description = "Potential null pointer dereference in patient data handling",
            .category = RiskCategory::PatientSafety,
            .severity = SeverityLevel::Critical,
            .isBlockingForRelease = true
        },
        {
            .ruleId = "MDUX-002", 
            .description = "Unchecked memory allocation in critical path",
            .category = RiskCategory::SystemReliability,
            .severity = SeverityLevel::High,
            .isBlockingForRelease = true
        },
        {
            .ruleId = "MDUX-003",
            .description = "Missing input validation for user interface",
            .category = RiskCategory::UsabilityError,
            .severity = SeverityLevel::Medium,
            .isBlockingForRelease = false
        }
    };
}
```

### 4.2 Runtime Risk Monitoring

```cpp
class RuntimeRiskMonitor {
private:
    struct RiskMetric {
        std::string name;
        double currentValue;
        double warningThreshold;
        double criticalThreshold;
        std::chrono::system_clock::time_point lastUpdate;
    };
    
    std::unordered_map<std::string, RiskMetric> metrics_;
    std::vector<RiskEvent> detectedRisks_;
    
public:
    void updateMetric(const std::string& name, double value) {
        auto& metric = metrics_[name];
        metric.currentValue = value;
        metric.lastUpdate = std::chrono::system_clock::now();
        
        // Assess risk level
        if (value > metric.criticalThreshold) {
            triggerCriticalRisk(name, value);
        } else if (value > metric.warningThreshold) {
            triggerWarningRisk(name, value);
        }
    }
    
    void monitorSystemHealth() {
        // Memory usage monitoring
        auto memoryUsage = getCurrentMemoryUsage();
        updateMetric("memory_usage", memoryUsage);
        
        // Response time monitoring
        auto avgResponseTime = getAverageResponseTime();
        updateMetric("response_time", avgResponseTime);
        
        // Error rate monitoring
        auto errorRate = getRecentErrorRate();
        updateMetric("error_rate", errorRate);
    }
    
private:
    void triggerCriticalRisk(const std::string& metric, double value) {
        RiskEvent event{
            .type = RiskEventType::Critical,
            .source = metric,
            .value = value,
            .timestamp = std::chrono::system_clock::now(),
            .requiresImmediateAction = true
        };
        
        detectedRisks_.push_back(event);
        notifyRiskManagement(event);
        
        // Automatic risk mitigation
        if (metric == "memory_usage") {
            triggerGarbageCollection();
        } else if (metric == "response_time") {
            enablePerformanceMode();
        }
    }
};
```

---

## 5. Risk Traceability Matrix

### 5.1 Requirements to Risk Mapping

```cpp
// Traceability between requirements and risks
namespace mdux::traceability {
    
    struct RequirementRiskMapping {
        std::string requirementId;
        std::string requirementText;
        std::vector<std::string> associatedRisks;
        std::vector<std::string> riskControls;
        VerificationMethod verification;
        ValidationMethod validation;
    };
    
    const std::vector<RequirementRiskMapping> SAFETY_REQUIREMENTS = {
        {
            .requirementId = "SR-001",
            .requirementText = "System shall render patient data within 100ms",
            .associatedRisks = {"RK-UI-002", "RK-PERF-001"},
            .riskControls = {"RC-001: Timeout mechanism", "RC-002: Cached rendering"},
            .verification = VerificationMethod::AutomatedTest,
            .validation = ValidationMethod::ClinicalTest
        },
        {
            .requirementId = "SR-002", 
            .requirementText = "System shall validate patient ID before displaying data",
            .associatedRisks = {"RK-UI-001", "RK-DATA-001"},
            .riskControls = {"RC-003: ID validation", "RC-004: Data integrity check"},
            .verification = VerificationMethod::CodeReview,
            .validation = ValidationMethod::UsabilityTest
        }
    };
}
```

### 5.2 Risk Control Verification Matrix

| Risk ID | Risk Description | Control ID | Control Description | Verification Method | Status | Evidence |
|---------|------------------|------------|---------------------|-------------------|--------|----------|
| RK-UI-001 | Wrong patient data display | RC-003 | Patient ID validation | Unit test | ✅ Complete | TestPatientValidation.cpp |
| RK-UI-002 | UI unresponsive | RC-001 | Rendering timeout | Integration test | ✅ Complete | TestRenderTimeout.cpp |
| RK-DATA-001 | Data corruption | RC-004 | Integrity checking | Static analysis | 🔄 In Progress | Clang-tidy report |

---

## 6. Medical Device Specific Checklists

### 6.1 IEC 62304 Software Safety Classification

```markdown
# Software Safety Classification Checklist

## Class C (Death or serious injury possible)
- [ ] Software controls life-supporting functions
- [ ] Software controls critical therapy delivery
- [ ] Software processes critical patient data for immediate clinical decisions
- [ ] Failure could directly cause patient death or serious injury

## Class B (Non-serious injury possible)  
- [ ] Software provides diagnostic information
- [ ] Software controls non-critical therapy functions
- [ ] Software manages patient data with clinical relevance
- [ ] Failure could cause minor injury or inconvenience

## Class A (No injury or damage to health)
- [ ] Software provides general information only
- [ ] Software has no direct patient impact
- [ ] Software failure cannot cause harm to patient
- [ ] Software is administrative or supportive only

**MduX Component Classification**: [A/B/C]
**Justification**: [Explain why this classification is appropriate]
```

### 6.2 Usability Risk Assessment (IEC 62366)

```markdown
# Usability Risk Assessment Checklist

## Use Error Identification
- [ ] Use errors that could lead to harm identified
- [ ] Use scenarios analyzed for potential errors  
- [ ] Critical tasks identified and prioritized
- [ ] User interface complexity assessed

## Risk Analysis for Use Errors
For each identified use error:

### Use Error: [Description]
**Hazardous situation**: [How could this lead to harm?]
**Harm**: [What harm could result?]
**Probability**: [How likely is this error?]
**Risk level**: [Low/Medium/High]

**Risk reduction measures**:
- [ ] Design changes to prevent error
- [ ] Warnings or alarms implemented  
- [ ] User training materials provided
- [ ] Instructions for use updated

## Usability Testing
- [ ] Representative users tested
- [ ] Realistic use conditions simulated
- [ ] Critical tasks successfully completed
- [ ] Use errors observed and documented
- [ ] Residual risk assessed as acceptable
```

---

## 7. Post-Market Risk Assessment

### 7.1 Incident Analysis Template

```markdown
# Incident Analysis Report

## Incident Information
**Incident ID**: INC-2024-001
**Date Reported**: [Date]
**Reporter**: [Name/Organization]
**Device Information**:
- MduX Version: [Version]
- Device Classification: [Class A/B/C]
- Installation Environment: [Clinical/Home/Mobile]

## Incident Description
**What happened?**: [Detailed description]
**When did it happen?**: [Timeline]
**Where did it happen?**: [Location/context]
**Who was involved?**: [Users affected]

## Initial Risk Assessment
**Immediate harm**: [Was anyone hurt?]
**Potential for wider impact**: [Could this affect other users?]
**System availability**: [Is the system still usable?]
**Data integrity**: [Was any data lost or corrupted?]

## Investigation Findings
**Root cause**: [What caused this incident?]
**Contributing factors**: [What made this more likely?]
**System response**: [How did the system behave?]
**User actions**: [What did users do?]

## Risk Evaluation
**New risks identified**: [Any previously unknown risks?]
**Existing risk assessment validity**: [Do we need to update risk analysis?]
**Control effectiveness**: [Did existing controls work as intended?]

## Corrective Actions
**Immediate actions**: [What was done right away?]
**Planned improvements**: [What will be changed?]
**Timeline**: [When will changes be implemented?]
**Verification**: [How will we verify the fix worked?]

## Risk Management Update
**Risk file updates**: [What documents need updating?]
**New controls needed**: [Additional risk controls to implement?]
**Monitoring requirements**: [Enhanced surveillance needed?]
```

### 7.2 Trend Analysis Template

```cpp
// Automated trend analysis for risk management
class RiskTrendAnalyzer {
public:
    struct TrendData {
        std::string riskCategory;
        std::vector<double> monthlyIncidenceRates;
        TrendDirection direction;
        double significanceLevel;
        std::string analysis;
    };
    
    std::vector<TrendData> analyzeTrends(
        const std::vector<IncidentReport>& incidents,
        std::chrono::months lookbackPeriod
    ) {
        std::vector<TrendData> trends;
        
        // Group incidents by risk category
        auto categoryGroups = groupByCategory(incidents);
        
        for (const auto& [category, categoryIncidents] : categoryGroups) {
            TrendData trend;
            trend.riskCategory = category;
            trend.monthlyIncidenceRates = calculateMonthlyRates(categoryIncidents, lookbackPeriod);
            trend.direction = detectTrendDirection(trend.monthlyIncidenceRates);
            trend.significanceLevel = calculateSignificance(trend.monthlyIncidenceRates);
            trend.analysis = generateAnalysis(trend);
            
            trends.push_back(trend);
        }
        
        return trends;
    }
    
private:
    TrendDirection detectTrendDirection(const std::vector<double>& rates) {
        if (rates.size() < 2) return TrendDirection::Insufficient_Data;
        
        double slope = calculateLinearRegressionSlope(rates);
        
        if (slope > 0.1) return TrendDirection::Increasing;
        if (slope < -0.1) return TrendDirection::Decreasing;
        return TrendDirection::Stable;
    }
};
```

---

## 8. Integration Examples

### 8.1 CMake Integration for Risk-Based Testing

```cmake
# CMakeLists.txt - Risk-based testing configuration
cmake_minimum_required(VERSION 3.28)
project(MduX_RiskManagement)

# Define risk-based test categories
set(SAFETY_CRITICAL_TESTS
    tests/PatientDataValidationTest.cpp
    tests/RenderingTimeoutTest.cpp
    tests/MemoryLeakTest.cpp
)

set(HIGH_RISK_TESTS
    tests/ErrorHandlingTest.cpp
    tests/ConcurrencyTest.cpp
    tests/InputValidationTest.cpp  
)

set(MEDIUM_RISK_TESTS
    tests/PerformanceTest.cpp
    tests/UsabilityTest.cpp
)

# Create risk-based test executables
add_executable(safety_critical_tests ${SAFETY_CRITICAL_TESTS})
add_executable(high_risk_tests ${HIGH_RISK_TESTS})  
add_executable(medium_risk_tests ${MEDIUM_RISK_TESTS})

# Link with MduX library
target_link_libraries(safety_critical_tests MduX::Core)
target_link_libraries(high_risk_tests MduX::Core)
target_link_libraries(medium_risk_tests MduX::Core)

# Custom test targets with risk-based ordering
add_custom_target(run_risk_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS safety_critical_tests high_risk_tests medium_risk_tests
)

# Fail build if safety critical tests fail
add_test(NAME safety_critical COMMAND safety_critical_tests)
set_tests_properties(safety_critical PROPERTIES 
    FAIL_REGULAR_EXPRESSION "FAILED"
    REQUIRED_FILES $<TARGET_FILE:safety_critical_tests>
)
```

### 8.2 Continuous Integration Pipeline

```yaml
# .github/workflows/risk-based-ci.yml
name: Risk-Based CI Pipeline

on: [push, pull_request]

jobs:
  safety-critical-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Setup C++23 Environment
        uses: ./.github/actions/setup-cpp23
      - name: Run Safety Critical Tests
        run: |
          cmake -B build -S . -DBUILD_SAFETY_TESTS=ON
          cmake --build build --target safety_critical_tests
          ./build/safety_critical_tests --gtest_output=xml:safety_results.xml
      - name: Block on Safety Test Failures
        run: |
          if [ $? -ne 0 ]; then
            echo "Safety critical tests failed - blocking pipeline"
            exit 1
          fi
          
  high-risk-tests:
    needs: safety-critical-tests
    runs-on: ubuntu-latest  
    steps:
      - name: Run High Risk Tests
        run: |
          cmake --build build --target high_risk_tests
          ./build/high_risk_tests
          
  risk-analysis:
    needs: [safety-critical-tests, high-risk-tests]
    runs-on: ubuntu-latest
    steps:
      - name: Generate Risk Report
        run: |
          python scripts/generate_risk_report.py \
            --test-results build/test_results/ \
            --output risk_analysis_report.html
      - name: Upload Risk Analysis
        uses: actions/upload-artifact@v3
        with:
          name: risk-analysis-report
          path: risk_analysis_report.html
```

---

## 9. Documentation Templates

### 9.1 Risk Management Plan Template

```markdown
# Risk Management Plan: MduX [Component Name]

## 1. Device Description
**Product Name**: MduX [Component Name]
**Intended Use**: [Description of intended use]
**Intended User**: [Healthcare professionals/Patients/etc.]
**Use Environment**: [Clinical/Home/Mobile settings]
**Device Classification**: [Class I/II/III per FDA, Class A/B/C per IEC 62304]

## 2. Risk Management Process
**Standard Applied**: ISO 14971:2019
**Risk Management Team**:
- Risk Manager: [Name]
- Software Lead: [Name] 
- Clinical Expert: [Name]
- Quality Representative: [Name]

## 3. Risk Acceptability Criteria
[Include risk matrix and acceptability thresholds]

## 4. Risk Analysis Plan
**Methods**:
- [ ] Preliminary Hazard Analysis (PHA)
- [ ] Failure Mode and Effects Analysis (FMEA)
- [ ] Fault Tree Analysis (FTA)
- [ ] Hazard and Operability Study (HAZOP)

**Scope**: [What will be analyzed?]
**Boundaries**: [What is excluded?]

## 5. Risk Control Strategy
**Hierarchy of Controls**:
1. Inherent safety by design
2. Protective measures in the device
3. Information for safety

## 6. Verification and Validation
**Verification Methods**: [How will controls be verified?]
**Validation Methods**: [How will effectiveness be validated?]

## 7. Post-Market Surveillance
**Surveillance Methods**: [How will risks be monitored after release?]
**Review Schedule**: [When will risk assessment be updated?]

## 8. Risk Management File
**Location**: [Where will risk management records be maintained?]
**Retention**: [How long will records be kept?]
**Access**: [Who can access the risk management file?]
```

### 9.2 Risk Control Implementation Guide

```markdown
# Risk Control Implementation Guide

## Control Categories

### 1. Inherent Safety by Design
**Principle**: Eliminate hazards through design choices

**Examples**:
```cpp
// Use type safety to prevent errors
enum class PatientState { Stable, Critical, Emergency };
void handlePatient(PatientState state) {
    // Compiler prevents invalid states
}

// Use RAII for resource management  
class SafeResourceManager {
    std::unique_ptr<Resource> resource_;
public:
    SafeResourceManager() : resource_(std::make_unique<Resource>()) {}
    // Automatic cleanup on destruction
};

// Use constexpr for compile-time validation
template<int MaxValue>
constexpr bool isValidReading(int value) {
    static_assert(MaxValue > 0, "MaxValue must be positive");
    return value >= 0 && value <= MaxValue;
}
```

### 2. Protective Measures
**Principle**: Add safeguards to detect and respond to hazards

**Examples**:
```cpp
// Input validation
class InputValidator {
public:
    static ValidationResult validate(const MedicalReading& reading) {
        ValidationResult result;
        
        if (reading.value < 0) {
            result.addError("Negative medical reading not allowed");
        }
        
        if (reading.timestamp > std::chrono::system_clock::now()) {
            result.addError("Future timestamp not allowed");
        }
        
        return result;
    }
};

// Watchdog timer for critical operations
class OperationWatchdog {
    std::chrono::milliseconds timeout_;
    std::thread watchdog_thread_;
    std::atomic<bool> operation_complete_{false};
    
public:
    template<typename Operation>
    Result executeWithWatchdog(Operation op, std::chrono::milliseconds timeout) {
        timeout_ = timeout;
        operation_complete_ = false;
        
        // Start watchdog
        watchdog_thread_ = std::thread([this]() {
            std::this_thread::sleep_for(timeout_);
            if (!operation_complete_) {
                // Operation timed out - trigger safe shutdown
                triggerSafeShutdown();
            }
        });
        
        // Execute operation
        auto result = op();
        
        operation_complete_ = true;
        watchdog_thread_.join();
        
        return result;
    }
};
```

### 3. Information for Safety
**Principle**: Provide information to help users avoid hazards

**Examples**:
- User interface warnings and alerts
- Documentation and training materials  
- Error messages and guidance
- Visual and auditory alarms
```
```

---

## 10. Conclusion

This comprehensive set of risk assessment templates and tools provides everything needed to implement ISO 14971 risk management for the MduX medical device library. The templates are specifically designed to integrate with your C++23 architecture and medical device compliance requirements.

### Quick Start Guide:
1. **Use the Component Risk Screener** to quickly assess any new MduX component
2. **Apply the FMEA template** for detailed analysis of critical components
3. **Implement the runtime monitoring tools** for continuous risk assessment
4. **Follow the integration examples** to embed risk management in your development process

### Key Benefits:
- **Practical**: Templates are ready to use with minimal customization
- **Integrated**: Designed specifically for your C++23 medical device architecture  
- **Compliant**: Follows ISO 14971:2019 requirements
- **Automated**: Includes code examples for automated risk monitoring
- **Traceable**: Provides full traceability from risks to controls to verification

These tools will help ensure that your MduX medical device library meets the highest safety and regulatory standards while maintaining development efficiency.

<function_calls>
<invoke name="TodoWrite">
<parameter name="todos">[{"content": "Create ISO 14971 risk management framework for MduX project", "status": "completed", "activeForm": "Creating risk management framework"}, {"content": "Create risk assessment templates and tools", "status": "completed", "activeForm": "Creating assessment tools"}, {"content": "Build integration examples with MduX architecture", "status": "completed", "activeForm": "Building integration examples"}, {"content": "Create compliance validation checklists", "status": "completed", "activeForm": "Creating validation checklists"}]