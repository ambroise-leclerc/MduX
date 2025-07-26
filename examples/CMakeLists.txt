# Examples for MduX Medical Device UI Library

# Unified comprehensive example demonstrating all MduX features
add_executable(MduXExample MduXExample.cpp)
target_link_libraries(MduXExample PRIVATE MduX::MduX glfw)
target_compile_definitions(MduXExample PRIVATE MDUX_GLFW_AVAILABLE=1)
target_sources(MduXExample PRIVATE
    ../include/mdux/FileWatcher.cpp
    ../include/mdux/CssParser.cpp
    ../include/mdux/HtmlCssLoader.cpp
)

# Platform-specific libraries for file watching
if(UNIX)
    # Linux needs pthread for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(MduXExample PRIVATE Threads::Threads)
endif()

# Set C++23 standard for example
set_target_properties(MduXExample
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# UI Integration example demonstrating overlay/underlay rendering
add_executable(UiIntegrationExample UiIntegrationExample.cpp)
target_link_libraries(UiIntegrationExample PRIVATE MduX::MduX glfw)
target_compile_definitions(UiIntegrationExample PRIVATE MDUX_GLFW_AVAILABLE=1)
target_sources(UiIntegrationExample PRIVATE
    ../include/mdux/FileWatcher.cpp
    ../include/mdux/CssParser.cpp
    ../include/mdux/HtmlCssLoader.cpp
)

# Platform-specific libraries for UI integration example
if(UNIX)
    target_link_libraries(UiIntegrationExample PRIVATE Threads::Threads)
endif()

# Set C++23 standard for UI integration example
set_target_properties(UiIntegrationExample
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)