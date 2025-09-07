*This document represents a comprehensive markdown version of IEC 62304:2006, compiled from public source materials. It should be used in conjunction with the official standard and relevant regulatory guidance for medical device software development.*
# 62304:2006 - Medical Device Software - Software Life Cycle Processes

## Table of Contents

1. [Scope](#1-scope)
2. [Normative References](#2-normative-references)
3. [Terms and Definitions](#3-terms-and-definitions)
4. [General Requirements](#4-general-requirements)
5. [Software Development Planning](#5-software-development-planning)
6. [Software Requirements Analysis](#6-software-requirements-analysis)
7. [Software Architectural Design](#7-software-architectural-design)
8. [Software Detailed Design](#8-software-detailed-design)
9. [Software Implementation](#9-software-implementation)
10. [Software Integration and Integration Testing](#10-software-integration-and-integration-testing)
11. [Software System Testing](#11-software-system-testing)
12. [Software Release](#12-software-release)
13. [Software Maintenance Process](#13-software-maintenance-process)
14. [Software Risk Management Process](#14-software-risk-management-process)
15. [Software Configuration Management Process](#15-software-configuration-management-process)
16. [Software Problem Resolution Process](#16-software-problem-resolution-process)

---

## 1. Scope

### 1.1 General

This International Standard specifies life cycle processes for medical device software. The set of processes, activities, and tasks described in this standard establishes a common framework for medical device software life cycle processes.

This International Standard applies to medical device software and to software within medical devices. It does not apply to:
- software used in the manufacturing of medical devices;
- software used in software tools that are not integral to the finished medical device;
- software that is itself a medical device or accessory to a medical device.

### 1.2 Relationship to Quality Management Systems

The medical device manufacturer shall incorporate the software life cycle processes described in this standard into their quality management system. The processes in this standard are intended to be used in conjunction with ISO 13485.

### 1.3 Software Safety Classification

This standard defines three safety classes for medical device software based on the possible consequences of software failure:

#### Class A: Non-Injury or Damage
Software that cannot contribute to a hazardous situation.

#### Class B: Non-Life-Threatening Injury  
Software that could contribute to a non-life-threatening injury or damage to the patient, operator, or the environment.

#### Class C: Death or Serious Injury
Software that could contribute to death or serious injury to the patient, operator, or the environment.

---

## 2. Normative References

The following referenced documents are indispensable for the application of this document:

- **ISO 13485**, Medical devices — Quality management systems — Requirements for regulatory purposes
- **ISO 14971**, Medical devices — Application of risk management to medical devices  
- **IEC 60601-1**, Medical electrical equipment — Part 1: General requirements for basic safety and essential performance
- **ISO 9000**, Quality management systems — Fundamentals and vocabulary

---

## 3. Terms and Definitions

### 3.1 Anomaly
Any condition that deviates from the expected based on requirements specifications, design documents, user documents, standards, etc., or from someone's perceptions or experiences.

### 3.2 COTS Software (Commercial Off-The-Shelf)
Software defined by a manufacturer for commercial use that the medical device manufacturer cannot modify.

### 3.3 Medical Device Software
Software that is itself a medical device or software that is an integral part of the medical device.

### 3.4 PEMS (Programmable Electrical Medical System)
A medical electrical system that incorporates one or more programmable electronic subsystems (PESS).

### 3.5 Software Configuration Item
An aggregation of software or hardware, or both, that is designated for configuration management and treated as a single entity in the configuration management process.

### 3.6 Software Item
An identifiable part of a computer program (e.g., module, object, subroutine, or other discrete programming element).

### 3.7 Software System
An integrated collection of software items organized to accomplish a specific function or set of functions.

### 3.8 Software Unit
Software item that cannot be subdivided into other items.

### 3.9 Verification
Confirmation, through the provision of objective evidence, that specified requirements have been fulfilled.

### 3.10 Validation
Confirmation, through the provision of objective evidence, that the requirements for a specific intended use or application have been fulfilled.

---

## 4. General Requirements

### 4.1 Quality Management System

The manufacturer shall establish, document, implement, and maintain a quality management system that includes the medical device software life cycle processes specified in this International Standard.

### 4.2 Software Life Cycle Process

The manufacturer shall implement a software life cycle process that includes the following activities:
- Software development planning
- Software requirements analysis  
- Software architectural design
- Software detailed design
- Software implementation
- Software integration and integration testing
- Software system testing
- Software release

### 4.3 Planning

#### 4.3.1 Software Development Plan

The manufacturer shall document software life cycle processes in a software development plan for each software system.

**Required Contents:**
- Purpose and scope of the software
- Assumptions and constraints
- Life cycle model
- Organization and responsibilities
- Resource and schedule allocations
- Methods and tools
- Risk management activities
- Software configuration management activities
- Software problem resolution activities
- Software verification and validation activities
- Documentation requirements

#### 4.3.2 Software Development Standards and Methods

The manufacturer shall establish standards and methods to be used during the software development process, including:
- Software design methods
- Language selection criteria  
- Coding standards
- Performance criteria
- Verification and validation methods
- Documentation standards

### 4.4 Software Safety Classification

#### 4.4.1 Assign Safety Classes

The manufacturer shall assign a safety class to each software system based on the possible consequences resulting from a hazard that the software system could contribute to.

**Classification Process:**
1. Identify the hazards that could arise from the medical device
2. Determine the contribution of the software system to each hazard
3. Apply the safety classification based on severity of harm

#### 4.4.2 Segregation of Software Items

Where software items of different safety classes exist in the same software system, the manufacturer shall either:
- Assign the highest safety class to the entire system, or
- Segregate software items and apply class-specific requirements to each segregated portion

---

## 5. Software Development Planning

### 5.1 General

The manufacturer shall plan the software development process for each software system.

### 5.2 Plan the Software Development Process

#### 5.2.1 Software Development Plan

The manufacturer shall document the software development process in a software development plan.

**Minimum Content Requirements:**

**A) Purpose and Scope**
- Identification of the software system
- Intended use and operational environment
- Hardware and software operating environment

**B) Organization and Responsibilities**  
- Organizational structure for the project
- Tasks and responsibilities of each organization
- Personnel qualifications and training requirements

**C) Relationship to Overall Medical Device Development**
- Integration with overall medical device development
- Interface to medical device risk management
- Interface to medical device design controls

**D) Software Development Standards**
- Applicable development standards and methods
- Programming languages and tools
- Documentation standards
- Verification and validation standards

**E) Software Life Cycle Model**
- Description of the chosen life cycle model
- Definition of life cycle phases and deliverables
- Entry and exit criteria for each phase

**F) Verification and Validation**
- Verification and validation planning
- Methods and tools for V&V activities
- Acceptance criteria for each life cycle phase

**G) Risk Management Activities**
- Software risk management process
- Integration with medical device risk management
- Risk control measures

**H) Configuration Management**
- Configuration identification scheme
- Change control procedures
- Configuration status accounting

**I) Problem Resolution**
- Problem reporting and tracking procedures
- Problem analysis and resolution methods
- Corrective and preventive action procedures

#### 5.2.2 Keep Software Development Plan Current

The manufacturer shall keep the software development plan current throughout the software development process.

### 5.3 Software Development Life Cycle Model

#### 5.3.1 Select Life Cycle Model

The manufacturer shall select an appropriate life cycle model for the development of the software system, considering:
- Project size and complexity
- Project organization structure  
- Requirements stability
- Technology and platform considerations
- Risk factors

**Common Life Cycle Models:**
- **Waterfall Model**: Sequential phases with formal reviews
- **V-Model**: Emphasizes verification and validation
- **Incremental Model**: Multiple software releases
- **Spiral Model**: Risk-driven iterative approach
- **Agile Models**: Iterative and incremental with frequent customer interaction

#### 5.3.2 Document Selected Model

The manufacturer shall document the rationale for selecting the life cycle model and describe how the selected model addresses the specific needs of the project.

---

## 6. Software Requirements Analysis

### 6.1 General

The manufacturer shall analyze and document the software requirements for each software system.

### 6.2 Define and Document Software Requirements

#### 6.2.1 Functional and Capability Requirements

The manufacturer shall define and document software requirements including:

**A) Functional Requirements**
- Input/output specifications
- Data formats and ranges
- Processing requirements  
- Response times and throughput
- System behavior under various conditions

**B) Performance Requirements**
- Response time requirements
- Throughput requirements
- Memory utilization requirements
- Resource usage requirements

**C) Interface Requirements**
- Hardware interfaces
- Software interfaces  
- User interfaces
- Communication interfaces

**D) Clinical Performance Requirements**
- Accuracy requirements
- Precision requirements
- Clinical safety requirements
- Usability requirements

#### 6.2.2 Software System Inputs and Outputs

The manufacturer shall identify and document:
- All software system inputs and their acceptable ranges
- All software system outputs and their acceptable ranges  
- Relationships between inputs and outputs
- Invalid input handling requirements

#### 6.2.3 Software Requirements Content

Each software requirement shall be:
- **Unambiguous**: Clear and precise meaning
- **Traceable**: To system requirements and risk analysis
- **Testable**: Verifiable through testing or analysis
- **Feasible**: Technically achievable within constraints
- **Necessary**: Required for the intended use

#### 6.2.4 Include Risk Control Measures

The manufacturer shall include in software requirements the risk control measures identified by the risk management process.

#### 6.2.5 Software Requirements Updating

The manufacturer shall establish procedures for updating software requirements, including:
- Change control procedures
- Impact analysis requirements
- Re-verification and re-validation requirements
- Traceability maintenance

### 6.3 Software Requirements Verification

#### 6.3.1 Verify Software Requirements

The manufacturer shall verify software requirements to ensure they:
- Implement system requirements allocated to software
- Are complete, unambiguous, and consistent
- Do not contradict each other
- Are expressed in terms that permit establishment of test criteria
- Address risk control measures

**Verification Methods:**
- **Reviews and Analysis**: Systematic examination by qualified personnel
- **Prototyping**: Demonstrations of feasibility and completeness  
- **Modeling**: Mathematical or simulation models
- **Testing**: Execution of requirement-based test cases

#### 6.3.2 Document Verification Results

The manufacturer shall document the results of software requirements verification, including:
- Verification methods used
- Results obtained
- Actions taken to address deficiencies
- Evidence of requirement compliance

### 6.4 Software Requirements Approval

#### 6.4.1 Obtain Approval

The manufacturer shall obtain approval of software requirements from authorized personnel before proceeding with architectural design.

#### 6.4.2 Approval Documentation

The approval documentation shall include:
- Identity of approving personnel
- Date of approval
- Version of requirements approved
- Any conditions or constraints on the approval

---

## 7. Software Architectural Design

### 7.1 General

The manufacturer shall transform software requirements into an architecture that describes the software's top-level structure and identifies the principal structural elements and their interfaces.

### 7.2 Develop Software Architecture

#### 7.2.1 Transform Requirements into Architecture

The manufacturer shall develop a software architecture from the software requirements that:
- Defines the major structural elements of the software
- Describes interfaces between elements
- Allocates software requirements to structural elements
- Addresses non-functional requirements (performance, reliability, security)

#### 7.2.2 Document Software Architecture  

The manufacturer shall document the software architecture including:

**A) Architectural Design Description**
- Overview of the software architecture
- Identification of major software components
- Component responsibilities and functionality
- Interfaces between components

**B) Interface Specifications**
- Internal interfaces between software components
- External interfaces to hardware and other software
- User interfaces
- Communication interfaces

**C) Segregation Between Software Items**
- Separation of software items of different safety classes
- Isolation mechanisms and barriers
- Access control and permission models

