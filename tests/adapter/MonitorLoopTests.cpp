/**
 * @file MonitorLoopTests.cpp
 * @brief BDD scenarios for the assembled monitor update - `examples/support/MonitorApp.hpp` (#318).
 *
 * @compliance ADR-004 Trust zones in C++ (examples-zone header, tested with MduX::Core only)
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 *
 * `updateMonitor()` is steps 1-2 of ADR-018 clause 6: it drains one accepted event batch and
 * applies its presses and edits to caller-owned state, then advances the deterministic clock and
 * the demonstration generators. These scenarios drive it with hand-built event batches over the
 * committed `endoscope-monitor` screen - no window, no Vulkan, no display. The `--headless-smoke`
 * mode of `MedicalScreenMonitorExample` is the end-to-end counterpart that also renders a frame.
 */

import std;
import speclab;
import mdux.core.units;
import mdux.core.result;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.medui.generated.screen_endoscope_monitor;

#include "../framework/SpecLabBridge.hpp"
#include "MonitorApp.hpp"

namespace {

namespace ms = mdux::medui;
namespace mx = mdux::examples;

/// The committed screen as generated code holds it.
[[nodiscard]] const ms::ScreenPackage& monitorScreen() {
    static const ms::ScreenPackage package = ms::generated::screen_endoscope_monitor::package();
    return package;
}

/// The committed font package, read from the repository the same way the other evidence tests do.
[[nodiscard]] const mdux::font::FontPackage& committedFont() {
    static const mdux::font::FontPackage font = [] {
        const std::filesystem::path path =
            std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "font" / "dejavu-ui" / "package.json";
        std::ifstream in{path, std::ios::binary};
        std::string   text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        auto          parsed = mdux::font::FontPackage::parse(text);
        if (!parsed) {
            throw speclab::core::AssertionFailure("the committed font package did not parse", std::source_location::current());
        }
        return std::move(*parsed);
    }();
    return font;
}

/// A fresh `EventQueue` over caller storage, kept alive by the returned holder.
struct QueueHolder {
    std::array<ms::InputEvent, ms::maxInputEvents> storage{};
    ms::EventQueue                                 queue{storage};
};

/// A pointer down then up over the centre of `nodeId`.
void clickNode(ms::EventQueue& queue, std::string_view nodeId) {
    const ms::CompiledNode* node = monitorScreen().find(nodeId);
    if (node == nullptr) {
        return;
    }
    const mdux::core::Px x = node->bounds.x + node->bounds.width / 2;
    const mdux::core::Px y = node->bounds.y + node->bounds.height / 2;
    (void)queue.push(ms::PointerEvent{.kind = ms::PointerKind::Down, .x = x, .y = y});
    (void)queue.push(ms::PointerEvent{.kind = ms::PointerKind::Up, .x = x, .y = y});
}

const mdux::spec::Register freezeButtonResolvesToItsOpenSource{
    "The ordinary freeze Button press resolves to its open source, and executes nothing",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-freeze-button-source")
            .Given("a batch with a click on the freeze Button", [] {})
            .When("updateMonitor() drains it", [] {})
            .Then("the outcome carries the 'FREEZE' source string and no critical action",
                  [] {
                      mdux::spec::Checks checks;
                      QueueHolder        q;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      ms::PressLatch latch;
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      clickNode(q.queue, mx::kFreezeNode);
                      const mx::MonitorUpdateOutcome out =
                          mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);

                      checks.expect(out.buttonSource.has_value() && *out.buttonSource == "FREEZE",
                                    std::format("freeze resolves to 'FREEZE', got '{}'", out.buttonSource.value_or("<none>")));
                      checks.expect(!out.criticalAction.has_value(), "an ordinary Button is not a critical action");
                      checks.expect(sequence == 0, "no ActionTrace sequence was advanced for an ordinary Button");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register haltResolvesToATracedActionTrace{
    "The emergency-halt press resolves to a traced TriggerHalt ActionTrace with a monotonic sequence",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-halt-action-trace")
            .Given("two batches, each a click on emergency-halt", [] {})
            .When("updateMonitor() drains them in turn", [] {})
            .Then("each yields an ActionTrace carrying REQ-EM-003, TriggerHalt and an increasing sequence",
                  [] {
                      mdux::spec::Checks checks;
                      QueueHolder        q;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      ms::PressLatch   latch;
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      clickNode(q.queue, mx::kHaltNode);
                      const mx::MonitorUpdateOutcome first =
                          mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);
                      clickNode(q.queue, mx::kHaltNode);
                      const mx::MonitorUpdateOutcome second =
                          mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);

                      checks.expect(first.criticalAction.has_value() && second.criticalAction.has_value(), "both presses resolved");
                      if (first.criticalAction && second.criticalAction) {
                          checks.expect(first.criticalAction->event == ms::SystemEvent::TriggerHalt, "the event is TriggerHalt");
                          checks.expect(first.criticalAction->requirement == "REQ-EM-003", "the requirement travels with the action");
                          checks.expect(first.criticalAction->nodeId == "emergency-halt", "the node id is the control's");
                          checks.expect(second.criticalAction->sequence > first.criticalAction->sequence,
                                        std::format("the sequence is monotonic ({} then {})", first.criticalAction->sequence,
                                                    second.criticalAction->sequence));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register releaseOnADifferentTargetDoesNotActivate{
    "A press armed on emergency-halt and released off it activates nothing",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-release-off-target")
            .Given("a Down on emergency-halt and an Up on the freeze Button", [] {})
            .When("updateMonitor() drains the batch", [] {})
            .Then("no action resolves - release activates only the same armed target (ADR-018 clause 4)",
                  [] {
                      mdux::spec::Checks checks;
                      QueueHolder        q;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      ms::PressLatch   latch;
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      const ms::CompiledNode* halt   = monitorScreen().find(mx::kHaltNode);
                      const ms::CompiledNode* freeze = monitorScreen().find(mx::kFreezeNode);
                      (void)q.queue.push(ms::PointerEvent{.kind = ms::PointerKind::Down,
                                                          .x    = halt->bounds.x + halt->bounds.width / 2,
                                                          .y    = halt->bounds.y + halt->bounds.height / 2});
                      (void)q.queue.push(ms::PointerEvent{.kind = ms::PointerKind::Up,
                                                          .x    = freeze->bounds.x + freeze->bounds.width / 2,
                                                          .y    = freeze->bounds.y + freeze->bounds.height / 2});
                      const mx::MonitorUpdateOutcome out =
                          mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);

                      checks.expect(!out.criticalAction.has_value() && !out.buttonSource.has_value(),
                                    "a release off the armed target activates nothing");
                      checks.expect(!latch.isArmed(), "and the latch is disarmed either way");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editingRespectsTheFieldCharset{
    "The patient-id field accepts digits and A-Z and refuses everything else, with no partial mutation",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-field-charset")
            .Given("a batch: focus, then 'A', '7', 'a', '-'", [] {})
            .When("updateMonitor() drains it", [] {})
            .Then("the field holds exactly \"A7\" and two edits were refused",
                  [] {
                      mdux::spec::Checks checks;
                      QueueHolder        q;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      checks.expect(state.field.has_value(), "the field bound against the committed font and node charset");
                      ms::PressLatch   latch;
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      (void)q.queue.push(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::Commit});
                      (void)q.queue.push(ms::TextEvent{.scalar = U'A'});
                      (void)q.queue.push(ms::TextEvent{.scalar = U'7'});
                      (void)q.queue.push(ms::TextEvent{.scalar = U'a'});
                      (void)q.queue.push(ms::TextEvent{.scalar = U'-'});
                      const mx::MonitorUpdateOutcome out =
                          mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);

                      const auto value = state.field->value();
                      checks.expect(value.size() == 2 && value[0] == U'A' && value[1] == U'7',
                                    std::format("the field holds \"A7\", got {} scalars", value.size()));
                      checks.expect(out.refusedEdits == 2, std::format("two edits were refused, saw {}", out.refusedEdits));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register overflowDiscardsTheWholeBatch{
    "An overflowed batch is discarded whole and the latch cancelled (ADR-019 clause 3)",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-overflow-discards")
            .Given("a queued click and the overflow signal set", [] {})
            .When("updateMonitor() is told the batch overflowed", [] {})
            .Then("nothing in the batch is acted on, the queue is cleared and the latch is disarmed",
                  [] {
                      mdux::spec::Checks checks;
                      QueueHolder        q;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      ms::PressLatch   latch;
                      latch.arm(mx::kHaltNode);
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      clickNode(q.queue, mx::kHaltNode);
                      const mx::MonitorUpdateOutcome out =
                          mx::updateMonitor(q.queue, true, monitorScreen(), state, latch, clock, sequence);

                      checks.expect(out.droppedBatch, "the outcome records the dropped batch");
                      checks.expect(!out.criticalAction.has_value() && !out.buttonSource.has_value(), "no event in the batch was acted on");
                      checks.expect(q.queue.empty(), "the queue was cleared");
                      checks.expect(!latch.isArmed(), "the armed press was cancelled");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theClockAdvancesDeterministically{
    "The deterministic clock advances one second per update and nothing else moves it",
    "evidence-unit",
    [] {
        return speclab::Test("monitor-loop-clock-deterministic")
            .Given("a clock at 08:00:00 and 65 empty updates", [] {})
            .When("updateMonitor() runs each with no events", [] {})
            .Then("the clock reads 08:01:05 - one second per update, minutes carried",
                  [] {
                      mdux::spec::Checks checks;
                      mx::DemoState      state;
                      state.bindField(monitorScreen(), committedFont());
                      ms::PressLatch   latch;
                      mx::MonitorClock clock;
                      std::uint64_t    sequence = 0;

                      for (int i = 0; i < 65; ++i) {
                          QueueHolder q;
                          (void)mx::updateMonitor(q.queue, false, monitorScreen(), state, latch, clock, sequence);
                      }
                      checks.expect(clock.now.hour == 8 && clock.now.minute == 1 && clock.now.second == 5,
                                    std::format("08:01:05, got {:02}:{:02}:{:02}", clock.now.hour, clock.now.minute, clock.now.second));
                      checks.expect(state.tick == 65, std::format("the demo generators stepped once per update, tick {}", state.tick));
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
