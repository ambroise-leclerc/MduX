# Configure-time checks for mdux_compile_screen()'s recipe reading (issue #198).
#
# Run as `cmake -DCASE=<case> -P tests/cmake/CompileScreenRecipeTest.cmake`. Each case is registered
# as its own CTest entry in tests/CMakeLists.txt; the negative ones match the message rather than the
# exit status, because a bare "it failed" would be satisfied by a typo in this file.
#
# Script mode rather than a fixture project: what is under test is the extraction and the identity
# check, both of which are pure functions of the recipe text. Configuring a whole project to exercise
# them would be slower and would report a failure two layers away from its cause.

cmake_minimum_required(VERSION 4.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MduXCompileScreen.cmake")

# A recipe carrying a decoy `source` in a table the compiler ignores, before the one it reads. This
# is the shape that made an unscoped regex dangerous: CMake would take `decoy.png` as the dependency
# while mdux-meduic compiled `real.medui`, so editing the screen would stop triggering a rebake.
set(decoy_recipe
"# a comment mentioning [package] and source = \"commented-out.medui\"
[assets]
source = \"decoy.png\"

[package]
id            = \"real-screen\"
source        = \"recipes/screen/real-screen/RealScreen.medui\"
surfaceWidth  = 400
surfaceHeight = 300
")

if(CASE STREQUAL "scoped")
    _mdux_screen_package_field(value "${decoy_recipe}" "source" "decoy.toml")
    if(NOT value STREQUAL "recipes/screen/real-screen/RealScreen.medui")
        message(FATAL_ERROR "expected the [package] source, got '${value}'")
    endif()
    _mdux_screen_package_field(id_value "${decoy_recipe}" "id" "decoy.toml")
    if(NOT id_value STREQUAL "real-screen")
        message(FATAL_ERROR "expected the [package] id, got '${id_value}'")
    endif()
    message(STATUS "scoped: OK")

elseif(CASE STREQUAL "identity")
    # The pair the wrapper must refuse: a recipe that compiles one screen, registered as another.
    _mdux_screen_check_recipe("${decoy_recipe}" "decoy.toml" "another-screen" unused_source)
    message(FATAL_ERROR "unreachable: a mismatched id was accepted")

elseif(CASE STREQUAL "identity-accepts")
    _mdux_screen_check_recipe("${decoy_recipe}" "decoy.toml" "real-screen" source_value)
    if(NOT source_value STREQUAL "recipes/screen/real-screen/RealScreen.medui")
        message(FATAL_ERROR "expected the [package] source, got '${source_value}'")
    endif()
    message(STATUS "identity-accepts: OK")

elseif(CASE STREQUAL "ambiguous")
    set(ambiguous
"[package]
id     = \"real-screen\"
source = \"one.medui\"
source = \"two.medui\"
")
    _mdux_screen_package_field(value "${ambiguous}" "source" "ambiguous.toml")
    message(FATAL_ERROR "unreachable: two source keys were accepted")

elseif(CASE STREQUAL "missing")
    set(missing
"[assets]
source = \"decoy.png\"

[package]
id = \"real-screen\"
")
    _mdux_screen_package_field(value "${missing}" "source" "missing.toml")
    message(FATAL_ERROR "unreachable: a [package] with no source was accepted")

elseif(CASE STREQUAL "committed")
    # The recipe this repository actually commits, read the way the wrapper reads it.
    file(READ "${CMAKE_CURRENT_LIST_DIR}/../../recipes/screen/endoscope-monitor.toml" committed)
    _mdux_screen_check_recipe("${committed}" "recipes/screen/endoscope-monitor.toml" "endoscope-monitor" source_value)
    if(NOT source_value STREQUAL "recipes/screen/endoscope-monitor/EndoscopeMonitor.medui")
        message(FATAL_ERROR "expected the committed screen's source, got '${source_value}'")
    endif()
    message(STATUS "committed: OK")

else()
    message(FATAL_ERROR "unknown CASE '${CASE}'")
endif()
