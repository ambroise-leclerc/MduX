# ADR-002 Implementation Plan: BDD Testing Framework Integration

## Implementation Phases

### Phase 1: Framework Integration (Week 1-2)
**Objective**: Integrate Catch2 v3 with existing CMake build system

**Tasks**:
1. **CMake Integration**
   - Add CPM dependency for Catch2 v3.4+
   - Create new BDD test targets alongside existing unit tests
   - Configure test discovery and CTest integration

2. **Basic BDD Test Structure**
   - Create BddTestMain.cpp with Catch2 main
   - Migrate existing compliance tests to BDD format
   - Establish test file naming conventions

**Deliverables**:
- Updated CMakeLists.txt with Catch2 integration
- Basic BDD test runner executable
- Migration of existing tests to BDD format

### Phase 2: Medical Device Extensions (Week 3-4)
**Objective**: Develop custom macros and extensions for IEC 62304/62366 compliance

**Tasks**:
1. **Compliance Macros**
   - `MEDICAL_REQUIREMENT()` for traceability
   - `RISK_LEVEL()` for safety classification
   - `IEC_62304_TEST()` and `IEC_62366_TEST()` markers

2. **Test Organization**
   - Tag system for regulatory categories
   - Test suite structure for different risk levels
   - Integration with existing compliance validation

**Deliverables**:
- MedicalDeviceTestMacros.hpp header
- Compliance test organization framework
- Documentation for medical device testing patterns

### Phase 3: Reporting and Documentation (Week 5-6)
**Objective**: Automated generation of regulatory-compliant test documentation

**Tasks**:
1. **Traceability Matrix Generator**
   - Parse test files for requirement links
   - Generate requirement-to-test mapping
   - Integration with documentation build process

2. **Compliance Reporting**
   - Medical device test report templates
   - Automated test result documentation
   - Integration with Doxygen documentation system

**Deliverables**:
- Automated traceability matrix generation
- Medical device test report templates
- Integration with existing documentation workflow

## CMake Integration Design

### Updated tests/CMakeLists.txt
```cmake
# Medical Device BDD Testing with Catch2

# Fetch Catch2 framework
CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.4.0
    OPTIONS
        "CATCH_INSTALL_DOCS OFF"
        "CATCH_INSTALL_EXTRAS OFF"
        "CATCH_CONFIG_DISABLE_EXCEPTIONS OFF"
)

# BDD Test Suite for Medical Device Compliance
add_executable(mdux_bdd_tests
    BddTestMain.cpp
    WindowBddTests.cpp
    ComplianceBddTests.cpp
    GraphicsBddTests.cpp
    UsabilityBddTests.cpp
    RegulatoryBddTests.cpp
)

target_link_libraries(mdux_bdd_tests PRIVATE 
    MduX::MduX 
    Catch2::Catch2WithMain
)

# Medical device specific compile definitions
target_compile_definitions(mdux_bdd_tests PRIVATE
    MDUX_BDD_TESTING=1
    MDUX_COMPLIANCE_REPORTING=1
    MDUX_TRACEABILITY_ENABLED=1
)

# Set C++23 standard for BDD tests
set_target_properties(mdux_bdd_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Register with CTest
include(Catch)
catch_discover_tests(mdux_bdd_tests
    TEST_PREFIX "BDD."
    REPORTER xml
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/test-reports
)

# Legacy unit tests (maintain existing)
add_executable(unit_tests 
    TestMain.cpp
    TestVersion.cpp
    TestCompliance.cpp
)

target_link_libraries(unit_tests PRIVATE MduX::MduX)
add_test(NAME MduXUnitTests COMMAND unit_tests)

# Medical device compliance tests (maintain existing)
add_executable(compliance_tests
    ComplianceTestMain.cpp
    TestRegulatoryCompliance.cpp
)

target_link_libraries(compliance_tests PRIVATE MduX::MduX)
add_test(NAME MduXComplianceTests COMMAND compliance_tests)

# Test report generation for regulatory documentation
if(MDUX_BUILD_DOCS AND MDUX_ENABLE_REGULATORY_DOCS)
    add_custom_target(medical_device_test_reports
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/regulatory-docs
        COMMAND mdux_bdd_tests --reporter junit::out=${CMAKE_BINARY_DIR}/regulatory-docs/bdd-test-results.xml
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target traceability-matrix
        DEPENDS mdux_bdd_tests
        COMMENT "Generating medical device test reports for regulatory submission"
    )
endif()
```

### Medical Device Test Macros Header
```cpp
// include/mdux/testing/MedicalDeviceTestMacros.hpp
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

// Medical device requirement traceability
#define MEDICAL_REQUIREMENT(req_id) \
    INFO("Requirement ID: " << req_id); \
    static_assert(true, "") // Force semicolon

// Risk level classification for IEC 62304
#define RISK_LEVEL(level) \
    INFO("Risk Level: " << level); \
    static_assert(true, "")

// IEC 62304 specific test marking
#define IEC_62304_TEST(description, tags) \
    TEST_CASE(description, tags " [iec62304]")

// IEC 62366 specific test marking  
#define IEC_62366_TEST(description, tags) \
    TEST_CASE(description, tags " [iec62366]")

// Medical device BDD scenario
#define MEDICAL_SCENARIO(description, tags) \
    SCENARIO(description, tags " [medical-device]")

// Safety-critical test marking
#define SAFETY_CRITICAL_TEST(description, tags) \
    TEST_CASE(description, tags " [safety-critical][high-risk]")
```

## Migration Strategy

### Existing Test Migration
1. **Preserve Current Tests**: Maintain existing unit_tests and compliance_tests
2. **Gradual Migration**: Convert tests to BDD format incrementally
3. **Dual Coverage**: Run both old and new tests during transition period
4. **Validation**: Ensure BDD tests provide equivalent or better coverage

### Risk Mitigation
- **Parallel Testing**: Run both frameworks until BDD tests are fully validated
- **Regression Protection**: Maintain existing test coverage during migration
- **Documentation**: Clear migration guides for development team
- **Training**: Team education on BDD methodology and Catch2 features

## Success Criteria

### Technical Criteria
- ✅ All existing tests migrated to BDD format with equivalent coverage
- ✅ Medical device compliance macros implemented and documented
- ✅ Automated traceability matrix generation working
- ✅ Regulatory test reports generated automatically
- ✅ Integration with existing CMake and documentation workflow

### Regulatory Criteria  
- ✅ IEC 62304 traceability requirements satisfied
- ✅ IEC 62366 usability testing patterns established
- ✅ Test documentation suitable for regulatory submission
- ✅ Risk-based test organization implemented
- ✅ Change control process for test modifications documented

### Operational Criteria
- ✅ Development team trained on BDD methodology
- ✅ CI/CD pipeline updated for new test framework
- ✅ Test execution time acceptable (< 2x current runtime)
- ✅ Regulatory team approval of testing approach
- ✅ Quality assurance validation of compliance features