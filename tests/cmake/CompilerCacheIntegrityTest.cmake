foreach(required_variable
        SOURCE_DIR BINARY_DIR CXX_COMPILER GENERATOR CACHE_BINARY CXX_FLAGS EXE_LINKER_FLAGS)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR
            "CompilerCacheIntegrityTest.cmake: ${required_variable} must be set")
    endif()
endforeach()

function(run_checked description)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR
            "${description} failed (${command_result}):\n${command_stdout}\n${command_stderr}")
    endif()
endfunction()

function(assert_probe expected fixture_build)
    execute_process(
        COMMAND "${fixture_build}/probe"
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_output
        ERROR_VARIABLE probe_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT probe_result EQUAL 0 OR NOT probe_output STREQUAL "${expected}")
        message(FATAL_ERROR
            "Probe reported '${probe_output}' with status ${probe_result}; expected '${expected}'.\n${probe_error}")
    endif()
endfunction()

set(fixture_root "${BINARY_DIR}/compiler-cache-integrity")
set(fixture_source "${fixture_root}/source")
set(fixture_build "${fixture_root}/build")
set(fixture_cache "${fixture_root}/cache")
set(fixture_config "${fixture_root}/ccache.conf")
file(REMOVE_RECURSE "${fixture_root}")
file(MAKE_DIRECTORY "${fixture_source}" "${fixture_cache}")
file(WRITE "${fixture_config}" "max_size = 32M\ndirect_mode = true\n")

set(cache_module "${SOURCE_DIR}/cmake/Cache.cmake")
string(CONFIGURE [=[
cmake_minimum_required(VERSION 4.0)
project(MduXCompilerCacheIntegrity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(ENABLE_CACHE ON CACHE BOOL "" FORCE)
set(CACHE_OPTION "ccache" CACHE STRING "" FORCE)
set(CACHE_BINARY "@CACHE_BINARY@" CACHE FILEPATH "" FORCE)
include("@cache_module@")

add_library(fixture)
target_sources(fixture
    PUBLIC FILE_SET CXX_MODULES FILES Api.cppm
    PRIVATE Consumer.cpp)
add_executable(probe Main.cpp)
target_link_libraries(probe PRIVATE fixture)
]=] fixture_cmakelists @ONLY)
file(WRITE "${fixture_source}/CMakeLists.txt" "${fixture_cmakelists}")
file(WRITE "${fixture_source}/Api.cppm" [=[
export module fixture.api;
export struct Layout { int first; };
]=])
file(WRITE "${fixture_source}/Consumer.cpp" [=[
import fixture.api;
int compiledLayoutSize() { return sizeof(Layout); }
]=])
file(WRITE "${fixture_source}/Main.cpp" [=[
#include <cstdio>
int compiledLayoutSize();
int main() { std::printf("%d\n", compiledLayoutSize()); }
]=])

set(cache_environment
    "${CMAKE_COMMAND}" -E env
    --unset=CCACHE_DISABLE
    --unset=CCACHE_RECACHE
    "CCACHE_DIR=${fixture_cache}"
    "CCACHE_CONFIGPATH=${fixture_config}")

run_checked("Fixture configure"
    ${cache_environment} "${CMAKE_COMMAND}"
    -S "${fixture_source}" -B "${fixture_build}" -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}")
file(READ "${fixture_build}/CMakeCache.txt" fixture_cache_contents)
foreach(expected_cache_entry
        "CACHE_BINARY:FILEPATH=${CACHE_BINARY}"
        "MDUX_CACHE_BINARY:INTERNAL=${CACHE_BINARY}"
        "MDUX_CACHE_KIND:INTERNAL=ccache"
        "MDUX_CACHE_CAN_HASH_BMIS:INTERNAL=ON")
    string(FIND "${fixture_cache_contents}" "${expected_cache_entry}" cache_entry_index)
    if(cache_entry_index EQUAL -1)
        message(FATAL_ERROR
            "Fixture cache does not contain the expected entry: ${expected_cache_entry}")
    endif()
endforeach()
run_checked("Ccache statistics reset"
    ${cache_environment} "${CACHE_BINARY}" --zero-stats)
run_checked("Baseline module build"
    ${cache_environment} "${CMAKE_COMMAND}" --build "${fixture_build}")
assert_probe("4" "${fixture_build}")

# This is the negative case from #280: only the provider changes. A cache key that omits the BMI
# returns the old Consumer.cpp object and the probe still prints 4.
file(WRITE "${fixture_source}/Api.cppm" [=[
export module fixture.api;
export struct Layout { int first; double second; };
]=])
run_checked("Build after exported layout change"
    ${cache_environment} "${CMAKE_COMMAND}" --build "${fixture_build}")
assert_probe("16" "${fixture_build}")

# A content-identical rebuild must still hit the cache; correctness was not bought by disabling it
# for consumers. Sleeping makes the timestamp change observable on filesystems with 1 s granularity.
run_checked("Timestamp separation" "${CMAKE_COMMAND}" -E sleep 1)
file(TOUCH "${fixture_source}/Consumer.cpp")
run_checked("Content-identical consumer rebuild"
    ${cache_environment} "${CMAKE_COMMAND}" --build "${fixture_build}")
assert_probe("16" "${fixture_build}")

execute_process(
    COMMAND ${cache_environment} "${CACHE_BINARY}" --print-stats
    RESULT_VARIABLE stats_result
    OUTPUT_VARIABLE stats_output
    ERROR_VARIABLE stats_error
)
if(NOT stats_result EQUAL 0)
    message(FATAL_ERROR "Reading ccache statistics failed: ${stats_error}")
endif()
string(REGEX MATCH "direct_cache_hit[	 ]+([0-9]+)" unused "${stats_output}")
set(direct_hits "${CMAKE_MATCH_1}")
string(REGEX MATCH "preprocessed_cache_hit[	 ]+([0-9]+)" unused "${stats_output}")
set(preprocessed_hits "${CMAKE_MATCH_1}")
if(direct_hits STREQUAL "")
    set(direct_hits 0)
endif()
if(preprocessed_hits STREQUAL "")
    set(preprocessed_hits 0)
endif()
math(EXPR cache_hits "${direct_hits} + ${preprocessed_hits}")
if(cache_hits LESS 1)
    message(FATAL_ERROR
        "The unchanged module consumer did not produce a ccache hit:\n${stats_output}")
endif()

message(STATUS
    "Compiler cache integrity: layout change invalidated the consumer and an unchanged rebuild hit ccache")
