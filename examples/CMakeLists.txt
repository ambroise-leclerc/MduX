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
find_program(GLSL_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/Bin)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(VERT_SHADER ${SHADER_DIR}/triangle.vert)
set(FRAG_SHADER ${SHADER_DIR}/triangle.frag)
set(VERT_SPV ${SHADER_BINARY_DIR}/triangle.vert.spv)
set(FRAG_SPV ${SHADER_BINARY_DIR}/triangle.frag.spv)

if(GLSL_VALIDATOR)
    add_custom_command(
        OUTPUT ${VERT_SPV}
        COMMAND ${GLSL_VALIDATOR} -V ${VERT_SHADER} -o ${VERT_SPV}
        DEPENDS ${VERT_SHADER}
        COMMENT "Compiling vertex shader"
    )

    add_custom_command(
        OUTPUT ${FRAG_SPV}
        COMMAND ${GLSL_VALIDATOR} -V ${FRAG_SHADER} -o ${FRAG_SPV}
        DEPENDS ${FRAG_SHADER}
        COMMENT "Compiling fragment shader"
    )

    add_custom_target(CompileShaders DEPENDS ${VERT_SPV} ${FRAG_SPV})
else()
    message(WARNING "glslangValidator not found. Shaders will not be compiled.")
endif()

# Vulkan SC Triangle Demo - Visual rendering with static memory
# Temporarily disabled on GCC 15 due to compiler ICE bugs with C++23 modules
# (Segmentation fault in std::array with modules, fixed in future GCC versions)
if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0))
    add_executable(VulkanSCTriangleExample
        VulkanSCTriangleExample.cpp
    )

    if(GLSL_VALIDATOR)
        add_dependencies(VulkanSCTriangleExample CompileShaders)
    endif()

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