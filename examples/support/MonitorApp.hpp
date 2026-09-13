/**
 * @file MonitorApp.hpp
 * @brief The assembled monitor application update - ADR-018 clause 6, steps 1 and 2 (#318).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone)
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 *
 * What this is: the one application-update implementation the interactive window and the
 * deterministic headless smoke both drive - there is no test-only state machine. It holds the
 * deterministic clock, the caller-owned demonstration state and its generators, and
 * `updateMonitor()`, which drains one accepted event batch and applies its presses and edits to
 * that state (ADR-018 clause 6, steps 1-2). Binding a snapshot, rendering and capturing (steps
 * 3-5) is the caller's, because that is where the committed packages and the device live.
 *
 * What this is not: it opens no window, touches no Vulkan, executes no action. A resolved
 * `emergency-halt` press is returned as an `ActionTrace` and a resolved `freeze` press as its
 * open `source` string - the host owns both (ADR-018 clause 7); this loop logs them and does
 * nothing else. **The readings, the waveform and the classifier state are demonstration data,
 * not clinically qualified input.**
 *
 * ## Include order
 *
 * Include this header **after** `import std;`, `import mdux.core.result;`, `import mdux.core.units;`,
 * `import mdux.draw;`, `import mdux.font.schema;`, `import mdux.medui.input;`,
 * `import mdux.medui.reading;`, `import mdux.medui.schema;`, `import mdux.medui.screen;`,
 * `import mdux.medui.trace;` and `import mdux.medui.viewport;` - it names those without importing
 * them, the way `GlfwPresentationAdapter.hpp` does.
 */
#pragma once

namespace mdux::examples {

namespace mapp_detail {
namespace core = mdux::core;
namespace ms   = mdux::medui;
}  // namespace mapp_detail

// ---------------------------------------------------------------------------
// The deterministic clock
// ---------------------------------------------------------------------------

/// A civil clock that advances by a fixed tick and never reads a wall clock, so an interactive
/// run and a headless replay that call `advance()` the same number of times show the same face.
/// `Clock` on the screen renders `HH:MM:SS` (`ClockFormat::TimeSeconds`), so only the time of day
/// has to be right; the date rolls over naively because it is never displayed.
struct MonitorClock {
    mdux::medui::CivilTime now{.year = 2026, .month = 9, .day = 9, .hour = 8, .minute = 0, .second = 0};

    /// Advances the clock by `seconds` (one frame's worth for the interactive loop, a fixed step
    /// for the headless one).
    constexpr void advance(std::uint32_t seconds = 1) noexcept {
        std::uint32_t s = static_cast<std::uint32_t>(now.second) + seconds;
        now.second      = static_cast<std::uint8_t>(s % 60u);
        std::uint32_t m = static_cast<std::uint32_t>(now.minute) + s / 60u;
        now.minute      = static_cast<std::uint8_t>(m % 60u);
        std::uint32_t h = static_cast<std::uint32_t>(now.hour) + m / 60u;
        now.hour        = static_cast<std::uint8_t>(h % 24u);
        std::uint32_t d = static_cast<std::uint32_t>(now.day) + h / 24u;
        now.day         = static_cast<std::uint8_t>(((d - 1u) % 28u) + 1u);
    }
};

// ---------------------------------------------------------------------------
// The demonstration data and its generators
// ---------------------------------------------------------------------------

/// A fixed-capacity ring of samples for the `ECG_LEAD_II` trace. The same shape
/// `EcgClassifierExample` uses; a local copy keeps this header free of that translation unit's
/// embedded-weights dependency.
template <std::size_t Capacity>
class MonitorSampleRing {
public:
    constexpr void push(float sample) noexcept {
        samples_[head_] = sample;
        head_           = (head_ + 1) % Capacity;
        if (filled_ < Capacity) {
            ++filled_;
        }
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return filled_ == Capacity;
    }