### 7.3 Develop Each Software Item

#### 7.3.1 Refine the Software Architecture

For each software item identified in the architecture, the manufacturer shall:
- Refine the software item requirements
- Develop software item interfaces  
- Select appropriate algorithms and data structures
- Identify relationships with other software items

#### 7.3.2 Specify Software Item Interfaces

The manufacturer shall specify interfaces for each software item including:
- Function interfaces and parameter specifications
- Data interfaces and format specifications
- Control interfaces and timing requirements  
- Error handling and exception interfaces

### 7.4 Software Architectural Design Verification

#### 7.4.1 Verify Software Architecture

The manufacturer shall verify that the software architecture:
- Implements software requirements
- Is able to support interfaces defined in system design
- Is appropriate for the medical device
- Supports proper operation of any SOUP (Software of Unknown Provenance)

**Verification Methods:**
- **Design Reviews**: Systematic examination by qualified personnel
- **Prototyping**: Architectural proof-of-concept demonstrations
- **Analysis**: Mathematical analysis or simulation
- **Testing**: Architecture-level testing

#### 7.4.2 Document Verification Results

The manufacturer shall document architectural design verification results, including:
- Verification activities performed
- Results of verification
- Actions taken to address findings
- Evidence of architecture adequacy

---

## 8. Software Detailed Design

