# Examples for MduX Medical Device UI Library

# Building a frame with no device and no window (issue #127).
#
# Deliberately does not link GLFW. It used to, back when it constructed a MedicalUiConfig and a
# UiFileWatcher without ever opening a window; now that it demonstrates mdux.draw, the absence of
# a windowing dependency is part of what it demonstrates.
add_executable(MedicalUiExample
    SimpleMedicalUiExample.cpp
)

target_link_libraries(MedicalUiExample PRIVATE MduX::MduX)

# No shader compilation here any more. SPIR-V is committed under recipes/shader/triangle/ and
# baked into generated/shader/triangle/ (issue #122), which removes both the build-time dependency
# on glslangValidator and the runtime dependency on the working directory.

# Vulkan SC Triangle Demo - visual rendering with static memory.
#
# No longer guarded. The exclusion here was issue #48 item 2: a GCC 15 internal compiler error
# ("internal compiler error: Segmentation fault" inside <array>) compiling this translation unit
# under C++23 modules. With the project's GCC floor raised to 16 the guard became unreachable -
# every GCC that can configure this project is one the ICE was never observed on - and an
# unreachable guard that still names a version is worse than none, because it reads as a live
# constraint. #48 item 2 is closed by the floor rather than by a workaround.
# GLFW is the triangle demo's, and only the triangle demo's - the examples-only windowing
# boundary. MduX itself never links it, which mdux_verify_trust_zones() enforces from the
# other side for the governed target.
#
# Unconditional again now the compiler guard is gone: this example is always built, so GLFW is
# always needed. It was moved inside the guard while the example could be excluded, to avoid a
# find_package and possibly a source download for a target that would not be built.
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    CPMAddPackage(NAME glfw GITHUB_REPOSITORY glfw/glfw GIT_TAG 3.4
                  OPTIONS "GLFW_BUILD_DOCS OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_EXAMPLES OFF" "GLFW_INSTALL OFF")
    message(STATUS "Using CPM GLFW for VulkanSCTriangleExample")
else()
    message(STATUS "Using system GLFW for VulkanSCTriangleExample")
endif()

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

# GLFW for windowing. The conditional this replaces linked `glfw` in both branches, so it
# never chose anything - which is why the target name is resolved above instead.
target_link_libraries(VulkanSCTriangleExample PRIVATE glfw)

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