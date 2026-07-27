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
        # Deliberately the old-style positional add_test(<name> <command> [args...])
        # rather than add_test(NAME ... COMMAND ...). Verified empirically (CMake/
        # CTest 4.2.3 on Ubuntu 26.04): the keyword form silently mis-parses in a
        # generated CTestTestfile.cmake - "NAME" itself becomes the displayed test
        # name and the actual name is treated as the executable to run ("Could not
        # find executable <name>"). The positional form works correctly, including
        # with spaces/colons in the name, confirmed by a minimal isolated repro
        # before changing this. Do not "modernize" this back without re-verifying
        # against the CTest version(s) actually in use.
        string(APPEND content
            "add_test(\"${TEST_TARGET}::${escaped_name}\" \"${TEST_EXECUTABLE}\" \"--run=${escaped_name}\")\n")
    endif()
endforeach()

file(WRITE "${TEST_OUTPUT_FILE}" "${content}")
