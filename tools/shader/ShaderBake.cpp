/**
 * @brief Implementation of the shader baker's recipe model and bake/verify core.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 */
module;

module mdux.tools.shaderbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.shader.schema;
import mdux.tools.cli;
import mdux.tools.spirv;
import mdux.tools.toml;

namespace mdux::tools::shaderbake {

namespace json = mdux::evidence::json;

namespace {

/// Diagnostic codes. Stable once published: an agent keys off these, and a reworded message must
/// not break it. See docs/governance/schemas/diagnostic.schema.json.
constexpr std::string_view recipeUnparsed = "SHB001";
constexpr std::string_view recipeMissingMember = "SHB002";
constexpr std::string_view recipeArrayLengthMismatch = "SHB003";
constexpr std::string_view recipeEmptyModules = "SHB004";
constexpr std::string_view sourceUnreadable = "SHB005";
constexpr std::string_view spirvRejected = "SHB006";
constexpr std::string_view descriptorConflict = "SHB007";
constexpr std::string_view packageInvalid = "SHB008";
constexpr std::string_view outputUnwritable = "SHB009";
constexpr std::string_view artifactMissing = "SHB010";
constexpr std::string_view artifactDiffers = "SHB011";
constexpr std::string_view recipeDuplicateModuleId = "SHB012";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::size_t line,
            std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file = std::move(file),
                                          .line = line,
                                          .code = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message = std::move(message),
                                          .fixHint = std::move(fixHint)});
}

