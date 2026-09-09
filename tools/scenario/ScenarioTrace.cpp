/**
 * @file ScenarioTrace.cpp
 * @brief Implementation of the host-side replay trace.
 */
module;

module mdux.tools.scenario.trace;

import std;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;

namespace mdux::tools::scenario {

namespace ms = mdux::medui;

namespace {

[[nodiscard]] std::string fieldValue(std::span<const char32_t> value) {
    std::string out = "\"";
    for (const char32_t cp : value) {
        if (cp >= 0x20 && cp < 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else {
            out += std::format("\\u{:04x}", static_cast<std::uint32_t>(cp));
        }
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string clockText(const ms::CivilTime& c) {
    return std::format("{:02}:{:02}:{:02}", static_cast<unsigned>(c.hour), static_cast<unsigned>(c.minute),
                       static_cast<unsigned>(c.second));
}

[[nodiscard]] std::string expectText(const ms::Expectation& e) {
    switch (e.kind) {
        case ms::ExpectKind::Clock:        return std::format("clock {}", clockText(e.clock));
        case ms::ExpectKind::Field:        return std::format("field {}{}", fieldValue(e.fieldValue),
                                                              e.fieldHasCaret ? std::format(" caret {}", e.caret) : "");
        case ms::ExpectKind::RefusedEdits: return std::format("refused {}", e.count);
        case ms::ExpectKind::Action:       return std::format("action {} {} {}", e.nodeId, ms::toWire(e.event), e.requirement);
        case ms::ExpectKind::ButtonSource: return std::format("button {} {}", e.nodeId, e.source);
        case ms::ExpectKind::Reading:      return std::format("reading {} {}", e.nodeId, e.value);
        case ms::ExpectKind::State:        return std::format("state {} {}", e.nodeId, e.count);
        case ms::ExpectKind::LatchArmed:   return std::format("latch {}", e.nodeId.empty() ? "-" : e.nodeId);
        case ms::ExpectKind::FrameStat:    return std::format("frame {} {}", ms::toWire(e.statField), e.count);
        case ms::ExpectKind::Overflow:     return std::format("overflow {}", e.flag ? "true" : "false");
        case ms::ExpectKind::Unspecified:  return "<unspecified>";
    }
    return "<unknown>";
}

[[nodiscard]] const ms::StepOutcome* outcomeForStep(const ms::ReplayReport& report, std::size_t stepIndex) {
    for (const ms::StepOutcome& o : report.outcomes) {
        if (o.stepIndex == stepIndex) {
            return &o;
        }
    }
    return nullptr;
}

}  // namespace

std::string renderTraceText(const ms::CompiledScenario& scenario, const ms::ReplayReport& report) {
    std::string out;
    out += std::format("scenario {} (screen {})\n", scenario.id, scenario.screenId);
    out += std::format("pinned clock {}-{:02}-{:02} {}\n", scenario.pinnedClock.year,
                       static_cast<unsigned>(scenario.pinnedClock.month), static_cast<unsigned>(scenario.pinnedClock.day),
                       clockText(scenario.pinnedClock));

    std::size_t frameGroup = 0;
    for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
        const ms::ScenarioStep& step = scenario.steps[i];
        switch (step.kind) {
            case ms::StepKind::Advance:
                ++frameGroup;
                out += std::format("  [frame {}] advance {}\n", frameGroup, step.frames);
                break;
            case ms::StepKind::Pointer:
                out += std::format("    queue  pointer {} ({}, {})\n", ms::toWire(step.pointer.kind), step.pointer.x, step.pointer.y);
                break;
            case ms::StepKind::Key:
                out += std::format("    queue  key {} {}\n", ms::toWire(step.key.kind), ms::toWire(step.key.key));
                break;
            case ms::StepKind::Text:
                out += std::format("    queue  text {}\n", fieldValue(std::span{&step.text.scalar, 1}));
                break;
            case ms::StepKind::Focus:
                out += std::format("    queue  focus {} {}\n", ms::toWire(step.focus.kind), step.focus.nodeId);
                break;
            case ms::StepKind::Capture:
                out += std::format("    capture {}\n", step.capture);
                break;
            case ms::StepKind::Expect: {
                const ms::StepOutcome* o = outcomeForStep(report, i);
                const std::string_view verdict = o == nullptr ? "NOT RUN" : (o->held ? "HELD" : "FAILED");
                out += std::format("    expect {:<44} {}\n", expectText(step.expect), verdict);
                break;
            }
        }
    }

    out += std::format("verdict: {} ({} of {} expectations held; {})\n",
                       report.passed() ? "PASS" : "FAIL", report.expectationsHeld, report.outcomes.size(),
                       ms::describe(report.fault));
    return out;
}

}  // namespace mdux::tools::scenario