### 8.1 General

The manufacturer shall develop a detailed design for each software item sufficient for that software item to be implemented and tested.

### 8.2 Refine the Software Architecture into a Detailed Design

#### 8.2.1 Detailed Design Development

The manufacturer shall develop detailed design for each software item that:
- Refines the software architecture
- Defines software units and their interfaces  
- Describes data structures and algorithms
- Specifies timing and resource requirements

#### 8.2.2 Document Detailed Design

The manufacturer shall document the detailed design including:

**A) Software Item Design Description**
- Purpose and functionality of the software item
- Inputs, outputs, and processing logic
- Data structures and variable definitions
- Algorithm descriptions and flowcharts

**B) Interface Specifications**
- Detailed interface definitions
- Parameter specifications and data types
- Error handling procedures
- Timing and sequencing requirements

**C) Software Unit Specifications**  
- Software unit identification and purpose
- Unit interfaces and dependencies
- Unit implementation requirements
- Unit testing requirements

### 8.3 Software Detailed Design Verification

#### 8.3.1 Verify Detailed Design

The manufacturer shall verify that the detailed design:
- Implements the software architecture
- Is free from contradiction
- Uses appropriate algorithms and data structures
- Supports the intended medical device functionality

**Verification Methods:**
- **Design Reviews**: Peer reviews and formal inspections
- **Analysis**: Static analysis and design rule checking
- **Prototyping**: Detailed design demonstrations
- **Modeling**: Design simulation and mathematical modeling

#### 8.3.2 Document Verification Results

