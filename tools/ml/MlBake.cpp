/**
 * @file MlBake.cpp
 * @brief Implementation of the model baker.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 */
module;

module mdux.tools.ml.mlbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.toml;
import mdux.tools.ml.safetensors;
import mdux.tools.ml.archvalidate;
import mdux.tools.ml.goldengen;

namespace mdux::tools::ml {

namespace evidence = mdux::evidence;
namespace json = mdux::evidence::json;
namespace toml = mdux::tools::toml;
namespace ml = mdux::ml;

namespace {

constexpr std::string_view weightsFileName = "weights.bin";

constexpr std::string_view recipeUnreadable = "mdux.ml.bake.recipeUnreadable";
constexpr std::string_view recipeInvalid = "mdux.ml.bake.recipeInvalid";
constexpr std::string_view weightsUnreadable = "mdux.ml.bake.weightsUnreadable";
constexpr std::string_view goldenFailed = "mdux.ml.bake.goldenGenerationFailed";
constexpr std::string_view packageInvalid = "mdux.ml.bake.packageInvalid";
constexpr std::string_view outputUnwritable = "mdux.ml.bake.outputUnwritable";
constexpr std::string_view artifactMissing = "mdux.ml.bake.artifactMissing";
constexpr std::string_view artifactDiffers = "mdux.ml.bake.artifactDiffers";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::size_t line,
            std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file = std::move(file),
                                          .line = line,
                                          .column = 0,
                                          .code = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message = std::move(message),
                                          .fixHint = std::move(fixHint)});
}

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path, std::span<const std::byte> bytes) {
    return evidence::FileRecord{.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

/// Repository-relative, forward slashes. A backslash breaks byte-identity between Windows and
/// Linux, and BakeReport::validate() rejects one.
[[nodiscard]] std::string reportPath(const std::filesystem::path& path) {
    std::string text = path.generic_string();
    return text;
}

[[nodiscard]] std::optional<ml::LayerKind> layerKindFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < ml::layerKindWireValues.size(); ++i) {
        if (ml::layerKindWireValues[i] == wire) {
            return static_cast<ml::LayerKind>(i);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ml::Activation> activationFromWire(std::string_view wire) noexcept {
    for (std::size_t i = 0; i < ml::activationWireValues.size(); ++i) {
        if (ml::activationWireValues[i] == wire) {
            return static_cast<ml::Activation>(i);
        }
    }
    return std::nullopt;
}

/// A TensorRef as package.json carries it. Shape is written at its declared rank, so a reader
/// never has to know that the array is padded to three.
[[nodiscard]] json::Value tensorToJson(const ml::TensorRef& tensor) {
    json::Value object = json::Value::emptyObject();
    (void)object.set("byteOffset", json::Value::unsignedInteger(tensor.byteOffset));
    std::vector<json::Value> shape;
    for (std::uint8_t i = 0; i < tensor.rank; ++i) {
        shape.push_back(json::Value::unsignedInteger(tensor.shape[i]));
    }
    (void)object.set("shape", json::Value::array(std::move(shape)));
    return object;
}

[[nodiscard]] json::Value layerToJson(const ml::LayerDesc& layer) {
    json::Value object = json::Value::emptyObject();
    (void)object.set(
        "kind",
        json::Value::string(std::string{
            ml::layerKindWireValues[static_cast<std::size_t>(layer.kind)]}));
    (void)object.set(
        "activation",
        json::Value::string(std::string{
            ml::activationWireValues[static_cast<std::size_t>(layer.activation)]}));
    (void)object.set("inLength", json::Value::unsignedInteger(layer.inLength));
    (void)object.set("inChannels", json::Value::unsignedInteger(layer.inChannels));
    (void)object.set("outLength", json::Value::unsignedInteger(layer.outLength));
    (void)object.set("outChannels", json::Value::unsignedInteger(layer.outChannels));
    if (ml::isWindowed(layer.kind)) {
        (void)object.set("kernelSize", json::Value::unsignedInteger(layer.kernelSize));
        (void)object.set("stride", json::Value::unsignedInteger(layer.stride));
    }
    if (layer.weights.present()) {
        (void)object.set("weights", tensorToJson(layer.weights));
    }
    if (layer.bias.present()) {
        (void)object.set("bias", tensorToJson(layer.bias));
    }
    return object;
}

/// Golden vectors as u32 bit patterns - never decimal. See ADR-008, decision 4.
[[nodiscard]] json::Value goldenToJson(const GeneratedGolden& golden) {
    json::Value object = json::Value::emptyObject();
    std::vector<json::Value> input;
    input.reserve(golden.inputBits.size());
    for (std::uint32_t bits : golden.inputBits) {
        input.push_back(json::Value::unsignedInteger(bits));
    }
    std::vector<json::Value> output;
    output.reserve(golden.expectedOutputBits.size());
    for (std::uint32_t bits : golden.expectedOutputBits) {
        output.push_back(json::Value::unsignedInteger(bits));
    }
    (void)object.set("inputBits", json::Value::array(std::move(input)));
    (void)object.set("expectedOutputBits", json::Value::array(std::move(output)));
    return object;
}

[[nodiscard]] std::string hexOf(const evidence::Digest& digest) {
    const auto hex = evidence::toHex(digest);
    return std::string{hex.data(), hex.size()};
}

/**
 * @brief Reads a required non-negative value that fits in `uint32`.
 *
 * The range check is not pedantry. TOML integers are signed 64-bit, so `inputLength = -1` used to
 * become 4294967295: the recipe author sees a nonsensical downstream error about layer chains, and
 * a value like `maxScratchFloats = -1` asks the runtime for a 16 GB buffer. Throwing TomlError here
 * keeps the diagnostic on the offending line, where the mistake actually is.
 */
[[nodiscard]] std::uint32_t requireUnsigned(const toml::Table& table, std::string_view key) {
    const toml::Value& value = table.require(key);
    const std::int64_t number = value.asInteger();
    if (number < 0 || number > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw toml::TomlError{
            value.line(),
            std::format("'{}' must be between 0 and 4294967295, got {}", key, number)};
    }
    return static_cast<std::uint32_t>(number);
}

/// Same range discipline for a count used as a size_t.
[[nodiscard]] std::size_t requireCount(const toml::Table& table, std::string_view key) {
    const toml::Value& value = table.require(key);
    const std::int64_t number = value.asInteger();
    if (number < 0) {
        throw toml::TomlError{value.line(),
                              std::format("'{}' must not be negative, got {}", key, number)};
    }
    return static_cast<std::size_t>(number);
}

}  // namespace

json::Value Recipe::toOptions(std::uint32_t resolvedScratch) const {
    // Fully resolved, defaults expanded - see the Recipe comment and ADR-007.
    json::Value options = json::Value::emptyObject();
    (void)options.set("inputLength", json::Value::unsignedInteger(inputLength));
    (void)options.set("outputLength", json::Value::unsignedInteger(outputLength));
    (void)options.set("maxScratchFloats", json::Value::unsignedInteger(resolvedScratch));
    (void)options.set("layerCount", json::Value::unsignedInteger(layers.size()));
    (void)options.set("goldenCount", json::Value::unsignedInteger(goldenCount));
    (void)options.set("goldenSeed", json::Value::unsignedInteger(goldenSeed));
    // The algorithm is recorded, not just the seed: a seed alone does not determine the sequence.
    (void)options.set("goldenPrng", json::Value::string(std::string{goldenPrngAlgorithm}));
    return options;
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<char> contents{std::istreambuf_iterator<char>{file},
                               std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(contents.size());
    for (std::size_t i = 0; i < contents.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(contents[i]));
    }
    return bytes;
}

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document{};
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeInvalid, error.what());
        return std::nullopt;
    }

    Recipe recipe;
    try {
        const toml::Table* package = document.table("package");
        if (package == nullptr) {
            report(diagnostics, std::string{recipePath}, 0, recipeInvalid,
                   "recipe has no [package] table");
            return std::nullopt;
        }
        recipe.id = package->require("id").asString();
        recipe.weightsSource = package->require("source").asString();
        recipe.inputLength = requireUnsigned(*package, "inputLength");
        recipe.outputLength = requireUnsigned(*package, "outputLength");
        if (package->contains("maxScratchFloats")) {
            recipe.maxScratchFloats = requireUnsigned(*package, "maxScratchFloats");
        }

        const toml::Table* goldens = document.table("goldens");
        if (goldens == nullptr) {
            report(diagnostics, std::string{recipePath}, 0, recipeInvalid,
                   "recipe has no [goldens] table",
                   "a package with no goldens has no self-test, which ADR-008 does not permit");
            return std::nullopt;
        }
        recipe.goldenCount = requireCount(*goldens, "count");
        recipe.goldenSeed = requireUnsigned(*goldens, "seed");
        // The algorithm is named in the recipe so that changing it is a visible recipe edit
        // rather than a silent tooling change; the baker accepts only the one it implements.
        const std::string algorithm = goldens->require("algorithm").asString();
        if (algorithm != goldenPrngAlgorithm) {
            report(diagnostics, std::string{recipePath}, goldens->require("algorithm").line(),
                   recipeInvalid,
                   std::format("unknown golden PRNG '{}'; this baker implements '{}'", algorithm,
                               goldenPrngAlgorithm));
            return std::nullopt;
        }

        const toml::Table* layers = document.table("layers");
        if (layers == nullptr) {
            report(diagnostics, std::string{recipePath}, 0, recipeInvalid,
                   "recipe has no [layers] table");
            return std::nullopt;
        }

        // Parallel arrays rather than an array of tables: mdux.tools.toml implements a deliberate
        // subset with no [[table]] support, and the shader recipe already uses this shape.
        const std::vector<std::string> kinds = layers->require("kinds").asStringArray();
        const std::vector<std::string> activations = layers->require("activations").asStringArray();
        const std::vector<std::int64_t> inLengths = layers->require("inLengths").asIntegerArray();
        const std::vector<std::int64_t> inChannels = layers->require("inChannels").asIntegerArray();
        const std::vector<std::int64_t> outLengths = layers->require("outLengths").asIntegerArray();
        const std::vector<std::int64_t> outChannels =
            layers->require("outChannels").asIntegerArray();
        const std::vector<std::int64_t> kernelSizes =
            layers->require("kernelSizes").asIntegerArray();
        const std::vector<std::int64_t> strides = layers->require("strides").asIntegerArray();
        const std::vector<std::string> weightNames = layers->require("weightNames").asStringArray();
        const std::vector<std::string> biasNames = layers->require("biasNames").asStringArray();

        const std::size_t count = kinds.size();
        const bool ragged =
            activations.size() != count || inLengths.size() != count ||
            inChannels.size() != count || outLengths.size() != count ||
            outChannels.size() != count || kernelSizes.size() != count ||
            strides.size() != count || weightNames.size() != count || biasNames.size() != count;
        if (ragged) {
            report(diagnostics, std::string{recipePath}, layers->line(), recipeInvalid,
                   "the [layers] arrays do not all have the same length",
                   "entry N of every array describes layer N");
            return std::nullopt;
        }

        for (std::size_t i = 0; i < count; ++i) {
            const std::optional<ml::LayerKind> kind = layerKindFromWire(kinds[i]);
            if (!kind.has_value()) {
                report(diagnostics, std::string{recipePath}, layers->require("kinds").line(),
                       recipeInvalid, std::format("layer {} has unknown kind '{}'", i, kinds[i]));
                return std::nullopt;
            }
            const std::optional<ml::Activation> activation = activationFromWire(activations[i]);
            if (!activation.has_value()) {
                report(diagnostics, std::string{recipePath}, layers->require("activations").line(),
                       recipeInvalid,
                       std::format("layer {} has unknown activation '{}'", i, activations[i]));
                return std::nullopt;
            }
            recipe.layers.push_back(
                LayerSpec{.kind = *kind,
                          .activation = *activation,
                          .inLength = static_cast<std::uint32_t>(inLengths[i]),
                          .inChannels = static_cast<std::uint32_t>(inChannels[i]),
                          .outLength = static_cast<std::uint32_t>(outLengths[i]),
                          .outChannels = static_cast<std::uint32_t>(outChannels[i]),
                          .kernelSize = static_cast<std::uint32_t>(kernelSizes[i]),
                          .stride = static_cast<std::uint32_t>(strides[i]),
                          .weightsTensor = weightNames[i],
                          .biasTensor = biasNames[i]});
        }
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeInvalid, error.what());
        return std::nullopt;
    }

    return recipe;
}

