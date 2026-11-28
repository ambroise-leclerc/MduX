foreach(required_variable
        SOURCE_DIR BINARY_DIR CXX_COMPILER GENERATOR CACHE_BINARY CACHE_KIND CXX_FLAGS
        EXE_LINKER_FLAGS)
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

if(NOT CACHE_KIND MATCHES "^(ccache|cppmcache)$")
    message(FATAL_ERROR
        "CompilerCacheIntegrityTest.cmake: CACHE_KIND must be ccache or cppmcache, not '${CACHE_KIND}'")
endif()

function(assert_probe expected fixture_build)
    # An optional third argument names another executable of the fixture.
    set(probe_name probe)
    if(ARGC GREATER 2)
        set(probe_name "${ARGV2}")
    endif()
    # CMAKE_EXECUTABLE_SUFFIX is not set in script mode, hence the lookup.
    set(probe "${fixture_build}/${probe_name}")
    if(EXISTS "${probe}.exe")
        set(probe "${probe}.exe")
    endif()
    execute_process(
        COMMAND "${probe}"
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_output
        ERROR_VARIABLE probe_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT probe_result EQUAL 0 OR NOT probe_output STREQUAL "${expected}")
        # A wrong value usually means a cached object was served where it should not have been;
        # cppmcache's log says, per compilation, which entry it used and why.
        set(cache_log "")
        if(EXISTS "${fixture_root}/cppmcache.log")
            file(READ "${fixture_root}/cppmcache.log" cache_log)
            set(cache_log "\nCache log:\n${cache_log}")
        endif()
        # And what the build thinks of its outputs: whether it still considers anything out of date,
        # and the modification times it compared. A wrong value with every compilation a miss
        # means the link, not the cache, used old inputs.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${fixture_build}" -- -n -d explain
            OUTPUT_VARIABLE explain_output
            ERROR_VARIABLE explain_output)
        file(GLOB_RECURSE fixture_outputs LIST_DIRECTORIES false
            "${fixture_build}/*.obj" "${fixture_build}/*.o" "${fixture_build}/*.lib" "${fixture_build}/*.a"
            "${fixture_build}/*.exe" "${fixture_build}/*.ifc" "${fixture_build}/*.pcm" "${fixture_build}/*.gcm"
            "${fixture_build}/probe" "${fixture_build}/shadowprobe")
        set(output_times "")
        foreach(output IN LISTS fixture_outputs)
            file(TIMESTAMP "${output}" stamp "%Y-%m-%dT%H:%M:%S.%f" UTC)
            file(RELATIVE_PATH relative "${fixture_build}" "${output}")
            string(APPEND output_times "  ${stamp}  ${relative}\n")
        endforeach()
        message(FATAL_ERROR
            "Probe reported '${probe_output}' with status ${probe_result}; expected '${expected}'.\n"
            "${probe_error}${cache_log}\nOutputs:\n${output_times}\nninja -n -d explain:\n${explain_output}")
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
set(CACHE_OPTION "@CACHE_KIND@" CACHE STRING "" FORCE)
set(CACHE_BINARY "@CACHE_BINARY@" CACHE FILEPATH "" FORCE)
include("@cache_module@")

# MSVC links Debug executables incrementally, and the incremental linker decides whether a library
# member changed by a timestamp with one-second resolution. The whole fixture builds in about a
# second, so a recompiled Consumer.cpp could keep its old timestamp inside fixture.lib and the probe
# be relinked with its old code: every compilation correct, the value wrong. A full link removes that
# from what this test can observe.
if(MSVC)
    string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "" CMAKE_EXE_LINKER_FLAGS_DEBUG
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG " /INCREMENTAL:NO")
endif()

add_library(fixture)
target_sources(fixture
    PUBLIC FILE_SET CXX_MODULES FILES Api.cppm
    PRIVATE Consumer.cpp)
add_executable(probe Main.cpp)
target_link_libraries(probe PRIVATE fixture)

