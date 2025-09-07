# IEC 62304: Software Architectural Design and Detailed Design

## Module Overview
This module covers the transformation of software requirements into comprehensive architectural and detailed designs. It focuses on creating robust, verifiable software architectures and detailed designs that support safe medical device operation.

**Key Areas Covered:**
- Software architectural design development and verification
- Detailed design refinement and documentation
- Interface specifications and segregation strategies
- Design verification methodologies and documentation

---

## 7. Software Architectural Design

### 7.1 General

The manufacturer shall transform software requirements into an architecture that describes the software's top-level structure and identifies the principal structural elements and their interfaces.

**AI-Agent Architecture Analysis Framework**:
- Pattern recognition for architectural styles
- Component dependency analysis
- Interface consistency checking
- Performance characteristic prediction

### 7.2 Develop Software Architecture

#### 7.2.1 Transform Requirements into Architecture

The manufacturer shall develop a software architecture from the software requirements that:

**Architecture Development Checklist**:
- [ ] Defines the major structural elements of the software
- [ ] Describes interfaces between elements
- [ ] Allocates software requirements to structural elements
- [ ] Addresses non-functional requirements (performance, reliability, security)

**Architectural Transformation Process**:
```python
def transform_requirements_to_architecture(requirements):
    architecture = {
        "components": [],
        "interfaces": [],
        "allocations": {},
        "non_functional_measures": {}
    }
    
    # Identify major functional areas
    functional_areas = group_requirements_by_function(requirements)
    
    # Create architectural components
    for area in functional_areas:
        component = create_component(area)
        architecture["components"].append(component)
    
    # Define interfaces
    interfaces = identify_interfaces(architecture["components"])
    architecture["interfaces"].extend(interfaces)
    
    # Allocate requirements
    for requirement in requirements:
        responsible_components = determine_allocation(requirement, architecture["components"])
        architecture["allocations"][requirement.id] = responsible_components
    
    # Address non-functional requirements
    architecture["non_functional_measures"] = address_non_functional(requirements)
    
    return architecture
```

#### 7.2.2 Document Software Architecture

The manufacturer shall document the software architecture including:

**A) Architectural Design Description**

**Architecture Documentation Structure**:
```yaml
overview:
  - architectural_style: "layered|microservices|component_based|event_driven"
  - design_principles: ["separation_of_concerns", "modularity", "reusability"]
  - key_design_decisions: ["technology_choices", "pattern_applications"]
  
major_components:
  - component_name:
      responsibilities: ["responsibility1", "responsibility2"]
      functionality: "detailed_description"
      interfaces: ["interface1", "interface2"]
      dependencies: ["component1", "component2"]
      
component_relationships:
  - interaction_patterns: ["client_server", "publisher_subscriber", "pipeline"]
  - data_flow: "description_of_data_movement"
  - control_flow: "description_of_control_flow"
```

**B) Interface Specifications**

**Interface Documentation Template**:
```json
{
  "interface_id": "unique_identifier",
  "interface_name": "descriptive_name",
  "type": "internal|external|user|communication",
  "components": {
    "provider": "component_name",
    "consumer": "component_name"
  },
  "specification": {
    "protocol": "http|tcp|udp|shared_memory|api",
    "data_format": "json|xml|binary|custom",
    "operations": [
      {
        "operation_name": "string",
        "parameters": [
          {
            "name": "string",
            "type": "string",
            "constraints": "string"
          }
        ],
        "return_type": "string",
        "preconditions": ["condition1"],
        "postconditions": ["condition1"],
        "exceptions": ["exception1"]
      }
    ],
    "timing_requirements": {
      "response_time_max": "duration",
      "throughput_min": "rate"
    },
    "reliability_requirements": {
      "availability": "percentage",
      "error_handling": "strategy"
    }
  }
}
```

**C) Segregation Between Software Items**

**Safety Segregation Framework**:
```yaml
segregation_mechanisms:
  temporal_separation:
    - time_partitioning
    - scheduling_policies
    - execution_slots
  spatial_separation:
    - memory_protection
    - address_space_isolation
    - hardware_boundaries
  logical_separation:
    - access_control_matrices
    - privilege_levels
    - permission_systems
    
isolation_barriers:
  hardware_barriers:
    - memory_management_units
    - privileged_instruction_sets
    - hardware_watchdogs
  software_barriers:
    - operating_system_services
    - middleware_isolation
    - application_sandboxing
    
access_control:
  authentication_mechanisms:
    - user_credentials
    - system_certificates
    - cryptographic_tokens
  authorisation_policies:
    - role_based_access
    - attribute_based_access
    - mandatory_access_control
```