The manufacturer shall document detailed design verification including:
- Verification methods applied
- Results and findings
- Resolution of identified issues  
- Evidence of design adequacy

---

## 9. Software Implementation

### 9.1 General

The manufacturer shall implement each software unit and document the implementation.

### 9.2 Implement Each Software Unit

#### 9.2.1 Software Implementation

The manufacturer shall implement each software unit according to the detailed design and document the implementation.

**Implementation Requirements:**
- Transform detailed design into executable code
- Follow established coding standards and guidelines
- Implement all specified functionality
- Include appropriate error handling
- Maintain traceability to design specifications

#### 9.2.2 Implementation Documentation

The manufacturer shall document software implementation including:
- Source code with appropriate comments
- Build procedures and compilation instructions
- Implementation decisions and rationale
- Traceability to detailed design

### 9.3 Software Unit Verification (Unit Testing)

#### 9.3.1 Establish Software Unit Verification Process

The manufacturer shall establish a software unit verification process including:
- Unit test planning and procedures
- Test environment setup and configuration
- Test data preparation and management
- Test execution and result documentation

#### 9.3.2 Software Unit Testing

The manufacturer shall test each software unit to verify that it:
- Implements the software detailed design
- Functions correctly according to specifications
- Handles boundary conditions appropriately
- Reports errors appropriately

**Unit Testing Techniques:**
- **Statement Coverage**: All executable statements executed
- **Branch Coverage**: All decision branches exercised  
- **Condition Coverage**: All Boolean conditions evaluated
- **Path Coverage**: All execution paths tested (where practical)

#### 9.3.3 Unit Testing for Different Safety Classes

**Class A Software Units**
- Test all software units

**Class B Software Units**  
- Test all software units
- Achieve statement coverage or branch coverage

**Class C Software Units**
- Test all software units  
- Achieve statement coverage and branch coverage
- Additional path coverage where practical

#### 9.3.4 Document Unit Testing

The manufacturer shall document unit testing including:
- Test procedures and test cases
- Test results and coverage achieved
- Issues identified and resolved
- Evidence of unit verification

---

## 10. Software Integration and Integration Testing

### 10.1 General  

The manufacturer shall integrate software units into software items and conduct integration testing to verify that the integrated software items meet their specified requirements.

### 10.2 Integrate Software Units

#### 10.2.1 Integration Strategy

The manufacturer shall develop an integration strategy that defines:
- Order of software unit integration
- Integration approach (big bang, incremental, etc.)
- Integration test environments and tools
- Integration success criteria

#### 10.2.2 Integration Implementation  

The manufacturer shall integrate software units according to the integration strategy and:
- Follow integration procedures
- Resolve integration issues as they arise
- Document integration activities
- Maintain version control

### 10.3 Software Integration Testing

#### 10.3.1 Develop Integration Test Procedures

The manufacturer shall develop integration test procedures that:
- Verify interfaces between software units
- Test integrated functionality
- Verify data flow between units
- Test error handling across interfaces

#### 10.3.2 Conduct Integration Testing

The manufacturer shall conduct integration testing using the developed procedures and:
- Execute all planned integration tests
- Document test results
- Investigate and resolve test failures
- Achieve specified coverage criteria

#### 10.3.3 Integration Testing for Different Safety Classes

**Class A Software Items**
- Develop test procedures based on software architectural design
- Conduct planned integration tests

**Class B Software Items**
- Develop test procedures based on software architectural design  
- Conduct planned integration tests
- Test sequences of software functions

**Class C Software Items**
- Develop test procedures based on software architectural design
- Conduct planned integration tests
- Test sequences of software functions
- Test all interfaces between software items

#### 10.3.4 Document Integration Testing

The manufacturer shall document integration testing including:
- Integration test procedures
- Test execution records
- Test results and any failures
- Corrective actions taken
- Evidence of successful integration

---

## 11. Software System Testing

### 11.1 General

The manufacturer shall conduct software system testing to provide evidence that the completed software system meets its specified requirements.

### 11.2 Establish Software System Test Procedures

#### 11.2.1 Develop System Test Procedures  

The manufacturer shall establish test procedures for software system testing that:
- Are based on software requirements
- Include both normal and abnormal operating conditions
- Include boundary value testing
- Include stress testing where appropriate
- Test risk control measures

#### 11.2.2 System Test Environment

The manufacturer shall establish a system test environment that:
- Represents the intended operating environment
- Includes representative hardware configuration
- Includes representative software configuration
- Supports test execution and result capture

### 11.3 Conduct Software System Testing

#### 11.3.1 Execute System Tests

The manufacturer shall execute software system tests and:
- Follow established test procedures
- Document test results
- Investigate and resolve test failures
- Re-test after corrective actions

#### 11.3.2 System Testing for Different Safety Classes

**Class A Software Systems**
- Execute system test procedures based on software requirements

