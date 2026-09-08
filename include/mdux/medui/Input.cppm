/**
 * @file Input.cppm
 * @brief The bounded input vocabulary: pointer/key/text/focus events, the press latch, coordinate
 *        normalization, and the bounded-editing predicate.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-010 No on-device text shaping (a TextEvent carries a scalar the platform resolved)
 * @compliance ADR-011 The deterministic `.medui` compile boundary (runtime types behind it)
 * @compliance ADR-015 Versioned sibling observations (decision D5, the accepted local direction)
 * @compliance ADR-018 Bounded input, application update order and action policy
 *
 * This module is the one home for the types #316 (the bounded event queue and text-editing model),
 * #317 (the platform adapter) and #319/#320 (scenario replay) all share. It is defined by
 * [ADR-018](../../../docs/adr/ADR-018-bounded-input-and-update-order.md); the numbered clauses
 * below refer to that record.
 *
 * ## What is here, and what is #316's
 *
 * Here: the closed event vocabulary (clause 1), `maxInputEvents` (clause 2), the pure
 * `normalizeSurfacePoint()` (clause 3), the pure `PressLatch` state machine (clause 4), the
 * `EditOp` vocabulary and the pure `editWouldBeAccepted()` predicate (clause 5), and the
 * `ActionTrace` value (clause 7). All of it is `constexpr`, allocation-free and `noexcept`, so the
 * module is header-only like `mdux.medui.schema`.
 *
 * #316's: the ring-buffer `EventQueue` over caller storage, and `applyEdit()`, which produces the
 * next `(value, caret)` from an `EditOp` over caller-owned field storage. Their contracts are in
 * ADR-018 clauses 2 and 5; putting a half-implemented governed symbol here would trip
 * `governed.noThrow.symbolScan` and `screen.noheap.symbolScan` for no benefit.
 *
 * ## Why this does not import `mdux.medui.screen`
 *
 * `PressLatch` arms a bare node-id `string_view`, and a caller wires `resolvePress()` (which lives
 * in `mdux.medui.screen`) to it. Keeping the dependency that direction only — screen may import
 * input later, input never imports screen — keeps the module graph acyclic. The cost is that the
 * `string_view` a latch holds is into the caller's screen storage: a caller that rebuilds that
 * storage must `cancel()` across the rebuild, which the latch cannot enforce and a test covers.
 */
module;

export module mdux.medui.input;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.font.schema;
import mdux.medui.field;
import mdux.medui.schema;

export namespace mdux::medui {

// ===========================================================================
// Clause 1 — the closed event vocabulary
// ===========================================================================

/// A pointer transition or motion. `Cancel` is the platform telling us the gesture is void (a
/// system takeover, a window losing pointer capture), and it disarms a pending press.
enum class PointerKind : std::uint8_t {
    Unspecified,  ///< never valid in a delivered event; the aggregate default
    Down,
    Up,
    Move,
    Cancel,
};

/// A physical key transition. Text is a separate `TextEvent`; a `KeyEvent` is for the non-text
/// keys — caret motion, delete, focus traversal, commit and cancel.
enum class KeyKind : std::uint8_t {
    Unspecified,
    Down,
    Up,
};

/// The focused control changed. `Leave` for the armed control disarms a pending press (clause 4).
enum class FocusKind : std::uint8_t {
    Unspecified,
    Enter,
    Leave,
};

/// The non-text keys this contract starts with. Deliberately minimal and **additive**: a real
/// adapter (#317) extends this enum rather than reinterpreting a member. A character is never a
/// `KeyCode` — it arrives as a `TextEvent`.
enum class KeyCode : std::uint8_t {
    Unspecified,
    CaretLeft,
    CaretRight,
    CaretHome,
    CaretEnd,
    DeleteBack,     ///< the key; the edit it maps to is `EditKind::DeleteBack`
    DeleteForward,
    FocusNext,
    FocusPrev,
    Commit,         ///< Enter/Return as an action, not as a text scalar
    Cancel,         ///< Escape
};

/// One pointer event, already in authored surface coordinates (clause 3).
struct PointerEvent {
    PointerKind    kind{PointerKind::Unspecified};
    mdux::core::Px x{};
    mdux::core::Px y{};

    [[nodiscard]] constexpr bool operator==(const PointerEvent&) const noexcept = default;
};

/// One physical key transition.
struct KeyEvent {
    KeyKind kind{KeyKind::Unspecified};
    KeyCode key{KeyCode::Unspecified};

