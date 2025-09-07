# IEC 62304: Software Implementation and Testing

## Module Overview
This module covers the complete software implementation process, comprehensive unit testing, integration testing, and system testing phases. It focuses on transforming detailed designs into verified, tested software systems ready for medical device deployment.

**Key Areas Covered:**
- Software implementation and documentation
- Comprehensive unit testing strategies
- Integration testing methodologies
- System testing and validation approaches

---

## 9. Software Implementation

### 9.1 General

The manufacturer shall implement each software unit and document the implementation.

**AI-Agent Implementation Support Framework**:
- Code generation from detailed design specifications
- Automated coding standard compliance checking
- Real-time implementation progress tracking
- Integration with version control and build systems

### 9.2 Implement Each Software Unit

#### 9.2.1 Software Implementation

The manufacturer shall implement each software unit according to the detailed design and document the implementation.

**Implementation Requirements Framework**:
```yaml
transformation_requirements:
  design_to_code:
    - faithful_translation: "Preserve all design decisions in code"
    - traceability_maintenance: "Maintain links between design elements and code"
    - design_intent_preservation: "Code should express design intentions clearly"
    
coding_standards_compliance:
  style_guidelines:
    - naming_conventions: "Consistent identifier naming"
    - formatting_standards: "Code layout and indentation"
    - commenting_standards: "Documentation within code"
  safety_rules:
    - memory_management: "Safe allocation and deallocation"
    - error_handling: "Comprehensive error detection and response"
    - defensive_programming: "Input validation and boundary checking"
    
functionality_implementation:
  completeness:
    - all_specified_functions: "Implement every function in the design"
    - all_specified_behaviours: "Implement all described behaviours"
    - all_interface_contracts: "Fulfill all interface specifications"
  correctness:
    - algorithm_fidelity: "Algorithms match design specifications"
    - data_structure_consistency: "Data structures match design"
    - interface_compliance: "Interfaces match specifications exactly"
    
error_handling_implementation:
  error_detection:
    - input_validation: "Check all inputs against constraints"
    - boundary_condition_detection: "Identify out-of-range conditions"
    - system_state_validation: "Verify system state assumptions"
  error_response:
    - graceful_degradation: "Maintain system stability during errors"
    - error_reporting: "Provide meaningful error information"
    - recovery_mechanisms: "Attempt to recover from recoverable errors"
    
traceability_maintenance:
  design_to_code_links:
    - requirement_traceability: "Link code to originating requirements"
    - design_element_mapping: "Map code structures to design elements"
    - decision_rationale: "Document why specific implementation choices were made"
```

**AI-Agent Implementation Process**:
```python
def implement_software_unit(unit_design, coding_standards, traceability_requirements):
    implementation = {
        "source_files": [],
        "documentation": {},
        "traceability_links": {},
        "compliance_status": {}
    }
    
    # Generate source code from design
    source_code = generate_code_from_design(
        unit_design, 
        coding_standards
    )
    
    # Apply coding standards
    compliant_code = apply_coding_standards(
        source_code, 
        coding_standards
    )
    
    # Add error handling
    robust_code = add_error_handling(
        compliant_code, 
        unit_design.error_specifications
    )
    
    # Generate documentation
    implementation["documentation"] = generate_implementation_documentation(
        unit_design, 
        robust_code
    )
    
    # Establish traceability
    implementation["traceability_links"] = establish_traceability(
        robust_code, 
        unit_design, 
        traceability_requirements
    )
    
    # Verify compliance
    implementation["compliance_status"] = verify_implementation_compliance(
        robust_code, 
        unit_design, 
        coding_standards
    )
    
    implementation["source_files"] = robust_code
    
    return implementation
```

#### 9.2.2 Implementation Documentation

The manufacturer shall document software implementation including:

