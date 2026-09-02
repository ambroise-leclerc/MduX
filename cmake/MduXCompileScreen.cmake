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
#   - **The four outputs are fixed here**, not per call site. ADR-012 makes `package.json`,
#     `goldens.json` and `report.json` unconditional and ADR-014 decision 4 adds `verification.json`,
#     so a screen that pins nothing writes `[]` rather than one fewer file. A call site listing its
#     own OUTPUTS could drop one and produce a smaller artifact that still passed its own
#     comparison.
#   - **The `.medui` source becomes a dependency automatically**, read out of the recipe. A
#     forgotten SOURCES entry does not fail: it makes edits to the screen stop triggering a rebake,
#     so the committed artifact quietly stops matching its source until someone runs the update
#     target for another reason.

include_guard(GLOBAL)

# _mdux_screen_package_field(<out_var> <recipe_text> <key> <label>)
#
# The value of `key` in the recipe's `[package]` table, and nothing else's.
#
# Scoped to that table on purpose. `mdux-meduic` reads `document.table("package")->require("source")`
# and ignores tables it does not know, so a recipe may legally carry an earlier `source` key in
# another table - and a regex over the whole document would then hand CMake a different file from the
# one the compiler compiles. The dependency edge would point at the wrong input, editing the real
# screen would stop triggering a rebake, and `mdux-bake-update` would stage bytes nobody asked for.
# That is precisely the silent drift this wrapper exists to prevent, so getting the scope wrong here
# is worse than not having the wrapper.
#
# Two keys in one table is an error rather than a first-wins: the compiler's TOML reader decides that
# case, this does not have to guess the same way, and a recipe that ambiguous should be fixed.
#
# Line-based rather than a single regex, because a value or a comment may contain `[` - this
# repository's own recipes carry `^[a-z0-9]...` inside a comment in `[package]` - and a section match
# that stopped at the next bracket would truncate the table.
function(_mdux_screen_package_field out_var recipe_text key label)
    string(REPLACE ";" "\\;" escaped "${recipe_text}")
    string(REGEX REPLACE "\r?\n" ";" lines "${escaped}")

    set(current_table "")
    set(found "")
    foreach(line IN LISTS lines)
        # Both patterns are anchored after leading whitespace, so a commented-out `# source = "x"` or
        # `# [package]` cannot match. That is why no comment stripping is needed, and why stripping
        # would be wrong: a `#` inside a quoted value is part of the value.
        if(line MATCHES "^[ \t]*\\[([A-Za-z0-9_.-]+)\\]")
            set(current_table "${CMAKE_MATCH_1}")
        elseif(current_table STREQUAL "package" AND line MATCHES "^[ \t]*${key}[ \t]*=[ \t]*\"([^\"]*)\"")
            list(APPEND found "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    list(LENGTH found count)
    if(count EQUAL 0)
        message(FATAL_ERROR
            "mdux_compile_screen: ${label} has no `${key} = \"...\"` line in its [package] table. "
            "The compiler reads that table, so anything else here would describe a different recipe.")
    endif()
    if(count GREATER 1)
        message(FATAL_ERROR
            "mdux_compile_screen: ${label} declares `${key}` ${count} times in [package]. "
            "Which one the compiler uses is its TOML reader's business; this refuses to guess.")
    endif()

    list(GET found 0 value)
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# _mdux_screen_check_recipe(<recipe_text> <label> <expected_id> <out_source_var>)
#
# Fails configuration unless the recipe describes the screen the caller says it does, and yields the
# `.medui` source so it can become a dependency.
#
# The id check is the one that keeps identity single. `mdux-meduic` checks the recipe's id against the
# screen's own name, but it never sees the CMake `ID` - so without this, `mdux_compile_screen(ID foo
# RECIPE bar.toml)` bakes a package that calls itself `bar` into `mdux_bake/screen/foo`, compares it
# as `evidence.screen.foo` and stages it into `generated/screen/foo/`. Every check passes while the
# directory, the test and the package disagree about which screen this is.
function(_mdux_screen_check_recipe recipe_text label expected_id out_source_var)
    _mdux_screen_package_field(recipe_id "${recipe_text}" "id" "${label}")
    if(NOT recipe_id STREQUAL expected_id)
        message(FATAL_ERROR
            "mdux_compile_screen: ${label} declares id '${recipe_id}', but was registered as "
            "'${expected_id}'. The id names the artifact directory, the evidence test and the "
            "emitted C++ identifier, so these must be one name.")
    endif()

    _mdux_screen_package_field(screen_source "${recipe_text}" "source" "${label}")
    set(${out_source_var} "${screen_source}" PARENT_SCOPE)
endfunction()

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

    # The screen source and the recipe's own id, taken from the recipe rather than from the call
    # site. See the two helpers above for what each check is for.
    file(READ "${recipe_path}" recipe_text)
    _mdux_screen_check_recipe("${recipe_text}" "${ARG_RECIPE}" "${ARG_ID}" screen_source)

    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${screen_source}")
        message(FATAL_ERROR
            "mdux_compile_screen: ${ARG_RECIPE} names the source '${screen_source}', which does not exist")
    endif()

    mdux_bake_artifact(
        KIND screen
        ID ${ARG_ID}
        TOOL mdux-meduic
        # The second half of the one production sequence (#254): mdux-meduic compiles the screen,
        # then this renders it and writes verification.json plus the report members naming it. Still
        # one mdux_bake_artifact() registration for screen/<id>, which ADR-014 decision 4 requires -
        # a second one would collide in its generated target, its test and its output directory.
        THEN_TOOLS mdux-verify-bake
        RECIPE ${ARG_RECIPE}
        SOURCES
            ${screen_source}
            ${ARG_SOURCES}
            # The committed artifacts the render reads. They are already dependencies of the compile
            # for the text packages; the shader package is new here, and it is a real edge: re-baking
            # mdux-ui changes the frame this screen is verified against.
            generated/shader/mdux-ui/package.json
            generated/shader/mdux-ui/shaders.spv
        # Fixed here rather than per call site: all four are unconditional (ADR-012 decision 1,
        # ADR-014 decision 4).
        OUTPUTS
            package.json
            goldens.json
            report.json
            verification.json
    )

    # The CI gate #255 asks for, registered here so a second committed screen gets one without
    # anybody remembering to add it - the same reason the bake registers its own evidence test.
    #
    # Its subject is the **committed** bundle, and that is what makes it a different check from the
    # bake step above rather than a slower copy of it. `mdux-verify-bake` renders the bundle that was
    # just produced in the build tree; this renders `generated/screen/<id>/`, the bytes a consumer of
    # this repository actually gets. The two agree today because `evidence.screen.<id>` byte-compares
    # them - and a gate that assumed that agreement rather than exercising the committed files would
    # be resting on the check it is supposed to be independent of.
    #
    # `--locales=all` is not a choice the invocation makes. It is the only accepted spelling, and the
    # driver refuses any narrowing of the screen's own manifest (ADR-014 decision 2), so this line
    # cannot be edited into one that verifies less while still looking like a full run.
    #
    # The diff directory is under the build tree, never `generated/`: ADR-014 decision 4 keeps the
    # image out of the byte-compared bundle, and the "no source-tree writes" gate on the GCC and
    # macOS legs would report it if that changed. CI uploads this directory when the step fails.
    # `add_test(NAME ... COMMAND ...)`, and the caution in cmake/MduXTestDiscoveryImpl.cmake does not
    # transfer. That file writes add_test() lines into a generated CTestTestfile.cmake, which CTest
    # parses itself and where the keyword form mis-parses; this one is evaluated by CMake at
    # configure time, where the keyword form is the only one that substitutes an executable target
    # for its built location. The positional form emits the literal string "mdux-verify-ui" and the
    # test does not run - verified here before changing it.
    add_test(NAME "verify.screen.${ARG_ID}"
        COMMAND mdux-verify-ui
            "--screen=${CMAKE_SOURCE_DIR}/generated/screen/${ARG_ID}"
            "--locales=all"
            "--diff-image-dir=${CMAKE_BINARY_DIR}/verify-diff"
    )
    # No SKIP_RETURN_CODE, deliberately, and it is worth saying why since every neighbouring rendered
    # test has one. `mdux-verify-ui` has no skip status: an absent device is `RunState::NoRenderDevice`
    # and exits 3 like every other impossible run, so a `SKIP_RETURN_CODE 77` here would be dead
    # configuration - CTest reports the run as Failed, which was verified with the ICD unset.
    #
    # That is also the behaviour to want. #254 made the bake render, so a leg without a device fails
    # to *build*; this test cannot be reached in a tree that did not already prove one existed. An
    # absent device at test time is therefore a device that disappeared, which is an infrastructure
    # failure - and #255 requires exactly that reading rather than a skip.
    #
    # `verify_ui_pixel_test` keeps its 77 because it is a different thing: a test binary that returns
    # 77 itself, over a fixture, on hosts that may legitimately have no GPU at all.
    set_tests_properties("verify.screen.${ARG_ID}" PROPERTIES LABELS "verify")
endfunction()
