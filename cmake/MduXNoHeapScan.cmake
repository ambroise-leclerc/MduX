# MduXNoHeapScan.cmake
#
# Scans compiled objects for undefined references that would contradict a zone's stated guarantee.
# Two profiles, selected by MDUX_SCAN_PROFILE:
#
#   ml-noheap      Layer 2 of issue #63, over the ML objects: no allocation family, no throwing
#                  runtime. This is the original scan.
#   governed-throw Issue #116, over every MduXCore object: no literal throw. Allocation is NOT
#                  forbidden here - mdux.evidence.json and mdux.governance legitimately allocate.
#
# Run as a ctest via `cmake -P`, not at configure time, because the object files do not exist until
# after a build.
#
# ## ml-noheap
#
# The delete operators are in the forbidden set alongside the new ones deliberately. A reference to
# `operator delete` means the object owns heap memory just as surely as a reference to `operator
# new` does - often more visibly, because a destructor emitted for a std container is what pulls it
# in. So a failure naming a delete symbol means the same thing as one naming new: something in the
# governed ML zone acquired owning memory.
#
# Where layer 1 (tests/ml/NoHeapTests.cpp) proves predict() does not allocate *when run*, this
# proves the kernels and the runtime cannot allocate *at all* - no path through them, taken or not,
# reaches operator new or malloc. It is also far cheaper, and it fails in the PR that introduced
# the reference rather than whenever someone next executes that path.
#
# ## governed-throw
#
# ADR-005 says governed code does not throw. The source lint checks that at the source level; this
# checks it in the emitted objects, which is the stronger of the two - it sees through a std
# facility that throws on a path the source never spells out.
#
# **This profile is only a gate on GCC/Clang, and reports rather than fails on MSVC.** The reason
# is a genuine difference in how the two standard libraries generate code, not a gap in effort.
#
# On libstdc++, a `throw` expression emits `__cxa_throw`, while the library's own throw sites go
# through out-of-line helpers - `std::__throw_length_error`, `std::__throw_logic_error`,
# `std::__throw_out_of_range_fmt`. The two are therefore distinguishable in the object, and this
# profile forbids the first while reporting the second. Eight governed objects reference the
# helpers, all from inside `std::string` and `std::vector`: growth paths reference
# `__throw_length_error`, and `std::string_view::substr` references `__throw_out_of_range_fmt` even
# where the caller's invariant makes the throw unreachable (Json.cpp's parser and Governance.cpp's
# id splitting are both of that shape).
#
# On the MSVC STL those throw sites are **inlined into the caller**, so the same `std::string` use
# emits `_CxxThrowException` directly in the governed object - the identical symbol a hand-written
# `throw` would emit. Nine governed objects reference it, and `mdux-governed-lint` independently
# confirms there is no `throw` in any of their sources. The symbol simply cannot tell the two apart
# there, so forbidding it would fail the build on correct code, and tolerating it would forbid
# nothing at all. It is reported instead, and the source lint - which is toolchain-independent - is
# what enforces the rule on Windows.
#
# Forbidding the helpers on GCC would mean banning `std::string`, `std::vector` and `substr` from
# the governed zone outright. That may be the right end state for a Class C build, but it is a much
# larger decision than this scan, and it needs its own ADR. Until then this file reports those
# references rather than failing on them, so that they stay visible and counted instead of being
# quietly implied not to exist. See ADR-005 for the claim as it is actually made.
#
# Expected variables (passed with -D):
#   MDUX_SCAN_PROFILE       - "ml-noheap" or "governed-throw"
#   MDUX_NOHEAP_OBJECT_LIST - path to a newline-separated file of object paths, written by
#                             file(GENERATE) so that a generator expression resolves per config
#   MDUX_NOHEAP_TOOL        - "nm" or "dumpbin"
#   MDUX_NOHEAP_COMMAND     - path to that tool

