/**
 * @file Emit.cpp
 * @brief Implementation of the screen package's two C++ renderings.
 */

module;

module mdux.tools.medui.emit;

import std;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.package;

namespace mdux::tools::medui {

namespace {

namespace ms = mdux::medui;

/// Stable diagnostic codes for this tool. A malformed package reports the reader's `SCP0NN` codes
/// instead, since the reader is the thing that found the problem and names it more precisely.
constexpr std::string_view packageUnreadable  = "SCE001";
constexpr std::string_view outputUnwritable   = "SCE002";
constexpr std::string_view identifierReserved = "SCE003";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::string_view code, std::string message, std::string fixHint = {}) {
    cli::Diagnostic diagnostic;
    diagnostic.file     = std::move(file);
    diagnostic.code     = std::string{code};
    diagnostic.severity = cli::Severity::Error;
    diagnostic.message  = std::move(message);
    diagnostic.fixHint  = std::move(fixHint);
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    // tellg() answers -1 on a stream error rather than throwing, and sizing the string before the
    // check would turn that into a request for 2^64-1 bytes.
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

/**
 * @brief A validated name as the body of a C++ string literal.
 *
 * Escapes every byte that could end the literal early or change what follows it, rather than only
 * the two that obviously could. That is not defensiveness for its own sake: the `.medui` lexer
 * decodes `\\n` and `\\t` inside a source string, semantic analysis accepts an unrestricted `String`
 * for `requirement`, `source`, `stream_source` and `template`, and the schema then asks only whether
 * a required name is non-empty. A `requirement: "REQ\\nHALT"` is therefore a legal screen whose
 * compiled package is valid, and emitting its decoded newline raw would not merely produce an
 * unhelpful compiler error - it would end the literal and let the rest of the value be read as C++
 * tokens. A code generator must not have that property.
 *
 * Control bytes become three-digit octal escapes rather than `\\xNN`, because a hex escape is greedy:
 * `"\\x0aBC"` is one enormous escape, while `"\\012BC"` is a newline followed by two letters. Bytes at
 * or above 0x80 are left alone - they are continuation bytes of a name that is already valid UTF-8,
 * and the generated file is UTF-8.
 */
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
        if (byte < 0x20 || byte == 0x7F) {
            out += std::format("\\{:03o}", byte);
            continue;
        }
        out.push_back(character);
    }
    return out;
}

[[nodiscard]] std::string quoted(std::string_view text) {
    return std::format("\"{}\"", escape(text));
}

/// Appends `.member = "value"` when the name is present. An absent optional name is omitted, which
/// is legal because every spec member carries a default initialiser - #218 made that a property of
/// the type precisely so generated code would not have to spell out what a component did not say.
void appendName(std::string& out, bool& first, std::string_view member, std::string_view value) {
    if (value.empty()) {
        return;
    }
    out  += std::format("{}.{} = {}", first ? "" : ", ", member, quoted(value));
    first = false;
}

/// A closed-set member, emitted as the enumerator rather than as its wire spelling: generated code
/// that named a string would reintroduce the parse this whole path exists to remove. `Unspecified`
/// is never written, because `validate()` has already refused it - so an omitted member here is a
/// screen that would not have compiled.
template <typename Member>
void appendMember(std::string& out, bool& first, std::string_view member, std::string_view type, Member value) {
    const std::string_view spelling = ms::toWire(value);
    if (spelling.empty()) {
        return;
    }
    out  += std::format("{}.{} = mdux::medui::{}::{}", first ? "" : ", ", member, type, spelling);
    first = false;
}

void appendSpan(std::string& out, bool& first, std::string_view member, std::string_view arrayName) {
    out  += std::format("{}.{} = {}", first ? "" : ", ", member, arrayName);
    first = false;
}

/// The name of the array holding one node's state keys or per-state tints.
///
/// Indexed by node position rather than by node id: an id may contain `-` and `_`, so sanitising two
/// different ids could produce one array name, and the collision would be a compile error in
/// generated code that names no cause. The node's id goes in a comment beside the array instead.
[[nodiscard]] std::string arrayName(std::string_view what, std::size_t index) {
    return std::format("{}OfNode{}", what, index);
}