### 7.3 Develop Each Software Item

#### 7.3.1 Refine the Software Architecture

For each software item identified in the architecture, the manufacturer shall:

**Software Item Development Process**:
```python
def refine_software_item(architectural_component):
    software_item = {
        "refined_requirements": [],
        "interfaces": [],
        "algorithms": [],
        "data_structures": [],
        "relationships": []
    }
    
    # Refine requirements
    software_item["refined_requirements"] = refine_requirements(
        architectural_component.allocated_requirements
    )
    
    # Develop interfaces
    software_item["interfaces"] = develop_item_interfaces(
        architectural_component.external_interfaces
    )
    
    # Select algorithms and data structures
    software_item["algorithms"] = select_algorithms(
        software_item["refined_requirements"]
    )
    software_item["data_structures"] = select_data_structures(
        software_item["algorithms"]
    )
    
    # Identify relationships
    software_item["relationships"] = identify_relationships(
        architectural_component, other_components
    )
    
    return software_item
```

#### 7.3.2 Specify Software Item Interfaces

The manufacturer shall specify interfaces for each software item:

**Interface Specification Framework**:
```yaml
function_interfaces:
  interface_name:
    signature: "return_type function_name(parameter_list)"
    parameters:
      - name: "parameter_name"
        type: "data_type"
        direction: "in|out|inout"
        constraints: "validation_rules"
    return_value:
      type: "data_type"
      constraints: "validation_rules"
    exceptions:
      - exception_type: "string"
        conditions: "when_thrown"
        
data_interfaces:
  interface_name:
    format: "structure_definition"
    encoding: "binary|text|json|xml"
    validation_rules: ["rule1", "rule2"]
    versioning: "compatibility_requirements"
    
control_interfaces:
  interface_name:
    timing_requirements:
      synchronous: "boolean"
      timeout: "duration"
      periodicity: "interval"
    sequencing:
      call_order: "sequence_constraints"
      state_dependencies: "required_states"
      
error_handling:
  error_codes: ["code1", "code2"]
  error_recovery: "strategy_description"
  logging_requirements: "what_to_log"
```

### 7.4 Software Architectural Design Verification

#### 7.4.1 Verify Software Architecture

The manufacturer shall verify that the software architecture:

**Architecture Verification Checklist**:
- [ ] Implements software requirements completely and correctly
- [ ] Is able to support interfaces defined in system design
- [ ] Is appropriate for the medical device application domain
- [ ] Supports proper operation of any SOUP (Software of Unknown Provenance)

**Verification Methods Selection Matrix**:

| Verification Aspect | Design Reviews | Prototyping | Analysis | Testing |
|---------------------|---------------|-------------|----------|---------|
| Requirements Implementation | ✓ | ✓ | ✓ | - |
| Interface Compatibility | ✓ | ✓ | ✓ | ✓ |
| Medical Device Appropriateness | ✓ | - | ✓ | - |
| SOUP Integration | ✓ | ✓ | ✓ | ✓ |
| Performance Characteristics | - | ✓ | ✓ | ✓ |
| Safety Properties | ✓ | - | ✓ | - |

**AI-Agent Verification Automation**:
```python
def verify_architecture(architecture, requirements, system_interfaces):
    verification_results = {
        "requirements_coverage": check_requirements_coverage(architecture, requirements),
        "interface_compatibility": check_interface_compatibility(architecture, system_interfaces),
        "architectural_consistency": check_internal_consistency(architecture),
        "soup_integration": verify_soup_integration(architecture),
        "performance_feasibility": analyse_performance_characteristics(architecture)
    }
    
    overall_status = aggregate_verification_results(verification_results)
    return verification_results, overall_status
```

#### 7.4.2 Document Verification Results

The manufacturer shall document architectural design verification results:

**Verification Results Documentation Template**:
```json
{
  "verification_summary": {
    "architecture_version": "string",
    "verification_date": "ISO_date",
    "verification_team": ["person1", "person2"],
    "overall_result": "pass|conditional_pass|fail"
  },
  "verification_activities": [
    {
      "activity_type": "design_review|prototyping|analysis|testing",
      "scope": "requirements_implementation|interface_compatibility|medical_appropriateness|soup_integration",
      "method_details": "specific_method_or_tool_used",
      "participants": ["person1", "person2"],
      "duration": "hours_or_days",
      "findings": [
        {
          "finding_id": "string",
          "description": "string",
          "severity": "critical|major|minor|observation",
          "architectural_element": "affected_component_or_interface",
          "recommendation": "suggested_action"
        }
      ]
    }
  ],
  "actions_taken": [
    {
      "action_id": "string",
      "finding_addressed": "finding_id",
      "description": "corrective_action_description",
      "implementation_status": "planned|in_progress|completed",
      "verification_of_fix": "how_fix_was_verified"
    }
  ],
  "evidence_repository": {
    "review_records": ["file_reference1"],
    "analysis_reports": ["file_reference2"],
    "prototype_results": ["file_reference3"],
    "test_results": ["file_reference4"]
  }
}
```

---

## 8. Software Detailed Design

### 8.1 General

The manufacturer shall develop a detailed design for each software item sufficient for that software item to be implemented and tested.

**AI-Agent Detailed Design Framework**:
- Automated design pattern recognition and application
- Code structure generation from design specifications
- Test case derivation from design elements
- Design consistency checking across software items

### 8.2 Refine the Software Architecture into a Detailed Design

#### 8.2.1 Detailed Design Development

The manufacturer shall develop detailed design for each software item that:

**Design Development Requirements Matrix**:
| Requirement | Description | AI-Agent Support |
|------------|-------------|------------------|
| Architecture Refinement | Elaborate architectural components into implementable units | Pattern-based decomposition |
| Interface Definition | Specify precise interfaces between software units | API specification generation |
| Algorithm Description | Define computational methods and data transformations | Algorithm template library |
| Resource Specification | Detail timing, memory, and performance requirements | Resource estimation models |

**Detailed Design Process Flow**:
```python
def develop_detailed_design(software_item):
    detailed_design = {
        "software_units": [],
        "unit_interfaces": [],
        "data_structures": [],
        "algorithms": [],
        "resource_requirements": {}
    }
    
    # Decompose software item into units
    units = decompose_into_units(software_item)
    for unit in units:
        detailed_unit = design_software_unit(unit)
        detailed_design["software_units"].append(detailed_unit)
    
    # Define unit interfaces
    interfaces = derive_unit_interfaces(detailed_design["software_units"])
    detailed_design["unit_interfaces"] = interfaces
    
    # Specify data structures
    detailed_design["data_structures"] = design_data_structures(
        software_item.data_requirements
    )
    
    # Detail algorithms
    detailed_design["algorithms"] = specify_algorithms(
        software_item.functional_requirements
    )
    
    # Calculate resource requirements
    detailed_design["resource_requirements"] = estimate_resources(
        detailed_design
    )
    
    return detailed_design
```

#### 8.2.2 Document Detailed Design

The manufacturer shall document the detailed design including:

**A) Software Item Design Description**

**Design Documentation Structure**:
```yaml
software_item_overview:
  purpose: "clear_statement_of_item_purpose"
  scope: "boundaries_and_limitations"
  context: "relationship_to_other_items"
  
functionality_specification:
  inputs:
    - name: "input_name"
      type: "data_type"
      source: "origin_description"
      validation: "input_validation_rules"
  outputs:
    - name: "output_name"
      type: "data_type"
      destination: "target_description"
      constraints: "output_constraints"
  processing_logic:
    - step_description: "detailed_processing_step"
      algorithms_used: ["algorithm1", "algorithm2"]
      decision_points: ["condition1", "condition2"]
      
data_structures:
  - structure_name: "identifier"
    type: "class|struct|array|list|tree|graph"
    definition: "formal_structure_definition"
    operations: ["operation1", "operation2"]
    invariants: ["invariant1", "invariant2"]
    
algorithm_descriptions:
  - algorithm_name: "identifier"
    purpose: "what_the_algorithm_accomplishes"
    complexity: "time_and_space_complexity"
    pseudocode: "step_by_step_algorithm"
    error_conditions: ["error1", "error2"]
    performance_characteristics: "expected_performance"
```

**B) Interface Specifications**

