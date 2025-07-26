# CompilerSettings.cmake
# Handle compiler-specific configuration and generator optimizations

# Configure Ninja generator for optimal builds
if(CMAKE_GENERATOR STREQUAL "Ninja")
  # Enable colored output for Ninja
  set(CMAKE_COLOR_MAKEFILE ON)
  set(CMAKE_COLOR_DIAGNOSTICS ON)
  # Use response files for long command lines to handle very long command lines
  set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
  message(STATUS "Using Ninja build system with optimizations")
endif()

# MSVC specific compiler settings
function(configure_msvc_settings target_name)
  if(MSVC)
    target_compile_options(${target_name} INTERFACE 
      /EHsc  # Enable exception handling
      /W4    # Warning level 4
      /wd4530  # Disable C4530 warning about exception handling
    )
    message(STATUS "Applied MSVC-specific compiler settings to ${target_name}")
  endif()
endfunction()

# Function to apply compiler-specific settings and definitions to a target
function(configure_compiler_settings target_name)
  # Apply MSVC settings if applicable
  configure_msvc_settings(${target_name})
  
  # Medical device specific compile options
  target_compile_options(${target_name} INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:GNU>:-fconcepts-diagnostics-depth=2>
    $<$<CXX_COMPILER_ID:Clang>:-fconcepts-diagnostics-depth=2>
  )

  # Platform-specific definitions
  target_compile_definitions(${target_name} INTERFACE
    $<$<PLATFORM_ID:Windows>:MDUX_PLATFORM_WINDOWS;WIN32_LEAN_AND_MEAN;NOMINMAX>
    $<$<PLATFORM_ID:Linux>:MDUX_PLATFORM_LINUX>
  )
endfunction()

# Function to configure medical device compliance definitions
function(configure_medical_compliance target_name version_major version_minor version_patch)
  target_compile_definitions(${target_name} INTERFACE
    MDUX_VERSION_MAJOR=${version_major}
    MDUX_VERSION_MINOR=${version_minor}
    MDUX_VERSION_PATCH=${version_patch}
    MDUX_MEDICAL_DEVICE_COMPLIANCE=1
  )
endfunction()

# Function to configure Vulkan definitions
function(configure_vulkan_definitions target_name)
  target_compile_definitions(${target_name} INTERFACE
    MDUX_VULKAN_VERSION_MAJOR=1
    MDUX_VULKAN_VERSION_MINOR=3
    MDUX_VULKAN_VERSION_PATCH=0
    MDUX_VULKAN_VALIDATION_LAYERS=$<IF:$<CONFIG:Debug>,1,0>
    MDUX_GRAPHICS_ENABLED=1
  )
endfunction()