std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                               std::span<const std::byte> recipeBytes,
                               const std::filesystem::path& root,
                               std::vector<cli::Diagnostic>& diagnostics) {
    const std::filesystem::path weightsPath = root / recipe.weightsSource;
    auto weightsBytes = readFile(weightsPath);
    if (!weightsBytes.has_value()) {
        report(diagnostics, recipe.weightsSource, 0, weightsUnreadable,
               "cannot read the weights file named by the recipe");
        return std::nullopt;
    }

    auto parsedWeights = parseSafetensors(*weightsBytes, recipe.weightsSource);
    if (!parsedWeights.has_value()) {
        diagnostics.push_back(parsedWeights.error());
        return std::nullopt;
    }

    ArchitectureSpec spec;
    spec.id = recipe.id;
    spec.inputLength = recipe.inputLength;
    spec.outputLength = recipe.outputLength;
    spec.maxScratchFloats = recipe.maxScratchFloats;
    spec.layers = recipe.layers;

    auto resolved = resolveArchitecture(spec, *parsedWeights, *weightsBytes, recipePath);
    if (!resolved.has_value()) {
        for (const cli::Diagnostic& diagnostic : resolved.error()) {
            diagnostics.push_back(diagnostic);
        }
        return std::nullopt;
    }

    // The goldens run through mdux.ml.kernels - the same governed module the device executes.
    auto goldens = generateGoldens(resolved->layers, resolved->weights, resolved->inputLength,
                                   resolved->outputLength, resolved->maxScratchFloats,
                                   recipe.goldenCount, recipe.goldenSeed);
    if (!goldens.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, goldenFailed,
               std::string{describe(goldens.error())});
        return std::nullopt;
    }

    const evidence::Digest weightsDigest = evidence::sha256(resolved->weights);

    // Assemble the package and validate it before rendering, so a malformed package is reported
    // as such rather than as valid-looking JSON that the runtime will later refuse.
    std::vector<ml::GoldenVector> goldenViews;
    goldenViews.reserve(goldens->size());
    for (const GeneratedGolden& golden : *goldens) {
        goldenViews.push_back(ml::GoldenVector{.inputBits = golden.inputBits,
                                               .expectedOutputBits = golden.expectedOutputBits});
    }
    const ml::ModelPackage package{.id = recipe.id,
                                   .schemaVersion = evidence::kSchemaVersion,
                                   .weightsDigest = weightsDigest,
                                   .weightsByteLength = resolved->weights.size(),
                                   .layers = resolved->layers,
                                   .goldens = goldenViews,
                                   .inputLength = resolved->inputLength,
                                   .outputLength = resolved->outputLength,
                                   .maxScratchFloats = resolved->maxScratchFloats};
    if (auto valid = package.validate(); !valid.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               std::format("assembled package is not valid: {}", ml::describe(valid.error())));
        return std::nullopt;
    }

    json::Value packageJson = json::Value::emptyObject();
    const evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion,
                                         .id = recipe.id,
                                         .kind = std::string{ml::packageKind}};
    if (auto written = header.writeInto(packageJson); !written.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "package header could not be serialized");
        return std::nullopt;
    }

    (void)packageJson.set("inputLength", json::Value::unsignedInteger(resolved->inputLength));
    (void)packageJson.set("outputLength", json::Value::unsignedInteger(resolved->outputLength));
    (void)packageJson.set("maxScratchFloats",
                          json::Value::unsignedInteger(resolved->maxScratchFloats));

    json::Value weightsRecord = json::Value::emptyObject();
    (void)weightsRecord.set("path", json::Value::string(std::string{weightsFileName}));
    (void)weightsRecord.set("byteLength", json::Value::unsignedInteger(resolved->weights.size()));
    (void)weightsRecord.set("sha256", json::Value::string(hexOf(weightsDigest)));
    (void)packageJson.set("weights", std::move(weightsRecord));

    std::vector<json::Value> layerValues;
    layerValues.reserve(resolved->layers.size());
    for (const ml::LayerDesc& layer : resolved->layers) {
        layerValues.push_back(layerToJson(layer));
    }
    (void)packageJson.set("layers", json::Value::array(std::move(layerValues)));

    std::vector<json::Value> goldenValues;
    goldenValues.reserve(goldens->size());
    for (const GeneratedGolden& golden : *goldens) {
        goldenValues.push_back(goldenToJson(golden));
    }
    (void)packageJson.set("goldens", json::Value::array(std::move(goldenValues)));

    auto packageText = json::write(packageJson);
    if (!packageText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               std::format("package JSON could not be rendered: {}",
                           json::describe(packageText.error().code)));
        return std::nullopt;
    }

    BakeOutputs outputs;
    outputs.packageJson = std::move(*packageText);
    outputs.weights = std::move(resolved->weights);
    outputs.weightsName = std::string{weightsFileName};
    outputs.packageId = recipe.id;
    outputs.layerCount = resolved->layers.size();
    outputs.goldenCount = goldens->size();

    evidence::BakeReport bakeReport;
    bakeReport.tool = std::string{bakeToolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs = {fileRecord(recipe.weightsSource, *weightsBytes)};
    bakeReport.options = recipe.toOptions(resolved->maxScratchFloats);
    // report.json is deliberately absent from its own outputs: a file cannot carry its own digest.
    bakeReport.outputs = {fileRecord("package.json", asBytes(outputs.packageJson)),
                          fileRecord(outputs.weightsName, outputs.weights)};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               std::format("bake report is not valid: {}",
                           evidence::describe(reportText.error())));
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);

    return outputs;
}

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

