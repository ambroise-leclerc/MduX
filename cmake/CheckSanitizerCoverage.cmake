# Fails when a sanitizer build compiled any translation unit without its sanitizer flags (#360).
#
# Run as a CTest script (`-P`), registered by tests/CMakeLists.txt only in a sanitizer build:
#
#   cmake -D COMPILE_COMMANDS=<build>/compile_commands.json
#         -D SANITIZER_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
#         -D REQUIRED_TARGETS="MduXCore,MduX,..."
#         -P cmake/CheckSanitizerCoverage.cmake
#
# The ASan+UBSan job once reported green for months while 0 of MduXCore's 46 translation units
# carried -fsanitize, because the flags reached a target only through the Vulkan-carrying
# MduX_options. A green sanitizer job is evidence about the code it instrumented and nothing else,
# so this check makes "everything was instrumented" a test result rather than an assumption.
#
# What is checked is the *effect* of each compile command's options, evaluated in command-line
# order, not the presence of SANITIZER_FLAGS' text. Presence alone would accept a command whose
# global flags are followed by a configuration- or target-specific `-fno-sanitize=all
# -fsanitize-recover=all`, which switches the sanitizers off and recovery back on: the compiler
# honours the last word, so this check must too. For every command:
#
# - every sanitizer SANITIZER_FLAGS enables with -fsanitize= must still be enabled at the end;
# - nothing may disable part of an enabled sanitizer: a -fno-sanitize= naming anything but `all` or
#   a whole group SANITIZER_FLAGS enables (say `alignment`, inside `undefined`) is refused, even
#   when that name was itself enabled earlier, because group/sub-check overlap is not tracked here;
# - no sanitizer may recover at the end. Recovery is the compilers' default for UBSan, so the
#   command must carry -fno-sanitize-recover=all and nothing after it may turn
#   any recovery back on; without that a UBSan finding prints and the test passes.
#
# Each target in REQUIRED_TARGETS must also have at least one compile command, so a renamed or
# deleted target, or an empty database, fails here instead of passing vacuously.
# tests/CMakeLists.txt registers `sanitizers.coverage.selftest`
# (cmake/TestCheckSanitizerCoverage.cmake), which runs this script over hand-written databases,
# contradictory options included.

foreach(required COMPILE_COMMANDS SANITIZER_FLAGS REQUIRED_TARGETS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "CheckSanitizerCoverage: ${required} is not set")
    endif()
endforeach()

if(NOT EXISTS "${COMPILE_COMMANDS}")
    message(FATAL_ERROR "CheckSanitizerCoverage: ${COMPILE_COMMANDS} does not exist")
endif()

file(READ "${COMPILE_COMMANDS}" database)
string(JSON entryCount LENGTH "${database}")
if(entryCount EQUAL 0)
    message(FATAL_ERROR "CheckSanitizerCoverage: ${COMPILE_COMMANDS} has no compile commands")
endif()

string(REPLACE "," ";" REQUIRED_TARGETS "${REQUIRED_TARGETS}")

# Evaluates sanitizer options in order, the way GCC and Clang do. Sets, in the caller's scope:
#   <prefix>_ENABLED     sanitizer groups still enabled after the last option
#   <prefix>_RECOVERING  groups that still recover ("default" is the compilers' own UBSan default,
#                        cleared only by -fno-sanitize-recover=all)
#   <prefix>_REFUSED     options this script cannot evaluate precisely and so refuses
#
# CMAKE_MATCH_<n> is compared through "${...}" below on purpose: when an optional group does not
# participate in the match the variable is left undefined, and an unquoted `if(CMAKE_MATCH_2 ...)`
# then compares the literal text "CMAKE_MATCH_2". The self-test caught exactly that, on the bare
# -fno-sanitize-recover form.
function(mdux_evaluate_sanitizer_options prefix)
    set(enabled "")
    set(recovering "default")
    set(refused "")
    foreach(arg IN LISTS ARGN)
        if(arg MATCHES "^-fsanitize=(.+)$")
            string(REPLACE "," ";" groups "${CMAKE_MATCH_1}")
            list(APPEND enabled ${groups})
        elseif(arg MATCHES "^-fno-sanitize=(.+)$")
            string(REPLACE "," ";" groups "${CMAKE_MATCH_1}")
            foreach(group IN LISTS groups)
                if(group STREQUAL "all")
                    set(enabled "")
                elseif(group IN_LIST MDUX_EXPECTED_SANITIZERS)
                    # A whole expected group: removing it is exact, and leaves it reported missing
                    # unless a later -fsanitize= enables it again.
                    list(REMOVE_ITEM enabled "${group}")
                else()
                    # Anything else may be a sub-check of an enabled group - `alignment` inside
                    # `undefined`, `pointer-compare` beside `address` - and that holds even when the
                    # same name was enabled separately first: `-fsanitize=undefined
                    # -fsanitize=alignment -fno-sanitize=alignment` leaves UBSan without its
                    # alignment check. This script keeps no table of which check belongs to which
                    # group, so it refuses the option rather than guess about the overlap.
                    list(APPEND refused "${arg}")
                endif()
            endforeach()
        elseif(arg MATCHES "^-fsanitize-recover(=(.+))?$")
            if("${CMAKE_MATCH_2}" STREQUAL "")
                list(APPEND recovering "default")
            else()
                string(REPLACE "," ";" groups "${CMAKE_MATCH_2}")
                list(APPEND recovering ${groups})
            endif()
        elseif(arg MATCHES "^-fno-sanitize-recover(=(.+))?$")
            if("${CMAKE_MATCH_2}" STREQUAL "all")
                set(recovering "")
            elseif("${CMAKE_MATCH_2}" STREQUAL "")
                # The bare form does not disable recovery for every sanitizer (GCC excludes
                # address). Its exact scope varies by compiler, so do not infer coverage from it.
                list(APPEND refused "${arg}")
            else()
                # Removing one group from `all` or from the compiler default cannot be tracked
                # precisely, so those stay in the list and fail the command - conservatively.
                string(REPLACE "," ";" groups "${CMAKE_MATCH_2}")
                list(REMOVE_ITEM recovering ${groups})
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES enabled)
    list(REMOVE_DUPLICATES recovering)
    set(${prefix}_ENABLED "${enabled}" PARENT_SCOPE)
    set(${prefix}_RECOVERING "${recovering}" PARENT_SCOPE)
    set(${prefix}_REFUSED "${refused}" PARENT_SCOPE)
