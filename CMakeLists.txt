cmake_minimum_required(VERSION 4.0.0)

# CRITICAL: Must be the first thing after cmake_minimum_required for toolchain detection
# Official UUID for CMake 4.0.3 import std support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/PreventInSourceBuilds.cmake)

project(MduX VERSION 0.3.0 DESCRIPTION "Modern C++23 Modules UI Library for Medical Devices" HOMEPAGE_URL "https://github.com/ambroise-leclerc/MduX" LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

# Debug: Check if import std support was detected
message(STATUS "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD: ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(TARGET __CMAKE::CXX23)
    message(STATUS "__CMAKE::CXX23 target available")
else()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19.40")
        message(STATUS "__CMAKE::CXX23 target NOT available, but MSVC ${CMAKE_CXX_COMPILER_VERSION} should support import std with /experimental:module")
    else()
        message(WARNING "__CMAKE::CXX23 target NOT available - import std; will not work")
    endif()
endif()

# Compiler version requirements for C++23 modules support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.40")
        message(FATAL_ERROR "MSVC 17.14 (Visual Studio 2022 version 17.10) or later is required for C++23 modules support. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC 16, not 15. GCC 15 builds the library, but cannot build a test suite written against
    # SpecLab: reading a named module that transitively re-exports parts of `std` fails with
    # "failed to load pendings for 'std::_Sp_counted_ptr_inplace'", a defect in GCC's own std BMI
    # rather than in any code here or in SpecLab. It is also the compiler whose ICE keeps
    # VulkanSCTriangleExample excluded, and whose -Warray-bounds false positive on std::string
    # concatenation has failed CI on code the other two toolchains accept.
    #
    # Raising the floor rather than excluding the BDD suites on one leg: a test that does not run
    # on a supported compiler is a coverage hole that reads as coverage.
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        message(FATAL_ERROR "GCC 16 or later is required. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "20.0")
        message(FATAL_ERROR "Clang 20 or later is required for C++23 modules support. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
else()
    message(WARNING "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. MduX requires MSVC 17.14+, GCC 16+, or Clang 20+ for C++23 modules support.")
endif()

# Platform validation
if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "MduX only supports Windows and Linux platforms.")
endif()

# Force 64-bit architecture on Windows for Vulkan compatibility
if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "MduX requires 64-bit build on Windows for Vulkan compatibility. Please use 64-bit compiler.")
    endif()
endif()

# C++23 Modules support
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Compiler-specific modules configuration
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fmodules>)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC-specific modules configuration for C++23 and import std
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/experimental:module>)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/std:c++latest>)
    # Source and execution character sets are both UTF-8. Without this MSVC reads sources in the
    # host's ANSI code page and, worse, encodes a universal-character-name like "²" into that
    # code page rather than into UTF-8 - so `Value::string("mm²")` is two bytes on GCC and
    # Clang and one byte on MSVC, and every test asserting on UTF-8 output fails on Windows only.
    # mdux.evidence.json validates its input as UTF-8 and emits UTF-8 unconditionally (ADR-007);
    # a build whose execution charset is anything else contradicts the format the library defines.
    #
    # Set through CMAKE_CXX_FLAGS rather than add_compile_options() on purpose. /utf-8 defines
    # _UTF8, and MSVC raises C5050 ("possible incompatible environment while importing module
    # 'std'") when a translation unit's macro environment differs from the one the std module was
    # built with. add_compile_options() does not reach CMake's internal __cmake_cxx_std_23 target,
    # so the flag has to be somewhere that target inherits too, or every import std becomes a
    # warning - and warnings are errors here.
    string(APPEND CMAKE_CXX_FLAGS " /utf-8")
endif()

# Module setup
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StandardProjectSettings)
include(Cache)
include(CPM)
include(CompilerSettings)
include(Linker)
include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalyzers)
include(Doxygen)
include(DependencySummaryTable)
include(MduXTrustZones)
include(MduXDeterminism)
include(MduXBuildInfo)
include(MduXBake)

# Artifact provenance for host tools (ADR-007): defines MduX::BuildInfo carrying
# MDUX_TOOL_VERSION and MDUX_GIT_SHA. Runs before any target is created so bakers can link it.
mdux_define_build_info()

# Bake aggregate targets, declared here rather than next to add_subdirectory(tools) below so
# this has no dependency on tools/ existing. They exist even with no artifact registered yet, so
# `--target mdux-bake-all` succeeds trivially rather than failing with "no such target".
mdux_declare_bake_targets()

# Build options
option(MDUX_BUILD_EXAMPLES "Build MduX examples" ON)
option(MDUX_BUILD_TESTS "Build MduX tests" ON)
option(MDUX_BUILD_DOCS "Build MduX documentation" OFF)
option(MDUX_ENABLE_REGULATORY_DOCS "Generate regulatory compliance documentation" ON)

