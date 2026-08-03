# MduXDeterminism.cmake
#
# Mechanical enforcement of ADR-008's floating-point determinism rule: the ML kernels must produce
# bit-identical results on every supported toolchain, because that identity is what makes a
# golden-vector mismatch on device mean "the FPU or the toolchain differs" rather than "one of two
# implementations drifted".
#
# Correct kernel source is necessary but not sufficient in C++. Three hazards, and the guard for
# each:
#
#   1. FP contraction. GCC and Clang default to -ffp-contract=fast outside strict modes and WILL
#      fuse `acc += w * x` into an FMA, which rounds once where the scalar sequence rounds twice.
#      mdux_enforce_fp_determinism() turns it off explicitly.
#
#   2. -ffast-math arriving from somewhere nobody was looking - a preset, a toolchain file, a
#      dependency's interface compile options. mdux_verify_fp_determinism() inspects the effective
#      options of every enrolled target, transitively, and fails the configure step. This is the
#      guard that catches the change nobody thought was related to ML.
#
#   3. x87 excess precision, which only bites 32-bit x86: the FPU computes at 80 bits and rounds
#      unpredictably on spill. Windows is 64-bit-only here, so this is a Linux-only check.
#
# Usage:
#   mdux_enforce_fp_determinism(MduXCore)   # applies the flags AND enrolls the target
#   ...
#   mdux_verify_fp_determinism()            # once, at the end of the top-level CMakeLists.txt
#
# See cmake/MduXTrustZones.cmake for the naming note on why no parameter here is called TARGET.

define_property(GLOBAL PROPERTY MDUX_DETERMINISTIC_TARGETS
    BRIEF_DOCS "Targets whose floating-point behaviour ADR-008 constrains"
    FULL_DOCS "Targets that mdux_verify_fp_determinism() scans for fast-math compile options. Populated by mdux_enforce_fp_determinism()."
)

# Options that break bit-reproducibility, in the spellings each compiler accepts. -ffp-contract=fast
# is included beyond the four ADR-008 names because it re-enables precisely the fusion decision 3
# forbids, and it is the one most likely to arrive from a well-meaning performance change.
set(_MDUX_FORBIDDEN_FP_OPTIONS
    "-ffast-math"
    "-Ofast"
    "-funsafe-math-optimizations"
    "-fassociative-math"
    "-freciprocal-math"
    "-ffinite-math-only"
    "-ffp-contract=fast"
    "/fp:fast"
    "-fp:fast"
    # /fp:contract is MSVC's opt-in for fusing into FMA. Issue #59's text asks for it; ADR-008
    # decision 3 forbids exactly what it does. It is listed here rather than applied, and it
    # carries more weight on MSVC than the others because nothing is passed there - see
    # mdux_enforce_fp_determinism().
    "/fp:contract"
    "-fp:contract"
)

function(mdux_enforce_fp_determinism TGT)
    if(NOT TARGET ${TGT})
        message(FATAL_ERROR "mdux_enforce_fp_determinism: '${TGT}' is not a target")
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # Nothing is added on MSVC, and that is the correct answer rather than a gap.
        #
        # /fp:precise is already MSVC's default, and under it VS2022 does not contract into FMA -
        # /fp:contract exists precisely as the opt-in. So the behaviour ADR-008 decision 3 requires
        # is what an unadorned MSVC build already does.
        #
        # Passing /fp:precise *explicitly* is not merely redundant, it breaks the build: cl defines
        # _M_FP_PRECISE when the flag is given on the command line, the prebuilt `std` module is
        # compiled without it, and importing std from a translation unit whose command line
        # disagrees raises C5050 ("Possible incompatible environment while importing module 'std'").
        # Warnings are errors here, so every governed module that imports std fails to compile.
        # This was observed in CI, not theorised - see the note in issue #59.
        #
        # Note also that issue #59's text asks for "/fp:precise /fp:contract". /fp:contract would
        # *enable* the fusion decision 3 forbids, so it is in the forbidden list below rather than
        # applied here.
        #
        # Enforcement on MSVC is therefore entirely mdux_verify_fp_determinism()'s job: it rejects
        # /fp:fast and /fp:contract if either ever arrives.
    else()
        target_compile_options(${TGT} PRIVATE
            -ffp-contract=off
            -fno-fast-math
            # "No heap" must not quietly become "enormous stack", which on a device is the same bug
            # wearing a different hat. Re-checked by issue #63; the flag belongs with the other
            # determinism flags.
            -Wframe-larger-than=4096
        )

        # x87 excess precision, hazard 3. Only 32-bit x86 has the 80-bit-register problem; on
        # x86-64, SSE2 is architecturally guaranteed and f32 arithmetic is already IEEE single.
        if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "(i.86|x86)")
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${TGT} PRIVATE -msse2 -mfpmath=sse)
            else()
                message(FATAL_ERROR
                    "ADR-008: 32-bit x86 target with an unrecognised compiler cannot be shown to "
                    "use SSE2 math. x87 computes at 80 bits and rounds unpredictably on spill, so "
                    "bit-reproducibility cannot be claimed. Build 64-bit, or add the SSE2 flags "
                    "for this compiler here.")
            endif()
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY MDUX_DETERMINISTIC_TARGETS ${TGT})
endfunction()

