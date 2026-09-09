/**
 * @file Scenario.cppm
 * @brief The bounded interaction-scenario data a replay consumes (#319, ADR-020).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-011 The deterministic `.medui` compile boundary (a scenario compiles the same way)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * Part of MduXCore. A `.scenario` script is compiled on the host by `mdux-scenariobake` into a
 * committed `generated/scenario/<id>/scenario.json`, and `mdux-scenarioemit` renders that into a
 * `constexpr` `CompiledScenario` a device build can hold by name - exactly the arrangement a
 * `.medui` screen has (ADR-012). This module is the runtime side of that boundary: the immutable
 * data types, their bounds, and `validate()`. The **replay** of a scenario through an application's
 * event/update path is `ScenarioRunner` (#320), added to this module without changing anything
 * here.
 *
 * ## Implementation-local, like the input contract
 *
 * MedUI #16 / MEDUI-DEC-008 name interaction *profiles* but deliver no schema and no corpus, so a
 * scenario carries no `medui-conformance.toml` key and every type here begins implementation-local
 * - the same stance [ADR-018](../../../docs/adr/ADR-018-bounded-input-and-update-order.md) took for
 * the event vocabulary and [ADR-016](../../../docs/adr/ADR-016-locally-versioned-observation-profiles.md)
 * for the rendered checks. A later reviewed migration maps these onto the canonical set if it is
 * ever delivered.
 *
 * ## The step model
 *
 * A scenario is an ordered list of `ScenarioStep`s. `Pointer` / `Key` / `Text` / `Focus` steps
 * queue one platform event; an `Advance` step consumes the events queued since the previous
 * `Advance` as **one accepted batch** and runs that many application updates (ADR-018 clause 6);
 * an `Expect` step checks one typed fact about the state settled by the preceding `Advance`; a
 * `Capture` step names a frame the replay must hand to its capture callback. Everything is
 * caller-owned and bounded.
 */
module;

export module mdux.medui.scenario;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;