/// The `StatusIndicator` arrays a screen needs, declared before the node table that views them.
[[nodiscard]] std::string renderStateArrays(const ms::ScreenPackage& package) {
    std::string out;
    for (std::size_t index = 0; index < package.nodes.size(); ++index) {
        const auto* status = std::get_if<ms::StatusIndicatorSpec>(&package.nodes[index].payload);
        if (status == nullptr) {
            continue;
        }
        out += std::format("/// The states node '{}' shows, and the tint each one carries.\n", escape(package.nodes[index].id));
        out += std::format("inline constexpr std::string_view {}[] = {{", arrayName("stateKeys", index));
        for (std::size_t key = 0; key < status->stateKeys.size(); ++key) {
            out += std::format("{}{}", key == 0 ? "" : ", ", quoted(status->stateKeys[key]));
        }
        out += "};\n";
        if (!status->colorTokens.empty()) {
            out += std::format("inline constexpr std::string_view {}[] = {{", arrayName("colorTokens", index));
            for (std::size_t token = 0; token < status->colorTokens.size(); ++token) {
                out += std::format("{}{}", token == 0 ? "" : ", ", quoted(status->colorTokens[token]));
            }
            out += "};\n";
        }
        out += "\n";
    }
    return out;
}

[[nodiscard]] std::string renderApprovals(const ms::ScreenPackage& package) {
    if (package.approvedTextPackages.empty()) {
        return "/// This screen was compiled without text packages.\n"
               "inline constexpr std::span<const mdux::medui::TextPackageApproval> approvedTextPackages{};\n\n";
    }

    std::string out = "/// The exact per-locale text packages measured when this screen was compiled.\n"
                      "inline constexpr mdux::medui::TextPackageApproval approvedTextPackages[] = {\n";
    for (const ms::TextPackageApproval& approval : package.approvedTextPackages) {
        out += std::format("    {{.locale = {}, .packageId = {}, .packageSha256 = {{", quoted(approval.locale), quoted(approval.packageId));
        for (std::size_t index = 0; index < approval.packageSha256.size(); ++index) {
            if (index != 0) {
                out += ", ";
            }
            out += std::to_string(approval.packageSha256[index]);
        }
        out += "}},\n";
    }
    out += "};\n\n";
    return out;
}

[[nodiscard]] std::string renderImageApprovals(const ms::ScreenPackage& package) {
    if (package.approvedImagePackages.empty()) {
        return "/// This screen was compiled without image packages.\n"
               "inline constexpr std::span<const mdux::medui::ImagePackageApproval> approvedImagePackages{};\n\n";
    }
    std::string out = "/// The exact image packages measured when this screen was compiled.\n"
                      "inline constexpr mdux::medui::ImagePackageApproval approvedImagePackages[] = {\n";
    for (const ms::ImagePackageApproval& approval : package.approvedImagePackages) {
        out += std::format("    {{.packageId = {}, .packageSha256 = {{", quoted(approval.packageId));
        for (std::size_t index = 0; index < approval.packageSha256.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += std::to_string(approval.packageSha256[index]);
        }
        out += std::format("}}, .width = {}, .height = {}}},\n", approval.width, approval.height);
    }
    out += "};\n\n";
    return out;
}

/**
 * @brief One node's payload as a spec initialiser.
 *
 * Members are written in declaration order, because a designated initialiser out of order is
 * ill-formed - so this function and `mdux.medui.schema` are coupled, and the `static_assert` in the
 * generated source is what reports a divergence at the point it happens.
 */