# Target architecture - C++23 modules approach
#
# Trust zones (ADR-004): MduXCore is the governed zone - std only, no Vulkan, no
# windowing - checked mechanically by mdux_verify_trust_zones() at the bottom of
# this file. MduX is the adapter zone: it PUBLIC-links MduXCore plus Vulkan, so
# existing consumers of MduX::MduX are unaffected and get MduXCore transitively.
add_library(MduXCore)
add_library(MduX::Core ALIAS MduXCore)
set_target_properties(MduXCore PROPERTIES EXPORT_NAME Core)

add_library(MduX)
add_library(MduX::MduX ALIAS MduX)
add_library(MduX_options INTERFACE)
add_library(MduX_warnings INTERFACE)

# Compiler warnings are configuration-only and carry no runtime or adapter
# dependency, so this interface target is part of the governed graph too.
mdux_declare_governed(MduXCore)
mdux_declare_governed(MduX_warnings)

# Floating-point determinism (ADR-008, issue #59). Applied to the whole governed
# target rather than to src/ml/Kernels.cpp alone: the kernels are what the rule
# exists for, but no governed module has any business being compiled with
# contraction or fast-math, and a target-wide setting cannot be defeated by
# someone adding a second ML source file and forgetting the per-source property.
mdux_enforce_fp_determinism(MduXCore)

# MduXCore's own module sources. Deliberately does not link MduX_options - that
# interface target carries Vulkan::Vulkan (see the find_package(Vulkan) block
# below), which a governed target must never reach, transitively or otherwise.
target_sources(MduXCore
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES
            include/mdux/core/Units.cppm
            include/mdux/core/Result.cppm
            include/mdux/evidence/Digest.cppm
            include/mdux/evidence/Json.cppm
            include/mdux/evidence/Report.cppm
            include/mdux/governance/Governance.cppm
            include/mdux/governance/Compliance.cppm
            include/mdux/shader/Schema.cppm
            include/mdux/ml/Schema.cppm
            include/mdux/ml/Kernels.cppm
            include/mdux/ml/Runtime.cppm
            include/mdux/draw/Draw.cppm
    PRIVATE
        src/evidence/Digest.cpp
        src/evidence/Json.cpp
        src/evidence/Report.cpp
        src/shader/Schema.cpp
        src/ml/Kernels.cpp
        src/ml/Runtime.cpp
        src/draw/Draw.cpp
        src/governance/Governance.cpp
        src/governance/Justification.cpp
        src/governance/Program.cpp
        src/governance/Compliance.cpp
)
target_compile_features(MduXCore PUBLIC cxx_std_23)
target_include_directories(MduXCore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXCore PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXCore PRIVATE /experimental:module /std:c++latest)
endif()

# C++23 modules configuration
target_sources(MduX
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES
            include/mdux/render/VulkanRenderer.cppm
            include/mdux/render/Offscreen.cppm
            include/mdux/mdux.cppm
            include/mdux/vulkansc/MemoryPoolManager.cppm
            include/mdux/vulkansc/DeviceObjectManager.cppm
    PRIVATE
        src/render/VulkanRenderer.cpp
        src/render/Offscreen.cpp
        src/mdux.cpp
        src/vulkansc/MemoryPoolManager.cpp
        src/vulkansc/DeviceObjectManager.cpp
)

target_compile_features(MduX_options INTERFACE cxx_std_23)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(MduX PROPERTIES CXX_MODULE_STD ON)
    message(STATUS "CXX_MODULE_STD enabled for MduX target")
else()
    message(STATUS "CXX_MODULE_STD disabled - __CMAKE::CXX23 target not available")
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        message(FATAL_ERROR 
            "Visual Studio generator does not support import std; with C++23 modules.\n"
            "Please use Ninja generator instead:\n"
            "  1. Install Ninja: winget install Ninja-build.Ninja\n"
            "  2. Use: cmake .. -G Ninja\n"
            "  3. Build with: cmake --build .")
    endif()
endif()

target_include_directories(MduX PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

# MSVC-specific import std configuration
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduX PRIVATE /experimental:module)
    target_compile_options(MduX PRIVATE /std:c++latest)
    # Ensure import std is available
    target_compile_options(MduX_options INTERFACE /experimental:module)
    target_compile_options(MduX_options INTERFACE /std:c++latest)
endif()

