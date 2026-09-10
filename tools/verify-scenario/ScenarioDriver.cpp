/**
 * @file ScenarioDriver.cpp
 * @brief Replay, capture rendering, obligation enumeration and reconciliation for mdux-verify-scenario.
 *
 * **Not a module.** It links the examples-zone replay glue, which is global-module; a module
 * implementation unit would attach a declaration of it to the module and Clang would reject the
 * linkage mismatch. So this is an ordinary translation unit that `import`s the modules it needs and
 * `#include`s the rest in ordinary code. See `ScenarioDriver.hpp`.
 *
 * ## Everything is read from `generated/` on disk
 *
 * The verifier records the digest of each artifact it read as evidence, so it must render and replay
 * against **those** bytes, not a `constexpr` copy compiled into the tool: it reconstructs the
 * `CompiledScenario` from `scenario.json` with the shared `mdux.tools.scenario` reader, and loads
 * the screen, goldens and the shader / font / text / image packages with `mdux-verify-ui`'s own
 * loaders (`mdux.tools.verify.artifacts`). The executable embeds nothing screen- or
 * scenario-specific.
 */
import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.font.schema;
import mdux.image.schema;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.medui.scenario;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.package;
import mdux.tools.scenario;
import mdux.tools.scenario.script;
import mdux.tools.verify.artifacts;
import mdux.tools.verify.diff;
import mdux.tools.verify.driver;
import mdux.verify;

// After `import std;`, in ordinary code: `<vulkan/vulkan.h>`, the shared headless device, and the
// examples-support replay glue (every entity in these headers is `inline` or a type - no link
// symbol, and no embedded blob or generated module is named).
#include <vulkan/vulkan.h>

#include "HeadlessDevice.hpp"

#include "support/MonitorApp.hpp"
#include "support/ScenarioReplay.hpp"
#include "support/MonitorFrame.hpp"

#include "ScenarioDriver.hpp"

namespace mdux::tools::verify::scenario {
namespace {

namespace cli = mdux::tools::cli;
namespace ms  = mdux::medui;
namespace mv  = mdux::verify;
namespace mx  = mdux::examples;
namespace rnd = mdux::render;
namespace sc  = mdux::tools::scenario;

void report(std::vector<cli::Diagnostic>& diagnostics, const std::filesystem::path& file, std::string code, std::string message, std::string fix = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = file.generic_string(),
                                          .code     = std::move(code),
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fix)});
}

void warn(std::vector<cli::Diagnostic>& diagnostics, const std::filesystem::path& file, std::string code, std::string message) {
    diagnostics.push_back(cli::Diagnostic{.file     = file.generic_string(),
                                          .code     = std::move(code),
                                          .severity = cli::Severity::Warning,
                                          .message  = std::move(message),
                                          .fixHint  = {}});
}

[[nodiscard]] std::string sha256Hex(std::span<const std::byte> bytes) {
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(bytes));
    return std::string{digest.data(), digest.size()};
}

