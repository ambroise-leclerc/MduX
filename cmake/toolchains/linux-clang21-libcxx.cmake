# Candidate Linux toolchain for issue #246.
#
# The counterpart to cmake/toolchains/macos-arm64-llvm.cmake. That file is what made `import std`
# resolve for Clang on macOS, and the part that made it work is not macOS-specific: CMake needs to be
# pointed at libc++'s own `libc++.modules.json`, and the compile must actually select libc++.
#
# #246 established that this works: libc++-21-dev ships its manifest and `import std` resolves, so
# the leg now runs on every push. The FATAL_ERROR messages below stay verbose anyway - they are what
# a contributor on a different LLVM packaging will read, and each names what was missing rather than
# failing later as an unresolved `std` module.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The linux-clang21-libcxx toolchain is only valid on Linux hosts.")
endif()

set(_mdux_llvm_root "$ENV{MDUX_LLVM_ROOT}")
if(NOT _mdux_llvm_root)
    # apt.llvm.org installs versioned roots here; this is the layout clang-build.yml provisions.
    set(_mdux_llvm_root "/usr/lib/llvm-21")
endif()
if(NOT IS_DIRECTORY "${_mdux_llvm_root}")
    message(FATAL_ERROR
        "No LLVM 21 root at '${_mdux_llvm_root}'. Set MDUX_LLVM_ROOT, or install the packages "
        "clang-build.yml installs: clang-21, libc++-21-dev, libc++abi-21-dev.")
endif()

set(CMAKE_C_COMPILER "${_mdux_llvm_root}/bin/clang" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_mdux_llvm_root}/bin/clang++" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_mdux_llvm_root}/bin/llvm-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_mdux_llvm_root}/bin/llvm-ranlib" CACHE FILEPATH "" FORCE)

# libc++ rather than libstdc++, for the same reason the macOS lane uses it: the `std` module is
# shipped by the standard library, and only libc++ ships one this project has been built against.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

# Where Debian/Ubuntu put `libc++.modules.json` is not fixed across LLVM packagings, so search the
# layouts that exist rather than asserting one. A wrong guess here would fail later and less
# legibly, as a missing `std` module rather than a missing file.
if(NOT DEFINED CMAKE_CXX_STDLIB_MODULES_JSON)
    file(GLOB_RECURSE _mdux_libcxx_modules_json
        "${_mdux_llvm_root}/lib/*/libc++.modules.json"
        "${_mdux_llvm_root}/lib/libc++.modules.json"
        "${_mdux_llvm_root}/share/libc++/*/libc++.modules.json")
    list(LENGTH _mdux_libcxx_modules_json _mdux_libcxx_modules_json_count)
    if(_mdux_libcxx_modules_json_count EQUAL 0)
        message(FATAL_ERROR
            "No libc++.modules.json under '${_mdux_llvm_root}'. This is the #246 experiment's "
            "answer, not a configuration mistake: this LLVM packaging does not ship the std module "
            "manifest, so `import std` cannot resolve for libc++ here. Record it on the issue.")
    endif()

    # `file(GLOB_RECURSE)` does not guarantee an order, so taking element 0 of the raw result would
    # let the chosen manifest depend on directory iteration order - and this toolchain feeds a
    # pipeline whose entire claim is byte-identical output across machines. Sort first, so the same
    # installation always selects the same manifest.
    list(SORT _mdux_libcxx_modules_json)
    list(GET _mdux_libcxx_modules_json 0 _mdux_libcxx_modules_json_first)
    if(_mdux_libcxx_modules_json_count GREATER 1)
        # More than one packaging is installed under the same root. Which one is right is a judgement
        # this file should not make silently, so name them all and let the operator pin it.
        message(WARNING
            "Multiple libc++.modules.json under '${_mdux_llvm_root}': "
            "${_mdux_libcxx_modules_json}. Using '${_mdux_libcxx_modules_json_first}'. Set "
            "CMAKE_CXX_STDLIB_MODULES_JSON explicitly to choose a different one.")
    endif()
    set(CMAKE_CXX_STDLIB_MODULES_JSON "${_mdux_libcxx_modules_json_first}"
        CACHE FILEPATH "" FORCE)
endif()

foreach(_mdux_required_tool
        "${CMAKE_CXX_COMPILER}"
        "${CMAKE_AR}"
        "${CMAKE_RANLIB}"
        "${CMAKE_CXX_STDLIB_MODULES_JSON}")
    if(NOT EXISTS "${_mdux_required_tool}")
        message(FATAL_ERROR "Required Linux Clang toolchain input is missing: ${_mdux_required_tool}")
    endif()
endforeach()
