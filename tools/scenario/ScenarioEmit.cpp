/**
 * @file ScenarioEmit.cpp
 * @brief Implementation of the scenario C++ emitter.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-012 What a compiled artifact emits
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 */
module;

module mdux.tools.scenarioemit;

import std;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.scenario;
import mdux.tools.scenario.script;

namespace mdux::tools::scenario {

namespace ms = mdux::medui;

namespace {

// Stable diagnostic codes; see docs/governance/schemas/diagnostic.schema.json.
constexpr std::string_view scenarioUnreadable = "SCE001";
constexpr std::string_view scenarioMalformed  = "SCE002";
constexpr std::string_view outputUnwritable   = "SCE003";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::string_view code,
            std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = std::move(file),
                                          .code     = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fixHint)});
}

[[nodiscard]] std::optional<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

/// Escapes a value so it cannot change the structure of a `//` comment or a string literal -
/// `preamble()`'s reasoning in `tools/medui/Emit.cpp`.
[[nodiscard]] std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

[[nodiscard]] std::string quotedLiteral(std::string_view text) { return "\"" + escape(text) + "\""; }

[[nodiscard]] std::string pointerKindEnum(ms::PointerKind k) {
    return std::string{"mdux::medui::PointerKind::"}
           + (k == ms::PointerKind::Down ? "Down" : k == ms::PointerKind::Up ? "Up" : k == ms::PointerKind::Move ? "Move" : "Cancel");
}
[[nodiscard]] std::string keyKindEnum(ms::KeyKind k) {
    return std::string{"mdux::medui::KeyKind::"} + (k == ms::KeyKind::Up ? "Up" : "Down");
}
[[nodiscard]] std::string keyCodeEnum(ms::KeyCode c) {
    return "mdux::medui::KeyCode::" + std::string{ms::toWire(c)};
}
[[nodiscard]] std::string focusKindEnum(ms::FocusKind k) {
    return std::string{"mdux::medui::FocusKind::"} + (k == ms::FocusKind::Leave ? "Leave" : "Enter");
}
[[nodiscard]] std::string systemEventEnum(ms::SystemEvent e) {
    return std::string{"mdux::medui::SystemEvent::"}
           + (e == ms::SystemEvent::TriggerHalt ? "TriggerHalt" : e == ms::SystemEvent::NoOp ? "NoOp" : "Unspecified");
}
[[nodiscard]] std::string expectKindEnum(ms::ExpectKind k) {
    switch (k) {
        case ms::ExpectKind::Clock:        return "mdux::medui::ExpectKind::Clock";
        case ms::ExpectKind::Field:        return "mdux::medui::ExpectKind::Field";
        case ms::ExpectKind::RefusedEdits: return "mdux::medui::ExpectKind::RefusedEdits";
        case ms::ExpectKind::Action:       return "mdux::medui::ExpectKind::Action";
        case ms::ExpectKind::ButtonSource: return "mdux::medui::ExpectKind::ButtonSource";
        case ms::ExpectKind::Reading:      return "mdux::medui::ExpectKind::Reading";
        case ms::ExpectKind::State:        return "mdux::medui::ExpectKind::State";
        case ms::ExpectKind::LatchArmed:   return "mdux::medui::ExpectKind::LatchArmed";
        case ms::ExpectKind::FrameStat:    return "mdux::medui::ExpectKind::FrameStat";
        case ms::ExpectKind::Overflow:     return "mdux::medui::ExpectKind::Overflow";
        case ms::ExpectKind::Unspecified:  return "mdux::medui::ExpectKind::Unspecified";
    }
    return "mdux::medui::ExpectKind::Unspecified";
}
[[nodiscard]] std::string frameStatFieldEnum(ms::FrameStatField f) {
    switch (f) {
        case ms::FrameStatField::Nodes:    return "mdux::medui::FrameStatField::Nodes";
        case ms::FrameStatField::Rects:    return "mdux::medui::FrameStatField::Rects";
        case ms::FrameStatField::Deferred: return "mdux::medui::FrameStatField::Deferred";
        case ms::FrameStatField::Traces:   return "mdux::medui::FrameStatField::Traces";
        case ms::FrameStatField::Readings: return "mdux::medui::FrameStatField::Readings";
        case ms::FrameStatField::States:   return "mdux::medui::FrameStatField::States";
        case ms::FrameStatField::Fields:   return "mdux::medui::FrameStatField::Fields";
        case ms::FrameStatField::Unspecified: return "mdux::medui::FrameStatField::Unspecified";
    }
    return "mdux::medui::FrameStatField::Unspecified";
}

[[nodiscard]] std::string clockInit(const ms::CivilTime& c) {
    return std::format("{{.year = {}, .month = {}, .day = {}, .hour = {}, .minute = {}, .second = {}}}",
                       c.year, static_cast<unsigned>(c.month), static_cast<unsigned>(c.day),
                       static_cast<unsigned>(c.hour), static_cast<unsigned>(c.minute), static_cast<unsigned>(c.second));
}