**Implementation Documentation Template**:
```json
{
  "implementation_package": {
    "unit_identifier": "software_unit_name",
    "version": "implementation_version",
    "implementation_date": "ISO_date",
    "implementer": "developer_name",
    "design_reference": "detailed_design_document_reference"
  },
  
  "source_code_inventory": [
    {
      "file_name": "source_file_name",
      "file_type": "header|source|configuration|build_script",
      "purpose": "what_this_file_provides",
      "size_metrics": {
        "lines_of_code": "integer",
        "lines_of_comments": "integer",
        "complexity_measure": "cyclomatic_complexity_or_similar"
      },
      "dependencies": ["file1", "file2"],
      "interfaces_implemented": ["interface1", "interface2"]
    }
  ],
  
  "build_configuration": {
    "build_environment": {
      "compiler": "compiler_name_and_version",
      "build_tools": ["tool1", "tool2"],
      "target_platform": "platform_specification"
    },
    "build_settings": {
      "compilation_flags": ["flag1", "flag2"],
      "optimization_level": "optimization_setting",
      "debugging_symbols": "enabled|disabled",
      "static_analysis": "tools_and_settings_used"
    },
    "build_procedure": {
      "build_steps": ["step1", "step2"],
      "build_verification": "how_to_verify_successful_build",
      "build_artifacts": ["artifact1", "artifact2"]
    }
  },
  
  "implementation_decisions": [
    {
      "decision_category": "algorithm|data_structure|interface|error_handling|performance",
      "design_specification": "what_the_design_specified",
      "implementation_choice": "what_was_actually_implemented",
      "rationale": "why_this_choice_was_made",
      "alternatives_considered": ["alternative1", "alternative2"],
      "trade_offs": "benefits_and_drawbacks"
    }
  ],
  
  "traceability_matrix": [
    {
      "design_element": "design_element_identifier",
      "code_location": "file_name:line_number_or_function_name",
      "implementation_notes": "how_the_design_element_is_implemented"
    }
  ]
}
```

### 9.3 Software Unit Verification (Unit Testing)

#### 9.3.1 Establish Software Unit Verification Process

The manufacturer shall establish a software unit verification process including:

**Unit Verification Process Framework**:
```yaml
unit_test_planning:
  test_strategy:
    - testing_approach: "white_box|black_box|grey_box"
    - coverage_criteria: "statement|branch|condition|path"
    - test_case_design_methods: ["equivalence_partitioning", "boundary_value_analysis", "decision_table"]
    
  test_environment_requirements:
    - hardware_requirements: "Required hardware for testing"
    - software_requirements: "Operating system, libraries, test frameworks"
    - test_data_requirements: "Test inputs, expected outputs, test databases"
    - isolation_requirements: "How to isolate units for testing"
    
test_environment_setup:
  test_harness_design:
    - driver_programs: "Programs that call the unit being tested"
    - stub_programs: "Programs that simulate called units"
    - test_data_generators: "Tools to generate test inputs"
    - result_comparators: "Tools to verify test outputs"
    
  configuration_management:
    - version_control: "How test code and data are managed"
    - baseline_establishment: "Known good configurations"
    - change_control: "How test environment changes are managed"
    
test_execution_framework:
  automated_testing:
    - test_framework_selection: "Tools for automated test execution"
    - continuous_integration: "Integration with build systems"
    - regression_testing: "Automated re-testing after changes"
    
  manual_testing:
    - test_procedure_documentation: "Step-by-step manual test procedures"
    - result_recording: "How to document manual test results"
    - repeatability_measures: "Ensuring consistent manual testing"
```

#### 9.3.2 Software Unit Testing

The manufacturer shall test each software unit to verify that it:

**Unit Testing Requirements Matrix**:
| Verification Aspect | Testing Method | Success Criteria |
|---------------------|----------------|------------------|
| Design Implementation | Functional Testing | All specified functions work correctly |
| Specification Compliance | Interface Testing | All interface contracts are fulfilled |
| Boundary Handling | Boundary Value Testing | Correct behaviour at limits and edges |
| Error Reporting | Error Injection Testing | Appropriate error detection and reporting |

