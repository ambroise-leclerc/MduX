/**
 * @file ScenarioScript.cpp
 * @brief Implementation of the `.scenario` line parser and its validation.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 */
module;

module mdux.tools.scenario.script;

import std;
import mdux.core.units;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.tools.cli;

namespace mdux::tools::scenario {

namespace ms = mdux::medui;

namespace {

// Stable diagnostic codes. See docs/governance/schemas/diagnostic.schema.json.
constexpr std::string_view unknownToken       = "SCN001";
constexpr std::string_view headerFault        = "SCN002";
constexpr std::string_view boundExceeded      = "SCN003";
constexpr std::string_view unsupportedVersion = "SCN004";
constexpr std::string_view unknownTarget      = "SCN005";
constexpr std::string_view malformedValue     = "SCN006";
constexpr std::string_view badArity           = "SCN007";
constexpr std::string_view danglingBatch      = "SCN008";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string_view path, std::size_t line,
            std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = std::string{path},
                                          .line     = line,
                                          .code     = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fixHint)});
}

/// Splits `line` into whitespace-separated fields, honouring one level of `"…"` quoting for the
/// last operand only (`text "AB C"`, `expect field "AB C"`). A `#` outside quotes ends the line.
[[nodiscard]] std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t              i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i >= line.size() || line[i] == '#') {
            break;
        }
        if (line[i] == '"') {
            ++i;
            std::string quoted;
            while (i < line.size() && line[i] != '"') {
                quoted.push_back(line[i]);
                ++i;
            }
            if (i < line.size()) {
                ++i;  // closing quote
            }
            fields.push_back("\"" + quoted + "\"");  // keep the marker so the caller knows it was quoted
            continue;
        }
        std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '#') {
            ++i;
        }
        fields.emplace_back(line.substr(start, i - start));
    }
    return fields;
}

[[nodiscard]] bool isQuoted(std::string_view field) {
    return field.size() >= 2 && field.front() == '"' && field.back() == '"';
}
[[nodiscard]] std::string_view unquote(std::string_view field) {
    return isQuoted(field) ? field.substr(1, field.size() - 2) : field;
}

