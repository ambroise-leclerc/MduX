
# Display the dependency summary using auto-detection of version and source


# Function to format dependency line with proper alignment
function(format_dependency_line NAME VERSION SOURCE OUTPUT_VAR)
    set(TOTAL_WIDTH 56)  # Total width inside the box
    set(PREFIX "║  • ")
    set(SUFFIX " ║")
    
    # Handle special case for Threads (no parentheses)
    if(SOURCE STREQUAL "")
        set(VERSION_INFO "${VERSION}")
    else()
        set(VERSION_INFO "${VERSION} (${SOURCE})")
    endif()
    
    # Calculate dots needed for alignment
    string(LENGTH "${PREFIX}${NAME}" PREFIX_LEN)
    string(LENGTH "${VERSION_INFO}${SUFFIX}" SUFFIX_LEN)
    math(EXPR DOTS_NEEDED "${TOTAL_WIDTH} - ${PREFIX_LEN} - ${SUFFIX_LEN}")
    
    # Ensure minimum dots
    if(DOTS_NEEDED LESS 3)
        set(DOTS_NEEDED 3)
    endif()
    
    # Create dots string
    set(DOTS "")
    foreach(i RANGE 1 ${DOTS_NEEDED})
        string(APPEND DOTS ".")
    endforeach()
    
    set(${OUTPUT_VAR} "${PREFIX}${NAME} ${DOTS} ${VERSION_INFO}${SUFFIX}" PARENT_SCOPE)
endfunction()

# Function to display dependency summary table
# Usage: display_dependency_summary(HEADER_TEXT PREFIX1 PREFIX2 ...)
# Automatically detects system vs CPM versions using PREFIX_VERSION or CPM_PACKAGE_PREFIX_VERSION
function(display_dependency_summary HEADER_TEXT)
    message(STATUS "")
    message(STATUS "╔══════════════════════════════════════════════════╗")
    
    # Center the header text
    set(BOX_WIDTH 50)  # Total width inside the box
    string(LENGTH "${HEADER_TEXT}" HEADER_LEN)
    math(EXPR PADDING_TOTAL "${BOX_WIDTH} - ${HEADER_LEN}")
    math(EXPR PADDING_LEFT "${PADDING_TOTAL} / 2")
    math(EXPR PADDING_RIGHT "${PADDING_TOTAL} - ${PADDING_LEFT}")
    
    # Create padding strings
    set(LEFT_PADDING "")
    set(RIGHT_PADDING "")
    foreach(i RANGE 1 ${PADDING_LEFT})
        string(APPEND LEFT_PADDING " ")
    endforeach()
    foreach(i RANGE 1 ${PADDING_RIGHT})
        string(APPEND RIGHT_PADDING " ")
    endforeach()
    
    message(STATUS "║${LEFT_PADDING}${HEADER_TEXT}${RIGHT_PADDING}║")
    message(STATUS "╠══════════════════════════════════════════════════╣")
    
    # Process each prefix argument to auto-detect version and source
    set(DEPENDENCY_LIST "")
    
    foreach(PREFIX ${ARGN})
        # Check for system version first (PREFIX_VERSION)
        if(DEFINED ${PREFIX}_VERSION AND NOT "${${PREFIX}_VERSION}" STREQUAL "")
            set(DEP_VERSION "${${PREFIX}_VERSION}")
            set(DEP_SOURCE "system")
        # Check for CPM version (CPM_PACKAGE_PREFIX_VERSION)
        elseif(DEFINED CPM_PACKAGE_${PREFIX}_VERSION AND NOT "${CPM_PACKAGE_${PREFIX}_VERSION}" STREQUAL "")
            set(DEP_VERSION "${CPM_PACKAGE_${PREFIX}_VERSION}")
            set(DEP_SOURCE "CPM")
        # Check for lowercase prefix version
        elseif(DEFINED ${PREFIX}_version AND NOT "${${PREFIX}_version}" STREQUAL "")
            set(DEP_VERSION "${${PREFIX}_version}")
            set(DEP_SOURCE "CPM")
        # Check if the package was added via CPM (PREFIX_ADDED)
        elseif(DEFINED ${PREFIX}_ADDED)
            if(DEFINED ${PREFIX}_VERSION AND NOT "${${PREFIX}_VERSION}" STREQUAL "")
                set(DEP_VERSION "${${PREFIX}_VERSION}")
            else()
                set(DEP_VERSION "unknown")
            endif()
            set(DEP_SOURCE "CPM")
        # Special handling for common packages
        elseif(PREFIX STREQUAL "Threads")
            set(DEP_VERSION "system")
            set(DEP_SOURCE "system")
        # Special handling for asio
        elseif(PREFIX STREQUAL "asio")
            # asio is always added via CPM with a specific version
            set(DEP_VERSION "asio-1-34-2")
            set(DEP_SOURCE "CPM")
        else()
            # Package not found or no version available
            set(DEP_VERSION "not found")
            set(DEP_SOURCE "unknown")
        endif()
        
        # Format and add to list
        format_dependency_line("${PREFIX}" "${DEP_VERSION}" "${DEP_SOURCE}" FORMATTED_LINE)
        list(APPEND DEPENDENCY_LIST "${FORMATTED_LINE}")
    endforeach()
    
    # Print the list
    foreach(DEPENDENCY ${DEPENDENCY_LIST})
        message(STATUS "${DEPENDENCY}")
    endforeach()
    
    message(STATUS "╚══════════════════════════════════════════════════╝")
    message(STATUS "")
endfunction()