/// Compares produced bytes against a committed file, reporting the first differing offset - which
/// for a package.json usually identifies the field without opening a diff tool.
[[nodiscard]] bool compareArtifact(std::string_view label, std::span<const std::byte> produced,
                                   const std::filesystem::path& committedPath,
                                   std::vector<cli::Diagnostic>& diagnostics) {
    auto committed = readFile(committedPath);
    if (!committed.has_value()) {
        report(diagnostics, reportPath(committedPath), 0, artifactMissing,
               std::format("{} is missing or unreadable", label),
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return false;
    }

    const std::size_t common = std::min(produced.size(), committed->size());
    for (std::size_t i = 0; i < common; ++i) {
        if (produced[i] != (*committed)[i]) {
            report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
                   std::format("{} differs at byte {}: produced 0x{:02x}, committed 0x{:02x}",
                               label, i, std::to_integer<unsigned>(produced[i]),
                               std::to_integer<unsigned>((*committed)[i])),
                   "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
            return false;
        }
    }
    if (produced.size() != committed->size()) {
        report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
               std::format("{} length differs: produced {} bytes, committed {}", label,
                           produced.size(), committed->size()),
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
               std::format("cannot create output directory: {}", code.message()));
        return false;
    }

    return writeBytes(outputDir / "package.json", asBytes(outputs.packageJson), diagnostics) &&
           writeBytes(outputDir / outputs.weightsName, outputs.weights, diagnostics) &&
           writeBytes(outputDir / "report.json", asBytes(outputs.reportJson), diagnostics);
}

bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
            const std::filesystem::path& reportPath_, std::vector<cli::Diagnostic>& diagnostics) {
    // The sidecar sits beside package.json by construction, so its path is derived rather than
    // being a fourth command-line argument nobody would keep in step.
    const std::filesystem::path weightsPath =
        packagePath.parent_path() / outputs.weightsName;

    bool ok = compareArtifact("package.json", asBytes(outputs.packageJson), packagePath,
                              diagnostics);
    ok = compareArtifact(outputs.weightsName, outputs.weights, weightsPath, diagnostics) && ok;
    ok = compareArtifact("report.json", asBytes(outputs.reportJson), reportPath_, diagnostics) && ok;
    return ok;
}

}  // namespace mdux::tools::ml