[[nodiscard]] std::filesystem::path normalize(std::filesystem::path path) {
    path = path.lexically_normal();
    while (!path.has_filename()) {
        const std::filesystem::path parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return path;
}

// ---------------------------------------------------------------------------
// Rebuilding a `CompiledScenario` from the committed `scenario.json` on disk.
// ---------------------------------------------------------------------------

/// The `CompiledScenario` the verifier replays, rebuilt from a parsed `Script` rather than from a
/// `constexpr` module - so every value, event coordinate and expectation it checks is the one the
/// committed JSON carries. Spans view `script`, which the caller keeps alive; `steps` and the two
/// name-view vectors are owned here.
struct OwnedScenario {
    sc::Script                    script;
    std::vector<ms::ScenarioStep> steps;
    std::vector<std::string_view> requirementViews;
    std::vector<std::string_view> captureViews;

    explicit OwnedScenario(sc::Script parsed) : script{std::move(parsed)} {
        requirementViews.assign(script.requirements.begin(), script.requirements.end());
        captureViews.assign(script.captureNames.begin(), script.captureNames.end());
        steps.reserve(script.steps.size());
        for (const sc::ScriptStep& s : script.steps) {
            ms::ScenarioStep step{.kind = s.kind, .frames = s.frames};
            switch (s.kind) {
                case ms::StepKind::Pointer: step.pointer = s.pointer; break;
                case ms::StepKind::Key:     step.key     = s.key; break;
                case ms::StepKind::Text:    step.text    = s.text; break;
                case ms::StepKind::Focus:   step.focus   = ms::FocusEvent{.kind = s.focusKind, .nodeId = s.focusNode}; break;
                case ms::StepKind::Capture: step.capture = s.capture; break;
                case ms::StepKind::Expect:
                    step.expect = ms::Expectation{.kind          = s.expect.kind,
                                                  .clock         = s.expect.clock,
                                                  .fieldValue    = std::span<const char32_t>{s.expect.fieldValue.data(), s.expect.fieldValue.size()},
                                                  .fieldHasCaret = s.expect.fieldHasCaret,
                                                  .caret         = s.expect.caret,
                                                  .count         = s.expect.count,
                                                  .nodeId        = s.expect.nodeId,
                                                  .event         = s.expect.event,
                                                  .requirement   = s.expect.requirement,
                                                  .source        = s.expect.source,
                                                  .value         = s.expect.value,
                                                  .statField     = s.expect.statField,
                                                  .flag          = s.expect.flag};
                    break;
                case ms::StepKind::Advance: break;
            }
            steps.push_back(step);
        }
    }
    OwnedScenario(const OwnedScenario&)            = delete;
    OwnedScenario& operator=(const OwnedScenario&) = delete;

    [[nodiscard]] ms::CompiledScenario view() const noexcept {
        return ms::CompiledScenario{.id            = script.id,
                                    .screenId      = script.screenId,
                                    .schemaVersion = script.version,
                                    .pinnedClock   = script.clock,
                                    .sampleSeed    = script.sampleSeed,
                                    .requirements  = requirementViews,
                                    .captureNames  = captureViews,
                                    .steps         = steps};
    }
};

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

/// The command-buffer recorder `OffscreenTarget::renderAndRead()` calls, once, inside the pass.
struct Recording {
    rnd::UiRenderer*               renderer{nullptr};
    const mdux::draw::DrawList*    list{nullptr};
    std::optional<rnd::RenderError> error;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto& recording = *static_cast<Recording*>(context);
    if (auto result = recording.renderer->record(commandBuffer, *recording.list); !result.has_value()) {
        recording.error = result.error();
    }
}

/// The near-black ground every scene-driven node is composited over. The endoscope screen packs
/// those nodes edge to edge with no panel beneath, so `groundFor()` would return exactly this - the
/// offscreen clear colour, which is also what the window presents (`mx::kClear`).
constexpr mdux::core::ColorRgba8 kSceneGround = mx::kClear;

/**
 * @brief Whether `nodeId` names a node whose *content* this scenario drives with live data.
 *
 * A capture frame binds the pressure reading, the classifier state, the `patient-id` field, the
 * `wall-clock` and the ECG trace (`recordMonitorFrame()`). Those nodes render content the static
 * screen gate never bound, so on a capture frame: golden `Bounds` still applies (the node fills its
 * box), golden `ColorHash` does **not** (a NumericDisplay showing "12.0 mmHg" paints digit glyphs
 * whose edges are a legitimate third colour a ground-and-tint blend cannot be), and the gate adds a
 * `RegionPainted` check instead (ADR-021 decision 2).
 */
[[nodiscard]] bool contentIsSceneDriven(const ms::ScreenPackage& screen, std::string_view nodeId) {
    if (nodeId == mx::kPressureNode || nodeId == mx::kStatusNode || nodeId == mx::kPatientNode) {
        return true;
    }
    const ms::CompiledNode* node = screen.find(nodeId);
    return node != nullptr
        && (std::holds_alternative<ms::SignalTraceSpec>(node->payload) || std::holds_alternative<ms::ClockSpec>(node->payload));
}

/// The nodes `contentIsSceneDriven()` selects, in screen order.
[[nodiscard]] std::vector<std::string_view> sceneDrivenNodes(const ms::ScreenPackage& screen) {
    std::vector<std::string_view> ids;
    for (const ms::CompiledNode& node : screen.nodes) {
        if (contentIsSceneDriven(screen, node.id)) {
            ids.push_back(node.id);
        }
    }
    return ids;
}

/// True for a golden `ColorHash` obligation the scenario gate must not raise on a scene-driven node.
[[nodiscard]] bool skipRenderedObligation(const ms::ScreenPackage& screen, std::string_view nodeId, std::string_view check) {
    return check == mv::spell(mv::CvCheck::ColorHash) && contentIsSceneDriven(screen, nodeId);
}

/// The obligation an outcome discharges, as `reconcile()` keys them.
[[nodiscard]] Obligation obligationOf(const Outcome& outcome) {
    return Obligation{.kind       = outcome.kind,
                      .scope      = outcome.scope,
                      .capture    = outcome.capture,
                      .stepIndex  = outcome.stepIndex,
                      .expectKind = outcome.expectKind,
                      .nodeId     = outcome.nodeId,
                      .check      = outcome.check};
}

/// One rendered `Outcome` from a governed `CheckOutcome`, tagged for a capture and locale.
[[nodiscard]] Outcome rendered(std::string capture, std::string scope, const mv::CheckOutcome& checked) {
    return Outcome{.kind       = ObligationKind::Rendered,
                   .scope      = std::move(scope),
                   .capture    = std::move(capture),
                   .stepIndex  = 0,
                   .expectKind = {},
                   .nodeId     = std::string{checked.nodeId},
                   .check      = std::string{checked.check},
                   .held       = checked.held(),
                   .finding    = checked.finding,
                   .profile    = checked.profile};
}

}  // namespace