[[nodiscard]] std::string renderPayload(const ms::CompiledNode& node, std::size_t index) {
    std::string out;
    bool        first = true;

    if (const auto* panel = std::get_if<ms::PanelSpec>(&node.payload)) {
        out = "mdux::medui::PanelSpec{";
        appendName(out, first, "colorToken", panel->colorToken);
    } else if (const auto* label = std::get_if<ms::LabelSpec>(&node.payload)) {
        out = "mdux::medui::LabelSpec{";
        appendName(out, first, "textKey", label->textKey);
        appendName(out, first, "colorToken", label->colorToken);
    } else if (const auto* clock = std::get_if<ms::ClockSpec>(&node.payload)) {
        out = "mdux::medui::ClockSpec{";
        appendMember(out, first, "format", "ClockFormat", clock->format);
    } else if (const auto* image = std::get_if<ms::ImageSpec>(&node.payload)) {
        out = "mdux::medui::ImageSpec{";
        appendName(out, first, "source", image->source);
    } else if (const auto* viewport = std::get_if<ms::VulkanViewportSpec>(&node.payload)) {
        out = "mdux::medui::VulkanViewportSpec{";
        appendName(out, first, "streamSource", viewport->streamSource);
    } else if (const auto* trace = std::get_if<ms::SignalTraceSpec>(&node.payload)) {
        out = "mdux::medui::SignalTraceSpec{";
        appendName(out, first, "streamSource", trace->streamSource);
        appendName(out, first, "colorToken", trace->colorToken);
    } else if (const auto* button = std::get_if<ms::ButtonSpec>(&node.payload)) {
        out = "mdux::medui::ButtonSpec{";
        appendName(out, first, "labelKey", button->labelKey);
        appendName(out, first, "colorToken", button->colorToken);
        appendName(out, first, "source", button->source);
        appendName(out, first, "requirement", button->requirement);
    } else if (const auto* critical = std::get_if<ms::CriticalButtonSpec>(&node.payload)) {
        out = "mdux::medui::CriticalButtonSpec{";
        appendName(out, first, "requirement", critical->requirement);
        appendName(out, first, "labelKey", critical->labelKey);
        appendName(out, first, "colorToken", critical->colorToken);
        appendMember(out, first, "onPress", "SystemEvent", critical->onPress);
    } else if (const auto* numeric = std::get_if<ms::NumericDisplaySpec>(&node.payload)) {
        out = "mdux::medui::NumericDisplaySpec{";
        appendName(out, first, "requirement", numeric->requirement);
        appendName(out, first, "templateId", numeric->templateId);
        appendName(out, first, "source", numeric->source);
        appendName(out, first, "colorToken", numeric->colorToken);
    } else if (const auto* status = std::get_if<ms::StatusIndicatorSpec>(&node.payload)) {
        out = "mdux::medui::StatusIndicatorSpec{";
        appendName(out, first, "requirement", status->requirement);
        appendName(out, first, "source", status->source);
        appendSpan(out, first, "stateKeys", arrayName("stateKeys", index));
        if (!status->colorTokens.empty()) {
            appendSpan(out, first, "colorTokens", arrayName("colorTokens", index));
        }
    } else if (const auto* input = std::get_if<ms::TextInputSpec>(&node.payload)) {
        out = "mdux::medui::TextInputSpec{";
        appendName(out, first, "source", input->source);
        appendName(out, first, "colorToken", input->colorToken);
        out  += std::format("{}.maxLength = {}", first ? "" : ", ", input->maxLength);
        first = false;
        appendName(out, first, "charset", input->charset);
        appendName(out, first, "requirement", input->requirement);
    } else {
        // Unreachable for a package that validated, which is the only kind that reaches here.
        throw std::logic_error("a node carries a payload this emitter does not know");
    }
    out += "}";
    return out;
}

