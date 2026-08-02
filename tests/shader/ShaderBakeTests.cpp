/**
 * @file ShaderBakeTests.cpp
 * @brief Tests for the shader baker's recipe model and bake/verify core.
 *
 * These drive `run()`, `write()` and `verify()` as calls rather than spawning mdux-shaderbake,
 * which is why the library and the executable are separate targets: a failing assertion here says
 * which artifact differed and at which byte, not that an exit status was 1.
 *
 * The verify cases are the ones that matter most. ADR-007's whole argument rests on bake and
 * verify being one code path, so the tests that earn that claim are the ones proving verify
 * *fails* - on a changed byte, a changed length, and a missing file.
 */
import std;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.shader.schema;
import mdux.tools.cli;
import mdux.tools.shaderbake;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "SpirvFixtures.hpp"

namespace {

using namespace mdux::tools::shaderbake;
using namespace mdux::test::spirv;
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
        Builder vertex = minimal(kExecutionModelVertex);
        addVec4PushConstant(vertex);
        Builder fragment = minimal(kExecutionModelFragment);
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

}  // namespace

// ---------------------------------------------------------------------------
// Recipe parsing
// ---------------------------------------------------------------------------

TEST_CASE("A well-formed recipe parses into modules in declaration order", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe(
        "[package]\nid = \"mdux-ui\"\n\n[modules]\nids = [\"a\", \"b\"]\n"
        "sources = [\"x.spv\", \"y.spv\"]\n",
        "r.toml", diagnostics);
    REQUIRE(recipe.has_value());
    CHECK(diagnostics.empty());
    CHECK(recipe->id == "mdux-ui");
    // The default is expanded here rather than at the point of use, so report.json records what
    // the bake actually did - ADR-007's rule about silently changed defaults.
    CHECK(recipe->sidecar == "shaders.spv");
    REQUIRE(recipe->modules.size() == 2);
    CHECK(recipe->modules[0].id == "a");
    CHECK(recipe->modules[0].source == "x.spv");
    CHECK(recipe->modules[1].id == "b");
}

TEST_CASE("An explicit sidecar name overrides the default", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe(
        "[package]\nid = \"x\"\nsidecar = \"blob.bin\"\n\n[modules]\nids = [\"a\"]\n"
        "sources = [\"x.spv\"]\n",
        "r.toml", diagnostics);
    REQUIRE(recipe.has_value());
    CHECK(recipe->sidecar == "blob.bin");
}

