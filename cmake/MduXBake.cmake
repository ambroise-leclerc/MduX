# MduXBake.cmake
#
# The CMake surface every baker registers through, so adding an asset kind is one function call
# rather than a new pile of custom commands (ADR-007).
#
# Usage:
#   include(MduXBake)
#   mdux_declare_bake_targets()          # once, before any mdux_bake_artifact() call
#   ...
#   mdux_bake_artifact(
#       KIND    font
#       ID      roboto-ui
#       TOOL    MduX::fontbake
#       RECIPE  recipes/font/roboto-ui.toml
#       SOURCES assets/fonts/Roboto-Regular.ttf
#       OUTPUTS package.json report.json atlas.bin)
#
# Each call registers three things:
#
#   1. A `bake-<kind>-<id>` target that writes to ${CMAKE_BINARY_DIR}/mdux_bake/<kind>/<id>/.
#   2. A ctest `evidence.<kind>.<id>` with LABELS evidence, byte-comparing every output against
#      generated/<kind>/<id>/.
#   3. Membership in the aggregate targets `mdux-bake-all` and `mdux-bake-update`.
#
# ## Two labels, deliberately distinct
#
# The `evidence` label means exactly one thing: **a committed artifact is byte-identical to a
# freshly baked one**. Nothing else carries it, so when `ctest -L evidence` fails in CI the
# meaning is unambiguous - an artifact drifted from its recipe.
#
# The unit tests for the evidence modules themselves (digest, canonical JSON, bake report) use
# `evidence-unit` instead. They run in the ordinary suite. Putting them under `evidence` would
# mean a broken SHA-256 test and a drifted font atlas produced the same CI signal, which is
# precisely the distinction the dedicated CI step exists to make.
#
# ## The source-tree rule, enforced by construction
#
# A normal build **never** writes into the source tree. It bakes into the build directory and
# compares. `cmake --build <dir> --target mdux-bake-update` is the only path that copies
# build-directory artifacts over `generated/`. An author runs it deliberately and commits the
# diff; a reviewer reads that diff.
#
# This is not a convention that has to be remembered - the custom command's OUTPUT paths are all
# under CMAKE_BINARY_DIR, so a baker cannot write into the source tree without someone editing
# this file. It mirrors Cargo's OUT_DIR discipline, which MduX otherwise loses by having no
# build.rs equivalent.
#
# ## Artifacts that consume other artifacts
#
# A bake may list a committed artifact under SOURCES - the screen bake reads the font package, a
# text package and its sidecar. That edge is real for the *bake*: ninja re-runs the screen's bake
# when the committed text package changes on disk.
#
# What it is **not** is an ordering between artifacts inside one `mdux-bake-update`. Each
# `<kind>-<id>-update` target depends only on its own baked outputs, and `mdux-bake-update` depends
# on all of them independently, so ninja is free to bake the screen before the text update has
# copied the new text package into `generated/`. Registration order in CMakeLists.txt does not
# change that, and a comment claiming it does is worse than none.
#
# So a single update pass over an upstream change can stage a downstream artifact that attests the
# *previous* upstream digest. Two things follow, and both matter:
#
# 1. **Run the update to a fixpoint.** Stage, reconfigure, build, and run `ctest -L evidence`. A
#    downstream artifact left behind fails its own comparison; re-run the update and repeat until
#    the tests pass and a pass stages nothing. Introducing a *new* consumed artifact needs this
#    even to configure: the consumer's dependency on a file that does not exist yet is a ninja
#    graph error, not a build that does what it can.
#
# 2. **Nothing escapes to a reviewer.** CI builds from an empty tree, so every bake reads the
#    committed inputs; a screen staged against a superseded text package cannot match the bake that
#    reads the current one, because `report.json` records every input's digest. The failure is
#    `evidence.<kind>.<id>` on both toolchain legs. The fixpoint rule above is about not spending a
#    CI round trip to learn it.
#
# ## Host-tool resolution
#
# TOOL is always a `MduX::<tool>` target. A cross-compiling build substitutes an imported
# executable for the same name without any call site changing.