/// A path as it is recorded in a report: repository-relative with `/` separators, on every
/// platform. BakeReport::validate() rejects a backslash, and would be right to.
[[nodiscard]] std::string reportPath(const std::filesystem::path& path) {
    std::string text = path.generic_string();
    return text;
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path,
                                              std::span<const std::byte> bytes) {
    return evidence::FileRecord{.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

}  // namespace

// ---------------------------------------------------------------------------
// Recipe
// ---------------------------------------------------------------------------

json::Value Recipe::toOptions() const {
    json::Value options = json::Value::emptyObject();
    static_cast<void>(options.set("id", json::Value::string(id)));
    static_cast<void>(options.set("sidecar", json::Value::string(sidecar)));

    json::Value ids = json::Value::array({});
    json::Value sources = json::Value::array({});
    for (const RecipeModule& module : modules) {
        static_cast<void>(ids.push(json::Value::string(module.id)));
        static_cast<void>(sources.push(json::Value::string(module.source)));
    }
    static_cast<void>(options.set("moduleIds", std::move(ids)));
    static_cast<void>(options.set("moduleSources", std::move(sources)));
    return options;
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    std::array<char, 8192> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           file.gcount() > 0) {
        const auto read = static_cast<std::size_t>(file.gcount());
        const auto* start = reinterpret_cast<const std::byte*>(buffer.data());
        bytes.insert(bytes.end(), start, start + read);
        if (file.eof()) {
            break;
        }
    }
    if (file.bad()) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document{};
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeUnparsed, error.what(),
               "See tools/shader/ShaderBake.cppm for the recipe format.");
        return std::nullopt;
    }

    Recipe recipe;

    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember,
               "recipe has no [package] table",
               "Add a [package] table with an 'id' key.");
        return std::nullopt;
    }

    try {
        recipe.id = package->require("id").asString();
        if (const toml::Value* sidecar = package->find("sidecar"); sidecar != nullptr) {
            recipe.sidecar = sidecar->asString();
        }
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember,
               error.what());
        return std::nullopt;
    }

    const toml::Table* modules = document.table("modules");
    if (modules == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember,
               "recipe has no [modules] table",
               "Add a [modules] table with parallel 'ids' and 'sources' arrays.");
        return std::nullopt;
    }

    std::vector<std::string> ids;
    std::vector<std::string> sources;
    std::size_t idsLine = 0;
    try {
        const toml::Value& idsValue = modules->require("ids");
        idsLine = idsValue.line();
        ids = idsValue.asStringArray();
        sources = modules->require("sources").asStringArray();
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember,
               error.what(),
               "[modules] needs an 'ids' array and a 'sources' array, both of strings.");
        return std::nullopt;
    }

    if (ids.size() != sources.size()) {
        report(diagnostics, std::string{recipePath}, idsLine, recipeArrayLengthMismatch,
               "[modules] ids has " + std::to_string(ids.size()) + " entries but sources has " +
                   std::to_string(sources.size()),
               "The two arrays are positional: entry N of ids names entry N of sources.");
        return std::nullopt;
    }
    if (ids.empty()) {
        report(diagnostics, std::string{recipePath}, idsLine, recipeEmptyModules,
               "[modules] declares no modules",
               "A shader package with no modules has nothing to bake.");
        return std::nullopt;
    }

    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (ids[j] == ids[i]) {
                report(diagnostics, std::string{recipePath}, idsLine, recipeDuplicateModuleId,
                       "duplicate module id '" + ids[i] + "'",
                       "Module ids identify a module within its package and must be unique.");
                return std::nullopt;
            }
        }
        recipe.modules.push_back(RecipeModule{.id = ids[i], .source = sources[i]});
    }

    return recipe;
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                               std::span<const std::byte> recipeBytes,
                               const std::filesystem::path& root,
                               std::vector<cli::Diagnostic>& diagnostics) {
    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;

    shader::ShaderPackage package;
    package.header.id = recipe.id;
    package.header.kind = std::string{shader::kind};
    package.sidecarPath = recipe.sidecar;

    std::vector<evidence::FileRecord> inputs;

    // Merged across modules: a binding declared by both stages is one binding visible to both.
    std::map<std::pair<std::uint32_t, std::uint32_t>, shader::DescriptorBinding> descriptors;
    std::map<std::pair<std::uint32_t, std::uint32_t>, shader::PushConstantRange> pushConstants;

    for (const RecipeModule& entry : recipe.modules) {
        const std::filesystem::path source = root / entry.source;
        auto bytes = readFile(source);
        if (!bytes.has_value()) {
            report(diagnostics, entry.source, 0, sourceUnreadable,
                   "cannot read SPIR-V for module '" + entry.id + "'",
                   "Check the path in the recipe's [modules] sources array.");
            return std::nullopt;
        }

        auto reflection = spirv::reflect(*bytes);
        if (!reflection.has_value()) {
            report(diagnostics, entry.source, 0, spirvRejected,
                   "module '" + entry.id + "' was rejected: " +
                       std::string{spirv::describe(reflection.error())},
                   "Recompile the shader, or see tools/shader/Spirv.cppm for what is supported.");
            return std::nullopt;
        }

        const std::uint64_t offset = outputs.sidecar.size();
        outputs.sidecar.insert(outputs.sidecar.end(), bytes->begin(), bytes->end());

        package.modules.push_back(
            shader::ShaderModule{.id = entry.id,
                                 .stage = reflection->stage,
                                 .entryPoint = reflection->entryPoint,
                                 .byteOffset = offset,
                                 .byteLength = static_cast<std::uint64_t>(bytes->size()),
                                 .sha256 = evidence::sha256(*bytes)});

        inputs.push_back(fileRecord(entry.source, *bytes));

        for (const shader::DescriptorBinding& binding : reflection->descriptors) {
            const auto key = std::pair{binding.set, binding.binding};
            const auto existing = descriptors.find(key);
            if (existing == descriptors.end()) {
                descriptors.emplace(key, binding);
                continue;
            }
            // Two stages may share a binding, but they must agree about what is bound there.
            // Silently taking one of them would produce a pipeline layout that matches neither.
            if (existing->second.kind != binding.kind ||
                existing->second.count != binding.count) {
                report(diagnostics, entry.source, 0, descriptorConflict,
                       "module '" + entry.id + "' declares set " + std::to_string(binding.set) +
                           " binding " + std::to_string(binding.binding) + " as " +
                           std::string{shader::toWire(binding.kind)} + " x" +
                           std::to_string(binding.count) + ", but another module declares it as " +
                           std::string{shader::toWire(existing->second.kind)} + " x" +
                           std::to_string(existing->second.count),
                       "Make the declarations identical in both shaders.");
                return std::nullopt;
            }
            existing->second.stages |= binding.stages;
        }

        if (reflection->pushConstant.has_value()) {
            const shader::PushConstantRange& range = *reflection->pushConstant;
            const auto key = std::pair{range.offset, range.size};
            const auto existing = pushConstants.find(key);
            if (existing == pushConstants.end()) {
                pushConstants.emplace(key, range);
            } else {
                existing->second.stages |= range.stages;
            }
        }
    }

    for (const auto& [key, binding] : descriptors) {
        static_cast<void>(key);
        package.descriptors.push_back(binding);
    }
    for (const auto& [key, range] : pushConstants) {
        static_cast<void>(key);
        package.pushConstants.push_back(range);
    }

    package.sidecarByteLength = static_cast<std::uint64_t>(outputs.sidecar.size());
    package.sidecarSha256 = evidence::sha256(outputs.sidecar);

    auto packageText = package.write();
    if (!packageText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "assembled package is not valid: " +
                   std::string{shader::describe(packageText.error())});
        return std::nullopt;
    }
    outputs.packageJson = std::move(*packageText);
    outputs.packageId = package.header.id;
    outputs.moduleCount = package.modules.size();

    evidence::BakeReport bakeReport;
    bakeReport.tool = std::string{toolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs = std::move(inputs);
    bakeReport.options = recipe.toOptions();
    // report.json is deliberately absent from its own outputs: a file cannot carry its own
    // digest, for the same reason ADR-007 gives for there being no commit SHA in here.
    bakeReport.outputs = {fileRecord("package.json", asBytes(outputs.packageJson)),
                          fileRecord(outputs.sidecarName, outputs.sidecar)};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "bake report is not valid: " +
                   std::string{evidence::describe(reportText.error())});
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);

    return outputs;
}

