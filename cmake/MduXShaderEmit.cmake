# Generated C++ from a committed shader package (issue #121).
#
# `mdux_emit_shader_package()` runs mdux-shaderemit over a committed
# `generated/shader/<id>/package.json` and produces two files in the *build* tree:
#
#   <binary>/mdux_generated/shader/<identifier>.cppm   a module interface
#   <binary>/mdux_generated/shader/<identifier>.hpp    the same data, for consumers that cannot
#                                                      import a named module
#
# Neither is committed. The reviewed artifact is the JSON and the digests beside it; the C++ is a
# mechanical rendering of exactly those bytes, and committing it would put a few thousand hex
# bytes under review that no reviewer can meaningfully check - noise that hides signal. See
# tools/shader/Emit.cppm for the argument in full.
#
# This deliberately does not use MduXBake.cmake. A bake produces a *committed* artifact and gets a
# byte-comparison test; an emission produces a build artifact and gets none, because the bytes it
# renders are already byte-compared as `package.json` and `shaders.spv`. Registering it as a bake
# would claim a second, redundant piece of evidence.

include_guard(GLOBAL)

# mdux_shader_identifier(<out_var> <package_id>)
#
# The C++ identifier mdux-shaderemit derives from a package id, and therefore the stem of the
# files it writes. This must agree with identifierFor() in tools/shader/Emit.cpp exactly; a
# disagreement surfaces as a build failure on a file nobody wrote, with nothing pointing at the
# cause. It is a named function rather than two inline lines so that a test can call it - see
# `shader-identifier-parity` in tests/CMakeLists.txt, which runs this and the C++ side over the
# same ids and compares. Asserting only the C++ half, as this file previously claimed to do,
# cannot catch a CMake-side divergence.
function(mdux_shader_identifier out_var package_id)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" identifier "${package_id}")
    # A C++ identifier may not start with a digit; a package id may.
    if(identifier MATCHES "^[0-9]")
        set(identifier "_${identifier}")
    endif()
    set(${out_var} "${identifier}" PARENT_SCOPE)
endfunction()

# mdux_emit_shader_package(ID <id> [OUT_MODULE <var>] [OUT_HEADER <var>] [OUT_DIR <var>])
#
# Adds a custom command generating the sources, and a target `emit-shader-<id>` that produces them.
# The caller adds the returned module file to a target's CXX_MODULES file set.
function(mdux_emit_shader_package)
    set(options "")
    set(single ID OUT_MODULE OUT_HEADER OUT_DIR)
    set(multi "")
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_emit_shader_package: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_ID)
        message(FATAL_ERROR "mdux_emit_shader_package: ID is required")
    endif()
    if(NOT TARGET mdux-shaderemit)
        message(FATAL_ERROR "mdux_emit_shader_package: target mdux-shaderemit does not exist")
    endif()

    set(package_path "${CMAKE_SOURCE_DIR}/generated/shader/${ARG_ID}/package.json")
    if(NOT EXISTS "${package_path}")
        message(FATAL_ERROR
            "mdux_emit_shader_package: ${package_path} does not exist. Bake it first with "
            "`cmake --build <dir> --target mdux-bake-update`.")
    endif()

    mdux_shader_identifier(identifier "${ARG_ID}")

    set(output_dir "${CMAKE_BINARY_DIR}/mdux_generated/shader")
    set(module_file "${output_dir}/${identifier}.cppm")
    set(header_file "${output_dir}/${identifier}.hpp")

    # DEPENDS on the sidecar as well as the package: editing one without the other is exactly the
    # inconsistency the emitter refuses to render, and it should refuse at build time rather than
    # after a stale generation.
    # WORKING_DIRECTORY is the repository root and the package path is passed relative to it, as
    # mdux_bake_artifact() does, so the provenance comment the emitter writes into the generated
    # source names a repository path rather than the machine that ran the build. The generated
    # files are not committed, but two developers' copies should still be identical - an absolute
    # path in there would defeat a shared compiler cache for no benefit.
    add_custom_command(
        OUTPUT "${module_file}" "${header_file}"
        COMMAND mdux-shaderemit "generated/shader/${ARG_ID}/package.json" "${output_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS
            mdux-shaderemit
            "${package_path}"
            "${CMAKE_SOURCE_DIR}/generated/shader/${ARG_ID}/shaders.spv"
        COMMENT "Emitting C++ for shader package ${ARG_ID}"
        VERBATIM
    )

    add_custom_target(emit-shader-${ARG_ID} DEPENDS "${module_file}" "${header_file}")

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
