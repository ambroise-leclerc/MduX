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
    shader/UiPackageTests.cpp
    shader/EmitTests.cpp
    shader/GeneratedTests.cpp
    shader/GeneratedModuleConsumer.cpp
    shader/GeneratedHeaderConsumer.cpp
)

# The generated module joins a file set; the generated header is reached through the include
# directory. Both are build-tree paths (issue #121) - nothing generated is committed.
#
# Compiling both forms in one binary is #121's acceptance criterion, not incidental. It works
# because the module form is attached to a named module and the header form is not, so they are
# distinct entities that can coexist; GeneratedTests then asserts they describe the same bytes and
# the same contract.
#
# The generated module needs its own named file set: the default CXX_MODULES set's base directory
# is this source directory, and a generated module lives in the build tree.
target_sources(shader_tests
    PRIVATE
        FILE_SET CXX_MODULES FILES framework/MduXTest.cppm
        FILE_SET generated_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
            FILES ${MDUX_UI_SHADER_MODULE}
)
add_dependencies(shader_tests emit-shader-mdux-ui)
target_link_libraries(shader_tests PRIVATE MduX::ShaderBakeLib)
target_include_directories(shader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SHADER_DIR}
)

# UiPackageTests reads the committed artifact under generated/, which is a fixed location in the
# source tree rather than anything the build produces. It reads only; the source-tree-cleanliness
# gate in CI stays satisfied.
target_compile_definitions(shader_tests PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# The build's own answers for mdux_shader_identifier(), written where GeneratedTests can read them
# and compare against identifierFor() in the C++ emitter.
#
# Both halves have to be *executed* for this to mean anything. GeneratedTests previously asserted
# identifierFor()'s behaviour and called that "kept in step with cmake/MduXShaderEmit.cmake" - but
# a C++ assertion cannot observe the CMake regex, so the two drifted: the CMake side never applied
# the leading-underscore rule for ids beginning with a digit, and would have looked for `2d.cppm`
# while the emitter wrote `_2d.cppm`.
set(mdux_identifier_parity_ids "mdux-ui" "a.b-c" "plain" "2d" "3d" "already_fine" "a b" "ui.v2")
set(mdux_identifier_parity_lines "")
foreach(mdux_identifier_parity_id IN LISTS mdux_identifier_parity_ids)
    mdux_shader_identifier(mdux_identifier_parity_value "${mdux_identifier_parity_id}")
    string(APPEND mdux_identifier_parity_lines
           "${mdux_identifier_parity_id}\t${mdux_identifier_parity_value}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shader_identifier_parity.tsv"
     "${mdux_identifier_parity_lines}")
target_compile_definitions(shader_tests PRIVATE
    MDUX_IDENTIFIER_PARITY_FILE="${CMAKE_CURRENT_BINARY_DIR}/shader_identifier_parity.tsv")

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

# Governed fixed-budget draw types (issue #123)
#
# Links MduX::Core only, deliberately. mdux.draw contains no Vulkan handle and no device concept,
# so a test that needed MduX::MduX to build it would mean the trust-zone boundary had moved -
# which is the property #125's headless verification and #126's pixel test both rest on.
add_executable(draw_tests
    draw/DrawTestMain.cpp
    draw/DrawTests.cpp
)

target_sources(draw_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(draw_tests PRIVATE MduX::Core)
target_include_directories(draw_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(draw_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(draw_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(draw_tests PRIVATE /experimental:module)
    target_compile_options(draw_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(draw_tests)

# Adapter-zone Vulkan renderer (issue #124)
#
# Links MduX::MduX, so this suite sees Vulkan where draw_tests deliberately does not. Only the
# device-free half of the renderer is covered here - create()'s validation runs before the first
# Vulkan call, so it is testable with no ICD present, which is what CI currently has. The device
# path lands with #125's headless harness.
add_executable(render_tests
    render/RenderTestMain.cpp
    render/VulkanRendererTests.cpp
)

target_sources(render_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(render_tests PRIVATE MduX::MduX)
target_include_directories(render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(render_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(render_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(render_tests PRIVATE /experimental:module)
    target_compile_options(render_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(render_tests)

# Headless offscreen rendering and readback (issue #125)
#
# Separate from render_tests because this suite needs a real Vulkan device where that one
# deliberately needs none. It consumes the generated shader module, so it depends on the emitter
# the same way shader_tests does.
#
# SKIP_RETURN_CODE 77 is the point: with no loader, no ICD or no device the binary exits 77 and
# CTest reports Skipped, which is a distinct outcome from passing. A contributor without Vulkan
# sees neither a red test they cannot act on nor a green one that ran nothing.
add_executable(offscreen_tests
    render/OffscreenTestMain.cpp
    render/OffscreenTests.cpp
)

target_sources(offscreen_tests
    PRIVATE
        FILE_SET CXX_MODULES FILES framework/MduXTest.cppm
        FILE_SET generated_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
            FILES ${MDUX_UI_SHADER_MODULE}
)
add_dependencies(offscreen_tests emit-shader-mdux-ui)
target_link_libraries(offscreen_tests PRIVATE MduX::MduX)
target_include_directories(offscreen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SHADER_DIR}
)

set_target_properties(offscreen_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(offscreen_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(offscreen_tests PRIVATE /experimental:module)
    target_compile_options(offscreen_tests PRIVATE /std:c++latest)
endif()

# Registered as one test rather than through mdux_discover_tests: discovery runs the binary with
# --list-tests at build time, which on a machine with no device would list nothing and silently
# register no tests at all. One entry that can report Skipped is honest where zero entries is not.
add_test(NAME offscreen_tests COMMAND offscreen_tests)
set_tests_properties(offscreen_tests PROPERTIES LABELS "pixel" SKIP_RETURN_CODE 77)

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
