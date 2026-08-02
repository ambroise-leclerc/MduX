/**
 * @brief Implementation of the shader package C++ emitter.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-007 Evidence pipeline doctrine
 */
module;

module mdux.tools.shaderemit;

import std;
import mdux.evidence.digest;
import mdux.shader.schema;
import mdux.tools.cli;

namespace mdux::tools::shaderemit {

namespace {

// Stable diagnostic codes; see docs/governance/schemas/diagnostic.schema.json.
constexpr std::string_view packageUnreadable = "SHE001";
constexpr std::string_view packageUnparsed = "SHE002";
constexpr std::string_view sidecarUnreadable = "SHE003";
constexpr std::string_view sidecarMismatch = "SHE004";
constexpr std::string_view outputUnwritable = "SHE005";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::string_view code,
            std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file = std::move(file),
                                          .code = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message = std::move(message),
                                          .fixHint = std::move(fixHint)});
}

[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
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
        if (character == '"' || character == '\\') {
            out.push_back('\\');
        }
        out.push_back(character);
    }
    return out;
}

/// The SPIR-V as a C array initialiser, twelve bytes per line.
///
/// Twelve rather than sixteen so a line stays inside a hundred columns with the indent, which
/// matters only because a reviewer who does open this file should not have it wrap.
[[nodiscard]] std::string renderPayload(std::span<const std::byte> bytes) {
    std::string out;
    // Six characters per byte ("0xNN, ") plus a newline every twelve.
    out.reserve(bytes.size() * 6 + bytes.size() / 12 + 16);
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i % 12 == 0) {
            out += "\n    ";
        }
        const auto value = std::to_integer<std::uint8_t>(bytes[i]);
        out += "0x";
        out.push_back(digits[(value >> 4) & 0x0fu]);
        out.push_back(digits[value & 0x0fu]);
        out += ',';
        if (i % 12 != 11 && i + 1 != bytes.size()) {
            out += ' ';
        }
    }
    out += '\n';
    return out;
}

[[nodiscard]] std::string renderStage(shader::Stage stage) {
    return std::string{"mdux::shader::Stage::"} +
           (stage == shader::Stage::Vertex ? "Vertex" : "Fragment");
}

[[nodiscard]] std::string renderStageMask(shader::StageMask mask) {
    std::string out;
    for (std::size_t i = 0; i < shader::stageWireValues.size(); ++i) {
        const auto stage = static_cast<shader::Stage>(i);
        if ((mask & shader::stageBit(stage)) == 0) {
            continue;
        }
        if (!out.empty()) {
            out += " | ";
        }
        out += "mdux::shader::stageBit(" + renderStage(stage) + ")";
    }
    return out.empty() ? std::string{"0"} : out;
}

[[nodiscard]] std::string renderDescriptorKind(shader::DescriptorKind kind) {
    static constexpr std::array<std::string_view, 5> names{
        "UniformBuffer", "StorageBuffer", "CombinedImageSampler", "SampledImage", "Sampler"};
    const auto index = static_cast<std::size_t>(kind);
    return std::string{"mdux::shader::DescriptorKind::"} +
           std::string{index < names.size() ? names[index] : names[0]};
}

