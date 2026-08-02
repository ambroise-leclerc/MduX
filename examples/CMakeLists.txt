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

# Compile shaders for triangle demo
# No shader compilation here any more. SPIR-V is committed under recipes/shader/triangle/ and
# baked into generated/shader/triangle/ (issue #122), which removes both the build-time dependency
# on glslangValidator and the runtime dependency on the working directory.

# Vulkan SC Triangle Demo - visual rendering with static memory.
#
# The exclusion below is issue #48 item 2. It used to read VERSION_GREATER_EQUAL 15.0 while its
# message named only GCC 15, so it silently disabled the example on GCC 16 as well - meaning the
# example was built on MSVC alone and neither Linux leg compiled it at all.
#
# Now scoped to the version range where the ICE is actually reproduced. Confirmed still present
# on GCC 15.2.0 while writing this: "internal compiler error: Segmentation fault" inside
# <array>:102 compiling this translation unit. GCC 16 is presumed fixed and is now enabled - if
# that is wrong, the GCC 16 leg says so rather than the example being quietly absent everywhere.
if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0))
    add_executable(VulkanSCTriangleExample
        VulkanSCTriangleExample.cpp
    )

    # The generated module lives in the build tree, so it needs a file set naming that directory.
    target_sources(VulkanSCTriangleExample
        PRIVATE
            FILE_SET generated_modules
                TYPE CXX_MODULES
                BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
                FILES ${MDUX_TRIANGLE_SHADER_MODULE}
    )
    add_dependencies(VulkanSCTriangleExample emit-shader-triangle)

    target_link_libraries(VulkanSCTriangleExample PRIVATE MduX::MduX)

    # GLFW for windowing
    if(glfw3_FOUND)
        target_link_libraries(VulkanSCTriangleExample PRIVATE glfw)
    else()
        target_link_libraries(VulkanSCTriangleExample PRIVATE glfw)
    endif()

    # Set C++23 standard
    set_target_properties(VulkanSCTriangleExample PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(TARGET __CMAKE::CXX23)
        set_target_properties(VulkanSCTriangleExample PROPERTIES CXX_MODULE_STD ON)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(VulkanSCTriangleExample PRIVATE /experimental:module /std:c++latest)
    endif()

    if(UNIX)
        target_link_libraries(VulkanSCTriangleExample PRIVATE Threads::Threads)
    endif()
else()
    message(STATUS "Skipping VulkanSCTriangleExample on GCC 15 due to compiler ICE bugs with C++23 modules")
endif()

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