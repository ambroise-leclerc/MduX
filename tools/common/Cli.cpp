/**
 * @file Cli.cpp
 * @brief Implementation of the shared baker CLI and diagnostic envelope.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The JSON rendering here is hand-written rather than going through
 * mdux.evidence.json's canonical writer, and that is deliberate: this output is a diagnostic
 * stream for humans and agents, not an artifact. It is never hashed, never committed, and never
 * compared byte-for-byte, so it does not need canonical form - and coupling a tool's error
 * reporting to the artifact writer would mean a bug in the writer could suppress the diagnostic
 * explaining it.
 */
module;

module mdux.tools.cli;

import std;

namespace mdux::tools::cli {

namespace {

/// Minimal JSON string escaping for diagnostic output.
[[nodiscard]] std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                constexpr std::string_view digits = "0123456789abcdef";
                const auto value = static_cast<unsigned char>(c);
                out += "\\u00";
                out.push_back(digits[(value >> 4) & 0x0fu]);
                out.push_back(digits[value & 0x0fu]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    return out;
}

}  // namespace

std::string_view describe(Severity severity) noexcept {
    switch (severity) {
    case Severity::Error:   return "error";
    case Severity::Warning: return "warning";
    case Severity::Note:    return "note";
    }
    return "error";
}

std::string usage(std::string_view toolName) {
    std::string name{toolName};
    return "usage:\n"
           "  " + name + " bake   <recipe> <output-dir>\n"
           "  " + name + " verify <recipe> <package.json> <report.json>\n"
           "\n"
           "options:\n"
           "  --format=json|text   diagnostic output format (default: text)\n"
           "  --help               print this message\n"
           "\n"
           "bake writes the artifact into <output-dir>. verify produces the same artifact and\n"
           "compares it against the given committed files, writing nothing. A normal build only\n"
           "ever runs verify; see ADR-007.\n";
}

Invocation parse(std::string_view toolName, std::span<const std::string_view> arguments) {
    Invocation invocation;

    // Separate options from positionals in one pass, so --format may appear anywhere.
    std::vector<std::string_view> positional;
    for (const std::string_view argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            throw UsageError{usage(toolName)};
        }
        if (argument.starts_with("--format=")) {
            const std::string_view value = argument.substr(std::string_view{"--format="}.size());
            if (value == "json") {
                invocation.format = Format::Json;
            } else if (value == "text") {
                invocation.format = Format::Text;
            } else {
                throw UsageError{"unrecognized --format value '" + std::string{value} +
                                 "'; expected 'json' or 'text'\n\n" + usage(toolName)};
            }
            continue;
        }
        if (argument.starts_with("--format")) {
            // Catch the space-separated spelling explicitly rather than letting "--format" fall
            // through to the unknown-option branch, whose message would be less useful.
            throw UsageError{"--format takes its value with '=', as --format=json\n\n" +
                             usage(toolName)};
        }
        if (argument.starts_with("-") && argument != "-") {
            throw UsageError{"unrecognized option '" + std::string{argument} + "'\n\n" +
                             usage(toolName)};
        }
        positional.push_back(argument);
    }

    if (positional.empty()) {
        throw UsageError{"expected a subcommand\n\n" + usage(toolName)};
    }

    const std::string_view subcommand = positional.front();
    const std::span<const std::string_view> rest{positional.begin() + 1, positional.end()};

    if (subcommand == "bake") {
        if (rest.size() != 2) {
            throw UsageError{"bake takes exactly 2 arguments (<recipe> <output-dir>), got " +
                             std::to_string(rest.size()) + "\n\n" + usage(toolName)};
        }
        invocation.mode = Mode::Bake;
        invocation.bake = BakeArguments{.recipe = std::string{rest[0]},
                                        .outputDir = std::string{rest[1]}};
        return invocation;
    }

    if (subcommand == "verify") {
        if (rest.size() != 3) {
            throw UsageError{
                "verify takes exactly 3 arguments (<recipe> <package.json> <report.json>), got " +
                std::to_string(rest.size()) + "\n\n" + usage(toolName)};
        }
        invocation.mode = Mode::Verify;
        invocation.verify = VerifyArguments{.recipe = std::string{rest[0]},
                                            .packagePath = std::string{rest[1]},
                                            .reportPath = std::string{rest[2]}};
        return invocation;
    }

    throw UsageError{"unrecognized subcommand '" + std::string{subcommand} +
                     "'; expected 'bake' or 'verify'\n\n" + usage(toolName)};
}

Invocation parse(std::string_view toolName, int argc, const char* const* argv) {
    std::vector<std::string_view> arguments;
    // argv[0] is the program name; the tool name is passed explicitly so a renamed or wrapped
    // executable still reports the name its diagnostics are keyed to.
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return parse(toolName, arguments);
}

std::string render(std::span<const Diagnostic> diagnostics, Format format,
                   std::string_view toolName) {
    if (format == Format::Json) {
        std::string out = "{\n  \"tool\": \"" + escape(toolName) + "\",\n  \"findings\": [";
        if (diagnostics.empty()) {
            out += "]\n}\n";
            return out;
        }
        out += "\n";
        for (std::size_t i = 0; i < diagnostics.size(); ++i) {
            const Diagnostic& diagnostic = diagnostics[i];
            out += "    {\n";
            out += "      \"file\": \"" + escape(diagnostic.file) + "\",\n";
            out += "      \"line\": " + std::to_string(diagnostic.line) + ",\n";
            out += "      \"column\": " + std::to_string(diagnostic.column) + ",\n";
            out += "      \"code\": \"" + escape(diagnostic.code) + "\",\n";
            out += "      \"severity\": \"" + std::string{describe(diagnostic.severity)} + "\",\n";
            out += "      \"message\": \"" + escape(diagnostic.message) + "\",\n";
            out += "      \"fixHint\": \"" + escape(diagnostic.fixHint) + "\"\n";
            out += (i + 1 < diagnostics.size()) ? "    },\n" : "    }\n";
        }
        out += "  ]\n}\n";
        return out;
    }

    // Text form follows the file:line:column: severity: [code] message convention the existing
    // lints use, which is also what an editor's error parser expects. A zero on either axis is
    // omitted rather than printed, so a tool that knows no position still reads as "file: error:"
    // and one that knows only the line does not gain a misleading ":0".
    std::string out;
    for (const Diagnostic& diagnostic : diagnostics) {
        out += diagnostic.file;
        if (diagnostic.line != 0) {
            out += ":" + std::to_string(diagnostic.line);
            if (diagnostic.column != 0) {
                out += ":" + std::to_string(diagnostic.column);
            }
        }
        out += ": ";
        out += describe(diagnostic.severity);
        out += ": ";
        if (!diagnostic.code.empty()) {
            out += "[" + diagnostic.code + "] ";
        }
        out += diagnostic.message;
        out += "\n";
        if (!diagnostic.fixHint.empty()) {
            out += "    fix: " + diagnostic.fixHint + "\n";
        }
    }
    return out;
}

int exitStatus(std::span<const Diagnostic> diagnostics) noexcept {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            return 1;
        }
    }
    return 0;
}

}  // namespace mdux::tools::cli
