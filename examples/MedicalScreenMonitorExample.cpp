/**
 * @file MedicalScreenMonitorExample.cpp
 * @brief The assembled interactive monitor: the committed `endoscope-monitor` screen presented in a
 *        window, with real pointer/keyboard input flowing through one application-update
 *        implementation into every live binding, in two approved locales (#318, ADR-018 clause 6).
 *
 * @compliance IEC 62304 Class B - Medical Device Example
 * @compliance ADR-018 Bounded input, application update order and action policy (clause 6)
 * @compliance ADR-019 The windowed presentation and input adapter
 *
 * What this shows:
 *
 * - `examples/support/GlfwPresentationAdapter.hpp` owns the window, the Vulkan instance / device /
 *   swapchain and the native-event translation. `examples/support/MonitorApp.hpp` owns the
 *   deterministic clock, the demonstration state and `updateMonitor()` - the one application update
 *   (ADR-018 clause 6, steps 1-2). This file binds a snapshot of that state, renders it and, in the
 *   headless mode, reads it back (steps 3-5). **There is no test-only state machine: the interactive
 *   window and the headless smoke drive the same `updateMonitor()`.**
 * - Every live component draws: a numeric insufflation-pressure reading, a deterministic clock, the
 *   ECG signal trace, the classifier status, the editable `patient-id` field, and the two control
 *   faces.
 * - A left-click resolved to `emergency-halt` arms a `PressLatch`; the release on the same control
 *   activates it and this example prints an `ActionTrace` and **executes nothing** - the host owns
 *   the halt path (ADR-018 clause 7). A click on the ordinary `freeze` Button prints its open
 *   `source` string and also executes nothing.
 * - Typing routes through a `FieldEditor` bound to `patient-id`, whose `charset:` admits digits and
 *   `A`-`Z` only; a lowercase letter or a symbol is refused with no mutation.
 * - A resize rebuilds the `SurfaceMapping` and cancels the latch; a focus loss cancels it; a dropped
 *   batch is discarded whole and the latch cancelled (ADR-019 clause 3).
 *
 * **The readings, the waveform and the classifier state are demonstration data, not clinically
 * qualified input.**
 *
 * Modes:
 *   (no args)                        interactive, en-US
 *   --locale=fr-FR                   interactive, fr-FR (also valid with the two modes below)
 *   --smoke-test                     present a few frames then exit; non-zero on a deadline or a Vulkan failure
 *   --headless-smoke                 run a fixed script of events through updateMonitor(), render one
 *                                    frame offscreen (no window) and assert the resolved actions,
 *                                    the refused edits and the painted content
 */

import std;
import mdux;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.image.schema;
import mdux.text.schema;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.medui.scenario;
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.medui.generated.scenario_endoscope_monitor_basics;
import mdux.render.vulkan;
import mdux.render.offscreen;
import mdux.shader.generated.mdux_ui;
import mdux.shader.schema;

#include "support/GlfwPresentationAdapter.hpp"
#include "support/MonitorApp.hpp"
#include "support/ScenarioReplay.hpp"

#include "brandMarkPackageJson.hpp"
#include "brandMarkPixels.hpp"
#include "dejavuUiAtlas.hpp"
#include "dejavuUiPackageJson.hpp"
#include "endoscopeTextEnUsPackageJson.hpp"
#include "endoscopeTextEnUsRuns.hpp"
#include "endoscopeTextFrFrPackageJson.hpp"
#include "endoscopeTextFrFrRuns.hpp"