TEST_CASE("Unparseable TOML is reported with its line", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe("[package\nid = \"x\"\n", "r.toml", diagnostics);
    CHECK(!recipe.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHB001");
    CHECK(diagnostics[0].file == "r.toml");
    CHECK(diagnostics[0].line != 0);
}

TEST_CASE("A recipe missing a required table or key is reported", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    CHECK(!parseRecipe("[modules]\nids = []\nsources = []\n", "r.toml", diagnostics).has_value());
    CHECK(contains(codesOf(diagnostics), "SHB002"));

    diagnostics.clear();
    CHECK(!parseRecipe("[package]\nid = \"x\"\n", "r.toml", diagnostics).has_value());
    CHECK(contains(codesOf(diagnostics), "SHB002"));

    diagnostics.clear();
    CHECK(!parseRecipe("[package]\nsidecar = \"s.spv\"\n\n[modules]\nids = [\"a\"]\n"
                       "sources = [\"x.spv\"]\n",
                       "r.toml", diagnostics)
               .has_value());
    CHECK(contains(codesOf(diagnostics), "SHB002"));
}

TEST_CASE("Parallel arrays of different lengths are rejected by name", "evidence-unit") {
    // The failure mode this format is most exposed to: the two arrays are positional, so a
    // mismatch would otherwise silently drop or misname a module.
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe("[package]\nid = \"x\"\n\n[modules]\nids = [\"a\", \"b\"]\n"
                              "sources = [\"x.spv\"]\n",
                              "r.toml", diagnostics);
    CHECK(!recipe.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHB003");
    CHECK(diagnostics[0].line != 0);
}

TEST_CASE("A recipe declaring no modules is rejected", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe("[package]\nid = \"x\"\n\n[modules]\nids = []\nsources = []\n",
                              "r.toml", diagnostics);
    CHECK(!recipe.has_value());
    CHECK(contains(codesOf(diagnostics), "SHB004"));
}

TEST_CASE("Duplicate module ids are rejected", "evidence-unit") {
    std::vector<cli::Diagnostic> diagnostics;
    auto recipe = parseRecipe("[package]\nid = \"x\"\n\n[modules]\nids = [\"a\", \"a\"]\n"
                              "sources = [\"x.spv\", \"y.spv\"]\n",
                              "r.toml", diagnostics);
    CHECK(!recipe.has_value());
    CHECK(contains(codesOf(diagnostics), "SHB012"));
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

TEST_CASE("A bake produces a package, a report and a sidecar", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    CHECK(diagnostics.empty());

    CHECK(outputs->packageId == "test-ui");
    CHECK(outputs->moduleCount == 2);
    CHECK(outputs->sidecarName == "shaders.spv");
    CHECK(!outputs->sidecar.empty());

    auto package = shader::ShaderPackage::parse(outputs->packageJson);
    REQUIRE(package.has_value());
    CHECK(package->header.id == "test-ui");
    CHECK(package->header.kind == "shader");
    CHECK(package->sidecarByteLength == outputs->sidecar.size());
    CHECK(package->sidecarSha256 == evidence::sha256(outputs->sidecar));
}

TEST_CASE("Modules are concatenated in recipe order with contiguous ranges", "evidence-unit") {
    // Recipe order rather than directory order: the latter differs between filesystems and would
    // break byte-identity between two developers on the same commit.
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());

    auto package = shader::ShaderPackage::parse(outputs->packageJson);
    REQUIRE(package.has_value());
    REQUIRE(package->modules.size() == 2);
    CHECK(package->modules[0].id == "test.vert");
    CHECK(package->modules[0].stage == shader::Stage::Vertex);
    CHECK(package->modules[0].byteOffset == 0);
    CHECK(package->modules[1].id == "test.frag");
    CHECK(package->modules[1].stage == shader::Stage::Fragment);
    CHECK(package->modules[1].byteOffset == package->modules[0].byteLength);
    CHECK(package->modules[1].byteEnd() == outputs->sidecar.size());
}

TEST_CASE("A module's recorded digest covers exactly its own range", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    auto package = shader::ShaderPackage::parse(outputs->packageJson);
    REQUIRE(package.has_value());

    for (const shader::ShaderModule& module : package->modules) {
        const std::span<const std::byte> range{outputs->sidecar.data() + module.byteOffset,
                                               static_cast<std::size_t>(module.byteLength)};
        CHECK(module.sha256 == evidence::sha256(range));
    }
}

TEST_CASE("Descriptors and push constants carry the stages that declared them",
          "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    auto package = shader::ShaderPackage::parse(outputs->packageJson);
    REQUIRE(package.has_value());

    REQUIRE(package->descriptors.size() == 1);
    CHECK(package->descriptors[0].kind == shader::DescriptorKind::CombinedImageSampler);
    CHECK(package->descriptors[0].stages == shader::kFragmentBit);

    REQUIRE(package->pushConstants.size() == 1);
    CHECK(package->pushConstants[0].size == 16);
    CHECK(package->pushConstants[0].stages == shader::kVertexBit);
}

TEST_CASE("A binding declared by both stages becomes one binding visible to both",
          "evidence-unit") {
    TempDir dir;
    Builder vertex = minimal(kExecutionModelVertex);
    addCombinedImageSampler(vertex, 0, 0);
    Builder fragment = minimal(kExecutionModelFragment);
    addCombinedImageSampler(fragment, 0, 0);
    writeBytes(dir.path() / "v.spv", vertex.bytes());
    writeBytes(dir.path() / "f.spv", fragment.bytes());

    const Recipe recipe{.id = "shared",
                        .sidecar = "shaders.spv",
                        .modules = {{.id = "v", .source = "v.spv"},
                                    {.id = "f", .source = "f.spv"}}};
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = run(recipe, "r.toml", {}, dir.path(), diagnostics);
    REQUIRE(outputs.has_value());

    auto package = shader::ShaderPackage::parse(outputs->packageJson);
    REQUIRE(package.has_value());
    REQUIRE(package->descriptors.size() == 1);
    CHECK(package->descriptors[0].stages == (shader::kVertexBit | shader::kFragmentBit));
}

TEST_CASE("Two stages disagreeing about a binding is an error, not a silent choice",
          "evidence-unit") {
    // Taking either declaration would produce a pipeline layout that matches one shader and not
    // the other, which the driver reports much later and much less clearly.
    TempDir dir;
    Builder vertex = minimal(kExecutionModelVertex);
    addCombinedImageSampler(vertex, 0, 0, 1);
    Builder fragment = minimal(kExecutionModelFragment);
    addCombinedImageSampler(fragment, 0, 0, 4);
    writeBytes(dir.path() / "v.spv", vertex.bytes());
    writeBytes(dir.path() / "f.spv", fragment.bytes());

    const Recipe recipe{.id = "conflict",
                        .sidecar = "shaders.spv",
                        .modules = {{.id = "v", .source = "v.spv"},
                                    {.id = "f", .source = "f.spv"}}};
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = run(recipe, "r.toml", {}, dir.path(), diagnostics);
    CHECK(!outputs.has_value());
    CHECK(contains(codesOf(diagnostics), "SHB007"));
}

TEST_CASE("An unreadable source is reported against the module that named it", "evidence-unit") {
    TempDir dir;
    const Recipe recipe{
        .id = "missing", .sidecar = "shaders.spv", .modules = {{.id = "v", .source = "gone.spv"}}};
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = run(recipe, "r.toml", {}, dir.path(), diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHB005");
    CHECK(diagnostics[0].file == "gone.spv");
}

TEST_CASE("A source that is not SPIR-V is reported with the reason", "evidence-unit") {
    TempDir dir;
    writeText(dir.path() / "bad.spv", "this is not a shader");
    const Recipe recipe{
        .id = "bad", .sidecar = "shaders.spv", .modules = {{.id = "v", .source = "bad.spv"}}};
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = run(recipe, "r.toml", {}, dir.path(), diagnostics);
    CHECK(!outputs.has_value());
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == "SHB006");
    // The reflector's reason is carried through rather than flattened to "invalid".
    CHECK(diagnostics[0].message.find("magic") != std::string::npos);
}

TEST_CASE("The report records the recipe, every input, and both outputs", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());

    auto report = evidence::BakeReport::parse(outputs->reportJson);
    REQUIRE(report.has_value());
    CHECK(report->tool == "mdux-shaderbake");
    CHECK(!report->toolVersion.empty());
    CHECK(report->recipe.path == fixture.recipeRelative);
    CHECK(report->recipe.sha256 == evidence::sha256(fixture.recipeBytes));
    REQUIRE(report->inputs.size() == 2);
    CHECK(report->inputs[0].path == "shaders/test.vert.spv");

    // report.json is deliberately not one of its own outputs: a file cannot carry its own digest.
    REQUIRE(report->outputs.size() == 2);
    CHECK(report->outputs[0].path == "package.json");
    CHECK(report->outputs[1].path == "shaders.spv");
    for (const evidence::FileRecord& output : report->outputs) {
        CHECK(output.path != "report.json");
    }
}

TEST_CASE("A bake is reproducible byte for byte", "evidence-unit") {
    // The property the whole evidence pipeline rests on, asserted at the smallest scale that can
    // demonstrate it: same inputs, same bytes, twice in one process.
    const Fixture fixture;
    std::vector<cli::Diagnostic> first;
    std::vector<cli::Diagnostic> second;
    auto a = fixture.bake(first);
    auto b = fixture.bake(second);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->packageJson == b->packageJson);
    CHECK(a->reportJson == b->reportJson);
    CHECK(a->sidecar == b->sidecar);
}

