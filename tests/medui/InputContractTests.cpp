/**
 * @file InputContractTests.cpp
 * @brief BDD scenarios for `mdux.medui.input`: the bounded input vocabulary and its pure pieces (#315).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-018 Bounded input, application update order and action policy
 *
 * [ADR-018](../../docs/adr/ADR-018-bounded-input-and-update-order.md) defines the contract; the
 * clause numbers below are its. This suite covers the pieces #315 implements — the wire spellings
 * (clause 1), `normalizeSurfacePoint()` (clause 3), `PressLatch` (clause 4) and
 * `editWouldBeAccepted()` (clause 5). The `EventQueue` body and `applyEdit()` are #316's and are
 * tested there.
 *
 * The scenarios this issue is judged on:
 *
 * - `medui-input-normalize-floors-toward-negative-infinity` — the coordinate rule is floor, not a
 *   truncating cast. A truncating implementation passes every non-negative case and activates the
 *   wrong control the moment an out-of-flow `position:` puts one at a negative authored coordinate.
 * - `medui-input-press-latch-release-activates-only-the-armed-target` — a release on a different
 *   control does not activate the armed one, and cancellation disarms.
 * - `medui-input-edit-refuses-without-mutating` — a rejected edit returns its reason and the
 *   caller's value is untouched, because the predicate mutates nothing.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.font.schema;
import mdux.medui.input;
import mdux.medui.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace core = mdux::core;
namespace font = mdux::font;

// ---------------------------------------------------------------------------
// Compile-time contract: these types cross a bounded ring buffer and a replay boundary, so they
// must stay trivially copyable, and the pure operations must stay noexcept.
// ---------------------------------------------------------------------------

static_assert(std::is_trivially_copyable_v<ms::PointerEvent>);
static_assert(std::is_trivially_copyable_v<ms::KeyEvent>);
static_assert(std::is_trivially_copyable_v<ms::TextEvent>);
static_assert(std::is_trivially_copyable_v<ms::FocusEvent>);
static_assert(std::is_trivially_copyable_v<ms::EditOp>);
static_assert(std::is_trivially_copyable_v<ms::SurfacePoint>);
static_assert(std::is_trivially_copyable_v<ms::PressLatch>);
static_assert(std::is_trivially_copyable_v<ms::ActionTrace>);

// A latch is one string_view and a flag — it fits two pointers plus alignment, so a caller can
// hold one per focusable region without thinking about it.
static_assert(sizeof(ms::PressLatch) <= sizeof(std::string_view) + alignof(std::string_view));

// The latch operations are noexcept. The argument is a pre-built string_view, because
// `string_view(const char*)` is not itself noexcept on every standard library (libc++ leaves the
// `char_traits::length` call unmarked), and that is not what is under test here.
inline constexpr std::string_view sampleNodeId{"stop-button"};
static_assert(noexcept(ms::PressLatch{}.arm(sampleNodeId)));
static_assert(noexcept(ms::PressLatch{}.release(sampleNodeId)));
static_assert(noexcept(ms::PressLatch{}.cancel()));
static_assert(noexcept(ms::normalizeSurfacePoint(0, 0, 1, 1, 0, 0)));
static_assert(noexcept(ms::editWouldBeAccepted(U'x', {}, 0, 1)));

// The wire spellings are constant-evaluable, and the aggregate default never round-trips.
static_assert(ms::toWire(ms::PointerKind::Down) == "Down");
static_assert(ms::toWire(ms::PointerKind::Unspecified).empty());
static_assert(!ms::pointerKindFromWire("").has_value());
static_assert(ms::pointerKindFromWire("Cancel") == ms::PointerKind::Cancel);

// The floor helper, checked directly at the sign boundary.
static_assert(ms::floorScaled(-1, 1, 2) == -1);   // floor(-0.5)
static_assert(ms::floorScaled(1, 1, 2) == 0);     // floor(0.5)
static_assert(ms::floorScaled(-4, 1, 2) == -2);   // exact, no step
static_assert(ms::floorScaled(7, 2, 1) == 14);

// ---------------------------------------------------------------------------
// Clause 1 — the closed vocabulary
// ---------------------------------------------------------------------------

/// Round-trips one enum's whole non-default set and proves the default and an unknown string map
/// to nothing.
template <typename Kind>
void checkClosedWire(mdux::spec::Checks& checks, std::string_view name, Kind aggregateDefault,
                     std::span<const Kind> members, auto toWireFn, auto fromWireFn) {
    checks.expect(toWireFn(aggregateDefault).empty(),
                  std::format("{}: the aggregate default has no spelling", name));
    checks.expect(!fromWireFn(std::string_view{}).has_value(),
                  std::format("{}: the empty string is not a member", name));
    checks.expect(!fromWireFn(std::string_view{"NotAMember"}).has_value(),
                  std::format("{}: an unknown spelling is not a member", name));
    for (const Kind member : members) {
        const std::string_view wire = toWireFn(member);
        checks.expect(!wire.empty(), std::format("{}: a real member has a spelling", name));
        checks.expect(fromWireFn(wire) == member,
                      std::format("{}: '{}' round-trips", name, wire));
    }
}

const mdux::spec::Register everyEventEnumIsAClosedWireSet{
    "Every input enum has contract spellings, and its aggregate default is never one of them",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-enums-are-closed-wire-sets")
            .Given("the pointer, key, focus, key-code and edit enums", [] {})
            .When("each member is taken to its wire spelling and back", [] {})
            .Then("every real member round-trips and neither the default nor an unknown string does",
                  [] {
                      mdux::spec::Checks checks;

                      static constexpr std::array pointerKinds{ms::PointerKind::Down, ms::PointerKind::Up,
                                                               ms::PointerKind::Move, ms::PointerKind::Cancel};
                      checkClosedWire<ms::PointerKind>(
                          checks, "PointerKind", ms::PointerKind::Unspecified, pointerKinds,
                          [](ms::PointerKind k) { return ms::toWire(k); },
                          [](std::string_view w) { return ms::pointerKindFromWire(w); });

                      static constexpr std::array keyKinds{ms::KeyKind::Down, ms::KeyKind::Up};
                      checkClosedWire<ms::KeyKind>(
                          checks, "KeyKind", ms::KeyKind::Unspecified, keyKinds,
                          [](ms::KeyKind k) { return ms::toWire(k); },
                          [](std::string_view w) { return ms::keyKindFromWire(w); });

                      static constexpr std::array focusKinds{ms::FocusKind::Enter, ms::FocusKind::Leave};
                      checkClosedWire<ms::FocusKind>(
                          checks, "FocusKind", ms::FocusKind::Unspecified, focusKinds,
                          [](ms::FocusKind k) { return ms::toWire(k); },
                          [](std::string_view w) { return ms::focusKindFromWire(w); });

                      static constexpr std::array keyCodes{
                          ms::KeyCode::CaretLeft,  ms::KeyCode::CaretRight, ms::KeyCode::CaretHome,
                          ms::KeyCode::CaretEnd,   ms::KeyCode::DeleteBack, ms::KeyCode::DeleteForward,
                          ms::KeyCode::FocusNext,  ms::KeyCode::FocusPrev,  ms::KeyCode::Commit,
                          ms::KeyCode::Cancel};
                      checkClosedWire<ms::KeyCode>(
                          checks, "KeyCode", ms::KeyCode::Unspecified, keyCodes,
                          [](ms::KeyCode k) { return ms::toWire(k); },
                          [](std::string_view w) { return ms::keyCodeFromWire(w); });

                      static constexpr std::array editKinds{ms::EditKind::InsertScalar, ms::EditKind::DeleteBack,
                                                            ms::EditKind::DeleteForward, ms::EditKind::MoveCaret};
                      checkClosedWire<ms::EditKind>(
                          checks, "EditKind", ms::EditKind::Unspecified, editKinds,
                          [](ms::EditKind k) { return ms::toWire(k); },
                          [](std::string_view w) { return ms::editKindFromWire(w); });

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anInputEventCannotBeUnspecified{
    "An InputEvent is a variant of real event kinds, so an unspecified entry is not representable",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-event-has-no-unspecified-state")
            .Given("a default-constructed InputEvent", [] {})
            .When("its alternative is inspected", [] {})
            .Then("it holds a real event kind, not a null one",
                  [] {
                      mdux::spec::Checks checks;
                      const ms::InputEvent event{};
                      checks.expect(std::holds_alternative<ms::PointerEvent>(event),
                                    "a default InputEvent is a PointerEvent, the first alternative");
                      // The pointer it holds is itself unusable until a kind is set, which is the
                      // per-field default rule, not a whole-event null state.
                      checks.expect(std::get<ms::PointerEvent>(event).kind == ms::PointerKind::Unspecified,
                                    "and that pointer's kind is still Unspecified");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 3 — coordinate normalization
// ---------------------------------------------------------------------------

const mdux::spec::Register normalizeFloorsTowardNegativeInfinity{
    "normalizeSurfacePoint floors toward -inf, so a negative authored coordinate is not rounded up",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-normalize-floors-toward-negative-infinity")
            .Given("physical coordinates on both sides of the authored origin", [] {})
            .When("they are normalized at a half-scale ratio", [] {})
            .Then("each maps to the cell that contains it, flooring rather than truncating",
                  [] {
                      mdux::spec::Checks checks;

                      // num/den = authored pixels per physical pixel. 1/2 means two physical pixels
                      // per authored cell, so the sign boundary is where floor and trunc disagree.
                      const auto justInside = ms::normalizeSurfacePoint(1, 3, 1, 2, 0, 0);
                      checks.expect(justInside.has_value() && justInside->x == 0 && justInside->y == 1,
                                    "phys (1,3) at 1/2 -> authored (0,1)");

                      const auto onABoundary = ms::normalizeSurfacePoint(4, 8, 1, 2, 0, 0);
                      checks.expect(onABoundary.has_value() && onABoundary->x == 2 && onABoundary->y == 4,
                                    "phys (4,8) at 1/2 -> authored (2,4), exact");

                      const auto belowOrigin = ms::normalizeSurfacePoint(-1, -3, 1, 2, 0, 0);
                      checks.expect(belowOrigin.has_value() && belowOrigin->x == -1 && belowOrigin->y == -2,
                                    "phys (-1,-3) at 1/2 -> authored (-1,-2): floor(-0.5)=-1, not 0");

                      const auto withOrigin = ms::normalizeSurfacePoint(10, 10, 1, 1, 12, 12);
                      checks.expect(withOrigin.has_value() && withOrigin->x == -2 && withOrigin->y == -2,
                                    "an origin to the right/below yields negative authored coordinates");

                      const auto magnifying = ms::normalizeSurfacePoint(3, 5, 2, 1, 0, 0);
                      checks.expect(magnifying.has_value() && magnifying->x == 6 && magnifying->y == 10,
                                    "phys (3,5) at 2/1 -> authored (6,10)");

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register normalizeRefusesANonPositiveScale{
    "A coordinate scale with a non-positive term is refused, not divided by",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-normalize-refuses-a-non-positive-scale")
            .Given("scales with a zero denominator and a negative numerator", [] {})
            .When("a point is normalized against each", [] {})
            .Then("both are refused with MalformedScale",
                  [] {
                      mdux::spec::Checks checks;

                      const auto zeroDen = ms::normalizeSurfacePoint(1, 1, 1, 0, 0, 0);
                      checks.expect(!zeroDen.has_value() && zeroDen.error() == ms::InputError::MalformedScale,
                                    "a zero denominator is MalformedScale");

                      const auto negNum = ms::normalizeSurfacePoint(1, 1, -1, 1, 0, 0);
                      checks.expect(!negNum.has_value() && negNum.error() == ms::InputError::MalformedScale,
                                    "a negative numerator is MalformedScale");

                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 4 — press arms a target; release activates only the same target
// ---------------------------------------------------------------------------

const mdux::spec::Register releaseActivatesOnlyTheArmedTarget{
    "A press arms a control and a release activates it only if it lands on the same one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-press-latch-release-activates-only-the-armed-target")
            .Given("a latch that armed the id 'stop-button'", [] {})
            .When("a release lands on the same id, a different id, or after a cancel", [] {})
            .Then("only the same-id release activates, and the latch disarms every time",
                  [] {
                      mdux::spec::Checks checks;

                      ms::PressLatch latch;
                      latch.arm("stop-button");
                      checks.expect(latch.isArmed() && latch.armedNode() == "stop-button", "the latch is armed");
                      checks.expect(latch.release("stop-button"), "a release on the armed id activates");
                      checks.expect(!latch.isArmed(), "and the latch is disarmed afterwards");

                      latch.arm("stop-button");
                      checks.expect(!latch.release("freeze-button"), "a release on a different id does not activate");
                      checks.expect(!latch.isArmed(), "and it still disarms — a stale arm cannot linger");

                      latch.arm("stop-button");
                      latch.cancel();
                      checks.expect(!latch.release("stop-button"), "a release after cancel() does not activate");

                      checks.expect(!latch.release("stop-button"), "a release with nothing armed is a no-op, not an error");

                      latch.arm("");
                      checks.expect(!latch.isArmed(), "arming an empty id arms nothing");

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aLatchDoesNotActivateAcrossAStorageRebuild{
    "A caller that rebuilds its screen storage must cancel the latch, and a matching-value release then does not activate",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-press-latch-storage-rebuild-needs-cancel")
            .Given("a latch armed with an id viewing one buffer, then that buffer is replaced", [] {})
            .When("the caller cancels across the rebuild as the contract requires", [] {})
            .Then("a release carrying an equal-valued id from the new buffer does not activate",
                  [] {
                      mdux::spec::Checks checks;

                      const std::string firstBuffer  = "stop-button";
                      const std::string secondBuffer = "stop-button";  // same value, different storage

                      ms::PressLatch latch;
                      latch.arm(firstBuffer);
                      // The contract: a screen-storage rebuild is a disarm point (ADR-018 clause 4).
                      latch.cancel();
                      checks.expect(!latch.release(secondBuffer),
                                    "after the required cancel, the equal-valued id does not activate a stale arm");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 5 — bounded editing, no partial mutation
// ---------------------------------------------------------------------------

const mdux::spec::Register editRefusesWithoutMutating{
    "editWouldBeAccepted refuses an out-of-charset scalar and a full field, and mutates nothing",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-edit-refuses-without-mutating")
            .Given("a caller's value and caret, and a digits-only narrowed charset", [] {})
            .When("a letter, then a digit into a full field, then a digit with room are checked", [] {})
            .Then("each refusal names its reason, the accepted one passes, and the value is never touched",
                  [] {
                      mdux::spec::Checks checks;

                      // The caller's state. The predicate never receives it, which is the point.
                      std::u32string value = U"12";
                      std::size_t    caret = 2;

                      static constexpr std::array<font::CharsetRange, 1> digitsOnly{
                          font::CharsetRange{.first = U'0', .last = U'9'}};

                      const auto aLetter = ms::editWouldBeAccepted(U'A', digitsOnly, value.size(), 8);
                      checks.expect(!aLetter.has_value() && aLetter.error() == ms::InputError::ScalarNotPermitted,
                                    "a letter is outside a digits-only charset");

                      const auto whenFull = ms::editWouldBeAccepted(U'3', digitsOnly, /*currentLength=*/4, /*maxLength=*/4);
                      checks.expect(!whenFull.has_value() && whenFull.error() == ms::InputError::FieldAtCapacity,
                                    "a digit into a field at max_length is FieldAtCapacity");

                      const auto whenRoom = ms::editWouldBeAccepted(U'3', digitsOnly, value.size(), 8);
                      checks.expect(whenRoom.has_value(), "a digit with room and in-charset is accepted");

                      // An empty narrowed set narrows nothing and admits any scalar (the #297 rule).
                      const auto noNarrowing = ms::editWouldBeAccepted(U'@', {}, 0, 4);
                      checks.expect(noNarrowing.has_value(), "an empty charset admits everything within capacity");

                      checks.expect(value == U"12" && caret == 2, "the caller's value and caret are untouched");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 7 — the traceable critical-action record
// ---------------------------------------------------------------------------

const mdux::spec::Register anActionTraceCarriesTheClosedEventAndItsRequirement{
    "An ActionTrace carries the closed SystemEvent, the requirement it is traced to, and an order",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-action-trace-carries-the-closed-event")
            .Given("two critical presses a host is about to act on", [] {})
            .When("each is recorded as an ActionTrace", [] {})
            .Then("NoOp and TriggerHalt stay distinct and the sequence orders them",
                  [] {
                      mdux::spec::Checks checks;

                      const ms::ActionTrace first{
                          .nodeId = "ack", .requirement = "REQ-UI-014", .event = ms::SystemEvent::NoOp, .sequence = 1};
                      const ms::ActionTrace second{
                          .nodeId = "halt", .requirement = "REQ-SAFETY-002", .event = ms::SystemEvent::TriggerHalt, .sequence = 2};

                      checks.expect(first.event != second.event, "NoOp is not TriggerHalt");
                      checks.expect(ms::toWire(second.event) == "TriggerHalt", "the event keeps its contract spelling");
                      checks.expect(!second.requirement.empty(), "a critical action carries the requirement it is traced to");
                      checks.expect(first.sequence < second.sequence, "the sequence gives the host an order");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