std::vector<Obligation> enumerateObligations(const ms::CompiledScenario&                scenario,
                                             std::span<const std::string_view>          locales,
                                             const ms::ScreenPackage&                   screen,
                                             std::span<const mv::GoldenEntry>           goldens) {
    std::vector<Obligation> obligations;
    const auto              plan       = mdux::tools::verify::enumerate(screen, goldens);
    const auto              sceneNodes = sceneDrivenNodes(screen);

    for (const std::string_view locale : locales) {
        // binding: one per Expect step.
        for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
            if (scenario.steps[i].kind != ms::StepKind::Expect) {
                continue;
            }
            obligations.push_back(Obligation{.kind       = ObligationKind::Binding,
                                             .scope      = std::string{locale},
                                             .capture    = {},
                                             .stepIndex  = i,
                                             .expectKind = std::string{ms::toWire(scenario.steps[i].expect.kind)},
                                             .nodeId     = {},
                                             .check      = {}});
        }

        for (const std::string_view capture : scenario.captureNames) {
            // rendered: every golden/text check the screen gate enumerates, minus the tint check on
            // a scene-driven node.
            for (const mdux::tools::verify::Obligation& item : plan.obligations) {
                if (item.scope != locale || skipRenderedObligation(screen, item.nodeId, item.check)) {
                    continue;
                }
                obligations.push_back(Obligation{.kind       = ObligationKind::Rendered,
                                                 .scope      = std::string{locale},
                                                 .capture    = std::string{capture},
                                                 .stepIndex  = 0,
                                                 .expectKind = {},
                                                 .nodeId     = item.nodeId,
                                                 .check      = item.check});
            }
            // rendered: a `RegionPainted` check per scene-driven node - it drew *something* (the
            // settled *value* is the binding obligation's, not a rendered check's).
            for (const std::string_view node : sceneNodes) {
                obligations.push_back(Obligation{.kind       = ObligationKind::Rendered,
                                                 .scope      = std::string{locale},
                                                 .capture    = std::string{capture},
                                                 .stepIndex  = 0,
                                                 .expectKind = {},
                                                 .nodeId     = std::string{node},
                                                 .check      = "RegionPainted"});
            }
            // capture: one per declared marker.
            obligations.push_back(Obligation{.kind       = ObligationKind::Capture,
                                             .scope      = std::string{locale},
                                             .capture    = std::string{capture},
                                             .stepIndex  = 0,
                                             .expectKind = {},
                                             .nodeId     = {},
                                             .check      = {}});
        }
    }
    return obligations;
}

RunState reconcile(std::span<const Obligation> obligations, std::vector<Outcome>& outcomes, std::vector<cli::Diagnostic>& diagnostics) {
    RunState    state      = RunState::Passed;
    std::size_t discharged = 0;

    for (const Obligation& obligation : obligations) {
        std::size_t matches = 0;
        bool        held    = false;
        std::string finding;
        for (const Outcome& outcome : outcomes) {
            if (obligationOf(outcome) == obligation) {
                ++matches;
                held    = outcome.held;
                finding = obligation.kind == ObligationKind::Rendered ? std::string{mv::spell(outcome.finding)} : "did not hold";
            }
        }
        discharged += matches;

        const std::string where = std::format("scope '{}', capture '{}', step {}, node '{}', check '{}'",
                                               obligation.scope, obligation.capture, obligation.stepIndex, obligation.nodeId, obligation.check);
        if (matches == 0) {
            // A missing outcome is a failure, not an absence: ADR-021 decision 2, PAR-REQ-003.
            outcomes.push_back(Outcome{.kind       = obligation.kind,
                                       .scope      = obligation.scope,
                                       .capture    = obligation.capture,
                                       .stepIndex  = obligation.stepIndex,
                                       .expectKind = obligation.expectKind,
                                       .nodeId     = obligation.nodeId,
                                       .check      = obligation.check,
                                       .held       = false,
                                       .finding    = mv::Finding::Held,
                                       .profile    = {}});
            report(diagnostics, {}, "VSC010", std::format("{} obligation ({}) produced no outcome", toWire(obligation.kind), where));
            state = RunState::ChecksFailed;
            continue;
        }
        if (matches > 1) {
            report(diagnostics, {}, "VSC011", std::format("{} obligation ({}) has {} outcomes", toWire(obligation.kind), where, matches));
            state = RunState::ChecksFailed;
        }
        if (!held) {
            report(diagnostics, {}, "VSC101", std::format("{} obligation did not hold: {}: {}", toWire(obligation.kind), where, finding));
            state = RunState::ChecksFailed;
        }
    }

    // Every outcome must discharge an enumerated obligation - a surplus means the outcome set does
    // not correspond to the obligation set, and the verdict must not stay `Passed`.
    if (discharged < outcomes.size()) {
        report(diagnostics, {}, "VSC014", std::format("{} outcome(s) discharge no enumerated obligation", outcomes.size() - discharged));
        state = RunState::ChecksFailed;
    }
    return state;
}

