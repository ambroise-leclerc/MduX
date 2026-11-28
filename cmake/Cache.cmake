option(ENABLE_CACHE "Enable a compiler cache if available" ON)
set(CACHE_OPTION
    "ccache"
    CACHE STRING "Compiler cache to be used")
set(CACHE_BINARY "" CACHE FILEPATH
    "Compiler cache executable override; empty searches for CACHE_OPTION")
set(CACHE_OPTION_VALUES "ccache" "sccache" "cppmcache")
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
# cppmcache is never the default, so selecting it is a request rather than a preference: a build
# that asked for it and silently fell back would cache differently from what it claims, and CI
# would lose the cache #392 exists for without a red run.
if(NOT mdux_resolved_cache_binary)
    if(CACHE_OPTION STREQUAL "cppmcache")
        message(FATAL_ERROR
            "CACHE_OPTION=cppmcache, but cppmcache was not found. Put it on PATH, set CACHE_BINARY, "
            "or choose another CACHE_OPTION (-DENABLE_CACHE=OFF disables the compiler cache).")
    endif()
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
elseif(mdux_cache_version_result EQUAL 0 AND
       mdux_cache_version_banner MATCHES "(^|\n)cppmcache version ([0-9]+(\\.[0-9]+)+)")
    set(MDUX_CACHE_KIND "cppmcache" CACHE INTERNAL "Resolved compiler cache kind" FORCE)
    set(mdux_cache_version "${CMAKE_MATCH_2}")
endif()
if(CACHE_OPTION STREQUAL "cppmcache" AND NOT MDUX_CACHE_KIND STREQUAL "cppmcache")
    message(FATAL_ERROR
        "CACHE_OPTION=cppmcache, but '${MDUX_CACHE_BINARY} --version' did not report a cppmcache "
        "version:\n${mdux_cache_version_banner}")
endif()
# The other direction. The kind decides the launcher and which tests are registered, so a
# CACHE_BINARY that names a different cache from CACHE_OPTION would leave the configuration saying
# one thing while the build does another. Each of the two caches recognised here must therefore be
# the one CACHE_OPTION asked for.
if(MDUX_CACHE_KIND MATCHES "^(ccache|cppmcache)$" AND NOT CACHE_OPTION STREQUAL MDUX_CACHE_KIND)
    message(FATAL_ERROR
        "CACHE_OPTION=${CACHE_OPTION}, but '${MDUX_CACHE_BINARY} --version' reports ${MDUX_CACHE_KIND} "
        "${mdux_cache_version}. Set CACHE_OPTION=${MDUX_CACHE_KIND}, or point CACHE_BINARY at a "
        "${CACHE_OPTION} executable.")
endif()
# cppmcache 0.1.0 can serve an object compiled against a header that one created earlier in the
# include search order should have replaced (ambroise-leclerc/cppmcache#24). ccache below 4.8 still
# has a safe degraded mode, module consumers compiled directly; cppmcache is the launcher for every
# compilation and has none, so an older one is refused rather than used.
if(MDUX_CACHE_KIND STREQUAL "cppmcache" AND mdux_cache_version VERSION_LESS "0.1.1")
    message(FATAL_ERROR
        "cppmcache ${mdux_cache_version} at '${MDUX_CACHE_BINARY}' is too old: 0.1.1 or later is "
        "required, because 0.1.0 can serve an object compiled against the wrong header "
        "(ambroise-leclerc/cppmcache#24).")
endif()

# cppmcache (https://github.com/ambroise-leclerc/cppmcache) is the one cache here that models
# named-module state itself: a consumer's key holds the content of every BMI it imports, under the
# module's name, and a hit on a provider restores its BMI along with its object. It therefore needs
# none of the triage below and is the launcher directly. Anything it does not understand it passes
# to the compiler unchanged, which is the same fail-closed behaviour the triage script gives the
# other caches. build.cache.namedModuleIntegrity holds it to the same negative case as ccache.
if(MDUX_CACHE_KIND STREQUAL "cppmcache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${MDUX_CACHE_BINARY}")
    message(STATUS
        "cppmcache ${mdux_cache_version} found: named-module providers and consumers are cached")
    return()
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