/// Parses a non-negative decimal integer that fits `std::uint32_t`.
[[nodiscard]] std::optional<std::uint32_t> parseU32(std::string_view text) {
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// Parses a decimal integer (possibly negative) that fits `std::int64_t`.
[[nodiscard]] std::optional<std::int64_t> parseI64(std::string_view text) {
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// `YYYY-MM-DD` into the three fields, or nullopt.
[[nodiscard]] std::optional<std::array<std::uint32_t, 3>> parseDate(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const auto y = parseU32(text.substr(0, 4));
    const auto m = parseU32(text.substr(5, 2));
    const auto d = parseU32(text.substr(8, 2));
    if (!y || !m || !d) {
        return std::nullopt;
    }
    return std::array{*y, *m, *d};
}

/// `HH:MM:SS` into the three fields, or nullopt.
[[nodiscard]] std::optional<std::array<std::uint32_t, 3>> parseTime(std::string_view text) {
    if (text.size() != 8 || text[2] != ':' || text[5] != ':') {
        return std::nullopt;
    }
    const auto h = parseU32(text.substr(0, 2));
    const auto mi = parseU32(text.substr(3, 2));
    const auto s = parseU32(text.substr(6, 2));
    if (!h || !mi || !s) {
        return std::nullopt;
    }
    return std::array{*h, *mi, *s};
}

/// A REQ-ID: `[A-Z0-9-]+` starting with a letter, so a typo like a lowercase id or a bare number
/// is caught rather than committed.
[[nodiscard]] bool isRequirementId(std::string_view text) {
    if (text.empty() || !(text.front() >= 'A' && text.front() <= 'Z')) {
        return false;
    }
    for (const char ch : text) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// A scenario id: a lowercase slug `[a-z0-9]([a-z0-9-]*[a-z0-9])?`. The emitter maps every
/// non-alphanumeric character to `_` when it forms a C++ identifier, so if `.`, `_` or a
/// trailing/leading `-` were allowed here two distinct ids could render the same module name.
/// Restricting the source id the way a screen or recipe id is already restricted removes that
/// collision at the door.
[[nodiscard]] bool isScenarioId(std::string_view text) {
    if (text.empty() || text.front() == '-' || text.back() == '-') {
        return false;
    }
    for (const char ch : text) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// Decodes a UTF-8 string to a `std::u32string`, or nullopt on malformed input.
[[nodiscard]] std::optional<std::u32string> decodeUtf8(std::string_view text) {
    std::u32string out;
    std::size_t    i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        char32_t   cp = 0;
        std::size_t extra = 0;
        if (b0 < 0x80u) {
            cp = b0;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            cp = b0 & 0x1Fu;
            extra = 1;
        } else if ((b0 & 0xF0u) == 0xE0u) {
            cp = b0 & 0x0Fu;
            extra = 2;
        } else if ((b0 & 0xF8u) == 0xF0u) {
            cp = b0 & 0x07u;
            extra = 3;
        } else {
            return std::nullopt;
        }
        if (i + extra >= text.size()) {
            return std::nullopt;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bx = static_cast<unsigned char>(text[i + k]);
            if ((bx & 0xC0u) != 0x80u) {
                return std::nullopt;
            }
            cp = (cp << 6) | (bx & 0x3Fu);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

[[nodiscard]] const ScreenNode* findNode(std::span<const ScreenNode> nodes, std::string_view id) {
    for (const ScreenNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<Script> parseScript(std::string_view              text,
                                  std::string_view              scriptPath,
                                  std::string_view              expectedScreen,
                                  std::span<const ScreenNode>   nodes,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    Script script;
    bool   sawScenario = false;
    bool   sawScreen   = false;
    bool   sawVersion  = false;
    bool   sawClock    = false;
    bool   inSteps     = false;   // a step directive has appeared; header lines are now closed
    std::size_t pendingEvents = 0;  // events queued since the last `advance`
    std::size_t expectCount   = 0;
    bool        sawAdvance    = false;  // an `expect`/`capture` before the first `advance` checks nothing

    const std::size_t before = diagnostics.size();
    const auto        failed = [&] { return diagnostics.size() != before; };

    std::size_t lineNo = 0;
    for (const auto part : std::views::split(text, '\n')) {
        ++lineNo;
        const std::string_view       rawLine{part.data(), part.size()};
        const std::vector<std::string> f = tokenize(rawLine);
        if (f.empty()) {
            continue;
        }
        const std::string& directive = f[0];

        // ------- header directives -------
        if (directive == "scenario") {
            if (inSteps || sawScenario || f.size() != 2) {
                report(diagnostics, scriptPath, lineNo, headerFault,
                       "`scenario <id>` must appear once, before any step", "Put it on the first line.");
                return std::nullopt;
            }
            script.id = unquote(f[1]);
            if (!isScenarioId(script.id)) {
                report(diagnostics, scriptPath, lineNo, headerFault,
                       std::format("'{}' is not a scenario id (a lowercase slug [a-z0-9-], no leading or trailing '-')",
                                   script.id),
                       "Match the recipe's `id` and the screen id convention.");
                return std::nullopt;
            }
            sawScenario = true;
            continue;
        }
        if (directive == "screen") {
            if (inSteps || sawScreen || f.size() != 2) {
                report(diagnostics, scriptPath, lineNo, headerFault, "`screen <id>` must appear once, in the header");
                return std::nullopt;
            }
            script.screenId = unquote(f[1]);
            if (script.screenId != expectedScreen) {
                report(diagnostics, scriptPath, lineNo, unknownTarget,
                       std::format("the script names screen '{}' but the recipe declares '{}'", script.screenId, expectedScreen),
                       "Make the `screen` line match the recipe's `screen` key.");
                return std::nullopt;
            }
            sawScreen = true;
            continue;
        }
        if (directive == "version") {
            if (inSteps || sawVersion || f.size() != 2) {
                report(diagnostics, scriptPath, lineNo, headerFault, "`version <N>` must appear once, in the header");
                return std::nullopt;
            }
            const auto v = parseU32(f[1]);
            if (!v) {
                report(diagnostics, scriptPath, lineNo, malformedValue, "`version` is not a non-negative integer");
                return std::nullopt;
            }
            script.version = *v;
            if (script.version != ms::currentScenarioSchemaVersion) {
                report(diagnostics, scriptPath, lineNo, unsupportedVersion,
                       std::format("scenario version {} is not the one this compiler implements ({})", script.version,
                                   ms::currentScenarioSchemaVersion),
                       "Regenerate the script against this toolchain, or bump the compiler.");
                return std::nullopt;
            }
            sawVersion = true;
            continue;
        }
        if (directive == "clock") {
            if (inSteps || sawClock || f.size() != 3) {
                report(diagnostics, scriptPath, lineNo, headerFault, "`clock <YYYY-MM-DD> <HH:MM:SS>` must appear once, in the header");
                return std::nullopt;
            }
            const auto date = parseDate(f[1]);
            const auto time = parseTime(f[2]);
            if (!date || !time) {
                report(diagnostics, scriptPath, lineNo, malformedValue, "`clock` is not `YYYY-MM-DD HH:MM:SS`");
                return std::nullopt;
            }
            script.clock = ms::CivilTime{.year   = static_cast<std::int32_t>((*date)[0]),
                                         .month  = static_cast<std::uint8_t>((*date)[1]),
                                         .day    = static_cast<std::uint8_t>((*date)[2]),
                                         .hour   = static_cast<std::uint8_t>((*time)[0]),
                                         .minute = static_cast<std::uint8_t>((*time)[1]),
                                         .second = static_cast<std::uint8_t>((*time)[2])};
            if (script.clock.month < 1 || script.clock.month > 12 || script.clock.day < 1 || script.clock.day > 31
                || script.clock.hour > 23 || script.clock.minute > 59 || script.clock.second > 59) {
                report(diagnostics, scriptPath, lineNo, malformedValue, "`clock` has an out-of-range field");
                return std::nullopt;
            }
            sawClock = true;
            continue;
        }
        if (directive == "sample") {
            if (inSteps || f.size() < 2 || f.size() > 3) {
                report(diagnostics, scriptPath, lineNo, headerFault, "`sample <beatPeriod> [<warmupFrames>]` belongs in the header");
                return std::nullopt;
            }
            const auto period = parseU32(f[1]);
            if (!period || *period == 0) {
                report(diagnostics, scriptPath, lineNo, malformedValue, "`sample` beat period must be a positive integer");
                return std::nullopt;
            }
            script.sampleSeed.beatPeriod = *period;
            if (f.size() == 3) {
                const auto warmup = parseU32(f[2]);
                if (!warmup) {
                    report(diagnostics, scriptPath, lineNo, malformedValue, "`sample` warmup frames must be a non-negative integer");
                    return std::nullopt;
                }
                script.sampleSeed.warmupFrames = *warmup;
            }
            continue;
        }
        if (directive == "requirement") {
            if (inSteps || f.size() < 2) {
                report(diagnostics, scriptPath, lineNo, headerFault, "`requirement <REQ-ID> ...` belongs in the header");
                return std::nullopt;
            }
            for (std::size_t i = 1; i < f.size(); ++i) {
                if (!isRequirementId(f[i])) {
                    report(diagnostics, scriptPath, lineNo, malformedValue,
                           std::format("'{}' is not a requirement id ([A-Z][A-Z0-9-]*)", f[i]));
                    return std::nullopt;
                }
                if (std::ranges::find(script.requirements, f[i]) == script.requirements.end()) {
                    script.requirements.push_back(f[i]);
                }
            }
            if (script.requirements.size() > ms::maxScenarioRequirements) {
                report(diagnostics, scriptPath, lineNo, boundExceeded,
                       std::format("more than {} distinct requirements", ms::maxScenarioRequirements));
                return std::nullopt;
            }
            continue;
        }

        // Any other directive is a step. The header is now closed.
        if (!sawScenario || !sawScreen || !sawVersion || !sawClock) {
            report(diagnostics, scriptPath, lineNo, headerFault,
                   "every scenario needs `scenario`, `screen`, `version` and `clock` before its first step");
            return std::nullopt;
        }
        inSteps = true;

        ScriptStep step;
        step.line = lineNo;

        if (directive == "pointer") {
            if (f.size() < 2 || f.size() > 3) {
                report(diagnostics, scriptPath, lineNo, badArity, "`pointer <down|up|move|cancel> [<node>]`");
                return std::nullopt;
            }
            const auto kind = ms::pointerKindFromWire(
                f[1] == "down" ? "Down" : f[1] == "up" ? "Up" : f[1] == "move" ? "Move" : f[1] == "cancel" ? "Cancel" : "");
            if (!kind) {
                report(diagnostics, scriptPath, lineNo, unknownToken, std::format("'{}' is not a pointer transition", f[1]));
                return std::nullopt;
            }
            step.kind         = ms::StepKind::Pointer;
            step.pointer.kind = *kind;
            if (*kind == ms::PointerKind::Cancel) {
                if (f.size() != 2) {
                    report(diagnostics, scriptPath, lineNo, badArity, "`pointer cancel` takes no node");
                    return std::nullopt;
                }
            } else {
                if (f.size() != 3) {
                    report(diagnostics, scriptPath, lineNo, badArity, "`pointer down|up|move` needs a node");
                    return std::nullopt;
                }
                const ScreenNode* node = findNode(nodes, f[2]);
                if (node == nullptr) {
                    report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", f[2]));
                    return std::nullopt;
                }
                step.pointer.x = node->centreX;
                step.pointer.y = node->centreY;
            }
            ++pendingEvents;
        } else if (directive == "key") {
            std::string_view codeText = f.size() >= 2 ? std::string_view{f[1]} : std::string_view{};
            ms::KeyKind      keyKind  = ms::KeyKind::Down;
            std::size_t      expected = 2;
            if (f.size() == 3 && (f[1] == "down" || f[1] == "up")) {
                keyKind  = f[1] == "up" ? ms::KeyKind::Up : ms::KeyKind::Down;
                codeText = f[2];
                expected = 3;
            }
            if (f.size() != expected) {
                report(diagnostics, scriptPath, lineNo, badArity, "`key [down|up] <KeyCode>`");
                return std::nullopt;
            }
            const auto code = ms::keyCodeFromWire(codeText);
            if (!code) {
                report(diagnostics, scriptPath, lineNo, unknownToken,
                       std::format("'{}' is not a KeyCode (see ADR-018: Commit, Cancel, CaretLeft, …)", codeText));
                return std::nullopt;
            }
            step.kind     = ms::StepKind::Key;
            step.key.kind = keyKind;
            step.key.key  = *code;
            ++pendingEvents;
        } else if (directive == "text") {
            if (f.size() != 2) {
                report(diagnostics, scriptPath, lineNo, badArity, "`text <scalar>` or `text \"<run>\"`");
                return std::nullopt;
            }
            const auto decoded = decodeUtf8(unquote(f[1]));
            if (!decoded || decoded->empty()) {
                report(diagnostics, scriptPath, lineNo, malformedValue, "`text` operand is empty or not valid UTF-8");
                return std::nullopt;
            }
            // A run expands to one `Text` step per scalar; a bare operand must be exactly one scalar.
            if (!isQuoted(f[1]) && decoded->size() != 1) {
                report(diagnostics, scriptPath, lineNo, badArity, "`text` with no quotes takes exactly one scalar; quote a run");
                return std::nullopt;
            }
            for (const char32_t scalar : *decoded) {
                ScriptStep textStep;
                textStep.line        = lineNo;
                textStep.kind        = ms::StepKind::Text;
                textStep.text.scalar = scalar;
                script.steps.push_back(textStep);
                ++pendingEvents;
            }
            if (script.steps.size() > ms::maxScenarioSteps) {
                report(diagnostics, scriptPath, lineNo, boundExceeded, std::format("more than {} steps", ms::maxScenarioSteps));
                return std::nullopt;
            }
            continue;  // already pushed
        } else if (directive == "focus") {
            if (f.size() != 3 || (f[1] != "enter" && f[1] != "leave")) {
                report(diagnostics, scriptPath, lineNo, badArity, "`focus <enter|leave> <node>`");
                return std::nullopt;
            }
            const ScreenNode* node = findNode(nodes, f[2]);
            if (node == nullptr) {
                report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", f[2]));
                return std::nullopt;
            }
            step.kind          = ms::StepKind::Focus;
            step.focusKind     = f[1] == "enter" ? ms::FocusKind::Enter : ms::FocusKind::Leave;
            step.focusNode     = f[2];
            ++pendingEvents;
        } else if (directive == "advance") {
            if (f.size() > 2) {
                report(diagnostics, scriptPath, lineNo, badArity, "`advance [<N>]`");
                return std::nullopt;
            }
            std::uint32_t frames = 1;
            if (f.size() == 2) {
                const auto n = parseU32(f[1]);
                if (!n || *n == 0) {
                    report(diagnostics, scriptPath, lineNo, malformedValue, "`advance` count must be a positive integer");
                    return std::nullopt;
                }
                frames = *n;
            }
            step.kind   = ms::StepKind::Advance;
            step.frames = frames;
            pendingEvents = 0;
            sawAdvance    = true;
        } else if (directive == "capture") {
            if (!sawAdvance) {
                report(diagnostics, scriptPath, lineNo, danglingBatch,
                       "`capture` before the first `advance` has no settled frame to capture",
                       "Put an `advance` ahead of it.");
                return std::nullopt;
            }
            if (f.size() != 2) {
                report(diagnostics, scriptPath, lineNo, badArity, "`capture <name>`");
                return std::nullopt;
            }
            const std::string name{unquote(f[1])};
            if (std::ranges::find(script.captureNames, name) != script.captureNames.end()) {
                report(diagnostics, scriptPath, lineNo, badArity, std::format("capture name '{}' is used twice", name));
                return std::nullopt;
            }
            script.captureNames.push_back(name);
            if (script.captureNames.size() > ms::maxScenarioCaptures) {
                report(diagnostics, scriptPath, lineNo, boundExceeded, std::format("more than {} captures", ms::maxScenarioCaptures));
                return std::nullopt;
            }
            step.kind    = ms::StepKind::Capture;
            step.capture = name;
        } else if (directive == "expect") {
            if (!sawAdvance) {
                report(diagnostics, scriptPath, lineNo, danglingBatch,
                       "`expect` before the first `advance` checks state no batch has settled",
                       "Put an `advance` ahead of it.");
                return std::nullopt;
            }
            if (f.size() < 2) {
                report(diagnostics, scriptPath, lineNo, badArity, "`expect <kind> <args>`");
                return std::nullopt;
            }
            const auto kind = ms::expectKindFromWire(f[1]);
            if (!kind) {
                report(diagnostics, scriptPath, lineNo, unknownToken, std::format("'{}' is not an expect kind", f[1]));
                return std::nullopt;
            }
            step.kind         = ms::StepKind::Expect;
            step.expect.kind  = *kind;
            const auto args   = std::span{f}.subspan(2);
            const auto arity  = [&](std::size_t lo, std::size_t hi) {
                if (args.size() < lo || args.size() > hi) {
                    report(diagnostics, scriptPath, lineNo, badArity,
                           std::format("`expect {}` takes {}..{} arguments, got {}", f[1], lo, hi, args.size()));
                    return false;
                }
                return true;
            };
            switch (*kind) {
                case ms::ExpectKind::Clock: {
                    if (!arity(1, 1)) {
                        return std::nullopt;
                    }
                    const auto time = parseTime(args[0]);
                    if (!time) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect clock <HH:MM:SS>`");
                        return std::nullopt;
                    }
                    step.expect.clock        = script.clock;
                    step.expect.clock.hour   = static_cast<std::uint8_t>((*time)[0]);
                    step.expect.clock.minute = static_cast<std::uint8_t>((*time)[1]);
                    step.expect.clock.second = static_cast<std::uint8_t>((*time)[2]);
                    break;
                }
                case ms::ExpectKind::Field: {
                    if (!arity(1, 3)) {
                        return std::nullopt;
                    }
                    const auto value = decodeUtf8(unquote(args[0]));
                    if (!value) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect field` value is not valid UTF-8");
                        return std::nullopt;
                    }
                    step.expect.fieldValue = *value;
                    if (args.size() == 3 && args[1] == "caret") {
                        const auto c = parseU32(args[2]);
                        if (!c) {
                            report(diagnostics, scriptPath, lineNo, malformedValue, "`caret` needs a non-negative integer");
                            return std::nullopt;
                        }
                        step.expect.fieldHasCaret = true;
                        step.expect.caret         = *c;
                    } else if (args.size() != 1) {
                        report(diagnostics, scriptPath, lineNo, badArity, "`expect field \"<value>\" [caret <N>]`");
                        return std::nullopt;
                    }
                    break;
                }
                case ms::ExpectKind::RefusedEdits: {
                    if (!arity(1, 1)) {
                        return std::nullopt;
                    }
                    const auto n = parseU32(args[0]);
                    if (!n) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect refused <N>`");
                        return std::nullopt;
                    }
                    step.expect.count = *n;
                    break;
                }
                case ms::ExpectKind::Action: {
                    if (!arity(3, 3)) {
                        return std::nullopt;
                    }
                    const ScreenNode* node = findNode(nodes, args[0]);
                    if (node == nullptr) {
                        report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", args[0]));
                        return std::nullopt;
                    }
                    const auto event = ms::systemEventFromWire(args[1]);
                    if (!event) {
                        report(diagnostics, scriptPath, lineNo, unknownToken,
                               std::format("'{}' is not a SystemEvent (NoOp, TriggerHalt)", args[1]));
                        return std::nullopt;
                    }
                    if (!isRequirementId(args[2])) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect action` needs a requirement id");
                        return std::nullopt;
                    }
                    step.expect.nodeId      = args[0];
                    step.expect.event       = *event;
                    step.expect.requirement = args[2];
                    break;
                }
                case ms::ExpectKind::ButtonSource: {
                    if (!arity(2, 2)) {
                        return std::nullopt;
                    }
                    const ScreenNode* node = findNode(nodes, args[0]);
                    if (node == nullptr) {
                        report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", args[0]));
                        return std::nullopt;
                    }
                    step.expect.nodeId = args[0];
                    step.expect.source = unquote(args[1]);
                    break;
                }
                case ms::ExpectKind::Reading: {
                    if (!arity(2, 2)) {
                        return std::nullopt;
                    }
                    if (findNode(nodes, args[0]) == nullptr) {
                        report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", args[0]));
                        return std::nullopt;
                    }
                    const auto v = parseI64(args[1]);
                    if (!v) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect reading <node> <int>`");
                        return std::nullopt;
                    }
                    step.expect.nodeId = args[0];
                    step.expect.value  = *v;
                    break;
                }
                case ms::ExpectKind::State: {
                    if (!arity(2, 2)) {
                        return std::nullopt;
                    }
                    if (findNode(nodes, args[0]) == nullptr) {
                        report(diagnostics, scriptPath, lineNo, unknownTarget, std::format("the screen has no node '{}'", args[0]));
                        return std::nullopt;
                    }
                    const auto idx = parseU32(args[1]);
                    if (!idx) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect state <node> <index>`");
                        return std::nullopt;
                    }
                    step.expect.nodeId = args[0];
                    step.expect.count  = *idx;
                    break;
                }
                case ms::ExpectKind::LatchArmed: {
                    if (!arity(1, 1)) {
                        return std::nullopt;
                    }
                    if (args[0] != "-") {
                        if (findNode(nodes, args[0]) == nullptr) {
                            report(diagnostics, scriptPath, lineNo, unknownTarget,
                                   std::format("the screen has no node '{}'", args[0]));
                            return std::nullopt;
                        }
                        step.expect.nodeId = args[0];
                    }
                    break;
                }
                case ms::ExpectKind::FrameStat: {
                    if (!arity(2, 2)) {
                        return std::nullopt;
                    }
                    const auto field = ms::frameStatFieldFromWire(args[0]);
                    if (!field) {
                        report(diagnostics, scriptPath, lineNo, unknownToken,
                               std::format("'{}' is not a FrameStats field", args[0]));
                        return std::nullopt;
                    }
                    const auto n = parseU32(args[1]);
                    if (!n) {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect frame <field> <N>`");
                        return std::nullopt;
                    }
                    step.expect.statField = *field;
                    step.expect.count     = *n;
                    break;
                }
                case ms::ExpectKind::Overflow: {
                    if (!arity(1, 1)) {
                        return std::nullopt;
                    }
                    if (args[0] != "true" && args[0] != "false") {
                        report(diagnostics, scriptPath, lineNo, malformedValue, "`expect overflow <true|false>`");
                        return std::nullopt;
                    }
                    step.expect.flag = args[0] == "true";
                    break;
                }
                case ms::ExpectKind::Unspecified:
                    report(diagnostics, scriptPath, lineNo, unknownToken, "empty expect kind");
                    return std::nullopt;
            }
            ++expectCount;
            if (expectCount > ms::maxScenarioExpectations) {
                report(diagnostics, scriptPath, lineNo, boundExceeded,
                       std::format("more than {} expectations", ms::maxScenarioExpectations));
                return std::nullopt;
            }
        } else {
            report(diagnostics, scriptPath, lineNo, unknownToken, std::format("'{}' is not a scenario directive", directive));
            return std::nullopt;
        }

        script.steps.push_back(std::move(step));
        if (script.steps.size() > ms::maxScenarioSteps) {
            report(diagnostics, scriptPath, lineNo, boundExceeded, std::format("more than {} steps", ms::maxScenarioSteps));
            return std::nullopt;
        }
    }

    if (failed()) {
        return std::nullopt;
    }

    // Whole-scenario checks the line loop cannot make.
    if (!sawAdvance) {
        report(diagnostics, scriptPath, lineNo, danglingBatch,
               "the scenario never `advance`s a batch, so it verifies nothing", "Add an `advance` after the events.");
        return std::nullopt;
    }
    if (pendingEvents != 0) {
        report(diagnostics, scriptPath, lineNo, danglingBatch,
               std::format("{} event(s) queued after the last `advance` are never consumed", pendingEvents),
               "End the script with an `advance`.");
        return std::nullopt;
    }

    return script;
}

}  // namespace mdux::tools::scenario