namespace {

/// The packages one approved locale's frame is rendered from, with the screen and shader shared
/// across locales.
struct LocaleRender {
    ms::TextBinding  textBinding;
    ms::ImageBinding imageBinding;
    rnd::UiRenderer  renderer;
};

/// Everything the per-capture callback and the post-replay pass share for one locale leg.
struct LegContext {
    RunResult*                      result{nullptr};
    const RunOptions*               options{nullptr};
    const ms::ScreenPackage*        screen{nullptr};
    LocaleRender*                   render{nullptr};
    rnd::OffscreenTarget*           target{nullptr};
    VkQueue                         queue{VK_NULL_HANDLE};
    mv::RenderScope                 scope{mv::RenderScope::localeFree()};
    std::string                     scopeTag;
    std::span<const mv::GoldenEntry> goldens;
    std::span<const std::byte>      atlas;
    std::span<const std::string_view> sceneNodes;
    std::string_view                scenarioId;
    mx::FrameStorage*               storage{nullptr};
    std::set<std::string>           invoked;
    std::set<std::string>           renderFailed;
    bool                            structuralFailure{false};
    std::string                     structuralMessage;
};

/// Renders one capture frame and discharges its rendered obligations. A render or evaluation
/// failure is recorded on the leg and never silently swallowed.
void captureFrame(LegContext& leg, const mx::ScenarioCaptureContext& ctx) {
    const std::string marker{ctx.name};
    leg.invoked.insert(marker);

    auto list = mx::recordMonitorFrame(*leg.screen, leg.render->textBinding, leg.render->imageBinding, *leg.storage, ctx.state, ctx.clock);
    if (!list) {
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008",
               std::format("capture '{}' ({}): the frame could not be recorded: {}", marker, leg.scopeTag, ms::describe(list.error())));
        return;
    }

    Recording recording{.renderer = &leg.render->renderer, .list = &*list, .error = std::nullopt};
    auto      pixels = leg.target->renderAndRead(leg.queue, mx::kClear, recordFrame, &recording);
    if (recording.error.has_value() || !pixels.has_value()) {
        const std::string reason = recording.error.has_value() ? std::string{rnd::describe(*recording.error)} : std::string{rnd::describe(pixels.error())};
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008", std::format("capture '{}' ({}): render/readback failed: {}", marker, leg.scopeTag, reason));
        return;
    }
    ++leg.result->renderCount;

    const auto width  = static_cast<mdux::core::Px>(leg.screen->surfaceWidth);
    const auto height = static_cast<mdux::core::Px>(leg.screen->surfaceHeight);
    const auto frame  = mv::FramebufferView::createPacked(*pixels, width, height);
    if (!frame.has_value()) {
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008",
               std::format("capture '{}' ({}): readback cannot form a framebuffer: {}", marker, leg.scopeTag, mv::describe(frame.error())));
        return;
    }

    // The screen's own golden and mandatory-text obligations, against this frame.
    auto evaluated = mdux::tools::verify::evaluateFrame(*leg.screen, leg.goldens, leg.scope, &leg.render->textBinding, leg.atlas, *frame);
    if (evaluated.failure.has_value()) {
        leg.structuralFailure = true;
        leg.structuralMessage = std::format("capture '{}' ({}): {}", marker, leg.scopeTag, *evaluated.failure);
        return;
    }
    for (const mdux::tools::verify::Outcome& o : evaluated.outcomes) {
        if (skipRenderedObligation(*leg.screen, o.nodeId, o.check)) {
            continue;
        }
        leg.result->outcomes.push_back(Outcome{.kind       = ObligationKind::Rendered,
                                               .scope      = leg.scopeTag,
                                               .capture    = marker,
                                               .stepIndex  = 0,
                                               .expectKind = {},
                                               .nodeId     = o.nodeId,
                                               .check      = o.check,
                                               .held       = o.held(),
                                               .finding    = o.finding,
                                               .profile    = o.profile});
    }

    // `RegionPainted` per scene-driven node: it drew something.
    for (const std::string_view node : leg.sceneNodes) {
        const ms::CompiledNode* compiled = leg.screen->find(node);
        if (compiled == nullptr) {
            continue;
        }
        leg.result->outcomes.push_back(rendered(marker, leg.scopeTag, mv::regionPainted(*frame, compiled->bounds, kSceneGround, node, leg.scope)));
    }

    // Diagnostic attachments (ADR-021 decision 3): the PNG and the per-backend digest. Neither is
    // committed; a failure to write either is a warning, never a verdict.
    leg.result->captureDigests.push_back(
        RunResult::CaptureDigest{.capture = marker, .scope = leg.scopeTag, .sha256 = sha256Hex(std::as_bytes(std::span{pixels->data(), pixels->size()}))});

    if (!leg.options->frameImageDirectory.empty()) {
        std::error_code created;
        std::filesystem::create_directories(leg.options->frameImageDirectory, created);
        if (created) {
            warn(leg.result->diagnostics, leg.options->frameImageDirectory, "VSC009", "cannot create the frame image directory: " + created.message());
        } else {
            const std::filesystem::path path =
                leg.options->frameImageDirectory / std::format("{}.{}.{}.frame.png", leg.scenarioId, marker, leg.scopeTag);
            const auto encoded = mdux::tools::verify::encodePng(*pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
            std::ofstream out{path, std::ios::binary | std::ios::trunc};
            out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
            if (out && !encoded.empty()) {
                leg.result->frameImages.push_back(path);
            } else {
                warn(leg.result->diagnostics, path, "VSC009", "cannot write the frame image");
            }
        }
    }
}

