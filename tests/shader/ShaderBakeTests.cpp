/**
 * @file ShaderBakeTests.cpp
 * @brief BDD scenarios for the shader baker's recipe model and bake/verify core, converted from
 * the Wave 3 MduXTest suite (issue #141).
 *
 * These drive `run()`, `write()` and `verify()` as calls rather than spawning mdux-shaderbake,
 * which is why the library and the executable are separate targets: a failing assertion here says
 * which artifact differed and at which byte, not that an exit status was 1.
 *
 * The verify cases are the ones that matter most. ADR-007's whole argument rests on bake and
 * verify being one code path, so the tests that earn that claim are the ones proving verify
 * *fails* - on a changed byte, a changed length, and a missing file.
 *
 * Conversion rule from the issue: a REQUIRE stays a hard failure (thrown AssertionFailure) and a
 * CHECK becomes a collected expectation (`mdux::spec::Checks`). Sizes that guard indexing stay
 * hard, so a wrong size throws rather than letting the checks read out of bounds.
 */
import std;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.shader.schema;
import mdux.tools.cli;
import mdux.tools.shaderbake;
import speclab;

#include "../framework/SpecLabBridge.hpp"
#include "SpirvFixtures.hpp"

namespace {

using namespace mdux::tools::shaderbake;
using namespace mdux::test::spirv;
using speclab::core::Assertions;

namespace cli = mdux::tools::cli;
namespace shader = mdux::shader;
namespace evidence = mdux::evidence;

/// A temporary directory that removes itself, so a failing test cannot leave the tree dirty -
/// which CI's `git status --porcelain` gate would report as a source-tree write.
class TempDir {
public:
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("mdux-shaderbake-test-" + std::to_string(stamp) + "-" +
                 std::to_string(counter_++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    static inline int counter_ = 0;
};

void writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    writeBytes(path, std::as_bytes(std::span{text.data(), text.size()}));
}

[[nodiscard]] std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    return bytes.value_or(std::vector<std::byte>{});
}

/// The set of diagnostic codes reported, for asserting on the code rather than the wording.
[[nodiscard]] std::vector<std::string> codesOf(const std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<std::string> codes;
    for (const cli::Diagnostic& diagnostic : diagnostics) {
        codes.push_back(diagnostic.code);
    }
    return codes;
}

[[nodiscard]] bool contains(const std::vector<std::string>& codes, std::string_view code) {
    return std::ranges::find(codes, code) != codes.end();
}

/// A repository-shaped temp tree holding a vertex and a fragment module plus its recipe.
struct Fixture {
    TempDir dir;
    std::string recipeRelative{"recipes/shader/test.toml"};
    std::vector<std::byte> recipeBytes;
    Recipe recipe;

    Fixture() {
        Builder vertex = minimal(executionModelVertex);
        addVec4PushConstant(vertex);
        Builder fragment = minimal(executionModelFragment);
        addCombinedImageSampler(fragment, 0, 0);

        writeBytes(dir.path() / "shaders/test.vert.spv", vertex.bytes());
        writeBytes(dir.path() / "shaders/test.frag.spv", fragment.bytes());

        const std::string text =
            "[package]\n"
            "id = \"test-ui\"\n"
            "sidecar = \"shaders.spv\"\n"
            "\n"
            "[modules]\n"
            "ids = [\"test.vert\", \"test.frag\"]\n"
            "sources = [\"shaders/test.vert.spv\", \"shaders/test.frag.spv\"]\n";
        writeText(dir.path() / recipeRelative, text);
        recipeBytes = readBytes(dir.path() / recipeRelative);

        std::vector<cli::Diagnostic> diagnostics;
        auto parsed = parseRecipe(text, recipeRelative, diagnostics);
        recipe = parsed.value_or(Recipe{});
    }

