# MduXTestDiscovery.cmake
#
# mdux_discover_tests(<target>) registers one CTest entry per TEST_CASE in <target>,
# so `ctest -R <name>` selects an individual case instead of only the whole
# executable. Modeled on the same POST_BUILD-discovery pattern Catch2's and
# GoogleTest's CMake integration modules use: after the test binary links, run it
# once with --list-tests, and generate an include file of add_test() calls from the
# names it prints.
#
# Requires the target to be built on tests/framework/MduXTest.cppm + MduXTest.hpp
# (MDUX_TEST_MAIN provides the --list-tests / --run=<name> support this depends on).

set(_MDUX_TEST_DISCOVERY_IMPL_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/MduXTestDiscoveryImpl.cmake")

function(mdux_discover_tests TARGET)
    set(ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_discovered")
    set(ctest_include_file "${ctest_file_base}_include.cmake")
    set(ctest_tests_file "${ctest_file_base}_tests.cmake")

    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        BYPRODUCTS "${ctest_tests_file}"
        COMMAND "${CMAKE_COMMAND}"
                -D "TEST_TARGET=${TARGET}"
                -D "TEST_EXECUTABLE=$<TARGET_FILE:${TARGET}>"
                -D "TEST_OUTPUT_FILE=${ctest_tests_file}"
                -P "${_MDUX_TEST_DISCOVERY_IMPL_SCRIPT}"
        VERBATIM
    )

    file(WRITE "${ctest_include_file}"
        "if(EXISTS \"${ctest_tests_file}\")\n"
        "  include(\"${ctest_tests_file}\")\n"
        "else()\n"
        "  add_test(${TARGET}_NOT_BUILT ${TARGET}_NOT_BUILT)\n"
        "endif()\n"
    )

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_include_file}")
endfunction()