# The header case: Shadow.cpp includes <fixture/value.h>, found in `second`, which is searched after
# `first`. A file of that name created in `first` must reach it.
add_executable(shadowprobe Shadow.cpp)
target_include_directories(shadowprobe PRIVATE "${CMAKE_SOURCE_DIR}/first" "${CMAKE_SOURCE_DIR}/second")
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
file(MAKE_DIRECTORY "${fixture_source}/first" "${fixture_source}/second/fixture")
file(WRITE "${fixture_source}/second/fixture/value.h" "#define FIXTURE_VALUE 1\n")
file(WRITE "${fixture_source}/Shadow.cpp" [=[
#include <cstdio>
#include <fixture/value.h>
int main() { std::printf("%d\n", FIXTURE_VALUE); }
]=])
file(WRITE "${fixture_source}/Main.cpp" [=[
#include <cstdio>
int compiledLayoutSize();
int main() { std::printf("%d\n", compiledLayoutSize()); }
]=])

# The fixture's cache is its own: nothing of the caller's cache configuration may reach it, and in
# particular not a remote cache or a read-only setting.
if(CACHE_KIND STREQUAL "cppmcache")
    set(cache_environment
        "${CMAKE_COMMAND}" -E env
        --unset=CPPMCACHE_DISABLE
        --unset=CPPMCACHE_RECACHE
        --unset=CPPMCACHE_REMOTE
        --unset=CPPMCACHE_BASEDIR
        "CPPMCACHE_LOG=${fixture_root}/cppmcache.log"
        "CPPMCACHE_DIR=${fixture_cache}")
    set(expected_cache_entries
        "CACHE_BINARY:FILEPATH=${CACHE_BINARY}"
        "MDUX_CACHE_BINARY:INTERNAL=${CACHE_BINARY}"
        "MDUX_CACHE_KIND:INTERNAL=cppmcache")
else()
    set(cache_environment
        "${CMAKE_COMMAND}" -E env
        --unset=CCACHE_DISABLE
        --unset=CCACHE_RECACHE
        "CCACHE_DIR=${fixture_cache}"
        "CCACHE_CONFIGPATH=${fixture_config}")
    set(expected_cache_entries
        "CACHE_BINARY:FILEPATH=${CACHE_BINARY}"
        "MDUX_CACHE_BINARY:INTERNAL=${CACHE_BINARY}"
        "MDUX_CACHE_KIND:INTERNAL=ccache"
        "MDUX_CACHE_CAN_HASH_BMIS:INTERNAL=ON")
endif()

run_checked("Fixture configure"
    ${cache_environment} "${CMAKE_COMMAND}"
    -S "${fixture_source}" -B "${fixture_build}" -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    # With no build type, CMake configures MSVC for Debug, whose default debug format is a PDB
    # shared between compilations (/Zi). No cache can restore that, and cppmcache passes such a
    # command straight to the compiler, which left this check with nothing to observe. Embedded
    # debug information (/Z7) lives in the object. The variable means nothing to other compilers.
    -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}")
file(READ "${fixture_build}/CMakeCache.txt" fixture_cache_contents)
foreach(expected_cache_entry IN LISTS expected_cache_entries)
    string(FIND "${fixture_cache_contents}" "${expected_cache_entry}" cache_entry_index)
    if(cache_entry_index EQUAL -1)
        message(FATAL_ERROR
            "Fixture cache does not contain the expected entry: ${expected_cache_entry}")
    endif()
