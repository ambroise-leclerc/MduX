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
