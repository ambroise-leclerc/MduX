/**
 * @file InputNoHeapTests.cpp
 * @brief Layer 1 of the no-heap property for `mdux.medui.input` (#316): the event queue and the
 *        field editor make no `operator new` call, proved by interposition.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no allocation)
 * @compliance ADR-018 Bounded input, application update order and action policy
 *
 * ADR-018's storage is caller-owned: the queue is a ring over the caller's span, and every edit
 * shifts scalars inside the caller's own buffer. "It allocates nothing" is easy to claim and easy
 * to break silently - a `std::vector` scratch in a helper, a `std::string` on an error path. This
 * binary replaces the global `operator new` family with counting versions and measures instead,
 * on the accepted *and* the refused paths.
 *
 * `tests/framework/CountingAllocations.hpp` says what the interposition catches and what it does
 * not; the object-file symbol scan (`screen.noheap.symbolScan`, which already covers
 * `Input.cppm`'s object because it filters MduXCore on `/medui/`) is layer 2.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.font.schema;
import mdux.medui.input;

#include "../framework/SpecLabBridge.hpp"

// The interposition itself. Included by exactly one translation unit in this binary.
#include "../framework/CountingAllocations.hpp"

namespace {

namespace ms = mdux::medui;

constexpr std::array<mdux::font::CharsetRange, 1> asciiFont{mdux::font::CharsetRange{.first = U' ', .last = U'~'}};
constexpr std::array<mdux::font::CharsetRange, 1> digits{mdux::font::CharsetRange{.first = U'0', .last = U'9'}};

[[nodiscard]] ms::InputEvent down(mdux::core::Px x) {
    return ms::InputEvent{ms::PointerEvent{.kind = ms::PointerKind::Down, .x = x, .y = 0}};
}

}  // namespace

const mdux::spec::Register theCounterMoves{
    "The allocation counter moves when something allocates",
    "noheap",
    [] {
        return speclab::Test("medui-input-noheap-selftest")
            .Given("the interposed operator new", [] {})
            .When("a deliberate allocation is made", [] {})
            .Then("the counter records it",
                  [] {
                      mdux::spec::Checks checks;
                      // Without this scenario the file would be a test that cannot fail: if the
                      // interposition ever stopped taking effect, the counter would never move and
                      // every scenario below would pass on it.
                      const std::size_t before = allocations();
                      auto* volatile    leaked = new std::array<std::byte, 64>{};
                      const std::size_t after  = allocations();
                      delete leaked;
                      checks.expect(after > before, std::format("the counter moved, {} then {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theQueueAllocatesNothing{
    "Pushing, draining and overflowing the event queue allocates nothing",
    "noheap",
    [] {
        return speclab::Test("medui-input-noheap-queue")
            .Given("a queue over a fixed span", [] {})
            .When("it is filled past capacity, drained, and wrapped", [] {})
            .Then("not one allocation happens after construction",
                  [] {
                      mdux::spec::Checks             checks;
                      std::array<ms::InputEvent, 8>  storage{};
                      ms::EventQueue                 queue{storage};

                      const std::size_t before = allocations();
                      for (mdux::core::Px i = 0; i < 20; ++i) {
                          (void)queue.push(down(i));  // some Accepted, some DroppedNewest
                      }
                      while (const auto event = queue.pop()) {
                          (void)event;
                      }
                      for (mdux::core::Px i = 0; i < 12; ++i) {
                          (void)queue.push(down(i));
                          (void)queue.pop();  // head chases tail: exercises the wrap
                      }
                      queue.clear();
                      const std::size_t after = allocations();

                      checks.expect(after == before, std::format("no allocation, {} then {}", before, after));
                      checks.expect(queue.droppedCount() > 0, "and the overflow path really was exercised");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theEditorAllocatesNothing{
    "Creating a field editor and running edits - accepted and refused - allocates nothing after creation",
    "noheap",
    [] {
        return speclab::Test("medui-input-noheap-editor")
            .Given("an editor over a fixed buffer, focused", [] {})
            .When("inserts, deletes, caret moves and a run of refused edits are applied", [] {})
            .Then("only creation touched the heap, and nothing after it",
                  [] {
                      mdux::spec::Checks       checks;
                      std::array<char32_t, 32> buffer{};
                      static constexpr std::array<char32_t, 2> initial{U'1', U'2'};

                      auto made = ms::FieldEditor::create("f", buffer, initial, asciiFont, digits, 6);
                      if (!made.has_value()) {
                          checks.expect(false, "the editor was created");
                          checks.raise();
                          return;
                      }
                      ms::FieldEditor editor = *made;
                      editor.focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = "f"});

                      const std::size_t before = allocations();

                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 0});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'9'});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'A'});   // refused: charset
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'8'});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'7'});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'6'});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'5'});   // refused: full
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::DeleteForward});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::DeleteBack});
                      (void)editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 999});       // refused: range
                      (void)editor.apply(ms::EditOp{});                                                      // refused: malformed
                      (void)editor.handleKey(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::CaretEnd});
                      (void)editor.handleKey(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::Commit});
                      (void)editor.handleText(ms::TextEvent{.scalar = U'4'});

                      const std::size_t after = allocations();
                      checks.expect(after == before, std::format("no allocation after create(), {} then {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};