**Detailed Interface Documentation Template**:
```json
{
  "interface_specification": {
    "interface_id": "unique_identifier",
    "interface_type": "function_call|data_exchange|event_notification|control_signal",
    "participants": {
      "caller": "software_unit_name",
      "callee": "software_unit_name"
    },
    "detailed_definition": {
      "signature": "formal_interface_signature",
      "parameters": [
        {
          "name": "parameter_name",
          "data_type": "specific_type_definition",
          "direction": "input|output|input_output",
          "constraints": {
            "range": "min_max_values",
            "format": "data_format_specification",
            "validation_rules": ["rule1", "rule2"]
          },
          "default_value": "default_if_applicable"
        }
      ],
      "return_value": {
        "data_type": "return_type_specification",
        "success_conditions": "when_successful",
        "error_conditions": "when_errors_occur"
      }
    },
    "error_handling": {
      "exception_types": ["exception1", "exception2"],
      "error_codes": [
        {
          "code": "error_code_value",
          "description": "error_meaning",
          "recovery_action": "suggested_response"
        }
      ],
      "error_propagation": "how_errors_are_communicated"
    },
    "timing_requirements": {
      "synchronisation": "synchronous|asynchronous",
      "timeout_specification": "maximum_wait_time",
      "periodicity": "call_frequency_if_applicable"
    },
    "sequencing_requirements": {
      "prerequisite_calls": ["required_prior_calls"],
      "state_dependencies": ["required_system_states"],
      "concurrent_access": "thread_safety_specification"
    }
  }
}
```

**C) Software Unit Specifications**

**Unit Specification Framework**:
```yaml
software_unit:
  identification:
    unit_name: "unique_identifier"
    version: "version_number"
    author: "developer_name"
    creation_date: "ISO_date"
    
  purpose_and_functionality:
    primary_function: "main_responsibility"
    secondary_functions: ["additional_responsibilities"]
    assumptions: ["assumption1", "assumption2"]
    limitations: ["limitation1", "limitation2"]
    
  interfaces:
    provided_interfaces: ["interface1", "interface2"]
    required_interfaces: ["interface3", "interface4"]
    internal_interfaces: ["interface5"]
    
  dependencies:
    software_dependencies: ["unit1", "unit2"]
    hardware_dependencies: ["hardware_resource1"]
    external_dependencies: ["library1", "service1"]
    
  implementation_requirements:
    programming_language: "language_specification"
    coding_standards: ["standard1", "standard2"]
    performance_requirements:
      execution_time: "maximum_duration"
      memory_usage: "maximum_allocation"
      throughput: "minimum_rate"
    reliability_requirements:
      error_handling: "error_handling_strategy"
      fault_tolerance: "fault_tolerance_approach"
      recovery_mechanisms: ["recovery1", "recovery2"]
      
  testing_requirements:
    unit_test_coverage: "minimum_coverage_percentage"
    test_data_requirements: "test_data_specifications"
    test_environment: "testing_environment_setup"
    acceptance_criteria: ["criterion1", "criterion2"]
```

### 8.3 Software Detailed Design Verification

#### 8.3.1 Verify Detailed Design

The manufacturer shall verify that the detailed design:

**Design Verification Checklist**:
- [ ] Implements the software architecture correctly and completely
- [ ] Is free from internal contradictions
- [ ] Uses appropriate algorithms and data structures for the application
- [ ] Supports the intended medical device functionality

**Verification Methods Application Matrix**:

| Verification Method | Architecture Implementation | Contradiction Detection | Algorithm Appropriateness | Medical Functionality |
|---------------------|----------------------------|------------------------|---------------------------|----------------------|
| **Design Reviews** | ✓ | ✓ | ✓ | ✓ |
| **Static Analysis** | ✓ | ✓ | ✓ | - |
| **Prototyping** | ✓ | - | ✓ | ✓ |
| **Mathematical Modeling** | - | ✓ | ✓ | - |