**Class B Software Systems**
- Execute system test procedures based on software requirements  
- Test software system functions in sequence

**Class C Software Systems**
- Execute system test procedures based on software requirements
- Test software system functions in sequence
- Conduct additional testing for risk control measures

#### 11.3.3 Document System Testing

The manufacturer shall document system testing including:
- System test procedures and test cases
- Test execution records and results
- Analysis of test results
- Corrective actions for any failures
- Evidence of requirement satisfaction

---

## 12. Software Release

### 12.1 General

The manufacturer shall ensure that software system requirements have been satisfied and that the software system is ready for release.

### 12.2 Ensure Software Verification is Complete

#### 12.2.1 Verify All Activities Complete

Before software release, the manufacturer shall verify that:
- All planned software life cycle processes have been completed
- All software verification activities have been completed  
- All identified problems have been resolved or documented
- All documentation is complete and approved

#### 12.2.2 Document Verification Status

The manufacturer shall document the verification status including:
- Summary of verification activities performed
- Outstanding issues and their disposition  
- Overall assessment of software system readiness
- Recommendation for release approval

### 12.3 Document Known Residual Anomalies

#### 12.3.1 Anomaly Documentation

The manufacturer shall document known residual anomalies including:
- Description of each anomaly
- Analysis of potential impact
- Risk assessment and mitigation measures
- Justification for release with known anomalies

#### 12.3.2 Anomaly Evaluation

For each known anomaly, the manufacturer shall evaluate:
- Safety impact and risk level
- Functional impact on intended use
- Workarounds or limitations  
- Need for corrective action

### 12.4 Ensure Software Configuration Management

#### 12.4.1 Configuration Baseline

The manufacturer shall establish and maintain a configuration baseline that includes:
- All software items being released
- Version identification for each item
- Configuration documentation
- Associated documentation versions

#### 12.4.2 Release Documentation

The manufacturer shall prepare release documentation including:
- Release identification and version information
- Installation and upgrade procedures  
- Known limitations and anomalies
- User documentation and training materials

### 12.5 Archive Software

#### 12.5.1 Archive Requirements

The manufacturer shall archive the following items:
- Source code and executable software
- Documentation produced during development
- Test procedures and results
- Configuration management records
- Development tools and environment information

#### 12.5.2 Archive Accessibility

The manufacturer shall ensure archived information:
- Remains accessible throughout software lifecycle
- Is protected from unauthorized modification
- Is backed up and recoverable
- Includes retrieval procedures and responsibilities

---

## 13. Software Maintenance Process

### 13.1 General

The manufacturer shall establish a software maintenance process to provide for the modification of released software while preserving its integrity.

### 13.2 Establish Software Maintenance Plan

#### 13.2.1 Maintenance Planning

The manufacturer shall establish and maintain a software maintenance plan that addresses:
- Maintenance organization and responsibilities
- Problem analysis and modification procedures
- Modification approval and implementation procedures  
- Maintenance verification and validation
- Impact analysis procedures
- Configuration management for maintenance
- Maintenance records and documentation

#### 13.2.2 Maintenance Process Integration

The maintenance process shall be integrated with:
- Medical device risk management process
- Software configuration management process
- Software problem resolution process
- Medical device surveillance and feedback systems

### 13.3 Problem and Modification Analysis

#### 13.3.1 Analyze Feedback and Problems

The manufacturer shall analyze feedback and problems to determine:
- Root cause of problems
- Impact on safety and effectiveness
- Need for modification or corrective action
- Priority for addressing problems

#### 13.3.2 Evaluate Modification Impact

For each proposed modification, the manufacturer shall evaluate:
- Impact on software system safety and effectiveness
- Impact on other software items and system components
- Verification and validation requirements  
- Risk-benefit analysis of the modification

### 13.4 Modification Implementation

#### 13.4.1 Implement Modifications

The manufacturer shall implement approved modifications using:
- Appropriate software life cycle processes
- Configuration management procedures
- Change control procedures
- Verification and validation activities appropriate to the change

#### 13.4.2 Verification and Validation of Changes

The manufacturer shall verify and validate software modifications including:
- Testing of modified software items
- Regression testing to ensure unintended changes have not occurred
- System testing where modifications affect system behavior
- Risk analysis updates where appropriate

### 13.5 Software Maintenance Documentation

#### 13.5.1 Document Maintenance Activities

The manufacturer shall document maintenance activities including:
- Problem reports and analysis results
- Modification requests and approvals
- Implementation procedures and results
- Verification and validation results
- Updated software documentation

#### 13.5.2 Update Software Documentation

The manufacturer shall update software documentation affected by maintenance modifications to ensure:
- Documentation accurately reflects modified software
- Traceability is maintained
- Version control is applied to documentation
- Distribution of updated documentation is controlled

---

## 14. Software Risk Management Process

### 14.1 General