namespace {

namespace core = mdux::core;
namespace draw = mdux::draw;
namespace ms   = mdux::medui;
namespace mx   = mdux::examples;
namespace rnd  = mdux::render;

enum class Locale { EnUs, FrFr };

[[nodiscard]] std::string_view localeTag(Locale locale) noexcept {
    return locale == Locale::FrFr ? "fr-FR" : "en-US";
}

/// An embedded blob's bytes as UTF-8 text, for the `*Package::parse()` calls.
[[nodiscard]] std::string_view asText(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// The compiled screen and the committed packages a device joins it to at start-up, for one locale.
///
/// Held on the heap for the life of the program and never moved: `TextBinding` / `ImageBinding`
/// keep pointers into `font` / `text` / `image`, so a move would dangle them.
struct BoundScreen {
    ms::ScreenPackage         screen{ms::generated::screen_endoscope_monitor::package()};
    mdux::font::FontPackage    font{};
    mdux::text::TextPackage    text{};
    mdux::image::ImagePackage  image{};
    ms::TextBinding            textBinding{};
    ms::ImageBinding           imageBinding{};

    BoundScreen()                              = default;
    BoundScreen(const BoundScreen&)            = delete;
    BoundScreen& operator=(const BoundScreen&) = delete;

    /// The authored surface extent the screen was compiled for - the `UiRenderer` viewport.
    [[nodiscard]] core::Extent2D surface() const noexcept {
        return {screen.surfaceWidth, screen.surfaceHeight};
    }

    /// Parses the embedded font / text / image packages for `locale` and builds the two bindings, or
    /// prints why and returns `nullptr`.
    [[nodiscard]] static std::unique_ptr<BoundScreen> load(Locale locale) {
        auto bound = std::make_unique<BoundScreen>();

        const std::span<const std::byte> textJson =
            locale == Locale::FrFr ? endoscopeTextFrFrPackageJson() : endoscopeTextEnUsPackageJson();
        const std::span<const std::byte> textRuns =
            locale == Locale::FrFr ? endoscopeTextFrFrRuns() : endoscopeTextEnUsRuns();

        auto font  = mdux::font::FontPackage::parse(asText(dejavuUiPackageJson()));
        auto text  = mdux::text::TextPackage::parse(asText(textJson));
        auto image = mdux::image::ImagePackage::parse(asText(brandMarkPackageJson()));
        if (!font || !text || !image) {
            std::cerr << "monitor: a committed package did not parse\n";
            return nullptr;
        }
        bound->font  = std::move(*font);
        bound->text  = std::move(*text);
        bound->image = std::move(*image);

        auto textBinding = ms::TextBinding::create(bound->screen, bound->font, bound->text, textJson, textRuns);
        if (!textBinding) {
            std::cerr << "monitor: the committed text artifacts were refused: " << ms::describe(textBinding.error()) << '\n';
            return nullptr;
        }
        bound->textBinding = *textBinding;

        auto imageBinding = ms::ImageBinding::create(bound->screen, bound->image, brandMarkPackageJson(), brandMarkPixels());
        if (!imageBinding) {
            std::cerr << "monitor: the committed image artifacts were refused: " << ms::describe(imageBinding.error()) << '\n';
            return nullptr;
        }
        bound->imageBinding = *imageBinding;
        return bound;
    }
};

/// Storage a caller sizes once from the screen's own budget, as a device would.
struct FrameStorage {
    std::vector<draw::UiVertex>    vertices;
    std::vector<draw::Index>       indices;
    std::vector<draw::DrawCommand> commands;

    explicit FrameStorage(const draw::DrawBudget& budget)
        : vertices(budget.maxVertices), indices(budget.maxIndices), commands(budget.maxCommands) {}
};

/// Builds this frame's `DrawList` from the screen and one snapshot of the demonstration state
/// (ADR-018 clause 6, steps 3-4): the ECG trace, the pressure reading, the deterministic clock, the
/// classifier state and the `patient-id` field value are all bound here, after `updateMonitor()`
/// resolved the batch, so a capture never shows input the operator has not seen resolved.
[[nodiscard]] mdux::core::Result<draw::DrawList, ms::ScreenError> recordMonitorFrame(const BoundScreen&    bound,
                                                                                    FrameStorage&         storage,
                                                                                    const mx::DemoState&  state,
                                                                                    const mx::MonitorClock& clock) {
    auto list = draw::DrawList::create(storage.vertices, storage.indices, storage.commands, bound.screen.budget);
    if (!list) {
        std::cerr << "monitor: draw list refused: " << draw::describe(list.error()) << '\n';
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    const ms::SampleRing                 traceView = state.ecg.view();
    const std::array<ms::SignalSlot, 1>  signalSlots{
        ms::SignalSlot{.streamSource = mx::kTraceStream, .ring = &traceView, .style = mx::monitorTraceStyle}
    };
    ms::SignalBinding signals{};
    if (auto made = ms::SignalBinding::create(bound.screen, signalSlots); made) {
        signals = *made;
    } else {
        std::cerr << "monitor: signal binding refused: " << ms::describe(made.error()) << '\n';
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    const std::array<ms::ReadingSlot, 1> readingSlots{
        ms::ReadingSlot{.nodeId = mx::kPressureNode, .rendering = mx::kPressureRendering, .value = state.pressureTenths}
    };
    ms::ReadingBinding readings{};
    if (auto made = ms::ReadingBinding::create(bound.screen, readingSlots, &clock.now, mx::kClockColorToken); made) {
        readings = *made;
    } else {
        std::cerr << "monitor: reading binding refused: " << ms::describe(made.error()) << '\n';
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    const std::array<ms::StatusSlot, 1> statusSlots{
        ms::StatusSlot{.nodeId = mx::kStatusNode, .state = state.classifierState}
    };
    ms::StatusBinding status{};
    if (auto made = ms::StatusBinding::create(bound.screen, statusSlots); made) {
        status = *made;
    } else {
        std::cerr << "monitor: status binding refused: " << ms::describe(made.error()) << '\n';
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    std::array<ms::TextInputSlot, 1> inputSlots{};
    ms::TextInputBinding             inputs{};
    if (state.field) {
        inputSlots[0] = ms::TextInputSlot{.nodeId = state.field->nodeId(),
                                          .text   = state.field->value(),
                                          .caret  = state.field->caret()};
        auto made = ms::TextInputBinding::create(bound.screen, inputSlots);
        if (!made) {
            // Fail closed rather than render a deferred field: a monitor that silently drops the
            // patient id it was asked to show is the wrong failure.
            std::cerr << "monitor: text input binding refused: " << ms::describe(made.error()) << '\n';
            return mdux::core::err(made.error());
        }
        inputs = *made;
    }

    const auto recorded =
        ms::render(bound.screen, *list, bound.textBinding, bound.imageBinding, signals, readings, status, inputs);
    if (!recorded) {
        std::cerr << "monitor: render refused: " << ms::describe(recorded.error()) << '\n';
        return mdux::core::err(recorded.error());
    }
    return std::move(*list);
}

/// A `UiRenderer` for `context` with the committed font coverage atlas and the brand-mark image
/// atlas, so text and the logo draw as themselves rather than white blocks.
[[nodiscard]] mdux::core::Result<rnd::UiRenderer, rnd::RenderError> makeRenderer(const rnd::VulkanRenderContext& context,
                                                                                const BoundScreen&             bound) {
    return rnd::UiRenderer::createWithAtlases(context, mdux::shader::generated::mdux_ui::package(), bound.screen.budget,
                                              dejavuUiAtlas(), bound.font.atlas.width, bound.font.atlas.height,
                                              brandMarkPixels(), bound.image.width, bound.image.height);
}

/// The near-black ground the screen is composited over.
constexpr core::ColorRgba8 kClear{.r = 6, .g = 8, .b = 10, .a = 255};

// ---------------------------------------------------------------------------
// Interactive / smoke-test mode
// ---------------------------------------------------------------------------

int runWindowed(bool smokeTest, Locale locale) {
    auto bound = BoundScreen::load(locale);
    if (!bound) {
        return 1;
    }
    const core::Extent2D surface = bound->surface();

    auto window = mx::GlfwWindow::create(surface.width, surface.height, "MduX - Medical Screen Monitor");
    if (!window) {
        return 1;
    }

    rnd::VulkanRenderContext context = window->renderContext();
    context.viewport                 = surface;
    auto renderer                    = makeRenderer(context, *bound);
    if (!renderer) {
        std::cerr << "monitor: could not create the renderer: " << rnd::describe(renderer.error()) << '\n';
        return 1;
    }

    struct IdleBeforeRendererTeardown {
        const mx::GlfwWindow* window;
        ~IdleBeforeRendererTeardown() { window->waitIdle(); }
    } const idleGuard{&*window};

    mx::DemoState  state;
    state.bindField(bound->screen, bound->font);
    mx::MonitorClock clock;

    std::array<ms::InputEvent, ms::maxInputEvents> queueStorage{};
    ms::EventQueue                                 queue{queueStorage};
    ms::PressLatch                                 latch;
    std::uint64_t                                  sequence = 0;

    const auto buildMapping = [&] {
        return ms::SurfaceMapping::create(window->framebufferExtent(), window->windowExtent());
    };
    auto mapping = buildMapping();
    if (!mapping) {
        std::cerr << "monitor: the framebuffer-to-window ratio is not one this adapter supports\n";
        return 1;
    }
    mx::WindowEventPump pump{window->handle(), queue, *mapping};

    FrameStorage storage{bound->screen.budget};

    constexpr std::uint32_t        smokeFramesRequired = 3;
    constexpr std::chrono::seconds smokeDeadline{30};
    const std::uint64_t            timeoutNanos = smokeTest ? std::uint64_t{2'000'000'000} : UINT64_MAX;
    const auto                     startedAt    = std::chrono::steady_clock::now();
    std::uint32_t                  presented    = 0;

    const auto smokeExpired = [&] {
        return smokeTest && std::chrono::steady_clock::now() - startedAt > smokeDeadline;
    };

    // The one resync path (ADR-019 clause 4): rebuild the swapchain and the SurfaceMapping and
    // cancel any armed press. `false` means exit non-zero.
    const auto resync = [&]() -> bool {
        if (!window->recreateSwapchain()) {
            return false;
        }
        const core::Extent2D fb  = window->framebufferExtent();
        const core::Extent2D win = window->windowExtent();
        if (fb.width == 0 || fb.height == 0 || win.width == 0 || win.height == 0) {
            latch.cancel();
            return true;
        }
        auto rebuilt = buildMapping();
        if (!rebuilt) {
            std::cerr << "monitor: after the resize the framebuffer-to-window ratio is no longer supported\n";
            return false;
        }
        pump.setMapping(*rebuilt);
        latch.cancel();
        return true;
    };

    std::println("Monitor running ({}). Click the red halt control or the grey FREEZE button;", localeTag(locale));
    std::println("type a patient id (digits, A-Z). Press Esc to stop editing, close the window to exit.");

    while (!window->shouldClose()) {
        glfwPollEvents();

        if (pump.takeSurfaceDirty()) {
            if (!resync()) {
                return 1;
            }
        }
        if (pump.takeFocusLost()) {
            latch.cancel();
        }

        const mx::MonitorUpdateOutcome update =
            mx::updateMonitor(queue, pump.takeOverflow(), bound->screen, state, latch, clock, sequence);
        if (update.droppedBatch) {
            std::println("(input overflowed - dropped a partial batch)");
        }
        if (update.criticalAction) {
            std::println("ActionTrace #{}: node='{}' requirement='{}' event={} - the host executes this, not MduX",
                         update.criticalAction->sequence, update.criticalAction->nodeId,
                         update.criticalAction->requirement, ms::toWire(update.criticalAction->event));
        }
        if (update.buttonSource) {
            std::println("Button press: node='freeze' source='{}' - the host owns this action, not MduX", *update.buttonSource);
        }

        auto list = recordMonitorFrame(*bound, storage, state, clock);
        if (!list) {
            return 1;
        }

        mx::FrameContext         frame{};
        const mx::PresentOutcome begun = window->beginFrame(kClear, timeoutNanos, frame);
        if (begun == mx::PresentOutcome::DeviceLost) {
            std::cerr << "monitor: the Vulkan device was lost\n";
            return 1;
        }
        if (begun == mx::PresentOutcome::OutOfDate) {
            if (!resync()) {
                return 1;
            }
            if (smokeExpired()) {
                break;
            }
            continue;
        }
        if (begun == mx::PresentOutcome::Skipped) {
            if (smokeExpired()) {
                break;
            }
            continue;
        }

        if (const auto recorded = renderer->record(frame.commandBuffer, *list); !recorded) {
            std::cerr << "monitor: record refused: " << rnd::describe(recorded.error()) << '\n';
            return 1;
        }

        const mx::PresentOutcome ended = window->endFrame();
        if (ended == mx::PresentOutcome::DeviceLost) {
            std::cerr << "monitor: presentation failed\n";
            return 1;
        }
        if (ended == mx::PresentOutcome::OutOfDate) {
            if (!resync()) {
                return 1;
            }
        } else {
            ++presented;
        }

        if (smokeTest && (presented >= smokeFramesRequired || smokeExpired())) {
            break;
        }
    }

    if (smokeTest && presented < smokeFramesRequired) {
        std::cerr << std::format("monitor: smoke test presented {} of {} frames within {}s\n", presented,
                                 smokeFramesRequired, smokeDeadline.count());
        return 1;
    }
    if (smokeTest) {
        std::println("smoke test: presented {} frames", presented);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Deterministic headless smoke (no window): a fixed script through updateMonitor()
// ---------------------------------------------------------------------------

/// A pointer down then up over the centre of `nodeId`'s rectangle.
void clickNode(const ms::ScreenPackage& screen, ms::EventQueue& queue, std::string_view nodeId) {
    const ms::CompiledNode* node = screen.find(nodeId);
    if (node == nullptr) {
        return;
    }
    const core::Px x = node->bounds.x + node->bounds.width / 2;
    const core::Px y = node->bounds.y + node->bounds.height / 2;
    (void)queue.push(ms::PointerEvent{.kind = ms::PointerKind::Down, .x = x, .y = y});
    (void)queue.push(ms::PointerEvent{.kind = ms::PointerKind::Up, .x = x, .y = y});
}

int runHeadlessSmoke(Locale locale) {
    auto bound = BoundScreen::load(locale);
    if (!bound) {
        return 1;
    }
    const core::Extent2D surface = bound->surface();

    mx::DemoState state;
    state.bindField(bound->screen, bound->font);
    if (!state.field) {
        std::cerr << "monitor: the patient-id field did not bind\n";
        return 1;
    }
    mx::MonitorClock clock;

    std::array<ms::InputEvent, ms::maxInputEvents> queueStorage{};
    ms::EventQueue                                 queue{queueStorage};
    ms::PressLatch                                 latch;
    std::uint64_t                                  sequence = 0;

    // Frame 1: start editing the field, type two accepted characters and two refused ones.
    (void)queue.push(ms::KeyEvent{.kind = ms::KeyKind::Down, .key = ms::KeyCode::Commit});
    (void)queue.push(ms::TextEvent{.scalar = U'A'});
    (void)queue.push(ms::TextEvent{.scalar = U'7'});
    (void)queue.push(ms::TextEvent{.scalar = U'a'});   // lowercase: not in the PATIENT-ID charset
    (void)queue.push(ms::TextEvent{.scalar = U'-'});   // punctuation: not in the charset
    const mx::MonitorUpdateOutcome f1 = mx::updateMonitor(queue, false, bound->screen, state, latch, clock, sequence);

    // Frame 2: click the ordinary freeze Button.
    clickNode(bound->screen, queue, mx::kFreezeNode);
    const mx::MonitorUpdateOutcome f2 = mx::updateMonitor(queue, false, bound->screen, state, latch, clock, sequence);

    // Frame 3: click the emergency-halt critical control.
    clickNode(bound->screen, queue, mx::kHaltNode);
    const mx::MonitorUpdateOutcome f3 = mx::updateMonitor(queue, false, bound->screen, state, latch, clock, sequence);

    bool ok = true;
    const auto fieldValue = state.field->value();
    if (!(fieldValue.size() == 2 && fieldValue[0] == U'A' && fieldValue[1] == U'7')) {
        std::cerr << "monitor: the field should hold exactly \"A7\" after two accepted and two refused edits\n";
        ok = false;
    }
    if (f1.refusedEdits != 2) {
        std::cerr << std::format("monitor: expected 2 refused edits, saw {}\n", f1.refusedEdits);
        ok = false;
    }
    if (!f2.buttonSource || *f2.buttonSource != "FREEZE") {
        std::cerr << "monitor: the freeze Button press did not resolve to its 'FREEZE' source\n";
        ok = false;
    }
    if (!f3.criticalAction || f3.criticalAction->event != ms::SystemEvent::TriggerHalt ||
        f3.criticalAction->requirement != "REQ-EM-003") {
        std::cerr << "monitor: the emergency-halt press did not resolve to a traced TriggerHalt ActionTrace\n";
        ok = false;
    }
    // The clock advanced once per updateMonitor() call from 08:00:00, deterministically.
    if (!(clock.now.hour == 8 && clock.now.minute == 0 && clock.now.second == 3)) {
        std::cerr << std::format("monitor: the deterministic clock should read 08:00:03, got {:02}:{:02}:{:02}\n",
                                 clock.now.hour, clock.now.minute, clock.now.second);
        ok = false;
    }
    if (!ok) {
        return 1;
    }

    // Render one frame offscreen from the resolved state and check the painted content.
    auto boot = mx::VulkanBoot::headless();
    if (!boot) {
        std::cerr << "monitor: headless Vulkan boot failed: " << boot.error().what << '\n';
        return 1;
    }
    auto target = rnd::OffscreenTarget::create(boot->device(), boot->physicalDevice(), surface, boot->graphicsFamily());
    if (!target) {
        std::cerr << "monitor: offscreen target failed: " << rnd::describe(target.error()) << '\n';
        return 1;
    }

    rnd::VulkanRenderContext context{};
    context.device           = boot->device();
    context.physicalDevice   = boot->physicalDevice();
    context.renderPass       = target->renderPass();
    context.queue            = boot->graphicsQueue();
    context.queueFamilyIndex = boot->graphicsFamily();
    context.viewport         = surface;

    auto renderer = makeRenderer(context, *bound);
    if (!renderer) {
        std::cerr << "monitor: renderer failed: " << rnd::describe(renderer.error()) << '\n';
        return 1;
    }

    FrameStorage storage{bound->screen.budget};
    auto         list = recordMonitorFrame(*bound, storage, state, clock);
    if (!list) {
        return 1;
    }

    struct Recording {
        rnd::UiRenderer* renderer;
        draw::DrawList*  list;
    } recording{.renderer = &*renderer, .list = &*list};

    const auto pixels = target->renderAndRead(
        boot->graphicsQueue(), kClear,
        [](VkCommandBuffer cb, void* ctx) {
            auto* rec = static_cast<Recording*>(ctx);
            (void)rec->renderer->record(cb, *rec->list);
        },
        &recording);
    if (!pixels) {
        std::cerr << "monitor: offscreen render failed: " << rnd::describe(pixels.error()) << '\n';
        return 1;
    }

    const auto painted = [&](std::string_view nodeId) -> bool {
        const ms::CompiledNode* node = bound->screen.find(nodeId);
        if (node == nullptr) {
            std::cerr << "monitor: the compiled screen has no node '" << nodeId << "'\n";
            return false;
        }
        const core::Px x  = node->bounds.x + node->bounds.width / 2;
        const core::Px y  = node->bounds.y + node->bounds.height / 2;
        const auto     px = target->pixelAt(x, y);
        const bool     hit = px.has_value() && *px != kClear;
        if (!hit) {
            std::cerr << std::format("monitor: node '{}' centre ({},{}) was not painted\n", nodeId, x, y);
        }
        return hit;
    };

    if (!painted("topbar-background") || !painted(mx::kHaltNode) || !painted(mx::kFreezeNode) ||
        !painted("wall-clock") || !painted(mx::kPressureNode) || !painted(mx::kStatusNode)) {
        return 1;
    }

    std::println("headless smoke ({}): field=\"A7\" after 2 refused edits, freeze->'FREEZE', "
                 "emergency-halt->TriggerHalt/REQ-EM-003, clock 08:00:03; the topbar, both controls, "
                 "the clock, the pressure reading and the classifier all drew.",
                 localeTag(locale));
    return 0;
}

// ---------------------------------------------------------------------------
// Deterministic scenario replay (#320): replay a committed .scenario through the same
// updateMonitor() loop the window uses, rendering one offscreen frame per `capture` marker.
// ---------------------------------------------------------------------------

int runReplay(std::string_view scenarioId, Locale locale) {
    if (scenarioId != "endoscope-monitor-basics") {
        std::cerr << "monitor: the only committed scenario is 'endoscope-monitor-basics'\n";
        return 2;
    }
    const ms::CompiledScenario scenario = ms::generated::scenario_endoscope_monitor_basics::package();

    auto bound = BoundScreen::load(locale);
    if (!bound) {
        return 1;
    }
    const core::Extent2D surface = bound->surface();

    auto boot = mx::VulkanBoot::headless();
    if (!boot) {
        std::cerr << "monitor: headless Vulkan boot failed: " << boot.error().what << '\n';
        return 1;
    }
    auto target = rnd::OffscreenTarget::create(boot->device(), boot->physicalDevice(), surface, boot->graphicsFamily());
    if (!target) {
        std::cerr << "monitor: offscreen target failed: " << rnd::describe(target.error()) << '\n';
        return 1;
    }
    rnd::VulkanRenderContext context{};
    context.device           = boot->device();
    context.physicalDevice   = boot->physicalDevice();
    context.renderPass       = target->renderPass();
    context.queue            = boot->graphicsQueue();
    context.queueFamilyIndex = boot->graphicsFamily();
    context.viewport         = surface;
    auto renderer            = makeRenderer(context, *bound);
    if (!renderer) {
        std::cerr << "monitor: renderer failed: " << rnd::describe(renderer.error()) << '\n';
        return 1;
    }

    FrameStorage storage{bound->screen.budget};
    bool         captureFailed = false;

    const auto captureFn = [&](const mx::ScenarioCaptureContext& ctx) {
        auto list = recordMonitorFrame(*bound, storage, ctx.state, ctx.clock);
        if (!list) {
            captureFailed = true;
            return;
        }
        struct Recording {
            rnd::UiRenderer* renderer;
            draw::DrawList*  list;
        } recording{.renderer = &*renderer, .list = &*list};
        const auto pixels = target->renderAndRead(
            boot->graphicsQueue(), kClear,
            [](VkCommandBuffer cb, void* c) {
                auto* rec = static_cast<Recording*>(c);
                (void)rec->renderer->record(cb, *rec->list);
            },
            &recording);
        if (!pixels) {
            std::cerr << std::format("monitor: capture '{}' failed to render: {}\n", ctx.name, rnd::describe(pixels.error()));
            captureFailed = true;
            return;
        }
        std::println("  capture '{}' at frame {}: rendered {}x{} offscreen", ctx.name, ctx.frameIndex, surface.width,
                     surface.height);
    };

    std::array<ms::StepOutcome, ms::maxScenarioExpectations> outcomeStorage{};
    const ms::ReplayReport report =
        mx::replayMonitorScenario(scenario, bound->screen, bound->font, outcomeStorage, captureFn);

    for (const ms::StepOutcome& o : report.outcomes) {
        if (!o.held) {
            std::cerr << std::format("monitor: expectation at step {} ({}) FAILED\n", o.stepIndex, ms::toWire(o.kind));
        }
    }
    if (!report.passed()) {
        std::cerr << std::format("monitor: replay of '{}' failed: {} (step {})\n", scenarioId, ms::describe(report.fault),
                                 report.faultStep);
        return 1;
    }
    if (captureFailed) {
        std::cerr << "monitor: a capture render failed\n";
        return 1;
    }
    std::println("replay '{}' ({}): {} of {} expectations held over {} frames; every capture rendered.", scenarioId,
                 localeTag(locale), report.expectationsHeld, report.outcomes.size(), report.framesRun);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> args{argv + 1, static_cast<std::size_t>(argc > 0 ? argc - 1 : 0)};

    bool             smokeTest = false;
    bool             headless  = false;
    std::string_view replay{};
    Locale           locale = Locale::EnUs;
    for (const char* arg : args) {
        const std::string_view a{arg};
        if (a == "--smoke-test") {
            smokeTest = true;
        } else if (a == "--headless-smoke") {
            headless = true;
        } else if (a.starts_with("--replay=")) {
            replay = a.substr(std::string_view{"--replay="}.size());
        } else if (a == "--locale=fr-FR") {
            locale = Locale::FrFr;
        } else if (a == "--locale=en-US") {
            locale = Locale::EnUs;
        } else {
            std::cerr << "Usage: MedicalScreenMonitorExample [--smoke-test | --headless-smoke | --replay=<scenario-id>] "
                         "[--locale=en-US|fr-FR]\n";
            return 2;
        }
    }

    std::println("MduX {} - {}", mdux::Version::getString(), mdux::Compliance::standards);
    if (!mdux::initialize()) {
        std::cerr << "monitor: mdux::initialize() failed\n";
        return 1;
    }
    const int rc = !replay.empty() ? runReplay(replay, locale)
                   : headless       ? runHeadlessSmoke(locale)
                                    : runWindowed(smokeTest, locale);
    mdux::shutdown();
    return rc;
}
