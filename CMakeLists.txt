cmake_minimum_required(VERSION 4.0.0)

# CRITICAL: Must be the first thing after cmake_minimum_required for toolchain detection
# Official UUID for CMake 4.0.3 import std support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

include(cmake/PreventInSourceBuilds.cmake)

project(MduX VERSION 0.1.0 DESCRIPTION "Modern C++23 Modules UI Library for Medical Devices" HOMEPAGE_URL "https://github.com/ambroise-leclerc/MduX" LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

# Debug: Check if import std support was detected
message(STATUS "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD: ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
if(TARGET __CMAKE::CXX23)
    message(STATUS "__CMAKE::CXX23 target available")
else()
    message(WARNING "__CMAKE::CXX23 target NOT available - import std; will not work")
endif()

# Compiler version requirements for C++23 modules support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.40")
        message(FATAL_ERROR "MSVC 17.14 (Visual Studio 2022 version 17.10) or later is required for C++23 modules support. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message(FATAL_ERROR "GCC 15 or later is required for C++23 modules support. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "20.0")
        message(FATAL_ERROR "Clang 20 or later is required for C++23 modules support. Current version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
else()
    message(WARNING "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. MduX requires MSVC 17.14+, GCC 15+, or Clang 20+ for C++23 modules support.")
endif()

# Platform validation
if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "MduX only supports Windows and Linux platforms.")
endif()

# C++23 Modules support
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# GCC-specific modules configuration
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fmodules>)
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

# Build options
option(MDUX_BUILD_EXAMPLES "Build MduX examples" ON)
option(MDUX_BUILD_TESTS "Build MduX tests" ON)
option(MDUX_BUILD_DOCS "Build MduX documentation" OFF)
option(MDUX_ENABLE_REGULATORY_DOCS "Generate regulatory compliance documentation" ON)

# Target architecture - C++23 modules approach
add_library(MduX)
add_library(MduX::MduX ALIAS MduX)
add_library(MduX_options INTERFACE)
add_library(MduX_warnings INTERFACE)

# C++23 modules configuration
target_sources(MduX
    PUBLIC
        FILE_SET CXX_MODULES FILES
            include/mdux/mdux.cppm
    PRIVATE
        src/mdux.cpp
)

target_compile_features(MduX_options INTERFACE cxx_std_23)
set_target_properties(MduX PROPERTIES CXX_MODULE_STD ON)
target_include_directories(MduX PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

# Configuration application
configure_compiler_settings(MduX_options)
configure_medical_compliance(MduX_options ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
configure_vulkan_definitions(MduX_options)
configure_linker(MduX_options)
set_project_warnings(MduX_warnings)

# Note: Removed -Wnull-dereference from CompilerWarnings.cmake to fix GCC 15 + import std compatibility
enable_sanitizers(MduX_options)


# Dependencies
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(MduX_options INTERFACE Vulkan::Vulkan Threads::Threads)

CPMAddPackage(NAME glfw GITHUB_REPOSITORY glfw/glfw GIT_TAG 3.4 OPTIONS "GLFW_BUILD_DOCS OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_EXAMPLES OFF" "GLFW_INSTALL OFF" "GLFW_BUILD_WAYLAND ON" "GLFW_BUILD_X11 ON")

# Configure GLFW integration when available
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES CXX_MODULE_STD OFF)
    # Define MDUX_GLFW_AVAILABLE when GLFW is available
    target_compile_definitions(MduX_options INTERFACE MDUX_GLFW_AVAILABLE=1)
    # Link privately to avoid export issues
    target_link_libraries(MduX PRIVATE glfw)
endif()

# Windows runtime library configuration
if(MSVC)
    set_property(TARGET MduX_options MduX_warnings PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Target linking
target_link_libraries(MduX PUBLIC MduX_options MduX_warnings)

# Subdirectories
if(MDUX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(MDUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(MDUX_BUILD_DOCS)
    enable_doxygen($<IF:$<BOOL:${MDUX_ENABLE_REGULATORY_DOCS}>,"awesome-sidebar","awesome">)
endif()

# Installation (temporarily disabled for import std testing)
# include(GNUInstallDirs)
# install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.cppm")
# install(TARGETS MduX MduX_options MduX_warnings EXPORT MduXTargets 
#         INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#         FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mdux)
# install(EXPORT MduXTargets FILE MduXTargets.cmake NAMESPACE MduX:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

# include(CMakePackageConfigHelpers)
# configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MduXConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)
# write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
# install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

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