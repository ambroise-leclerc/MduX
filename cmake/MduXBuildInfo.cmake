# MduXBuildInfo.cmake
#
# Provenance a baked artifact's report.json is allowed to carry: which tool *version* produced it
# (ADR-007, decision 5).
#
# Creates an INTERFACE target MduX_buildinfo (alias MduX::BuildInfo) carrying one compile
# definition:
#
#   MDUX_TOOL_VERSION  the project version, e.g. "0.2.0"
#
# Every host tool links MduX::BuildInfo and passes this straight into BakeReport::toolVersion.
#
# Deliberately does NOT expose a git commit SHA. An earlier version of this file also defined
# MDUX_GIT_SHA from a configure-time `git rev-parse HEAD`, for BakeReport to embed as
# `toolGitSha`. That was caught in review (issue #52) as structurally unsound: baking happens at
# commit H0, but embedding H0's hash into report.json and committing it produces a *different*
# commit H1 (the tree now contains a file H0 didn't have) - so CI re-baking at H1 always embeds
# H1, the committed copy always says H0, and the byte-comparison this whole pipeline exists to
# run fails every single time, for every report, regardless of whether the artifact actually
# changed. There is no fixed point: a commit's tree cannot correctly name its own hash, because
# the hash is computed from the (already-final) tree. See ADR-007, decision 5, for the full
# writeup and the rejected-alternatives entry recording this so it isn't re-proposed unread.
#
# A commit SHA is still fine for *diagnostic, non-compared* output - e.g. a future tool's
# `--version` string - which is a different use case with no self-reference problem, since that
# output is never checked byte-for-byte against a committed copy. Provided as
# MDUX_BUILD_DIAGNOSTIC_SHA, first used by #314's `mdux-verify-ui --medui-evidence-out` for the
# `producer.source` field of a *derived, uncommitted* MEDUI-PROFILE-RENDERED evidence envelope
# (ADR-014 decision 4, ADR-016). It is a placeholder of forty zeros when HEAD cannot be read (a
# tarball build, a shallow checkout with no `.git`). Never plumb it into BakeReport.
#
# Usage:
#   include(cmake/MduXBuildInfo.cmake)
#   mdux_define_build_info()
#   ...
#   target_link_libraries(mdux-fontbake PRIVATE MduX::BuildInfo)

function(mdux_define_build_info)
    add_library(MduX_buildinfo INTERFACE)
    add_library(MduX::BuildInfo ALIAS MduX_buildinfo)
    set_target_properties(MduX_buildinfo PROPERTIES EXPORT_NAME BuildInfo)
    # Diagnostic, never byte-compared: the current commit, for the derived evidence envelope's
    # `producer.source`. Forty zeros when HEAD is unavailable so a consumer sees a placeholder
    # rather than a build failure. See the header comment for why this must never reach BakeReport.
    set(_mdux_diagnostic_sha "0000000000000000000000000000000000000000")
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _mdux_head
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _mdux_head_rc
        ERROR_QUIET)
    string(LENGTH "${_mdux_head}" _mdux_head_len)
    if(_mdux_head_rc EQUAL 0 AND _mdux_head_len EQUAL 40 AND _mdux_head MATCHES "^[0-9a-f]+$")
        set(_mdux_diagnostic_sha "${_mdux_head}")
    endif()

    target_compile_definitions(MduX_buildinfo INTERFACE
        MDUX_TOOL_VERSION="${PROJECT_VERSION}"
        MDUX_BUILD_DIAGNOSTIC_SHA="${_mdux_diagnostic_sha}"
    )

    message(STATUS "MduX build info: version ${PROJECT_VERSION}, diagnostic SHA ${_mdux_diagnostic_sha}")
endfunction()