    /// A description of the ring's storage the trace expansion reads afresh each frame - not a copy.
    [[nodiscard]] mdux::medui::SampleRing view() const noexcept {
        return mdux::medui::SampleRing{.storage = samples_, .oldest = full() ? head_ : 0, .count = filled_};
    }

private:
    std::array<float, Capacity> samples_{};
    std::size_t                 head_{0};
    std::size_t                 filled_{0};
};

/// A crude synthetic beat: a baseline with a periodic spike. Not an ECG and not pretending to be -
/// it gives the trace something time-varying to draw.
[[nodiscard]] inline float syntheticSample(std::size_t index, std::size_t beatPeriod) noexcept {
    const std::size_t phase = index % beatPeriod;
    if (phase == 0) {
        return 1.0F;
    }
    if (phase == 1) {
        return -0.5F;
    }
    return 0.05F * static_cast<float>(index % 7) - 0.15F;
}

/// The band the synthetic samples are read against - the device's numbers, per `TraceStyle`'s own
/// contract, here standing in for an amplifier the host would own.
inline constexpr mdux::medui::TraceStyle monitorTraceStyle{.minimum = -1.0F, .maximum = 1.5F, .strokeWidth = 2};

/// A fixed-capacity ring of waterfall rows for the `ENDOSCOPE_PRIMARY` viewport (#324). `SampleRing`
/// generalised to `WaterfallGrid`'s row granularity, exactly `MonitorSampleRing`'s shape one level
/// up: `pushRow()` writes a whole row where `push()` writes one scalar, and `view()` describes the
/// same storage afresh rather than copying it, for `MonitorSampleRing::view()`'s reason.
template <std::size_t RowCapacity, std::size_t Bins>
class MonitorWaterfallRing {
public:
    /// Writes one new row from `generator(bin)`, evaluated for `bin` in `[0, Bins)`.
    template <typename Generator>
    constexpr void pushRow(Generator&& generator) noexcept {
        for (std::size_t bin = 0; bin < Bins; ++bin) {
            storage_[(head_ * Bins) + bin] = generator(bin);
        }
        head_ = (head_ + 1) % RowCapacity;
        if (filled_ < RowCapacity) {
            ++filled_;
        }
    }

