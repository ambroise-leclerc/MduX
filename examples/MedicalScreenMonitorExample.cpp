/**
 * @file MedicalScreenMonitorExample.cpp
 * @brief Presents the committed `endoscope-monitor` screen in a window and routes real pointer and
 *        keyboard input through the bounded `mdux.medui.input` contract (#317, ADR-019).
 *
 * @compliance IEC 62304 Class B - Medical Device Example
 * @compliance ADR-019 The windowed presentation and input adapter
 *
 * What this shows:
 *
 * - `examples/support/GlfwPresentationAdapter.hpp` owns the window, the Vulkan instance / device /
 *   swapchain and the native-event translation. `mdux::render::UiRenderer` draws the compiled screen
 *   through the borrowed `VulkanRenderContext` unchanged.
 * - A left-click resolved to the `emergency-halt` control arms a `PressLatch`; the release on the
 *   same control activates it, and this example prints an `ActionTrace` (node, requirement,
 *   `SystemEvent`, monotonic sequence) and **executes nothing** — the host owns the halt path
 *   (ADR-018 clause 7).
 * - Typing routes through a `FieldEditor` bound to `patient-id`, whose `charset:` admits digits and
 *   `A`-`Z` only; a lowercase letter is refused with no mutation.
 * - A resize recreates the swapchain, rebuilds the `SurfaceMapping` and cancels the latch; a focus
 *   loss cancels the latch; a dropped event cancels the latch.
 *
 * This example does not assemble the input -> update -> bind -> render loop as a reusable contract:
 * that is ADR-018 clause 6 and #318. It drains one batch per frame and acts on it inline.
 *
 * Modes:
 *   (no args)          interactive
 *   --smoke-test       present a few frames then exit; non-zero on a deadline or a Vulkan failure
 *   --headless-frame   render one frame offscreen (no window) and check the control rectangles
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
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.render.vulkan;
import mdux.render.offscreen;
import mdux.shader.generated.mdux_ui;
import mdux.shader.schema;

#include "support/GlfwPresentationAdapter.hpp"

#include "brandMarkPackageJson.hpp"
#include "brandMarkPixels.hpp"
#include "dejavuUiAtlas.hpp"
#include "dejavuUiPackageJson.hpp"
#include "endoscopeTextPackageJson.hpp"
#include "endoscopeTextRuns.hpp"

namespace {

namespace core = mdux::core;
namespace draw = mdux::draw;
namespace ms   = mdux::medui;
namespace mx   = mdux::examples;
namespace rnd  = mdux::render;

constexpr std::string_view kHaltNode    = "emergency-halt";
constexpr std::string_view kPatientNode = "patient-id";

/// An embedded blob's bytes as UTF-8 text, for the `*Package::parse()` calls.
[[nodiscard]] std::string_view asText(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// The compiled screen and the committed packages a device joins it to at start-up.
///
/// Held on the heap for the life of the program and never moved: `TextBinding` / `ImageBinding`
/// keep pointers into `font` / `text` / `image`, so this struct is self-referential and a move
/// would dangle them. `load()` returns a `unique_ptr` for exactly that reason.
struct BoundScreen {
    ms::ScreenPackage        screen{ms::generated::screen_endoscope_monitor::package()};
    mdux::font::FontPackage   font{};
    mdux::text::TextPackage   text{};
    mdux::image::ImagePackage image{};
    ms::TextBinding           textBinding{};
    ms::ImageBinding          imageBinding{};

    BoundScreen()                              = default;
    BoundScreen(const BoundScreen&)            = delete;
    BoundScreen& operator=(const BoundScreen&) = delete;

    /// The authored surface extent the screen was compiled for — the `UiRenderer` viewport.
    [[nodiscard]] core::Extent2D surface() const noexcept {
        return {screen.surfaceWidth, screen.surfaceHeight};
    }

    /// Parses the embedded font / text / image packages and builds the two bindings, or prints why
    /// and returns `nullptr`. Heap-allocated because the bindings hold pointers into this struct.
    [[nodiscard]] static std::unique_ptr<BoundScreen> load() {
        auto bound = std::make_unique<BoundScreen>();

        auto font  = mdux::font::FontPackage::parse(asText(dejavuUiPackageJson()));
        auto text  = mdux::text::TextPackage::parse(asText(endoscopeTextPackageJson()));
        auto image = mdux::image::ImagePackage::parse(asText(brandMarkPackageJson()));
        if (!font || !text || !image) {
            std::cerr << "monitor: a committed package did not parse\n";
            return nullptr;
        }
        bound->font  = std::move(*font);
        bound->text  = std::move(*text);
        bound->image = std::move(*image);

        auto textBinding = ms::TextBinding::create(bound->screen, bound->font, bound->text,
                                                   endoscopeTextPackageJson(), endoscopeTextRuns());
        if (!textBinding) {
            std::cerr << "monitor: the committed text artifacts were refused: "
                      << ms::describe(textBinding.error()) << '\n';
            return nullptr;
        }
        bound->textBinding = *textBinding;

        auto imageBinding = ms::ImageBinding::create(bound->screen, bound->image, brandMarkPackageJson(),
                                                     brandMarkPixels());
        if (!imageBinding) {
            std::cerr << "monitor: the committed image artifacts were refused: "
                      << ms::describe(imageBinding.error()) << '\n';
            return nullptr;
        }
        bound->imageBinding = *imageBinding;
        return bound;
    }
};

/// Storage a caller sizes once from the screen's own budget, as a device would.
struct FrameStorage {
    std::vector<draw::UiVertex>   vertices;
    std::vector<draw::Index>      indices;
    std::vector<draw::DrawCommand> commands;

    explicit FrameStorage(const draw::DrawBudget& budget)
        : vertices(budget.maxVertices), indices(budget.maxIndices), commands(budget.maxCommands) {}
};

/// The caller-owned editing state for `patient-id`, plus the char32_t buffer it edits in place.
struct FieldState {
    std::array<char32_t, 64> buffer{};
    std::optional<ms::FieldEditor> editor{};

    /// Creates the `FieldEditor` for the `patient-id` `TextInput`, over `buffer`, bounded by the
    /// font charset and the node's `charset:`. Leaves `editor` empty if the node is absent.
    void bind(const BoundScreen& bound) {
        const ms::CompiledNode* node = bound.screen.find(kPatientNode);
        if (node == nullptr) {
            return;
        }
        const auto* spec = std::get_if<ms::TextInputSpec>(&node->payload);
        if (spec == nullptr) {
            return;
        }
        const std::size_t maxLength = spec->maxLength < 0 ? 0 : static_cast<std::size_t>(spec->maxLength);
        auto made = ms::FieldEditor::create(kPatientNode, buffer, {}, bound.font.restrictedCharset,
                                            spec->charsetRanges, maxLength);
        if (made) {
            editor = *made;
        }
    }
};

/// One application update: drain the batch, act on it, and return whether a critical action was
/// resolved this frame (for the caller to log).
struct UpdateResult {
    std::optional<ms::ActionTrace> action{};
};

/// One application update (not the assembled ADR-018 clause-6 loop — that is #318): drains the
/// queue, routes pointers through `PressLatch` + `resolvePress` and keys/text through the
/// `FieldEditor`, and returns the `ActionTrace` if the `emergency-halt` control was activated.
[[nodiscard]] UpdateResult applyBatch(ms::EventQueue& queue, const BoundScreen& bound, ms::PressLatch& latch,
                                      FieldState& field, std::uint64_t& sequence) {
    UpdateResult result;

    while (const auto event = queue.pop()) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, ms::PointerEvent>) {
                    if (value.kind == ms::PointerKind::Cancel) {
                        latch.cancel();
                        return;
                    }
                    const auto press = ms::resolvePress(bound.screen, value.x, value.y);
                    const std::string_view node =
                        (press && press->has_value()) ? (*press)->nodeId : std::string_view{};
                    if (value.kind == ms::PointerKind::Down) {
                        latch.arm(node);
                    } else if (value.kind == ms::PointerKind::Up) {
                        if (latch.release(node) && node == kHaltNode && press && press->has_value() &&
                            (*press)->event.has_value()) {
                            result.action = ms::ActionTrace{.nodeId      = (*press)->nodeId,
                                                            .requirement = (*press)->requirement,
                                                            .event       = *(*press)->event,
                                                            .sequence    = ++sequence};
                        }
                    }
                } else if constexpr (std::is_same_v<T, ms::FocusEvent>) {
                    if (field.editor) {
                        field.editor->focus(value);
                    }
                } else if constexpr (std::is_same_v<T, ms::KeyEvent>) {
                    if (!field.editor) {
                        return;
                    }
                    // Enter-as-Commit here means "start editing the patient id": this example has
                    // one field and no real focus traversal, so Commit/FocusNext both mean focus it.
                    if (value.kind == ms::KeyKind::Down &&
                        (value.key == ms::KeyCode::FocusNext || value.key == ms::KeyCode::Commit)) {
                        field.editor->focus(ms::FocusEvent{.kind = ms::FocusKind::Enter, .nodeId = kPatientNode});
                        return;
                    }
                    if (value.kind == ms::KeyKind::Down && value.key == ms::KeyCode::Cancel) {
                        field.editor->focus(ms::FocusEvent{.kind = ms::FocusKind::Leave, .nodeId = kPatientNode});
                        return;
                    }
                    if (const auto handled = field.editor->handleKey(value); !handled) {
                        std::cerr << "monitor: edit refused: " << ms::describe(handled.error()) << '\n';
                    }
                } else if constexpr (std::is_same_v<T, ms::TextEvent>) {
                    if (field.editor) {
                        if (const auto handled = field.editor->handleText(value); !handled) {
                            std::cerr << "monitor: '" << static_cast<std::uint32_t>(value.scalar)
                                      << "' refused: " << ms::describe(handled.error()) << '\n';
                        }
                    }
                }
            },
            *event);
    }
    return result;
}

/// Builds this frame's `DrawList` from the screen and the current field value.
[[nodiscard]] mdux::core::Result<draw::DrawList, ms::ScreenError> recordScreen(const BoundScreen& bound,
                                                                               FrameStorage& storage,
                                                                               const FieldState& field) {
    auto list = draw::DrawList::create(storage.vertices, storage.indices, storage.commands, bound.screen.budget);
    if (!list) {
        std::cerr << "monitor: draw list refused: " << draw::describe(list.error()) << '\n';
        return mdux::core::err(ms::ScreenError::BudgetExhausted);
    }

    std::array<ms::TextInputSlot, 1> slots{};
    ms::TextInputBinding             inputs{};
    if (field.editor) {
        slots[0] = ms::TextInputSlot{.nodeId = field.editor->nodeId(),
                                     .text   = field.editor->value(),
                                     .caret  = field.editor->caret()};
        if (auto made = ms::TextInputBinding::create(bound.screen, slots); made) {
            inputs = *made;
        }
    }

    const auto recorded = ms::render(bound.screen, *list, bound.textBinding, bound.imageBinding, {}, {}, {}, inputs);
    if (!recorded) {
        std::cerr << "monitor: render refused: " << ms::describe(recorded.error()) << '\n';
        return mdux::core::err(recorded.error());
    }
    return std::move(*list);
}

/// A `UiRenderer` for `context` with the committed font coverage atlas and the brand-mark image
/// atlas, so text and the logo draw as themselves rather than white blocks.
[[nodiscard]] mdux::core::Result<rnd::UiRenderer, rnd::RenderError> makeRenderer(
    const rnd::VulkanRenderContext& context, const BoundScreen& bound) {
    return rnd::UiRenderer::createWithAtlases(context, mdux::shader::generated::mdux_ui::package(),
                                              bound.screen.budget, dejavuUiAtlas(), bound.font.atlas.width,
                                              bound.font.atlas.height, brandMarkPixels(), bound.image.width,
                                              bound.image.height);
}

/// The near-black ground the screen is composited over.
constexpr core::ColorRgba8 kClear{.r = 6, .g = 8, .b = 10, .a = 255};

// ---------------------------------------------------------------------------
// Interactive / smoke-test mode
// ---------------------------------------------------------------------------

/// Opens a window, presents the screen, and routes input until the window closes (or, under
/// `--smoke-test`, until N frames are presented). Returns a process exit code.
int runWindowed(bool smokeTest) {
    auto bound = BoundScreen::load();
    if (!bound) {
        return 1;
    }
    const core::Extent2D surface = bound->surface();

    auto window = mx::GlfwWindow::create(surface.width, surface.height, "MduX - Medical Screen Monitor");
    if (!window) {
        return 1;  // GlfwWindow::create already explained the failure on stderr
    }

    rnd::VulkanRenderContext context = window->renderContext();
    context.viewport                 = surface;
    auto renderer                    = makeRenderer(context, *bound);
    if (!renderer) {
        std::cerr << "monitor: could not create the renderer: " << rnd::describe(renderer.error()) << '\n';
        return 1;
    }

    // The renderer holds buffers, descriptors and a pipeline the GPU may still be reading from the
    // last submitted frame. It is destroyed before `window` (declaration order) and only `window`'s
    // own teardown waits for the device, so idle it here first — on every return path, including the
    // error exits below. Declared right after `renderer` so it runs immediately before it.
    struct IdleBeforeRendererTeardown {
        const mx::GlfwWindow* window;
        ~IdleBeforeRendererTeardown() { window->waitIdle(); }
    } const idleGuard{&*window};

    FieldState field;
    field.bind(*bound);

    std::array<ms::InputEvent, ms::maxInputEvents> queueStorage{};
    ms::EventQueue                                 queue{queueStorage};
    ms::PressLatch                                 latch;
    std::uint64_t                                  sequence = 0;

    const auto buildMapping = [&] {
        return ms::SurfaceMapping::create(window->framebufferExtent(), window->windowExtent());
    };
    auto mapping = buildMapping();
    if (!mapping) {
        std::cerr << "monitor: the framebuffer-to-window ratio is not one this adapter supports "
                     "(a per-axis-different DPI, or one past maxCoordinateScale)\n";
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

    // The one resync path (ADR-019 clause 4): rebuild the swapchain, rebuild and revalidate the
    // SurfaceMapping from the new framebuffer extent, and cancel any armed press. A mapping that no
    // longer validates is fatal here just as it is at start-up — otherwise every later click would
    // hit-test with a stale ratio and no diagnostic. `false` means exit non-zero.
    const auto resync = [&]() -> bool {
        if (!window->recreateSwapchain()) {
            return false;
        }
        // A minimised window reports a zero framebuffer, `recreateSwapchain()` skips the rebuild,
        // and there is nothing to map. Stay disarmed and retry when the window comes back — a zero
        // extent is not a DPI failure.
        const core::Extent2D fb  = window->framebufferExtent();
        const core::Extent2D win = window->windowExtent();
        if (fb.width == 0 || fb.height == 0 || win.width == 0 || win.height == 0) {
            latch.cancel();
            return true;
        }
        auto rebuilt = buildMapping();
        if (!rebuilt) {
            std::cerr << "monitor: after the resize the framebuffer-to-window ratio is no longer one "
                         "this adapter supports (a per-axis-different DPI, or one past maxCoordinateScale)\n";
            return false;
        }
        pump.setMapping(*rebuilt);
        latch.cancel();
        return true;
    };

    std::println("Monitor running. Click the red halt control; type a patient id (digits, A-Z).");
    std::println("Press Esc to stop editing, close the window to exit.");

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

        UpdateResult update{};
        if (pump.takeOverflow()) {
            // The batch is not a complete record of what the operator did (ADR-018 clause 2).
            // Discard it whole and stay disarmed — acting on what did arrive could let a queued Down
            // re-arm the control the overflow was meant to cancel, and a queued Up then activate it.
            // The frame still renders, so the display does not freeze.
            queue.clear();
            latch.cancel();
            std::println("(input overflowed - dropped a partial batch)");
        } else {
            update = applyBatch(queue, *bound, latch, field, sequence);
        }
        if (update.action) {
            std::println("ActionTrace #{}: node='{}' requirement='{}' event={} - the host executes this, not MduX",
                         update.action->sequence, update.action->nodeId, update.action->requirement,
                         ms::toWire(update.action->event));
        }

        auto list = recordScreen(*bound, storage, field);
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
// Headless one-frame content check
// ---------------------------------------------------------------------------

/// Renders one frame through `mdux.render.offscreen` (no window) and asserts the topbar and the
/// halt control were painted where the compiled screen places them. Returns a process exit code.
int runHeadlessFrame() {
    auto bound = BoundScreen::load();
    if (!bound) {
        return 1;
    }
    const core::Extent2D surface = bound->surface();

    auto boot = mx::VulkanBoot::headless();
    if (!boot) {
        std::cerr << "monitor: headless Vulkan boot failed: " << boot.error().what << '\n';
        return 1;
    }

    auto target = rnd::OffscreenTarget::create(boot->device(), boot->physicalDevice(), surface,
                                               boot->graphicsFamily());
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

    FieldState   field;
    field.bind(*bound);
    FrameStorage storage{bound->screen.budget};
    auto         list = recordScreen(*bound, storage, field);
    if (!list) {
        return 1;
    }

    /// Context for the C-style `RecordCommands` callback `renderAndRead` takes.
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

    /// Whether the centre of `nodeId`'s rectangle is painted (not the clear colour).
    const auto painted = [&](std::string_view nodeId) -> bool {
        const ms::CompiledNode* node = bound->screen.find(nodeId);
        if (node == nullptr) {
            std::cerr << "monitor: the compiled screen has no node '" << nodeId << "'\n";
            return false;
        }
        const core::Px x = node->bounds.x + node->bounds.width / 2;
        const core::Px y = node->bounds.y + node->bounds.height / 2;
        const auto     px = target->pixelAt(x, y);
        const bool     ok = px.has_value() && *px != kClear;
        if (!ok) {
            std::cerr << std::format("monitor: node '{}' centre ({},{}) was not painted\n", nodeId, x, y);
        }
        return ok;
    };

    if (!painted("topbar-background") || !painted(kHaltNode)) {
        return 1;
    }
    std::println("headless frame: the topbar and the halt control were drawn where the compiled screen places them");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> args{argv + 1, static_cast<std::size_t>(argc > 0 ? argc - 1 : 0)};

    bool smokeTest = false;
    bool headless  = false;
    for (const char* arg : args) {
        const std::string_view a{arg};
        if (a == "--smoke-test") {
            smokeTest = true;
        } else if (a == "--headless-frame") {
            headless = true;
        } else {
            std::cerr << "Usage: MedicalScreenMonitorExample [--smoke-test | --headless-frame]\n";
            return 2;
        }
    }

    std::println("MduX {} - {}", mdux::Version::getString(), mdux::Compliance::standards);
    if (!mdux::initialize()) {
        std::cerr << "monitor: mdux::initialize() failed\n";
        return 1;
    }
    const int rc = headless ? runHeadlessFrame() : runWindowed(smokeTest);
    mdux::shutdown();
    return rc;
}