[[nodiscard]] std::string u32ArrayInit(const std::u32string& text) {
    std::string out = "{";
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += std::format("static_cast<char32_t>({}u)", static_cast<std::uint32_t>(text[i]));
    }
    out += "}";
    return out;
}

/// Renders the body common to the module and header.
[[nodiscard]] std::string renderBody(const Script& script, std::string_view identifier) {
    std::string out;
    out += std::format("namespace mdux::medui::generated::{} {{\n\n", identifier);

    out += std::format("inline constexpr std::string_view id = {};\n", quotedLiteral(script.id));
    out += std::format("inline constexpr std::string_view screenId = {};\n\n", quotedLiteral(script.screenId));

    // Requirements + capture names.
    if (script.requirements.empty()) {
        out += "inline constexpr std::span<const std::string_view> requirements{};\n";
    } else {
        out += "inline constexpr std::string_view requirements[] = {";
        for (std::size_t i = 0; i < script.requirements.size(); ++i) {
            out += (i ? ", " : "") + quotedLiteral(script.requirements[i]);
        }
        out += "};\n";
    }
    if (script.captureNames.empty()) {
        out += "inline constexpr std::span<const std::string_view> captureNames{};\n\n";
    } else {
        out += "inline constexpr std::string_view captureNames[] = {";
        for (std::size_t i = 0; i < script.captureNames.size(); ++i) {
            out += (i ? ", " : "") + quotedLiteral(script.captureNames[i]);
        }
        out += "};\n\n";
    }

    // Field-value backing arrays for Expect/Field steps that carry one.
    for (std::size_t i = 0; i < script.steps.size(); ++i) {
        const ScriptStep& s = script.steps[i];
        if (s.kind == ms::StepKind::Expect && s.expect.kind == ms::ExpectKind::Field && !s.expect.fieldValue.empty()) {
            out += std::format("inline constexpr char32_t kFieldValue_{}[] = {};\n", i, u32ArrayInit(s.expect.fieldValue));
        }
    }
    out += "\n";

    out += "inline constexpr mdux::medui::ScenarioStep steps[] = {\n";
    for (std::size_t i = 0; i < script.steps.size(); ++i) {
        const ScriptStep& s = script.steps[i];
        out += "    {";
        out += std::format(".kind = mdux::medui::StepKind::{}", [&] {
            switch (s.kind) {
                case ms::StepKind::Advance: return "Advance";
                case ms::StepKind::Pointer: return "Pointer";
                case ms::StepKind::Key:     return "Key";
                case ms::StepKind::Text:    return "Text";
                case ms::StepKind::Focus:   return "Focus";
                case ms::StepKind::Expect:  return "Expect";
                case ms::StepKind::Capture: return "Capture";
            }
            return "Advance";
        }());
        switch (s.kind) {
            case ms::StepKind::Advance:
                out += std::format(", .frames = {}u", s.frames);
                break;
            case ms::StepKind::Pointer:
                out += std::format(", .pointer = {{.kind = {}, .x = {}, .y = {}}}", pointerKindEnum(s.pointer.kind),
                                   s.pointer.x, s.pointer.y);
                break;
            case ms::StepKind::Key:
                out += std::format(", .key = {{.kind = {}, .key = {}}}", keyKindEnum(s.key.kind), keyCodeEnum(s.key.key));
                break;
            case ms::StepKind::Text:
                out += std::format(", .text = {{.scalar = static_cast<char32_t>({}u)}}",
                                   static_cast<std::uint32_t>(s.text.scalar));
                break;
            case ms::StepKind::Focus:
                out += std::format(", .focus = {{.kind = {}, .nodeId = {}}}", focusKindEnum(s.focusKind), quotedLiteral(s.focusNode));
                break;
            case ms::StepKind::Capture:
                out += std::format(", .capture = {}", quotedLiteral(s.capture));
                break;
            case ms::StepKind::Expect: {
                const ScriptExpect& e = s.expect;
                out += std::format(", .expect = {{.kind = {}", expectKindEnum(e.kind));
                switch (e.kind) {
                    case ms::ExpectKind::Clock:
                        out += std::format(", .clock = {}", clockInit(e.clock));
                        break;
                    case ms::ExpectKind::Field:
                        if (!e.fieldValue.empty()) {
                            out += std::format(", .fieldValue = kFieldValue_{}", i);
                        }
                        if (e.fieldHasCaret) {
                            out += std::format(", .fieldHasCaret = true, .caret = {}u", e.caret);
                        }
                        break;
                    case ms::ExpectKind::RefusedEdits:
                        out += std::format(", .count = {}u", e.count);
                        break;
                    case ms::ExpectKind::Action:
                        out += std::format(", .nodeId = {}, .event = {}, .requirement = {}", quotedLiteral(e.nodeId),
                                           systemEventEnum(e.event), quotedLiteral(e.requirement));
                        break;
                    case ms::ExpectKind::ButtonSource:
                        out += std::format(", .nodeId = {}, .source = {}", quotedLiteral(e.nodeId), quotedLiteral(e.source));
                        break;
                    case ms::ExpectKind::Reading:
                        out += std::format(", .nodeId = {}, .value = {}", quotedLiteral(e.nodeId), e.value);
                        break;
                    case ms::ExpectKind::State:
                        out += std::format(", .count = {}u, .nodeId = {}", e.count, quotedLiteral(e.nodeId));
                        break;
                    case ms::ExpectKind::LatchArmed:
                        if (!e.nodeId.empty()) {
                            out += std::format(", .nodeId = {}", quotedLiteral(e.nodeId));
                        }
                        break;
                    case ms::ExpectKind::FrameStat:
                        out += std::format(", .count = {}u, .statField = {}", e.count, frameStatFieldEnum(e.statField));
                        break;
                    case ms::ExpectKind::Overflow:
                        out += std::format(", .flag = {}", e.flag ? "true" : "false");
                        break;
                    case ms::ExpectKind::Unspecified:
                        break;
                }
                out += "}";
                break;
            }
        }
        out += "},\n";
    }
    out += "};\n\n";

    out += "inline constexpr mdux::medui::CompiledScenario scenario{\n";
    out += "    .id = id,\n";
    out += "    .screenId = screenId,\n";
    out += std::format("    .schemaVersion = {}u,\n", ms::currentScenarioSchemaVersion);
    out += std::format("    .pinnedClock = {},\n", clockInit(script.clock));
    out += std::format("    .sampleSeed = {{.beatPeriod = {}u, .warmupFrames = {}u}},\n", script.sampleSeed.beatPeriod,
                       script.sampleSeed.warmupFrames);
    out += "    .requirements = requirements,\n";
    out += "    .captureNames = captureNames,\n";
    out += "    .steps = steps,\n";
    out += "};\n\n";

    out += "// Checked where it is defined, so a malformed scenario is a build failure in whatever links it.\n";
    out += "static_assert(scenario.validate().has_value(), \"this compiled scenario does not satisfy mdux.medui.scenario\");\n\n";

    out += "[[nodiscard]] constexpr mdux::medui::CompiledScenario package() noexcept { return scenario; }\n\n";
    out += std::format("}}  // namespace mdux::medui::generated::{}\n", identifier);
    return out;
}

}  // namespace