    [[nodiscard]] std::optional<BakeOutputs> bake(std::vector<cli::Diagnostic>& diagnostics) const {
        return run(recipe, recipeRelative, recipeBytes, dir.path(), diagnostics);
    }
};

// ---------------------------------------------------------------------------
// Recipe parsing
// ---------------------------------------------------------------------------

/// A recipe parsed from text: the diagnostics parseRecipe() emitted, and the recipe if it held.
struct ParsedRecipeState {
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<Recipe> recipe;
};

const mdux::spec::Register wellFormedRecipeParses{
    "A well-formed recipe parses into modules in declaration order", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-well-formed-recipe-parses")
            .Given("a well-formed recipe declaring two modules in order",
                   [](ParsedRecipeState& state) {
                       state.recipe = parseRecipe(
                           "[package]\nid = \"mdux-ui\"\n\n[modules]\nids = [\"a\", \"b\"]\n"
                           "sources = [\"x.spv\", \"y.spv\"]\n",
                           "r.toml", state.diagnostics);
                       Assertions::require(state.recipe.has_value(), "a well-formed recipe failed to parse");
                   })
            .When("it is parsed", [] {})
            .Then("it parses into modules in declaration order",
                  [](ParsedRecipeState& state) {
                      Assertions::require(state.recipe->modules.size() == 2, "expected 2 modules, got {}", state.recipe->modules.size());
                      mdux::spec::Checks checks;
                      checks.expect(state.diagnostics.empty(), "no diagnostics");
                      checks.expect(state.recipe->id == "mdux-ui", "the package id");
                      // The default is expanded here rather than at the point of use, so
                      // report.json records what the bake actually did - ADR-007's rule about
                      // silently changed defaults.
                      checks.expect(state.recipe->sidecar == "shaders.spv",
                                    "the default sidecar name");
                      checks.expect(state.recipe->modules[0].id == "a",
                                    "the first module id");
                      checks.expect(state.recipe->modules[0].source == "x.spv",
                                    "the first module source");
                      checks.expect(state.recipe->modules[1].id == "b",
                                    "the second module id");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register explicitSidecarOverrides{
    "An explicit sidecar name overrides the default", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-explicit-sidecar-overrides-default")
            .Given("a recipe naming its sidecar explicitly",
                   [](ParsedRecipeState& state) {
                       state.recipe = parseRecipe(
                           "[package]\nid = \"x\"\nsidecar = \"blob.bin\"\n\n[modules]\n"
                           "ids = [\"a\"]\nsources = [\"x.spv\"]\n",
                           "r.toml", state.diagnostics);
                       Assertions::require(state.recipe.has_value(), "a recipe with an explicit sidecar failed to parse");
                   })
            .When("its sidecar name is read", [] {})
            .Then("the explicit name wins over the default",
                  [](ParsedRecipeState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(state.recipe->sidecar == "blob.bin",
                                    "the sidecar name is blob.bin");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unparseableTomlReported{
    "Unparseable TOML is reported with its line", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-unparseable-toml-line")
            .Given("a recipe whose TOML does not parse",
                   [](ParsedRecipeState& state) {
                       state.recipe = parseRecipe("[package\nid = \"x\"\n", "r.toml",
                                                   state.diagnostics);
                   })
            .When("it is parsed", [] {})
            .Then("parsing fails and the diagnostic names the line",
                  [](ParsedRecipeState& state) {
                      Assertions::require(state.diagnostics.size() == 1, "expected 1 diagnostic, got {}", state.diagnostics.size());
                      mdux::spec::Checks checks;
                      checks.expect(!state.recipe.has_value(),
                                    "the recipe has no value");
                      checks.expect(state.diagnostics[0].code == "SHB001",
                                    "the code is SHB001");
                      checks.expect(state.diagnostics[0].file == "r.toml",
                                    "the file is r.toml");
                      checks.expect(state.diagnostics[0].line != 0,
                                    "a line is reported");
                      checks.raise();
                  })
            .Execute();
    }};

struct MissingTableOrKeyReportedState {
    bool missingPackageTable{false};
    bool missingModulesTable{false};
    bool missingKeys{false};
    std::vector<std::string> firstCodes;
    std::vector<std::string> secondCodes;
    std::vector<std::string> thirdCodes;
};

const mdux::spec::Register missingTableOrKeyReported{
    "A recipe missing a required table or key is reported", "evidence-unit", [] {
        return speclab::Test<MissingTableOrKeyReportedState>("shader-bake-missing-table-or-key")
            .Given("three recipes each missing a required table or key",
                   [](MissingTableOrKeyReportedState& state) {
                       std::vector<cli::Diagnostic> diagnostics;
                       state.missingPackageTable =
                           !parseRecipe("[modules]\nids = []\nsources = []\n", "r.toml",
                                        diagnostics)
                                .has_value();
                       state.firstCodes = codesOf(diagnostics);

                       diagnostics.clear();
                       state.missingModulesTable =
                           !parseRecipe("[package]\nid = \"x\"\n", "r.toml", diagnostics)
                                .has_value();
                       state.secondCodes = codesOf(diagnostics);

                       diagnostics.clear();
                       state.missingKeys =
                           !parseRecipe("[package]\nsidecar = \"s.spv\"\n\n[modules]\n"
                                        "ids = [\"a\"]\nsources = [\"x.spv\"]\n",
                                        "r.toml", diagnostics)
                                .has_value();
                       state.thirdCodes = codesOf(diagnostics);
                   })
            .When("each is parsed", [] {})
            .Then("each is rejected with SHB002",
                  [](MissingTableOrKeyReportedState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(state.missingPackageTable,
                                    "a missing [package] table is rejected");
                      checks.expect(contains(state.firstCodes, "SHB002"),
                                    "SHB002 for the missing [package] table");
                      checks.expect(state.missingModulesTable,
                                    "a missing [modules] table is rejected");
                      checks.expect(contains(state.secondCodes, "SHB002"),
                                    "SHB002 for the missing [modules] table");
                      checks.expect(state.missingKeys,
                                    "missing ids/sources keys are rejected");
                      checks.expect(contains(state.thirdCodes, "SHB002"),
                                    "SHB002 for the missing keys");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register mismatchedArraysRejected{
    "Parallel arrays of different lengths are rejected by name", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-mismatched-parallel-arrays")
            .Given("a recipe whose ids and sources arrays differ in length",
                   [](ParsedRecipeState& state) {
                       // The failure mode this format is most exposed to: the two arrays are
                       // positional, so a mismatch would otherwise silently drop or misname a
                       // module.
                       state.recipe = parseRecipe(
                           "[package]\nid = \"x\"\n\n[modules]\nids = [\"a\", \"b\"]\n"
                           "sources = [\"x.spv\"]\n",
                           "r.toml", state.diagnostics);
                   })
            .When("it is parsed", [] {})
            .Then("it is rejected by name with SHB003",
                  [](ParsedRecipeState& state) {
                      Assertions::require(state.diagnostics.size() == 1, "expected 1 diagnostic, got {}", state.diagnostics.size());
                      mdux::spec::Checks checks;
                      checks.expect(!state.recipe.has_value(),
                                    "the recipe has no value");
                      checks.expect(state.diagnostics[0].code == "SHB003",
                                    "the code is SHB003");
                      checks.expect(state.diagnostics[0].line != 0,
                                    "a line is reported");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register noModulesRejected{
    "A recipe declaring no modules is rejected", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-no-modules-rejected")
            .Given("a recipe declaring no modules",
                   [](ParsedRecipeState& state) {
                       state.recipe = parseRecipe(
                           "[package]\nid = \"x\"\n\n[modules]\nids = []\nsources = []\n",
                           "r.toml", state.diagnostics);
                   })
            .When("it is parsed", [] {})
            .Then("it is rejected with SHB004",
                  [](ParsedRecipeState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(!state.recipe.has_value(),
                                    "the recipe has no value");
                      checks.expect(contains(codesOf(state.diagnostics), "SHB004"),
                                    "SHB004 is reported");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register duplicateModuleIdsRejected{
    "Duplicate module ids are rejected", "evidence-unit", [] {
        return speclab::Test<ParsedRecipeState>("shader-bake-duplicate-module-ids")
            .Given("a recipe declaring the same module id twice",
                   [](ParsedRecipeState& state) {
                       state.recipe = parseRecipe(
                           "[package]\nid = \"x\"\n\n[modules]\nids = [\"a\", \"a\"]\n"
                           "sources = [\"x.spv\", \"y.spv\"]\n",
                           "r.toml", state.diagnostics);
                   })
            .When("it is parsed", [] {})
            .Then("it is rejected with SHB012",
                  [](ParsedRecipeState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(!state.recipe.has_value(),
                                    "the recipe has no value");
                      checks.expect(contains(codesOf(state.diagnostics), "SHB012"),
                                    "SHB012 is reported");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

/// A bake run over a fixture tree: what run() reported and what it produced.
struct BakedFixtureState {
    Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
    std::optional<shader::ShaderPackage> package;
};

const mdux::spec::Register bakeProducesPackageReportSidecar{
    "A bake produces a package, a report and a sidecar", "evidence-unit", [] {
        return speclab::Test<BakedFixtureState>("shader-bake-produces-package-report-sidecar")
            .Given("a repository-shaped fixture with a vertex and a fragment module", [] {})
            .When("the fixture is baked and its package parsed",
                  [](BakedFixtureState& state) {
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                      auto parsed = shader::ShaderPackage::parse(state.outputs->packageJson);
                      Assertions::require(parsed.has_value(), "the package JSON did not parse");
                      state.package = *parsed;
                  })
            .Then("it records the package id, both modules and the sidecar digest",
                  [](BakedFixtureState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(state.diagnostics.empty(), "no diagnostics");
                      checks.expect(state.outputs->packageId == "test-ui",
                                    "the package id");
                      checks.expect(state.outputs->moduleCount == 2,
                                    "the module count is 2");
                      checks.expect(state.outputs->sidecarName == "shaders.spv",
                                    "the sidecar name");
                      checks.expect(!state.outputs->sidecar.empty(),
                                    "the sidecar is not empty");
                      checks.expect(state.package->header.id == "test-ui",
                                    "the header id");
                      checks.expect(state.package->header.kind == "shader",
                                    "the header kind");
                      checks.expect(state.package->sidecarByteLength ==
                                        state.outputs->sidecar.size(),
                                    "the recorded sidecar length matches");
                      checks.expect(state.package->sidecarSha256 ==
                                        evidence::sha256(state.outputs->sidecar),
                                    "the recorded sidecar digest matches");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register modulesConcatenatedInRecipeOrder{
    "Modules are concatenated in recipe order with contiguous ranges", "evidence-unit", [] {
        return speclab::Test<BakedFixtureState>("shader-bake-modules-concatenated-in-order")
            .Given("a fixture whose recipe lists the vertex module before the fragment",
                   [](BakedFixtureState& state) {
                       // Recipe order rather than directory order: the latter differs between
                       // filesystems and would break byte-identity between two developers on the
                       // same commit.
                       state.outputs = state.fixture.bake(state.diagnostics);
                       Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                   })
            .When("the package is parsed",
                  [](BakedFixtureState& state) {
                      auto parsed = shader::ShaderPackage::parse(state.outputs->packageJson);
                      Assertions::require(parsed.has_value(), "the package JSON did not parse");
                      state.package = *parsed;
                  })
            .Then("the modules are contiguous in recipe order",
                  [](BakedFixtureState& state) {
                      Assertions::require(state.package->modules.size() == 2, "expected 2 modules, got {}", state.package->modules.size());
                      mdux::spec::Checks checks;
                      checks.expect(state.package->modules[0].id == "test.vert",
                                    "the first module id");
                      checks.expect(state.package->modules[0].stage == shader::Stage::Vertex,
                                    "the first module stage");
                      checks.expect(state.package->modules[0].byteOffset == 0,
                                    "the first module offset");
                      checks.expect(state.package->modules[1].id == "test.frag",
                                    "the second module id");
                      checks.expect(state.package->modules[1].stage == shader::Stage::Fragment,
                                    "the second module stage");
                      checks.expect(state.package->modules[1].byteOffset ==
                                        state.package->modules[0].byteLength,
                                    "the second module follows the first");
                      checks.expect(state.package->modules[1].byteEnd() ==
                                        state.outputs->sidecar.size(),
                                    "the sidecar ends with the last module");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register moduleDigestCoversOwnRange{
    "A module's recorded digest covers exactly its own range", "evidence-unit", [] {
        return speclab::Test<BakedFixtureState>("shader-bake-module-digest-own-range")
            .Given("a baked fixture",
                   [](BakedFixtureState& state) {
                       state.outputs = state.fixture.bake(state.diagnostics);
                       Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                       auto parsed = shader::ShaderPackage::parse(state.outputs->packageJson);
                       Assertions::require(parsed.has_value(), "the package JSON did not parse");
                       state.package = *parsed;
                   })
            .When("each module's range is re-digested", [] {})
            .Then("each recorded digest matches its own range",
                  [](BakedFixtureState& state) {
                      mdux::spec::Checks checks;
                      for (const shader::ShaderModule& module : state.package->modules) {
                          const std::span<const std::byte> range{
                              state.outputs->sidecar.data() + module.byteOffset,
                              static_cast<std::size_t>(module.byteLength)};
                          checks.expect(module.sha256 == evidence::sha256(range),
                                        "the digest covers the module's own range");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register stagesCarriedByBindings{
    "Descriptors and push constants carry the stages that declared them", "evidence-unit", [] {
        return speclab::Test<BakedFixtureState>("shader-bake-stages-carried-by-bindings")
            .Given("a fixture with a vertex push constant and a fragment sampler",
                   [](BakedFixtureState& state) {
                       state.outputs = state.fixture.bake(state.diagnostics);
                       Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                       auto parsed = shader::ShaderPackage::parse(state.outputs->packageJson);
                       Assertions::require(parsed.has_value(), "the package JSON did not parse");
                       state.package = *parsed;
                   })
            .When("the package metadata is read", [] {})
            .Then("each binding carries the stage that declared it",
                  [](BakedFixtureState& state) {
                      Assertions::require(state.package->descriptors.size() == 1, "expected 1 descriptor, got {}", state.package->descriptors.size());
                      Assertions::require(state.package->pushConstants.size() == 1, "expected 1 push constant, got {}", state.package->pushConstants.size());
                      mdux::spec::Checks checks;
                      checks.expect(state.package->descriptors[0].kind ==
                                        shader::DescriptorKind::CombinedImageSampler,
                                    "the descriptor kind");
                      checks.expect(state.package->descriptors[0].stages ==
                                        shader::fragmentBit,
                                    "the descriptor is fragment-only");
                      checks.expect(state.package->pushConstants[0].size == 16,
                                    "the push constant is 16 bytes");
                      checks.expect(state.package->pushConstants[0].stages ==
                                        shader::vertexBit,
                                    "the push constant is vertex-only");
                      checks.raise();
                  })
            .Execute();
    }};

struct BindingSharedByBothStagesState {
    TempDir dir;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
    std::optional<shader::ShaderPackage> package;
};

const mdux::spec::Register bindingSharedByBothStages{
    "A binding declared by both stages becomes one binding visible to both", "evidence-unit", [] {
        return speclab::Test<BindingSharedByBothStagesState>("shader-bake-binding-shared-by-both-stages")
            .Given("a vertex and a fragment module declaring the same binding",
                   [](BindingSharedByBothStagesState& state) {
                       Builder vertex = minimal(executionModelVertex);
                       addCombinedImageSampler(vertex, 0, 0);
                       Builder fragment = minimal(executionModelFragment);
                       addCombinedImageSampler(fragment, 0, 0);
                       writeBytes(state.dir.path() / "v.spv", vertex.bytes());
                       writeBytes(state.dir.path() / "f.spv", fragment.bytes());

                       const Recipe recipe{.id = "shared",
                                           .sidecar = "shaders.spv",
                                           .modules = {{.id = "v", .source = "v.spv"},
                                                       {.id = "f", .source = "f.spv"}}};
                       state.outputs =
                           run(recipe, "r.toml", {}, state.dir.path(), state.diagnostics);
                       Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                   })
            .When("the package is parsed",
                  [](BindingSharedByBothStagesState& state) {
                      auto parsed = shader::ShaderPackage::parse(state.outputs->packageJson);
                      Assertions::require(parsed.has_value(), "the package JSON did not parse");
                      state.package = *parsed;
                  })
            .Then("there is one binding visible to both stages",
                  [](BindingSharedByBothStagesState& state) {
                      Assertions::require(state.package->descriptors.size() == 1, "expected 1 descriptor, got {}", state.package->descriptors.size());
                      mdux::spec::Checks checks;
                      checks.expect(state.package->descriptors[0].stages ==
                                        (shader::vertexBit | shader::fragmentBit),
                                    "the binding is visible to both stages");
                      checks.raise();
                  })
            .Execute();
    }};

/// A bake expected to be refused: the directory it ran in and the diagnostics it reported.
struct BakeRefusalState {
    TempDir dir;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
};

const mdux::spec::Register conflictingBindingsRejected{
    "Two stages disagreeing about a binding is an error, not a silent choice", "evidence-unit", [] {
        return speclab::Test<BakeRefusalState>("shader-bake-conflicting-bindings")
            .Given("two stages declaring the same binding with different array counts",
                   [](BakeRefusalState& state) {
                       // Taking either declaration would produce a pipeline layout that matches
                       // one shader and not the other, which the driver reports much later and
                       // much less clearly.
                       Builder vertex = minimal(executionModelVertex);
                       addCombinedImageSampler(vertex, 0, 0, 1);
                       Builder fragment = minimal(executionModelFragment);
                       addCombinedImageSampler(fragment, 0, 0, 4);
                       writeBytes(state.dir.path() / "v.spv", vertex.bytes());
                       writeBytes(state.dir.path() / "f.spv", fragment.bytes());

                       const Recipe recipe{.id = "conflict",
                                           .sidecar = "shaders.spv",
                                           .modules = {{.id = "v", .source = "v.spv"},
                                                       {.id = "f", .source = "f.spv"}}};
                       state.outputs =
                           run(recipe, "r.toml", {}, state.dir.path(), state.diagnostics);
                   })
            .When("the bake runs", [] {})
            .Then("it fails and reports SHB007",
                  [](BakeRefusalState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(!state.outputs.has_value(),
                                    "the bake produced no outputs");
                      checks.expect(contains(codesOf(state.diagnostics), "SHB007"),
                                    "SHB007 is reported");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unreadableSourceReported{
    "An unreadable source is reported against the module that named it", "evidence-unit", [] {
        return speclab::Test<BakeRefusalState>("shader-bake-unreadable-source")
            .Given("a recipe naming a source file that does not exist",
                   [](BakeRefusalState& state) {
                       const Recipe recipe{.id = "missing",
                                           .sidecar = "shaders.spv",
                                           .modules = {{.id = "v", .source = "gone.spv"}}};
                       state.outputs =
                           run(recipe, "r.toml", {}, state.dir.path(), state.diagnostics);
                   })
            .When("the bake runs", [] {})
            .Then("it fails and names the module's file",
                  [](BakeRefusalState& state) {
                      Assertions::require(state.diagnostics.size() == 1, "expected 1 diagnostic, got {}", state.diagnostics.size());
                      mdux::spec::Checks checks;
                      checks.expect(!state.outputs.has_value(),
                                    "the bake produced no outputs");
                      checks.expect(state.diagnostics[0].code == "SHB005",
                                    "the code is SHB005");
                      checks.expect(state.diagnostics[0].file == "gone.spv",
                                    "the diagnostic names gone.spv");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register nonSpirvSourceReported{
    "A source that is not SPIR-V is reported with the reason", "evidence-unit", [] {
        return speclab::Test<BakeRefusalState>("shader-bake-non-spirv-source")
            .Given("a source file that is not SPIR-V",
                   [](BakeRefusalState& state) {
                       writeText(state.dir.path() / "bad.spv", "this is not a shader");
                       const Recipe recipe{.id = "bad",
                                           .sidecar = "shaders.spv",
                                           .modules = {{.id = "v", .source = "bad.spv"}}};
                       state.outputs =
                           run(recipe, "r.toml", {}, state.dir.path(), state.diagnostics);
                   })
            .When("the bake runs", [] {})
            .Then("it fails and carries the reflector's reason",
                  [](BakeRefusalState& state) {
                      Assertions::require(state.diagnostics.size() == 1, "expected 1 diagnostic, got {}", state.diagnostics.size());
                      mdux::spec::Checks checks;
                      checks.expect(!state.outputs.has_value(),
                                    "the bake produced no outputs");
                      checks.expect(state.diagnostics[0].code == "SHB006",
                                    "the code is SHB006");
                      // The reflector's reason is carried through rather than flattened to
                      // "invalid".
                      checks.expect(state.diagnostics[0].message.find("magic") !=
                                        std::string::npos,
                                    "the message names the missing magic");
                      checks.raise();
                  })
            .Execute();
    }};

struct ReportRecordsRecipeInputsOutputsState {
    Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
    std::optional<evidence::BakeReport> report;
};

const mdux::spec::Register reportRecordsRecipeInputsOutputs{
    "The report records the recipe, every input, and both outputs", "evidence-unit", [] {
        return speclab::Test<ReportRecordsRecipeInputsOutputsState>("shader-bake-report-records-everything")
            .Given("a baked fixture",
                   [](ReportRecordsRecipeInputsOutputsState& state) {
                       state.outputs = state.fixture.bake(state.diagnostics);
                       Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                       auto parsed = evidence::BakeReport::parse(state.outputs->reportJson);
                       Assertions::require(parsed.has_value(), "the report JSON did not parse");
                       state.report = *parsed;
                   })
            .When("the report is read", [] {})
            .Then("it records the recipe, every input, and both outputs",
                  [](ReportRecordsRecipeInputsOutputsState& state) {
                      Assertions::require(state.report->inputs.size() == 2, "expected 2 inputs, got {}", state.report->inputs.size());
                      Assertions::require(state.report->outputs.size() == 2, "expected 2 outputs, got {}", state.report->outputs.size());
                      mdux::spec::Checks checks;
                      checks.expect(state.report->tool == "mdux-shaderbake",
                                    "the tool is mdux-shaderbake");
                      checks.expect(!state.report->toolVersion.empty(),
                                    "a tool version is recorded");
                      checks.expect(state.report->recipe.path == state.fixture.recipeRelative,
                                    "the recipe path is recorded");
                      checks.expect(state.report->recipe.sha256 ==
                                        evidence::sha256(state.fixture.recipeBytes),
                                    "the recipe digest is recorded");
                      checks.expect(state.report->inputs[0].path ==
                                        "shaders/test.vert.spv",
                                    "the first input is the vertex module");
                      checks.expect(state.report->outputs[0].path == "package.json",
                                    "the first output is package.json");
                      checks.expect(state.report->outputs[1].path == "shaders.spv",
                                    "the second output is the sidecar");
                      // report.json is deliberately not one of its own outputs: a file cannot
                      // carry its own digest.
                      for (const evidence::FileRecord& output : state.report->outputs) {
                          checks.expect(output.path != "report.json",
                                        "report.json is not one of its own outputs");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

struct BakeReproducibleByteForByteState {
    Fixture fixture;
    std::vector<cli::Diagnostic> first;
    std::vector<cli::Diagnostic> second;
    std::optional<BakeOutputs> a;
    std::optional<BakeOutputs> b;
};

const mdux::spec::Register bakeReproducibleByteForByte{
    "A bake is reproducible byte for byte", "evidence-unit", [] {
        return speclab::Test<BakeReproducibleByteForByteState>("shader-bake-reproducible-byte-for-byte")
            .Given("a fixture",
                   [](BakeReproducibleByteForByteState& state) {
                       // The property the whole evidence pipeline rests on, asserted at the
                       // smallest scale that can demonstrate it: same inputs, same bytes, twice
                       // in one process.
                       state.a = state.fixture.bake(state.first);
                       state.b = state.fixture.bake(state.second);
                       if (!state.a.has_value() || !state.b.has_value()) {
                           Assertions::fail("one of the two bakes did not produce outputs");
                       }
                   })
            .When("the same inputs are baked twice", [] {})
            .Then("both bakes are byte-identical",
                  [](BakeReproducibleByteForByteState& state) {
                      mdux::spec::Checks checks;
                      checks.expect(state.a->packageJson == state.b->packageJson,
                                    "the package JSON matches");
                      checks.expect(state.a->reportJson == state.b->reportJson,
                                    "the report JSON matches");
                      checks.expect(state.a->sidecar == state.b->sidecar,
                                    "the sidecar matches");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// write() and verify()
// ---------------------------------------------------------------------------

struct WriteVerifyRoundTripState {
    Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
    bool cleanAfterWrite{false};
};

const mdux::spec::Register writeVerifyRoundTrip{
    "What write() writes, verify() accepts", "evidence-unit", [] {
        return speclab::Test<WriteVerifyRoundTripState>("shader-bake-write-verify-round-trip")
            .Given("a baked fixture", [] {})
            .When("the outputs are written",
                  [](WriteVerifyRoundTripState& state) {
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      Assertions::require(write(*state.outputs, outputDir, state.diagnostics), "write() failed on the baked outputs");
                      state.cleanAfterWrite = state.diagnostics.empty();
                  })
            .Then("verify() accepts everything that was written",
                  [](WriteVerifyRoundTripState& state) {
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      mdux::spec::Checks checks;
                      checks.expect(state.cleanAfterWrite,
                                    "write produced no diagnostics");
                      checks.expect(std::filesystem::exists(outputDir / "package.json"),
                                    "package.json exists");
                      checks.expect(std::filesystem::exists(outputDir / "report.json"),
                                    "report.json exists");
                      checks.expect(std::filesystem::exists(outputDir / "shaders.spv"),
                                    "shaders.spv exists");
                      checks.expect(verify(*state.outputs, outputDir / "package.json",
                                           outputDir / "report.json", state.diagnostics),
                                    "verify accepts what write wrote");
                      checks.expect(state.diagnostics.empty(),
                                    "verify produced no diagnostics");
                      checks.raise();
                  })
            .Execute();
    }};

struct VerifyReportsDifferingByteState {
    Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
    std::size_t position{0};
};

const mdux::spec::Register verifyReportsDifferingByte{
    "verify() reports the first differing byte of a changed artifact", "evidence-unit", [] {
        return speclab::Test<VerifyReportsDifferingByteState>("shader-bake-verify-differing-byte")
            .Given("a baked fixture", [] {})
            .When("a byte of the committed package is changed",
                  [](VerifyReportsDifferingByteState& state) {
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      Assertions::require(write(*state.outputs, outputDir, state.diagnostics), "write() failed on the baked outputs");

                      // Change one character of the committed package without changing its length.
                      std::string committed;
                      {
                          auto bytes = readBytes(outputDir / "package.json");
                          committed.assign(reinterpret_cast<const char*>(bytes.data()),
                                           bytes.size());
                      }
                      state.position = committed.find("test-ui");
                      Assertions::require(state.position != std::string::npos, "the committed package did not contain the package id");
                      committed[state.position] = 'T';
                      writeText(outputDir / "package.json", committed);
                  })
            .Then("verify() reports the differing byte",
                  [](VerifyReportsDifferingByteState& state) {
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      state.diagnostics.clear();
                      mdux::spec::Checks checks;
                      checks.expect(!verify(*state.outputs, outputDir / "package.json",
                                            outputDir / "report.json", state.diagnostics),
                                    "verify rejects the changed package");
                      Assertions::require(!state.diagnostics.empty(), "verify produced no diagnostic for the changed package");
                      checks.expect(state.diagnostics[0].code == "SHB011",
                                    "the code is SHB011");
                      // The offset is what makes the diagnostic useful without a diff tool.
                      checks.expect(state.diagnostics[0].message.find(
                                        "byte " + std::to_string(state.position)) !=
                                        std::string::npos,
                                    "the message names the differing byte");
                      checks.raise();
                  })
            .Execute();
    }};

/// A verify run over a fixture tree: what verify() reported about the committed artifacts.
struct VerifiedFixtureState {
    Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    std::optional<BakeOutputs> outputs;
};

const mdux::spec::Register verifyReportsLengthChange{
    "verify() reports a length change", "evidence-unit", [] {
        return speclab::Test<VerifiedFixtureState>("shader-bake-verify-length-change")
            .Given("a baked fixture", [] {})
            .When("the committed package is shortened",
                  [](VerifiedFixtureState& state) {
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      Assertions::require(write(*state.outputs, outputDir, state.diagnostics), "write() failed on the baked outputs");

                      // A prefix of the real content: every byte compared matches, so only the
                      // length check fires.
                      writeText(outputDir / "package.json",
                                state.outputs->packageJson.substr(0, 10));
                  })
            .Then("verify() reports the length change",
                  [](VerifiedFixtureState& state) {
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      state.diagnostics.clear();
                      mdux::spec::Checks checks;
                      checks.expect(!verify(*state.outputs, outputDir / "package.json",
                                            outputDir / "report.json", state.diagnostics),
                                    "verify rejects the shortened package");
                      Assertions::require(!state.diagnostics.empty(), "verify produced no diagnostic for the shortened package");
                      checks.expect(state.diagnostics[0].code == "SHB011",
                                    "the code is SHB011");
                      checks.expect(state.diagnostics[0].message.find("length differs") !=
                                        std::string::npos,
                                    "the message names the length");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register verifyReportsMissingArtifact{
    "verify() reports a missing artifact", "evidence-unit", [] {
        return speclab::Test<VerifiedFixtureState>("shader-bake-verify-missing-artifact")
            .Given("a baked fixture", [] {})
            .When("verify() is asked about artifacts that were never written",
                  [](VerifiedFixtureState& state) {
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                  })
            .Then("verify() reports the missing artifact",
                  [](VerifiedFixtureState& state) {
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      state.diagnostics.clear();
                      mdux::spec::Checks checks;
                      checks.expect(!verify(*state.outputs, outputDir / "package.json",
                                            outputDir / "report.json", state.diagnostics),
                                    "verify rejects the missing artifacts");
                      Assertions::require(!state.diagnostics.empty(), "verify produced no diagnostic for the missing artifact");
                      checks.expect(state.diagnostics[0].code == "SHB010",
                                    "the code is SHB010");
                      checks.expect(state.diagnostics[0].fixHint.find("mdux-bake-update") !=
                                        std::string::npos,
                                    "the fix hint names mdux-bake-update");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register verifyComparesSidecar{
    "verify() compares the sidecar, not just the package that names it", "evidence-unit", [] {
        return speclab::Test<VerifiedFixtureState>("shader-bake-verify-compares-sidecar")
            .Given("a baked fixture", [] {})
            .When("the committed sidecar is corrupted",
                  [](VerifiedFixtureState& state) {
                      // A package whose recorded digest matches a sidecar nobody compared proves
                      // nothing: the whole point of the byte comparison is that it does not trust
                      // the artifact's own claims.
                      state.outputs = state.fixture.bake(state.diagnostics);
                      Assertions::require(state.outputs.has_value(), "the bake did not produce outputs");
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      Assertions::require(write(*state.outputs, outputDir, state.diagnostics), "write() failed on the baked outputs");

                      std::vector<std::byte> corrupted = state.outputs->sidecar;
                      const std::size_t last = corrupted.size() - 1;
                      corrupted[last] = static_cast<std::byte>(
                          std::to_integer<unsigned>(corrupted[last]) ^ 0xffu);
                      writeBytes(outputDir / "shaders.spv", corrupted);
                  })
            .Then("verify() reports the sidecar itself",
                  [](VerifiedFixtureState& state) {
                      const std::filesystem::path outputDir =
                          state.fixture.dir.path() / "generated/shader/test-ui";
                      state.diagnostics.clear();
                      mdux::spec::Checks checks;
                      checks.expect(!verify(*state.outputs, outputDir / "package.json",
                                            outputDir / "report.json", state.diagnostics),
                                    "verify rejects the corrupted sidecar");
                      Assertions::require(!state.diagnostics.empty(), "verify produced no diagnostic for the corrupted sidecar");
                      checks.expect(state.diagnostics[0].code == "SHB011",
                                    "the code is SHB011");
                      checks.expect(state.diagnostics[0].file.find("shaders.spv") !=
                                        std::string::npos,
                                    "the diagnostic names the sidecar");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