**Unit Testing Techniques Framework**:
```yaml
structural_testing_techniques:
  statement_coverage:
    definition: "Every executable statement is executed at least once"
    measurement: "Percentage of statements executed during testing"
    tools: ["gcov", "lcov", "JaCoCo", "OpenCover"]
    target_coverage: "100% for safety-critical code"
    
  branch_coverage:
    definition: "Every decision branch is exercised at least once"
    measurement: "Percentage of decision outcomes tested"
    includes: ["if_statement_branches", "switch_case_branches", "loop_conditions"]
    target_coverage: "100% for Class B and C software"
    
  condition_coverage:
    definition: "Every Boolean sub-expression is evaluated to both true and false"
    measurement: "Percentage of condition combinations tested"
    complexity: "Exponential growth with condition count"
    practical_application: "Focus on safety-critical conditions"
    
  path_coverage:
    definition: "Every possible execution path through the unit is tested"
    measurement: "Percentage of unique paths exercised"
    limitations: "Exponential path explosion, loops create infinite paths"
    practical_application: "Limited to small units with few paths"
    
functional_testing_techniques:
  equivalence_partitioning:
    definition: "Divide input domain into equivalence classes"
    application: "Test one representative from each class"
    benefits: "Reduces test case count while maintaining coverage"
    
  boundary_value_analysis:
    definition: "Focus testing on boundaries of equivalence classes"
    rationale: "Errors often occur at boundaries"
    test_points: ["minimum", "maximum", "just_below_minimum", "just_above_maximum"]
    
  decision_table_testing:
    definition: "Systematic testing of complex business logic"
    application: "Multiple input conditions with complex relationships"
    benefits: "Ensures all condition combinations are considered"
    
error_handling_testing:
  invalid_input_testing:
    null_pointer_tests: "Pass null pointers where not expected"
    out_of_range_tests: "Values outside acceptable ranges"
    malformed_data_tests: "Incorrectly formatted input data"
    
  resource_exhaustion_testing:
    memory_exhaustion: "Test behaviour when memory allocation fails"
    disk_space_exhaustion: "Test behaviour when storage is full"
    network_failure: "Test behaviour when network is unavailable"
    
  exception_handling_testing:
    exception_propagation: "Verify exceptions are properly propagated"
    exception_recovery: "Test recovery from exception conditions"
    resource_cleanup: "Verify resources are cleaned up after exceptions"
```

**AI-Agent Testing Automation Framework**:
```python
def execute_unit_testing(software_unit, test_specifications, coverage_requirements):
    testing_results = {
        "test_execution_summary": {},
        "coverage_analysis": {},
        "defect_reports": [],
        "quality_metrics": {}
    }
    
    # Generate test cases
    test_cases = generate_test_cases(software_unit, test_specifications)
    
    # Execute functional tests
    functional_results = execute_functional_tests(software_unit, test_cases)
    testing_results["functional_results"] = functional_results
    
    # Execute structural tests
    structural_results = execute_structural_tests(software_unit, coverage_requirements)
    testing_results["structural_results"] = structural_results
    
    # Analyse coverage
    coverage_analysis = analyse_code_coverage(structural_results)
    testing_results["coverage_analysis"] = coverage_analysis
    
    # Generate defect reports
    defects = identify_defects(functional_results, structural_results)
    testing_results["defect_reports"] = defects
    
    # Calculate quality metrics
    quality_metrics = calculate_quality_metrics(testing_results)
    testing_results["quality_metrics"] = quality_metrics
    
    return testing_results

def generate_test_cases(software_unit, test_specifications):
    """Generate comprehensive test cases for a software unit"""
    test_cases = []
    
    # Generate normal operation test cases
    normal_cases = generate_normal_operation_tests(software_unit.interfaces)
    test_cases.extend(normal_cases)
    
    # Generate boundary test cases
    boundary_cases = generate_boundary_tests(software_unit.input_constraints)
    test_cases.extend(boundary_cases)
    
    # Generate error condition test cases
    error_cases = generate_error_condition_tests(software_unit.error_specifications)
    test_cases.extend(error_cases)
    
    # Generate stress test cases
    stress_cases = generate_stress_tests(software_unit.performance_requirements)
    test_cases.extend(stress_cases)
    
    return test_cases
```

#### 9.3.3 Unit Testing for Different Safety Classes

**Safety Class-Specific Testing Requirements**:

**Class A Software Units**:
```yaml
testing_requirements:
  scope: "Test all software units"
  coverage_criteria: "Basic functional testing"
  documentation: "Standard test documentation"
  
test_execution:
  functional_testing: "Verify basic functionality works"
  error_handling: "Basic error condition testing"
  interface_testing: "Verify interface contracts"
  
coverage_targets:
  functional_coverage: "All specified functions tested"
  statement_coverage: "Recommended but not required"
  branch_coverage: "Not required"
  
documentation_requirements:
  test_procedures: "Basic test case documentation"
  test_results: "Pass/fail results recording"
  defect_tracking: "Basic defect identification and resolution"
```