    [[nodiscard]] constexpr bool operator==(const KeyEvent&) const noexcept = default;
};

/// One committed Unicode scalar. The platform's keymap/IME already resolved it; MduX does no
/// mapping, no dead-key composition and no shaping (ADR-010).
struct TextEvent {
    char32_t scalar{};

    [[nodiscard]] constexpr bool operator==(const TextEvent&) const noexcept = default;
};

/// A focus transition. `nodeId` is a view into the caller's screen storage.
struct FocusEvent {
    FocusKind        kind{FocusKind::Unspecified};
    std::string_view nodeId{};

    [[nodiscard]] constexpr bool operator==(const FocusEvent&) const noexcept = default;
};

/// One entry in the bounded queue (clause 2). A variant, not a fat struct, so an "unspecified"
/// event is not representable: every alternative is a real event kind.
using InputEvent = std::variant<PointerEvent, KeyEvent, TextEvent, FocusEvent>;

// ---------------------------------------------------------------------------
// Contract spellings, under the same rule `toWire(SystemEvent)` follows: an out-of-set spelling,
// `""` included, yields no member, so a missing field is refused rather than read as the default.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::string_view toWire(PointerKind kind) noexcept {
    switch (kind) {
        case PointerKind::Down:   return "Down";
        case PointerKind::Up:     return "Up";
        case PointerKind::Move:   return "Move";
        case PointerKind::Cancel: return "Cancel";
        case PointerKind::Unspecified: return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<PointerKind> pointerKindFromWire(std::string_view wire) noexcept {
    if (wire == "Down")   { return PointerKind::Down; }
    if (wire == "Up")     { return PointerKind::Up; }
    if (wire == "Move")   { return PointerKind::Move; }
    if (wire == "Cancel") { return PointerKind::Cancel; }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view toWire(KeyKind kind) noexcept {
    switch (kind) {
        case KeyKind::Down: return "Down";
        case KeyKind::Up:   return "Up";
        case KeyKind::Unspecified: return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<KeyKind> keyKindFromWire(std::string_view wire) noexcept {
    if (wire == "Down") { return KeyKind::Down; }
    if (wire == "Up")   { return KeyKind::Up; }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view toWire(FocusKind kind) noexcept {
    switch (kind) {
        case FocusKind::Enter: return "Enter";
        case FocusKind::Leave: return "Leave";
        case FocusKind::Unspecified: return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<FocusKind> focusKindFromWire(std::string_view wire) noexcept {
    if (wire == "Enter") { return FocusKind::Enter; }
    if (wire == "Leave") { return FocusKind::Leave; }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view toWire(KeyCode code) noexcept {
    switch (code) {
        case KeyCode::CaretLeft:     return "CaretLeft";
        case KeyCode::CaretRight:    return "CaretRight";
        case KeyCode::CaretHome:     return "CaretHome";
        case KeyCode::CaretEnd:      return "CaretEnd";
        case KeyCode::DeleteBack:    return "DeleteBack";
        case KeyCode::DeleteForward: return "DeleteForward";
        case KeyCode::FocusNext:     return "FocusNext";
        case KeyCode::FocusPrev:     return "FocusPrev";
        case KeyCode::Commit:        return "Commit";
        case KeyCode::Cancel:        return "Cancel";
        case KeyCode::Unspecified:   return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<KeyCode> keyCodeFromWire(std::string_view wire) noexcept {
    if (wire == "CaretLeft")     { return KeyCode::CaretLeft; }
    if (wire == "CaretRight")    { return KeyCode::CaretRight; }
    if (wire == "CaretHome")     { return KeyCode::CaretHome; }
    if (wire == "CaretEnd")      { return KeyCode::CaretEnd; }
    if (wire == "DeleteBack")    { return KeyCode::DeleteBack; }
    if (wire == "DeleteForward") { return KeyCode::DeleteForward; }
    if (wire == "FocusNext")     { return KeyCode::FocusNext; }
    if (wire == "FocusPrev")     { return KeyCode::FocusPrev; }
    if (wire == "Commit")        { return KeyCode::Commit; }
    if (wire == "Cancel")        { return KeyCode::Cancel; }
    return std::nullopt;
}

// ===========================================================================
// Clause 2 — the bounded queue's default capacity
// ===========================================================================

/// The default event-queue capacity. `maxFieldCells`'s counterpart, chosen the same way: large
/// enough for the input an operator produces between two frames of a medical display, small enough
/// that a reviewer can multiply it by `sizeof(InputEvent)` in their head. A caller may size its own
/// storage; the ring-buffer `EventQueue` over it lands in #316.
inline constexpr std::size_t maxInputEvents = 64;

/// Why a pure input operation was refused. Every one leaves the caller's value and caret exactly as
/// they were found — there is no partial mutation (clause 5).
enum class InputError : std::uint8_t {
    MalformedScale,        ///< a coordinate scale term that is non-positive or past `maxCoordinateScale`
    CoordinateOutOfRange,  ///< a normalized coordinate that does not fit in `core::Px`
    CaretOutOfRange,       ///< a caret position outside `[0, length]`
    ScalarNotInFont,       ///< the font package has no glyph for the scalar — the physical limit
    ScalarNotPermitted,    ///< the font can draw it, but the node's `charset:` excludes it — the policy limit
    FieldAtCapacity,       ///< an insertion into a field already holding `max_length` scalars
    EmptyEdit,             ///< a delete with nothing on the side it would remove from
};

[[nodiscard]] constexpr std::string_view describe(InputError error) noexcept {
    switch (error) {
        case InputError::MalformedScale:       return "the coordinate scale is not strictly positive or is too large";
        case InputError::CoordinateOutOfRange: return "the normalized coordinate does not fit in a surface pixel";
        case InputError::CaretOutOfRange:      return "the caret is outside [0, length]";
        case InputError::ScalarNotInFont:      return "the font package has no glyph for the scalar";
        case InputError::ScalarNotPermitted:   return "the scalar is outside the node's charset";
        case InputError::FieldAtCapacity:      return "the field already holds max_length scalars";
        case InputError::EmptyEdit:            return "the delete has nothing to remove";
    }
    return {};
}

// ===========================================================================
// Clause 3 — coordinate normalization (floor toward -inf)
// ===========================================================================

/// A point in authored surface coordinates — the space `NodeRect` and `resolvePress()` resolve in.
struct SurfacePoint {
    mdux::core::Px x{};
    mdux::core::Px y{};

    [[nodiscard]] constexpr bool operator==(const SurfacePoint&) const noexcept = default;
};

/// The largest scale-ratio term `normalizeSurfacePoint()` accepts, each way. 4096 is far past any
/// real physical-to-authored ratio, and capping it keeps every intermediate `normalizeSurfacePoint()`
/// computes inside `std::int64_t`: the `core::Px` inputs bound `value` to ±2³³, and `value * num`
/// then stays under 2⁴⁵.
inline constexpr std::int32_t maxCoordinateScale = 4096;

/// `floor(value * num / den)` toward -inf, in 64-bit. The caller of `normalizeSurfacePoint()`
/// guarantees `num`/`den` in `[1, maxCoordinateScale]` and `value` in `core::Px` range, so
/// `value * num` cannot overflow. Split out so the flooring is tested directly.
[[nodiscard]] constexpr std::int64_t floorScaled(std::int64_t value, std::int64_t num, std::int64_t den) noexcept {
    const std::int64_t scaled   = value * num;
    const std::int64_t quotient = scaled / den;
    const std::int64_t rem      = scaled % den;
    // Trunc-toward-zero gives the wrong answer for a negative dividend; step down one when it did.
    return (rem != 0 && (scaled < 0)) ? quotient - 1 : quotient;
}

/**
 * @brief Maps a physical device-pixel coordinate to authored surface pixels, once.
 *
 * ADR-018 clause 3: the platform adapter is the only place this happens, and governed code never
 * sees a physical coordinate, a scale factor or a sub-pixel value. The rule is **floor toward
 * -inf**, which is the one that agrees with the half-open `[x, x + w)` rectangle
 * `core::Rect::contains` and `resolvePress()` already use — a truncating cast rounds toward zero
 * and would map two physical rows onto authored row 0 once a control sits at a negative authored
 * coordinate.
 *
 * Fails closed: a non-positive or oversized scale term is `MalformedScale`, and a normalized
 * coordinate that does not fit in `core::Px` is `CoordinateOutOfRange` rather than a value silently
 * wrapped by the narrowing cast — a wrapped coordinate could land on a real control.
 *
 * @param physX,physY   the physical coordinate, in device pixels
 * @param scaleNum,scaleDen  authored pixels per physical pixel, as a ratio; each must be in
 *                           `[1, maxCoordinateScale]`
 * @param originX,originY  the physical coordinate of the surface's authored origin
 */
[[nodiscard]] constexpr mdux::core::Result<SurfacePoint, InputError>
normalizeSurfacePoint(mdux::core::Px physX, mdux::core::Px physY, std::int32_t scaleNum, std::int32_t scaleDen,
                      mdux::core::Px originX, mdux::core::Px originY) noexcept {
    if (scaleNum <= 0 || scaleDen <= 0 || scaleNum > maxCoordinateScale || scaleDen > maxCoordinateScale) {
        return mdux::core::err(InputError::MalformedScale);
    }
    const std::int64_t nx = floorScaled(static_cast<std::int64_t>(physX) - originX, scaleNum, scaleDen);
    const std::int64_t ny = floorScaled(static_cast<std::int64_t>(physY) - originY, scaleNum, scaleDen);
    constexpr std::int64_t pxMin = std::numeric_limits<mdux::core::Px>::min();
    constexpr std::int64_t pxMax = std::numeric_limits<mdux::core::Px>::max();
    if (nx < pxMin || nx > pxMax || ny < pxMin || ny > pxMax) {
        return mdux::core::err(InputError::CoordinateOutOfRange);
    }
    return SurfacePoint{.x = static_cast<mdux::core::Px>(nx), .y = static_cast<mdux::core::Px>(ny)};
}

// ===========================================================================
// Clause 4 — press arms a target; release activates only the same target
// ===========================================================================

/**
 * @brief The one-slot press state machine: at most one armed node id at a time.
 *
 * A `PointerKind::Down` that `resolvePress()` resolved to a control arms that control's node id. A
 * `PointerKind::Up` activates — the caller acts on the `PressAction` — only if it resolves to the
 * same armed id. `Cancel`, a `FocusKind::Leave` for the armed control, the armed control leaving
 * the screen, and a queue overflow all disarm (`cancel()`); a release after a disarm is a no-op,
 * not an error.
 *
 * Pure and `constexpr`: it holds a `string_view` and a flag and nothing else. Half-open
 * rectangles, reverse paint order and every-node occlusion are `resolvePress()`'s rules and are
 * not restated here.
 */
class PressLatch {
public:
    constexpr PressLatch() noexcept = default;

    /// Arms `nodeId`. A previous arm is replaced. An empty id is not a valid control and is
    /// treated as "arm nothing".
    constexpr void arm(std::string_view nodeId) noexcept {
        armed_    = nodeId;
        hasArmed_ = !nodeId.empty();
    }

    /// Clears any pending target: cancellation, focus loss, a screen-storage rebuild, overflow.
    constexpr void cancel() noexcept {
        armed_    = {};
        hasArmed_ = false;
    }

    [[nodiscard]] constexpr bool             isArmed() const noexcept { return hasArmed_; }
    [[nodiscard]] constexpr std::string_view armedNode() const noexcept { return armed_; }

    /// A release on `nodeId`. Returns `true` (activate) iff a target is armed and `nodeId` is the
    /// same non-empty id; `false` otherwise (nothing armed, or a different target). The latch is
    /// disarmed either way.
    [[nodiscard]] constexpr bool release(std::string_view nodeId) noexcept {
        const bool activate = hasArmed_ && !nodeId.empty() && nodeId == armed_;
        cancel();
        return activate;
    }

private:
    std::string_view armed_{};
    bool             hasArmed_{false};
};

// ===========================================================================
// Clause 5 — bounded, scalar-indexed keyboard editing
// ===========================================================================

/// The edit vocabulary. `applyEdit()` — which produces the next `(value, caret)` over caller
/// storage — lands in #316 against this type; `editWouldBeAccepted()` below is the pure predicate
/// #316 and the caller share.
enum class EditKind : std::uint8_t {
    Unspecified,
    InsertScalar,   ///< insert `scalar` at the caret, then advance the caret
    DeleteBack,     ///< remove the scalar before the caret
    DeleteForward,  ///< remove the scalar after the caret
    MoveCaret,      ///< set the caret to `caretTo`
};

/// One edit. `scalar` is used only by `InsertScalar`; `caretTo` only by `MoveCaret`.
struct EditOp {
    EditKind    kind{EditKind::Unspecified};
    char32_t    scalar{};
    std::size_t caretTo{};

    [[nodiscard]] constexpr bool operator==(const EditOp&) const noexcept = default;
};

[[nodiscard]] constexpr std::string_view toWire(EditKind kind) noexcept {
    switch (kind) {
        case EditKind::InsertScalar:  return "InsertScalar";
        case EditKind::DeleteBack:    return "DeleteBack";
        case EditKind::DeleteForward: return "DeleteForward";
        case EditKind::MoveCaret:     return "MoveCaret";
        case EditKind::Unspecified:   return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<EditKind> editKindFromWire(std::string_view wire) noexcept {
    if (wire == "InsertScalar")  { return EditKind::InsertScalar; }
    if (wire == "DeleteBack")    { return EditKind::DeleteBack; }
    if (wire == "DeleteForward") { return EditKind::DeleteForward; }
    if (wire == "MoveCaret")     { return EditKind::MoveCaret; }
    return std::nullopt;
}

/**
 * @brief Whether inserting `scalar` into a field is allowed — capacity, then the same two charset
 *        bounds `mdux.medui.field` applies, no mutation.
 *
 * ADR-018 clause 5. `mdux.medui.field` asks two charset questions in one order and this asks the
 * same two, so a scalar this accepts is one `recordField()` will also draw rather than reject with
 * `GlyphNotInPackage` after the caller has already committed the edit:
 *
 * 1. **`fontCharset`** — the font package's `restrictedCharset`, the set it *can draw*. A scalar
 *    outside it is `ScalarNotInFont` (the physical limit; there is no glyph and ADR-010 leaves no
 *    fallback). An empty `fontCharset` is a malformed package and refuses everything.
 * 2. **`nodeCharset`** — the node's narrowed `charsetRanges`, via `mdux::medui::admits()` (#297),
 *    reused rather than reimplemented: empty means the node narrows nothing and every scalar the
 *    font can draw is in policy. A scalar the font draws but this set excludes is
 *    `ScalarNotPermitted` (the policy limit — a different fact, so a different error).
 *
 * Capacity is checked first, against the node's `max_length`. A rejected edit produces no partial
 * mutation, because this function mutates nothing: the caller holds the value and caret and does
 * not touch them when this returns an error.
 *
 * @param scalar         the candidate Unicode scalar
 * @param fontCharset    the bound font package's `restrictedCharset`
 * @param nodeCharset    the node's `charsetRanges`, or empty for a node that narrows nothing
 * @param currentLength  how many scalars the field currently holds
 * @param maxLength      the node's `max_length`
 */
[[nodiscard]] constexpr mdux::core::Result<void, InputError>
editWouldBeAccepted(char32_t scalar, std::span<const mdux::font::CharsetRange> fontCharset,
                    std::span<const mdux::font::CharsetRange> nodeCharset, std::size_t currentLength,
                    std::size_t maxLength) noexcept {
    if (currentLength >= maxLength) {
        return mdux::core::err(InputError::FieldAtCapacity);
    }
    // The font's set is asked first — the physical limit, `mdux.medui.field`'s ordering. `admits()`
    // reads an empty set as "narrows nothing", which is wrong for a font, so refuse that here.
    if (fontCharset.empty() || !admits(fontCharset, scalar)) {
        return mdux::core::err(InputError::ScalarNotInFont);
    }
    if (!admits(nodeCharset, scalar)) {
        return mdux::core::err(InputError::ScalarNotPermitted);
    }
    return {};
}

// ===========================================================================
// Clause 7 — MduX resolves and traces a critical action; the host executes it
// ===========================================================================

/**
 * @brief The ordered, traceable record of a critical press a host is about to act on.
 *
 * `resolvePress()` already yields `{nodeId, requirement, event}` and already fails closed on an
 * unimplemented or untraced critical control. This adds `sequence` — a monotonic counter the
 * caller advances — so a host holds a critical press in order. MduX asserts nothing about what
 * `SystemEvent::TriggerHalt` does to a device; the host executes it and owns the orderly-stop
 * behavior, its timing and its audit persistence (ADR-018 clause 7).
 */
struct ActionTrace {
    std::string_view nodeId{};
    std::string_view requirement{};
    SystemEvent      event{SystemEvent::Unspecified};
    std::uint64_t    sequence{};

    [[nodiscard]] constexpr bool operator==(const ActionTrace&) const noexcept = default;
};

}  // namespace mdux::medui