export namespace mdux::medui {

// ===========================================================================
// Schema version and bounds
// ===========================================================================

/// The `schemaVersion` a `CompiledScenario` currently carries. Bumped when a field's meaning
/// changes, not when one is added. `validate()` refuses any other value, and the host compiler
/// refuses a `.scenario` whose `version` line names one it does not implement.
inline constexpr std::uint32_t currentScenarioSchemaVersion = 1;

/// The most steps one scenario may hold. Large enough for the interaction a reviewer would script
/// against one screen, small enough that a reviewer can multiply it by `sizeof(ScenarioStep)` in
/// their head - `maxInputEvents`'s reasoning applied to the step list.
inline constexpr std::size_t maxScenarioSteps = 256;

/// The most `Expect` steps one scenario may hold.
inline constexpr std::size_t maxScenarioExpectations = 128;

/// The most distinct `Capture` markers one scenario may name.
inline constexpr std::size_t maxScenarioCaptures = 16;

/// The most `requirement` references one scenario may cite.
inline constexpr std::size_t maxScenarioRequirements = 16;

// ===========================================================================
// The typed expectation vocabulary (ADR-020)
// ===========================================================================

/// Which fact an `Expect` step checks about the settled state. `Unspecified` is the aggregate
/// default and is never valid in a compiled scenario.
enum class ExpectKind : std::uint8_t {
    Unspecified,
    Clock,         ///< the injected civil time equals a pinned value
    Field,         ///< the bound `TextInput` value (and optionally caret) equals a pinned value
    RefusedEdits,  ///< the count of edits the `FieldEditor` refused this batch equals a pinned value
    Action,        ///< a critical press resolved to a pinned {node, SystemEvent, requirement}
    ButtonSource,  ///< an ordinary `Button` press resolved to a pinned {node, source}
    Reading,       ///< a named live reading equals a pinned fixed-point value
    State,         ///< a named `StatusIndicator` is at a pinned position in its `states:` list
    LatchArmed,    ///< the `PressLatch` is armed on a pinned node (empty node = disarmed)
    FrameStat,     ///< a `FrameStats` counter equals a pinned value
    Overflow,      ///< the batch overflowed (or did not)
};

[[nodiscard]] constexpr std::string_view toWire(ExpectKind kind) noexcept {
    switch (kind) {
        case ExpectKind::Clock:        return "clock";
        case ExpectKind::Field:        return "field";
        case ExpectKind::RefusedEdits: return "refused";
        case ExpectKind::Action:       return "action";
        case ExpectKind::ButtonSource: return "button";
        case ExpectKind::Reading:      return "reading";
        case ExpectKind::State:        return "state";
        case ExpectKind::LatchArmed:   return "latch";
        case ExpectKind::FrameStat:    return "frame";
        case ExpectKind::Overflow:     return "overflow";
        case ExpectKind::Unspecified:  return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<ExpectKind> expectKindFromWire(std::string_view wire) noexcept {
    if (wire == "clock")    return ExpectKind::Clock;
    if (wire == "field")    return ExpectKind::Field;
    if (wire == "refused")  return ExpectKind::RefusedEdits;
    if (wire == "action")   return ExpectKind::Action;
    if (wire == "button")   return ExpectKind::ButtonSource;
    if (wire == "reading")  return ExpectKind::Reading;
    if (wire == "state")    return ExpectKind::State;
    if (wire == "latch")    return ExpectKind::LatchArmed;
    if (wire == "frame")    return ExpectKind::FrameStat;
    if (wire == "overflow") return ExpectKind::Overflow;
    return std::nullopt;
}

/// Which `FrameStats` counter a `FrameStat` expectation names. Kept as its own enum rather than an
/// index into `FrameStats` so a reordered struct cannot silently re-point a committed scenario.
enum class FrameStatField : std::uint8_t {
    Unspecified,
    Nodes,
    Rects,
    Deferred,
    Traces,
    Readings,
    States,
    Fields,
};

[[nodiscard]] constexpr std::string_view toWire(FrameStatField field) noexcept {
    switch (field) {
        case FrameStatField::Nodes:    return "nodes";
        case FrameStatField::Rects:    return "rects";
        case FrameStatField::Deferred: return "deferred";
        case FrameStatField::Traces:   return "traces";
        case FrameStatField::Readings: return "readings";
        case FrameStatField::States:   return "states";
        case FrameStatField::Fields:   return "fields";
        case FrameStatField::Unspecified: return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<FrameStatField> frameStatFieldFromWire(std::string_view wire) noexcept {
    if (wire == "nodes")    return FrameStatField::Nodes;
    if (wire == "rects")    return FrameStatField::Rects;
    if (wire == "deferred") return FrameStatField::Deferred;
    if (wire == "traces")   return FrameStatField::Traces;
    if (wire == "readings") return FrameStatField::Readings;
    if (wire == "states")   return FrameStatField::States;
    if (wire == "fields")   return FrameStatField::Fields;
    return std::nullopt;
}

/**
 * @brief One typed expectation about the state a batch settled.
 *
 * A flat tagged aggregate rather than a variant, `mdux::evidence::json::Value`'s reasoning: the
 * members cost a little space that is irrelevant at scenario sizes and buy a type whose validity
 * nobody has to argue about, and a flat aggregate is what an emitter renders as a `constexpr`
 * initialiser without nesting. `kind` selects which members are meaningful; the rest are their
 * defaults.
 *
 * `fieldValue` is a view into a `constexpr char32_t` array the generated scenario module holds -
 * the same self-referential arrangement `ScreenPackage` uses for its node span.
 */
struct Expectation {
    ExpectKind kind{ExpectKind::Unspecified};

    mdux::medui::CivilTime    clock{};          ///< Clock
    std::span<const char32_t> fieldValue{};     ///< Field
    bool                      fieldHasCaret{false};
    std::uint32_t             caret{0};         ///< Field (when fieldHasCaret)
    std::uint32_t             count{0};         ///< RefusedEdits / State index / FrameStat count
    std::string_view          nodeId{};         ///< Action / ButtonSource / Reading / State / LatchArmed
    mdux::medui::SystemEvent  event{mdux::medui::SystemEvent::Unspecified};  ///< Action
    std::string_view          requirement{};    ///< Action
    std::string_view          source{};         ///< ButtonSource
    std::int64_t              value{0};         ///< Reading, in the template's fixed-point units
    FrameStatField            statField{FrameStatField::Unspecified};  ///< FrameStat
    bool                      flag{false};      ///< Overflow

    // No defaulted operator==: `fieldValue` is a `std::span`, which libc++ leaves without an
    // equality operator, and a pointer-identity comparison of it would be the wrong semantics
    // anyway. Nothing compares whole `Expectation`s; the replay reads members by `kind`.
};

// ===========================================================================
// The step model
// ===========================================================================

/// Which kind of step this is. `Advance` is the aggregate default so a default-constructed
/// `ScenarioStep` is a harmless no-op rather than an event with an unspecified kind.
enum class StepKind : std::uint8_t {
    Advance,
    Pointer,
    Key,
    Text,
    Focus,
    Expect,
    Capture,
};

[[nodiscard]] constexpr std::string_view toWire(StepKind kind) noexcept {
    switch (kind) {
        case StepKind::Advance:  return "advance";
        case StepKind::Pointer:  return "pointer";
        case StepKind::Key:      return "key";
        case StepKind::Text:     return "text";
        case StepKind::Focus:    return "focus";
        case StepKind::Expect:   return "expect";
        case StepKind::Capture:  return "capture";
    }
    return {};
}

[[nodiscard]] constexpr std::optional<StepKind> stepKindFromWire(std::string_view wire) noexcept {
    if (wire == "advance")  return StepKind::Advance;
    if (wire == "pointer")  return StepKind::Pointer;
    if (wire == "key")      return StepKind::Key;
    if (wire == "text")     return StepKind::Text;
    if (wire == "focus")    return StepKind::Focus;
    if (wire == "expect")   return StepKind::Expect;
    if (wire == "capture")  return StepKind::Capture;
    return std::nullopt;
}

/**
 * @brief One step of a scenario: an event to queue, a batch to advance, a fact to check, or a
 *        frame to capture.
 *
 * Flat and tagged, for `Expectation`'s reason. The event members mirror `mdux.medui.input`'s
 * structs one-for-one so the replay reconstructs an `InputEvent` for the queue without this type
 * having to nest a `std::variant`.
 */
struct ScenarioStep {
    StepKind kind{StepKind::Advance};

    mdux::medui::PointerEvent pointer{};   ///< Pointer
    mdux::medui::KeyEvent     key{};       ///< Key
    mdux::medui::TextEvent    text{};      ///< Text
    mdux::medui::FocusEvent   focus{};     ///< Focus
    std::uint32_t             frames{1};   ///< Advance: how many application updates this batch drives
    Expectation               expect{};    ///< Expect
    std::string_view          capture{};   ///< Capture: the marker name

    // No defaulted operator==: `expect` holds a `std::span` (see `Expectation`).
};

/// How the replay seeds the demonstration ECG generator before the first frame. `beatPeriod` is
/// the synthetic-beat period `MonitorApp.hpp`'s `syntheticSample()` takes; `warmupFrames` runs
/// that many generator steps before the scenario's first `Advance` so a trace has history.
struct ScenarioSampleSeed {
    std::uint32_t beatPeriod{60};
    std::uint32_t warmupFrames{0};

    [[nodiscard]] constexpr bool operator==(const ScenarioSampleSeed&) const noexcept = default;
};

// ===========================================================================
// CompiledScenario
// ===========================================================================

/// Why a `CompiledScenario` failed `validate()`. Every one is a structural fault the host compiler
/// should already have refused; re-checking here is the same second gate the emitted screen's
/// `static_assert(screen.validate())` is (ADR-012).
enum class ScenarioError : std::uint8_t {
    UnsupportedSchemaVersion,  ///< `schemaVersion` is not `currentScenarioSchemaVersion`
    EmptyIdentity,             ///< `id` or `screenId` is empty
    TooManySteps,              ///< more than `maxScenarioSteps`
    TooManyExpectations,       ///< more than `maxScenarioExpectations` `Expect` steps
    TooManyCaptures,           ///< more than `maxScenarioCaptures` distinct markers
    TooManyRequirements,       ///< more than `maxScenarioRequirements`
    NoAdvance,                 ///< a scenario that never advances a batch verifies nothing
    ExpectationBeforeAdvance,  ///< an `Expect` or `Capture` before the first `Advance`
    ExpectationInEventBatch,   ///< an `Expect` or `Capture` after events queued since the last `Advance`
    MalformedExpectation,      ///< an `Expect` step whose `kind` is `Unspecified`
    CaptureNameMismatch,       ///< a `Capture` step's name is absent from `captureNames`, or vice versa
    DuplicateCaptureName,      ///< two `captureNames` entries are equal
    MalformedClock,            ///< `pinnedClock` has an out-of-range month / day / hour / minute / second
    ZeroFrameAdvance,          ///< an `Advance` step with `frames == 0`
};

[[nodiscard]] constexpr std::string_view describe(ScenarioError error) noexcept {
    switch (error) {
        case ScenarioError::UnsupportedSchemaVersion: return "the scenario schema version is not the one this build implements";
        case ScenarioError::EmptyIdentity:            return "the scenario id or its screen id is empty";
        case ScenarioError::TooManySteps:             return "the scenario has more steps than the bound allows";
        case ScenarioError::TooManyExpectations:      return "the scenario has more expectations than the bound allows";
        case ScenarioError::TooManyCaptures:          return "the scenario names more captures than the bound allows";
        case ScenarioError::TooManyRequirements:      return "the scenario cites more requirements than the bound allows";
        case ScenarioError::NoAdvance:                return "the scenario never advances a batch, so it verifies nothing";
        case ScenarioError::ExpectationBeforeAdvance: return "an expectation or capture precedes the first advance";
        case ScenarioError::ExpectationInEventBatch:  return "an expectation or capture follows events that have not been advanced";
        case ScenarioError::MalformedExpectation:     return "an expect step carries no expectation kind";
        case ScenarioError::CaptureNameMismatch:      return "a capture step and the capture-name list disagree";
        case ScenarioError::DuplicateCaptureName:     return "two capture names are equal";
        case ScenarioError::MalformedClock:           return "the pinned clock has an out-of-range field";
        case ScenarioError::ZeroFrameAdvance:         return "an advance step drives zero application updates";
    }
    return "unknown scenario error";
}

/**
 * @brief One compiled, immutable interaction scenario.
 *
 * Every span views a `constexpr` array the generated translation unit holds; nothing is owned, and
 * `validate()` is pure. `pinnedClock` is the civil time the replay injects (ADR-018 clause 6:
 * replay injects time); the same batches and the same pinned clock produce the same frames, which
 * is PAR-REQ-005's "identical replay snapshot".
 */
struct CompiledScenario {
    std::string_view                     id{};
    std::string_view                     screenId{};
    std::uint32_t                        schemaVersion{0};
    mdux::medui::CivilTime               pinnedClock{};
    ScenarioSampleSeed                   sampleSeed{};
    std::span<const std::string_view>    requirements{};
    std::span<const std::string_view>    captureNames{};
    std::span<const ScenarioStep>        steps{};

    /// Structural checks only - the host compiler owns semantic validation against a screen. See
    /// `ScenarioError` for each.
    [[nodiscard]] constexpr mdux::core::Result<void, ScenarioError> validate() const noexcept {
        if (schemaVersion != currentScenarioSchemaVersion) {
            return mdux::core::err(ScenarioError::UnsupportedSchemaVersion);
        }
        if (id.empty() || screenId.empty()) {
            return mdux::core::err(ScenarioError::EmptyIdentity);
        }
        if (steps.size() > maxScenarioSteps) {
            return mdux::core::err(ScenarioError::TooManySteps);
        }
        if (requirements.size() > maxScenarioRequirements) {
            return mdux::core::err(ScenarioError::TooManyRequirements);
        }
        if (captureNames.size() > maxScenarioCaptures) {
            return mdux::core::err(ScenarioError::TooManyCaptures);
        }

        const auto& c = pinnedClock;
        if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > 31 || c.hour > 23 || c.minute > 59 || c.second > 59) {
            return mdux::core::err(ScenarioError::MalformedClock);
        }

        for (std::size_t i = 0; i < captureNames.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                if (captureNames[i] == captureNames[j]) {
                    return mdux::core::err(ScenarioError::DuplicateCaptureName);
                }
            }
        }

        std::size_t expectations       = 0;
        std::size_t advances           = 0;
        bool        eventsSinceAdvance = false;
        for (const ScenarioStep& step : steps) {
            switch (step.kind) {
                case StepKind::Advance:
                    if (step.frames == 0) {
                        return mdux::core::err(ScenarioError::ZeroFrameAdvance);
                    }
                    ++advances;
                    eventsSinceAdvance = false;
                    break;
                case StepKind::Expect:
                    if (step.expect.kind == ExpectKind::Unspecified) {
                        return mdux::core::err(ScenarioError::MalformedExpectation);
                    }
                    if (advances == 0) {
                        return mdux::core::err(ScenarioError::ExpectationBeforeAdvance);
                    }
                    if (eventsSinceAdvance) {
                        return mdux::core::err(ScenarioError::ExpectationInEventBatch);
                    }
                    ++expectations;
                    break;
                case StepKind::Capture:
                    if (advances == 0) {
                        return mdux::core::err(ScenarioError::ExpectationBeforeAdvance);
                    }
                    if (eventsSinceAdvance) {
                        return mdux::core::err(ScenarioError::ExpectationInEventBatch);
                    }
                    if (!nameListed(step.capture)) {
                        return mdux::core::err(ScenarioError::CaptureNameMismatch);
                    }
                    break;
                case StepKind::Pointer:
                case StepKind::Key:
                case StepKind::Text:
                case StepKind::Focus:
                    eventsSinceAdvance = true;
                    break;
            }
        }
        if (advances == 0) {
            return mdux::core::err(ScenarioError::NoAdvance);
        }
        if (expectations > maxScenarioExpectations) {
            return mdux::core::err(ScenarioError::TooManyExpectations);
        }

        // Every declared capture name must be produced by some Capture step.
        for (const std::string_view name : captureNames) {
            bool produced = false;
            for (const ScenarioStep& step : steps) {
                if (step.kind == StepKind::Capture && step.capture == name) {
                    produced = true;
                    break;
                }
            }
            if (!produced) {
                return mdux::core::err(ScenarioError::CaptureNameMismatch);
            }
        }

        return {};
    }

private:
    [[nodiscard]] constexpr bool nameListed(std::string_view name) const noexcept {
        for (const std::string_view listed : captureNames) {
            if (listed == name) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace mdux::medui
