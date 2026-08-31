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
    shader/CommittedPackageTests.cpp
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
    render/TextPixelTests.cpp
    render/ScreenPixelTests.cpp
)

target_sources(offscreen_tests
    PRIVATE
        FILE_SET CXX_MODULES FILES framework/MduXTest.cppm
        FILE_SET generated_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SHADER_DIR}
            FILES ${MDUX_UI_SHADER_MODULE}
        # The emitted screen joins its own file set, because its base directory is the screen output
        # directory rather than the shader one. ScreenPixelTests (#201) links it: the last link in
        # the chain from an authored .medui file to a compared pixel.
        FILE_SET generated_screen_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SCREEN_DIR}
            FILES ${MDUX_ENDOSCOPE_SCREEN_MODULE}
)
add_dependencies(offscreen_tests emit-shader-mdux-ui emit-screen-endoscope-monitor)

# ScreenPixelTests reads the committed golden sidecar under generated/, a fixed location in the
# source tree rather than anything the build produces. It reads only; the source-tree-cleanliness
# gate in CI stays satisfied.
target_compile_definitions(offscreen_tests PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(offscreen_tests PRIVATE MduX::MduX)
target_include_directories(offscreen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SHADER_DIR}
    ${MDUX_GENERATED_SCREEN_DIR}
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
# Pinned to a commit, not to a tag or a branch. A moving ref would mean the test framework under a
# medical device library could change between two builds of the same MduX commit - the same
# reproducibility argument ADR-007 makes about baked artifacts. Bump this deliberately, in a
# reviewable diff.
#
# The commit is SpecLab v0.1.0, its first release. Pinned by SHA and not by the tag name, and the
# reason is not theoretical: this pin was written against `v0.1.0` while the tag pointed at
# ea7f2e363, and within the hour the tag was moved to 37b51fcec to pick up a release-packaging fix
# (ambroise-leclerc/SpecLab#18). Nothing in that commit affects this build - it makes SpecLab's project version a
# cache variable and edits its own release workflow - but a reference that can change under a
# frozen MduX commit is a moving reference whatever it is called, and ADR-007's reproducibility
# argument does not distinguish between a branch and a retagged release.
#
# Read the tag name as documentation and the SHA as the pin. To bump, resolve the new tag to its
# commit and change both, in a reviewable diff.
#
# That release is also what makes MduX's own gcc:16.1.0 pin (#206) coherent rather than a
# workaround. SpecLab hit the same GCC 16.2 BMI corruption in its own CI
# (ambroise-leclerc/SpecLab#15), diagnosed
# it the same way, and pinned identically; its CMakeLists now enforces GCC 16.1+. A future working
# 16.2.x bump is tracked upstream as ambroise-leclerc/SpecLab#17, and this repository's #205
# follow-up depends on it. Upstream issue numbers are written in full: a bare #15 or #17 here names
# MduX's .medui compiler and Content components epics, which is a confusing thing to mean by
# accident in a comment about a different repository.
CPMAddPackage(
    NAME SpecLab
    GITHUB_REPOSITORY ambroise-leclerc/SpecLab
    # SpecLab v0.1.0
    GIT_TAG 37b51fcec
    PATCH_COMMAND
        ${CMAKE_COMMAND}
            -D "SOURCE_DIR=<SOURCE_DIR>"
            -D "PATCH_FILE=${CMAKE_SOURCE_DIR}/cmake/patches/speclab-cmake-4.3.patch"
            -P "${CMAKE_SOURCE_DIR}/cmake/patches/ApplySpecLabPatch.cmake"
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
    ml/GeneratedModelTests.cpp
    ml/GeneratedModelModuleConsumer.cpp
    ml/GeneratedModelHeaderConsumer.cpp
)

# Both generated forms are compiled into this one binary. The module joins a build-tree file set;
# the header is reached through the generated include directory. The target still links Core only,
# so package consumption cannot acquire the host-tools parser transitively.
target_sources(ml_spec
    PRIVATE
        FILE_SET generated_model_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_MODEL_DIR}
            FILES ${MDUX_ECG_MODEL_MODULE}
)
add_dependencies(ml_spec emit-model-ecg-demo)

