# MduXTestDiscoveryImpl.cmake
#
# Run in script mode (-P) by the POST_BUILD command in MduXTestDiscovery.cmake.
# Executes TEST_EXECUTABLE --list-tests, and writes one add_test() per line to
# TEST_OUTPUT_FILE. Each registered test invokes the same executable with
# --run=<name>, so a single build produces one binary and N ctest entries.

execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-tests
    OUTPUT_VARIABLE test_names_raw
    RESULT_VARIABLE list_result
)

if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "mdux_discover_tests: '${TEST_EXECUTABLE} --list-tests' failed with ${list_result}")
endif()

# Split on newlines, drop empty lines from trailing separators.
string(REPLACE "\n" ";" test_names "${test_names_raw}")

set(content "")
foreach(name ${test_names})
    string(STRIP "${name}" name)
    if(NOT name STREQUAL "")
        # Escape characters CTest test names / CMake strings care about.
        string(REPLACE "\\" "\\\\" escaped_name "${name}")
        string(REPLACE "\"" "\\\"" escaped_name "${escaped_name}")
        string(APPEND content
            "add_test(NAME \"${TEST_TARGET}::${escaped_name}\" COMMAND \"${TEST_EXECUTABLE}\" \"--run=${escaped_name}\")\n")
    endif()
endforeach()

file(WRITE "${TEST_OUTPUT_FILE}" "${content}")
