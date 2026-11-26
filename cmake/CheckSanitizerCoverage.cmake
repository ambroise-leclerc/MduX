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
# Every compile command must carry every flag in SANITIZER_FLAGS - including
# -fno-sanitize-recover=all, without which a UBSan finding prints and the test passes. Each target
# in REQUIRED_TARGETS must also have at least one compile command, so a renamed or deleted target,
# or an empty database, fails here instead of passing vacuously.

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
separate_arguments(expectedFlags UNIX_COMMAND "${SANITIZER_FLAGS}")

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
    foreach(flag IN LISTS expectedFlags)
        if(NOT flag IN_LIST arguments)
            list(APPEND uninstrumented "${target}: ${file} (missing ${flag})")
            break()
        endif()
    endforeach()
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
    message(SEND_ERROR "${badCount} translation unit(s) compiled without the sanitizer flags:")
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
