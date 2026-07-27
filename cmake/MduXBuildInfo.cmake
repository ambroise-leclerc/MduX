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
# output is never checked byte-for-byte against a committed copy. This file does not currently
# provide one; add it back under a name that makes the distinction obvious (e.g.
# MDUX_BUILD_DIAGNOSTIC_SHA) if that need arises, and never plumb it into BakeReport.
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
    target_compile_definitions(MduX_buildinfo INTERFACE
        MDUX_TOOL_VERSION="${PROJECT_VERSION}"
    )

    message(STATUS "MduX build info: version ${PROJECT_VERSION}")
endfunction()