/// Builds the per-locale bindings and renderer from already-loaded, already-authenticated assets.
[[nodiscard]] std::optional<LocaleRender> makeLocaleRender(const ms::ScreenPackage&                    screen,
                                                           const mdux::tools::verify::LocaleAssets&    locale,
                                                           const mdux::tools::verify::ImageAssets*     image,
                                                           const mdux::tools::verify::ShaderAssets&    shader,
                                                           const rnd::VulkanRenderContext&             context,
                                                           std::string&                               error) {
    auto textBinding = ms::TextBinding::create(screen, locale.font, locale.text, std::as_bytes(std::span{locale.textJson}), locale.runs);
    if (!textBinding) {
        error = "text binding refused: " + std::string{ms::describe(textBinding.error())};
        return std::nullopt;
    }
    ms::ImageBinding imageBinding{};
    if (image != nullptr) {
        auto made = ms::ImageBinding::create(screen, image->image, std::as_bytes(std::span{image->imageJson}), image->pixels);
        if (!made) {
            error = "image binding refused: " + std::string{ms::describe(made.error())};
            return std::nullopt;
        }
        imageBinding = *made;
    }

    const auto fontWidth  = locale.font.atlas.width;
    const auto fontHeight = locale.font.atlas.height;
    auto renderer = image != nullptr ? rnd::UiRenderer::createWithAtlases(context, shader.view(), screen.budget, locale.atlas, fontWidth, fontHeight,
                                                                          image->pixels, image->image.width, image->image.height)
                                     : rnd::UiRenderer::createWithCoverageAtlas(context, shader.view(), screen.budget, locale.atlas, fontWidth, fontHeight);
    if (!renderer) {
        error = "renderer creation failed: " + std::string{rnd::describe(renderer.error())};
        return std::nullopt;
    }
    return LocaleRender{.textBinding = *textBinding, .imageBinding = std::move(imageBinding), .renderer = std::move(*renderer)};
}

