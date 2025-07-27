# Tests for MduX Medical Device UI Library

# Find or fetch a testing framework (placeholder for now)
# In a real implementation, you might use CPM to fetch Catch2, Google Test, etc.

# Basic unit tests
add_executable(unit_tests 
    TestMain.cpp
    TestVersion.cpp
    TestCompliance.cpp
)

target_link_libraries(unit_tests PRIVATE MduX::MduX)

# Set C++23 standard for tests and enable module imports
set_target_properties(unit_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_MODULE_STD ON
)

# Add tests to CTest
add_test(NAME MduXUnitTests COMMAND unit_tests)

# Medical device compliance tests
add_executable(compliance_tests
    ComplianceTestMain.cpp
    TestRegulatoryCompliance.cpp
)

target_link_libraries(compliance_tests PRIVATE MduX::MduX)

set_target_properties(compliance_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_MODULE_STD ON
)

add_test(NAME MduXComplianceTests COMMAND compliance_tests)