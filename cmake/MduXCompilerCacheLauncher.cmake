include("${CMAKE_CURRENT_LIST_DIR}/MduXCompilerCache.cmake")

foreach(required_variable
        MDUX_CACHE_BINARY MDUX_CACHE_KIND MDUX_CACHE_COMPILER_ID MDUX_CACHE_CAN_HASH_BMIS)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR
            "MduXCompilerCacheLauncher.cmake: ${required_variable} must be set")
    endif()
endforeach()

set(separator_index -1)
math(EXPR last_argument_index "${CMAKE_ARGC} - 1")
foreach(index RANGE 0 ${last_argument_index})
    if(CMAKE_ARGV${index} STREQUAL "--")
        set(separator_index ${index})
        break()
    endif()
endforeach()
if(separator_index LESS 0)
    message(FATAL_ERROR
        "MduXCompilerCacheLauncher.cmake: missing -- before the compiler command")
endif()

math(EXPR command_start "${separator_index} + 1")
if(command_start GREATER last_argument_index)
    message(FATAL_ERROR
        "MduXCompilerCacheLauncher.cmake: missing compiler command after --")
endif()

set(compiler_command_code)
set(bypass_cache OFF)
set(imported_bmis)
foreach(index RANGE ${command_start} ${last_argument_index})
    set(argument "${CMAKE_ARGV${index}}")
    mdux_append_bracket_argument("${compiler_command_code}" "${argument}"
        compiler_command_code)

    mdux_argument_requires_cache_bypass("${argument}" argument_requires_bypass)
    if(argument_requires_bypass)
        set(bypass_cache ON)
    endif()

    if(argument MATCHES "^@(.+\\.modmap)$")
        set(module_map_path "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "^\"|\"$" "" module_map_path "${module_map_path}")
        if(NOT IS_ABSOLUTE "${module_map_path}")
            get_filename_component(module_map_path
                "${module_map_path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        endif()

        if(NOT EXISTS "${module_map_path}")
            set(bypass_cache ON)
            continue()
        endif()

        file(READ "${module_map_path}" module_map_content)
        string(STRIP "${module_map_content}" stripped_module_map)
        if(stripped_module_map STREQUAL "")
            continue()
        endif()

        if(MDUX_CACHE_KIND STREQUAL "ccache" AND
           MDUX_CACHE_COMPILER_ID STREQUAL "Clang" AND
           MDUX_CACHE_CAN_HASH_BMIS)
            mdux_analyze_clang_module_map(
                "${module_map_content}" "${CMAKE_CURRENT_BINARY_DIR}"
                module_map_cacheable module_map_bmis)
            if(module_map_cacheable)
                list(APPEND imported_bmis ${module_map_bmis})
            else()
                set(bypass_cache ON)
            endif()
        else()
            set(bypass_cache ON)
        endif()
    endif()
endforeach()

set(command_code "execute_process(COMMAND")
if(bypass_cache)
    string(APPEND command_code "${compiler_command_code}")
else()
    mdux_append_bracket_argument("${command_code}" "${MDUX_CACHE_BINARY}" command_code)
    if(imported_bmis)
        list(REMOVE_DUPLICATES imported_bmis)
        # Ccache follows the host path-list separator. Bracket quoting preserves the Windows
        # semicolon inside a single process argument.
        mdux_join_ccache_extra_files(
            "${imported_bmis}" "${WIN32}" imported_bmi_argument)
        mdux_append_bracket_argument("${command_code}"
            "extra_files_to_hash=${imported_bmi_argument}" command_code)
    endif()
    string(APPEND command_code "${compiler_command_code}")
endif()

string(APPEND command_code " RESULT_VARIABLE command_result)")
cmake_language(EVAL CODE "${command_code}")
if(command_result MATCHES "^[0-9]+$" AND command_result LESS_EQUAL 255)
    cmake_language(EXIT ${command_result})
else()
    message(FATAL_ERROR
        "MduX compiler-cache launcher could not execute the compiler: ${command_result}")
endif()
