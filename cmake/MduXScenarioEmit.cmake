# Generated C++ from a committed scenario artifact (#319, ADR-020).
#
# `mdux_emit_scenario_package()` runs mdux-scenarioemit over a committed `scenario.json` and
# produces two files in the *build* tree:
#
#   <binary>/mdux_generated/scenario/<identifier>.cppm   a module interface
#   <binary>/mdux_generated/scenario/<identifier>.hpp    the same scenario, for a translation unit
#                                                        that cannot import a named module
#
# Neither is committed. The reviewed artifact is the JSON and the digests beside it; the C++ is a
# mechanical rendering of exactly those bytes. This mirrors `cmake/MduXScreenEmit.cmake` deliberately
# (ADR-012 decision 3), and like it this is not a bake: an emission produces a build artifact and
# gets no byte-comparison test, because the bytes it renders are already byte-compared as
# `scenario.json`.

include_guard(GLOBAL)

# mdux_scenario_identifier(<out_var> <scenario_id>)
#
# Must agree with identifierForScenario() in tools/scenario/ScenarioEmit.cpp exactly; a
# disagreement surfaces as a build failure on a file nobody wrote. A named function so a parity
# test can call it (`scenario-identifier-parity`).
function(mdux_scenario_identifier out_var scenario_id)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" identifier "${scenario_id}")
    set(${out_var} "scenario_${identifier}" PARENT_SCOPE)
endfunction()

# mdux_emit_scenario_package(ID <id> [PACKAGE <path>] [OUT_MODULE <var>] [OUT_HEADER <var>]
#                            [OUT_DIR <var>])
function(mdux_emit_scenario_package)
    set(options "")
    set(single ID PACKAGE OUT_MODULE OUT_HEADER OUT_DIR)
    set(multi "")
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_emit_scenario_package: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_ID)
        message(FATAL_ERROR "mdux_emit_scenario_package: ID is required")
    endif()
    if(NOT TARGET mdux-scenarioemit)
        message(FATAL_ERROR "mdux_emit_scenario_package: target mdux-scenarioemit does not exist")
    endif()

    if(ARG_PACKAGE)
        set(package_relative "${ARG_PACKAGE}")
    else()
        set(package_relative "generated/scenario/${ARG_ID}/scenario.json")
    endif()
    set(package_path "${CMAKE_SOURCE_DIR}/${package_relative}")

    if(NOT EXISTS "${package_path}")
        message(FATAL_ERROR
            "mdux_emit_scenario_package: ${package_path} does not exist. Compile the scenario first.")
    endif()

    file(READ "${package_path}" package_text)
    string(JSON package_id GET "${package_text}" id)
    if(NOT package_id STREQUAL ARG_ID)
        message(FATAL_ERROR
            "mdux_emit_scenario_package: ${package_relative} declares id '${package_id}', but was "
            "registered as '${ARG_ID}'.")
    endif()

    mdux_scenario_identifier(identifier "${ARG_ID}")
    if(identifier MATCHES "__")
        message(FATAL_ERROR
            "mdux_emit_scenario_package: id '${ARG_ID}' maps to '${identifier}', which is a reserved "
            "identifier. Avoid two adjacent separators in a scenario id.")
    endif()

    set(output_dir "${CMAKE_BINARY_DIR}/mdux_generated/scenario")
    set(module_file "${output_dir}/${identifier}.cppm")
    set(header_file "${output_dir}/${identifier}.hpp")

    add_custom_command(
        OUTPUT "${module_file}" "${header_file}"
        COMMAND mdux-scenarioemit "${package_relative}" "${output_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS
            mdux-scenarioemit
            "${package_path}"
        COMMENT "Emitting C++ for scenario package ${ARG_ID}"
        VERBATIM
    )

    add_custom_target(emit-scenario-${ARG_ID} DEPENDS "${module_file}" "${header_file}")

    if(ARG_OUT_MODULE)
        set(${ARG_OUT_MODULE} "${module_file}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_HEADER)
        set(${ARG_OUT_HEADER} "${header_file}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_DIR)
        set(${ARG_OUT_DIR} "${output_dir}" PARENT_SCOPE)
    endif()
endfunction()