**Class B Software Units**:
```yaml
testing_requirements:
  scope: "Test all software units"
  coverage_criteria: "Statement coverage OR branch coverage"
  documentation: "Comprehensive test documentation"
  
test_execution:
  functional_testing: "Comprehensive functional verification"
  error_handling: "Thorough error condition testing"
  interface_testing: "Complete interface verification"
  boundary_testing: "Systematic boundary value testing"
  
coverage_targets:
  functional_coverage: "All specified functions tested"
  structural_coverage: "Statement coverage OR branch coverage"
  minimum_threshold: "≥95% coverage of selected criterion"
  
documentation_requirements:
  test_procedures: "Detailed test case specifications"
  test_results: "Comprehensive result documentation"
  coverage_analysis: "Coverage measurement and analysis"
  defect_tracking: "Complete defect lifecycle management"
```

**Class C Software Units**:
```yaml
testing_requirements:
  scope: "Test all software units"
  coverage_criteria: "Statement coverage AND branch coverage"
  additional_coverage: "Path coverage where practical"
  documentation: "Exhaustive test documentation"
  
test_execution:
  functional_testing: "Exhaustive functional verification"
  error_handling: "Comprehensive error scenario testing"
  interface_testing: "Complete interface verification with error injection"
  boundary_testing: "Extensive boundary and edge case testing"
  stress_testing: "Performance and resource limit testing"
  fault_injection: "Systematic fault injection testing"
  
coverage_targets:
  functional_coverage: "All specified functions tested"
  statement_coverage: "100% of executable statements"
  branch_coverage: "100% of decision branches"
  path_coverage: "All feasible paths where practical"
  
documentation_requirements:
  test_procedures: "Exhaustive test case specifications"
  test_results: "Complete result documentation with evidence"
  coverage_analysis: "Multi-criterion coverage analysis"
  defect_tracking: "Complete defect lifecycle with impact analysis"
  traceability: "Complete traceability to requirements and risks"
```

---

## 10. Software Integration and Integration Testing

### 10.1 General

The manufacturer shall integrate software units into software items and conduct integration testing to verify that the integrated software items meet their specified requirements.

**AI-Agent Integration Management Framework**:
- Automated dependency analysis and integration sequencing
- Interface compatibility verification
- Integration test case generation from interface specifications
- Continuous integration monitoring and reporting

### 10.2 Integrate Software Units

#### 10.2.1 Integration Strategy

The manufacturer shall develop an integration strategy that defines:

**Integration Strategy Framework**:
```yaml
integration_approach_selection:
  big_bang_integration:
    description: "Integrate all units simultaneously"
    advantages: ["Simple approach", "Fast initial integration"]
    disadvantages: ["Difficult fault isolation", "Complex debugging"]
    suitable_for: "Small systems with well-tested units"
    
  incremental_integration:
    description: "Integrate units one at a time"
    advantages: ["Easier fault isolation", "Progressive verification"]
    disadvantages: ["Requires stubs/drivers", "Longer integration phase"]
    suitable_for: "Most medical device software systems"
    
    top_down_integration:
      description: "Start with top-level modules, integrate downward"
      advantages: ["Early demonstration of major functions", "Natural test progression"]
      disadvantages: ["Requires stub development", "Lower-level issues found late"]
      
    bottom_up_integration:
      description: "Start with lowest-level modules, integrate upward"
      advantages: ["Lower-level functions thoroughly tested", "No stubs needed"]
      disadvantages: ["Requires driver development", "System-level view delayed"]
      
    sandwich_integration:
      description: "Combine top-down and bottom-up approaches"
      advantages: ["Benefits of both approaches", "Parallel integration possible"]
      disadvantages: ["More complex management", "Requires both stubs and drivers"]
```

**AI-Agent Integration Planning Algorithm**:
```python
def develop_integration_strategy(software_units, system_requirements):
    strategy = {
        "integration_approach": None,
        "integration_sequence": [],
        "test_environment_requirements": {},
        "success_criteria": {}
    }
    
    # Analyse system characteristics
    system_analysis = analyse_system_characteristics(software_units)
    
    # Select integration approach
    if system_analysis["complexity"] == "low" and system_analysis["size"] == "small":
        strategy["integration_approach"] = "big_bang"
    else:
        strategy["integration_approach"] = select_incremental_approach(system_analysis)
    
    # Determine integration sequence
    dependency_graph = build_dependency_graph(software_units)
    strategy["integration_sequence"] = optimize_integration_sequence(
        dependency_graph, 
        system_analysis["risk_profile"]
    )
    
    # Define test environment requirements
    strategy["test_environment_requirements"] = determine_test_environment(
        software_units, 
        strategy["integration_sequence"]
    )
    
    # Establish success criteria
    strategy["success_criteria"] = establish_success_criteria(
        system_requirements, 
        software_units
    )
    
    return strategy
```

