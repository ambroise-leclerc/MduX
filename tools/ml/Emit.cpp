/**
 * @file Emit.cpp
 * @brief Implementation of the model package's two C++ renderings.
 */
module;

module mdux.tools.ml.emit;

import std;
import mdux.ml.schema;
import mdux.tools.cli;
import mdux.tools.ml.packageload;

namespace mdux::tools::ml {

namespace {

namespace schema = mdux::ml;

constexpr std::string_view packageUnreadable  = "MLE001";
constexpr std::string_view outputUnwritable   = "MLE002";
constexpr std::string_view identifierReserved = "MLE003";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = std::move(file),
                                          .code     = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fixHint)});
}

[[nodiscard]] std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        file.read(bytes.data(), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

[[nodiscard]] std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
            case '"':
                out += "\\\"";
                continue;
            case '\\':
                out += "\\\\";
                continue;
            case '\n':
                out += "\\n";
                continue;
            case '\r':
                out += "\\r";
                continue;
            case '\t':
                out += "\\t";
                continue;
            default:
                break;
        }
        if (byte < 0x20 || byte == 0x7f) {
            out += std::format("\\{:03o}", byte);
        } else {
            out.push_back(character);
        }
    }
    return out;
}

[[nodiscard]] std::string renderLayerKind(schema::LayerKind kind) {
    static constexpr std::array<std::string_view, 5> names{"Dense", "Conv1d", "MaxPool1d", "AvgPool1d", "Flatten"};
    return std::format("mdux::ml::LayerKind::{}", names[static_cast<std::size_t>(kind)]);
}

[[nodiscard]] std::string renderActivation(schema::Activation activation) {
    static constexpr std::array<std::string_view, 4> names{"None", "Relu", "Sigmoid", "Softmax"};
    return std::format("mdux::ml::Activation::{}", names[static_cast<std::size_t>(activation)]);
}

[[nodiscard]] std::string renderTensor(const schema::TensorRef& tensor) {
    return std::format("{{.byteOffset = {}ull, .shape = {{{}, {}, {}}}, .rank = {}}}",
                       tensor.byteOffset,
                       tensor.shape[0],
                       tensor.shape[1],
                       tensor.shape[2],
                       static_cast<unsigned int>(tensor.rank));
}

[[nodiscard]] std::string renderBits(std::span<const std::uint32_t> bits, std::string_view name) {
    std::string out = std::format("inline constexpr std::uint32_t {}[] = {{", name);
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (index % 8 == 0) {
            out += "\n    ";
        }
        out += std::format("{}u,", bits[index]);
        if (index % 8 != 7 && index + 1 != bits.size()) {
            out += ' ';
        }
    }
    out += "\n};\n\n";
    return out;
}

[[nodiscard]] std::string renderBody(const schema::ModelPackage& package, std::string_view identifier) {
    std::string out = std::format("namespace mdux::ml::generated::{} {{\n\n", identifier);

    out += std::format("inline constexpr std::string_view id = \"{}\";\n\n", escape(package.id));
    out += "inline constexpr mdux::ml::LayerDesc layers[] = {\n";
    for (const schema::LayerDesc& layer : package.layers) {
        out += std::format("    {{.kind = {},\n", renderLayerKind(layer.kind));
        out += std::format("     .activation = {},\n", renderActivation(layer.activation));
        out += std::format("     .inLength = {}, .inChannels = {},\n", layer.inLength, layer.inChannels);
        out += std::format("     .outLength = {}, .outChannels = {},\n", layer.outLength, layer.outChannels);
        out += std::format("     .kernelSize = {}, .stride = {},\n", layer.kernelSize, layer.stride);
        out += std::format("     .weights = {},\n", renderTensor(layer.weights));
        out += std::format("     .bias = {}}},\n", renderTensor(layer.bias));
    }
    out += "};\n\n";

    for (std::size_t index = 0; index < package.goldens.size(); ++index) {
        out += renderBits(package.goldens[index].inputBits, std::format("golden{}InputBits", index));
        out += renderBits(package.goldens[index].expectedOutputBits, std::format("golden{}ExpectedOutputBits", index));
    }

    if (package.goldens.empty()) {
        out += "inline constexpr std::span<const mdux::ml::GoldenVector> goldens{};\n\n";
    } else {
        out += "inline constexpr mdux::ml::GoldenVector goldens[] = {\n";
        for (std::size_t index = 0; index < package.goldens.size(); ++index) {
            out += std::format("    {{.inputBits = golden{}InputBits,\n", index);
            out += std::format("     .expectedOutputBits = golden{}ExpectedOutputBits}},\n", index);
        }
        out += "};\n\n";
    }

    out += "/// The package metadata and golden vectors. Weights remain caller-supplied data.\n";
    out += "inline constexpr mdux::ml::ModelPackage model{\n";
    out += "    .id = id,\n";
    out += std::format("    .schemaVersion = {}ull,\n", package.schemaVersion);
    out += "    .weightsDigest = {\n        ";
    for (std::size_t index = 0; index < package.weightsDigest.size(); ++index) {
        out += std::format("{}u,", static_cast<unsigned int>(package.weightsDigest[index]));
        if (index % 8 == 7 && index + 1 != package.weightsDigest.size()) {
            out += "\n        ";
        } else if (index + 1 != package.weightsDigest.size()) {
            out += ' ';
        }
    }
    out += "\n    },\n";
    out += std::format("    .weightsByteLength = {}ull,\n", package.weightsByteLength);
    out += "    .layers = layers,\n";
    out += "    .goldens = goldens,\n";
    out += std::format("    .inputLength = {},\n", package.inputLength);
    out += std::format("    .outputLength = {},\n", package.outputLength);
    out += std::format("    .maxScratchFloats = {},\n", package.maxScratchFloats);
    out += "};\n\n";

    out += "// Validate the emitted form in the consumer's compiler, not at device startup.\n";
    out += "static_assert(model.validate().has_value(), \"this generated model package does not satisfy mdux.ml.schema\");\n\n";
    out += "[[nodiscard]] constexpr mdux::ml::ModelPackage package() noexcept {\n";
    out += "    return model;\n";
    out += "}\n\n";
    out += std::format("}}  // namespace mdux::ml::generated::{}\n", identifier);
    return out;
}