/// Everything between the module/header preamble and its closing brace. One function, so the two
/// outputs cannot describe different bytes or a different contract.
[[nodiscard]] std::string renderBody(const shader::ShaderPackage& package,
                                     std::span<const std::byte> sidecar,
                                     std::string_view identifier) {
    std::string out;

    out += "namespace mdux::shader::generated::" + std::string{identifier} + " {\n\n";

    out += "/// The package id this data was generated from.\n";
    out += "inline constexpr std::string_view id = \"" + escape(package.header.id) + "\";\n\n";

    out += "/// SHA-256 of the sidecar, as recorded in the committed package.json. Present so a\n";
    out += "/// consumer can assert the bytes it linked are the bytes that were reviewed.\n";
    out += "inline constexpr std::string_view spirvSha256 = \"";
    const std::array<char, 64> hex = evidence::toHex(package.sidecarSha256);
    out += std::string{hex.data(), hex.size()};
    out += "\";\n\n";

    out += "/// The whole sidecar. A C array rather than std::array on purpose - see Emit.cppm.\n";
    out += "inline constexpr unsigned char spirvBytes[] = {";
    out += renderPayload(sidecar);
    out += "};\n\n";

    out += "inline constexpr mdux::shader::ModuleView modules[] = {\n";
    for (const shader::ShaderModule& module : package.modules) {
        out += "    {.id = \"" + escape(module.id) + "\",\n";
        out += "     .stage = " + renderStage(module.stage) + ",\n";
        out += "     .entryPoint = \"" + escape(module.entryPoint) + "\",\n";
        out += "     .byteOffset = " + std::to_string(module.byteOffset) + ",\n";
        out += "     .byteLength = " + std::to_string(module.byteLength) + "},\n";
    }
    out += "};\n\n";

    // An empty C array is ill-formed, so an empty contract is spelled as an empty span rather
    // than an array of zero elements. Both branches produce the same accessor signature.
    if (package.descriptors.empty()) {
        out += "/// This package declares no descriptors.\n";
        out += "inline constexpr std::span<const mdux::shader::DescriptorBinding> descriptors{};\n\n";
    } else {
        out += "inline constexpr mdux::shader::DescriptorBinding descriptors[] = {\n";
        for (const shader::DescriptorBinding& binding : package.descriptors) {
            out += "    {.set = " + std::to_string(binding.set) + ",\n";
            out += "     .binding = " + std::to_string(binding.binding) + ",\n";
            out += "     .kind = " + renderDescriptorKind(binding.kind) + ",\n";
            out += "     .count = " + std::to_string(binding.count) + ",\n";
            out += "     .stages = static_cast<mdux::shader::StageMask>(" +
                   renderStageMask(binding.stages) + ")},\n";
        }
        out += "};\n\n";
    }

    if (package.pushConstants.empty()) {
        out += "/// This package declares no push constants.\n";
        out += "inline constexpr std::span<const mdux::shader::PushConstantRange> "
               "pushConstants{};\n\n";
    } else {
        out += "inline constexpr mdux::shader::PushConstantRange pushConstants[] = {\n";
        for (const shader::PushConstantRange& range : package.pushConstants) {
            out += "    {.offset = " + std::to_string(range.offset) + ",\n";
            out += "     .size = " + std::to_string(range.size) + ",\n";
            out += "     .stages = static_cast<mdux::shader::StageMask>(" +
                   renderStageMask(range.stages) + ")},\n";
        }
        out += "};\n\n";
    }

    out += "/// The whole package, as one value a renderer can take by copy.\n";
    out += "[[nodiscard]] inline mdux::shader::PackageView package() noexcept {\n";
    out += "    return mdux::shader::PackageView{\n";
    out += "        .id = id,\n";
    out += "        .spirv = std::as_bytes(std::span{spirvBytes}),\n";
    out += "        .modules = std::span{modules},\n";
    out += "        .descriptors = descriptors,\n";
    out += "        .pushConstants = pushConstants,\n";
    out += "    };\n";
    out += "}\n\n";

    out += "}  // namespace mdux::shader::generated::" + std::string{identifier} + "\n";
    return out;
}

[[nodiscard]] std::string preamble(std::string_view packageId, std::string_view packagePath) {
    std::string out;
    out += "// Generated by mdux-shaderemit from " + std::string{packagePath} + ".\n";
    out += "//\n";
    out += "// Do not edit, and do not commit: this file is a mechanical rendering of a committed\n";
    out += "// artifact, regenerated on every build. The reviewed source of truth is the JSON and\n";
    out += "// the digests beside it, not these bytes. See tools/shader/Emit.cppm.\n";
    out += "//\n";
    out += "// Package: " + std::string{packageId} + "\n";
    return out;
}

}  // namespace