/// Runs one approved locale: builds its render state, replays, records every outcome. Returns false
/// only on a structural impossibility (it has set `result.state` and reported).
[[nodiscard]] bool runLeg(RunResult&                                result,
                          const RunOptions&                         options,
                          const ms::CompiledScenario&               scenario,
                          const ms::ScreenPackage&                  screen,
                          std::span<const mv::GoldenEntry>          goldens,
                          std::span<const std::string_view>         sceneNodes,
                          std::string_view                          scenarioId,
                          const mdux::tools::verify::LocaleAssets&  locale,
                          const mdux::tools::verify::ImageAssets*   image,
                          const mdux::tools::verify::ShaderAssets&  shader,
                          HeadlessDevice&                           device,
                          rnd::OffscreenTarget&                     target) {
    const std::string scopeTag = locale.locale;

    const rnd::VulkanRenderContext context{.device           = device.device(),
                                           .physicalDevice   = device.physicalDevice(),
                                           .renderPass       = target.renderPass(),
                                           .subpass          = 0,
                                           .queue            = device.queue(),
                                           .queueFamilyIndex = device.family(),
                                           .viewport         = {screen.surfaceWidth, screen.surfaceHeight}};
    std::string localeError;
    auto        render = makeLocaleRender(screen, locale, image, shader, context, localeError);
    if (!render.has_value()) {
        report(result.diagnostics, {}, "VSC007", std::format("locale '{}': {}", scopeTag, localeError));
        result.state = RunState::CouldNotRun;
        return false;
    }

    mx::FrameStorage storage{screen.budget};
    LegContext       leg{.result            = &result,
                         .options           = &options,
                         .screen            = &screen,
                         .render            = &*render,
                         .target            = &target,
                         .queue             = device.queue(),
                         .scope             = mv::RenderScope::forLocale(scopeTag),
                         .scopeTag          = scopeTag,
                         .goldens           = goldens,
                         .atlas             = locale.atlas,
                         .sceneNodes        = sceneNodes,
                         .scenarioId        = scenarioId,
                         .storage           = &storage,
                         .invoked           = {},
                         .renderFailed      = {},
                         .structuralFailure = false,
                         .structuralMessage = {}};

    std::array<ms::StepOutcome, ms::maxScenarioExpectations> outcomeStorage{};
    const ms::ReplayReport                                   replay = mx::replayMonitorScenario(
        scenario, screen, locale.font, outcomeStorage, [&leg](const mx::ScenarioCaptureContext& ctx) { captureFrame(leg, ctx); });

    if (leg.structuralFailure) {
        report(result.diagnostics, {}, "VSC008", leg.structuralMessage);
        result.state = RunState::CouldNotRun;
        return false;
    }
    if (replay.fault == ms::ReplayFault::QueueTooSmall || replay.fault == ms::ReplayFault::OutcomeStorageFull
        || replay.fault == ms::ReplayFault::MalformedScenario) {
        report(result.diagnostics, {}, "VSC005",
               std::format("replay of locale '{}' could not complete: {} (step {})", scopeTag, ms::describe(replay.fault), replay.faultStep));
        result.state = RunState::CouldNotRun;
        return false;
    }

    // binding outcomes: one per Expect step, from the runner's own per-step record.
    for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
        if (scenario.steps[i].kind != ms::StepKind::Expect) {
            continue;
        }
        const ms::StepOutcome* found = nullptr;
        for (const ms::StepOutcome& so : replay.outcomes) {
            if (so.stepIndex == i) {
                found = &so;
                break;
            }
        }
        result.outcomes.push_back(Outcome{.kind       = ObligationKind::Binding,
                                          .scope      = scopeTag,
                                          .capture    = {},
                                          .stepIndex  = i,
                                          .expectKind = std::string{ms::toWire(scenario.steps[i].expect.kind)},
                                          .nodeId     = {},
                                          .check      = {},
                                          .held       = found != nullptr && found->held,
                                          .finding    = mv::Finding::Held,
                                          .profile    = {}});
        if (found == nullptr) {
            report(result.diagnostics, {}, "VSC012", std::format("locale '{}': the replay produced no outcome for expect step {}", scopeTag, i));
        }
    }

    // capture outcomes: one per declared marker.
    for (const std::string_view capture : scenario.captureNames) {
        const std::string marker{capture};
        result.outcomes.push_back(Outcome{.kind       = ObligationKind::Capture,
                                          .scope      = scopeTag,
                                          .capture    = marker,
                                          .stepIndex  = 0,
                                          .expectKind = {},
                                          .nodeId     = {},
                                          .check      = {},
                                          .held       = leg.invoked.contains(marker) && !leg.renderFailed.contains(marker),
                                          .finding    = mv::Finding::Held,
                                          .profile    = {}});
        if (!leg.invoked.contains(marker)) {
            report(result.diagnostics, {}, "VSC013", std::format("locale '{}': declared capture '{}' was never reached by the replay", scopeTag, marker));
        }
    }
    return true;
}

}  // namespace

