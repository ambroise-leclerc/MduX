# TestInstallConsumer.cmake
#
# Proves the install tree actually works, not just that `cmake --install`
# exits 0: installs to a scratch prefix, then configures, builds, and RUNS a
# tiny external consumer project against that prefix via find_package(MduX).
# The consumer program asserts a real value from the installed module, so a
# successful run means import std + the installed FILE_SET CXX_MODULES are
# both genuinely usable from outside this source tree, not just present on
# disk.
#
# Run via: cmake -D BUILD_DIR=... -D CXX_COMPILER=... -D GENERATOR=... -P TestInstallConsumer.cmake
# (see the add_test(NAME InstallTreeConsumer ...) call in the top-level CMakeLists.txt)

foreach(required_var BUILD_DIR CXX_COMPILER GENERATOR)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "TestInstallConsumer.cmake: ${required_var} must be set with -D")
    endif()
endforeach()

set(install_prefix "${BUILD_DIR}/_test_install_prefix")
set(consumer_src "${BUILD_DIR}/_test_consumer_src")
set(consumer_build "${BUILD_DIR}/_test_consumer_build")

file(REMOVE_RECURSE "${install_prefix}" "${consumer_src}" "${consumer_build}")

message(STATUS "InstallTreeConsumer: installing to ${install_prefix}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed (${install_result}):\n${install_output}\n${install_error}")
endif()

# A minimal external project - deliberately not part of this repository's own
# CMake build graph, so it can only see MduX through find_package(), exactly
# as a real downstream consumer would.
file(WRITE "${consumer_src}/main.cpp" "\
import std;\n\
import mdux;\n\
\n\
int main() {\n\
    // A real assertion, not just \"it links\": confirms the installed module\n\
    // is actually usable and its constexpr data survived installation intact.\n\
    if (mdux::Version::getString() != \"0.1.0\") {\n\
        return 1;\n\
    }\n\
    if (!mdux::Compliance::isMedicalDeviceCompliant) {\n\
        return 2;\n\
    }\n\
    return 0;\n\
}\n\
")

file(WRITE "${consumer_src}/CMakeLists.txt" "\
cmake_minimum_required(VERSION 4.0.0)\n\
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD \"d0edc3af-4c50-42ea-a356-e2862fe7a444\")\n\
set(CMAKE_CXX_STANDARD 23)\n\
set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\
set(CMAKE_CXX_EXTENSIONS OFF)\n\
project(MduXInstallConsumer LANGUAGES CXX)\n\
set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n\
find_package(MduX REQUIRED)\n\
add_executable(consumer main.cpp)\n\
target_link_libraries(consumer PRIVATE MduX::MduX)\n\
if(TARGET __CMAKE::CXX23)\n\
    set_target_properties(consumer PROPERTIES CXX_MODULE_STD ON)\n\
endif()\n\
")

message(STATUS "InstallTreeConsumer: configuring consumer project")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -B "${consumer_build}" -S "${consumer_src}"
            -G "${GENERATOR}"
            -DCMAKE_CXX_COMPILER=${CXX_COMPILER}
            -DCMAKE_PREFIX_PATH=${install_prefix}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Consumer configure failed (${configure_result}):\n${configure_output}\n${configure_error}")
endif()

message(STATUS "InstallTreeConsumer: building consumer project")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Consumer build failed (${build_result}):\n${build_output}\n${build_error}")
endif()

message(STATUS "InstallTreeConsumer: running consumer executable")
execute_process(
    COMMAND "${consumer_build}/consumer"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Consumer executable exited with ${run_result} (expected 0 - see main.cpp's assertions)")
endif()

message(STATUS "InstallTreeConsumer: OK")
