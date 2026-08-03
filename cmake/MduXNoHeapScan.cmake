# MduXNoHeapScan.cmake
#
# Layer 2 of issue #63: scan the compiled ML objects for any undefined reference to the allocation
# and deallocation family, or to the exception-throwing runtime.
#
# The delete operators are in the forbidden set alongside the new ones deliberately. A reference to
# `operator delete` means the object owns heap memory just as surely as a reference to `operator
# new` does - often more visibly, because a destructor emitted for a std container is what pulls it
# in. So a failure naming a delete symbol means the same thing as one naming new: something in the
# governed ML zone acquired owning memory. Run as a ctest via `cmake -P`, not at configure time, because
# the object files do not exist until after a build.
#
# Where layer 1 (tests/ml/NoHeapTests.cpp) proves predict() does not allocate *when run*, this
# proves the kernels and the runtime cannot allocate *at all* - no path through them, taken or not,
# reaches operator new or malloc. It is also far cheaper, and it fails in the PR that introduced
# the reference rather than whenever someone next executes that path.
#
# __cxa_throw is in the forbidden set for a second reason: it double-checks ADR-005's no-throwing
# rule for the governed zone. A std::vector::at() or a std::stoi() that crept in would show up here
# as a throw reference even if it never allocated.
#
# Expected variables (passed with -D):
#   MDUX_NOHEAP_OBJECT_LIST - path to a newline-separated file of object paths, written by
#                             file(GENERATE) so that a generator expression resolves per config
#   MDUX_NOHEAP_TOOL        - "nm" or "dumpbin"
#   MDUX_NOHEAP_COMMAND     - path to that tool

if(NOT DEFINED MDUX_NOHEAP_OBJECT_LIST OR NOT EXISTS "${MDUX_NOHEAP_OBJECT_LIST}")
    message(FATAL_ERROR
        "MduXNoHeapScan: object list '${MDUX_NOHEAP_OBJECT_LIST}' not found. The scan cannot pass "
        "vacuously.")
endif()

file(STRINGS "${MDUX_NOHEAP_OBJECT_LIST}" MDUX_NOHEAP_OBJECTS)
if(MDUX_NOHEAP_OBJECTS STREQUAL "")
    message(FATAL_ERROR
        "MduXNoHeapScan: the object list is empty. That would make this test pass by checking "
        "nothing - the ML sources are expected to be part of MduXCore.")
endif()

# Mangled and demangled spellings both, so this works whether or not the tool demangles.
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

set(violations "")
set(scanned_count 0)

foreach(object ${MDUX_NOHEAP_OBJECTS})
    if(NOT EXISTS "${object}")
        message(FATAL_ERROR
            "MduXNoHeapScan: object '${object}' does not exist. The scan would otherwise pass by "
            "checking nothing - build the ML targets before running this test.")
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
    endforeach()
endforeach()

if(violations)
    list(REMOVE_DUPLICATES violations)
    string(REPLACE ";" "\n  " violation_text "${violations}")
    message(FATAL_ERROR
        "No-heap violation (ADR-008, issue #63): the ML objects reference an allocator or the "
        "throwing runtime.\n  ${violation_text}\n"
        "mdux.ml.kernels and mdux.ml.runtime must reach neither. If this is a std facility that "
        "allocates only on a path you believe is unreachable, that is not sufficient - the device "
        "build must not contain the reference at all.")
endif()

message(STATUS "MduXNoHeapScan: ${scanned_count} object(s) free of allocator and throw references")
