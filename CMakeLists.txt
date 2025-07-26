cmake_minimum_required(VERSION 4.0 FATAL_ERROR)

include(cmake/PreventInSourceBuilds.cmake)

project(MduX VERSION 0.1.0 DESCRIPTION "Modern C++23 Header-Only UI Library for Medical Devices" HOMEPAGE_URL "https://github.com/ambroise-leclerc/MduX" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform validation
if(NOT WIN32 AND NOT UNIX)
    message(FATAL_ERROR "MduX only supports Windows and Linux platforms.")
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

# Target architecture - layered configuration approach
add_library(MduX INTERFACE)
add_library(MduX::MduX ALIAS MduX)
add_library(MduX_options INTERFACE)
add_library(MduX_warnings INTERFACE)

target_compile_features(MduX_options INTERFACE cxx_std_23)
target_include_directories(MduX INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

# Configuration application
configure_compiler_settings(MduX_options)
configure_medical_compliance(MduX_options ${PROJECT_VERSION_MAJOR} ${PROJECT_VERSION_MINOR} ${PROJECT_VERSION_PATCH})
configure_vulkan_definitions(MduX_options)
configure_linker(MduX_options)
set_project_warnings(MduX_warnings)
enable_sanitizers(MduX_options)

# Dependencies
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(MduX_options INTERFACE Vulkan::Vulkan Threads::Threads)

CPMAddPackage(NAME glfw GITHUB_REPOSITORY glfw/glfw GIT_TAG 3.4 OPTIONS "GLFW_BUILD_DOCS OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_EXAMPLES OFF" "GLFW_INSTALL OFF" "GLFW_BUILD_WAYLAND ON" "GLFW_BUILD_X11 ON")

# Windows runtime library configuration
if(MSVC)
    set_property(TARGET MduX_options MduX_warnings PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Target linking
target_link_libraries(MduX INTERFACE MduX_options MduX_warnings)

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

# Installation
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(TARGETS MduX MduX_options MduX_warnings EXPORT MduXTargets INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MduXTargets FILE MduXTargets.cmake NAMESPACE MduX:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MduXConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MduXConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/MduXConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MduX)

# Status display
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

display_dependency_summary("Dependencies" Threads Vulkan)