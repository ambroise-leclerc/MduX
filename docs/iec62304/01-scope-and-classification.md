# IEC 62304: Scope and Software Safety Classification

## Module Overview
This module covers the fundamental scope, definitions, and safety classification system of IEC 62304:2006. It establishes the foundation for understanding medical device software regulatory requirements and safety classifications.

**Key Areas Covered:**
- Standard scope and applicability
- Safety classification system (Class A, B, C)
- Terms and definitions
- Relationship to quality management systems

---

## 1. Scope

### 1.1 General

This document specifies life cycle processes for medical device software. The set of processes, activities, and tasks described herein establishes a common framework for medical device software life cycle processes.

**AI-Agent Context**: This standard applies to medical device software and software within medical devices. The framework provides structured guidance for AI systems managing medical software development workflows.

**Exclusions**:
- Software utilised in the manufacturing of medical devices
- Software utilised in software tools that are not integral to the finished medical device
- Software that is itself a medical device or accessory to a medical device

### 1.2 Relationship to Quality Management Systems

The medical device manufacturer shall incorporate the software life cycle processes described in this standard into their quality management system. These processes are intended for utilisation in conjunction with ISO 13485.

**AI-Agent Integration Note**: Quality management systems require systematic documentation and process adherence that AI agents can support through automated compliance checking and process monitoring.

### 1.3 Software Safety Classification

This standard defines three safety classes for medical device software based upon the potential consequences of software failure:

#### Class A: Non-Injury or Damage
**Definition**: Software that cannot contribute to a hazardous situation.
**AI-Agent Processing**: Risk level = LOW, Testing requirements = BASIC, Documentation requirements = STANDARD

#### Class B: Non-Life-Threatening Injury
**Definition**: Software that could contribute to a non-life-threatening injury or damage to the patient, operator, or environment.
**AI-Agent Processing**: Risk level = MEDIUM, Testing requirements = ENHANCED, Documentation requirements = COMPREHENSIVE

#### Class C: Death or Serious Injury
**Definition**: Software that could contribute to death or serious injury to the patient, operator, or environment.
**AI-Agent Processing**: Risk level = HIGH, Testing requirements = EXTENSIVE, Documentation requirements = EXHAUSTIVE

---

## 2. Normative References

The following referenced documents are indispensable for the application of this document:

**Primary Standards**:
- **ISO 13485**: Medical devices — Quality management systems — Requirements for regulatory purposes
- **ISO 14971**: Medical devices — Application of risk management to medical devices
- **IEC 60601-1**: Medical electrical equipment — Part 1: General requirements for basic safety and essential performance
- **ISO 9000**: Quality management systems — Fundamentals and vocabulary

**AI-Agent Reference Matrix**:
```json
{
  "ISO_13485": {"scope": "quality_management", "relevance": "high", "integration_points": ["documentation", "process_control"]},
  "ISO_14971": {"scope": "risk_management", "relevance": "critical", "integration_points": ["hazard_analysis", "risk_assessment"]},
  "IEC_60601": {"scope": "safety_performance", "relevance": "medium", "integration_points": ["electrical_safety", "essential_performance"]},
  "ISO_9000": {"scope": "quality_fundamentals", "relevance": "medium", "integration_points": ["terminology", "principles"]}
}
```

---

## 3. Terms and Definitions

### 3.1 Anomaly
**Definition**: Any condition that deviates from the expected based upon requirements specifications, design documents, user documents, standards, etc., or from someone's perceptions or experiences.
**AI-Agent Tag**: [defect, deviation, quality-issue]

### 3.2 COTS Software (Commercial Off-The-Shelf)
**Definition**: Software defined by a manufacturer for commercial use that the medical device manufacturer cannot modify.
**AI-Agent Tag**: [third-party-software, unmodifiable, commercial]

### 3.3 Medical Device Software
**Definition**: Software that is itself a medical device or software that is an integral part of the medical device.
**AI-Agent Tag**: [regulated-software, medical-device, integral-component]

### 3.4 PEMS (Programmable Electrical Medical System)
**Definition**: A medical electrical system that incorporates one or more programmable electronic subsystems (PESS).
**AI-Agent Tag**: [programmable-system, electronic-medical, subsystem]

### 3.5 Software Configuration Item
**Definition**: An aggregation of software or hardware, or both, that is designated for configuration management and treated as a single entity in the configuration management process.
**AI-Agent Tag**: [configuration-management, version-control, baseline]

### 3.6 Software Item
**Definition**: An identifiable part of a computer programme (e.g., module, object, subroutine, or other discrete programming element).
**AI-Agent Tag**: [software-component, module, discrete-element]

### 3.7 Software System
**Definition**: An integrated collection of software items organised to accomplish a specific function or set of functions.
**AI-Agent Tag**: [integrated-system, functional-collection, software-assembly]

