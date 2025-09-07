# Usage of printLibrariesStatus function:
# This function is used to print the status of the libraries used in your project.
# You need to pass a list of library names as an argument.
# Example:
#   set(MY_LIBRARIES "lib1;lib2;lib3")
#   printLibrariesStatus(${MY_LIBRARIES})
function(printLibrariesStatus library_names)
  message("┌─${PROJECT_NAME}─${PROJECT_VERSION}───Dependencies──────")
  foreach(library_name ${library_names})
    string(LENGTH ${library_name} name_length)
    math(EXPR padding_length "30 - ${name_length}")
    string(RANDOM LENGTH ${padding_length} ALPHABET "." padding)
    if(${library_name}_FOUND OR ${library_name}_ADDED)
      message("│${library_name}${padding}✅  ${${library_name}_VERSION}")
    else()
      message("│${library_name}${padding}❌  NOT FOUND")
    endif()
  endforeach()
  message("└────────────────────────────────────────")
endfunction()


# Usage of printAllVariablesTerminatedBy function:
# This function is used to print all the CMake variables that end with a specific string.
# You need to pass the termination string as an argument.
# Example:
#   printAllVariablesTerminatedBy("_FOUND")
# This will print all the CMake variables that end with "_FOUND".
function(printAllVariablesTerminatedBy termination)
  get_cmake_property(_variableNames VARIABLES)
  foreach (_variableName ${_variableNames})
      string(REGEX MATCH "${termination}$" _isFoundVariable ${_variableName})
      if (_isFoundVariable)
          message(STATUS "${_variableName}=${${_variableName}}")
      endif()
  endforeach()
endfunction()