The manufacturer shall apply risk management to software in accordance with ISO 14971 and establish procedures for risk management activities during software development and maintenance.

### 14.2 Risk Management Process

#### 14.2.1 Risk Management Integration

The software risk management process shall be integrated with:
- Overall medical device risk management process
- Software development life cycle processes
- Software configuration management process
- Software problem resolution process

#### 14.2.2 Risk Management Planning

The manufacturer shall plan software risk management activities including:
- Risk management responsibilities and authority
- Risk management procedures and methods
- Risk analysis and evaluation criteria
- Risk control and risk acceptance criteria
- Risk management file maintenance

### 14.3 Risk Analysis

#### 14.3.1 Hazard Identification

The manufacturer shall identify known and foreseeable hazards associated with the software, considering:
- Intended use and reasonably foreseeable misuse
- Software malfunction or unexpected behavior
- Hardware/software interaction hazards
- User interface and usability hazards
- Data corruption or loss hazards

#### 14.3.2 Risk Estimation

For each identified hazard, the manufacturer shall estimate:
- Severity of potential harm
- Probability of occurrence of harm
- Risk level using predefined risk evaluation criteria

#### 14.3.3 Software Contribution to Hazardous Situations

The manufacturer shall analyze how software could contribute to hazardous situations through:
- Failure to perform intended function
- Performance of unintended function  
- Performance of intended function at wrong time
- Performance of intended function with wrong magnitude/duration

### 14.4 Risk Control

#### 14.4.1 Risk Control Option Analysis

For unacceptable risks, the manufacturer shall evaluate risk control options in the following priority order:
1. **Safety by design**: Eliminate or reduce risk through design
2. **Protective measures**: Implement protective measures in software or device
3. **Information for safety**: Provide information to users about residual risks

#### 14.4.2 Risk Control Implementation

The manufacturer shall implement selected risk control measures and:
- Verify effectiveness of risk control measures
- Ensure risk control measures do not introduce new hazards
- Ensure residual risks are acceptable
- Document risk control implementation and verification

#### 14.4.3 Risk Control Verification

The manufacturer shall verify that risk control measures:
- Have been implemented correctly
- Are effective in reducing risk to acceptable levels
- Do not introduce unacceptable risks
- Do not impair safety or effectiveness

### 14.5 Risk Management File

#### 14.5.1 Establish Risk Management File

The manufacturer shall establish and maintain a risk management file that contains:
- Risk management plan
- Records of risk analysis activities
- Risk control measures and their verification
- Risk-benefit analysis documentation
- Risk management report

#### 14.5.2 Risk Management File Contents

The risk management file shall include:
- Hazard identification and risk analysis results
- Risk evaluation and acceptability decisions
- Risk control measures and implementation verification
- Residual risk assessment and acceptability
- Post-market surveillance plan for risk-related information

---

## 15. Software Configuration Management Process

### 15.1 General

The manufacturer shall establish a software configuration management process to provide for the identification, control, and tracking of software configuration items throughout the software life cycle.

### 15.2 Configuration Management Planning

#### 15.2.1 Configuration Management Plan

The manufacturer shall establish a configuration management plan that defines:
- Configuration management organization and responsibilities
- Configuration identification scheme
- Configuration control procedures
- Configuration status accounting procedures
- Configuration auditing procedures
- Release and delivery procedures

#### 15.2.2 Configuration Management Tools and Environment

The manufacturer shall establish configuration management tools and environment including:
- Version control system and procedures  
- Build and release management tools
- Change tracking and approval systems
- Backup and archive procedures
- Access control and security measures

### 15.3 Configuration Identification

#### 15.3.1 Identify Configuration Items

The manufacturer shall identify software configuration items including:
- Software requirements documents
- Software design documents
- Source code files and modules
- Executable software
- Test procedures and test data
- User documentation

#### 15.3.2 Configuration Item Naming and Versioning

The manufacturer shall establish naming and versioning schemes that:
- Uniquely identify each configuration item
- Indicate the version or revision level
- Support traceability between related items
- Enable automated configuration management

### 15.4 Configuration Control

#### 15.4.1 Change Control Process

The manufacturer shall establish a change control process that includes:
- Change request procedures and forms
- Change impact analysis requirements
- Change review and approval procedures
- Change implementation and verification procedures
- Change documentation and tracking

#### 15.4.2 Baseline Management

The manufacturer shall establish and maintain baselines at key points including:
- Requirements baseline after requirements approval
- Design baseline after design approval  
- Implementation baseline after code completion
- Release baseline for each software release

#### 15.4.3 Branch and Merge Management

For development environments with multiple parallel activities, the manufacturer shall:
- Establish branching and merging procedures
- Control access to different branches
- Ensure proper integration of parallel changes
- Maintain traceability across branches

### 15.5 Configuration Status Accounting

#### 15.5.1 Configuration Status Information

The manufacturer shall maintain configuration status information including:
- Current version identification of all configuration items
- Status of change requests and their implementation
- Build and release history
- Problem reports associated with each configuration item