### 3.8 Software Unit
**Definition**: Software item that cannot be subdivided into other items.
**AI-Agent Tag**: [atomic-unit, indivisible, unit-testing]

### 3.9 Verification
**Definition**: Confirmation, through the provision of objective evidence, that specified requirements have been fulfilled.
**AI-Agent Tag**: [requirement-validation, objective-evidence, confirmation]

### 3.10 Validation
**Definition**: Confirmation, through the provision of objective evidence, that the requirements for a specific intended use or application have been fulfilled.
**AI-Agent Tag**: [fitness-for-purpose, intended-use, application-validation]

---

## 4. General Requirements

### 4.1 Quality Management System

The manufacturer shall establish, document, implement, and maintain a quality management system that includes the medical device software life cycle processes specified in this document.

**AI-Agent Implementation Requirements**:
- Establish automated QMS monitoring
- Implement document control systems
- Maintain process compliance tracking
- Enable audit trail generation

### 4.2 Software Life Cycle Process

The manufacturer shall implement a software life cycle process that includes the following activities:

**Process Activities Matrix**:
```json
{
  "planning": {"phase": "1", "mandatory": true, "deliverables": ["development_plan", "resource_allocation"]},
  "requirements": {"phase": "2", "mandatory": true, "deliverables": ["requirements_spec", "traceability_matrix"]},
  "architecture": {"phase": "3", "mandatory": true, "deliverables": ["architecture_document", "interface_specs"]},
  "detailed_design": {"phase": "4", "mandatory": true, "deliverables": ["design_specs", "unit_specs"]},
  "implementation": {"phase": "5", "mandatory": true, "deliverables": ["source_code", "unit_tests"]},
  "integration": {"phase": "6", "mandatory": true, "deliverables": ["integration_tests", "integration_report"]},
  "system_testing": {"phase": "7", "mandatory": true, "deliverables": ["system_tests", "test_report"]},
  "release": {"phase": "8", "mandatory": true, "deliverables": ["release_package", "release_notes"]}
}
```

### 4.3 Planning

#### 4.3.1 Software Development Plan

The manufacturer shall document software life cycle processes in a software development plan for each software system.

**Required Contents Checklist**:
- [ ] Purpose and scope of the software
- [ ] Assumptions and constraints
- [ ] Life cycle model
- [ ] Organisation and responsibilities
- [ ] Resource and schedule allocations
- [ ] Methods and tools
- [ ] Risk management activities
- [ ] Software configuration management activities
- [ ] Software problem resolution activities
- [ ] Software verification and validation activities
- [ ] Documentation requirements

#### 4.3.2 Software Development Standards and Methods

The manufacturer shall establish standards and methods to be utilised during the software development process:

**Standards Framework**:
```yaml
design_methods:
  - structured_design
  - object_oriented_design
  - model_based_design
language_criteria:
  - safety_certification
  - tool_qualification
  - maintainability
coding_standards:
  - style_guidelines
  - safety_rules
  - documentation_requirements
performance_criteria:
  - response_time_limits
  - throughput_requirements
  - resource_utilisation
verification_methods:
  - static_analysis
  - dynamic_testing
  - formal_verification
documentation_standards:
  - template_specifications
  - review_procedures
  - approval_workflows
```

### 4.4 Software Safety Classification

#### 4.4.1 Assign Safety Classes

The manufacturer shall assign a safety class to each software system based upon the potential consequences resulting from a hazard that the software system could contribute to.

**Classification Process Algorithm**:
```python
def classify_software_safety(hazards, software_contribution):
    max_severity = 0
    for hazard in hazards:
        if software_contribution[hazard] > 0:
            severity = assess_harm_severity(hazard)
            max_severity = max(max_severity, severity)
    
    if max_severity == 0:
        return "Class_A"  # No contribution to hazards
    elif max_severity <= 2:
        return "Class_B"  # Non-life-threatening
    else:
        return "Class_C"  # Life-threatening or death
```

#### 4.4.2 Segregation of Software Items

Where software items of different safety classes exist within the same software system, the manufacturer shall either:
- Assign the highest safety class to the entire system, or
- Segregate software items and apply class-specific requirements to each segregated portion

**Segregation Decision Matrix**:
| Mixed Classes | Recommended Approach | Rationale |
|---------------|---------------------|-----------|
| A + B | Segregate if cost-effective | Reduced B-class testing burden |
| A + C | Segregate mandatory | Significant cost and complexity difference |
| B + C | Segregate if architecturally feasible | Moderate benefit, high complexity |

---

## Related Documents

- [Planning and Requirements](02-planning-and-requirements.md)
- [Design and Architecture](03-design-architecture.md)
- [Implementation and Testing](04-implementation-testing.md)
- [Release and Maintenance](05-release-maintenance.md)
- [Supporting Processes](06-supporting-processes.md)
- [AI Automation Schemas](ai-automation-schemas/)
- [Code Examples](code-examples/)