/// The screen value, its compile-time check and its accessor: everything after the node table, which
/// the empty and non-empty branches of `renderBody()` share.
[[nodiscard]] std::string renderTail(const ms::ScreenPackage& package, std::string_view identifier) {
    std::string out;

    out += "/// The screen itself, in read-only memory. A device holds this and parses nothing.\n";
    out += "inline constexpr mdux::medui::ScreenPackage screen{\n";
    out += "    .id = id,\n";
    out += std::format("    .schemaVersion = {},\n", package.schemaVersion);
    out += std::format("    .surfaceWidth = {},\n", package.surfaceWidth);
    out += std::format("    .surfaceHeight = {},\n", package.surfaceHeight);
    out += "    .approvedTextPackages = approvedTextPackages,\n";
    out += "    .approvedImagePackages = approvedImagePackages,\n";
    out += "    .nodes = nodes,\n";
    out += std::format("    .budget = {{.maxVertices = {}, .maxIndices = {}, .maxCommands = {}}},\n",
                       package.budget.maxVertices,
                       package.budget.maxIndices,
                       package.budget.maxCommands);
    out += "};\n\n";

    out += "// The screen is checked where it is defined, so a malformed one is a build failure in\n";
    out += "// whatever links it rather than a startup failure on a device.\n";
    out += "static_assert(screen.validate().has_value(), \"this compiled screen does not satisfy mdux.medui.schema\");\n\n";

    out += "/// The screen, as one value a runtime can take by copy.\n";
    out += "[[nodiscard]] constexpr mdux::medui::ScreenPackage package() noexcept {\n";
    out += "    return screen;\n";
    out += "}\n\n";

    out += std::format("}}  // namespace mdux::medui::generated::{}\n", identifier);
    return out;
}

/// Everything between the module or header preamble and its closing brace. One function, so the two
/// outputs cannot describe different screens or a different contract.
[[nodiscard]] std::string renderBody(const ms::ScreenPackage& package, std::string_view identifier) {
    std::string out;

    out += std::format("namespace mdux::medui::generated::{} {{\n\n", identifier);

    out += "/// The package id this screen was generated from.\n";
    out += std::format("inline constexpr std::string_view id = {};\n\n", quoted(package.id));

    out += renderApprovals(package);
    out += renderImageApprovals(package);
    out += renderStateArrays(package);

    // A screen with nothing to draw is valid - `ScreenPackage::validate()` permits it, with an empty
    // budget, and `medui-schema-budget` pins that. An empty C array is not valid C++, so the empty
    // case is spelled as an empty span, exactly as the shader emitter spells an empty descriptor
    // set. Both branches leave `nodes` convertible to the span `ScreenPackage` holds.
    if (package.nodes.empty()) {
        out += "/// This screen resolves to no nodes.\n";
        out += "inline constexpr std::span<const mdux::medui::CompiledNode> nodes{};\n\n";
        return out + renderTail(package, identifier);
    }

    out += "/// Every node the compiler resolved, in the order the package lists them.\n";
    out += "inline constexpr mdux::medui::CompiledNode nodes[] = {\n";
    for (std::size_t index = 0; index < package.nodes.size(); ++index) {
        const ms::CompiledNode& node = package.nodes[index];
        out                         += std::format("    {{.id = {},\n", quoted(node.id));
        out                         += std::format("     .bounds = {{.x = {}, .y = {}, .width = {}, .height = {}}},\n",
                           node.bounds.x,
                           node.bounds.y,
                           node.bounds.width,
                           node.bounds.height);
        out                         += std::format("     .payload = {}}},\n", renderPayload(node, index));
    }
    out += "};\n\n";

    return out + renderTail(package, identifier);
}

/**
 * @brief The provenance comment both outputs open with.
 *
 * The id and the path go through the same escaper the string literals use, which is not
 * belt-and-braces: a `//` comment ends at a newline, and a package id is a non-empty string the
 * schema does not otherwise restrict, so an id carrying a control character would end the comment
 * and leave what followed it as generated C++ source. A trailing backslash would do the opposite -
 * splice the *next* line into the comment - and the same escaper removes that too. Escaping a
 * comment is unusual enough to say why here: this is a code generator, so every value it writes out
 * has to be unable to change the structure of what it writes.
 */
