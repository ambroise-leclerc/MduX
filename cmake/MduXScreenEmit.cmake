# Generated C++ from a committed screen package (issue #197).
#
# `mdux_emit_screen_package()` runs mdux-screenemit over a committed screen `package.json` and
# produces two files in the *build* tree:
#
#   <binary>/mdux_generated/screen/<identifier>.cppm   a module interface
#   <binary>/mdux_generated/screen/<identifier>.hpp    the same screen, for consumers that cannot
#                                                      import a named module
#
# Neither is committed. The reviewed artifact is the JSON and the digests beside it; the C++ is a
# mechanical rendering of exactly those bytes. See tools/medui/Emit.cppm for the argument in full,
# and cmake/MduXShaderEmit.cmake for the precedent this follows deliberately rather than by
# resemblance - ADR-012 decision 3 names it as the arrangement to copy.
#
# Like the shader emitter, this is not a bake: an emission produces a build artifact and gets no
# byte-comparison test, because the bytes it renders are already byte-compared as `package.json`.

include_guard(GLOBAL)

# mdux_screen_identifier(<out_var> <package_id>)
#
# The C++ identifier mdux-screenemit derives from a package id, and therefore the stem of the files
# it writes. This must agree with identifierForScreen() in tools/medui/Emit.cpp exactly; a
# disagreement surfaces as a build failure on a file nobody wrote, with nothing pointing at the
# cause. It is a named function rather than two inline lines so that a test can call it - see
# `medui-screen-identifier-parity`, which runs this and the C++ side over the same ids and compares.
# Asserting only the C++ half is how the shader pair drifted on the leading-digit rule.
function(mdux_screen_identifier out_var package_id)
    # Unconditionally prefixed, which is what makes the answer an identifier rather than merely
    # identifier-shaped: `class`, `namespace`, `module` and `import` are all legal package slugs, and
    # mapping them to themselves produced a namespace and a module name no compiler accepts. Both
    # sides agreed on that invalid answer, so the parity test could not see it - which is why the
    # parity id list now carries keywords. The prefix also removes the leading-digit case this
    # function used to handle with a second rule.
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" identifier "${package_id}")
    set(${out_var} "screen_${identifier}" PARENT_SCOPE)
endfunction()

# mdux_emit_screen_package(ID <id> [PACKAGE <path>] [OUT_MODULE <var>] [OUT_HEADER <var>]
#                          [OUT_DIR <var>])
#
# Adds a custom command generating the sources, and a target `emit-screen-<id>` that produces them.
# The caller adds the returned module file to a target's CXX_MODULES file set.
#
# PACKAGE defaults to `generated/screen/<id>/package.json`, which is where #198's bake will put it.
# It is an argument because no screen is committed there yet: the test corpus emits from a fixture,
# and a function that could only read `generated/` would have to be rewritten the moment it had a
# second caller.
function(mdux_emit_screen_package)
    set(options "")
    set(single ID PACKAGE OUT_MODULE OUT_HEADER OUT_DIR)
    set(multi "")
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_emit_screen_package: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_ID)
        message(FATAL_ERROR "mdux_emit_screen_package: ID is required")
    endif()
    if(NOT TARGET mdux-screenemit)
        message(FATAL_ERROR "mdux_emit_screen_package: target mdux-screenemit does not exist")
    endif()

    if(ARG_PACKAGE)
        set(package_relative "${ARG_PACKAGE}")
    else()
        set(package_relative "generated/screen/${ARG_ID}/package.json")
    endif()
    set(package_path "${CMAKE_SOURCE_DIR}/${package_relative}")

    if(NOT EXISTS "${package_path}")
        message(FATAL_ERROR
            "mdux_emit_screen_package: ${package_path} does not exist. Compile the screen first.")
    endif()

    # The id the caller declares and the id the package carries must be the same, because the
    # filenames are predicted from the first and written from the second. Checked here rather than
    # left to fail as a missing file: `string(JSON)` reads the artifact CMake already depends on,
    # and the message can name both values.
    file(READ "${package_path}" package_text)
    string(JSON package_id GET "${package_text}" id)
    if(NOT package_id STREQUAL ARG_ID)
        message(FATAL_ERROR
            "mdux_emit_screen_package: ${package_relative} declares id '${package_id}', but was "
            "registered as '${ARG_ID}'. The generated filenames are derived from the id, so these "
            "must match.")
    endif()

    mdux_screen_identifier(identifier "${ARG_ID}")

    # Two adjacent separators map to `__`, which is reserved to the implementation everywhere in a
    # program. One `_` per separator keeps the mapping injective - collapsing a run would let two
    # screens claim one filename - so the reserved case is refused rather than rewritten, here and in
    # identifierForScreen().
    if(identifier MATCHES "__")
        message(FATAL_ERROR
            "mdux_emit_screen_package: id '${ARG_ID}' maps to '${identifier}', which is a reserved "
            "identifier. Avoid two adjacent separators in a screen id.")
    endif()

    set(output_dir "${CMAKE_BINARY_DIR}/mdux_generated/screen")
    set(module_file "${output_dir}/${identifier}.cppm")
    set(header_file "${output_dir}/${identifier}.hpp")

    # WORKING_DIRECTORY is the repository root and the package path is passed relative to it, as
    # mdux_emit_shader_package() does, so the provenance comment the emitter writes into the
    # generated source names a repository path rather than the machine that ran the build. The
    # generated files are not committed, but two developers' copies should still be identical - an
    # absolute path in there would defeat a shared compiler cache for no benefit.
    add_custom_command(
        OUTPUT "${module_file}" "${header_file}"
        COMMAND mdux-screenemit "${package_relative}" "${output_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS
            mdux-screenemit
            "${package_path}"
        COMMENT "Emitting C++ for screen package ${ARG_ID}"
        VERBATIM
    )

    add_custom_target(emit-screen-${ARG_ID} DEPENDS "${module_file}" "${header_file}")

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
