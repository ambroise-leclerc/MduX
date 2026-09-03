option(ENABLE_CACHE "Enable a compiler cache if available" ON)
set(CACHE_OPTION
    "ccache"
    CACHE STRING "Compiler cache to be used")
set(CACHE_BINARY "" CACHE FILEPATH
    "Compiler cache executable override; empty searches for CACHE_OPTION")
set(CACHE_OPTION_VALUES "ccache" "sccache")
set_property(CACHE CACHE_OPTION PROPERTY STRINGS ${CACHE_OPTION_VALUES})
list(FIND CACHE_OPTION_VALUES "${CACHE_OPTION}" CACHE_OPTION_INDEX)

# These cache entries are the explicit contract consumed by tests/CMakeLists.txt. Reset them on
# every configure so normal early-return paths cannot leave stale capability state behind.
set(MDUX_CACHE_BINARY "" CACHE INTERNAL "Resolved compiler cache executable" FORCE)
set(MDUX_CACHE_KIND "disabled" CACHE INTERNAL "Resolved compiler cache kind" FORCE)
set(MDUX_CACHE_CAN_HASH_BMIS OFF CACHE INTERNAL
    "Whether the compiler cache accepts per-compilation BMI inputs" FORCE)

if(NOT ENABLE_CACHE)
    return()
endif()

if(CACHE_OPTION_INDEX EQUAL -1)
    message(STATUS
        "Using custom compiler cache system: '${CACHE_OPTION}', explicitly supported entries are ${CACHE_OPTION_VALUES}")
endif()

if(CACHE_BINARY)
    set(mdux_resolved_cache_binary "${CACHE_BINARY}")
else()
    find_program(mdux_resolved_cache_binary NAMES "${CACHE_OPTION}" NO_CACHE)
endif()
if(NOT mdux_resolved_cache_binary)
    message(WARNING "${CACHE_OPTION} is enabled but was not found. Not using it")
    return()
endif()
set(MDUX_CACHE_BINARY "${mdux_resolved_cache_binary}"
    CACHE INTERNAL "Resolved compiler cache executable" FORCE)

# Ccache still does not understand C++20 named-module dependency state. In particular, neither
# depend_mode nor sloppiness=modules adds imported BMIs to an object's key. The launcher below
# therefore compiles module providers directly and, on Clang, hashes every imported BMI before
# caching a consumer. Other named-module command formats fail closed to a direct compilation;
# ordinary translation units remain eligible for the selected cache.
set(MDUX_CACHE_KIND "other" CACHE INTERNAL "Resolved compiler cache kind" FORCE)
execute_process(
    COMMAND "${MDUX_CACHE_BINARY}" --version
    RESULT_VARIABLE mdux_cache_version_result
    OUTPUT_VARIABLE mdux_cache_version_stdout
    ERROR_VARIABLE mdux_cache_version_stderr
)
string(CONCAT mdux_cache_version_banner
    "${mdux_cache_version_stdout}" "\n" "${mdux_cache_version_stderr}")
if(mdux_cache_version_result EQUAL 0 AND
   mdux_cache_version_banner MATCHES "(^|\n)ccache version ([0-9]+(\\.[0-9]+)+)")
    set(MDUX_CACHE_KIND "ccache" CACHE INTERNAL "Resolved compiler cache kind" FORCE)
    set(mdux_cache_version "${CMAKE_MATCH_2}")
    # Per-compilation configuration, used for extra_files_to_hash, arrived in ccache 4.8.
    if(mdux_cache_version VERSION_GREATER_EQUAL "4.8")
        set(MDUX_CACHE_CAN_HASH_BMIS ON CACHE INTERNAL
            "Whether the compiler cache accepts per-compilation BMI inputs" FORCE)
    endif()
endif()

set(CMAKE_CXX_COMPILER_LAUNCHER
    "${CMAKE_COMMAND}"
    "-DMDUX_CACHE_BINARY=${MDUX_CACHE_BINARY}"
    "-DMDUX_CACHE_KIND=${MDUX_CACHE_KIND}"
    "-DMDUX_CACHE_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
    "-DMDUX_CACHE_CAN_HASH_BMIS=${MDUX_CACHE_CAN_HASH_BMIS}"
    -P "${CMAKE_CURRENT_LIST_DIR}/MduXCompilerCacheLauncher.cmake"
    --
)

if(MDUX_CACHE_KIND STREQUAL "ccache" AND
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   MDUX_CACHE_CAN_HASH_BMIS)
    message(STATUS
        "ccache ${mdux_cache_version} found: module providers compile directly; Clang module consumers hash imported BMIs; other compilations remain cache-eligible")
else()
    message(STATUS
        "${CACHE_OPTION} found: named-module compilations bypass the cache; other compilations remain cache-eligible")
endif()
