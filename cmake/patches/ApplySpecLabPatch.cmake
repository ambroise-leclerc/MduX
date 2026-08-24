if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "ApplySpecLabPatch: SOURCE_DIR is missing or invalid")
endif()
if(NOT DEFINED PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "ApplySpecLabPatch: PATCH_FILE is missing or invalid")
endif()

# Probed before the apply attempts below. execute_process() reports a failure to *launch* the
# command by setting RESULT_VARIABLE to an error string rather than a number, which `EQUAL 0` then
# reads as simply "did not apply" - so a missing git would be reported as upstream drift and send
# the reader after the wrong thing entirely.
find_program(MDUX_GIT_EXECUTABLE NAMES git)
if(NOT MDUX_GIT_EXECUTABLE)
    message(FATAL_ERROR
        "ApplySpecLabPatch: git was not found on PATH, and it is what applies the SpecLab "
        "compatibility patch. Install git or put it on PATH.")
endif()

execute_process(
    COMMAND "${MDUX_GIT_EXECUTABLE}" apply --check --ignore-space-change "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_QUIET ERROR_QUIET
)
if(can_apply EQUAL 0)
    execute_process(
        COMMAND "${MDUX_GIT_EXECUTABLE}" apply --ignore-space-change "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    return()
endif()

# FetchContent may rerun PATCH_COMMAND without resetting an already-patched checkout. Accept only
# the exact reverse-applicable state; any third state is upstream drift or a partial patch.
execute_process(
    COMMAND "${MDUX_GIT_EXECUTABLE}" apply --reverse --check --ignore-space-change "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR "SpecLab compatibility patch neither applies nor is already applied")
endif()