// ---------------------------------------------------------------------------
// write() / verify()
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                              std::vector<cli::Diagnostic>& diagnostics) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "cannot open for writing");
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "write failed");
        return false;
    }
    return true;
}

/// Compares produced bytes against a committed file, reporting the first differing offset.
///
/// An offset is worth the few lines it costs: "package.json differs" sends a reader to a diff
/// tool, where "differs at byte 412 (produced 0x33, committed 0x34)" usually identifies the field
/// on its own.
[[nodiscard]] bool compareArtifact(std::string_view label, std::span<const std::byte> produced,
                                   const std::filesystem::path& committedPath,
                                   std::vector<cli::Diagnostic>& diagnostics) {
    auto committed = readFile(committedPath);
    if (!committed.has_value()) {
        report(diagnostics, reportPath(committedPath), 0, artifactMissing,
               std::string{label} + " is missing or unreadable",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return false;
    }

    const std::size_t common = std::min(produced.size(), committed->size());
    for (std::size_t i = 0; i < common; ++i) {
        if (produced[i] != (*committed)[i]) {
            report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
                   std::string{label} + " differs at byte " + std::to_string(i) + ": produced 0x" +
                       std::format("{:02x}", std::to_integer<unsigned>(produced[i])) +
                       ", committed 0x" +
                       std::format("{:02x}", std::to_integer<unsigned>((*committed)[i])),
                   "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
            return false;
        }
    }
    if (produced.size() != committed->size()) {
        report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
               std::string{label} + " length differs: produced " +
                   std::to_string(produced.size()) + " bytes, committed " +
                   std::to_string(committed->size()),
               "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
        return false;
    }
    return true;
}

}  // namespace

bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
           std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, reportPath(outputDir), 0, outputUnwritable,
               "cannot create output directory: " + code.message());
        return false;
    }

    bool ok = writeBytes(outputDir / "package.json", asBytes(outputs.packageJson), diagnostics);
    ok = writeBytes(outputDir / "report.json", asBytes(outputs.reportJson), diagnostics) && ok;
    ok = writeBytes(outputDir / outputs.sidecarName, outputs.sidecar, diagnostics) && ok;
    return ok;
}

bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
            const std::filesystem::path& reportPath, std::vector<cli::Diagnostic>& diagnostics) {
    bool ok = compareArtifact("package.json", asBytes(outputs.packageJson), packagePath,
                              diagnostics);
    ok = compareArtifact("report.json", asBytes(outputs.reportJson), reportPath, diagnostics) && ok;
    // The sidecar is not named on the command line - it sits beside package.json, under the name
    // the package itself records. Verifying it here rather than trusting the package's digest is
    // the point: a package whose recorded digest matches a sidecar nobody compared proves nothing.
    ok = compareArtifact(outputs.sidecarName, outputs.sidecar,
                         packagePath.parent_path() / outputs.sidecarName, diagnostics) &&
         ok;
    return ok;
}

}  // namespace mdux::tools::shaderbake
