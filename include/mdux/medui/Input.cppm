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
 * ## What is here
 *
 * - clause 1: the closed event vocabulary and its wire spellings;
 * - clause 2: `maxInputEvents` and the caller-owned bounded `EventQueue` (#316);
 * - clause 3: the pure `normalizeSurfacePoint()`, fail-closed on overflow, and `SurfaceMapping` —
 *   the value an adapter (#317) recomputes on every resize/scale change and asks each window-space
 *   pointer coordinate through, so hit testing always matches what was last rendered;
 * - clause 4: the pure `PressLatch` state machine;
 * - clause 5: the `EditOp` vocabulary, the pure `editWouldBeAccepted()` predicate, and the
 *   stateful `FieldEditor` (#316) — the realization of ADR-018's `applyEdit()` over caller storage;
 * - clause 7: the `ActionTrace` value.
 *
 * All of it is `inline`, allocation-free and `noexcept`, and all but `FieldEditor::create()`
 * (which does one overlap-safe `std::memmove`) is `constexpr`, so the module is header-only like
 * `mdux.medui.schema`. The `EventQueue` and `FieldEditor` state lives in caller-owned spans; the
 * types themselves are small value objects.
 *
 * ## Why this does not import `mdux.medui.screen`
 *
 * `PressLatch` arms a bare node-id `string_view`, and a caller wires `resolvePress()` (which lives
 * in `mdux.medui.screen`) to it. Keeping the dependency that direction only — screen may import
 * input later, input never imports screen — keeps the module graph acyclic. The cost is that the
 * `string_view` a latch holds is into the caller's screen storage: a caller that rebuilds that
 * storage must `cancel()` across the rebuild, which the latch cannot enforce and a test covers.
 *
 * For the same reason `FieldEditor` does not build a `TextInputSlot` (that type is
 * `mdux.medui.screen`'s). It exposes `nodeId()`, `value()` and `caret()`, and a caller assembles
 * the slot per frame:
 *
 *     const mdux::medui::TextInputSlot slot{
 *         .nodeId = editor.nodeId(), .text = editor.value(), .caret = editor.caret()};
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
// Clause 2 — the bounded event queue
// ===========================================================================

/// The default event-queue capacity. `maxFieldCells`'s counterpart, chosen the same way: large
/// enough for the input an operator produces between two frames of a medical display, small enough
/// that a reviewer can multiply it by `sizeof(InputEvent)` in their head. A caller may size its own
/// storage.
inline constexpr std::size_t maxInputEvents = 64;

/// Why a pure input operation was refused. Every one leaves the caller's value and caret exactly as
/// they were found — there is no partial mutation (clause 5).
enum class InputError : std::uint8_t {
    MalformedScale,        ///< a coordinate scale term that is non-positive or past `maxCoordinateScale`
    CoordinateOutOfRange,  ///< a normalized coordinate that does not fit in `core::Px`
    NotFocused,            ///< an edit offered to a field that is not being edited (`caret()` is `nullopt`)
    MalformedEditOp,       ///< an `EditOp` whose `kind` is `EditKind::Unspecified`
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
        case InputError::NotFocused:           return "the field is not being edited";
        case InputError::MalformedEditOp:      return "the edit operation has no kind";
        case InputError::CaretOutOfRange:      return "the caret is outside [0, length]";
        case InputError::ScalarNotInFont:      return "the font package has no glyph for the scalar";
        case InputError::ScalarNotPermitted:   return "the scalar is outside the node's charset";
        case InputError::FieldAtCapacity:      return "the field already holds max_length scalars";
        case InputError::EmptyEdit:            return "the delete has nothing to remove";
    }
    return {};
}

/// The outcome of offering an event to an `EventQueue`.
enum class PushOutcome : std::uint8_t {
    Accepted,       ///< the event was stored
    DroppedNewest,  ///< the queue was full; this event was discarded and `droppedCount()` advanced
};

/**
 * @brief A caller-owned bounded FIFO of input events (ADR-018 clause 2).
 *
 * The platform adapter `push()`es events in; one application update `pop()`s them out in order
 * until `empty()`. `storage` is a span the caller allocated once — a `std::array<InputEvent, N>`
 * on a device — and the queue never grows it, never copies it elsewhere, and allocates nothing.
 * It is a ring: `head_` and `count_` index into `storage`, wrapping.
 *
 * On overflow the **newest** event is dropped, not the oldest: the oldest events are the ones the
 * application has most likely already acted on (an arm, a caret move), so discarding them would
 * desynchronise state from what the operator last saw. `droppedCount()` saturates rather than
 * wrapping. A `push()` returning `DroppedNewest` is the signal to `PressLatch::cancel()` any armed
 * press — the event stream is no longer a complete record.
 */
class EventQueue {
public:
    /// A queue over `storage`. An empty span is a zero-capacity queue: every `push()` drops.
    constexpr explicit EventQueue(std::span<InputEvent> storage) noexcept : storage_{storage} {}

    /// Offers `event`. Stored (`Accepted`) unless the queue is `full()`, in which case `event` is
    /// discarded, `droppedCount()` advances (saturating), and `DroppedNewest` is returned.
    [[nodiscard]] constexpr PushOutcome push(const InputEvent& event) noexcept {
        if (count_ >= storage_.size()) {
            if (droppedCount_ != std::numeric_limits<std::uint32_t>::max()) {
                ++droppedCount_;
            }
            return PushOutcome::DroppedNewest;
        }
        storage_[(head_ + count_) % storage_.size()] = event;
        ++count_;
        return PushOutcome::Accepted;
    }

    /// The oldest queued event, or `nullopt` when `empty()`.
    [[nodiscard]] constexpr std::optional<InputEvent> pop() noexcept {
        if (count_ == 0) {
            return std::nullopt;
        }
        const InputEvent event = storage_[head_];
        head_ = (head_ + 1) % storage_.size();
        --count_;
        return event;
    }

    /// Discards every queued event without returning them — for a caller that has decided the batch
    /// is void, e.g. the screen was replaced under it. Does not reset `droppedCount()`.
    constexpr void clear() noexcept {
        head_  = 0;
        count_ = 0;
    }

    [[nodiscard]] constexpr std::size_t   size() const noexcept { return count_; }
    [[nodiscard]] constexpr std::size_t   capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] constexpr bool          empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr bool          full() const noexcept { return count_ >= storage_.size(); }
    [[nodiscard]] constexpr std::uint32_t droppedCount() const noexcept { return droppedCount_; }

private:
    std::span<InputEvent> storage_{};
    std::size_t           head_{0};
    std::size_t           count_{0};
    std::uint32_t         droppedCount_{0};
};

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

/**
 * @brief The window→authored coordinate transform an adapter keeps for one presented frame
 *        (ADR-018 clause 3, ADR-019).
 *
 * `normalizeSurfacePoint()` is the arithmetic; this is the state an adapter recomputes on every
 * framebuffer-resize and content-scale change so a pointer is hit-tested against the geometry the
 * operator last saw. It holds a uniform authored-per-window-pixel ratio (`scaleNum / scaleDen`,
 * each in `[1, maxCoordinateScale]`) and, optionally, a window-space origin.
 *
 * The presented renderer draws the authored surface at 1:1 into the framebuffer origin
 * (`mdux::render::UiRenderer`), so authored pixels and framebuffer pixels coincide and the only
 * scaling is the display's **device-pixel ratio** — framebuffer pixels per window pixel. A pointer
 * arrives in window (screen) coordinates; `map()` takes it to the authored grid. A point off the
 * authored surface is not this type's concern — `resolvePress()` returns nothing for it.
 *
 * The adapter `PressLatch::cancel()`s across every rebuild (ADR-018 clause 4 lists a screen-storage
 * rebuild and a queue overflow as disarm points; ADR-019 adds the surface rebuild). Governed code
 * downstream only ever sees integer authored `core::Px`.
 *
 * Pure, `constexpr`, `noexcept`, allocation-free — like the rest of this module.
 */
struct SurfaceMapping {
    std::int32_t  scaleNum{1};  ///< authored pixels per window pixel, numerator (the device-pixel ratio)
    std::int32_t  scaleDen{1};  ///< authored pixels per window pixel, denominator
    mdux::core::Px originX{};   ///< window x of the authored origin, normally 0
    mdux::core::Px originY{};   ///< window y of the authored origin

    /// The identity: authored pixels are window pixels, no scaling.
    [[nodiscard]] static constexpr SurfaceMapping identity() noexcept { return SurfaceMapping{}; }

    /**
     * @brief The mapping for a 1:1 framebuffer-origin renderer at the current device-pixel ratio.
     *
     * `framebuffer` is `glfwGetFramebufferSize`, `window` is the client-area size in screen
     * coordinates. The ratio is taken from the width and the height is required to reduce to the
     * same fraction — a display's device-pixel ratio is uniform, and an anamorphic or fractional
     * ratio is out of initial adapter scope (ADR-019), so this **fails closed** (`MalformedScale`)
     * rather than picking one axis. Also `MalformedScale` for a non-positive extent or a reduced
     * term past `maxCoordinateScale`.
     */
    [[nodiscard]] static constexpr mdux::core::Result<SurfaceMapping, InputError>
    create(mdux::core::Extent2D framebuffer, mdux::core::Extent2D window) noexcept {
        if (framebuffer.width <= 0 || framebuffer.height <= 0 || window.width <= 0 || window.height <= 0) {
            return mdux::core::err(InputError::MalformedScale);
        }
        std::int32_t       num = framebuffer.width;
        std::int32_t       den = window.width;
        const std::int32_t g   = std::gcd(num, den);
        num /= g;
        den /= g;
        if (num > maxCoordinateScale || den > maxCoordinateScale) {
            return mdux::core::err(InputError::MalformedScale);
        }
        // The height must carry the same ratio: framebuffer.h / window.h == num / den.
        if (static_cast<std::int64_t>(framebuffer.height) * den !=
            static_cast<std::int64_t>(window.height) * num) {
            return mdux::core::err(InputError::MalformedScale);
        }
        return SurfaceMapping{.scaleNum = num, .scaleDen = den, .originX = 0, .originY = 0};
    }

    /// Maps one window-space pointer coordinate to authored surface pixels, fail-closed
    /// (`CoordinateOutOfRange` for a point whose authored coordinate does not fit `core::Px`).
    [[nodiscard]] constexpr mdux::core::Result<SurfacePoint, InputError>
    toSurface(mdux::core::Px windowX, mdux::core::Px windowY) const noexcept {
        return normalizeSurfacePoint(windowX, windowY, scaleNum, scaleDen, originX, originY);
    }

    /// The `PointerEvent` of `kind` at window coordinate `(windowX, windowY)` — the one call an
    /// adapter makes per pointer event before it reaches the queue.
    [[nodiscard]] constexpr mdux::core::Result<PointerEvent, InputError>
    map(PointerKind kind, mdux::core::Px windowX, mdux::core::Px windowY) const noexcept {
        const auto surface = toSurface(windowX, windowY);
        if (!surface) {
            return mdux::core::err(surface.error());
        }
        return PointerEvent{.kind = kind, .x = surface->x, .y = surface->y};
    }
};

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

/**
 * @brief The controlled editing state of one `TextInput`, over caller-owned scalar storage
 *        (ADR-018 clause 5) — the realization of the ADR's `applyEdit()`.
 *
 * Holds a view of a `char32_t` buffer the caller allocated once (at least `maxLength` long), the
 * current length, and the caret. Every mutation is bounded by the two charset questions
 * `editWouldBeAccepted()` asks and by `max_length`, all checked before anything moves, so a
 * refused edit leaves the value and caret exactly as they were — no partial mutation. Nothing is
 * allocated: an insert or delete shifts scalars inside the caller's own buffer.
 *
 * `caret()` is `nullopt` when the field is not being edited (a fresh editor, or `focus()` with
 * `FocusKind::Leave`), which is exactly `TextInputSlot::caret`'s "draws no caret" state. The
 * editor holds no `TextInputSlot` — see the module header for the one-line assembly a caller does.
 *
 * The `fontCharset` / `nodeCharset` spans are the caller's storage (the bound `FontPackage`'s
 * `restrictedCharset` and the node's `charsetRanges`); the editor keeps views, not copies.
 */
class FieldEditor {
public:
    /**
     * @brief A fresh editor over `storage` for `nodeId`, holding `initial`, not yet being edited.
     *
     * Refused when `maxLength` exceeds `storage.size()` or `maxFieldCells`, when `initial` is
     * longer than `maxLength`, or when `initial` carries a scalar the font or node charset
     * excludes. **A refused `create()` never writes `storage`** — the whole of `initial` is
     * validated before anything is copied — so a caller's existing buffer contents survive a
     * rejection intact. On success `initial` is copied into `storage` (overlap-safe: `initial`
     * may legitimately be a view of `storage` the caller is adopting in place) and `caret()` is
     * `nullopt`. Not `constexpr` for the same reason `TextInputBinding::create()` is not — the
     * copy is an overlap-safe `std::memmove`.
     */
    [[nodiscard]] static mdux::core::Result<FieldEditor, InputError>
    create(std::string_view nodeId, std::span<char32_t> storage, std::span<const char32_t> initial,
           std::span<const mdux::font::CharsetRange> fontCharset,
           std::span<const mdux::font::CharsetRange> nodeCharset, std::size_t maxLength) noexcept {
        if (maxLength > storage.size() || maxLength > maxFieldCells) {
            return mdux::core::err(InputError::FieldAtCapacity);
        }
        if (initial.size() > maxLength) {
            return mdux::core::err(InputError::FieldAtCapacity);
        }
        // Validate the whole value first, writing nothing: a rejected create() must leave the
        // caller's buffer exactly as it found it.
        for (const char32_t scalar : initial) {
            // Charset only — capacity is `initial.size() <= maxLength` by the check above, so ask
            // against a fresh field so `editWouldBeAccepted` never answers `FieldAtCapacity` here.
            if (auto ok = editWouldBeAccepted(scalar, fontCharset, nodeCharset, 0, maxLength); !ok) {
                return mdux::core::err(ok.error());
            }
        }
        // Overlap-safe: `initial` and `storage` may alias (a caller adopting a value already at
        // the front of its buffer, or a sub-view of it). `std::memmove` is correct for every
        // overlap; a forward element copy would read scalars it had already overwritten.
        if (!initial.empty()) {
            std::memmove(storage.data(), initial.data(), initial.size() * sizeof(char32_t));
        }
        return FieldEditor{nodeId, storage, initial.size(), fontCharset, nodeCharset, maxLength};
    }

    [[nodiscard]] constexpr std::string_view           nodeId() const noexcept { return nodeId_; }
    [[nodiscard]] constexpr std::span<const char32_t>   value() const noexcept { return storage_.first(length_); }
    [[nodiscard]] constexpr std::optional<std::size_t>  caret() const noexcept { return caret_; }
    [[nodiscard]] constexpr std::size_t                 length() const noexcept { return length_; }
    [[nodiscard]] constexpr std::size_t                 maxLength() const noexcept { return maxLength_; }
    [[nodiscard]] constexpr bool                        editing() const noexcept { return caret_.has_value(); }

    /**
     * @brief A focus transition. `FocusKind::Enter` starts editing with the caret after the last
     *        scalar, `Leave` ends it (`caret()` -> `nullopt`). Returns whether this editor changed
     *        state for the event: `false` for an event naming another node and `false` for a
     *        kindless (`FocusKind::Unspecified`) event, which is never a delivered one (clause 1) —
     *        the same fail-closed reading `apply()` gives an `EditKind::Unspecified` op.
     */
    constexpr bool focus(const FocusEvent& event) noexcept {
        if (event.nodeId != nodeId_) {
            return false;
        }
        if (event.kind == FocusKind::Enter) {
            caret_ = length_;
            return true;
        }
        if (event.kind == FocusKind::Leave) {
            caret_ = std::nullopt;
            return true;
        }
        return false;
    }

    /**
     * @brief Applies one edit, or refuses it without mutating anything.
     *
     * `NotFocused` when the field is not being edited; `MalformedEditOp` for an `Unspecified`
     * `EditOp`; `InsertScalar` is bounded by `editWouldBeAccepted()`; `DeleteBack` / `DeleteForward`
     * are `EmptyEdit` at their boundary; `MoveCaret` is `CaretOutOfRange` past `length()`.
     */
    [[nodiscard]] constexpr mdux::core::Result<void, InputError> apply(const EditOp& op) noexcept {
        if (!caret_.has_value()) {
            return mdux::core::err(InputError::NotFocused);
        }
        const std::size_t caret = *caret_;
        switch (op.kind) {
            case EditKind::InsertScalar: {
                if (auto ok = editWouldBeAccepted(op.scalar, fontCharset_, nodeCharset_, length_, maxLength_); !ok) {
                    return mdux::core::err(ok.error());
                }
                for (std::size_t i = length_; i > caret; --i) {
                    storage_[i] = storage_[i - 1];
                }
                storage_[caret] = op.scalar;
                ++length_;
                caret_ = caret + 1;
                return {};
            }
            case EditKind::DeleteBack: {
                if (caret == 0) {
                    return mdux::core::err(InputError::EmptyEdit);
                }
                for (std::size_t i = caret - 1; i + 1 < length_; ++i) {
                    storage_[i] = storage_[i + 1];
                }
                --length_;
                caret_ = caret - 1;
                return {};
            }
            case EditKind::DeleteForward: {
                if (caret >= length_) {
                    return mdux::core::err(InputError::EmptyEdit);
                }
                for (std::size_t i = caret; i + 1 < length_; ++i) {
                    storage_[i] = storage_[i + 1];
                }
                --length_;
                return {};
            }
            case EditKind::MoveCaret: {
                if (op.caretTo > length_) {
                    return mdux::core::err(InputError::CaretOutOfRange);
                }
                caret_ = op.caretTo;
                return {};
            }
            case EditKind::Unspecified:
                return mdux::core::err(InputError::MalformedEditOp);
        }
        return mdux::core::err(InputError::MalformedEditOp);
    }

    /**
     * @brief Routes a key event to an edit. Returns `true` when it was an edit this field consumed,
     *        `false` for a key that is not an edit (`Commit`, `Cancel`, focus traversal — the
     *        caller's to act on), a key-up, or an unfocused field. An error is a refused edit.
     */
    [[nodiscard]] constexpr mdux::core::Result<bool, InputError> handleKey(const KeyEvent& event) noexcept {
        if (event.kind != KeyKind::Down || !caret_.has_value()) {
            return false;
        }
        const std::size_t caret = *caret_;
        EditOp            op{};
        switch (event.key) {
            case KeyCode::DeleteBack:    op = {.kind = EditKind::DeleteBack}; break;
            case KeyCode::DeleteForward: op = {.kind = EditKind::DeleteForward}; break;
            case KeyCode::CaretLeft:     op = {.kind = EditKind::MoveCaret, .caretTo = caret == 0 ? 0 : caret - 1}; break;
            case KeyCode::CaretRight:    op = {.kind = EditKind::MoveCaret, .caretTo = caret + 1 > length_ ? length_ : caret + 1}; break;
            case KeyCode::CaretHome:     op = {.kind = EditKind::MoveCaret, .caretTo = 0}; break;
            case KeyCode::CaretEnd:      op = {.kind = EditKind::MoveCaret, .caretTo = length_}; break;
            case KeyCode::Commit:
            case KeyCode::Cancel:
            case KeyCode::FocusNext:
            case KeyCode::FocusPrev:
            case KeyCode::Unspecified:
                return false;
        }
        if (auto r = apply(op); !r) {
            return mdux::core::err(r.error());
        }
        return true;
    }

    /// Inserts `event.scalar` at the caret when focused; `false` otherwise, an error when refused.
    [[nodiscard]] constexpr mdux::core::Result<bool, InputError> handleText(const TextEvent& event) noexcept {
        if (!caret_.has_value()) {
            return false;
        }
        if (auto r = apply(EditOp{.kind = EditKind::InsertScalar, .scalar = event.scalar}); !r) {
            return mdux::core::err(r.error());
        }
        return true;
    }

private:
    constexpr FieldEditor(std::string_view nodeId, std::span<char32_t> storage, std::size_t length,
                          std::span<const mdux::font::CharsetRange> fontCharset,
                          std::span<const mdux::font::CharsetRange> nodeCharset, std::size_t maxLength) noexcept
        : nodeId_{nodeId}, storage_{storage}, fontCharset_{fontCharset}, nodeCharset_{nodeCharset},
          length_{length}, maxLength_{maxLength} {}

    std::string_view                         nodeId_{};
    std::span<char32_t>                       storage_{};
    std::span<const mdux::font::CharsetRange> fontCharset_{};
    std::span<const mdux::font::CharsetRange> nodeCharset_{};
    std::size_t                              length_{0};
    std::size_t                              maxLength_{0};
    std::optional<std::size_t>               caret_{};
};

// The guarantee `create()` is documented to give, held by the language rather than by discipline —
// `TextInputBinding`'s static_assert, for its reason.
static_assert(!std::is_aggregate_v<FieldEditor>, "a FieldEditor must only be obtainable through create()");

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