endfunction()

# Empty while SANITIZER_FLAGS itself is evaluated, so any -fno-sanitize= in it is refused.
set(MDUX_EXPECTED_SANITIZERS "")
separate_arguments(expectedFlags UNIX_COMMAND "${SANITIZER_FLAGS}")
mdux_evaluate_sanitizer_options(expected ${expectedFlags})
set(MDUX_EXPECTED_SANITIZERS "${expected_ENABLED}")
if(expected_ENABLED STREQUAL "" OR NOT expected_RECOVERING STREQUAL "" OR NOT expected_REFUSED STREQUAL "")
    message(FATAL_ERROR
        "CheckSanitizerCoverage: SANITIZER_FLAGS '${SANITIZER_FLAGS}' must enable at least one "
        "sanitizer and disable recovery with -fno-sanitize-recover=all")
endif()

set(uninstrumented "")
set(seenTargets "")
math(EXPR lastIndex "${entryCount} - 1")
foreach(index RANGE 0 ${lastIndex})
    string(JSON command ERROR_VARIABLE commandError GET "${database}" ${index} command)
    if(commandError)
        # The `arguments` form is the other one the format allows; CMake writes `command`, but a
        # database this script cannot read must fail rather than count as instrumented.
        message(FATAL_ERROR "CheckSanitizerCoverage: entry ${index} has no `command` field")
    endif()
    string(JSON file GET "${database}" ${index} file)
    string(JSON output ERROR_VARIABLE outputError GET "${database}" ${index} output)

    # Target name, from CMake's object path: .../CMakeFiles/<target>.dir/<source>.o
    set(target "<unknown>")
    if(NOT outputError AND output MATCHES "CMakeFiles/([^/]+)\\.dir/")
        set(target "${CMAKE_MATCH_1}")
    endif()
    list(APPEND seenTargets "${target}")

    separate_arguments(arguments UNIX_COMMAND "${command}")
    mdux_evaluate_sanitizer_options(actual ${arguments})
    set(reasons "")
    foreach(group IN LISTS expected_ENABLED)
        if(NOT group IN_LIST actual_ENABLED)
            list(APPEND reasons "${group} not enabled")
        endif()
    endforeach()
    if(actual_RECOVERING)
        string(REPLACE ";" "," recoveringText "${actual_RECOVERING}")
        list(APPEND reasons "recovery on for ${recoveringText}")
    endif()
    foreach(option IN LISTS actual_REFUSED)
        list(APPEND reasons "cannot verify option ${option}")
    endforeach()
    if(reasons)
        string(REPLACE ";" ", " reasonText "${reasons}")
        list(APPEND uninstrumented "${target}: ${file} (${reasonText})")
    endif()
endforeach()

list(REMOVE_DUPLICATES seenTargets)
set(missingTargets "")
foreach(target IN LISTS REQUIRED_TARGETS)
    if(NOT target IN_LIST seenTargets)
        list(APPEND missingTargets "${target}")
    endif()
endforeach()

list(LENGTH seenTargets targetCount)
message(STATUS "Checked ${entryCount} compile commands across ${targetCount} targets for: ${SANITIZER_FLAGS}")

set(failed FALSE)
if(uninstrumented)
    set(failed TRUE)
    list(LENGTH uninstrumented badCount)
    message(SEND_ERROR "${badCount} translation unit(s) not compiled with the sanitizers in effect:")
    foreach(line IN LISTS uninstrumented)
        message("  ${line}")
    endforeach()
endif()
if(missingTargets)
    set(failed TRUE)
    message(SEND_ERROR "Expected targets with no compile command in ${COMPILE_COMMANDS}: ${missingTargets}")
endif()
if(failed)
    message(FATAL_ERROR "Sanitizer coverage is incomplete; a green sanitizer run would not cover the code listed above.")
endif()
message(STATUS "Every translation unit is instrumented.")
