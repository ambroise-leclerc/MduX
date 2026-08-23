# Supported macOS toolchain for issue #222.
#
# MduX deliberately supports one reproducible macOS configuration: Apple Silicon,
# upstream LLVM/Clang 21.1.8, libc++, Ninja, and CMake 4.3.1. AppleClang and
# Homebrew GCC do not provide the same C++23 named-module surface.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "The macos-arm64-llvm toolchain is only valid on macOS hosts.")
endif()
if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR
        "MduX's verified macOS target is Apple Silicon (arm64); host architecture is "
        "${CMAKE_HOST_SYSTEM_PROCESSOR}.")
endif()
if(NOT CMAKE_VERSION VERSION_EQUAL "4.3.1")
    message(FATAL_ERROR
        "The verified macOS toolchain requires CMake 4.3.1 exactly; found ${CMAKE_VERSION}.")
endif()

set(_mdux_llvm_root "$ENV{MDUX_LLVM_ROOT}")
if(NOT _mdux_llvm_root)
    find_program(_mdux_brew brew PATHS /opt/homebrew/bin NO_DEFAULT_PATH)
    if(_mdux_brew)
        execute_process(
            COMMAND "${_mdux_brew}" --prefix llvm
            RESULT_VARIABLE _mdux_brew_result
            OUTPUT_VARIABLE _mdux_llvm_root
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _mdux_brew_result EQUAL 0)
            set(_mdux_llvm_root "")
        endif()
    endif()
endif()
if(NOT _mdux_llvm_root)
    message(FATAL_ERROR
        "Set MDUX_LLVM_ROOT to an upstream LLVM 21.1.8 installation (or install Homebrew llvm).")
endif()

set(CMAKE_CXX_COMPILER "${_mdux_llvm_root}/bin/clang++" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_mdux_llvm_root}/bin/llvm-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_mdux_llvm_root}/bin/llvm-ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_STDLIB_MODULES_JSON
    "${_mdux_llvm_root}/lib/c++/libc++.modules.json" CACHE FILEPATH "" FORCE)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    RESULT_VARIABLE _mdux_sdk_result
    OUTPUT_VARIABLE _mdux_macos_sdk
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _mdux_sdk_result EQUAL 0 OR NOT IS_DIRECTORY "${_mdux_macos_sdk}")
    message(FATAL_ERROR "xcrun could not locate the active macOS SDK.")
endif()
set(CMAKE_OSX_SYSROOT "${_mdux_macos_sdk}" CACHE PATH "" FORCE)
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

foreach(_mdux_required_tool
        "${CMAKE_CXX_COMPILER}"
        "${CMAKE_AR}"
        "${CMAKE_RANLIB}"
        "${CMAKE_CXX_STDLIB_MODULES_JSON}")
    if(NOT EXISTS "${_mdux_required_tool}")
        message(FATAL_ERROR "Required macOS toolchain input is missing: ${_mdux_required_tool}")
    endif()
endforeach()