RunResult run(const std::filesystem::path& scenarioDirectory, const RunOptions& options) {
    RunResult                   result;
    const std::filesystem::path dir          = normalize(scenarioDirectory);
    const auto                  scenarioPath = dir / "scenario.json";
    const auto                  scenarioText = mdux::tools::verify::readText(scenarioPath);
    if (!scenarioText.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC001", "cannot read scenario.json");
        return result;
    }

    // Rebuild the scenario from the committed bytes with the same reader `mdux-scenarioemit` uses -
    // so the replay checks the values, events and expectations *this file* carries, and the digest
    // recorded below is of what was actually replayed.
    std::vector<cli::Diagnostic> scenarioDiagnostics;
    auto parsed = sc::readScenarioDoc(std::as_bytes(std::span{scenarioText->data(), scenarioText->size()}), scenarioPath.generic_string(), scenarioDiagnostics);
    if (!parsed.has_value()) {
        for (auto& diagnostic : scenarioDiagnostics) {
            result.diagnostics.push_back(std::move(diagnostic));
        }
        return result;
    }
    const OwnedScenario        owned{std::move(*parsed)};
    const ms::CompiledScenario scenario = owned.view();
    if (dir.filename() != std::string_view{scenario.id}) {
        report(result.diagnostics, scenarioPath, "VSC002",
               std::format("scenario.json id '{}' does not match its directory '{}'", scenario.id, dir.filename().generic_string()));
        return result;
    }
    if (const auto valid = scenario.validate(); !valid.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC003", std::string{ms::describe(valid.error())});
        return result;
    }
    result.inputs.push_back({.role = "scenarioPackage", .id = std::string{scenario.id}, .locale = {}, .sha256 = sha256Hex(std::as_bytes(std::span{*scenarioText}))});

    // The screen the scenario is scripted against.
    const std::filesystem::path screenDir  = options.artifactRoot / "screen" / std::string{scenario.screenId};
    const auto                  screenText = mdux::tools::verify::readText(screenDir / "package.json");
    if (!screenText.has_value()) {
        report(result.diagnostics, screenDir / "package.json", "VSC004", "cannot read the screen package the scenario names");
        return result;
    }
    auto screenRead = mdux::tools::medui::readPackage(*screenText, (screenDir / "package.json").generic_string());
    if (!screenRead.ok()) {
        for (auto& d : screenRead.diagnostics) {
            result.diagnostics.push_back(std::move(d));
        }
        return result;
    }
    const ms::ScreenPackage screen = screenRead.document.package();
    if (mdux::tools::medui::writePackage(screen) != *screenText || screen.id != scenario.screenId) {
        report(result.diagnostics, screenDir / "package.json", "VSC004", "the screen package is non-canonical or its id does not match the scenario");
        return result;
    }
    result.inputs.push_back({.role = "screenPackage", .id = std::string{scenario.screenId}, .locale = {}, .sha256 = mdux::tools::verify::hexDigest(*screenText)});

    std::string goldensDigest;
    auto        ownedGoldens = mdux::tools::verify::readGoldens(screenDir / "goldens.json", goldensDigest, result.diagnostics);
    if (!ownedGoldens.has_value()) {
        return result;
    }
    result.inputs.push_back({.role = "goldens", .id = std::string{scenario.screenId}, .locale = {}, .sha256 = std::move(goldensDigest)});
    std::vector<mv::GoldenEntry> goldenViews;
    goldenViews.reserve(ownedGoldens->size());
    for (const auto& g : *ownedGoldens) {
        goldenViews.push_back(g.view());
    }

    // The committed shader, and every approved locale's committed text / font packages and the
    // committed image - loaded and authenticated exactly as `mdux-verify-ui` loads them.
    auto shader = mdux::tools::verify::loadShader(options.artifactRoot, result.diagnostics);
    if (!shader.has_value()) {
        return result;
    }
    result.inputs.push_back({.role = "shaderPackage", .id = std::string{shader->package.header.id}, .locale = {}, .sha256 = shader->sha256});

    std::vector<mdux::tools::verify::LocaleAssets> locales;
    locales.reserve(screen.approvedTextPackages.size());
    for (const auto& approval : screen.approvedTextPackages) {
        auto assets = mdux::tools::verify::loadLocale(approval, options.artifactRoot, result.diagnostics);
        if (!assets.has_value()) {
            return result;
        }
        result.inputs.push_back(
            {.role = "textPackage", .id = std::string{assets->text.header.id}, .locale = assets->locale, .sha256 = mdux::tools::verify::hexDigest(assets->textJson)});
        result.inputs.push_back(
            {.role = "fontPackage", .id = std::string{assets->font.id}, .locale = assets->locale, .sha256 = mdux::tools::verify::hexDigest(assets->fontJson)});
        locales.push_back(std::move(*assets));
    }
    if (locales.empty()) {
        report(result.diagnostics, screenDir / "package.json", "VSC004", "the screen the scenario names approves no locale");
        return result;
    }

    // Built only now that `locales` has stopped growing: a view taken during the loop above would
    // dangle when the vector reallocated (and `LocaleAssets::locale`'s SSO buffer moved with it).
    std::vector<std::string_view> localeTags;
    localeTags.reserve(locales.size());
    for (const auto& asset : locales) {
        localeTags.push_back(asset.locale);
    }

    std::optional<mdux::tools::verify::ImageAssets> image;
    if (!screen.approvedImagePackages.empty()) {
        if (screen.approvedImagePackages.size() != 1) {
            report(result.diagnostics, screenDir / "package.json", "VSC004", "the scenario verifier supports exactly one approved image package");
            return result;
        }
        image = mdux::tools::verify::loadImage(screen.approvedImagePackages.front(), options.artifactRoot, result.diagnostics);
        if (!image.has_value()) {
            return result;
        }
        result.inputs.push_back(
            {.role = "imagePackage", .id = std::string{image->image.header.id}, .locale = {}, .sha256 = mdux::tools::verify::hexDigest(image->imageJson)});
    }

    const auto sceneNodes = sceneDrivenNodes(screen);
    result.obligations    = enumerateObligations(scenario, localeTags, screen, goldenViews);

    HeadlessDevice device;
    if (!device.available()) {
        report(result.diagnostics, scenarioPath, "VSC005", "verification run could not be made: " + std::string{device.reason()},
               "Install a Vulkan 1.3 implementation; automatic Linux legs use lavapipe and macOS uses MoltenVK.");
        result.state = RunState::NoRenderDevice;
        return result;
    }
    result.backend = device.backendName();

    const mdux::core::Extent2D extent{.width = screen.surfaceWidth, .height = screen.surfaceHeight};
    auto                       target = rnd::OffscreenTarget::create(device.device(), device.physicalDevice(), extent, device.family());
    if (!target.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC007", "verification run could not create its offscreen target: " + std::string{rnd::describe(target.error())});
        return result;
    }

    for (const auto& locale : locales) {
        if (!runLeg(result, options, scenario, screen, goldenViews, sceneNodes, scenario.id, locale, image.has_value() ? &*image : nullptr, *shader, device,
                    *target)) {
            return result;
        }
    }

    result.state = reconcile(result.obligations, result.outcomes, result.diagnostics);

    if (!options.captureDigestPath.empty() && !result.captureDigests.empty()) {
        std::error_code created;
        std::filesystem::create_directories(options.captureDigestPath.parent_path(), created);
        std::ofstream out{options.captureDigestPath, std::ios::binary | std::ios::trunc};
        out << "# mdux-verify-scenario capture digests - diagnostic, backend-specific, NOT committed evidence (ADR-021 D3)\n";
        out << "# backend: " << result.backend << "\n";
        for (const auto& d : result.captureDigests) {
            out << d.capture << '\t' << d.scope << '\t' << d.sha256 << '\n';
        }
        if (!out) {
            warn(result.diagnostics, options.captureDigestPath, "VSC009", "cannot write the capture digest manifest");
        }
    }
    return result;
}