#### 15.5.2 Configuration Reports

The manufacturer shall generate configuration status reports including:
- Configuration item status reports
- Change status reports  
- Build and release reports
- Configuration differences between versions

### 15.6 Configuration Audit

#### 15.6.1 Configuration Verification

The manufacturer shall conduct configuration audits to verify:
- Configuration item completeness and correctness
- Consistency between related configuration items
- Proper implementation of changes
- Compliance with configuration management procedures

#### 15.6.2 Audit Documentation

The manufacturer shall document configuration audit results including:
- Items audited and audit criteria
- Audit findings and discrepancies
- Corrective actions taken
- Verification of corrective action effectiveness

---

## 16. Software Problem Resolution Process

### 16.1 General

The manufacturer shall establish a software problem resolution process to ensure that problems found in software are analyzed, documented, and resolved.

### 16.2 Prepare Problem Resolution Process

#### 16.2.1 Problem Resolution Plan

The manufacturer shall establish a problem resolution plan that defines:
- Problem reporting procedures and responsibilities
- Problem classification and prioritization criteria
- Problem analysis and resolution procedures
- Problem tracking and status reporting
- Communication procedures for problem resolution
- Integration with risk management process

#### 16.2.2 Problem Resolution Organization

The manufacturer shall establish problem resolution organization including:
- Roles and responsibilities for problem resolution
- Authority levels for problem resolution decisions
- Escalation procedures for critical problems
- Resource allocation for problem resolution activities

### 16.3 Problem Reporting

#### 16.3.1 Problem Identification and Reporting

The manufacturer shall establish procedures for identifying and reporting problems including:
- Sources of problem reports (testing, field use, etc.)
- Problem report format and required information
- Problem report submission and acknowledgment procedures
- Initial problem classification and priority assignment

#### 16.3.2 Problem Report Content

Each problem report shall include:
- Unique problem identification
- Problem description and symptoms  
- Software configuration identification
- Steps to reproduce the problem
- Impact assessment and severity classification
- Reporter identification and contact information

### 16.4 Problem Investigation

#### 16.4.1 Problem Analysis

The manufacturer shall analyze each problem to determine:
- Root cause of the problem
- Impact on safety, effectiveness, and performance
- Affected software configuration items
- Potential solutions and their implications
- Priority for problem resolution

#### 16.4.2 Change Request Generation

Based on problem analysis, the manufacturer shall:
- Generate change requests for problem resolution
- Evaluate change impact and risk
- Obtain appropriate approvals for changes
- Coordinate with configuration management process

### 16.5 Problem Resolution Implementation

#### 16.5.1 Implement Resolution

The manufacturer shall implement approved problem resolutions using:
- Appropriate software life cycle processes
- Change control procedures
- Verification and validation activities
- Risk assessment updates where necessary

#### 16.5.2 Resolution Verification

The manufacturer shall verify that problem resolutions:
- Effectively address the reported problem
- Do not introduce new problems
- Do not adversely affect other software functions
- Meet all specified requirements and constraints

### 16.6 Problem Resolution Documentation

#### 16.6.1 Document Resolution Activities

The manufacturer shall document problem resolution activities including:
- Problem analysis results and root cause determination
- Resolution approach and rationale
- Changes made to resolve the problem  
- Verification and validation results
- Problem closure and approval

#### 16.6.2 Problem Trending and Analysis

The manufacturer shall analyze problem trends to identify:
- Recurring problem patterns
- Areas of software requiring improvement  
- Process improvements for problem prevention
- Training needs and opportunities
- Risk management implications

---

## Annexes

### Annex A (Informative) - Software Life Cycle Model Examples

This annex provides examples of different software life cycle models that may be appropriate for medical device software development.

#### A.1 Waterfall Model

**Characteristics:**
- Sequential phases with defined deliverables
- Each phase completed before next phase begins
- Extensive documentation at each phase
- Formal reviews and approvals between phases

**Advantages:**
- Simple and easy to understand
- Well-defined milestones and deliverables  
- Good for projects with stable requirements
- Comprehensive documentation

**Disadvantages:**
- Limited flexibility for requirement changes
- Late discovery of integration issues
- Customer feedback only at end of project

#### A.2 V-Model  

**Characteristics:**
- Extension of waterfall model
- Emphasizes verification and validation
- Each development phase has corresponding test phase
- Test planning occurs early in development

**Advantages:**  
- Strong emphasis on testing and quality
- Early test planning reduces defects
- Clear traceability between requirements and tests
- Good for safety-critical applications

**Disadvantages:**
- Still inflexible to requirement changes
- Can be resource intensive
- May be overly rigid for some projects

#### A.3 Incremental Model

**Characteristics:**
- Software developed in increments
- Each increment adds functionality
- Core functionality implemented first
- Incremental integration and testing

