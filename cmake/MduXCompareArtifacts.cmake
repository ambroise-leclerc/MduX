# MduXCompareArtifacts.cmake
#
# Script-mode (cmake -P) byte comparison of freshly-baked artifacts against the committed ones,
# used by the `evidence`-labelled ctest that mdux_bake_artifact() registers (ADR-007).
#
# `cmake -E compare_files` would be enough to decide pass or fail, but its failure output is
# just "files differ", which tells whoever broke a baker nothing about how. This reports the
# first differing byte offset, both bytes, and the surrounding context - the difference between
# "the artifact changed" and "the artifact changed at the atlas header's width field".
#
# Expected -D arguments:
#   BAKED_DIR      directory the baker wrote into, under the build tree
#   COMMITTED_DIR  the generated/<kind>/<id>/ directory tracked in git
#   OUTPUTS        ';'-separated list of file names to compare
#   LABEL          "<kind>/<id>", for messages
#
# The cheap check runs first: sizes, then a whole-file comparison. The hex scan that locates the
# offset only runs on a file that has already been shown to differ, so the cost is paid on
# failure and never on a passing build.

if(NOT DEFINED BAKED_DIR OR NOT DEFINED COMMITTED_DIR OR NOT DEFINED OUTPUTS OR NOT DEFINED LABEL)
    message(FATAL_ERROR "MduXCompareArtifacts: BAKED_DIR, COMMITTED_DIR, OUTPUTS and LABEL are all required")
endif()

# Bytes per window in the coarse scan. Chosen so the fine scan never loops more than this many
# times, while the coarse scan stays a few hundred reads even on a large payload.
set(_MDUX_COMPARE_WINDOW 65536)

# Returns the offset of the first differing byte between two files known to differ.
#
# Scans in windows via file(READ ... OFFSET ... LIMIT ... HEX) rather than reading each file
# whole. Reading the whole file and walking it with string(SUBSTRING) is quadratic - each
# SUBSTRING call scans the multi-megabyte hex string from the start - and measured 8.3 s on a
# 4 MiB payload whose difference was near the end. Windowed reads make it linear: the same case
# measures 2.3 s, so a 40 MB model blob stays practical rather than timing out.
#
# This runs only after `cmake -E compare_files` has already established that the files differ, so
# it is never on the passing path. It still has to be fast enough that a failing evidence test
# reports rather than times out.
function(_mdux_first_difference baked committed out_offset out_baked_byte out_committed_byte)
    file(SIZE "${baked}" baked_size)
    file(SIZE "${committed}" committed_size)
    set(shortest ${baked_size})
    if(committed_size LESS shortest)
        set(shortest ${committed_size})
    endif()

    set(window_start 0)
    set(found_window -1)
    while(window_start LESS shortest)
        math(EXPR remaining "${shortest} - ${window_start}")
        set(take ${_MDUX_COMPARE_WINDOW})
        if(remaining LESS take)
            set(take ${remaining})
        endif()
        file(READ "${baked}" baked_window OFFSET ${window_start} LIMIT ${take} HEX)
        file(READ "${committed}" committed_window OFFSET ${window_start} LIMIT ${take} HEX)
        if(NOT baked_window STREQUAL committed_window)
            set(found_window ${window_start})
            break()
        endif()
        math(EXPR window_start "${window_start} + ${take}")
    endwhile()

    if(found_window LESS 0)
        # No differing byte in the common prefix, so one file is a prefix of the other and the
        # difference is the length itself.
        set(${out_offset} ${shortest} PARENT_SCOPE)
        set(${out_baked_byte} "<end of file>" PARENT_SCOPE)
        set(${out_committed_byte} "<end of file>" PARENT_SCOPE)
        return()
    endif()

    # Fine pass: byte by byte within the window just identified. Both hex strings are at most
    # 2 * _MDUX_COMPARE_WINDOW characters, so SUBSTRING here is cheap.
    string(LENGTH "${baked_window}" window_hex_length)
    set(cursor 0)
    while(cursor LESS window_hex_length)
        string(SUBSTRING "${baked_window}" ${cursor} 2 baked_byte)
        string(SUBSTRING "${committed_window}" ${cursor} 2 committed_byte)
        if(NOT baked_byte STREQUAL committed_byte)
            math(EXPR byte_offset "${found_window} + ${cursor} / 2")
            set(${out_offset} ${byte_offset} PARENT_SCOPE)
            set(${out_baked_byte} "0x${baked_byte}" PARENT_SCOPE)
            set(${out_committed_byte} "0x${committed_byte}" PARENT_SCOPE)
            return()
        endif()
        math(EXPR cursor "${cursor} + 2")
    endwhile()

    # Unreachable: the window compared unequal, so some byte in it differs. Reported rather than
    # silently returning a wrong offset, because a wrong offset in a diagnostic is worse than none.
    set(${out_offset} ${found_window} PARENT_SCOPE)
    set(${out_baked_byte} "<unknown>" PARENT_SCOPE)
    set(${out_committed_byte} "<unknown>" PARENT_SCOPE)
endfunction()

# Accumulated as a plain string with explicit newlines, never as a list: a CMake list uses ';'
# as its separator, so any ';' inside a message would silently split it into two elements and be
# lost when the list was joined back together (observed while testing this script).
set(failures "")
set(failure_count 0)

foreach(output ${OUTPUTS})
    set(baked "${BAKED_DIR}/${output}")
    set(committed "${COMMITTED_DIR}/${output}")

    if(NOT EXISTS "${baked}")
        string(APPEND failures "  ${output}: the baker did not produce it at ${baked}\n")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()
    if(NOT EXISTS "${committed}")
        string(APPEND failures
            "  ${output}: no committed copy at ${committed} - this artifact has never been "
            "committed. Run 'cmake --build <dir> --target mdux-bake-update' and commit the "
            "result.\n")
        math(EXPR failure_count "${failure_count} + 1")
        continue()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${baked}" "${committed}"
        RESULT_VARIABLE compare_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(compare_result EQUAL 0)
        continue()
    endif()

    file(SIZE "${baked}" baked_size)
    file(SIZE "${committed}" committed_size)
    _mdux_first_difference("${baked}" "${committed}" offset baked_byte committed_byte)
    string(APPEND failures
        "  ${output}: first differs at byte ${offset} - baked ${baked_byte}, committed "
        "${committed_byte} (sizes: baked ${baked_size}, committed ${committed_size})\n")
    math(EXPR failure_count "${failure_count} + 1")
endforeach()

if(failure_count GREATER 0)
    message(FATAL_ERROR
        "Evidence mismatch for ${LABEL}: the baked artifact is not byte-identical to the "
        "committed one.\n"
        "${failures}\n"
        "\n"
        "This means the committed artifact no longer corresponds to its recipe and sources, or "
        "the baker's output changed. Neither is something to work around: re-bake deliberately "
        "with\n"
        "    cmake --build <build-dir> --target mdux-bake-update\n"
        "then review the resulting diff and commit it. If the diff is unexpected, the baker "
        "changed behaviour and that is the bug. See ADR-007.")
endif()

message(STATUS "Evidence OK for ${LABEL}: every output is byte-identical to the committed copy")
