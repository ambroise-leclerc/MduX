# Examples for MduX Medical Device UI Library

# Unified comprehensive example demonstrating all MduX features
add_executable(MduXExample MduXExample.cpp)
target_link_libraries(MduXExample PRIVATE MduX::MduX)
target_compile_definitions(MduXExample PRIVATE MDUX_GLFW_AVAILABLE=1)

# Platform-specific libraries for file watching
if(UNIX)
    # Linux needs pthread for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(MduXExample PRIVATE Threads::Threads)
endif()

# Set C++23 standard for example and enable import std; support
set_target_properties(MduXExample PROPERTIES 
    CXX_STANDARD 23 
    CXX_STANDARD_REQUIRED ON 
    CXX_EXTENSIONS OFF
    CXX_MODULE_STD ON
)