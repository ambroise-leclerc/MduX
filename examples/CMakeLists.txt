# Examples for MduX Medical Device UI Library

# Basic example demonstrating library usage
add_executable(basic_example BasicExample.cpp)
target_link_libraries(basic_example PRIVATE MduX::MduX glfw)
target_compile_definitions(basic_example PRIVATE MDUX_GLFW_AVAILABLE=1)

# Medical device UI example
add_executable(medical_ui_example MedicalUiExample.cpp)
target_link_libraries(medical_ui_example PRIVATE MduX::MduX glfw)
target_compile_definitions(medical_ui_example PRIVATE MDUX_GLFW_AVAILABLE=1)

# Set C++23 standard for examples
set_target_properties(basic_example medical_ui_example
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)