// ---------------------------------------------------------------------------
// write() and verify()
// ---------------------------------------------------------------------------

TEST_CASE("What write() writes, verify() accepts", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());

    const std::filesystem::path outputDir = fixture.dir.path() / "generated/shader/test-ui";
    REQUIRE(write(*outputs, outputDir, diagnostics));
    CHECK(diagnostics.empty());

    CHECK(std::filesystem::exists(outputDir / "package.json"));
    CHECK(std::filesystem::exists(outputDir / "report.json"));
    CHECK(std::filesystem::exists(outputDir / "shaders.spv"));

    CHECK(verify(*outputs, outputDir / "package.json", outputDir / "report.json", diagnostics));
    CHECK(diagnostics.empty());
}

TEST_CASE("verify() reports the first differing byte of a changed artifact", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    const std::filesystem::path outputDir = fixture.dir.path() / "generated/shader/test-ui";
    REQUIRE(write(*outputs, outputDir, diagnostics));

    // Change one character of the committed package without changing its length.
    std::string committed;
    {
        auto bytes = readBytes(outputDir / "package.json");
        committed.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    const std::size_t position = committed.find("test-ui");
    REQUIRE(position != std::string::npos);
    committed[position] = 'T';
    writeText(outputDir / "package.json", committed);

    diagnostics.clear();
    CHECK(!verify(*outputs, outputDir / "package.json", outputDir / "report.json", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].code == "SHB011");
    // The offset is what makes the diagnostic useful without a diff tool.
    CHECK(diagnostics[0].message.find("byte " + std::to_string(position)) != std::string::npos);
}

TEST_CASE("verify() reports a length change", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    const std::filesystem::path outputDir = fixture.dir.path() / "generated/shader/test-ui";
    REQUIRE(write(*outputs, outputDir, diagnostics));

    // A prefix of the real content: every byte compared matches, so only the length check fires.
    writeText(outputDir / "package.json", outputs->packageJson.substr(0, 10));

    diagnostics.clear();
    CHECK(!verify(*outputs, outputDir / "package.json", outputDir / "report.json", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].code == "SHB011");
    CHECK(diagnostics[0].message.find("length differs") != std::string::npos);
}

TEST_CASE("verify() reports a missing artifact", "evidence-unit") {
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    const std::filesystem::path outputDir = fixture.dir.path() / "generated/shader/test-ui";

    diagnostics.clear();
    CHECK(!verify(*outputs, outputDir / "package.json", outputDir / "report.json", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].code == "SHB010");
    CHECK(diagnostics[0].fixHint.find("mdux-bake-update") != std::string::npos);
}

TEST_CASE("verify() compares the sidecar, not just the package that names it", "evidence-unit") {
    // A package whose recorded digest matches a sidecar nobody compared proves nothing: the whole
    // point of the byte comparison is that it does not trust the artifact's own claims.
    const Fixture fixture;
    std::vector<cli::Diagnostic> diagnostics;
    auto outputs = fixture.bake(diagnostics);
    REQUIRE(outputs.has_value());
    const std::filesystem::path outputDir = fixture.dir.path() / "generated/shader/test-ui";
    REQUIRE(write(*outputs, outputDir, diagnostics));

    std::vector<std::byte> corrupted = outputs->sidecar;
    corrupted.back() = static_cast<std::byte>(std::to_integer<unsigned>(corrupted.back()) ^ 0xffu);
    writeBytes(outputDir / "shaders.spv", corrupted);

    diagnostics.clear();
    CHECK(!verify(*outputs, outputDir / "package.json", outputDir / "report.json", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics[0].code == "SHB011");
    CHECK(diagnostics[0].file.find("shaders.spv") != std::string::npos);
}
