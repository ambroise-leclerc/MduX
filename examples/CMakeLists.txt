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

# The ECG classifier demonstrator (issue #64, ADR-008).
#
# Links MduX::Core only: the package is generated `constexpr` source, so the device target needs no
# host-tools parser, Vulkan, or windowing dependency.
#
# The one mdux_embed_blob() call is the "weights are data" half. The same package ID selects both
# the generated metadata and its weights, so those two inputs cannot silently diverge here.
# tests/ml/WeightSwapTests.cpp independently exercises both committed packages.
add_executable(EcgClassifierExample
    EcgClassifierExample.cpp
)

target_sources(EcgClassifierExample
    PRIVATE
        FILE_SET generated_model_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_MODEL_DIR}
            FILES ${MDUX_ECG_MODEL_MODULE}
        # The committed endoscope screen as generated `constexpr` C++ (issue #257). Its own file
        # set, because the two emitted modules live under different generated directories and a
        # file set has one set of base dirs. Build-tree only, like the model module beside it.
        #
        # This is what makes the demonstrator a *consumer* of the screen rather than a program that
        # prints numbers: `EndoscopeMonitor` carries a `SignalTrace` named `ECG_LEAD_II`, and the
        # ring this example already had is bound to it. Still no Vulkan and no window - a draw list
        # is geometry, and building one needs neither.
        FILE_SET generated_screen_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SCREEN_DIR}
            FILES ${MDUX_ENDOSCOPE_SCREEN_MODULE}
)
add_dependencies(EcgClassifierExample emit-model-${MDUX_ECG_MODEL_ID} emit-screen-endoscope-monitor)
target_link_libraries(EcgClassifierExample PRIVATE MduX::Core)

mdux_embed_blob(EcgClassifierExample
    FILE   ${CMAKE_SOURCE_DIR}/generated/model/${MDUX_ECG_MODEL_ID}/weights.bin
    SYMBOL ecgModelWeights
)

set_target_properties(EcgClassifierExample
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(EcgClassifierExample PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(EcgClassifierExample PRIVATE /experimental:module /std:c++latest)
endif()
