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
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(unit_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(unit_tests PRIVATE /experimental:module)
    target_compile_options(unit_tests PRIVATE /std:c++latest)
endif()

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
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(compliance_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(compliance_tests PRIVATE /experimental:module)
    target_compile_options(compliance_tests PRIVATE /std:c++latest)
endif()

add_test(NAME MduXComplianceTests COMMAND compliance_tests)

# Vulkan SC specific tests
add_executable(vulkansc_memory_tests
    vulkansc/MemoryPoolManagerTests.cpp
)

target_link_libraries(vulkansc_memory_tests PRIVATE MduX::MduX)

set_target_properties(vulkansc_memory_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(vulkansc_memory_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vulkansc_memory_tests PRIVATE /experimental:module)
    target_compile_options(vulkansc_memory_tests PRIVATE /std:c++latest)
endif()

# Work around a GCC internal compiler error in this specific translation unit at
# optimization levels above -O0 (GIMPLE ealias pass, "internal compiler error: in
# nonnull_arg_p, at tree.cc"). Reproduced deterministically on both GCC 15.2.0 and
# GCC 16 with C++23 modules + import std + <vulkan/vulkan.h>; -O1/-O2/-O3/-Og all
# trigger it, -O0 does not. -fno-tree-pta and dropping -Wformat=2 do not help. See
# issue #48 for the investigation. A later -O flag wins on GCC's command line, so
# this override is safe to append regardless of CMAKE_BUILD_TYPE.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(vulkansc_memory_tests PRIVATE -O0)
endif()

add_test(NAME VulkanSCMemoryPoolTests COMMAND vulkansc_memory_tests)

# Vulkan SC device object tests
add_executable(vulkansc_object_tests
    vulkansc/DeviceObjectManagerTests.cpp
)

target_link_libraries(vulkansc_object_tests PRIVATE MduX::MduX)

set_target_properties(vulkansc_object_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(vulkansc_object_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vulkansc_object_tests PRIVATE /experimental:module)
    target_compile_options(vulkansc_object_tests PRIVATE /std:c++latest)
endif()

add_test(NAME VulkanSCDeviceObjectTests COMMAND vulkansc_object_tests)