**Advantages:**
- Early delivery of core functionality
- Reduced risk through incremental approach
- Easier to accommodate requirement changes
- Early user feedback possible

**Disadvantages:**
- Requires careful architectural planning
- May require more resources for integration
- Can be complex to manage multiple increments

#### A.4 Spiral Model

**Characteristics:**
- Risk-driven iterative approach
- Four main activities: planning, risk analysis, engineering, evaluation
- Multiple cycles through the spiral
- Prototyping used to manage risk

**Advantages:**
- Excellent for high-risk projects
- Early identification and mitigation of risks
- Flexibility to accommodate changes
- Customer involvement throughout process

**Disadvantages:**
- Can be complex to manage
- Requires expertise in risk assessment
- May be expensive due to repeated activities
- Success depends on risk analysis skills

### Annex B (Informative) - Software Safety Classification Examples  

This annex provides examples to help determine appropriate safety classification for medical device software.

#### B.1 Class A Examples

Software that cannot contribute to a hazardous situation:

- Patient database software that only stores and retrieves patient demographic information
- Hospital scheduling software for appointment management
- Medical device inventory management software
- Basic calculator functions in medical devices
- Software for generating standard reports

#### B.2 Class B Examples  

Software that could contribute to a non-life-threatening injury:

- Blood pressure monitor software with incorrect readings leading to inappropriate treatment
- Software controlling non-critical device functions that could cause minor patient discomfort
- Medication reminder software that could lead to missed doses
- Medical imaging software with display errors that could affect diagnosis
- Laboratory equipment software that could produce inaccurate but non-critical results

#### B.3 Class C Examples

Software that could contribute to death or serious injury:

- Infusion pump software controlling drug delivery rates
- Ventilator control software managing patient breathing
- Pacemaker or defibrillator software controlling cardiac therapy
- Radiation therapy software controlling beam delivery
- Surgical robot control software
- Life support system software
- Software in devices used in emergency medical situations

### Annex C (Informative) - SOUP (Software of Unknown Provenance) Considerations

This annex provides guidance for using SOUP in medical device software.

#### C.1 SOUP Identification and Documentation

For each SOUP item, document:
- Identity and version of SOUP item
- Purpose and function of SOUP in the medical device
- Hardware and software requirements for SOUP
- SOUP supplier and support arrangements
- Known anomalies and limitations

#### C.2 SOUP Risk Analysis

Analyze risks associated with SOUP including:
- Functional risks from SOUP malfunction
- Security risks from SOUP vulnerabilities  
- Integration risks with medical device software
- Maintenance and support risks
- Obsolescence and availability risks

#### C.3 SOUP Validation

Validate SOUP for its intended use including:
- Functional testing in the medical device environment
- Performance testing under expected conditions
- Interface testing with medical device software
- Stress testing and error condition testing
- Security testing where appropriate

### Annex D (Informative) - Documentation Templates

This annex provides templates and examples for key documents required by this standard.

#### D.1 Software Development Plan Template

1. **Introduction and Scope**
2. **Organization and Resources**  
3. **Standards and Methods**
4. **Life Cycle Model**
5. **Risk Management**
6. **Configuration Management**
7. **Verification and Validation**
8. **Problem Resolution**
9. **Documentation Plan**
10. **Quality Assurance**

#### D.2 Software Requirements Specification Template

1. **Introduction**
2. **General Description**
3. **Functional Requirements**
4. **Performance Requirements**  
5. **Interface Requirements**
6. **Design Constraints**
7. **Quality Attributes**
8. **Verification Criteria**

#### D.3 Software Architecture Document Template

1. **Introduction**
2. **Architectural Overview**
3. **System Context**  
4. **Architectural Design**
5. **Interface Specifications**
6. **Data Architecture**
7. **Security Architecture**
8. **Deployment Architecture**

#### D.4 Test Plan Template

1. **Introduction**
2. **Test Strategy**
3. **Test Environment**
4. **Test Cases and Procedures** 
5. **Test Schedule**
6. **Test Resources**
7. **Risk Assessment**  
8. **Approval Criteria**

---

## Bibliography

- **ISO 13485:2016**, Medical devices — Quality management systems — Requirements for regulatory purposes
- **ISO 14971:2019**, Medical devices — Application of risk management to medical devices
- **IEC 60601-1:2012**, Medical electrical equipment — Part 1: General requirements for basic safety and essential performance
- **ISO 9000:2015**, Quality management systems — Fundamentals and vocabulary
- **IEC 80001-1:2010**, Application of risk management for IT-networks incorporating medical devices
- **FDA Guidance for the Content of Premarket Submissions for Software Contained in Medical Devices**, 2005
- **AAMI TIR36:2007**, Validation of software for regulated processes
- **IEEE 829-2008**, Standard for Software and System Test Documentation
- **IEEE 1012-2016**, Standard for System, Software, and Hardware Verification and Validation

---