set(_MDUX_COMPARE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/MduXCompareArtifacts.cmake")
set(_MDUX_UPDATE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/MduXUpdateArtifacts.cmake")

# Creates the aggregate targets. Separate from mdux_bake_artifact() so they exist even when no
# artifact is registered yet - `cmake --build . --target mdux-bake-all` should succeed trivially
# rather than fail with "no such target", which is what a contributor hits before the first
# baker lands.
function(mdux_declare_bake_targets)
    if(TARGET mdux-bake-all)
        return()
    endif()
    add_custom_target(mdux-bake-all
        COMMENT "Baking every registered evidence artifact into the build tree")
    add_custom_target(mdux-bake-update
        COMMENT "Copying baked artifacts over generated/ - the only path that writes into the source tree")
endfunction()

function(mdux_bake_artifact)
    set(options "")
    set(single KIND ID TOOL RECIPE)
    set(multi SOURCES OUTPUTS)
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    foreach(required KIND ID TOOL RECIPE OUTPUTS)
        if(NOT ARG_${required})
            message(FATAL_ERROR "mdux_bake_artifact: ${required} is required")
        endif()
    endforeach()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_bake_artifact: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET mdux-bake-all)
        message(FATAL_ERROR
            "mdux_bake_artifact: call mdux_declare_bake_targets() before registering artifacts")
    endif()
    if(NOT TARGET ${ARG_TOOL})
        message(FATAL_ERROR
            "mdux_bake_artifact: TOOL '${ARG_TOOL}' is not a target. Host tools are resolved "
            "through a MduX::<tool> target so a cross-compiling build can substitute an "
            "imported executable; declare it before registering this artifact.")
    endif()

    foreach(component KIND ID)
        if(NOT ARG_${component} MATCHES "^[a-z0-9][a-z0-9-]*$")
            message(FATAL_ERROR
                "mdux_bake_artifact: ${component} must be a lowercase slug containing only "
                "letters, digits and '-' (got '${ARG_${component}}')")
        endif()
    endforeach()

    set(label "${ARG_KIND}/${ARG_ID}")
    set(target_name "bake-${ARG_KIND}-${ARG_ID}")
    set(baked_dir "${CMAKE_BINARY_DIR}/mdux_bake/${ARG_KIND}/${ARG_ID}")
    set(committed_dir "${CMAKE_SOURCE_DIR}/generated/${ARG_KIND}/${ARG_ID}")
    set(recipe_path "${CMAKE_SOURCE_DIR}/${ARG_RECIPE}")

    if(NOT EXISTS "${recipe_path}")
        message(FATAL_ERROR "mdux_bake_artifact: recipe '${ARG_RECIPE}' does not exist")
    endif()

    # Absolute paths for the outputs, all under the build tree - see the source-tree rule above.
    set(baked_outputs "")
    set(seen_outputs "")
    foreach(output ${ARG_OUTPUTS})
        if(IS_ABSOLUTE "${output}" OR output MATCHES "[/\\\\]")
            message(FATAL_ERROR
                "mdux_bake_artifact: OUTPUTS entries must be file names directly within the "
                "artifact directory (got '${output}')")
        endif()
        if(output IN_LIST seen_outputs)
            message(FATAL_ERROR
                "mdux_bake_artifact: duplicate OUTPUTS entry '${output}'")
        endif()
        list(APPEND seen_outputs "${output}")
        list(APPEND baked_outputs "${baked_dir}/${output}")
    endforeach()

    set(source_paths "")
    foreach(source ${ARG_SOURCES})
        if(IS_ABSOLUTE "${source}")
            list(APPEND source_paths "${source}")
        else()
            list(APPEND source_paths "${CMAKE_SOURCE_DIR}/${source}")
        endif()
    endforeach()

    # The baker runs with the source directory as its working directory, so every path it reads
    # from the recipe and every path it records in report.json is repository-relative. That is
    # what keeps a report free of absolute paths, which BakeReport::validate() rejects.
    add_custom_command(
        OUTPUT ${baked_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${baked_dir}"
        COMMAND ${ARG_TOOL} bake "${ARG_RECIPE}" "${baked_dir}"
        DEPENDS ${ARG_TOOL} "${recipe_path}" ${source_paths}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Baking ${label}"
        VERBATIM
    )

    # ALL, so an ordinary `cmake --build .` produces the artifacts the evidence tests compare.
    # Without this the tests would pass or fail depending on whether someone had remembered to
    # build a separate target first.
    add_custom_target(${target_name} ALL DEPENDS ${baked_outputs})
    add_dependencies(mdux-bake-all ${target_name})

    add_custom_target(${target_name}-update
        COMMAND "${CMAKE_COMMAND}"
                -D "BAKED_DIR=${baked_dir}"
                -D "COMMITTED_DIR=${committed_dir}"
                -D "OUTPUTS=${ARG_OUTPUTS}"
                -D "LABEL=${label}"
                -P "${_MDUX_UPDATE_SCRIPT}"
        DEPENDS ${baked_outputs}
        COMMENT "Updating generated/${label} from the build tree"
        VERBATIM
    )
    add_dependencies(mdux-bake-update ${target_name}-update)

    # Old-style positional add_test(<name> <command> [args...]) rather than
    # add_test(NAME ... COMMAND ...), matching the convention established in
    # cmake/MduXTestDiscoveryImpl.cmake and the InstallTreeConsumer test - see the comment there
    # for the CTest mis-parse that motivates it. Do not "modernize" without re-verifying.
    add_test("evidence.${ARG_KIND}.${ARG_ID}"
        "${CMAKE_COMMAND}"
        -D "BAKED_DIR=${baked_dir}"
        -D "COMMITTED_DIR=${committed_dir}"
        -D "OUTPUTS=${ARG_OUTPUTS}"
        -D "LABEL=${label}"
        -P "${_MDUX_COMPARE_SCRIPT}"
    )
    set_tests_properties("evidence.${ARG_KIND}.${ARG_ID}" PROPERTIES LABELS "evidence")
endfunction()
