# Host tools (ADR-004 host-tools zone)
#
# Nothing here is ever linked into MduXCore or MduX, so this code may use exceptions freely
# (ADR-005) and may depend on parsers that must not reach a device build (ADR-007). The
# trust-zone check enforces the boundary from the other side: MduXToolsCommon is deliberately
# *not* declared governed, so linking it from a governed target is a configure-time FATAL_ERROR.
#
# MduXToolsCommon is the shared infrastructure every baker uses:
#   mdux.tools.toml - the TOML-subset recipe reader
#   mdux.tools.cli  - argument parsing and the shared diagnostic envelope
#
# It links MduX::Core because bakers build artifacts through the governed evidence modules
# (digest, canonical JSON, bake report). That direction is fine and is the intended one: tools may
# depend on governed code, never the reverse.

add_library(MduXToolsCommon)
add_library(MduX::ToolsCommon ALIAS MduXToolsCommon)

target_sources(MduXToolsCommon
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            common/Toml.cppm
            common/Cli.cppm
    PRIVATE
        common/Toml.cpp
        common/Cli.cpp
)

target_compile_features(MduXToolsCommon PUBLIC cxx_std_23)
target_link_libraries(MduXToolsCommon PUBLIC MduX::Core)
target_link_libraries(MduXToolsCommon PRIVATE MduX_warnings)

# Provenance for every report a baker writes (ADR-007, decision 5). PUBLIC so a baker linking
# MduXToolsCommon gets MDUX_TOOL_VERSION and MDUX_GIT_SHA without repeating this.
target_link_libraries(MduXToolsCommon PUBLIC MduX::BuildInfo)

set_target_properties(MduXToolsCommon
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXToolsCommon PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXToolsCommon PRIVATE /experimental:module /std:c++latest)
endif()

# ---------------------------------------------------------------------------
# mdux-shaderbake (issue #119) - the first baker
# ---------------------------------------------------------------------------
#
# Split into a library and a two-page executable on purpose. mdux_bake_artifact() needs an
# executable target to invoke, while the tests need to drive run()/write()/verify() directly -
# spawning a process per case would make a failing assertion tell you only that an exit status was
# wrong. The executable is the boundary where a command line becomes a call; everything worth
# testing is in the library.
#
# MduXShaderBakeLib links MduX::ToolsCommon (and through it MduX::Core), so the baker builds its
# artifact through the same governed evidence modules the runtime reads it with. It is deliberately
# not declared governed: it parses untrusted input and may throw.

add_library(MduXShaderBakeLib)
add_library(MduX::ShaderBakeLib ALIAS MduXShaderBakeLib)

target_sources(MduXShaderBakeLib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            shader/Spirv.cppm
            shader/ShaderBake.cppm
            shader/Emit.cppm
    PRIVATE
        shader/Spirv.cpp
        shader/ShaderBake.cpp
        shader/Emit.cpp
)

target_compile_features(MduXShaderBakeLib PUBLIC cxx_std_23)
target_link_libraries(MduXShaderBakeLib PUBLIC MduX::ToolsCommon)
target_link_libraries(MduXShaderBakeLib PRIVATE MduX_warnings)

set_target_properties(MduXShaderBakeLib
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXShaderBakeLib PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXShaderBakeLib PRIVATE /experimental:module /std:c++latest)
endif()

# Zero-SOUP ML weight import and baking (epic #18, ADR-008)
#
# Host-tools zone, exactly like the shader baker: it parses untrusted weight files and may throw,
# so it is deliberately not declared governed. It links MduX::ToolsCommon (and through it
# MduX::Core), which is how the baker reaches mdux.ml.kernels - the same governed module the device
# runtime executes. That identity is the whole basis of the golden-vector argument in ADR-008.
add_library(MduXMlBakeLib)
add_library(MduX::MlBakeLib ALIAS MduXMlBakeLib)

target_sources(MduXMlBakeLib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ml/Safetensors.cppm
            ml/ArchValidate.cppm
            ml/GoldenGen.cppm
            ml/MlBake.cppm
    PRIVATE
        ml/Safetensors.cpp
        ml/ArchValidate.cpp
        ml/GoldenGen.cpp
        ml/MlBake.cpp
)

target_compile_features(MduXMlBakeLib PUBLIC cxx_std_23)
target_link_libraries(MduXMlBakeLib PUBLIC MduX::ToolsCommon)
target_link_libraries(MduXMlBakeLib PRIVATE MduX_warnings)

set_target_properties(MduXMlBakeLib
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXMlBakeLib PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXMlBakeLib PRIVATE /experimental:module /std:c++latest)
endif()

add_executable(mdux-mlbake ml/MlBakeMain.cpp)
target_link_libraries(mdux-mlbake PRIVATE MduX::MlBakeLib MduX_warnings)

set_target_properties(mdux-mlbake
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(mdux-mlbake PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(mdux-mlbake PRIVATE /experimental:module /std:c++latest)
endif()

add_executable(mdux-shaderbake shader/ShaderBakeMain.cpp)
target_link_libraries(mdux-shaderbake PRIVATE MduX::ShaderBakeLib MduX_warnings)

set_target_properties(mdux-shaderbake
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(mdux-shaderbake PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(mdux-shaderbake PRIVATE /experimental:module /std:c++latest)
endif()

add_executable(mdux-shaderemit shader/ShaderEmitMain.cpp)
target_link_libraries(mdux-shaderemit PRIVATE MduX::ShaderBakeLib MduX_warnings)

set_target_properties(mdux-shaderemit
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(mdux-shaderemit PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(mdux-shaderemit PRIVATE /experimental:module /std:c++latest)
endif()
