# Registering a compiled screen as an evidence artifact (issue #198).
#
# `mdux_compile_screen()` is a thin front for `mdux_bake_artifact()`: a screen is baked like a font,
# a shader or a model, so it gets the same build-tree output, the same `-update` target that stages
# into `generated/`, and the same `evidence.screen.<id>` byte-comparison on every toolchain leg.
# Nothing about a screen justifies a second mechanism, and a second mechanism is how one artifact
# kind ends up with weaker evidence than its neighbours.
#
# What this wrapper adds over calling `mdux_bake_artifact()` directly is the two things a caller
# could otherwise get wrong silently:
#
#   - **The three outputs are fixed here**, not per call site. ADR-012 makes `package.json`,
#     `goldens.json` and `report.json` unconditional, so a screen that pins nothing writes `[]`
#     rather than one fewer file. A call site listing its own OUTPUTS could drop one and produce a
#     smaller artifact that still passed its own comparison.
#   - **The `.medui` source becomes a dependency automatically**, read out of the recipe. A
#     forgotten SOURCES entry does not fail: it makes edits to the screen stop triggering a rebake,
#     so the committed artifact quietly stops matching its source until someone runs the update
#     target for another reason.

include_guard(GLOBAL)

# mdux_compile_screen(ID <id> RECIPE <path> [SOURCES <path>...])
#
# SOURCES names further inputs the recipe references - the font and text packages a screen that
# draws text needs. The `.medui` source itself is found in the recipe and does not have to be
# repeated.
function(mdux_compile_screen)
    set(options "")
    set(single ID RECIPE)
    set(multi SOURCES)
    cmake_parse_arguments(ARG "${options}" "${single}" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "mdux_compile_screen: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required ID RECIPE)
        if(NOT ARG_${required})
            message(FATAL_ERROR "mdux_compile_screen: ${required} is required")
        endif()
    endforeach()

    set(recipe_path "${CMAKE_SOURCE_DIR}/${ARG_RECIPE}")
    if(NOT EXISTS "${recipe_path}")
        message(FATAL_ERROR "mdux_compile_screen: recipe '${ARG_RECIPE}' does not exist")
    endif()

    # The screen source, taken from the recipe rather than from the call site. A regex over one
    # `key = "value"` line rather than a TOML parse: CMake has no TOML reader, the compiler is the
    # authority on the recipe's meaning, and the only thing needed here is the dependency edge.
    # A recipe whose `source` this cannot find is a configure error rather than a build that
    # silently never rebakes.
    file(READ "${recipe_path}" recipe_text)
    string(REGEX MATCH "\n[ \t]*source[ \t]*=[ \t]*\"([^\"]+)\"" _match "\n${recipe_text}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR
            "mdux_compile_screen: no `source = \"...\"` line in ${ARG_RECIPE}. The screen source has "
            "to be a dependency of the bake, or editing it would not trigger one.")
    endif()
    set(screen_source "${CMAKE_MATCH_1}")

    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${screen_source}")
        message(FATAL_ERROR
            "mdux_compile_screen: ${ARG_RECIPE} names the source '${screen_source}', which does not exist")
    endif()

    mdux_bake_artifact(
        KIND screen
        ID ${ARG_ID}
        TOOL mdux-meduic
        RECIPE ${ARG_RECIPE}
        SOURCES
            ${screen_source}
            ${ARG_SOURCES}
        # Fixed here rather than per call site: all three are unconditional (ADR-012, decision 1).
        OUTPUTS
            package.json
            goldens.json
            report.json
    )
endfunction()