    /// A description of the ring's storage the waterfall expansion reads afresh each frame - not a
    /// copy, `MonitorSampleRing::view()`'s reason.
    [[nodiscard]] mdux::medui::WaterfallGrid view() const noexcept {
        return mdux::medui::WaterfallGrid{.storage = storage_, .bins = Bins, .oldestRow = filled_ == RowCapacity ? head_ : 0, .rowCount = filled_};
    }

private:
    std::array<float, RowCapacity * Bins> storage_{};
    std::size_t                           head_{0};
    std::size_t                           filled_{0};
};

/// A crude synthetic intensity sweep: one bright band, `waterfallBandHalfWidth` bins wide, drifting
/// sideways one bin per row and wrapping the short way round the row's edges. Not an endoscopic
/// image and not pretending to be - it gives the waterfall something time-varying and spatially
/// bounded to draw. Integer arithmetic only, `syntheticSample()`'s reason: two toolchains computing
/// this must quantise every cell to the same byte, and a transcendental function is not guaranteed
/// to round the same way on all of them.
inline constexpr std::int64_t waterfallBandHalfWidth = 4;

[[nodiscard]] constexpr float syntheticWaterfallCell(std::size_t row, std::size_t bin, std::size_t bins) noexcept {
    if (bins == 0) {
        // A degenerate row width is a caller defect this generator cannot draw anything sensible
        // for - `row % bins` below would be a division by zero. The floor value is the same one a
        // real, out-of-range distance already returns, so a caller that somehow reaches this still
        // gets a defined, dim cell rather than undefined behaviour.
        return 0.05F;
    }
    const auto peak     = static_cast<std::int64_t>(row % bins);
    const auto position = static_cast<std::int64_t>(bin);
    auto       distance = peak - position;
    if (distance < 0) {
        distance = -distance;
    }
    const auto half = static_cast<std::int64_t>(bins / 2);
    if (distance > half) {
        // The short way round: a band drifting past bin 0 re-enters at the far edge rather than
        // vanishing, exactly as it would on a display with no seam.
        distance = static_cast<std::int64_t>(bins) - distance;
    }
    if (distance > waterfallBandHalfWidth) {
        return 0.05F;  // a low, non-zero floor - "no signal here" reads as dim, not as invisible
    }
    return 1.0F - (static_cast<float>(distance) / static_cast<float>(waterfallBandHalfWidth)) * 0.8F;
}

/// The domain the synthetic cells are read against, and the ramp they paint with - the device's
/// numbers and a caller-chosen palette, per `WaterfallStyle`'s own contract (ADR-022 decision 2: no
/// governed colour token exists for this component, so this is caller-supplied and unverified, same
/// as `monitorTraceStyle` above). Demonstration colours, not a clinical palette.
inline constexpr mdux::medui::WaterfallStyle monitorWaterfallStyle{
    .minimum   = 0.0F,
    .maximum   = 1.0F,
    .lowColor  = mdux::core::ColorRgba8{ .r = 10,  .g = 8, .b = 14, .a = 255},
    .highColor = mdux::core::ColorRgba8{.r = 214, .g = 96, .b = 64, .a = 255}
};

/// The waterfall's row/bin shape. Both at their type-level cap (`maxWaterfallRows`/`maxWaterfallBins`)
/// so the demonstrator exercises the worst case #322/ADR-022 sized the budget arithmetic against,
/// rather than a smaller one nothing would catch a regression against.
inline constexpr std::size_t kViewportRows = mdux::medui::maxWaterfallRows;
inline constexpr std::size_t kViewportBins = mdux::medui::maxWaterfallBins;

/// The node ids the monitor's controls and readings bind to.
inline constexpr std::string_view kHaltNode          = "emergency-halt";
inline constexpr std::string_view kFreezeNode        = "freeze";
inline constexpr std::string_view kPatientNode       = "patient-id";
inline constexpr std::string_view kPressureNode      = "insufflation-pressure";
inline constexpr std::string_view kStatusNode        = "classifier-state";
inline constexpr std::string_view kViewportNode      = "endoscope-view";
inline constexpr std::string_view kTraceStream       = "ECG_LEAD_II";
inline constexpr std::string_view kViewportStream    = "ENDOSCOPE_PRIMARY";
inline constexpr std::string_view kPressureRendering = "##.# mmHg";
/// The clock's tint - `ReadingBinding::create()` validates it against the governed colour table.
inline constexpr std::string_view kClockColorToken = "Theme.Colors.Title";

/// The caller-owned application state one `updateMonitor()` mutates (ADR-018 clause 6, step 2).
struct DemoState {
    MonitorSampleRing<180>                             ecg{};
    MonitorWaterfallRing<kViewportRows, kViewportBins> waterfall{};
    std::int64_t                                       pressureTenths{143};  ///< `INSUFFLATION_PRESSURE`, tenths of mmHg
    std::uint32_t                                      classifierState{0};   ///< `ECG_CLASS`, a position in the node's states
    std::uint64_t                                      tick{0};
    std::array<char32_t, 64>                           fieldBuffer{};
    std::optional<mdux::medui::FieldEditor>            field{};

    /// Binds the `patient-id` `FieldEditor` over `fieldBuffer`, bounded by the font charset and the
    /// node's `charset:`. Leaves `field` empty if the node is absent or of the wrong kind.
    void bindField(const mdux::medui::ScreenPackage& screen, const mdux::font::FontPackage& font) {
        const mdux::medui::CompiledNode* node = screen.find(kPatientNode);
        if (node == nullptr) {
            return;
        }
        const auto* spec = std::get_if<mdux::medui::TextInputSpec>(&node->payload);
        if (spec == nullptr) {
            return;
        }
        const std::size_t maxLength = spec->maxLength < 0 ? 0 : static_cast<std::size_t>(spec->maxLength);
        auto              made      = mdux::medui::FieldEditor::create(kPatientNode, fieldBuffer, {}, font.restrictedCharset, spec->charsetRanges, maxLength);
        if (made) {
            field = *made;
        }
    }