std::string identifierFor(std::string_view packageId) {
    std::string out;
    out.reserve(packageId.size());
    for (const char character : packageId) {
        const bool alnum = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9');
        out.push_back(alnum ? character : '_');
    }
    // A C++ identifier may not start with a digit; a package id may.
    if (!out.empty() && out.front() >= '0' && out.front() <= '9') {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::optional<EmitOutputs> render(const std::filesystem::path& packagePath,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    const std::string packageDisplay = packagePath.generic_string();

    auto packageBytes = readFile(packagePath);
    if (!packageBytes.has_value()) {
        report(diagnostics, packageDisplay, packageUnreadable, "cannot read package.json",
               "Run `cmake --build <dir> --target mdux-bake-update` to produce it.");
        return std::nullopt;
    }

    const std::string_view packageText{reinterpret_cast<const char*>(packageBytes->data()),
                                       packageBytes->size()};
    auto package = shader::ShaderPackage::parse(packageText);
    if (!package.has_value()) {
        report(diagnostics, packageDisplay, packageUnparsed,
               std::string{"package.json is not a valid shader package: "} +
                   std::string{shader::describe(package.error())});
        return std::nullopt;
    }

    const std::filesystem::path sidecarPath = packagePath.parent_path() / package->sidecarPath;
    auto sidecar = readFile(sidecarPath);
    if (!sidecar.has_value()) {
        report(diagnostics, sidecarPath.generic_string(), sidecarUnreadable,
               "cannot read the sidecar the package names");
        return std::nullopt;
    }

    // The package's own claims, checked before they are frozen into source. Generating code from
    // a sidecar that does not match the digest under review would put unreviewed bytes into the
    // binary while every artifact check stayed green.
    if (sidecar->size() != package->sidecarByteLength ||
        evidence::sha256(*sidecar) != package->sidecarSha256) {
        report(diagnostics, sidecarPath.generic_string(), sidecarMismatch,
               "the sidecar does not match the digest recorded in package.json",
               "Re-bake with `cmake --build <dir> --target mdux-bake-update`; do not hand-edit "
               "anything under generated/.");
        return std::nullopt;
    }

    EmitOutputs outputs;
    outputs.stem = identifierFor(package->header.id);
    outputs.moduleName = "mdux.shader.generated." + outputs.stem;

    const std::string body = renderBody(*package, *sidecar, outputs.stem);
    const std::string head = preamble(package->header.id, packageDisplay);

    outputs.moduleSource = head + "\nmodule;\n\nexport module " + outputs.moduleName +
                           ";\n\nimport std;\nimport mdux.shader.schema;\n\nexport " + body;

    outputs.headerSource = head +
                           "\n#pragma once\n\n"
                           "// The header form assumes <span>, <string_view> and mdux.shader.schema\n"
                           "// are already available to the including translation unit, exactly as\n"
                           "// MduXTest.hpp assumes `import std;` has been seen. A header cannot\n"
                           "// import a named module, and including <span> here would reintroduce\n"
                           "// the include-before-import ordering this repository keeps avoiding.\n\n" +
                           body;

    return outputs;
}

bool write(const EmitOutputs& outputs, const std::filesystem::path& outputDir,
           std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, outputDir.generic_string(), outputUnwritable,
               "cannot create output directory: " + code.message());
        return false;
    }

    const auto writeIfChanged = [&](const std::filesystem::path& path,
                                    const std::string& content) {
        // Rewriting an unchanged file would restamp it and force every consumer to recompile on
        // every build, which for a file holding a few thousand bytes of shader is not free.
        if (auto existing = readFile(path); existing.has_value()) {
            const std::string_view text{reinterpret_cast<const char*>(existing->data()),
                                        existing->size()};
            if (text == content) {
                return true;
            }
        }
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        if (!file) {
            report(diagnostics, path.generic_string(), outputUnwritable,
                   "cannot open for writing");
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
    ok = writeIfChanged(outputDir / (outputs.stem + ".hpp"), outputs.headerSource) && ok;
    return ok;
}

}  // namespace mdux::tools::shaderemit