if(NOT DEFINED MDUX_SCAN_PROFILE)
    message(FATAL_ERROR
        "MduXNoHeapScan: MDUX_SCAN_PROFILE is not set. Pass 'ml-noheap' or 'governed-throw' - "
        "defaulting would let a caller get the wrong forbidden set without noticing.")
endif()

if(NOT DEFINED MDUX_NOHEAP_OBJECT_LIST OR NOT EXISTS "${MDUX_NOHEAP_OBJECT_LIST}")
    message(FATAL_ERROR
        "MduXNoHeapScan: object list '${MDUX_NOHEAP_OBJECT_LIST}' not found. The scan cannot pass "
        "vacuously.")
endif()

file(STRINGS "${MDUX_NOHEAP_OBJECT_LIST}" MDUX_NOHEAP_OBJECTS)
if(MDUX_NOHEAP_OBJECTS STREQUAL "")
    message(FATAL_ERROR
        "MduXNoHeapScan: the object list is empty. That would make this test pass by checking "
        "nothing - the '${MDUX_SCAN_PROFILE}' profile expects objects to be part of MduXCore.")
endif()

# Mangled and demangled spellings both, so this works whether or not the tool demangles.
if(MDUX_SCAN_PROFILE STREQUAL "ml-noheap")
    set(forbidden_symbols
        "operator new"
        "operator delete"
        "_Znwm"      # operator new(unsigned long)
        "_Znam"      # operator new[](unsigned long)
        "_ZdlPv"     # operator delete(void*)
        "_ZdaPv"     # operator delete[](void*)
        "malloc"
        "calloc"
        "realloc"
        "__cxa_throw"
        "??2@"       # MSVC operator new
        "??_U@"      # MSVC operator new[]
    )
    set(reported_symbols "")
    set(reported_explanation "")
    # One string, not a list of strings: message() concatenates its own arguments cleanly, but a
    # ;-separated list expanded through ${} arrives with the separators embedded in the text.
    string(CONCAT violation_explanation
        "mdux.ml.kernels and mdux.ml.runtime must reach neither. If this is a std facility that "
        "allocates only on a path you believe is unreachable, that is not sufficient - the device "
        "build must not contain the reference at all.")
elseif(MDUX_SCAN_PROFILE STREQUAL "governed-throw")
    # Toolchain-dependent, because the distinction this profile rests on is a property of the
    # standard library's code generation rather than of the source. See the header comment.
    if(MDUX_NOHEAP_TOOL STREQUAL "dumpbin")
        set(forbidden_symbols "")
        set(reported_symbols
            "_CxxThrowException"
            "_Xlength_error"
            "_Xout_of_range"
            "_Xbad_alloc"
            "_Xinvalid_argument"
        )
        string(CONCAT reported_explanation
            "tolerated throw reference(s). The MSVC STL inlines its own throw sites, so "
            "_CxxThrowException in a governed object is indistinguishable from a hand-written "
            "throw - see this file's header. mdux-governed-lint is what enforces the no-throw rule "
            "on this toolchain; this scan is informational here")
    else()
        set(forbidden_symbols
            "__cxa_throw"
            "__cxa_rethrow"
        )
        # Counted and printed, never failed on.
        set(reported_symbols
            "__throw_length_error"
            "__throw_logic_error"
            "__throw_out_of_range"
            "__throw_bad_alloc"
        )
        string(CONCAT reported_explanation
            "tolerated throw-helper reference(s), from std::string, std::vector and "
            "std::string_view::substr internals - see this file's header and ADR-005 for why "
            "these are reported rather than forbidden")
    endif()
    string(CONCAT violation_explanation
        "A governed module contains a throw expression. ADR-005 requires governed code to report "
        "failure through mdux.core.result instead. If the throwing construct is unavoidable, the "
        "module belongs in the host-tools zone - which is where mdux.text.raster went in #116.")
