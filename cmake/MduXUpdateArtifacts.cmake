# MduXUpdateArtifacts.cmake
#
# Script-mode (cmake -P) copy of freshly-baked artifacts over the committed ones. This is the
# body of the `mdux-bake-update` target, and it is the **only** path in the build that writes
# into the source tree (ADR-007, decision 3).
#
# Keeping it in its own script rather than inlining a `cmake -E copy` per output does two things:
# it can report what actually changed, so an author knows whether their edit had an effect, and
# it keeps the source-tree write in one auditable place instead of scattered across every
# mdux_bake_artifact() call site.
#
# Expected -D arguments:
#   BAKED_DIR      directory the baker wrote into, under the build tree
#   COMMITTED_DIR  the generated/<kind>/<id>/ directory tracked in git
#   OUTPUTS        ';'-separated list of file names to copy
#   LABEL          "<kind>/<id>", for messages

if(NOT DEFINED BAKED_DIR OR NOT DEFINED COMMITTED_DIR OR NOT DEFINED OUTPUTS OR NOT DEFINED LABEL)
    message(FATAL_ERROR "MduXUpdateArtifacts: BAKED_DIR, COMMITTED_DIR, OUTPUTS and LABEL are all required")
endif()

file(MAKE_DIRECTORY "${COMMITTED_DIR}")

set(changed "")
set(added "")

foreach(output ${OUTPUTS})
    set(baked "${BAKED_DIR}/${output}")
    set(committed "${COMMITTED_DIR}/${output}")

    if(NOT EXISTS "${baked}")
        # LABEL is "<kind>/<id>"; the target that produces it is "bake-<kind>-<id>", so do not
        # interpolate LABEL into a target name here.
        string(REPLACE "/" "-" bake_target "bake-${LABEL}")
        message(FATAL_ERROR
            "mdux-bake-update: ${LABEL}/${output} was not produced at ${baked}. Build the "
            "${bake_target} target first, or fix the baker.")
    endif()

    if(NOT EXISTS "${committed}")
        list(APPEND added "${output}")
    else()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files "${baked}" "${committed}"
            RESULT_VARIABLE compare_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT compare_result EQUAL 0)
            list(APPEND changed "${output}")
        endif()
    endif()

    # copy_if_different rather than copy, so an unchanged artifact keeps its mtime and does not
    # look modified to the build system or to anything watching the tree.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${baked}" "${committed}"
        RESULT_VARIABLE copy_result
    )
    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "mdux-bake-update: failed to copy ${baked} to ${committed}")
    endif()
endforeach()

if(added OR changed)
    if(added)
        string(REPLACE ";" ", " added_text "${added}")
        message(STATUS "mdux-bake-update ${LABEL}: added ${added_text}")
    endif()
    if(changed)
        string(REPLACE ";" ", " changed_text "${changed}")
        message(STATUS "mdux-bake-update ${LABEL}: updated ${changed_text}")
    endif()
    message(STATUS "  review the diff under ${COMMITTED_DIR} and commit it")
else()
    message(STATUS "mdux-bake-update ${LABEL}: already up to date")
endif()