RunResult run(const std::filesystem::path& scenarioDirectory) {
    const std::filesystem::path normalized = normalize(scenarioDirectory);
    return run(normalized, RunOptions{.artifactRoot = normalized.parent_path().parent_path(), .frameImageDirectory = {}, .captureDigestPath = {}});
}

std::string usage() {
    return std::format("usage:\n  {} --scenario=<generated/scenario/id> --locales=all [--format=json|text]\n"
                       "  {:{}}  [--frame-image-dir=<dir>] [--capture-digest-out=<file>]\n\n"
                       "Replays the committed scenario through the application's real update path and verifies\n"
                       "every capture frame's golden and text obligations in every approved locale, plus the\n"
                       "settled binding values against the scenario's pinned expectations. The locale manifest\n"
                       "cannot be narrowed.\n\n"
                       "--frame-image-dir writes <scenario>.<capture>.<locale>.frame.png per capture: a\n"
                       "diagnostic attachment, never committed evidence.\n\n"
                       "--capture-digest-out writes a per-backend rgba8-sha256 manifest: the backend-specific\n"
                       "baseline PAR-REQ-009 keeps out of the byte-compared bundle.\n",
                       toolName,
                       "",
                       toolName.size());
}

Invocation parseArguments(std::span<const std::string_view> arguments) {
    Invocation result;
    bool       scenarioSeen = false;
    bool       localesSeen  = false;
    bool       frameSeen    = false;
    bool       digestSeen   = false;
    for (std::string_view argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            throw cli::UsageError{usage()};
        }
        if (argument == "--format=json") {
            result.format = cli::Format::Json;
            continue;
        }
        if (argument == "--format=text") {
            result.format = cli::Format::Text;
            continue;
        }
        if (argument.starts_with("--scenario=")) {
            constexpr std::string_view flag = "--scenario=";
            if (scenarioSeen || argument.size() == flag.size()) {
                throw cli::UsageError{"--scenario must occur once with a non-empty bundle path\n\n" + usage()};
            }
            result.scenarioDirectory = normalize(std::filesystem::path{argument.substr(flag.size())});
            scenarioSeen             = true;
            continue;
        }
        if (argument == "--locales=all") {
            if (localesSeen) {
                throw cli::UsageError{"--locales=all must occur exactly once\n\n" + usage()};
            }
            localesSeen = true;
            continue;
        }
        if (argument.starts_with("--locales=")) {
            throw cli::UsageError{"locale selection cannot narrow the approved manifest; use --locales=all\n\n" + usage()};
        }
        if (argument.starts_with("--frame-image-dir=")) {
            constexpr std::string_view flag = "--frame-image-dir=";
            if (frameSeen || argument.size() == flag.size()) {
                throw cli::UsageError{"--frame-image-dir must occur at most once with a non-empty directory\n\n" + usage()};
            }
            result.frameImageDirectory = std::filesystem::path{argument.substr(flag.size())};
            frameSeen                  = true;
            continue;
        }
        if (argument.starts_with("--capture-digest-out=")) {
            constexpr std::string_view flag = "--capture-digest-out=";
            if (digestSeen || argument.size() == flag.size()) {
                throw cli::UsageError{"--capture-digest-out must occur at most once with a non-empty path\n\n" + usage()};
            }
            result.captureDigestPath = std::filesystem::path{argument.substr(flag.size())};
            digestSeen               = true;
            continue;
        }
        throw cli::UsageError{"unrecognized argument '" + std::string{argument} + "'\n\n" + usage()};
    }
    if (!scenarioSeen || !localesSeen) {
        throw cli::UsageError{"both --scenario=<bundle> and --locales=all are required\n\n" + usage()};
    }
    return result;
}

Invocation parseArguments(int argc, const char* const* argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return parseArguments(arguments);
}

}  // namespace mdux::tools::verify::scenario