else()
    message(FATAL_ERROR
        "MduXNoHeapScan: unknown MDUX_SCAN_PROFILE '${MDUX_SCAN_PROFILE}'. Expected 'ml-noheap' "
        "or 'governed-throw'.")
endif()

set(violations "")
set(reported "")
set(scanned_count 0)

foreach(object ${MDUX_NOHEAP_OBJECTS})
    if(NOT EXISTS "${object}")
        message(FATAL_ERROR
            "MduXNoHeapScan: object '${object}' does not exist. The scan would otherwise pass by "
            "checking nothing - build the targets before running this test.")
    endif()
    math(EXPR scanned_count "${scanned_count} + 1")

    if(MDUX_NOHEAP_TOOL STREQUAL "dumpbin")
        execute_process(COMMAND "${MDUX_NOHEAP_COMMAND}" /SYMBOLS "${object}"
                        OUTPUT_VARIABLE symbol_output
                        ERROR_VARIABLE symbol_error
                        RESULT_VARIABLE symbol_result)
    else()
        # -u lists only undefined symbols, which is exactly the question being asked: does this
        # object *call out* to an allocator. Defined local symbols are irrelevant.
        execute_process(COMMAND "${MDUX_NOHEAP_COMMAND}" -u -C "${object}"
                        OUTPUT_VARIABLE symbol_output
                        ERROR_VARIABLE symbol_error
                        RESULT_VARIABLE symbol_result)
    endif()

    if(NOT symbol_result EQUAL 0)
        message(FATAL_ERROR
            "MduXNoHeapScan: ${MDUX_NOHEAP_TOOL} failed on '${object}': ${symbol_error}")
    endif()

    get_filename_component(object_name "${object}" NAME)
    string(REPLACE "\n" ";" symbol_lines "${symbol_output}")
    foreach(line ${symbol_lines})
        if(MDUX_NOHEAP_TOOL STREQUAL "dumpbin")
            # Only undefined symbols matter; dumpbin marks them UNDEF.
            if(NOT line MATCHES "UNDEF")
                continue()
            endif()
        endif()
        foreach(symbol ${forbidden_symbols})
            string(FIND "${line}" "${symbol}" found)
            if(NOT found EQUAL -1)
                list(APPEND violations "${object_name}: ${line}")
            endif()
        endforeach()
        foreach(symbol ${reported_symbols})
            string(FIND "${line}" "${symbol}" found)
            if(NOT found EQUAL -1)
                list(APPEND reported "${object_name}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(REMOVE_DUPLICATES violations)
    string(REPLACE ";" "\n  " violation_text "${violations}")
    message(FATAL_ERROR
        "Symbol scan violation (profile '${MDUX_SCAN_PROFILE}'):\n  ${violation_text}\n"
        "${violation_explanation}")
endif()

# Printed rather than failed on, and printed every run rather than only when the count changes:
# a tolerated reference that nobody sees becomes a reference nobody remembers tolerating.
if(reported)
    list(REMOVE_DUPLICATES reported)
    list(LENGTH reported reported_count)
    string(REPLACE ";" "\n  " reported_text "${reported}")
    message(STATUS
        "MduXNoHeapScan: ${reported_count} ${reported_explanation}:\n  ${reported_text}")
endif()

if(MDUX_SCAN_PROFILE STREQUAL "ml-noheap")
    message(STATUS
        "MduXNoHeapScan: ${scanned_count} object(s) free of allocator and throw references")
elseif(forbidden_symbols)
    message(STATUS
        "MduXNoHeapScan: ${scanned_count} governed object(s) contain no throw expression")
else()
    # Never "no throw expression" here - nothing was forbidden, so nothing was established. Saying
    # otherwise would be the exact shape of unearned claim issue #116 exists to remove.
    message(STATUS
        "MduXNoHeapScan: ${scanned_count} governed object(s) scanned; this toolchain cannot "
        "distinguish a governed throw from an inlined std one, so no verdict is claimed here - "
        "mdux-governed-lint enforces the rule at source level")
endif()
