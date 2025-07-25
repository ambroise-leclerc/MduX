cmake_minimum_required(VERSION 4.0 FATAL_ERROR)

# Prevent in-source builds
include(cmake/PreventInSourceBuilds.cmake)

# Project definition
project(
    MduX
    VERSION 0.1.0
    DESCRIPTION "Modern C++23 Header-Only UI Library for Medical Devices"
    HOMEPAGE_URL "https://github.com/ambroise-leclerc/MduX"
    LANGUAGES CXX
)

# Set C++23 standard requirement
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Supported platforms (macOS removed as per ADR-001 revision)
if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "MduX only supports Windows and Linux platforms. macOS support has been discontinued.")
endif()

if(APPLE)
    message(FATAL_ERROR "macOS support has been discontinued in MduX v0.1.0. Only Windows and Linux are supported.")
endif()

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(StandardProjectSettings)
include(Cache)
include(CPM)
include(CompilerSettings)
include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalyzers)
include(Doxygen)
include(DependencySummaryTable)

# Options
option(MDUX_BUILD_EXAMPLES "Build MduX examples" ON)
option(MDUX_BUILD_TESTS "Build MduX tests" ON)
option(MDUX_BUILD_DOCS "Build MduX documentation" ON)
option(MDUX_ENABLE_REGULATORY_DOCS "Generate regulatory compliance documentation" ON)

# Create interface targets for proper configuration layering
add_library(MduX INTERFACE)
add_library(MduX::MduX ALIAS MduX)

# Create options target for compiler and linker settings
add_library(MduX_options INTERFACE)
target_compile_features(MduX_options INTERFACE cxx_std_23)

# Apply compiler-specific settings
configure_compiler_settings(MduX_options)

# Create warnings target
add_library(MduX_warnings INTERFACE)

# Configure linker settings
include(Linker)
configure_linker(MduX_options)

# Configure project warnings
set_project_warnings(MduX_warnings)

# Enable sanitizers
enable_sanitizers(MduX_options)

# Include directories
target_include_directories(MduX
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Medical device specific compile options
target_compile_options(MduX_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:GNU>:-fconcepts-diagnostics-depth=2>
    $<$<CXX_COMPILER_ID:Clang>:-fconcepts-diagnostics-depth=2>
)

# Platform-specific definitions (macOS removed)
target_compile_definitions(MduX_options INTERFACE
    $<$<PLATFORM_ID:Windows>:MDUX_PLATFORM_WINDOWS>
    $<$<PLATFORM_ID:Linux>:MDUX_PLATFORM_LINUX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

# Medical device compliance definitions
target_compile_definitions(MduX_options INTERFACE
    MDUX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MDUX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MDUX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    MDUX_MEDICAL_DEVICE_COMPLIANCE=1
)

# Dependencies using CPMAddPackage for Windows compatibility

# Vulkan dependency (mandatory for UI library as per ADR-001 revision)
find_package(Vulkan REQUIRED)
target_link_libraries(MduX_options INTERFACE Vulkan::Vulkan)

# Threads dependency
find_package(Threads REQUIRED)
target_link_libraries(MduX_options INTERFACE Threads::Threads)

# GLFW for cross-platform window creation and Vulkan surface management  
# Using CPMAddPackage for Windows compatibility instead of pkg-config
CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.4
    OPTIONS
        "GLFW_BUILD_DOCS OFF"
        "GLFW_BUILD_TESTS OFF"
        "GLFW_BUILD_EXAMPLES OFF"
        "GLFW_INSTALL OFF"
        "GLFW_BUILD_WAYLAND ON"   # Enable Wayland support
        "GLFW_BUILD_X11 ON"       # Enable X11 support for WSL compatibility
)

# GLFW is available for use but users must explicitly link it and define MDUX_GLFW_AVAILABLE
# This allows the library to work without GLFW for basic functionality

# Vulkan version and configuration definitions
target_compile_definitions(MduX_options INTERFACE
    MDUX_VULKAN_VERSION_MAJOR=1
    MDUX_VULKAN_VERSION_MINOR=3
    MDUX_VULKAN_VERSION_PATCH=0
    MDUX_VULKAN_VALIDATION_LAYERS=$<IF:$<CONFIG:Debug>,1,0>
    MDUX_GRAPHICS_ENABLED=1
)

# Windows runtime library configuration for medical device compliance
if(MSVC)
    set_property(TARGET MduX_options PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET MduX_warnings PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Link all configuration targets to main library
target_link_libraries(MduX INTERFACE MduX_options MduX_warnings)

# Configuration targets are already set up above

# Examples
if(MDUX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(MDUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Documentation
if(MDUX_BUILD_DOCS)
    if(MDUX_ENABLE_REGULATORY_DOCS)
        enable_doxygen("awesome-sidebar")
    else()
        enable_doxygen("awesome")
    endif()
endif()

# Installation
include(GNUInstallDirs)

# Install headers
install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install targets (include all configuration targets)
install(
    TARGETS MduX MduX_options MduX_warnings
    EXPORT MduXTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install export
install(
    EXPORT MduXTargets
    FILE MduXTargets.cmake
    NAMESPACE MduX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX
)

# Create config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MduXConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX
)

# Display project status
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════╗")
message(STATUS "║                    MduX v${PROJECT_VERSION}                   ║")
message(STATUS "║        Medical Device UI Library (C++23)         ║")
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

# Display dependency summary
display_dependency_summary("Dependencies" Threads Vulkan)