### 10.3 Software Integration Testing

#### 10.3.1 Develop Integration Test Procedures

The manufacturer shall develop integration test procedures that:

**Integration Test Development Framework**:
```yaml
test_procedure_categories:
  interface_verification_tests:
    purpose: "Verify interfaces between software units work correctly"
    test_types:
      - data_interface_tests: "Verify data passes correctly between units"
      - control_interface_tests: "Verify control signals work properly"
      - exception_interface_tests: "Verify error conditions are handled correctly"
    test_design_approach:
      - interface_specification_based: "Derive tests from interface specifications"
      - data_flow_based: "Test all data flows between units"
      - protocol_based: "Verify communication protocols work correctly"
      
  integrated_functionality_tests:
    purpose: "Test functionality that spans multiple software units"
    test_types:
      - end_to_end_functionality: "Complete functions from input to output"
      - workflow_tests: "Multi-step processes involving multiple units"
      - transaction_tests: "Complete transactions across unit boundaries"
    test_design_approach:
      - scenario_based: "Real-world usage scenarios"
      - use_case_based: "Systematic coverage of use cases"
      - business_rule_based: "Verify business logic across units"
```

#### 10.3.2 Integration Testing for Different Safety Classes

**Class A Software Items**:
```yaml
integration_testing_scope:
  test_procedures: "Based on software architectural design"
  coverage_requirements: "Basic integration functionality"
  documentation_level: "Standard integration test documentation"
  
test_execution_requirements:
  interface_testing:
    - basic_interface_verification: "Verify interfaces work as designed"
    - data_exchange_testing: "Test data passes correctly between items"
    - error_condition_testing: "Basic error handling verification"
    
  functionality_testing:
    - primary_function_testing: "Test main integrated functions"
    - workflow_testing: "Basic workflow verification"
    - user_scenario_testing: "Key user scenarios"
```

**Class B Software Items**:
```yaml
integration_testing_scope:
  test_procedures: "Comprehensive, based on software architectural design"
  coverage_requirements: "Enhanced integration testing including function sequences"
  documentation_level: "Comprehensive test documentation"
  
test_execution_requirements:
  interface_testing:
    - comprehensive_interface_verification: "All interfaces thoroughly tested"
    - boundary_condition_testing: "Interface limits and edge cases"
    - error_injection_testing: "Systematic error condition testing"
    - performance_testing: "Interface performance verification"
    
  functionality_testing:
    - complete_function_testing: "All integrated functions tested"
    - sequence_testing: "Software functions tested in sequence"
    - complex_workflow_testing: "Multi-step workflow verification"
    - integration_scenario_testing: "Comprehensive scenario coverage"
```

**Class C Software Items**:
```yaml
integration_testing_scope:
  test_procedures: "Exhaustive, based on software architectural design"
  coverage_requirements: "Extensive testing including all function sequences and interfaces"
  documentation_level: "Exhaustive test documentation with full traceability"
  
test_execution_requirements:
  interface_testing:
    - exhaustive_interface_verification: "All interfaces tested under all conditions"
    - comprehensive_boundary_testing: "All boundary and edge conditions"
    - systematic_error_injection: "All possible error conditions tested"
    - performance_stress_testing: "Performance limits and degradation testing"
    - security_testing: "Interface security and data integrity testing"
    
  functionality_testing:
    - complete_function_verification: "All functions tested individually and in combination"
    - comprehensive_sequence_testing: "All possible function sequences tested"
    - complex_integration_testing: "Multi-component integration scenarios"
    - end_to_end_testing: "Complete system workflows from input to output"
    
  interface_specific_requirements:
    - all_interface_testing: "Every interface between software items tested"
    - interface_interaction_testing: "Complex interactions between interfaces"
    - interface_failure_mode_testing: "Interface behaviour during failures"
```

---

## 11. Software System Testing

### 11.1 General