std::string identifierForScenario(std::string_view scenarioId) {
    std::string out = "scenario_";
    for (const char ch : scenarioId) {
        const bool alnum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        out.push_back(alnum ? ch : '_');
    }
    return out;
}

std::optional<EmitOutputs> renderScenario(const std::filesystem::path& scenarioPath,
                                          std::vector<cli::Diagnostic>& diagnostics) {
    auto bytes = readFileBytes(scenarioPath);
    if (!bytes.has_value()) {
        report(diagnostics, scenarioPath.generic_string(), scenarioUnreadable, "cannot read scenario.json",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return std::nullopt;
    }
    auto script = readScenarioDoc(*bytes, scenarioPath.generic_string(), diagnostics);
    if (!script.has_value()) {
        // `readScenarioDoc` has already pushed the specific `SCN02x` shape fault; add the emitter's
        // own top-level code so a caller filtering on the `mdux-scenarioemit` family still sees one.
        report(diagnostics, scenarioPath.generic_string(), scenarioMalformed,
               "scenario.json is not a compiled scenario this build can emit");
        return std::nullopt;
    }

    EmitOutputs outputs;
    outputs.stem       = identifierForScenario(script->id);
    outputs.moduleName = "mdux.medui.generated." + outputs.stem;

    const std::string preamble = std::format("// Generated by {} from {}.\n// Do not edit; regenerated on every build. The reviewed artifact is the JSON.\n",
                                             emitToolName, escape(scenarioPath.generic_string()));
    const std::string body = renderBody(*script, outputs.stem);

    outputs.moduleSource = preamble + "\nmodule;\n\nexport module " + outputs.moduleName
                           + ";\n\nimport std;\nimport mdux.medui.scenario;\nimport mdux.medui.input;\nimport mdux.medui.reading;\nimport mdux.medui.schema;\n\nexport " + body;
    outputs.headerSource = preamble + "\n#pragma once\n\n#include <span>\n#include <string_view>\n\nimport mdux.medui.scenario;\nimport mdux.medui.input;\nimport mdux.medui.reading;\nimport mdux.medui.schema;\n\n" + body;
    return outputs;
}

bool writeScenario(const EmitOutputs& outputs, const std::filesystem::path& outputDir,
                   std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, outputDir.generic_string(), outputUnwritable, "cannot create output directory: " + code.message());
        return false;
    }
    const auto writeIfChanged = [&](const std::filesystem::path& path, std::string_view content) {
        if (auto existing = readFileBytes(path); existing.has_value()) {
            const std::string_view current{reinterpret_cast<const char*>(existing->data()), existing->size()};
            if (current == content) {
                return true;
            }
        }
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        if (!file) {
            report(diagnostics, path.generic_string(), outputUnwritable, "cannot open for writing");
            return false;
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return static_cast<bool>(file);
    };
    bool ok = writeIfChanged(outputDir / (outputs.stem + ".cppm"), outputs.moduleSource);
    ok      = writeIfChanged(outputDir / (outputs.stem + ".hpp"), outputs.headerSource) && ok;
    return ok;
}

}  // namespace mdux::tools::scenario
