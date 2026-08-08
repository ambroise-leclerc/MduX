# Tests for MduX Medical Device UI Library
#
# Built on the in-repo MduXTest framework (tests/framework/MduXTest.cppm +
# MduXTest.hpp) rather than a third-party framework - a zero-SOUP library
# should not depend on a framework it would itself need to qualify. See
# ADR-004/ADR-005 and issue #44 for the rationale.

include(${CMAKE_SOURCE_DIR}/cmake/MduXTestDiscovery.cmake)

# Governed-zone module tests. Deliberately links MduX::Core only, not MduX::MduX -
# this is itself a check that the governed modules are usable without pulling in
# Vulkan (see ADR-004).
add_executable(core_tests
    core/CoreTests.cpp
)

target_sources(core_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(core_tests PRIVATE MduX::Core)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(core_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(core_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(core_tests PRIVATE /experimental:module)
    target_compile_options(core_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(core_tests)

# Evidence-kernel and governance tests (ADR-007, issues #34-35). Links MduX::Core only, for the
# same reason core_tests does: both are governed modules, so a test that needed MduX::MduX to
# build them would mean the trust-zone boundary had been crossed somewhere.
add_executable(evidence_tests
    evidence/EvidenceTestMain.cpp
    evidence/DigestTests.cpp
    evidence/JsonTests.cpp
    evidence/ReportTests.cpp
    governance/JustificationTests.cpp
    governance/ComplianceProgramTests.cpp
    governance/ComplianceTests.cpp
)

target_sources(evidence_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(evidence_tests PRIVATE MduX::Core)
target_include_directories(evidence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(evidence_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(evidence_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(evidence_tests PRIVATE /experimental:module)
    target_compile_options(evidence_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(evidence_tests)

# Host-tools tests (ADR-004 host-tools zone). Links MduX::ToolsCommon, which is deliberately not
# a governed target - these tests exercise code permitted to throw (ADR-005).
add_executable(tools_tests
    tools/ToolsTestMain.cpp
    tools/TomlTests.cpp
)

target_sources(tools_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(tools_tests PRIVATE MduX::ToolsCommon)
target_include_directories(tools_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(tools_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(tools_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(tools_tests PRIVATE /experimental:module)
    target_compile_options(tools_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(tools_tests)

# Shader schema, baker and emitter (issues #119, #141)
#
# Links MduX::ShaderBakeLib rather than the executable, so the scenarios drive run()/write()/verify()
# as calls. Spawning mdux-shaderbake per case would make a failing assertion report only that an
# exit status was wrong, which is the least useful thing it could say. The whole suite converted to
# SpecLab (issue #141), so this is a spec target rather than an MduXTest one.
add_executable(shader_spec
    shader/ShaderSpecMain.cpp
    shader/SchemaTests.cpp
    shader/SpirvTests.cpp
    shader/ShaderBakeTests.cpp
    shader/UiPackageTests.cpp
    shader/EmitTests.cpp
    shader/GeneratedTests.cpp
    shader/GeneratedModuleConsumer.cpp
    shader/GeneratedHeaderConsumer.cpp
)

# The generated module joins a file set; the generated header is reached through the include
# directory. Both are build-tree paths (issue #121) - nothing generated is committed.
#
# Compiling both forms in one binary is #121's acceptance criterion, not incidental. It works
# because the module form is attached to a named module and the header form is not, so they are
# distinct entities that can coexist; GeneratedTests then asserts they describe the same bytes and
# the same contract.
#
# The generated module needs its own named file set: the default CXX_MODULES set's base directory
# is this source directory, and a generated module lives in the build tree.
target_sources(shader_spec
    PRIVATE
        FILE_SET generated_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
            FILES ${MDUX_UI_SHADER_MODULE}
)
add_dependencies(shader_spec emit-shader-mdux-ui)
target_link_libraries(shader_spec PRIVATE MduX::ShaderBakeLib speclab::speclab)
target_include_directories(shader_spec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SHADER_DIR}
)

# UiPackageTests reads the committed artifact under generated/, which is a fixed location in the
# source tree rather than anything the build produces. It reads only; the source-tree-cleanliness
# gate in CI stays satisfied.
target_compile_definitions(shader_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# The build's own answers for mdux_shader_identifier(), written where GeneratedTests can read them
# and compare against identifierFor() in the C++ emitter.
#
# Both halves have to be *executed* for this to mean anything. GeneratedTests previously asserted
# identifierFor()'s behaviour and called that "kept in step with cmake/MduXShaderEmit.cmake" - but
# a C++ assertion cannot observe the CMake regex, so the two drifted: the CMake side never applied
# the leading-underscore rule for ids beginning with a digit, and would have looked for `2d.cppm`
# while the emitter wrote `_2d.cppm`.
set(mdux_identifier_parity_ids "mdux-ui" "a.b-c" "plain" "2d" "3d" "already_fine" "a b" "ui.v2")
set(mdux_identifier_parity_lines "")
foreach(mdux_identifier_parity_id IN LISTS mdux_identifier_parity_ids)
    mdux_shader_identifier(mdux_identifier_parity_value "${mdux_identifier_parity_id}")
    string(APPEND mdux_identifier_parity_lines
           "${mdux_identifier_parity_id}\t${mdux_identifier_parity_value}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shader_identifier_parity.tsv"
     "${mdux_identifier_parity_lines}")
target_compile_definitions(shader_spec PRIVATE
    MDUX_IDENTIFIER_PARITY_FILE="${CMAKE_CURRENT_BINARY_DIR}/shader_identifier_parity.tsv")

set_target_properties(shader_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(shader_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(shader_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(shader_spec)

# Governed fixed-budget draw types (issue #123), Wave 3 conversion (issue #141)
#
# Links MduX::Core only, deliberately. mdux.draw contains no Vulkan handle and no device concept,
# so a scenario that needed MduX::MduX to build it would mean the trust-zone boundary had moved -
# which is the property #125's headless verification and #126's pixel test both rest on. The whole
# suite converted to SpecLab, so this is a spec target rather than an MduXTest one.
add_executable(draw_spec
    draw/DrawSpecMain.cpp
    draw/DrawTests.cpp
)

target_link_libraries(draw_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(draw_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(draw_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(draw_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(draw_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(draw_spec)

# Adapter-zone Vulkan renderer (issue #124)
#
# Links MduX::MduX, so this suite sees Vulkan where draw_spec deliberately does not. Only the
# device-free half of the renderer is covered here - create()'s validation runs before the first
# Vulkan call, so it is testable with no ICD present, which is what CI currently has. The device
# path lands with #125's headless harness.
add_executable(render_tests
    render/RenderTestMain.cpp
    render/VulkanRendererTests.cpp
)

target_sources(render_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(render_tests PRIVATE MduX::MduX)
target_include_directories(render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(render_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(render_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(render_tests PRIVATE /experimental:module)
    target_compile_options(render_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(render_tests)

# Headless offscreen rendering and readback (issue #125)
#
# Separate from render_tests because this suite needs a real Vulkan device where that one
# deliberately needs none. It consumes the generated shader module, so it depends on the emitter
# the same way shader_spec does.
#
# SKIP_RETURN_CODE 77 is the point: with no loader, no ICD or no device the binary exits 77 and
# CTest reports Skipped, which is a distinct outcome from passing. A contributor without Vulkan
# sees neither a red test they cannot act on nor a green one that ran nothing.
add_executable(offscreen_tests
    render/OffscreenTestMain.cpp
    render/OffscreenTests.cpp
    render/PixelTests.cpp
)

target_sources(offscreen_tests
    PRIVATE
        FILE_SET CXX_MODULES FILES framework/MduXTest.cppm
        FILE_SET generated_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
            FILES ${MDUX_UI_SHADER_MODULE}
)
add_dependencies(offscreen_tests emit-shader-mdux-ui)
target_link_libraries(offscreen_tests PRIVATE MduX::MduX)
target_include_directories(offscreen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SHADER_DIR}
)

set_target_properties(offscreen_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(offscreen_tests PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(offscreen_tests PRIVATE /experimental:module)
    target_compile_options(offscreen_tests PRIVATE /std:c++latest)
endif()

# Registered as one test rather than through mdux_discover_tests: discovery runs the binary with
# --list-tests at build time, which on a machine with no device would list nothing and silently
# register no tests at all. One entry that can report Skipped is honest where zero entries is not.
add_test(NAME offscreen_tests COMMAND offscreen_tests)
set_tests_properties(offscreen_tests PROPERTIES LABELS "pixel" SKIP_RETURN_CODE 77)

# Basic unit tests
add_executable(unit_tests
    TestMain.cpp
    TestVersion.cpp
    TestCompliance.cpp
)

target_sources(unit_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(unit_tests PRIVATE MduX::MduX)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Set C++23 standard for tests and enable module imports
set_target_properties(unit_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(unit_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(unit_tests PRIVATE /experimental:module)
    target_compile_options(unit_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(unit_tests)

# Medical device compliance tests
add_executable(compliance_tests
    ComplianceTestMain.cpp
    TestRegulatoryCompliance.cpp
)

target_sources(compliance_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(compliance_tests PRIVATE MduX::MduX)
target_include_directories(compliance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(compliance_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(compliance_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(compliance_tests PRIVATE /experimental:module)
    target_compile_options(compliance_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(compliance_tests)

# Vulkan SC specific tests
add_executable(vulkansc_memory_tests
    vulkansc/MemoryPoolManagerTests.cpp
)

target_sources(vulkansc_memory_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(vulkansc_memory_tests PRIVATE MduX::MduX)
target_include_directories(vulkansc_memory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(vulkansc_memory_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(vulkansc_memory_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vulkansc_memory_tests PRIVATE /experimental:module)
    target_compile_options(vulkansc_memory_tests PRIVATE /std:c++latest)
endif()

# Work around a GCC internal compiler error in this specific translation unit at
# optimization levels above -O0 (GIMPLE ealias pass, "internal compiler error: in
# nonnull_arg_p, at tree.cc"). Reproduced deterministically on both GCC 15.2.0 and
# GCC 16 with C++23 modules + import std + <vulkan/vulkan.h>; -O1/-O2/-O3/-Og all
# trigger it, -O0 does not. -fno-tree-pta and dropping -Wformat=2 do not help. See
# issue #48 for the investigation. A later -O flag wins on GCC's command line, so
# this override is safe to append regardless of CMAKE_BUILD_TYPE.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(vulkansc_memory_tests PRIVATE -O0)
endif()

mdux_discover_tests(vulkansc_memory_tests)

# Vulkan SC device object tests
add_executable(vulkansc_object_tests
    vulkansc/DeviceObjectManagerTests.cpp
)

target_sources(vulkansc_object_tests PRIVATE FILE_SET CXX_MODULES FILES framework/MduXTest.cppm)
target_link_libraries(vulkansc_object_tests PRIVATE MduX::MduX)
target_include_directories(vulkansc_object_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(vulkansc_object_tests
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Only set CXX_MODULE_STD if __CMAKE::CXX23 target is available
if(TARGET __CMAKE::CXX23)
    set_target_properties(vulkansc_object_tests PROPERTIES CXX_MODULE_STD ON)
endif()

# MSVC-specific configuration for import std support
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vulkansc_object_tests PRIVATE /experimental:module)
    target_compile_options(vulkansc_object_tests PRIVATE /std:c++latest)
endif()

mdux_discover_tests(vulkansc_object_tests)

# ---------------------------------------------------------------------------
# SpecLab BDD specs
# ---------------------------------------------------------------------------
#
# SpecLab (https://github.com/ambroise-leclerc/SpecLab) provides Given/When/Then without macros,
# via `import speclab;`. It is a test-only dependency: no library target links it, and
# mdux_verify_trust_zones() would fail the configure step if a governed target ever did.
#
# It is also the reason this project's GCC floor is 16. GCC 15 cannot build SpecLab at all -
# reading a named module that transitively re-exports parts of `std` fails with "failed to load
# pendings for 'std::_Sp_counted_ptr_inplace'", a defect in GCC's own std BMI.
# Pinned to a commit, not to a branch. SpecLab has no tagged release yet, and a moving ref would
# mean the test framework under a medical device library could change between two builds of the
# same MduX commit - which is the same reproducibility argument ADR-007 makes about baked
# artifacts. Bump this deliberately, in a reviewable diff.
CPMAddPackage(
    NAME SpecLab
    GITHUB_REPOSITORY ambroise-leclerc/SpecLab
    GIT_TAG 84631c9f01f5309baf1d12618b2ef66c3512b511
    OPTIONS
        "SPECLAB_BUILD_TESTS OFF"
        "SPECLAB_BUILD_EXAMPLES OFF"
        "ENABLE_CACHE OFF"
)

# The bridge's own coverage. Deliberately a separate binary from the converted suites: if the
# integration breaks, this fails with a name that says so rather than looking like a draw-list bug.
add_executable(bridge_spec
    spec/BridgeSpec.cpp
)

target_link_libraries(bridge_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(bridge_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(bridge_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(bridge_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(bridge_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(bridge_spec)

# Wave 3 conversions (issue #141) - the host-tools CLI suite. Links MduX::ToolsCommon, the same
# host-tools-zone target tools_tests links: a SpecLab binary is still a test, not a governed or
# library target.
add_executable(tools_spec
    tools/ToolsSpecMain.cpp
    tools/CliTests.cpp
)

target_link_libraries(tools_spec PRIVATE MduX::ToolsCommon speclab::speclab)
target_include_directories(tools_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(tools_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(tools_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(tools_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(tools_spec)

# Zero-SOUP ML inference (epic #18, ADR-008)
#
# Links MduX::Core only. The schema, kernels and runtime are governed modules by design - a
# scenario that needed MduX::MduX to build one would mean the trust-zone boundary had moved, and
# the whole zero-SOUP argument rests on the ML subsystem reaching nothing but std.
add_executable(ml_spec
    ml/MlSpecMain.cpp
    ml/SchemaTests.cpp
    ml/KernelTests.cpp
    ml/DeterminismTests.cpp
    ml/RuntimeTests.cpp
)

target_link_libraries(ml_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(ml_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(ml_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(ml_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ml_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(ml_spec)

# Text pipeline schema (epic #14, issue #157, ADR-010)
#
# Links MduX::Core only. mdux.text.schema is governed and contains no Vulkan handle, so a
# scenario that needed MduX::MduX to build it would mean the trust-zone boundary had moved -
# the same standing evidence shader_spec, draw_spec and ml_spec give. S1 ships the schema and a
# no-run baker skeleton; the TrueType parser (#158), rasteriser (#159), atlas font baker (#160),
# font schema (#161) and coverage draw path (#162) follow as their blockers close.
# Font schema suite (issue #161)
#
# Its own executable rather than cases inside text_spec, because a font package and a text package
# are different shapes with different rules, and a failure should name which schema rejected the
# input. Links MduX::Core only - see FontSpecMain.cpp on why that link line is evidence.
add_executable(font_spec
    font/FontSpecMain.cpp
    font/SchemaTests.cpp
)

target_link_libraries(font_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(font_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The committed-package scenario reads generated/font/dejavu-ui/package.json, so the suite has to
# know where the repository is - the same way shader_spec locates its own artifacts.
target_compile_definitions(font_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

set_target_properties(font_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(font_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(font_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(font_spec)

add_executable(text_spec
    text/TextSpecMain.cpp
    text/SchemaTests.cpp
    text/RasterTests.cpp
)

target_link_libraries(text_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(text_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(text_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(text_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(text_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(text_spec)

# Text baker host-tools suite (issue #157)
#
# Links MduX::TextBakeLib, the host-tools-zone target. Deliberately separate from text_spec, which
# links MduX::Core only - keeping the two apart is what preserves text_spec's standing evidence
# that the governed `mdux.text.schema` reaches nothing but std. The cases mirror the pattern
# `ml_tools_spec` and `shader_spec` follow: drive `parseRecipe()` / `run()` / `write()` / `verify()`
# as calls so a failing assertion names the diagnostic code that failed rather than only an exit
# status from a spawned process.
add_executable(text_tools_spec
    text/TextToolsSpecMain.cpp
    text/TextBakeTests.cpp
    text/TruetypeTests.cpp
    text/AtlasPackerTests.cpp
)

target_link_libraries(text_tools_spec PRIVATE MduX::TextBakeLib speclab::speclab)
target_include_directories(text_tools_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(text_tools_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

set_target_properties(text_tools_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(text_tools_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(text_tools_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(text_tools_spec)

# ML host-tools suite (issue #60)
#
# Links MduX::MlBakeLib, the host-tools-zone target. Deliberately separate from ml_spec, which
# links MduX::Core only - keeping the two apart is what preserves ml_spec's standing evidence that
# the governed ML modules reach nothing but std.
add_executable(ml_tools_spec
    ml/MlToolsSpecMain.cpp
    ml/SafetensorsTests.cpp
    ml/WeightSwapTests.cpp
)

target_link_libraries(ml_tools_spec PRIVATE MduX::MlBakeLib speclab::speclab)
target_include_directories(ml_tools_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# WeightSwapTests reads the two committed model packages under generated/, a fixed location in the
# source tree rather than anything the build produces. It reads only, so CI's
# source-tree-cleanliness gate stays satisfied - the same arrangement shader_spec uses.
target_compile_definitions(ml_tools_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

set_target_properties(ml_tools_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(ml_tools_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ml_tools_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(ml_tools_spec)

# No-heap verification, layer 1 (issue #63): global operator new interposition.
#
# Its own binary because it replaces the global allocator - see tests/ml/NoHeapSpecMain.cpp.
add_executable(ml_noheap_spec
    ml/NoHeapSpecMain.cpp
    ml/NoHeapTests.cpp
)

target_link_libraries(ml_noheap_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(ml_noheap_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(ml_noheap_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(ml_noheap_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ml_noheap_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(ml_noheap_spec)

# No-heap verification, layer 2 (issue #63): object-file symbol scan.
#
# Catches an allocator reference at build time, in the PR that introduced it, rather than whenever
# someone next executes the path that would have allocated. The object list is written by
# file(GENERATE) rather than passed as a -D argument, because a generator expression producing a
# ;-separated list would be split into separate arguments by add_test().
set(mdux_ml_object_list "${CMAKE_CURRENT_BINARY_DIR}/ml_objects_$<CONFIG>.txt")
file(GENERATE
    OUTPUT "${mdux_ml_object_list}"
    CONTENT "$<JOIN:$<FILTER:$<TARGET_OBJECTS:MduXCore>,INCLUDE,/ml/>,\n>"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    find_program(MDUX_SYMBOL_TOOL NAMES dumpbin)
    set(mdux_symbol_tool_kind "dumpbin")
else()
    # CMAKE_NM is set by the toolchain; fall back to a plain nm on PATH.
    if(CMAKE_NM)
        set(MDUX_SYMBOL_TOOL "${CMAKE_NM}")
    else()
        find_program(MDUX_SYMBOL_TOOL NAMES nm)
    endif()
    set(mdux_symbol_tool_kind "nm")
endif()

if(MDUX_SYMBOL_TOOL)
    add_test(NAME ml.noheap.symbolScan
        COMMAND ${CMAKE_COMMAND}
            -DMDUX_NOHEAP_OBJECT_LIST=${mdux_ml_object_list}
            -DMDUX_NOHEAP_TOOL=${mdux_symbol_tool_kind}
            -DMDUX_NOHEAP_COMMAND=${MDUX_SYMBOL_TOOL}
            -P ${CMAKE_SOURCE_DIR}/cmake/MduXNoHeapScan.cmake
    )
    set_tests_properties(ml.noheap.symbolScan PROPERTIES LABELS "noheap")
else()
    # Deliberately not silent. A missing tool means this layer is not running, and the other two
    # layers do not cover what it covers.
    message(WARNING
        "No symbol tool (nm/dumpbin) found - ml.noheap.symbolScan will not run, so issue #63's "
        "layer 2 is unverified in this build.")
endif()