[[nodiscard]] std::string preamble(std::string_view packageId, std::string_view packagePath) {
    return std::format("// Generated by {} from {}.\n"
                       "//\n"
                       "// Do not edit, and do not commit: this is a build-tree rendering of the committed\n"
                       "// package.json. Weights deliberately remain in their caller-supplied blob.\n"
                       "// See tools/ml/Emit.cppm.\n"
                       "//\n"
                       "// Model: {}\n",
                       emitToolName,
                       escape(packagePath),
                       escape(packageId));
}

}  // namespace

std::string identifierForModel(std::string_view packageId) {
    std::string out{"model_"};
    out.reserve(out.size() + packageId.size());
    for (const char character : packageId) {
        const bool alnum = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        out.push_back(alnum ? character : '_');
    }
    return out;
}

std::optional<EmitOutputs> renderModel(const std::filesystem::path& packagePath, std::vector<cli::Diagnostic>& diagnostics) {
    const std::string                packageDisplay = packagePath.generic_string();
    const std::optional<std::string> bytes          = readFile(packagePath);
    if (!bytes.has_value()) {
        report(diagnostics,
               packageDisplay,
               packageUnreadable,
               "cannot read package.json",
               "Bake the model first with `cmake --build <dir> --target mdux-bake-update`.");
        return std::nullopt;
    }

    auto loaded = loadPackage(*bytes, packageDisplay);
    if (!loaded.has_value()) {
        diagnostics.push_back(std::move(loaded.error()));
        return std::nullopt;
    }
    const schema::ModelPackage package = (*loaded)->view();

    EmitOutputs outputs;
    outputs.stem = identifierForModel(package.id);
    if (outputs.stem.contains("__")) {
        report(diagnostics,
               packageDisplay,
               identifierReserved,
               std::format("the id '{}' maps to '{}', which is a reserved identifier", package.id, outputs.stem),
               "avoid two adjacent separators in a model id");
        return std::nullopt;
    }
    outputs.moduleName = "mdux.ml.generated." + outputs.stem;

    const std::string body = renderBody(package, outputs.stem);
    const std::string head = preamble(package.id, packageDisplay);
    outputs.moduleSource   = head + "\nmodule;\n\nexport module " + outputs.moduleName + ";\n\nimport std;\nimport mdux.ml.schema;\n\nexport " + body;
    outputs.headerSource   = head
                           + "\n#pragma once\n\n"
                             "// Include after `import std;` and `import mdux.ml.schema;`. A header\n"
                             "// cannot import named modules without reintroducing include/import\n"
                             "// ordering failures.\n\n"
                           + body;
    return outputs;
}

bool writeModel(const EmitOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, outputDir.generic_string(), outputUnwritable, "cannot create output directory: " + code.message());
        return false;
    }

    const auto writeIfChanged = [&](const std::filesystem::path& path, const std::string& content) {
        if (const std::optional<std::string> existing = readFile(path); existing.has_value() && *existing == content) {
            return true;
        }
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        if (!file) {
            report(diagnostics, path.generic_string(), outputUnwritable, "cannot open for writing");
            return false;
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            report(diagnostics, path.generic_string(), outputUnwritable, "write failed");
            return false;
        }
        return true;
    };

    bool ok = writeIfChanged(outputDir / (outputs.stem + ".cppm"), outputs.moduleSource);
    ok      = writeIfChanged(outputDir / (outputs.stem + ".hpp"), outputs.headerSource) && ok;
    return ok;
}

}  // namespace mdux::tools::ml
