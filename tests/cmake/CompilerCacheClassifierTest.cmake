foreach(required_variable SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR
            "CompilerCacheClassifierTest.cmake: ${required_variable} must be set")
    endif()
endforeach()

include("${SOURCE_DIR}/cmake/MduXCompilerCache.cmake")

set(fixture_directory "${BINARY_DIR}/compiler-cache-classifier")
file(REMOVE_RECURSE "${fixture_directory}")
file(MAKE_DIRECTORY "${fixture_directory}/bmis")
file(WRITE "${fixture_directory}/bmis/first.pcm" "first-bmi")
file(WRITE "${fixture_directory}/bmis/second.pcm" "second-bmi")

mdux_analyze_clang_module_map(
    "" "${fixture_directory}" empty_cacheable empty_bmis)
if(NOT empty_cacheable OR empty_bmis)
    message(FATAL_ERROR "An empty module map must remain cacheable without extra files")
endif()

mdux_analyze_clang_module_map(
    "-fmodule-file=first=bmis/first.pcm\n-fmodule-file=bmis/second.pcm"
    "${fixture_directory}" imports_cacheable imported_bmis)
if(NOT imports_cacheable)
    message(FATAL_ERROR "A complete Clang import map must be cacheable")
endif()
list(LENGTH imported_bmis imported_bmi_count)
if(NOT imported_bmi_count EQUAL 2)
    message(FATAL_ERROR
        "Expected two imported BMIs, found ${imported_bmi_count}: ${imported_bmis}")
endif()
foreach(imported_bmi IN LISTS imported_bmis)
    if(NOT IS_ABSOLUTE "${imported_bmi}" OR NOT EXISTS "${imported_bmi}")
        message(FATAL_ERROR "Imported BMI was not resolved to an existing absolute path: ${imported_bmi}")
    endif()
endforeach()

foreach(unsafe_map
        "-x c++-module\n-fmodule-output=bmis/provider.pcm"
        "-fmodule-file=missing=bmis/missing.pcm"
        "-fmodule-file=first=bmis/first.pcm\n-unknown-module-option")
    mdux_analyze_clang_module_map(
        "${unsafe_map}" "${fixture_directory}" unsafe_cacheable unsafe_bmis)
    if(unsafe_cacheable OR unsafe_bmis)
        message(FATAL_ERROR
            "An incomplete, provider, or unknown module map must bypass the cache: ${unsafe_map}")
    endif()
endforeach()

set(extra_file_paths "C:/first.pcm" "D:/second.pcm")
mdux_join_ccache_extra_files("${extra_file_paths}" FALSE unix_extra_files)
if(NOT unix_extra_files STREQUAL "C:/first.pcm:D:/second.pcm")
    message(FATAL_ERROR "Unexpected Unix ccache path list: ${unix_extra_files}")
endif()

mdux_join_ccache_extra_files("${extra_file_paths}" TRUE windows_extra_files)
if(NOT windows_extra_files STREQUAL "C:/first.pcm;D:/second.pcm")
    message(FATAL_ERROR "Unexpected Windows ccache path list: ${windows_extra_files}")
endif()

foreach(unsafe_argument
        "Api.cppm"
        "-fprebuilt-module-path=bmis"
        "-fmodules-cache-path=cache"
        "-fmodule-map-file=modules.map"
        "-fno-module-lazy"
        "/experimental:module"
        "-working-directory=elsewhere"
        "-working-directory"
        "@compile.rsp"
        "/reference:Api=Api.ifc")
    mdux_argument_requires_cache_bypass("${unsafe_argument}" requires_bypass)
    if(NOT requires_bypass)
        message(FATAL_ERROR "Module-affecting argument did not require bypass: ${unsafe_argument}")
    endif()
endforeach()
foreach(safe_argument "Consumer.cpp" "-Wall" "@Consumer.cpp.o.modmap")
    mdux_argument_requires_cache_bypass("${safe_argument}" requires_bypass)
    if(requires_bypass)
        message(FATAL_ERROR "Cache-neutral argument unexpectedly required bypass: ${safe_argument}")
    endif()
endforeach()

set(argument_probe "${fixture_directory}/ArgumentProbe.cmake")
file(WRITE "${argument_probe}" [=[
if(NOT CMAKE_ARGC EQUAL 7 OR
   NOT CMAKE_ARGV3 STREQUAL "a;b" OR
   NOT CMAKE_ARGV4 STREQUAL "" OR
   NOT CMAKE_ARGV5 STREQUAL "extra_files_to_hash=C:/first.pcm;D:/second.pcm" OR
   NOT CMAKE_ARGV6 STREQUAL "x]]y")
    message(FATAL_ERROR
        "Argument round-trip failed: argc=${CMAKE_ARGC}, '${CMAKE_ARGV3}', '${CMAKE_ARGV4}', "
        "'${CMAKE_ARGV5}', '${CMAKE_ARGV6}'")
endif()
]=])
set(argument_probe_code "execute_process(COMMAND")
mdux_append_bracket_argument("${argument_probe_code}" "${CMAKE_COMMAND}" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}"
    "-DMDUX_CACHE_BINARY=${CMAKE_COMMAND}" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-DMDUX_CACHE_KIND=other"
    argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-DMDUX_CACHE_COMPILER_ID=Clang"
    argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-DMDUX_CACHE_CAN_HASH_BMIS=OFF"
    argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-P" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}"
    "${SOURCE_DIR}/cmake/MduXCompilerCacheLauncher.cmake" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "--" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-E" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "env" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "${CMAKE_COMMAND}" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "-P" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "${argument_probe}" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "a;b" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}"
    "extra_files_to_hash=${windows_extra_files}" argument_probe_code)
mdux_append_bracket_argument("${argument_probe_code}" "x]]y" argument_probe_code)
string(APPEND argument_probe_code " RESULT_VARIABLE argument_probe_result)")
cmake_language(EVAL CODE "${argument_probe_code}")
if(NOT argument_probe_result EQUAL 0)
    message(FATAL_ERROR "Exact launcher argument round-trip failed: ${argument_probe_result}")
endif()

set(exit_probe "${fixture_directory}/Exit130.cmake")
file(WRITE "${exit_probe}" "cmake_language(EXIT 130)\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DMDUX_CACHE_BINARY=${fixture_directory}/must-not-run"
        -DMDUX_CACHE_KIND=ccache
        -DMDUX_CACHE_COMPILER_ID=Clang
        -DMDUX_CACHE_CAN_HASH_BMIS=ON
        -P "${SOURCE_DIR}/cmake/MduXCompilerCacheLauncher.cmake"
        -- "${CMAKE_COMMAND}" -P "${exit_probe}" Api.cppm
    RESULT_VARIABLE exit_probe_result)
if(NOT exit_probe_result EQUAL 130)
    message(FATAL_ERROR "Launcher changed child exit code 130 to ${exit_probe_result}")
endif()

message(STATUS "Compiler cache classifier: OK")
