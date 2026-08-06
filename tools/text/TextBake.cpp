/**
 * @brief Implementation of the text baker's recipe model and bake/verify core.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 */
module;

module mdux.tools.textbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.toml;

namespace mdux::tools::textbake {

namespace json = mdux::evidence::json;

namespace {

/// Diagnostic codes. Stable once published: an agent keys off these, and a reworded message must
/// not break it. See docs/governance/schemas/diagnostic.schema.json.
///
/// `TXT000` is the recipe-unreadable code, emitted by `mdux-textbake`'s `main()` (matching
/// `SHB000` in the shader baker). The numbered library codes below begin at `TXT001`.
constexpr std::string_view recipeUnparsed = "TXT001";
constexpr std::string_view recipeMissingMember = "TXT002";
constexpr std::string_view recipeEmptyAtlas = "TXT003";
constexpr std::string_view recipeEmptyLocale = "TXT004";
constexpr std::string_view packageInvalid = "TXT005";
constexpr std::string_view outputUnwritable = "TXT006";
constexpr std::string_view artifactMissing = "TXT007";
constexpr std::string_view artifactDiffers = "TXT008";
constexpr std::string_view recipeEmptyId = "TXT009";
constexpr std::string_view recipeSidecarPathHasSeparator = "TXT010";

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
    return path.generic_string();
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
    static_cast<void>(options.set("atlas", json::Value::string(atlas)));
    static_cast<void>(options.set("locale", json::Value::string(locale)));
    static_cast<void>(options.set("sidecar", json::Value::string(sidecar)));
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
               "See tools/text/TextBake.cppm for the recipe format.");
        return std::nullopt;
    }

    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember,
               "recipe has no [package] table",
               "Add a [package] table with 'id', 'atlas' and 'locale' keys.");
        return std::nullopt;
    }

    Recipe recipe;
    try {
        recipe.id = package->require("id").asString();
        recipe.atlas = package->require("atlas").asString();
        recipe.locale = package->require("locale").asString();
        if (const toml::Value* sidecar = package->find("sidecar"); sidecar != nullptr) {
            recipe.sidecar = sidecar->asString();
        }
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember,
               error.what());
        return std::nullopt;
    }

    if (recipe.atlas.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyAtlas,
               "recipe's 'atlas' is empty",
               "A text package references a font package id produced by S4 (#160).");
        return std::nullopt;
    }
    if (recipe.locale.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyLocale,
               "recipe's 'locale' is empty",
               "Use a BCP 47 tag such as 'en-US'. An unlocalized text package is not meaningful.");
        return std::nullopt;
    }
    // `id` and `sidecar` get dedicated recipe-level checks (rather than falling through to schema
    // validation as generic TXT005 "assembled package is not valid") because both are recipe-authoring
    // mistakes and a fix hint at the recipe line is more actionable than one at schema line 0.
    if (recipe.id.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyId,
               "recipe's 'id' is empty",
               "The id is the <id> in generated/text/<id>/ and must be non-empty.");
        return std::nullopt;
    }
    if (recipe.sidecar.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeSidecarPathHasSeparator,
               "recipe's 'sidecar' is empty; it must be a bare filename",
               "A sidecar sits beside package.json in generated/text/<id>/.");
        return std::nullopt;
    }
    if (recipe.sidecar.find('/') != std::string::npos ||
        recipe.sidecar.find('\\') != std::string::npos) {
        report(diagnostics, std::string{recipePath}, 0, recipeSidecarPathHasSeparator,
               "recipe's 'sidecar' contains a path separator; it must be a bare filename",
               "A sidecar sits beside package.json in generated/text/<id>/; a path would let it "
               "escape that directory.");
        return std::nullopt;
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
    static_cast<void>(root);  // S1 produces no sidecar inputs; S4 reads fonts through `root`.

    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;

    text::TextPackage package;
    package.header.id = recipe.id;
    package.header.kind = std::string{text::packageKind};
    package.atlasId = recipe.atlas;
    package.locale = recipe.locale;
    package.sidecarPath = recipe.sidecar;
    package.sidecarByteLength = 0;  // S1: no runs. The first real artifact is S4 (#160).
    package.sidecarSha256 = evidence::sha256(outputs.sidecar);
    // No runs are appended; the package validates with an empty run list, by design.

    auto packageText = package.write();
    if (!packageText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "assembled package is not valid: " +
                   std::string{text::describe(packageText.error())});
        return std::nullopt;
    }
    outputs.packageJson = std::move(*packageText);
    outputs.packageId = package.header.id;
    outputs.runCount = package.runs.size();

    // No inputs at S1: a no-run package reads no source files. S4's font pipeline will record its
    // TrueType and rasteriser inputs here; recording an empty list now keeps the report's inputs
    // array present and structured rather than omitted.
    std::vector<evidence::FileRecord> inputs;

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

}  // namespace mdux::tools::textbake
