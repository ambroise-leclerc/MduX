include_guard(GLOBAL)

# Classify a Clang CMake module-map response file. Only a list composed entirely of imported BMI
# arguments is cacheable. Provider output or any syntax this project does not understand fails
# closed, because a missed cache opportunity is preferable to an object built against a stale BMI.
function(mdux_analyze_clang_module_map content base_directory out_cacheable out_bmis)
    string(STRIP "${content}" stripped_content)
    if(stripped_content STREQUAL "")
        set("${out_cacheable}" TRUE PARENT_SCOPE)
        set("${out_bmis}" "" PARENT_SCOPE)
        return()
    endif()

    separate_arguments(module_arguments NATIVE_COMMAND "${stripped_content}")
    set(imported_bmis)
    foreach(argument IN LISTS module_arguments)
        if(NOT argument MATCHES "^-fmodule-file=(.+)$")
            set("${out_cacheable}" FALSE PARENT_SCOPE)
            set("${out_bmis}" "" PARENT_SCOPE)
            return()
        endif()

        set(module_file "${CMAKE_MATCH_1}")
        if(module_file MATCHES "^[^=]+=(.+)$")
            set(module_file "${CMAKE_MATCH_1}")
        endif()
        if(NOT IS_ABSOLUTE "${module_file}")
            get_filename_component(module_file
                "${module_file}" ABSOLUTE BASE_DIR "${base_directory}")
        endif()
        cmake_path(NORMAL_PATH module_file)

        if(NOT EXISTS "${module_file}" OR
           (NOT WIN32 AND module_file MATCHES ":"))
            set("${out_cacheable}" FALSE PARENT_SCOPE)
            set("${out_bmis}" "" PARENT_SCOPE)
            return()
        endif()
        list(APPEND imported_bmis "${module_file}")
    endforeach()

    if(NOT imported_bmis)
        set("${out_cacheable}" FALSE PARENT_SCOPE)
        set("${out_bmis}" "" PARENT_SCOPE)
        return()
    endif()

    list(REMOVE_DUPLICATES imported_bmis)
    set("${out_cacheable}" TRUE PARENT_SCOPE)
    set("${out_bmis}" "${imported_bmis}" PARENT_SCOPE)
endfunction()

# Encode ccache's platform-specific path list. The launcher bracket-quotes the completed setting,
# so a Windows semicolon remains inside one process argument rather than becoming a CMake list.
function(mdux_join_ccache_extra_files paths use_windows_separator out_argument)
    if(use_windows_separator)
        set(path_separator ";")
    else()
        set(path_separator ":")
    endif()
    list(JOIN paths "${path_separator}" extra_files)
    set("${out_argument}" "${extra_files}" PARENT_SCOPE)
endfunction()

# Append one exact process argument as a CMake bracket argument. The delimiter grows until its
# closing token is absent from the value, preserving semicolons, quotes, newlines, and empty argv
# entries when the resulting execute_process call is evaluated.
function(mdux_append_bracket_argument code argument out_code)
    set(equals)
    while(TRUE)
        set(terminator "]${equals}]")
        string(FIND "${argument}" "${terminator}" terminator_index)
        if(terminator_index EQUAL -1)
            break()
        endif()
        string(APPEND equals "=")
    endwhile()
    string(APPEND code " [${equals}[${argument}]${equals}]")
    set("${out_code}" "${code}" PARENT_SCOPE)
endfunction()

# Identify arguments that either produce module state or can hide/use module state the launcher
# cannot safely add to a cache key. A recognized CMake .modmap is parsed separately by the caller.
function(mdux_argument_requires_cache_bypass argument out_bypass)
    set(requires_bypass OFF)
    if(argument MATCHES "\\.(cppm|ixx|mpp)$" OR
       argument MATCHES "^[-/].*[Mm][Oo][Dd][Uu][Ll][Ee]" OR
       argument MATCHES "^-working-directory($|=)" OR
       argument STREQUAL "--precompile" OR
       argument MATCHES "^[-/]ifc(Output|Only)($|:)" OR
       argument MATCHES "^[-/]interface$" OR
       argument MATCHES "^[-/](reference|headerUnit)(:|$)")
        set(requires_bypass ON)
    elseif(argument MATCHES "^@" AND NOT argument MATCHES "^@(.+\\.modmap)$")
        set(requires_bypass ON)
    endif()
    set("${out_bypass}" "${requires_bypass}" PARENT_SCOPE)
endfunction()
