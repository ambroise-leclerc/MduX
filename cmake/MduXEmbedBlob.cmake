# MduXEmbedBlob.cmake
#
# Links a binary file into a target as a byte array, for demonstrators and for devices with no
# filesystem (issue #64, ADR-008 decision 2).
#
# Usage:
#   mdux_embed_blob(MyTarget
#       FILE   ${CMAKE_SOURCE_DIR}/generated/model/ecg-demo/weights.bin
#       SYMBOL ecgModelWeights)
#
# Generates a plain `.cpp` and a `.hpp` in the build tree and adds them to the target. The accessor
# is `std::span<const std::byte> <symbol>()`, declared in `<symbol>.hpp`.
#
# ## Plain .cpp, deliberately not a module and not constexpr
#
# A multi-megabyte `constexpr` array takes minutes to compile and can exhaust MSVC, which is the
# same reason ADR-008 keeps weights out of the package object entirely. A plain array in an
# ordinary translation unit compiles in reasonable time and lands in .rodata, which is where a
# device wants it anyway.
#
# ## alignas is load-bearing, not decoration
#
# `Classifier1D::create()` reads the blob as `f32` and refuses a blob that is not suitably aligned.
# A bare `unsigned char[]` has alignment 1, so without `alignas` the linker would be free to place
# it anywhere and the demonstrator would fail closed on its own weights - intermittently, depending
# on what else was in the section. `alignas(16)` covers f32 with room to spare.

set(_MDUX_EMBED_BLOB_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/MduXEmbedBlobImpl.cmake")

function(mdux_embed_blob TGT)
    set(options "")
    set(oneValueArgs FILE SYMBOL)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT TARGET ${TGT})
        message(FATAL_ERROR "mdux_embed_blob: '${TGT}' is not a target")
    endif()
    foreach(required FILE SYMBOL)
        if(NOT ARG_${required})
            message(FATAL_ERROR "mdux_embed_blob: ${required} is required")
        endif()
    endforeach()
    if(NOT ARG_SYMBOL MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "mdux_embed_blob: SYMBOL '${ARG_SYMBOL}' is not a valid C++ identifier")
    endif()
    if(NOT EXISTS "${ARG_FILE}")
        message(FATAL_ERROR "mdux_embed_blob: FILE '${ARG_FILE}' does not exist")
    endif()

    # No make_directory step: file(WRITE) creates any missing parent directories itself, which is
    # documented CMake behaviour and is what the impl script relies on. Noted because "the output
    # directory is never created" is a reasonable thing to think when reading this.
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/mdux_embed/${ARG_SYMBOL}")
    set(generated_source "${generated_dir}/${ARG_SYMBOL}.cpp")
    set(generated_header "${generated_dir}/${ARG_SYMBOL}.hpp")

    add_custom_command(
        OUTPUT "${generated_source}" "${generated_header}"
        COMMAND ${CMAKE_COMMAND}
            -DMDUX_BLOB_INPUT=${ARG_FILE}
            -DMDUX_BLOB_SYMBOL=${ARG_SYMBOL}
            -DMDUX_BLOB_SOURCE=${generated_source}
            -DMDUX_BLOB_HEADER=${generated_header}
            -P "${_MDUX_EMBED_BLOB_SCRIPT}"
        DEPENDS "${ARG_FILE}" "${_MDUX_EMBED_BLOB_SCRIPT}"
        COMMENT "Embedding ${ARG_SYMBOL} from ${ARG_FILE}"
        VERBATIM
    )

    target_sources(${TGT} PRIVATE "${generated_source}" "${generated_header}")
    target_include_directories(${TGT} PRIVATE "${generated_dir}")
endfunction()