[[nodiscard]] std::string preamble(std::string_view packageId, std::string_view packagePath) {
    std::string out;
    out += std::format("// Generated by {} from {}.\n", emitToolName, escape(packagePath));
    out += "//\n";
    out += "// Do not edit, and do not commit: this file is a mechanical rendering of a committed\n";
    out += "// artifact, regenerated on every build. The reviewed source of truth is the JSON beside\n";
    out += "// the digests, not these initialisers. See tools/medui/Emit.cppm.\n";
    out += "//\n";
    out += std::format("// Screen: {}\n", escape(packageId));
    return out;
}

}  // namespace

std::string identifierForScreen(std::string_view packageId) {
    // Unconditionally prefixed, which is what makes the result an identifier rather than merely
    // identifier-shaped. A package id is a slug, and `class`, `namespace`, `module` and `import` are
    // all legal slugs; mapping them to themselves produced a namespace and a module name no compiler
    // accepts, while both implementations of the rule agreed on that invalid answer, so the parity
    // test could not see it. The prefix also removes the leading-digit case an earlier revision
    // handled with a second rule.
    std::string out{"screen_"};
    out.reserve(out.size() + packageId.size());
    for (const char character : packageId) {
        const bool alnum = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        out.push_back(alnum ? character : '_');
    }
    return out;
}

std::optional<EmitOutputs> renderScreen(const std::filesystem::path& packagePath, std::vector<cli::Diagnostic>& diagnostics) {
    const std::string packageDisplay = packagePath.generic_string();

    const std::optional<std::string> bytes = readFile(packagePath);
    if (!bytes.has_value()) {
        report(diagnostics,
               packageDisplay,
               packageUnreadable,
               "cannot read package.json",
               "Compile the screen first; a screen's artifacts are produced by mdux-meduic.");
        return std::nullopt;
    }

    // The reader validates what it read, so everything below this point works on a screen that has
    // already satisfied the schema once. The static_assert the rendering carries is the second gate,
    // over the emitted form rather than the parsed one.
    PackageReadResult read = readPackage(*bytes, packageDisplay);
    if (!read.ok()) {
        diagnostics.insert(diagnostics.end(), read.diagnostics.begin(), read.diagnostics.end());
        return std::nullopt;
    }

    const ms::ScreenPackage package = read.document.package();

    EmitOutputs outputs;
    outputs.stem = identifierForScreen(package.id);

    // Two adjacent separators in an id map to `__`, which is reserved to the implementation
    // everywhere in a program. One `_` per separator keeps the mapping injective - collapsing a run
    // would let two screens claim one filename - so the reserved case is refused rather than
    // rewritten, and `mdux_emit_screen_package()` refuses it at configure time for the same reason.
    if (outputs.stem.contains("__")) {
        report(diagnostics,
               packageDisplay,
               identifierReserved,
               std::format("the id '{}' maps to '{}', which is a reserved identifier", package.id, outputs.stem),
               "avoid two adjacent separators in a screen id");
        return std::nullopt;
    }

    outputs.moduleName = "mdux.medui.generated." + outputs.stem;

    const std::string body = renderBody(package, outputs.stem);
    const std::string head = preamble(package.id, packageDisplay);

    outputs.moduleSource = head + "\nmodule;\n\nexport module " + outputs.moduleName + ";\n\nimport std;\nimport mdux.medui.schema;\n\nexport " + body;

    outputs.headerSource = head
                           + "\n#pragma once\n\n"
                             "// The header form assumes <span>, <string_view> and mdux.medui.schema are already\n"
                             "// available to the including translation unit, exactly as the shader emitter's header\n"
                             "// does. A header cannot import a named module, and including <span> here would\n"
                             "// reintroduce the include-before-import ordering this repository keeps avoiding.\n\n"
                           + body;

    return outputs;
}

bool writeScreen(const EmitOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, outputDir.generic_string(), outputUnwritable, "cannot create output directory: " + code.message());
        return false;
    }

    const auto writeIfChanged = [&](const std::filesystem::path& path, const std::string& content) {
        // Rewriting an unchanged file would restamp it and force every consumer to recompile on
        // every build, which for a module interface is not free.
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

}  // namespace mdux::tools::medui
