# Generated C++ from a committed model package (issue #153).
#
# The two outputs live in the build tree. `package.json` remains the reviewed, byte-compared
# artifact, and `weights.bin` remains caller-supplied data rather than generated C++.

include_guard(GLOBAL)

function(mdux_model_identifier out_var package_id)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" identifier "${package_id}")
    set(${out_var} "model_${identifier}" PARENT_SCOPE)
endfunction()

# mdux_emit_model_package(ID <id> [OUT_MODULE <var>] [OUT_HEADER <var>] [OUT_DIR <var>])
function(mdux_emit_model_package)
    set(options "")
    set(single ID OUT_MODULE OUT_HEADER OUT_DIR)
    set(multi "")
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_emit_model_package: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_ID)
        message(FATAL_ERROR "mdux_emit_model_package: ID is required")
    endif()
    if(NOT TARGET mdux-mlemit)
        message(FATAL_ERROR "mdux_emit_model_package: target mdux-mlemit does not exist")
    endif()

    set(package_relative "generated/model/${ARG_ID}/package.json")
    set(package_path "${CMAKE_SOURCE_DIR}/${package_relative}")
    if(NOT EXISTS "${package_path}")
        message(FATAL_ERROR
            "mdux_emit_model_package: ${package_path} does not exist. Bake it first with "
            "`cmake --build <dir> --target mdux-bake-update`.")
    endif()

    file(READ "${package_path}" package_text)
    string(JSON package_id GET "${package_text}" id)
    if(NOT package_id STREQUAL ARG_ID)
        message(FATAL_ERROR
            "mdux_emit_model_package: ${package_relative} declares id '${package_id}', but was "
            "registered as '${ARG_ID}'.")
    endif()

    mdux_model_identifier(identifier "${ARG_ID}")
    if(identifier MATCHES "__")
        message(FATAL_ERROR
            "mdux_emit_model_package: id '${ARG_ID}' maps to reserved identifier '${identifier}'.")
    endif()

    set(output_dir "${CMAKE_BINARY_DIR}/mdux_generated/model")
    set(module_file "${output_dir}/${identifier}.cppm")
    set(header_file "${output_dir}/${identifier}.hpp")

    add_custom_command(
        OUTPUT "${module_file}" "${header_file}"
        COMMAND mdux-mlemit "${package_relative}" "${output_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS mdux-mlemit "${package_path}"
        COMMENT "Emitting constexpr C++ for model package ${ARG_ID}"
        VERBATIM
    )
    add_custom_target(emit-model-${ARG_ID} DEPENDS "${module_file}" "${header_file}")

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