target_link_libraries(ml_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(ml_spec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_MODEL_DIR}
)

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
# the same standing evidence shader_spec, draw_spec and ml_spec give. The suite covers the
# governed half of the text pipeline: the schema (#157), the font schema (#161) and the coverage
# draw path (#162). The host-only halves - the TrueType parser (#158), the rasteriser (#159) and
# the atlas font baker (#160) - are covered by text_tools_spec instead.
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
    text/DrawTests.cpp
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
#
# RasterTests.cpp lives here rather than in text_spec since #116, because `mdux.text.raster` moved
# to the host-tools zone. Moving the test with the module is not bookkeeping: text_spec links
# MduX::Core alone, and that link line *is* the standing evidence above. Leaving raster cases there
# would force MduX::TextBakeLib onto text_spec and destroy the very thing it exists to demonstrate.
add_executable(text_tools_spec
    text/TextToolsSpecMain.cpp
    text/TextBakeTests.cpp
    text/TruetypeTests.cpp
    text/AtlasPackerTests.cpp
    text/RasterTests.cpp
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

# Governed compiled-screen schema (issue #197)
#
# Links MduX::Core only, and deliberately not MduX::MeduiLib. `mdux.medui.schema` is the one type a
# device holds, so a scenario that needed the parser or the solver to build one would mean the
# trust-zone boundary had moved - the same argument that keeps ml_spec apart from ml_tools_spec.
#
# The file's centre of gravity is a namespace-scope static_assert rather than a scenario: ADR-012
# decision 3 puts `static_assert(package.validate().has_value())` in every generated screen, so
# compile-time validation is a promise the device build depends on, and one no runtime test can
# make on its behalf.
# The emitted C++ for the test corpus screen (#197). Build-tree only, never committed - see
# cmake/MduXScreenEmit.cmake. The package it renders is a fixture rather than an artifact under
# generated/, because no screen is baked there until #198 registers one; the byte-comparison that
# keeps the fixture honest is `medui-screen-emit-fixture-is-current` in medui_tools_spec, which
# recompiles the source and compares.
include(${CMAKE_SOURCE_DIR}/cmake/MduXScreenEmit.cmake)
mdux_emit_screen_package(
    ID every-component
    PACKAGE tests/medui/fixtures/every-component-package.json
    OUT_MODULE MDUX_EVERY_COMPONENT_SCREEN_MODULE
    OUT_DIR MDUX_GENERATED_SCREEN_DIR
)

# The degenerate screen the schema permits: no nodes, empty budget. Emitted and compiled because an
# empty C array is not valid C++, so this is the case where a screen validate() accepts could not be
# consumed at all - see GeneratedEmptyScreenConsumer.cpp.
mdux_emit_screen_package(
    ID empty-screen
    PACKAGE tests/medui/fixtures/empty-screen-package.json
    OUT_MODULE MDUX_EMPTY_SCREEN_MODULE
)

add_executable(medui_spec
    medui/MeduiSpecMain.cpp
    medui/SchemaTests.cpp
    medui/ScreenRuntimeTests.cpp
    medui/GeneratedScreenTests.cpp
    medui/GeneratedScreenModuleConsumer.cpp
    medui/GeneratedScreenHeaderConsumer.cpp
    medui/GeneratedEmptyScreenConsumer.cpp
)

# Compiling both emitted forms in one binary is #197's acceptance criterion, not incidental. It
# works because the module form is attached to a named module and the header form is not, so they
# are distinct entities that can coexist; GeneratedScreenTests then asserts they describe one
# screen. The generated module needs its own named file set, because the default CXX_MODULES set's
# base directory is this source directory and a generated module lives in the build tree.
target_sources(medui_spec
    PRIVATE
        FILE_SET generated_screen_modules
            TYPE CXX_MODULES
            BASE_DIRS ${MDUX_GENERATED_SCREEN_DIR}
            FILES
                ${MDUX_EVERY_COMPONENT_SCREEN_MODULE}
                ${MDUX_EMPTY_SCREEN_MODULE}
)
add_dependencies(medui_spec emit-screen-every-component emit-screen-empty-screen)

# MduX::Core and no host-tools module: a device build reaching a compiled screen through generated
# code links no compiler and parses nothing, and this target linking is the evidence for it.
target_link_libraries(medui_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(medui_spec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MDUX_GENERATED_SCREEN_DIR}
)

set_target_properties(medui_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(medui_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(medui_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(medui_spec)

# .medui compiler host-tools suite (issues #191, #192, #193, #194, #195, #196)
#
# Links MduX::MeduiLib. Two groups of scenarios: the diagnostic registry's invariants (#191) -
# completeness, uniqueness, identifier shape, ordering against the retired list, and the envelope
# diagnose() produces - and the lexer and parser (#192), whose rejection cases assert the code and
# the position rather than the message, since #191 published the codes as the contract and
# messages are explicitly rewordable.
#
# TextBudgetTests (#195) builds its own font and text packages rather than reading generated/: the
# widest-approved-translation rule is only checkable when two locales differ by a width the test
# chose. The packages are still validated by the schema modules that own their formats, so the
# measurements are taken from artifacts that would be legal on disk.
#
# GoldenTests (#196) reads tests/medui/fixtures/accepted-goldens.medui, which carries every rule
# ADR-011 fixes for golden references in one screen - including the node that matches both rules and
# must therefore appear exactly once.
#
# CheckTests (#200) drives the single-file checker over the same rejection corpus the parser and
# analyzer suites use by call, so a code that stops surfacing to an author stops a test. Two of its
# scenarios spawn the tool, for the same reason CompileTests has one: nothing at call level shows
# that the executable exits non-zero on a finding and zero on a run whose only output is notes.
#
# CompileTests (#198) drives the compiler driver as calls rather than spawning mdux-meduic: what is
# under test is the order the stages run in, the recipe being complete for the screen it names, and
# the three artifacts a compile produces - none of which an exit status could report on.
#
# EmitTests (#197) covers everything that has to hold before a compiler sees the generated source:
# among them the two ways a valid screen used to become invalid C++ - a name carrying a control
# character the `.medui` lexer decodes, and the empty screen the schema permits, whose node table
# cannot be an empty C array.
# that the committed fixture package is still what compiling its source produces, that both halves of
# the filename rule agree, and that a malformed package is refused rather than rendered. The emitted
# C++ itself is compiled and compared in medui_spec, which links MduX::Core only.
#
# PackageTests (#197) reads tests/medui/fixtures/accepted-every-component.medui, a screen carrying
# all eleven compiled payloads, and round-trips it through canonical JSON. The writer and the reader
# spell each member name separately, so the scenario executes both directions over one screen rather
# than asserting either half - the shape of parity check this repository has got wrong before.
add_executable(medui_tools_spec
    medui/MeduiToolsSpecMain.cpp
    medui/DiagnosticsTests.cpp
    medui/ParserTests.cpp
    medui/SemanticTests.cpp
    medui/LayoutTests.cpp
    medui/TextBudgetTests.cpp
    medui/GoldenTests.cpp
    medui/PackageTests.cpp
    medui/EmitTests.cpp
    medui/CompileTests.cpp
    medui/CheckTests.cpp
    medui/SharedConformanceTests.cpp
)

target_link_libraries(medui_tools_spec PRIVATE MduX::MeduiLib speclab::speclab)
target_include_directories(medui_tools_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The parser scenarios read tests/medui/fixtures/*.medui, which are real files an author could
# open rather than string literals - #200's mdux-medui-check will be pointed at the same corpus,
# and a fixture that exists only inside a test cannot be reused by the tool that checks files.
target_compile_definitions(medui_tools_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# One scenario spawns the compiler rather than calling into it: `medui-compile-cli` is the only place
# that can show main() rendering a diagnostic envelope and returning a status, rather than letting an
# exception reach the terminate handler - which is the behaviour an author meets first when a recipe
# is wrong. Everything else in this suite drives the calls, because a failing assertion then says
# what was wrong instead of what the exit status was.
add_dependencies(medui_tools_spec mdux-meduic mdux-medui-check)
target_compile_definitions(medui_tools_spec PRIVATE
    MDUX_MEDUIC_PATH="$<TARGET_FILE:mdux-meduic>"
    MDUX_MEDUI_CHECK_PATH="$<TARGET_FILE:mdux-medui-check>")

# The build's own answers for mdux_screen_identifier(), written where EmitTests can read them and
# compare against identifierForScreen() in the C++ emitter. Both halves are executed, for the reason
# the shader pair had to learn: a C++ assertion cannot observe a CMake regex, so asserting one half
# and calling it consistent with the other let the two drift on the leading-digit rule.
# Keywords are in the list because they are legal package slugs: `class` and `module` map to
# identifiers a compiler rejects unless the rule prefixes them, and both implementations agreed on
# that invalid answer until the prefix landed.
set(mdux_screen_identifier_ids
    "every-component" "empty-screen" "neurosense-500" "plain" "2d" "a.b-c" "already_fine" "ui.v2"
    "class" "namespace" "module" "import" "int")
set(mdux_screen_identifier_lines "")
foreach(mdux_screen_identifier_id IN LISTS mdux_screen_identifier_ids)
    mdux_screen_identifier(mdux_screen_identifier_value "${mdux_screen_identifier_id}")
    string(APPEND mdux_screen_identifier_lines "${mdux_screen_identifier_id}\t${mdux_screen_identifier_value}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/screen_identifier_parity.tsv" "${mdux_screen_identifier_lines}")
target_compile_definitions(medui_tools_spec PRIVATE
    MDUX_SCREEN_IDENTIFIER_PARITY_FILE="${CMAKE_CURRENT_BINARY_DIR}/screen_identifier_parity.tsv")

set_target_properties(medui_tools_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(medui_tools_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # SharedConformanceTests uses getenv() only to read CI configuration. Suppress MSVC's
    # blanket deprecation warning for that test at the target boundary instead of adding
    # compiler-control macros and pragmas to the C++ source.
    target_compile_options(medui_tools_spec PRIVATE /experimental:module /std:c++latest /wd4996)
endif()

mdux_discover_tests(medui_tools_spec)

# mdux_compile_screen()'s recipe reading (issue #198)
#
# Script-mode CMake, because what is under test is a pure function of the recipe text: which
# `[package]` keys the wrapper extracts, and whether it refuses a registration that names a different
# screen than the recipe compiles. Configuring a fixture project to reach the same two functions
# would be slower and would report a failure two layers from its cause.
#
# The negative cases match the message rather than the exit status, following
# governed.noThrow.symbolScan.negative - and, as that test's comment records, PASS_REGULAR_EXPRESSION
# must not be combined with WILL_FAIL, which would invert the verdict. A bare "it failed" would be
# satisfied by a typo in the script.
foreach(mdux_screen_recipe_case scoped identity-accepts committed)
    add_test(NAME "screen.recipe.${mdux_screen_recipe_case}"
        COMMAND ${CMAKE_COMMAND} -DCASE=${mdux_screen_recipe_case}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileScreenRecipeTest.cmake")
endforeach()

add_test(NAME "screen.recipe.identity"
    COMMAND ${CMAKE_COMMAND} -DCASE=identity
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileScreenRecipeTest.cmake")
# Short fragments, on purpose: CMake wraps the text of a FATAL_ERROR to its own width, so a regex
# spanning two clauses can straddle the line break it inserts. This one did - the message was right
# and the test failed anyway - which is worth one line here to save the next person the same hunt.
set_tests_properties("screen.recipe.identity" PROPERTIES
    PASS_REGULAR_EXPRESSION "declares id 'real-screen'")

add_test(NAME "screen.recipe.ambiguous"
    COMMAND ${CMAKE_COMMAND} -DCASE=ambiguous
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileScreenRecipeTest.cmake")
set_tests_properties("screen.recipe.ambiguous" PROPERTIES
    PASS_REGULAR_EXPRESSION "declares .source. 2 times")

add_test(NAME "screen.recipe.missing"
    COMMAND ${CMAKE_COMMAND} -DCASE=missing
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileScreenRecipeTest.cmake")
set_tests_properties("screen.recipe.missing" PROPERTIES
    PASS_REGULAR_EXPRESSION "has no .source")

# ML host-tools suite (issue #60)
#
# Links MduX::MlBakeLib, the host-tools-zone target. Deliberately separate from ml_spec, which
# links MduX::Core only - keeping the two apart is what preserves ml_spec's standing evidence that
# the governed ML modules reach nothing but std.
add_executable(ml_tools_spec
    ml/MlToolsSpecMain.cpp
    ml/SafetensorsTests.cpp
    ml/WeightSwapTests.cpp
    ml/EmitTests.cpp
)

target_link_libraries(ml_tools_spec PRIVATE MduX::MlBakeLib speclab::speclab)
target_include_directories(ml_tools_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# WeightSwapTests reads the two committed model packages under generated/, a fixed location in the
# source tree rather than anything the build produces. It reads only, so CI's
# source-tree-cleanliness gate stays satisfied - the same arrangement shader_spec uses.
target_compile_definitions(ml_tools_spec PRIVATE MDUX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Execute both halves of the generated-name rule over the same ids. A C++-only assertion cannot
# observe the CMake regex, which is the exact drift the shader emitter once shipped with.
set(mdux_model_identifier_parity_ids
    "ecg-demo" "a.b-c" "plain" "2d" "class" "already_fine" "a b" "model.v2")
set(mdux_model_identifier_parity_lines "")
foreach(mdux_model_identifier_parity_id IN LISTS mdux_model_identifier_parity_ids)
    mdux_model_identifier(mdux_model_identifier_parity_value
                          "${mdux_model_identifier_parity_id}")
    string(APPEND mdux_model_identifier_parity_lines
           "${mdux_model_identifier_parity_id}\t${mdux_model_identifier_parity_value}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/model_identifier_parity.tsv"
     "${mdux_model_identifier_parity_lines}")
target_compile_definitions(ml_tools_spec PRIVATE
    MDUX_MODEL_IDENTIFIER_PARITY_FILE="${CMAKE_CURRENT_BINARY_DIR}/model_identifier_parity.tsv")

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

# Screen runtime no-heap suite (issue #199)
#
# Its own binary for the reason ml_noheap_spec has one: it replaces the global operator new family,
# and that is a whole-program property. Keeping it out of medui_spec means those scenarios run
# against the ordinary allocator, and a failure here names the no-heap property rather than
# appearing as one line among two dozen unrelated ones.
add_executable(medui_noheap_spec
    medui/ScreenNoHeapSpecMain.cpp
    medui/ScreenNoHeapTests.cpp
)

target_link_libraries(medui_noheap_spec PRIVATE MduX::Core speclab::speclab)
target_include_directories(medui_noheap_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(medui_noheap_spec
    PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

if(TARGET __CMAKE::CXX23)
    set_target_properties(medui_noheap_spec PROPERTIES CXX_MODULE_STD ON)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(medui_noheap_spec PRIVATE /experimental:module /std:c++latest)
endif()

mdux_discover_tests(medui_noheap_spec)

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

# The same scan over the screen runtime's objects (issue #199). The profile is named `ml-noheap` for
# historical reasons and what it forbids is not ML-specific - the allocation family and the throwing
# operators - so it is reused rather than copied under a second name. Renaming it would touch the ML
# wiring in a PR about screens; worth doing, separately.
set(mdux_screen_object_list "${CMAKE_CURRENT_BINARY_DIR}/screen_objects_$<CONFIG>.txt")
file(GENERATE
    OUTPUT "${mdux_screen_object_list}"
    CONTENT "$<JOIN:$<FILTER:$<TARGET_OBJECTS:MduXCore>,INCLUDE,/medui/>,\n>"
)

# The same mechanism over the whole governed target (issue #116), with the forbidden set narrowed
# to the throw expression alone. No filter: every MduXCore object, module interfaces included.
#
# The allocator symbols cannot come along. mdux.evidence.json and mdux.governance allocate by
# design - they build strings and vectors - so the ml-noheap forbidden set applied here would fail
# on correct code. What generalises is the no-throwing half, which is a property ADR-005 asserts of
# the whole governed zone rather than only of the ML modules.
set(mdux_governed_object_list "${CMAKE_CURRENT_BINARY_DIR}/governed_objects_$<CONFIG>.txt")
file(GENERATE
    OUTPUT "${mdux_governed_object_list}"
    CONTENT "$<JOIN:$<TARGET_OBJECTS:MduXCore>,\n>"
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

# Whether the standard library leaves its own throw sites indistinguishable from a source `throw`
# in the object. That is a property of the *standard library*, not of the platform: libc++ routes
# its string/vector failure paths straight through `__cxa_throw`, which is the same symbol a
# hand-written throw emits, while libstdc++ uses out-of-line helpers that the scan can tell apart.
# Keying this on APPLE happened to be right for the one macOS leg and wrong in principle - a Linux
# Clang build with -stdlib=libc++ would gate on libc++'s own throws and fail for a reason that is
# not a defect. Ask the compiler instead. See cmake/MduXNoHeapScan.cmake's header.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <version>
#ifndef _LIBCPP_VERSION
#error not libc++
#endif
int main() { return 0; }
" MDUX_STDLIB_IS_LIBCXX)

if(MDUX_SYMBOL_TOOL)
    add_test(NAME screen.noheap.symbolScan
        COMMAND ${CMAKE_COMMAND}
            -DMDUX_SCAN_PROFILE=ml-noheap
            -DMDUX_NOHEAP_OBJECT_LIST=${mdux_screen_object_list}
            -DMDUX_NOHEAP_TOOL=${mdux_symbol_tool_kind}
            -DMDUX_NOHEAP_COMMAND=${MDUX_SYMBOL_TOOL}
            -P ${CMAKE_SOURCE_DIR}/cmake/MduXNoHeapScan.cmake
    )
    set_tests_properties(screen.noheap.symbolScan PROPERTIES LABELS "noheap")

    add_test(NAME ml.noheap.symbolScan
        COMMAND ${CMAKE_COMMAND}
            -DMDUX_SCAN_PROFILE=ml-noheap
            -DMDUX_NOHEAP_OBJECT_LIST=${mdux_ml_object_list}
            -DMDUX_NOHEAP_TOOL=${mdux_symbol_tool_kind}
            -DMDUX_NOHEAP_COMMAND=${MDUX_SYMBOL_TOOL}
            -P ${CMAKE_SOURCE_DIR}/cmake/MduXNoHeapScan.cmake
    )
    set_tests_properties(ml.noheap.symbolScan PROPERTIES LABELS "noheap")

    # Issue #116. Labelled `governed` rather than `noheap`: it is not a no-heap check, and a CI leg
    # selecting -L noheap should not silently start covering something else.
    add_test(NAME governed.noThrow.symbolScan
        COMMAND ${CMAKE_COMMAND}
            -DMDUX_SCAN_PROFILE=governed-throw
            -DMDUX_NOHEAP_OBJECT_LIST=${mdux_governed_object_list}
            -DMDUX_NOHEAP_TOOL=${mdux_symbol_tool_kind}
            -DMDUX_NOHEAP_COMMAND=${MDUX_SYMBOL_TOOL}
            -DMDUX_THROW_SYMBOLS_AMBIGUOUS=${MDUX_STDLIB_IS_LIBCXX}
            -P ${CMAKE_SOURCE_DIR}/cmake/MduXNoHeapScan.cmake
    )
    set_tests_properties(governed.noThrow.symbolScan PROPERTIES LABELS "governed")

    # The negative fixture. See tests/governed/ThrowFixture.cpp for why a scan that only ever runs
    # against conforming code is not yet evidence of anything.
    #
    # MSVC and macOS/libc++ are excluded because their standard-library throw sites use the same
    # object symbol as a hand-written throw (`_CxxThrowException` or `__cxa_throw`). The profile
    # therefore forbids nothing there, and a negative test needs something to be forbidden before
    # it can prove rejection works. mdux-governed-lint is the source-level gate on those legs.
    #
    # CXX_SCAN_FOR_MODULES OFF keeps this object out of the module dependency graph: it declares no
    # module and imports none, and a fixture whose job is to be non-conforming has no business in
    # anyone's dyndep.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR MDUX_STDLIB_IS_LIBCXX)
        message(STATUS
            "governed.noThrow.symbolScan.negative not registered on this standard library - the "
            "governed-throw profile cannot distinguish library and source throws, so there is nothing for a negative test to "
            "prove. mdux-governed-lint covers the rule on this toolchain.")
    else()
        add_library(mdux_throw_fixture OBJECT governed/ThrowFixture.cpp)
        set_target_properties(mdux_throw_fixture
            PROPERTIES
                CXX_STANDARD 23
                CXX_STANDARD_REQUIRED ON
                CXX_EXTENSIONS OFF
                CXX_SCAN_FOR_MODULES OFF
        )

        set(mdux_throw_fixture_object_list
            "${CMAKE_CURRENT_BINARY_DIR}/throw_fixture_objects_$<CONFIG>.txt")
        file(GENERATE
            OUTPUT "${mdux_throw_fixture_object_list}"
            CONTENT "$<JOIN:$<TARGET_OBJECTS:mdux_throw_fixture>,\n>"
        )

        add_test(NAME governed.noThrow.symbolScan.negative
            COMMAND ${CMAKE_COMMAND}
                -DMDUX_SCAN_PROFILE=governed-throw
                -DMDUX_NOHEAP_OBJECT_LIST=${mdux_throw_fixture_object_list}
                -DMDUX_NOHEAP_TOOL=${mdux_symbol_tool_kind}
                -DMDUX_NOHEAP_COMMAND=${MDUX_SYMBOL_TOOL}
                -P ${CMAKE_SOURCE_DIR}/cmake/MduXNoHeapScan.cmake
        )
        # PASS_REGULAR_EXPRESSION rather than WILL_FAIL, and the two must not be combined. When
        # PASS_REGULAR_EXPRESSION is set CTest ignores the exit status and passes the test iff the
        # regex matches, so it already accommodates the expected non-zero exit; adding WILL_FAIL
        # then *inverts* that verdict and turns the correct outcome into a reported failure
        # (observed).
        #
        # Matching the message rather than merely the exit status is what makes this a real
        # negative test. A bare WILL_FAIL would be satisfied by a missing object, a misspelled
        # profile or a bad path - every way of failing except the one being verified.
        set_tests_properties(governed.noThrow.symbolScan.negative
            PROPERTIES
                PASS_REGULAR_EXPRESSION "Symbol scan violation \\(profile 'governed-throw'\\)"
                LABELS "governed"
        )
    endif()
else()
    # Deliberately not silent. A missing tool means these layers are not running, and nothing else
    # covers what they cover.
    message(WARNING
        "No symbol tool (nm/dumpbin) found - ml.noheap.symbolScan and governed.noThrow.symbolScan "
        "will not run, so issue #63's layer 2 and issue #116's object-level no-throw check are "
        "both unverified in this build.")
endif()
