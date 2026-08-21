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
            ml/PackageLoad.cppm
    PRIVATE
        ml/Safetensors.cpp
        ml/ArchValidate.cpp
        ml/GoldenGen.cpp
        ml/MlBake.cpp
        ml/PackageLoad.cpp
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

# ---------------------------------------------------------------------------
# mdux-textbake (issue #157) - the text pipeline's baker
# ---------------------------------------------------------------------------
#
# Host-tools zone, exactly like the shader and ML bakers: it parses recipes and may throw, so it
# is deliberately not declared governed. It links MduX::ToolsCommon (and through it MduX::Core),
# which is how the baker reaches mdux.text.schema - the same governed module the device runtime
# will consume (#15). That identity is the host/device-sharing argument ADR-008 decision 1 makes
# for ML, inherited by text via ADR-010.
#
# At S1 this is a skeleton: run() produces a no-run package whose sidecar is zero bytes. S2
# (#158) lands the host-only TrueType parser alongside the skeleton: `mdux.tools.truetype`
# walks the glyf table directly without linking freetype, and is registered as a public
# module of MduXTextBakeLib so S4's font baker (#160) can import it through the same target
# that hosts the recipe model. S4 extends the recipe with the font pipeline inputs and S5
# (#161) adds the restricted-charset validation; no mdux_bake_artifact() call site is
# registered until S4 commits a real artifact.
#
# `mdux.text.raster` moved here from MduXCore in #116. It allocates, and `std::vector` reports
# failure by throwing, so its noexcept entry point has to catch - which ADR-005 forbids in the
# governed zone.
# Nothing else about it changed - it is integer-only and its output is still byte-compared in CI.
# See tools/text/Raster.cppm for the full argument.
add_library(MduXTextBakeLib)
add_library(MduX::TextBakeLib ALIAS MduXTextBakeLib)

target_sources(MduXTextBakeLib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            text/TextBake.cppm
            text/Truetype.cppm
            text/AtlasPacker.cppm
            text/Raster.cppm
    PRIVATE
        text/TextBake.cpp
        text/Truetype.cpp
        text/AtlasPacker.cpp
        text/Raster.cpp
)

target_compile_features(MduXTextBakeLib PUBLIC cxx_std_23)
target_link_libraries(MduXTextBakeLib PUBLIC MduX::ToolsCommon)
target_link_libraries(MduXTextBakeLib PRIVATE MduX_warnings)

set_target_properties(MduXTextBakeLib
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXTextBakeLib PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXTextBakeLib PRIVATE /experimental:module /std:c++latest)
endif()

# ---------------------------------------------------------------------------
# The .medui compiler (epic #15, ADR-011, ADR-012)
# ---------------------------------------------------------------------------
#
# Host-tools zone, like every other baker: the compiler parses untrusted input and may throw and
# allocate freely, because nothing it contains reaches a device. Deliberately not declared
# governed, which is also what stops a governed target linking it - mdux_verify_trust_zones()
# reports that at configure time.
#
# At S8 (#197) this is the diagnostic registry (#191), lexer, AST, parser, semantic analyzer,
# integer-only bounded layout solver, the text-budget check that measures a resolved box against the
# widest approved translation (#195), the golden references that say where safety-critical content
# must appear (#196), and the compiled-screen document with the canonical JSON it is committed as.
# The mdux-meduic executable with its mdux_compile_screen() registration is #198, so there is still
# no bake call site. Every later stage adds sources to this target rather than inventing another.
#
# The package stage reaches mdux.evidence.json and mdux.medui.schema through MduX::ToolsCommon's
# link to MduX::Core, which is what the acceptance criterion on #197 asks for: the host compiler and
# the device runtime import one definition of a compiled screen rather than two that can drift.
#
# The budget stage reaches mdux.text.draw and mdux.font.schema through MduX::ToolsCommon's link to
# MduX::Core: it decodes committed run records with the same decoder the device draws them with, so
# the byte-order and baseline contracts have one implementation rather than a host copy of one.
add_library(MduXMeduiLib)
add_library(MduX::MeduiLib ALIAS MduXMeduiLib)

target_sources(MduXMeduiLib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            medui/Diagnostics.cppm
            medui/Lexer.cppm
            medui/Ast.cppm
            medui/Parser.cppm
            medui/Semantic.cppm
            medui/Layout.cppm
            medui/TextBudget.cppm
            medui/Goldens.cppm
            medui/Package.cppm
            medui/Emit.cppm
    PRIVATE
        medui/Diagnostics.cpp
        medui/Lexer.cpp
        medui/Parser.cpp
        medui/Semantic.cpp
        medui/Layout.cpp
        medui/TextBudget.cpp
        medui/Goldens.cpp
        medui/Package.cpp
        medui/Emit.cpp
)

target_compile_features(MduXMeduiLib PUBLIC cxx_std_23)
target_link_libraries(MduXMeduiLib PUBLIC MduX::ToolsCommon)
target_link_libraries(MduXMeduiLib PRIVATE MduX_warnings)

set_target_properties(MduXMeduiLib
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(MduXMeduiLib PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(MduXMeduiLib PRIVATE /experimental:module /std:c++latest)
endif()

# The screen emitter (issue #197). Its own executable rather than a mode of the compiler, for the
# reason mdux-shaderemit is its own: compiling a source and rendering a committed package are
# different jobs, and the second must stay runnable on an artifact whose source is not present.
add_executable(mdux-screenemit medui/ScreenEmitMain.cpp)
target_link_libraries(mdux-screenemit PRIVATE MduX::MeduiLib MduX_warnings)

set_target_properties(mdux-screenemit
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(mdux-screenemit PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(mdux-screenemit PRIVATE /experimental:module /std:c++latest)
endif()

add_executable(mdux-textbake text/TextBakeMain.cpp)
target_link_libraries(mdux-textbake PRIVATE MduX::TextBakeLib MduX_warnings)

set_target_properties(mdux-textbake
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(mdux-textbake PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(mdux-textbake PRIVATE /experimental:module /std:c++latest)
endif()
