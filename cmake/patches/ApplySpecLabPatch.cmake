if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "ApplySpecLabPatch: SOURCE_DIR is missing or invalid")
endif()
if(NOT DEFINED PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "ApplySpecLabPatch: PATCH_FILE is missing or invalid")
endif()

execute_process(
    COMMAND git apply --check --ignore-space-change "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_QUIET ERROR_QUIET
)
if(can_apply EQUAL 0)
    execute_process(
        COMMAND git apply --ignore-space-change "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    return()
endif()

# FetchContent may rerun PATCH_COMMAND without resetting an already-patched checkout. Accept only
# the exact reverse-applicable state; any third state is upstream drift or a partial patch.
execute_process(
    COMMAND git apply --reverse --check --ignore-space-change "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR "SpecLab compatibility patch neither applies nor is already applied")
endif()