The manufacturer shall conduct software system testing to provide evidence that the completed software system meets its specified requirements.

**AI-Agent System Testing Framework**:
- Automated test case generation from requirements
- Comprehensive coverage analysis
- Intelligent test result correlation
- Performance and reliability assessment

### 11.2 Establish System Test Environment

The manufacturer shall establish a system test environment that:

**System Test Environment Framework**:
```yaml
environment_configuration:
  hardware_environment:
    - target_hardware_platform: "Actual or representative hardware"
    - peripheral_devices: "All connected medical devices and interfaces"
    - network_infrastructure: "Communication systems and protocols"
    - environmental_conditions: "Temperature, humidity, electromagnetic conditions"
    
  software_environment:
    - operating_system: "Target OS configuration"
    - system_software: "Drivers, middleware, runtime libraries"
    - configuration_settings: "System parameters and configurations"
    - external_systems: "Connected systems and interfaces"
    
  test_infrastructure:
    - test_execution_tools: "Automated testing frameworks"
    - monitoring_systems: "Performance and behaviour monitoring"
    - data_collection: "Test data capture and analysis tools"
    - result_reporting: "Test result generation and distribution"
```

### 11.3 System Testing Procedures

#### 11.3.1 Develop System Test Procedures

The manufacturer shall develop system test procedures that verify:

**System Test Categories**:
```yaml
functional_system_tests:
  requirements_based_testing:
    - complete_requirement_coverage: "Every system requirement tested"
    - end_to_end_functionality: "Complete workflows from start to finish"
    - use_case_validation: "All user scenarios verified"
    
  clinical_workflow_testing:
    - clinical_scenario_testing: "Real clinical use cases"
    - workflow_integration: "Integration with clinical processes"
    - usability_verification: "User interface and experience validation"
    
performance_system_tests:
  timing_requirements:
    - response_time_verification: "System meets timing constraints"
    - throughput_validation: "System handles required data volumes"
    - real_time_behaviour: "Real-time system constraints verified"
    
  resource_utilisation:
    - memory_usage_verification: "Memory consumption within limits"
    - cpu_utilisation_testing: "Processing capacity adequate"
    - storage_requirements: "Data storage and retrieval performance"
    
reliability_system_tests:
  fault_tolerance:
    - component_failure_scenarios: "System behaviour during component failures"
    - degraded_operation_modes: "Graceful degradation verification"
    - recovery_mechanisms: "System recovery from failures"
    
  stress_testing:
    - maximum_load_testing: "System behaviour at capacity limits"
    - sustained_operation: "Long-term operation stability"
    - resource_exhaustion: "Behaviour when resources are depleted"
```

#### 11.3.2 System Testing for Different Safety Classes

**Class A Software Systems**:
```yaml
system_testing_requirements:
  functional_testing: "Verify all system functions work as specified"
  performance_testing: "Basic performance requirement verification"
  usability_testing: "Basic user interface verification"
  documentation_testing: "Verify documentation matches system behaviour"
```

**Class B Software Systems**:
```yaml
system_testing_requirements:
  comprehensive_functional_testing: "All functions tested under normal and abnormal conditions"
  performance_validation: "Performance requirements verified under stress"
  reliability_testing: "System reliability and fault tolerance verified"
  clinical_scenario_testing: "Clinical use cases comprehensively tested"
  risk_control_verification: "All risk control measures verified effective"
```

**Class C Software Systems**:
```yaml
system_testing_requirements:
  exhaustive_functional_testing: "All functions tested under all conditions"
  comprehensive_performance_testing: "Performance verified under all operating conditions"
  extensive_reliability_testing: "Complete fault tolerance and recovery verification"
  comprehensive_clinical_testing: "All clinical scenarios including emergency situations"
  complete_risk_control_verification: "All risk controls verified under all conditions"
  safety_system_validation: "Safety-critical functions extensively validated"
  regulatory_compliance_testing: "Complete regulatory requirement verification"
```

---

## Related Documents

- [Scope and Classification](01-scope-and-classification.md)
- [Planning and Requirements](02-planning-and-requirements.md)
- [Design and Architecture](03-design-architecture.md)
- [Release and Maintenance](05-release-maintenance.md)
- [Supporting Processes](06-supporting-processes.md)
- [AI Automation Schemas](ai-automation-schemas/)
- [Code Examples](code-examples/)