**AI-Agent Verification Automation Framework**:
```python
def verify_detailed_design(detailed_design, architecture, medical_requirements):
    verification_results = {}
    
    # Verify architecture implementation
    verification_results["architecture_implementation"] = verify_architecture_implementation(
        detailed_design, architecture
    )
    
    # Check for contradictions
    verification_results["contradiction_analysis"] = analyse_contradictions(
        detailed_design
    )
    
    # Evaluate algorithm appropriateness
    verification_results["algorithm_evaluation"] = evaluate_algorithms(
        detailed_design.algorithms, medical_requirements
    )
    
    # Verify medical functionality support
    verification_results["medical_functionality"] = verify_medical_support(
        detailed_design, medical_requirements
    )
    
    # Generate overall assessment
    overall_result = synthesise_verification_results(verification_results)
    
    return verification_results, overall_result

def verify_architecture_implementation(detailed_design, architecture):
    """Verify detailed design implements architectural decisions"""
    implementation_gaps = []
    
    for architectural_component in architecture.components:
        corresponding_units = find_implementing_units(
            detailed_design, architectural_component
        )
        
        if not corresponding_units:
            implementation_gaps.append({
                "component": architectural_component.name,
                "issue": "No implementing software units found"
            })
        else:
            # Verify functionality coverage
            required_functions = architectural_component.functions
            implemented_functions = extract_functions(corresponding_units)
            
            missing_functions = set(required_functions) - set(implemented_functions)
            if missing_functions:
                implementation_gaps.append({
                    "component": architectural_component.name,
                    "issue": f"Missing functions: {missing_functions}"
                })
    
    return {
        "status": "complete" if not implementation_gaps else "incomplete",
        "gaps": implementation_gaps
    }

def analyse_contradictions(detailed_design):
    """Detect internal contradictions in the detailed design"""
    contradictions = []
    
    # Check interface consistency
    interface_contradictions = check_interface_consistency(detailed_design.unit_interfaces)
    contradictions.extend(interface_contradictions)
    
    # Check data structure consistency
    data_contradictions = check_data_structure_consistency(detailed_design.data_structures)
    contradictions.extend(data_contradictions)
    
    # Check algorithm consistency
    algorithm_contradictions = check_algorithm_consistency(detailed_design.algorithms)
    contradictions.extend(algorithm_contradictions)
    
    return {
        "contradiction_count": len(contradictions),
        "contradictions": contradictions,
        "status": "consistent" if not contradictions else "inconsistent"
    }
```

#### 8.3.2 Document Verification Results

The manufacturer shall document detailed design verification including:

**Verification Documentation Template**:
```json
{
  "verification_overview": {
    "design_version": "string",
    "verification_period": {
      "start_date": "ISO_date",
      "end_date": "ISO_date"
    },
    "verification_team": [
      {
        "name": "string",
        "role": "design_reviewer|static_analyser|prototyper|domain_expert",
        "qualifications": "relevant_experience_and_skills"
      }
    ],
    "verification_scope": "full_design|specific_units|specific_aspects"
  },
  
  "verification_activities": [
    {
      "activity_id": "unique_identifier",
      "method": "design_review|static_analysis|prototyping|mathematical_modelling",
      "scope": "architecture_implementation|contradiction_detection|algorithm_evaluation|medical_functionality",
      "execution_details": {
        "duration": "time_spent",
        "tools_used": ["tool1", "tool2"],
        "criteria_applied": ["criterion1", "criterion2"]
      },
      "results": {
        "findings": [
          {
            "finding_id": "string",
            "category": "architecture|interface|algorithm|data_structure|performance|medical",
            "severity": "critical|major|minor|informational",
            "description": "detailed_finding_description",
            "affected_elements": ["element1", "element2"],
            "evidence": "supporting_evidence_reference"
          }
        ],
        "metrics": {
          "coverage_achieved": "percentage",
          "defect_density": "defects_per_unit",
          "review_efficiency": "findings_per_hour"
        }
      }
    }
  ],
  
  "issue_resolution": [
    {
      "finding_id": "reference_to_finding",
      "resolution_approach": "design_modification|clarification|exception_approval",
      "implementation_details": "what_was_changed_or_clarified",
      "verification_of_resolution": "how_resolution_was_confirmed",
      "responsible_person": "person_who_implemented_resolution",
      "completion_date": "ISO_date"
    }
  ],
  
  "design_adequacy_assessment": {
    "overall_rating": "adequate|adequate_with_conditions|inadequate",
    "adequacy_evidence": [
      {
        "evidence_type": "review_consensus|analysis_results|prototype_demonstration",
        "description": "evidence_description",
        "supporting_documents": ["document_reference1"]
      }
    ],
    "conditions_for_proceeding": [
      "condition_that_must_be_met_before_implementation"
    ],
    "recommendations": [
      "recommendation_for_improvement_or_consideration"
    ]
  }
}
```

---

## Related Documents

- [Scope and Classification](01-scope-and-classification.md)
- [Planning and Requirements](02-planning-and-requirements.md)
- [Implementation and Testing](04-implementation-testing.md)
- [Release and Maintenance](05-release-maintenance.md)
- [Supporting Processes](06-supporting-processes.md)
- [AI Automation Schemas](ai-automation-schemas/)
- [Code Examples](code-examples/)