endforeach()
run_checked("Cache statistics reset"
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

# Fails the test, with cppmcache's per-compilation log, if the cache has recorded fewer than
# `minimum` hits since its statistics were last zeroed. `what` heads the failure message.
function(assert_cache_hits minimum what)
    execute_process(
        COMMAND ${cache_environment} "${CACHE_BINARY}" --print-stats
        RESULT_VARIABLE stats_result
        OUTPUT_VARIABLE stats_output
        ERROR_VARIABLE stats_error
    )
    if(NOT stats_result EQUAL 0)
        message(FATAL_ERROR "Reading ${CACHE_KIND} statistics failed: ${stats_error}")
    endif()
    if(CACHE_KIND STREQUAL "cppmcache")
        # cppmcache prints one "name<TAB>value" line per counter.
        string(REGEX MATCH "(^|\n)hit[	 ]+([0-9]+)" unused "${stats_output}")
        set(cache_hits "${CMAKE_MATCH_2}")
        if(cache_hits STREQUAL "")
            set(cache_hits 0)
        endif()
    else()
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
    endif()
    if(cache_hits LESS minimum)
        # cppmcache records, for each compilation, whether it was a hit, a miss or passed through,
        # and why: without that, a failure here says only that something went wrong.
        set(cache_log "")
        if(EXISTS "${fixture_root}/cppmcache.log")
            file(READ "${fixture_root}/cppmcache.log" cache_log)
            set(cache_log "\nCache log:\n${cache_log}")
        endif()
        message(FATAL_ERROR
            "${what}: expected at least ${minimum} ${CACHE_KIND} hit(s), got ${cache_hits}:\n"
            "${stats_output}${cache_log}")
    endif()
endfunction()

assert_cache_hits(1 "The unchanged module consumer did not hit the cache")

message(STATUS
    "Compiler cache integrity: layout change invalidated the consumer and an unchanged rebuild hit ${CACHE_KIND}")

# The negative case of ambroise-leclerc/cppmcache#24, which is how CI uses a restored cache: a fresh
# build directory, a populated cache, and a pull request that adds a header earlier in the search
# order than one the cached objects were compiled with. No file those compilations read has changed,
# so a cache that checks only the files it recorded serves the object built with the old header.
# Only cppmcache is held to it: ccache's direct mode documents the same limitation.
if(CACHE_KIND STREQUAL "cppmcache")
    assert_probe("1" "${fixture_build}" shadowprobe)
    file(MAKE_DIRECTORY "${fixture_source}/first/fixture")
    file(WRITE "${fixture_source}/first/fixture/value.h" "#define FIXTURE_VALUE 2\n")
    file(REMOVE_RECURSE "${fixture_build}")
    run_checked("Fixture configure in a fresh build directory"
        ${cache_environment} "${CMAKE_COMMAND}"
        -S "${fixture_source}" -B "${fixture_build}" -G "${GENERATOR}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
        -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
        "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}")
    # The log is started afresh so that what it holds below is this build's alone.
    file(REMOVE "${fixture_root}/cppmcache.log")
    run_checked("Build with a shadowing header and a populated cache"
        ${cache_environment} "${CMAKE_COMMAND}" --build "${fixture_build}")
    assert_probe("2" "${fixture_build}" shadowprobe)
    # The positive control. Api.cppm, Consumer.cpp and Main.cpp are unchanged since the cache last
    # stored them, so this build must serve each of their objects from it; without that, a build
    # that missed everything - a key that came to depend on the build directory, say - would print 2
    # as well and the check above would prove nothing about a populated cache. It is checked per
    # object, from cppmcache's log, rather than by counting hits: a count could be made up by other
    # commands the launcher sees, such as module dependency scans, while every object missed.
    set(cache_log "")
    if(EXISTS "${fixture_root}/cppmcache.log")
        file(READ "${fixture_root}/cppmcache.log" cache_log)
    endif()
    foreach(unchanged_object Api.cppm Consumer.cpp Main.cpp)
        string(REPLACE "." "\\." object_pattern "${unchanged_object}")
        # One "<outcome>\t<object>\t<detail>" line per compilation; the object path uses the
        # platform's separator and suffix.
        if(NOT cache_log MATCHES "(^|\n)hit\t[^\t\n]*[/\\\\]${object_pattern}\\.(o|obj)\t")
            message(FATAL_ERROR
                "The fresh build directory was not served from the populated cache: no hit for "
                "${unchanged_object}'s object.\nCache log:\n${cache_log}")
        endif()
    endforeach()
    message(STATUS "Compiler cache integrity: a header added earlier in the search order reached its includer")
endif()
