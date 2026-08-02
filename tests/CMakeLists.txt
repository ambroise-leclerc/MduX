# Tests for MduX Medical Device UI Library
#
# Built on the in-repo MduXTest framework (tests/framework/MduXTest.cppm +
# MduXTest.hpp) rather than a third-party framework - a zero-SOUP library
# should not depend on a framework it would itself need to qualify. See
# ADR-004/ADR-005 and issue #44 for the rationale.

include(${CMAKE_SOURCE_DIR}/cmake/MduXTestDiscovery.cmake)

# Governed-zone module tests. Deliberately links MduX::Core only, not MduX::MduX -
# this is itself a check that the governed modules are usable without pulling in
# Vulkan (see ADR-004).
add_executable(core_tests
    core/CoreTests.cpp
)

target_sources(core_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(core_tests PRIVATE MduX::Core)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(core_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(core_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(core_tests PRIVATE /experimental:module)
    target_compile_options(core_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(core_tests)

# Evidence-kernel and governance tests (ADR-007, issues #34-35). Links MduX::Core only, for the
# same reason core_tests does: both are governed modules, so a test that needed MduX::MduX to
# build them would mean the trust-zone boundary had been crossed somewhere.
add_executable(evidence_tests
    evidence/EvidenceTestMain.cpp
    evidence/DigestTests.cpp
    evidence/JsonTests.cpp
    evidence/ReportTests.cpp
    governance/JustificationTests.cpp
    governance/ComplianceProgramTests.cpp
    governance/ComplianceTests.cpp
)

target_sources(evidence_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(evidence_tests PRIVATE MduX::Core)
target_include_directories(evidence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(evidence_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(evidence_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(evidence_tests PRIVATE /experimental:module)
    target_compile_options(evidence_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(evidence_tests)

# Host-tools tests (ADR-004 host-tools zone). Links MduX::ToolsCommon, which is deliberately not
# a governed target - these tests exercise code permitted to throw (ADR-005).
add_executable(tools_tests
    tools/ToolsTestMain.cpp
    tools/TomlTests.cpp
    tools/CliTests.cpp
)

target_sources(tools_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(tools_tests PRIVATE MduX::ToolsCommon)
target_include_directories(tools_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(tools_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(tools_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(tools_tests PRIVATE /experimental:module)
    target_compile_options(tools_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(tools_tests)

# Shader schema and baker (issue #119)
#
# Links MduX::ShaderBakeLib rather than the executable, so the tests drive run()/write()/verify()
# as calls. Spawning mdux-shaderbake per case would make a failing assertion report only that an
# exit status was wrong, which is the least useful thing it could say.
add_executable(shader_tests
    shader/ShaderTestMain.cpp
    shader/SchemaTests.cpp
    shader/SpirvTests.cpp
    shader/ShaderBakeTests.cpp
)

target_sources(shader_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(shader_tests PRIVATE MduX::ShaderBakeLib)
target_include_directories(shader_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(shader_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(shader_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(shader_tests PRIVATE /experimental:module)
    target_compile_options(shader_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(shader_tests)

# Basic unit tests
add_executable(unit_tests
    TestMain.cpp
    TestVersion.cpp
    TestCompliance.cpp
)

target_sources(unit_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(unit_tests PRIVATE MduX::MduX)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

mdux_discover_tests(unit_tests)

# Medical device compliance tests
add_executable(compliance_tests
    ComplianceTestMain.cpp
    TestRegulatoryCompliance.cpp
)

target_sources(compliance_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(compliance_tests PRIVATE MduX::MduX)
target_include_directories(compliance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

mdux_discover_tests(compliance_tests)

# Vulkan SC specific tests
add_executable(vulkansc_memory_tests
    vulkansc/MemoryPoolManagerTests.cpp
)

target_sources(vulkansc_memory_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(vulkansc_memory_tests PRIVATE MduX::MduX)
target_include_directories(vulkansc_memory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

mdux_discover_tests(vulkansc_memory_tests)

# Vulkan SC device object tests
add_executable(vulkansc_object_tests
    vulkansc/DeviceObjectManagerTests.cpp
)

target_sources(vulkansc_object_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(vulkansc_object_tests PRIVATE MduX::MduX)
target_include_directories(vulkansc_object_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

mdux_discover_tests(vulkansc_object_tests)