    /// Advances the demonstration generators one tick, deterministically: a fresh ECG sample, a
    /// pressure that breathes between 12.0 and 16.0 mmHg, a classifier state that steps once a
    /// second through its four positions, and one new waterfall row (#324).
    constexpr void step() noexcept {
        ecg.push(syntheticSample(static_cast<std::size_t>(tick), 60));
        waterfall.pushRow([this](std::size_t bin) {
            return syntheticWaterfallCell(static_cast<std::size_t>(tick), bin, kViewportBins);
        });
        pressureTenths  = 140 + static_cast<std::int64_t>(tick % 40u) - 20;  // 12.0 .. 16.0
        classifierState = static_cast<std::uint32_t>((tick / 30u) % 4u);
        ++tick;
    }
};

// ---------------------------------------------------------------------------
// The update (ADR-018 clause 6, steps 1-2)
// ---------------------------------------------------------------------------

/// What one update resolved for the caller to render and log. Both actions carry only a record;
/// the host executes neither (ADR-018 clause 7).
struct MonitorUpdateOutcome {
    std::optional<mdux::medui::ActionTrace> criticalAction{};     ///< a resolved `emergency-halt` press
    std::optional<std::string>              buttonSource{};       ///< a resolved `freeze` press, its open `source`
    std::string_view                        buttonNode{};         ///< the node `buttonSource` resolved to (empty = none)
    std::uint32_t                           refusedEdits{0};      ///< edits the `FieldEditor` refused this batch
    bool                                    droppedBatch{false};  ///< the batch overflowed and was discarded whole
};

/// Drains one accepted batch and applies it to `state` (ADR-018 clause 6, steps 1-2), then
/// advances the clock and the demonstration generators. On overflow (ADR-019 clause 3) the batch
/// is discarded whole and the latch cancelled - acting on a partial batch could let a queued Down
/// re-arm a control the cancel was meant to disarm.
[[nodiscard]] inline MonitorUpdateOutcome updateMonitor(mdux::medui::EventQueue&          queue,
                                                        bool                              overflowed,
                                                        const mdux::medui::ScreenPackage& screen,
                                                        DemoState&                        state,
                                                        mdux::medui::PressLatch&          latch,
                                                        MonitorClock&                     clock,
                                                        std::uint64_t&                    sequence,
                                                        std::uint32_t                     clockStepSeconds = 1) {
    namespace ms = mdux::medui;
    MonitorUpdateOutcome outcome;

    if (overflowed) {
        queue.clear();
        latch.cancel();
        outcome.droppedBatch = true;
    } else {
        while (const auto event = queue.pop()) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<T, ms::PointerEvent>) {
                        if (value.kind == ms::PointerKind::Cancel) {
                            latch.cancel();
                            return;
                        }
                        const auto             press = ms::resolvePress(screen, value.x, value.y);
                        const std::string_view node  = (press && press->has_value()) ? (*press)->nodeId : std::string_view{};
                        if (value.kind == ms::PointerKind::Down) {
                            latch.arm(node);
                        } else if (value.kind == ms::PointerKind::Up) {
                            if (latch.release(node) && press && press->has_value()) {
                                const ms::PressAction& action = **press;
                                if (node == kHaltNode && action.event.has_value()) {
                                    outcome.criticalAction = ms::ActionTrace{.nodeId      = action.nodeId,
                                                                             .requirement = action.requirement,
                                                                             .event       = *action.event,
                                                                             .sequence    = ++sequence};
                                } else if (node == kFreezeNode && !action.source.empty()) {
                                    outcome.buttonSource = std::string{action.source};
                                    outcome.buttonNode   = kFreezeNode;
                                }
                            }
                        }
                    } else if constexpr (std::is_same_v<T, ms::FocusEvent>) {
                        if (state.field) {
                            state.field->focus(value);
                        }
                    } else if constexpr (std::is_same_v<T, ms::KeyEvent>) {
                        if (!state.field) {
                            return;
                        }
                        if (value.kind == ms::KeyKind::Down && (value.key == ms::KeyCode::FocusNext || value.key == ms::KeyCode::Commit)) {
                            state.field->focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = kPatientNode});
                            return;
                        }
                        if (value.kind == ms::KeyKind::Down && value.key == ms::KeyCode::Cancel) {
                            state.field->focus(ms::FocusEvent{.kind = ms::FocusKind::Leave, .nodeId = kPatientNode});
                            return;
                        }
                        if (const auto handled = state.field->handleKey(value); !handled) {
                            ++outcome.refusedEdits;
                        }
                    } else if constexpr (std::is_same_v<T, ms::TextEvent>) {
                        if (state.field) {
                            if (const auto handled = state.field->handleText(value); !handled) {
                                ++outcome.refusedEdits;
                            }
                        }
                    }
                },
                *event);
        }
    }

    clock.advance(clockStepSeconds);
    state.step();
    return outcome;
}

}  // namespace mdux::examples