# Reports any forbidden option in `options`, attributing it to `origin` for the error message.
function(_mdux_scan_fp_options options origin root)
    foreach(option ${options})
        foreach(forbidden ${_MDUX_FORBIDDEN_FP_OPTIONS})
            # Substring, not equality. An earlier version compared the whole option against each
            # forbidden spelling after stripping one layer of generator expression, which missed
            # every form where the flag is not the entire token: `SHELL:/fp:fast /something`,
            # `$<$<COMPILE_LANGUAGE:CXX>:-ffast-math>`, a nested genex, or two flags in one quoted
            # argument. Those are precisely the "it arrived transitively from somewhere nobody was
            # looking" cases this guard exists for, so a false negative there defeats the point.
            #
            # No false positive from -fno-fast-math: it does not contain the string "-ffast-math".
            string(FIND "${option}" "${forbidden}" _mdux_found)
            if(NOT _mdux_found EQUAL -1)
                message(FATAL_ERROR
                    "Floating-point determinism violation (ADR-008): '${forbidden}' reaches "
                    "target '${root}' via ${origin}. Fast-math transformations reorder and fuse "
                    "the accumulations mdux.ml.kernels specifies exactly, which would silently "
                    "break golden-vector reproducibility across toolchains. Remove the option, or "
                    "keep the target it comes from out of the governed link graph.")
            endif()
        endforeach()
    endforeach()
endfunction()

function(_mdux_check_fp_graph TGT ROOT VISITED_VAR)
    set(visited "${${VISITED_VAR}}")
    if("${TGT}" IN_LIST visited)
        return()
    endif()
    list(APPEND visited "${TGT}")
    set(${VISITED_VAR} "${visited}" PARENT_SCOPE)

    if(NOT TARGET ${TGT})
        return()
    endif()

    get_target_property(aliased ${TGT} ALIASED_TARGET)
    if(aliased)
        set(canonical "${aliased}")
    else()
        set(canonical "${TGT}")
    endif()

    foreach(prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
        get_target_property(options ${canonical} ${prop})
        if(options)
            _mdux_scan_fp_options("${options}" "${canonical}'s ${prop}" "${ROOT}")
        endif()
    endforeach()

    foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(deps ${canonical} ${prop})
        if(deps)
            foreach(dep ${deps})
                string(REGEX REPLACE "^\\$<[A-Z_]+:(.*)>$" "\\1" dep_name "${dep}")
                if(NOT dep_name STREQUAL "" AND NOT dep_name MATCHES "^\\$<")
                    set(_visited_copy "${${VISITED_VAR}}")
                    _mdux_check_fp_graph("${dep_name}" "${ROOT}" _visited_copy)
                    set(${VISITED_VAR} "${_visited_copy}" PARENT_SCOPE)
                endif()
            endforeach()
        endif()
    endforeach()
endfunction()

function(mdux_verify_fp_determinism)
    get_property(deterministic GLOBAL PROPERTY MDUX_DETERMINISTIC_TARGETS)
    if(NOT deterministic)
        message(WARNING "mdux_verify_fp_determinism: no targets enrolled - nothing to check. "
                        "Did mdux_enforce_fp_determinism() run?")
        return()
    endif()

    # The global flag variables are checked too. A preset or a toolchain file that appends
    # -ffast-math to CMAKE_CXX_FLAGS never touches a target property, so a target-only scan would
    # miss the single most likely way this arrives.
    set(global_flag_vars CMAKE_CXX_FLAGS)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        list(APPEND global_flag_vars CMAKE_CXX_FLAGS_${config})
    endforeach()
    foreach(flag_var ${global_flag_vars})
        if(DEFINED ${flag_var} AND NOT "${${flag_var}}" STREQUAL "")
            separate_arguments(flag_list NATIVE_COMMAND "${${flag_var}}")
            _mdux_scan_fp_options("${flag_list}" "${flag_var}" "(global)")
        endif()
    endforeach()

    # Directory-scoped options apply to every target defined here, targets included.
    get_directory_property(dir_options COMPILE_OPTIONS)
    if(dir_options)
        _mdux_scan_fp_options("${dir_options}" "the top-level directory's COMPILE_OPTIONS" "(directory)")
    endif()

    foreach(enrolled ${deterministic})
        set(visited "")
        _mdux_check_fp_graph("${enrolled}" "${enrolled}" visited)
    endforeach()

    list(LENGTH deterministic count)
    message(STATUS "mdux_verify_fp_determinism: ${count} target(s) free of fast-math options")
endfunction()
