# MduXTrustZones.cmake
#
# Mechanical enforcement of the governed/adapter/tools split from ADR-004: a
# governed target's link graph may reach only other governed targets and must
# never reach Vulkan or a windowing library.
# This is deliberately a link-graph check, not just "don't #include vulkan.h" -
# the point is to catch the dependency arriving transitively through some other
# target just as reliably as directly.
#
# Usage:
#   mdux_declare_governed(MduXCore)     # once, right after the target is created
#   ...
#   mdux_verify_trust_zones()           # once, at the end of the top-level
#                                        # CMakeLists.txt, after every
#                                        # add_subdirectory() so the full link
#                                        # graph is established

define_property(GLOBAL PROPERTY MDUX_GOVERNED_TARGETS
    BRIEF_DOCS "Targets declared governed under ADR-004"
    FULL_DOCS "Targets whose link graph mdux_verify_trust_zones() checks for Vulkan/windowing dependencies. Populated by mdux_declare_governed()."
)

set(_MDUX_FORBIDDEN_TARGET_PATTERNS
    "^Vulkan::"
    "^glfw$"
    "^glfw3$"
)

# NOTE: none of the functions below name a parameter `TARGET` - CMake's if()
# command special-cases the bare word TARGET as the start of its `if(TARGET
# <name>)` existence-check keyword form, regardless of whether it's meant as a
# literal or a variable reference. A parameter literally named TARGET makes
# every unquoted, un-dereferenced use of it silently misparse (confirmed
# empirically: `if(TARGET IN_LIST visited)` fails with "Unknown arguments
# specified" because CMake reads it as "does a target named IN_LIST exist").
# TGT throughout sidesteps the whole class of mistake.

function(mdux_declare_governed TGT)
    if(NOT TARGET ${TGT})
        message(FATAL_ERROR "mdux_declare_governed: '${TGT}' is not a target")
    endif()
    set_property(GLOBAL APPEND PROPERTY MDUX_GOVERNED_TARGETS ${TGT})
endfunction()

# Recursively walks TGT's link interface looking for a forbidden dependency.
# ROOT is the originally-declared-governed target, threaded through for the
# error message; VISITED_VAR names a variable (in the caller's scope) holding
# the list of targets already visited, guarding against a cyclic or
# diamond-shaped link graph.
function(_mdux_check_link_graph TGT ROOT VISITED_VAR)
    set(visited "${${VISITED_VAR}}")
    if("${TGT}" IN_LIST visited)
        return()
    endif()
    list(APPEND visited "${TGT}")
    set(${VISITED_VAR} "${visited}" PARENT_SCOPE)

    foreach(pattern ${_MDUX_FORBIDDEN_TARGET_PATTERNS})
        if("${TGT}" MATCHES "${pattern}")
            message(FATAL_ERROR
                "Trust zone violation (ADR-004): governed target '${ROOT}' "
                "reaches forbidden dependency '${TGT}'. Governed targets "
                "must not link Vulkan or a windowing library, directly or "
                "transitively.")
        endif()
    endforeach()

    if(NOT TARGET ${TGT})
        # A plain library name/path (not a CMake target) - nothing further to walk.
        return()
    endif()

    # Resolve aliases before consulting the governed set. Dependencies may use a
    # namespaced alias while mdux_declare_governed() records its real target.
    get_target_property(aliased_target ${TGT} ALIASED_TARGET)
    if(aliased_target)
        set(canonical_target "${aliased_target}")
    else()
        set(canonical_target "${TGT}")
    endif()

    get_property(governed GLOBAL PROPERTY MDUX_GOVERNED_TARGETS)
    if(NOT "${canonical_target}" IN_LIST governed)
        message(FATAL_ERROR
            "Trust zone violation (ADR-004): governed target '${ROOT}' "
            "reaches non-governed target '${TGT}'. Declare a genuinely "
            "std-only target with mdux_declare_governed(), or keep it out "
            "of the governed link graph.")
    endif()

    foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(deps ${TGT} ${prop})
        if(deps)
            foreach(dep ${deps})
                # Strip generator-expression wrappers like $<LINK_ONLY:...> down to
                # the bare target name; this check runs at configure time so it
                # cannot evaluate config-dependent genexes, but the common cases
                # (plain names, $<LINK_ONLY:name>, $<BUILD_INTERFACE:name>) are
                # exactly the forms this project's own CMakeLists.txt files use.
                string(REGEX REPLACE "^\\$<[A-Z_]+:(.*)>$" "\\1" dep_name "${dep}")
                if("${dep}" MATCHES "^\\$<" AND dep_name STREQUAL "${dep}")
                    message(FATAL_ERROR
                        "Trust zone verification cannot prove generator expression "
                        "'${dep}' on governed target '${ROOT}' is clean. Use a plain "
                        "target, $<LINK_ONLY:target>, or $<BUILD_INTERFACE:target>.")
                endif()
                if(NOT dep_name STREQUAL "")
                    set(_visited_copy "${${VISITED_VAR}}")
                    _mdux_check_link_graph("${dep_name}" "${ROOT}" _visited_copy)
                    set(${VISITED_VAR} "${_visited_copy}" PARENT_SCOPE)
                endif()
            endforeach()
        endif()
    endforeach()
endfunction()

function(mdux_verify_trust_zones)
    get_property(governed GLOBAL PROPERTY MDUX_GOVERNED_TARGETS)
    if(NOT governed)
        message(WARNING "mdux_verify_trust_zones: no targets declared governed - "
                         "nothing to check. Did mdux_declare_governed() run?")
        return()
    endif()
    foreach(governed_target ${governed})
        set(visited "")
        _mdux_check_link_graph("${governed_target}" "${governed_target}" visited)
    endforeach()
    list(LENGTH governed count)
    message(STATUS "mdux_verify_trust_zones: ${count} governed target(s) clean of Vulkan/windowing dependencies")
endfunction()
