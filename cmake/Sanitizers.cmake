# Coverage stays on the options target it is given. Sanitizers do not, and the reason is #360.
#
# The sanitizer flags used to live on that target too - MduX_options - which reaches a translation
# unit only through the MduX adapter target. MduXCore deliberately never links MduX_options, because
# that target carries Vulkan::Vulkan, so the governed code, the host bake/tool libraries and every
# test linking only MduX::Core were compiled without -fsanitize: 0 of MduXCore's 46 translation
# units were instrumented in the ASan+UBSan job. The SpecLab-based suites were instrumented only
# because SpecLab happened to pass its own flags to its importers.
#
# So a sanitizer build now instruments everything it compiles, through CMAKE_CXX_FLAGS (and
# CMAKE_C_FLAGS, for C sources a dependency may bring) rather than a target: every MduX target,
# the fetched SpecLab and cpp-httplib sources, GLFW when it is built from source, and CMake's
# internal std-module target (which add_compile_options() would not reach - see the /utf-8 note
# in the root CMakeLists.txt). There is no carrier to forget to link, so a target added later
# cannot fall out of coverage again, and every importer of a named module - std included - is
# instrumented exactly as that module was. That consistency is not cosmetic: GCC 16 crashes in
# expand_UBSAN_NULL when a module interface and its importer disagree about instrumentation (#356).
#
# CMAKE_CXX_FLAGS also appears on every link line CMake writes for C++, so the sanitizer runtimes
# are linked into this build's own executables without a separate linker-flag variable. It does
# not leave the build, though, so MDUX_SANITIZER_LINK_OPTION is also returned for the root
# CMakeLists.txt to attach to MduXCore: an application linking an instrumented MduX, installed or
# through add_subdirectory(), needs the sanitizer runtime or it fails on unresolved __asan_* and
# __ubsan_* references. MduX_options used to carry that to consumers; MduXCore now does.
#
# CMAKE_CXX_FLAGS is set in the caller's scope, which must be the root directory and must precede
# every add_subdirectory(): a subdirectory takes the value the variable has when it is added.
# cmake/CheckSanitizerCoverage.cmake verifies the result against compile_commands.json, as the
# `sanitizer-coverage` CTest label; InstallTreeConsumer checks the exported link option.
function(enable_sanitizers project_name)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" OFF)

    if(ENABLE_COVERAGE)
      target_compile_options(${project_name} INTERFACE --coverage -O0 -g)
      target_link_libraries(${project_name} INTERFACE --coverage)
    endif()

    set(SANITIZERS "")

    option(ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    if(ENABLE_SANITIZER_ADDRESS)
      list(APPEND SANITIZERS "address")
    endif()

    option(ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    if(ENABLE_SANITIZER_LEAK)
      list(APPEND SANITIZERS "leak")
    endif()

    option(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR "Enable undefined behavior sanitizer" OFF)
    if(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR)
      list(APPEND SANITIZERS "undefined")
    endif()

    option(ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    if(ENABLE_SANITIZER_THREAD)
      if("address" IN_LIST SANITIZERS OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Thread sanitizer does not work with Address and Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "thread")
      endif()
    endif()

    option(ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    if(ENABLE_SANITIZER_MEMORY AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      message(WARNING "Memory sanitizer requires all the code (including libc++) to be MSan-instrumented otherwise it reports false positives")
      if("address" IN_LIST SANITIZERS
         OR "thread" IN_LIST SANITIZERS
         OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Memory sanitizer does not work with Address, Thread and Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "memory")
      endif()
    endif()

    list(
      JOIN
      SANITIZERS
      ","
      LIST_OF_SANITIZERS)

  endif()

  if(LIST_OF_SANITIZERS)
    if(NOT
       "${LIST_OF_SANITIZERS}"
       STREQUAL
       "")
      # -fno-sanitize-recover: UndefinedBehaviorSanitizer's default is to print and carry on, which
      # in CI means a job that reports the violation and then passes. Nobody reads a green log, so
      # a recoverable sanitizer finding is a finding nobody sees. Aborting turns it into a failure.
      set(MDUX_SANITIZER_FLAGS "-fsanitize=${LIST_OF_SANITIZERS} -fno-sanitize-recover=all")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MDUX_SANITIZER_FLAGS}" PARENT_SCOPE)
      # C too, although MduX itself is C++ only: a dependency built from source can bring C
      # translation units - GLFW, fetched by examples/CMakeLists.txt when no system package is
      # found. "Everything it compiles" has to include those, and the coverage check reads every
      # entry of compile_commands.json whatever its language.
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MDUX_SANITIZER_FLAGS}" PARENT_SCOPE)
      set(MDUX_SANITIZER_FLAGS "${MDUX_SANITIZER_FLAGS}" PARENT_SCOPE)
      # What a *consumer* must link with, which CMAKE_CXX_FLAGS cannot carry: it reaches neither an
      # installed package nor the parent of a project that add_subdirectory()s MduX. The root
      # CMakeLists.txt puts it on MduXCore as an INTERFACE link option, exported with the target.
      set(MDUX_SANITIZER_LINK_OPTION "-fsanitize=${LIST_OF_SANITIZERS}" PARENT_SCOPE)
      message(STATUS "Sanitizers: ${MDUX_SANITIZER_FLAGS} on every C and C++ translation unit")
    endif()
  endif()

endfunction()
