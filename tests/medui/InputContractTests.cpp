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
import mdux.draw;
import mdux.font.schema;
import mdux.medui.input;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.evidence.report;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace core = mdux::core;
namespace font = mdux::font;
namespace draw = mdux::draw;

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
static_assert(noexcept(ms::editWouldBeAccepted(U'x', {}, {}, 0, 1)));

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

const mdux::spec::Register normalizeFailsClosedOnBadInput{
    "normalizeSurfacePoint fails closed on a malformed scale or an out-of-range result, never wrapping",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-normalize-fails-closed")
            .Given("a zero denominator, a negative numerator, an oversized scale, and a coordinate that overflows Px", [] {})
            .When("a point is normalized against each", [] {})
            .Then("each is refused with the error that names it, and none returns a silently wrapped coordinate",
                  [] {
                      mdux::spec::Checks checks;

                      const auto zeroDen = ms::normalizeSurfacePoint(1, 1, 1, 0, 0, 0);
                      checks.expect(!zeroDen.has_value() && zeroDen.error() == ms::InputError::MalformedScale,
                                    "a zero denominator is MalformedScale");

                      const auto negNum = ms::normalizeSurfacePoint(1, 1, -1, 1, 0, 0);
                      checks.expect(!negNum.has_value() && negNum.error() == ms::InputError::MalformedScale,
                                    "a negative numerator is MalformedScale");

                      const auto hugeScale = ms::normalizeSurfacePoint(1, 1, ms::maxCoordinateScale + 1, 1, 0, 0);
                      checks.expect(!hugeScale.has_value() && hugeScale.error() == ms::InputError::MalformedScale,
                                    "a numerator past maxCoordinateScale is MalformedScale");

                      // The largest Px physical coordinate, magnified so the result leaves Px.
                      const auto overflows = ms::normalizeSurfacePoint(
                          std::numeric_limits<mdux::core::Px>::max(), 0, ms::maxCoordinateScale, 1, 0, 0);
                      checks.expect(!overflows.has_value() && overflows.error() == ms::InputError::CoordinateOutOfRange,
                                    "a result past INT32_MAX is CoordinateOutOfRange, not a wrapped value");

                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 3 — SurfaceMapping, the adapter's per-frame coordinate transform (#317, ADR-019)
// ---------------------------------------------------------------------------

static_assert(std::is_trivially_copyable_v<ms::SurfaceMapping>);
static_assert(noexcept(ms::SurfaceMapping::create({1, 1}, {1, 1})));
static_assert(ms::SurfaceMapping::identity().scaleNum == 1 && ms::SurfaceMapping::identity().scaleDen == 1);

// A framebuffer equal to the window is the identity, constant-evaluated.
static_assert([] {
    const auto m = ms::SurfaceMapping::create({400, 300}, {400, 300});
    return m && m->scaleNum == 1 && m->scaleDen == 1 && m->originX == 0 && m->originY == 0;
}());

const mdux::spec::Register surfaceMappingCarriesTheDevicePixelRatio{
    "SurfaceMapping carries the framebuffer-to-window device-pixel ratio and fails closed on a non-uniform or degenerate one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-surface-mapping-device-pixel-ratio")
            .Given("a window, and framebuffer extents at 1x, 2x, 3x, a non-uniform one, and a degenerate one", [] {})
            .When("a mapping is built for each", [] {})
            .Then("an integer uniform ratio is carried and everything else is refused rather than approximated",
                  [] {
                      mdux::spec::Checks checks;

                      const auto oneToOne = ms::SurfaceMapping::create({400, 300}, {400, 300});
                      checks.expect(oneToOne && oneToOne->scaleNum == 1 && oneToOne->scaleDen == 1 &&
                                        oneToOne->originX == 0 && oneToOne->originY == 0,
                                    "a framebuffer equal to the window is the identity");

                      const auto retina = ms::SurfaceMapping::create({800, 600}, {400, 300});
                      checks.expect(retina && retina->scaleNum == 2 && retina->scaleDen == 1,
                                    "a 2x framebuffer is a 2/1 authored-per-window ratio");

                      const auto scaled3 = ms::SurfaceMapping::create({1200, 900}, {400, 300});
                      checks.expect(scaled3 && scaled3->scaleNum == 3 && scaled3->scaleDen == 1, "and a 3x one is 3/1");

                      // 800x601 against 400x300: width says 2/1, height says 601/300 — not uniform.
                      const auto anamorphic = ms::SurfaceMapping::create({800, 601}, {400, 300});
                      checks.expect(!anamorphic && anamorphic.error() == ms::InputError::MalformedScale,
                                    "a framebuffer whose axes carry different ratios is refused, not squashed onto one axis");

                      const auto degenerate = ms::SurfaceMapping::create({0, 300}, {400, 300});
                      checks.expect(!degenerate && degenerate.error() == ms::InputError::MalformedScale,
                                    "a zero framebuffer dimension is MalformedScale, not a divide by zero");

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register renderingAndHitTestingAgreeAcrossScale{
    "A pointer mapped through SurfaceMapping resolves to the same control at 1x and at 2x DPI — rendering and hit testing agree",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-surface-mapping-hit-testing-agrees")
            .Given("a 400x300 screen with a critical control at (100,100)-(220,140), drawn 1:1 at the framebuffer origin", [] {})
            .When("a window-space point over the control is mapped and resolved at a 1x and a 2x device-pixel ratio", [] {})
            .Then("both resolve to the control, and a window point away from it resolves to nothing",
                  [] {
                      mdux::spec::Checks checks;

                      static constexpr ms::CriticalButtonSpec haltSpec{
                          .requirement = "REQ-EM-003", .labelKey = "STR-HALT",
                          .colorToken = "Theme.Colors.Fault", .onPress = ms::SystemEvent::TriggerHalt};
                      static constexpr std::array<ms::CompiledNode, 1> nodes{
                          ms::CompiledNode{.id = "emergency-halt", .bounds = {100, 100, 120, 40}, .payload = haltSpec}};
                      static constexpr std::array approvals{
                          ms::TextPackageApproval{.locale = "en-US", .packageId = "mon-text", .packageSha256 = {1}}};
                      static constexpr ms::ScreenPackage screen{
                          .id                   = "monitor",
                          .schemaVersion        = mdux::evidence::kSchemaVersion,
                          .surfaceWidth         = 400,
                          .surfaceHeight        = 300,
                          .approvedTextPackages = approvals,
                          .nodes                = nodes,
                          .budget               = draw::DrawBudget{.maxVertices = 64, .maxIndices = 96, .maxCommands = 8}};
                      static_assert(screen.validate().has_value(), "the reference screen is one a device could hold");

                      const auto resolveAt = [&](const ms::SurfaceMapping& m, core::Px winX, core::Px winY)
                          -> std::optional<std::string_view> {
                          const auto pt = m.toSurface(winX, winY);
                          if (!pt) { return std::nullopt; }
                          const auto press = ms::resolvePress(screen, pt->x, pt->y);
                          if (!press || !press->has_value()) { return std::nullopt; }
                          return (*press)->nodeId;
                      };

                      // 1x: window == framebuffer == authored. The control is at window (100,100)-(220,140).
                      const auto at1x = ms::SurfaceMapping::create({400, 300}, {400, 300});
                      // 2x: a 200x150 window on a HiDPI display, framebuffer 400x300. The control is
                      // drawn at framebuffer px (100,100) and so appears at window px (50,50).
                      const auto at2x = ms::SurfaceMapping::create({400, 300}, {200, 150});
                      checks.expect(at1x && at2x, "both mappings are well-formed");

                      checks.expect(resolveAt(*at1x, 150, 120) == std::optional<std::string_view>{"emergency-halt"},
                                    "at 1x, a window point over the control resolves to it");
                      checks.expect(resolveAt(*at2x, 75, 60) == std::optional<std::string_view>{"emergency-halt"},
                                    "at 2x, the halved window point over the same on-screen pixels resolves to the same control");
                      checks.expect(resolveAt(*at2x, 10, 10) == std::nullopt,
                                    "a window point away from the control resolves to nothing");

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
    "editWouldBeAccepted applies both charset bounds and a capacity bound, and mutates nothing",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-edit-refuses-without-mutating")
            .Given("a caller's value and caret, an ASCII-only font, and a digits-only node charset", [] {})
            .When("a scalar the font cannot draw, a scalar the node excludes, a full field, and a valid digit are checked", [] {})
            .Then("each refusal names its reason, the accepted one passes, and the value is never touched",
                  [] {
                      mdux::spec::Checks checks;

                      // The caller's state. The predicate never receives it, which is the point.
                      std::u32string value = U"12";
                      std::size_t    caret = 2;

                      // The committed dejavu-ui package draws printable ASCII; a real font would carry
                      // more ranges, but one is enough to prove the bound is applied.
                      static constexpr std::array<font::CharsetRange, 1> asciiFont{
                          font::CharsetRange{.first = U' ', .last = U'~'}};
                      static constexpr std::array<font::CharsetRange, 1> digitsOnly{
                          font::CharsetRange{.first = U'0', .last = U'9'}};

                      // An emoji the font has no glyph for — the physical limit, asked first.
                      const auto anEmoji = ms::editWouldBeAccepted(U'\U0001F642', asciiFont, digitsOnly, value.size(), 8);
                      checks.expect(!anEmoji.has_value() && anEmoji.error() == ms::InputError::ScalarNotInFont,
                                    "a scalar outside the font's charset is ScalarNotInFont, even though the node narrows nothing further");

                      // A letter the font draws but the digits-only node excludes — the policy limit.
                      const auto aLetter = ms::editWouldBeAccepted(U'A', asciiFont, digitsOnly, value.size(), 8);
                      checks.expect(!aLetter.has_value() && aLetter.error() == ms::InputError::ScalarNotPermitted,
                                    "a letter the font can draw but the node excludes is ScalarNotPermitted");

                      const auto whenFull = ms::editWouldBeAccepted(U'3', asciiFont, digitsOnly, /*currentLength=*/4, /*maxLength=*/4);
                      checks.expect(!whenFull.has_value() && whenFull.error() == ms::InputError::FieldAtCapacity,
                                    "a digit into a field at max_length is FieldAtCapacity — checked before the charset");

                      const auto whenRoom = ms::editWouldBeAccepted(U'3', asciiFont, digitsOnly, value.size(), 8);
                      checks.expect(whenRoom.has_value(), "a digit the font draws, in the node's charset, with room, is accepted");

                      // An empty node charset narrows nothing; the font bound still applies.
                      const auto noNarrowing = ms::editWouldBeAccepted(U'@', asciiFont, {}, 0, 4);
                      checks.expect(noNarrowing.has_value(), "with no node narrowing, any scalar the font can draw is accepted");

                      // An empty font charset is a malformed package and draws nothing.
                      const auto noFont = ms::editWouldBeAccepted(U'3', {}, {}, 0, 4);
                      checks.expect(!noFont.has_value() && noFont.error() == ms::InputError::ScalarNotInFont,
                                    "an empty font charset refuses everything rather than admitting all");

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

// ---------------------------------------------------------------------------
// Clause 2 — the bounded event queue (#316)
// ---------------------------------------------------------------------------

/// A pointer-down at (x, 0), for terse queue scenarios.
[[nodiscard]] ms::InputEvent down(mdux::core::Px x) {
    return ms::InputEvent{ms::PointerEvent{.kind = ms::PointerKind::Down, .x = x, .y = 0}};
}

const mdux::spec::Register queueIsFifoAndBounded{
    "The event queue delivers events in order and drops the newest once its storage is full",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-queue-is-fifo-and-drops-newest-on-overflow")
            .Given("a queue over storage for three events", [] {})
            .When("five are pushed and then all are drained", [] {})
            .Then("the first three come back in order, the last two were dropped, and the counter saturates at the drop count",
                  [] {
                      mdux::spec::Checks checks;

                      std::array<ms::InputEvent, 3> storage{};
                      ms::EventQueue                queue{storage};

                      checks.expect(queue.push(down(1)) == ms::PushOutcome::Accepted, "the first fits");
                      checks.expect(queue.push(down(2)) == ms::PushOutcome::Accepted, "the second fits");
                      checks.expect(queue.push(down(3)) == ms::PushOutcome::Accepted, "the third fills it");
                      checks.expect(queue.full() && queue.size() == 3, "the queue is full");
                      checks.expect(queue.push(down(4)) == ms::PushOutcome::DroppedNewest, "the fourth is dropped");
                      checks.expect(queue.push(down(5)) == ms::PushOutcome::DroppedNewest, "so is the fifth");
                      checks.expect(queue.droppedCount() == 2, "two events were dropped");

                      const auto a = queue.pop();
                      const auto b = queue.pop();
                      const auto c = queue.pop();
                      checks.expect(a.has_value() && std::get<ms::PointerEvent>(*a).x == 1, "the first out is the first in");
                      checks.expect(b.has_value() && std::get<ms::PointerEvent>(*b).x == 2, "then the second");
                      checks.expect(c.has_value() && std::get<ms::PointerEvent>(*c).x == 3, "then the third — the dropped two never entered");
                      checks.expect(!queue.pop().has_value() && queue.empty(), "and then it is empty");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register queueRingWrapsAndClears{
    "The queue is a ring: draining then filling again reuses the storage, and clear() empties it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-queue-ring-wraps-and-clears")
            .Given("a queue over storage for two events, partly drained", [] {})
            .When("more are pushed past the physical end of the buffer, and then clear() is called", [] {})
            .Then("order is still FIFO across the wrap, and clear() discards the batch but not the drop count",
                  [] {
                      mdux::spec::Checks checks;

                      std::array<ms::InputEvent, 2> storage{};
                      ms::EventQueue                queue{storage};

                      checks.expect(queue.push(down(1)) == ms::PushOutcome::Accepted, "push 1");
                      checks.expect(queue.push(down(2)) == ms::PushOutcome::Accepted, "push 2");
                      checks.expect(queue.pop().has_value(), "pop 1 — head advances");
                      checks.expect(queue.push(down(3)) == ms::PushOutcome::Accepted, "push 3 wraps into slot 0");
                      checks.expect(queue.push(down(4)) == ms::PushOutcome::DroppedNewest, "push 4 overflows");

                      const auto x = queue.pop();
                      const auto y = queue.pop();
                      checks.expect(x.has_value() && std::get<ms::PointerEvent>(*x).x == 2, "2 comes out before 3 across the wrap");
                      checks.expect(y.has_value() && std::get<ms::PointerEvent>(*y).x == 3, "then 3");

                      checks.expect(queue.push(down(5)) == ms::PushOutcome::Accepted, "and the ring keeps going");
                      queue.clear();
                      checks.expect(queue.empty() && queue.size() == 0, "clear() empties it");
                      checks.expect(queue.droppedCount() == 1, "but the drop count survives");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register zeroCapacityQueueDropsEverything{
    "A queue over an empty span has zero capacity and drops every event without a division by its size",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-queue-zero-capacity")
            .Given("a queue over an empty span", [] {})
            .When("events are pushed", [] {})
            .Then("every one is DroppedNewest and pop() is always empty",
                  [] {
                      mdux::spec::Checks checks;
                      ms::EventQueue     queue{std::span<ms::InputEvent>{}};
                      checks.expect(queue.capacity() == 0, "capacity is zero");
                      checks.expect(queue.push(down(1)) == ms::PushOutcome::DroppedNewest, "the first is dropped");
                      checks.expect(queue.push(down(2)) == ms::PushOutcome::DroppedNewest, "so is the second");
                      checks.expect(queue.droppedCount() == 2 && !queue.pop().has_value(), "and nothing can be drained");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Clause 5 — the FieldEditor (#316)
// ---------------------------------------------------------------------------

namespace fixture {
inline constexpr std::array<font::CharsetRange, 1> asciiFont{font::CharsetRange{.first = U' ', .last = U'~'}};
inline constexpr std::array<font::CharsetRange, 1> digits{font::CharsetRange{.first = U'0', .last = U'9'}};
}  // namespace fixture

const mdux::spec::Register editorCreateEnforcesItsBounds{
    "FieldEditor::create refuses an oversized max_length, an over-long initial value, and an out-of-charset one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-create-enforces-bounds")
            .Given("storage for eight scalars and an ASCII font with a digits-only node charset", [] {})
            .When("create is called with a too-large max_length, a too-long value, and a value with a letter", [] {})
            .Then("each is refused, and a well-formed call succeeds with no caret",
                  [] {
                      mdux::spec::Checks checks;
                      std::array<char32_t, 8> storage{};

                      const auto tooWide = ms::FieldEditor::create("id", storage, {}, fixture::asciiFont, fixture::digits, 9);
                      checks.expect(!tooWide.has_value() && tooWide.error() == ms::InputError::FieldAtCapacity,
                                    "max_length past the storage is refused");

                      static constexpr std::array<char32_t, 5> longValue{U'1', U'2', U'3', U'4', U'5'};
                      const auto tooLong = ms::FieldEditor::create("id", storage, longValue, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(!tooLong.has_value() && tooLong.error() == ms::InputError::FieldAtCapacity,
                                    "an initial value longer than max_length is refused");

                      static constexpr std::array<char32_t, 2> hasLetter{U'1', U'A'};
                      const auto badChar = ms::FieldEditor::create("id", storage, hasLetter, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(!badChar.has_value() && badChar.error() == ms::InputError::ScalarNotPermitted,
                                    "an initial value outside the node charset is refused");

                      static constexpr std::array<char32_t, 2> ok{U'1', U'2'};
                      const auto editor = ms::FieldEditor::create("id", storage, ok, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(editor.has_value(), "a well-formed value is accepted");
                      if (editor.has_value()) {
                          checks.expect(editor->length() == 2 && !editor->caret().has_value(),
                                        "it holds the value and is not yet being edited");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editorCreateNeverWritesOnRejection{
    "A rejected FieldEditor::create leaves the caller's buffer exactly as it found it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-create-rejection-does-not-write")
            .Given("a buffer already holding \"9999\" and a digits-only node charset", [] {})
            .When("create is called with the value \"1A\" (a digit then a letter)", [] {})
            .Then("it is refused for the letter, and the buffer is still \"9999\"",
                  [] {
                      mdux::spec::Checks           checks;
                      std::array<char32_t, 8>      storage{U'9', U'9', U'9', U'9', 0, 0, 0, 0};
                      static constexpr std::array<char32_t, 2> oneLetter{U'1', U'A'};

                      const auto r = ms::FieldEditor::create("id", storage, oneLetter, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(!r.has_value() && r.error() == ms::InputError::ScalarNotPermitted,
                                    "the letter is rejected");
                      checks.expect(storage[0] == U'9' && storage[1] == U'9' && storage[2] == U'9' && storage[3] == U'9',
                                    "not one scalar of the caller's buffer was overwritten");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editorCreateCopiesOverlapSafely{
    "FieldEditor::create copies an initial value that is a view of its own storage without corrupting it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-create-overlap-safe")
            .Given("one buffer where the initial value \"12\" sits one scalar ahead of the destination", [] {})
            .When("create copies it into the overlapping destination", [] {})
            .Then("the value is \"12\", not the \"11\" a forward element copy would produce",
                  [] {
                      mdux::spec::Checks       checks;
                      std::array<char32_t, 10> buffer{U'1', U'2', U'3', 0, 0, 0, 0, 0, 0, 0};

                      // destination overlaps the source: storage[0] is buffer[1], initial[1].
                      const std::span<char32_t>       storage{buffer.data() + 1, 8};
                      const std::span<const char32_t> initial{buffer.data(), 2};

                      const auto r = ms::FieldEditor::create("id", storage, initial, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(r.has_value(), "an overlapping in-buffer initial value is accepted");
                      if (r.has_value()) {
                          const auto v = r->value();
                          checks.expect(std::u32string_view{v.data(), v.size()} == U"12",
                                        "memmove shifted it left without reading an already-overwritten scalar");
                      }

                      // And the rejection path is overlap-safe too: "1A" must fail on the letter,
                      // not be silently turned into "11" by a corrupting copy.
                      std::array<char32_t, 10> withLetter{U'1', U'A', 0, 0, 0, 0, 0, 0, 0, 0};
                      const std::span<char32_t>       dst{withLetter.data() + 1, 8};
                      const std::span<const char32_t> src{withLetter.data(), 2};
                      const auto bad = ms::FieldEditor::create("id", dst, src, fixture::asciiFont, fixture::digits, 4);
                      checks.expect(!bad.has_value() && bad.error() == ms::InputError::ScalarNotPermitted,
                                    "\"1A\" is rejected for the letter, never accepted as \"11\"");
                      checks.expect(withLetter[0] == U'1' && withLetter[1] == U'A', "and nothing was copied");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editorInsertsDeletesAndMovesTheCaret{
    "A focused FieldEditor inserts at the caret, backspaces, deletes forward, and moves the caret",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-insert-delete-and-caret-moves")
            .Given("a focused editor holding \"25\"", [] {})
            .When("a 1 is inserted at the front, a 3 appended, then a backspace and a delete-forward", [] {})
            .Then("the value tracks each edit and the caret follows",
                  [] {
                      mdux::spec::Checks       checks;
                      std::array<char32_t, 16> storage{};
                      static constexpr std::array<char32_t, 2> initial{U'2', U'5'};

                      auto made = ms::FieldEditor::create("dose", storage, initial, fixture::asciiFont, fixture::digits, 8);
                      if (!made.has_value()) {
                          checks.expect(false, "the editor was created");
                          checks.raise();
                          return;
                      }
                      ms::FieldEditor editor = *made;
                      editor.focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = "dose"});
                      checks.expect(editor.caret() == std::optional<std::size_t>{2}, "focus puts the caret after the last scalar");

                      const auto valueIs = [&](std::u32string_view want, std::string_view what) {
                          const auto v = editor.value();
                          checks.expect(std::u32string_view{v.data(), v.size()} == want, what);
                      };

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 0}).has_value(), "caret home");
                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'1'}).has_value(), "insert 1 at the front");
                      valueIs(U"125", "the 1 landed before the 2");
                      checks.expect(editor.caret() == std::optional<std::size_t>{1}, "the caret advanced past the inserted scalar");

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = editor.length()}).has_value(), "caret end");
                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'3'}).has_value(), "append 3");
                      valueIs(U"1253", "the 3 landed at the end");

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::DeleteBack}).has_value(), "backspace");
                      valueIs(U"125", "backspace removed the trailing 3");
                      checks.expect(editor.caret() == std::optional<std::size_t>{3}, "the caret moved back");

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 1}).has_value(), "caret between 1 and 2");
                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::DeleteForward}).has_value(), "delete forward");
                      valueIs(U"15", "delete-forward removed the 2, the caret stayed put");
                      checks.expect(editor.caret() == std::optional<std::size_t>{1}, "the caret did not move on delete-forward");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editorRefusesLeaveEverythingUnchanged{
    "Every FieldEditor refusal leaves the value and caret exactly as they were",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-refusals-do-not-mutate")
            .Given("an editor holding \"123\", full at max_length 3", [] {})
            .When("an unfocused edit, a kindless op, an out-of-charset scalar, a full-field insert, a boundary delete, and an out-of-range caret are attempted", [] {})
            .Then("each names its reason and, checked right after it, leaves the value and caret exactly where they were",
                  [] {
                      mdux::spec::Checks       checks;
                      std::array<char32_t, 8>  storage{};
                      static constexpr std::array<char32_t, 2> initial{U'1', U'2'};

                      auto made = ms::FieldEditor::create("pin", storage, initial, fixture::asciiFont, fixture::digits, 3);
                      if (!made.has_value()) { checks.expect(false, "created"); checks.raise(); return; }
                      ms::FieldEditor editor = *made;

                      // Asserts, directly, that one refused operation changed neither the value nor
                      // the caret - the state as it stood right before it is the state right after.
                      const auto refusedUnchanged = [&](const char* what, ms::InputError want,
                                                        mdux::core::Result<void, ms::InputError> got) {
                          const auto v0 = editor.value();
                          const std::u32string valueBefore{v0.data(), v0.size()};
                          const auto caretBefore = editor.caret();
                          checks.expect(!got.has_value() && got.error() == want, what);
                          const auto v1 = editor.value();
                          checks.expect(std::u32string{v1.data(), v1.size()} == valueBefore && editor.caret() == caretBefore,
                                        std::format("{}: value and caret unchanged", what));
                      };

                      // Not focused yet: an edit is refused and nothing moves.
                      refusedUnchanged("an edit before focus is NotFocused", ms::InputError::NotFocused,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'3'}));

                      editor.focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = "pin"});

                      refusedUnchanged("an EditOp with no kind is MalformedEditOp", ms::InputError::MalformedEditOp,
                                       editor.apply(ms::EditOp{}));
                      refusedUnchanged("a letter is ScalarNotPermitted", ms::InputError::ScalarNotPermitted,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'A'}));
                      refusedUnchanged("an emoji is ScalarNotInFont", ms::InputError::ScalarNotInFont,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'\U0001F642'}));

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'3'}).has_value(),
                                    "one more digit fills the field to max_length 3");
                      refusedUnchanged("the field is now full: FieldAtCapacity", ms::InputError::FieldAtCapacity,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::InsertScalar, .scalar = U'4'}));

                      checks.expect(editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 0}).has_value(), "caret home");
                      refusedUnchanged("backspace at the start is EmptyEdit", ms::InputError::EmptyEdit,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::DeleteBack}));
                      refusedUnchanged("a caret past the length is CaretOutOfRange", ms::InputError::CaretOutOfRange,
                                       editor.apply(ms::EditOp{.kind = ms::EditKind::MoveCaret, .caretTo = 99}));

                      const auto v = editor.value();
                      checks.expect(std::u32string_view{v.data(), v.size()} == U"123", "the value is still the one create() accepted");
                      checks.expect(editor.caret() == std::optional<std::size_t>{0}, "the caret is where the one accepted move left it");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register editorRoutesKeysAndTextAndIgnoresOtherNodes{
    "handleKey routes edits, reports a non-edit key as not-consumed, and ignores a focus for another node",
    "evidence-unit",
    [] {
        return speclab::Test("medui-input-editor-routes-keys-and-text")
            .Given("an editor for the node \"name\"", [] {})
            .When("a focus for \"other\", a focus for \"name\", a text event, and Commit / CaretLeft keys are routed", [] {})
            .Then("the wrong-node focus is ignored, text inserts, Commit is not an edit, and CaretLeft moves the caret",
                  [] {
                      mdux::spec::Checks       checks;
                      std::array<char32_t, 16> storage{};

                      auto made = ms::FieldEditor::create("name", storage, {}, fixture::asciiFont, {}, 8);
                      if (!made.has_value()) { checks.expect(false, "created"); checks.raise(); return; }
                      ms::FieldEditor editor = *made;

                      checks.expect(!editor.focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = "other"}),
                                    "a focus event for another node is not taken");
                      checks.expect(!editor.editing(), "and this editor is still not editing");

                      checks.expect(!editor.focus(ms::FocusEvent{.nodeId = "name"}),
                                    "a kindless focus event for this node is not taken either — Unspecified is never delivered");
                      checks.expect(!editor.editing(), "and it did not start editing");

                      checks.expect(editor.focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = "name"}),
                                    "a focus event for this node is taken");

                      const auto typed = editor.handleText(ms::TextEvent{.scalar = U'A'});
                      checks.expect(typed.has_value() && *typed, "a text event inserts and reports consumed");

                      const auto commit = editor.handleKey(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::Commit});
                      checks.expect(commit.has_value() && !*commit, "Commit is not an edit — the caller handles it, and it is not an error");

                      const auto left = editor.handleKey(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::CaretLeft});
                      checks.expect(left.has_value() && *left, "CaretLeft is a consumed edit");
                      checks.expect(editor.caret() == std::optional<std::size_t>{0}, "and it moved the caret to the front");

                      const auto keyUp = editor.handleKey(ms::KeyEvent{.kind = ms::KeyKind::Up, .key = ms::KeyCode::CaretRight});
                      checks.expect(keyUp.has_value() && !*keyUp, "a key-up is not an edit");

                      const auto v = editor.value();
                      checks.expect(std::u32string_view{v.data(), v.size()} == U"A", "only the typed scalar is in the value");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
