# Examples for MduX Medical Device UI Library

# Simple Medical UI integration example using committed module interface
add_executable(MedicalUiExample 
    SimpleMedicalUiExample.cpp
)

target_link_libraries(MedicalUiExample PRIVATE MduX::MduX)

# Find GLFW for the example (user's windowing solution)
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    target_link_libraries(MedicalUiExample PRIVATE glfw)
    message(STATUS "Using system GLFW for MedicalUiExample")
else()
    # Fallback to CPM for examples only
    CPMAddPackage(NAME glfw GITHUB_REPOSITORY glfw/glfw GIT_TAG 3.4 
                  OPTIONS "GLFW_BUILD_DOCS OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_EXAMPLES OFF" "GLFW_INSTALL OFF")
    target_link_libraries(MedicalUiExample PRIVATE glfw)
    message(STATUS "Using CPM GLFW for MedicalUiExample")
endif()

# Legacy example (disabled during architecture transition)
# The MduXExample uses the old windowing architecture and needs to be updated
# Use MedicalUiExample instead, which demonstrates the new pure Vulkan integration
# 
# add_executable(MduXExample 
#     MduXExample.cpp
# )
# 
# # Add VulkanCubeRenderer module for legacy example
# target_sources(MduXExample
#     PUBLIC
#         FILE_SET CXX_MODULES FILES
#             VulkanCubeRenderer.cppm
#     PRIVATE
#         VulkanCubeRenderer.cpp
# )
# 
# target_link_libraries(MduXExample PRIVATE MduX::MduX)

# Platform-specific libraries for file watching
if(UNIX)
    # Linux needs pthread for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(MedicalUiExample PRIVATE Threads::Threads)
    # target_link_libraries(MduXExample PRIVATE Threads::Threads) # Disabled legacy example
endif()

# Set C++23 standard for examples and enable import std; support
set_target_properties(MedicalUiExample PROPERTIES 
    CXX_STANDARD 23 
    CXX_STANDARD_REQUIRED ON 
    CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(MedicalUiExample PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MedicalUiExample PRIVATE /experimental:module)
    target_compile_options(MedicalUiExample PRIVATE /std:c++latest)
    # target_compile_options(MduXExample PRIVATE /experimental:module) # Disabled legacy example
    # target_compile_options(MduXExample PRIVATE /std:c++latest) # Disabled legacy example
endif()