# Self-test for cmake/CheckSanitizerCoverage.cmake (#360).
#
# Run as a CTest script (`-P`) with -D CHECKER=<path to CheckSanitizerCoverage.cmake> and
# -D WORK_DIR=<scratch directory>. Registered in every build, not only sanitizer builds, because
# what it tests is the checker's reading of a command line, which needs no instrumented compiler.
#
# Each case writes a one-entry compile_commands.json and runs the checker on it. The cases that
# must fail are the point: a checker that accepted `-fno-sanitize=all` after the global flags would
# report a sanitizer job green while the sanitizers were off, which is the failure #360 was about.

foreach(required CHECKER WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "TestCheckSanitizerCoverage: ${required} is not set")
    endif()
endforeach()

set(sanitizerFlags "-fsanitize=address,undefined -fno-sanitize-recover=all")
set(base "-fsanitize=address,undefined -fno-sanitize-recover=all")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(failures "")

function(check_case name expectation command)
    set(database "${WORK_DIR}/${name}.json")
    file(WRITE "${database}" "[{\"directory\": \"/b\", \"command\": \"/usr/bin/g++ ${command} -c /s/a.cpp\", \"file\": \"/s/a.cpp\", \"output\": \"CMakeFiles/MduXCore.dir/a.cpp.o\"}]")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DCOMPILE_COMMANDS=${database}" "-DSANITIZER_FLAGS=${sanitizerFlags}"
                "-DREQUIRED_TARGETS=MduXCore" -P "${CHECKER}"
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if(expectation STREQUAL "pass" AND NOT result EQUAL 0)
        set(failures "${failures};${name}: rejected '${command}', expected it accepted" PARENT_SCOPE)
    elseif(expectation STREQUAL "fail" AND result EQUAL 0)
        set(failures "${failures};${name}: accepted '${command}', expected it rejected" PARENT_SCOPE)
    endif()
endfunction()

# Accepted: the global flags alone, repeated (SpecLab adds its own -fsanitize=), split, reordered
# so the recover flag comes first, or the bare -fno-sanitize-recover form.
check_case(global pass "${base}")
check_case(repeated pass "${base} -fsanitize=address,undefined")
check_case(split pass "-fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all")
check_case(recoverFirst pass "-fno-sanitize-recover=all -fsanitize=address,undefined")
check_case(bareNoRecover pass "-fsanitize=address,undefined -fno-sanitize-recover")
check_case(reenabled pass "${base} -fno-sanitize=all -fsanitize=address,undefined")
check_case(recoverDisabledAgain pass "${base} -fsanitize-recover=all -fno-sanitize-recover=all")

# Rejected: missing flags, then every later option that undoes an earlier one.
check_case(nothing fail "-O2")
check_case(noRecoverFlag fail "-fsanitize=address,undefined")
check_case(oneMissing fail "-fsanitize=address -fno-sanitize-recover=all")
check_case(allDisabled fail "${base} -fno-sanitize=all")
check_case(review fail "${base} -fno-sanitize=all -fsanitize-recover=all")
check_case(oneDisabled fail "${base} -fno-sanitize=undefined")
check_case(subCheckDisabled fail "${base} -fno-sanitize=alignment")
check_case(recoverAll fail "${base} -fsanitize-recover=all")
check_case(recoverOne fail "${base} -fsanitize-recover=undefined")
check_case(recoverBare fail "${base} -fsanitize-recover")
check_case(recoverDefaultLeft fail "-fsanitize=address,undefined -fno-sanitize-recover=address")

if(failures)
    string(REPLACE ";" "\n  " failureText "${failures}")
    message(FATAL_ERROR "CheckSanitizerCoverage misread a command line:${failureText}")
endif()
message(STATUS "CheckSanitizerCoverage self-test: every case read as expected.")