# Configuration application
configure_compiler_settings(MduX_options)
configure_medical_compliance(MduX_options ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
configure_vulkan_definitions(MduX_options)
configure_linker(MduX_options)
set_project_warnings(MduX_warnings)

# Note: Removed -Wnull-dereference from CompilerWarnings.cmake to fix GCC 15 + import std compatibility
enable_sanitizers(MduX_options)


# Dependencies - Enhanced Vulkan detection for Windows
if(WIN32)
    # Common Vulkan SDK installation paths on Windows
    set(VULKAN_SEARCH_PATHS
        "$ENV{VULKAN_SDK}"
        "C:/VulkanSDK/1.3.290.0"
        "C:/VulkanSDK/1.3.280.0"
        "C:/VulkanSDK/1.3.275.0"
        "C:/VulkanSDK/1.3.270.0"
        "C:/Program Files/VulkanSDK/1.3.290.0"
        "C:/Program Files/VulkanSDK/1.3.280.0"
        "C:/Program Files/VulkanSDK/1.3.275.0"
        "C:/Program Files/VulkanSDK/1.3.270.0"
    )
    
    # Try to find Vulkan in common locations
    foreach(VULKAN_PATH ${VULKAN_SEARCH_PATHS})
        if(EXISTS "${VULKAN_PATH}/Include/vulkan/vulkan.h")
            set(Vulkan_INCLUDE_DIR "${VULKAN_PATH}/Include")
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                # 64-bit build - prefer Lib over Lib32
                if(EXISTS "${VULKAN_PATH}/Lib/vulkan-1.lib")
                    set(Vulkan_LIBRARY "${VULKAN_PATH}/Lib/vulkan-1.lib")
                elseif(EXISTS "${VULKAN_PATH}/Lib32/vulkan-1.lib")
                    set(Vulkan_LIBRARY "${VULKAN_PATH}/Lib32/vulkan-1.lib")
                    message(WARNING "Using 32-bit Vulkan library on 64-bit system")
                endif()
            else()
                # 32-bit build
                if(EXISTS "${VULKAN_PATH}/Lib32/vulkan-1.lib")
                    set(Vulkan_LIBRARY "${VULKAN_PATH}/Lib32/vulkan-1.lib")
                elseif(EXISTS "${VULKAN_PATH}/Lib/vulkan-1.lib")
                    set(Vulkan_LIBRARY "${VULKAN_PATH}/Lib/vulkan-1.lib")
                    message(WARNING "Using 64-bit Vulkan library on 32-bit system")
                endif()
            endif()
            if(Vulkan_LIBRARY)
                message(STATUS "Found Vulkan at: ${VULKAN_PATH}")
                message(STATUS "Vulkan library: ${Vulkan_LIBRARY}")
                break()
            endif()
        endif()
    endforeach()
endif()

find_package(Vulkan REQUIRED)

# Provide helpful error message for Windows Vulkan SDK installation
if(WIN32 AND NOT Vulkan_FOUND)
    message(FATAL_ERROR 
        "Vulkan SDK not found on Windows. Please:\n"
        "  1. Download from: https://vulkan.lunarg.com/sdk/home#windows\n"
        "  2. Install the Vulkan SDK\n"
        "  3. Set environment variable: set VULKAN_SDK=C:\\VulkanSDK\\1.3.xxx.x\n"
        "  4. Or specify manually: cmake .. -DVulkan_INCLUDE_DIR=\"C:/VulkanSDK/1.3.xxx.x/Include\" -DVulkan_LIBRARY=\"C:/VulkanSDK/1.3.xxx.x/Lib/vulkan-1.lib\"\n"
        "Current VULKAN_SDK: $ENV{VULKAN_SDK}")
endif()
find_package(Threads REQUIRED)
target_link_libraries(MduX_options INTERFACE Vulkan::Vulkan Threads::Threads)

# MduX is now a pure Vulkan complement library - no windowing dependencies needed

# Windows runtime library configuration
if(MSVC)
    set_property(TARGET MduX_options MduX_warnings PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Target linking
target_link_libraries(MduX PUBLIC MduX::Core MduX_options)
target_link_libraries(MduX PRIVATE MduX_warnings)
target_link_libraries(MduXCore PRIVATE MduX_warnings)

# Subdirectories
#
# tools/ comes before tests/ so the test targets can link MduX::ToolsCommon. It is not gated on
# an option: the bakers under tools/ are what produce every committed artifact, so a build that
# skipped them could not run `ctest -L evidence` at all (ADR-007).
add_subdirectory(tools)

# Baked artifacts (ADR-007). Declared here rather than in tools/ because a bake is a property of
# the repository, not of the tool that happens to produce it - and because mdux_bake_artifact()
# requires its TOOL target to exist already.
#
# Each call registers three things: a build-time bake into the build tree, a `-update` target that
# stages the result into generated/ (the only path in the build permitted to write into the source
# tree), and an `evidence.<kind>.<id>` test that byte-compares the two. A normal build never
# writes into generated/; CI's "Verify no source-tree writes" step depends on that.
mdux_bake_artifact(
    KIND shader
    ID mdux-ui
    TOOL mdux-shaderbake
    RECIPE recipes/shader/mdux-ui.toml
    SOURCES
        recipes/shader/mdux-ui/ui.vert.spv
        recipes/shader/mdux-ui/ui.frag.spv
    OUTPUTS
        package.json
        report.json
        shaders.spv
)

# The triangle example's shaders (issue #122), so the example stops loading SPIR-V from the
# working directory at runtime. Also the first package with an empty contract - no descriptors,
# no push constants - which mdux-ui never exercises.
mdux_bake_artifact(
    KIND shader
    ID triangle
    TOOL mdux-shaderbake
    RECIPE recipes/shader/triangle.toml
    SOURCES
        recipes/shader/triangle/triangle.vert.spv
        recipes/shader/triangle/triangle.frag.spv
    OUTPUTS
        package.json
        report.json
        shaders.spv
)

# Generated C++ for that package (issue #121). Build-tree only, never committed - see
# cmake/MduXShaderEmit.cmake. The renderer (#124) and the triangle example (#122) consume the
# module form; the header form exists for a translation unit that cannot import a named module.
include(cmake/MduXShaderEmit.cmake)
mdux_emit_shader_package(
    ID mdux-ui
    OUT_MODULE MDUX_UI_SHADER_MODULE
    OUT_HEADER MDUX_UI_SHADER_HEADER
    OUT_DIR MDUX_GENERATED_SHADER_DIR
)
mdux_emit_shader_package(
    ID triangle
    OUT_MODULE MDUX_TRIANGLE_SHADER_MODULE
    OUT_DIR MDUX_GENERATED_SHADER_DIR
)

if(MDUX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(MDUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Proves the install tree actually works end-to-end (issue #47): installs to
    # a scratch prefix, then configures/builds/runs a tiny external consumer
    # project against it via find_package(MduX). Old-style positional add_test()
    # deliberately, not add_test(NAME ... COMMAND ...) - see
    # cmake/MduXTestDiscoveryImpl.cmake's comment for why the keyword form
    # mis-parses on this project's CTest version.
    add_test(InstallTreeConsumer
        "${CMAKE_COMMAND}"
        -D "BUILD_DIR=${CMAKE_BINARY_DIR}"
        -D "CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        -D "GENERATOR=${CMAKE_GENERATOR}"
        -D "EXPECTED_VERSION=${PROJECT_VERSION}"
        -P "${CMAKE_SOURCE_DIR}/cmake/TestInstallConsumer.cmake"
    )
endif()
if(MDUX_BUILD_DOCS)
    enable_doxygen($<IF:$<BOOL:${MDUX_ENABLE_REGULATORY_DOCS}>,"awesome-sidebar","awesome">)
endif()

# Trust zone check (ADR-004) - run last, after every add_subdirectory() above, so
# the full link graph (including test/example targets that might accidentally
# link a governed target to something Vulkan-adjacent) is established.
mdux_verify_trust_zones()

# Floating-point determinism check (ADR-008) - run last for the same reason, so a
# fast-math option arriving through any target's interface compile options, or
# through CMAKE_CXX_FLAGS, is seen rather than assumed absent.
mdux_verify_fp_determinism()

# Installation
#
# No separate install(DIRECTORY include/ ...) - the FILE_SET CXX_MODULES install
# below already copies exactly the module interface files, preserving their
# mdux/... relative path via each target's BASE_DIRS. A parallel DIRECTORY install
# duplicated every .cppm at a second, incorrectly-nested path (verified by
# inspecting an actual installed tree while testing issue #47 - do not re-add it
# without checking the result).
include(GNUInstallDirs)
install(TARGETS MduXCore MduX MduX_options MduX_warnings EXPORT MduXTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MduXTargets FILE MduXTargets.cmake NAMESPACE MduX:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MduXConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

# Status display
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════╗")
message(STATUS "║                    MduX v${PROJECT_VERSION}                   ║")
message(STATUS "║      Medical Device UI Library (C++23 Modules)   ║")
message(STATUS "╠══════════════════════════════════════════════════╣")
message(STATUS "║  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "║  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
message(STATUS "║  Examples: ${MDUX_BUILD_EXAMPLES}")
message(STATUS "║  Tests: ${MDUX_BUILD_TESTS}")
message(STATUS "║  Documentation: ${MDUX_BUILD_DOCS}")
message(STATUS "║  Graphics Support: Vulkan 1.3")
message(STATUS "║  Regulatory Docs: ${MDUX_ENABLE_REGULATORY_DOCS}")
message(STATUS "╚══════════════════════════════════════════════════╝")
message(STATUS "")

display_dependency_summary("Dependencies" Threads Vulkan)
