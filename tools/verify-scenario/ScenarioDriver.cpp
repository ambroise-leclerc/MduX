/**
 * @file ScenarioDriver.cpp
 * @brief Replay, capture rendering, obligation enumeration and reconciliation for mdux-verify-scenario.
 */
module;

#include <vulkan/vulkan.h>

// Shared with the screen driver (#253): the Vulkan 1.3 headless bring-up. A plain header, included
// in the global module fragment so it is not attached to this module - see the header.
#include "HeadlessDevice.hpp"

module mdux.tools.verify.scenario.driver;

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
import mdux.medui.generated.screen_endoscope_monitor;
import mdux.medui.generated.scenario_endoscope_monitor_basics;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.generated.mdux_ui;
import mdux.shader.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.package;
import mdux.tools.verify.diff;
import mdux.tools.verify.driver;
import mdux.verify;

// The examples-support replay glue this host tool deliberately links (ADR-021 decision 1). The
// embedded-blob headers come first: `BoundScreen::load()` names their accessors.
#include "brandMarkPackageJson.hpp"
#include "brandMarkPixels.hpp"
#include "dejavuUiAtlas.hpp"
#include "dejavuUiPackageJson.hpp"
#include "endoscopeTextEnUsPackageJson.hpp"
#include "endoscopeTextEnUsRuns.hpp"
#include "endoscopeTextFrFrPackageJson.hpp"
#include "endoscopeTextFrFrRuns.hpp"

#include "support/MonitorApp.hpp"
#include "support/ScenarioReplay.hpp"
#include "support/MonitorFrame.hpp"

namespace mdux::tools::verify::scenario {
namespace {

namespace cli = mdux::tools::cli;
namespace ms  = mdux::medui;
namespace mv  = mdux::verify;
namespace mx  = mdux::examples;
namespace rnd = mdux::render;

/// The `constexpr` scenario this build verifies. The tool is single-scenario, as
/// `MedicalScreenMonitorExample --replay` is - a second one adds one generated-module file set and
/// one `run()` branch (ADR-021, Consequences).
namespace generatedScenario = mdux::medui::generated::scenario_endoscope_monitor_basics;

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

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        file.read(text.data(), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return text;
}

/// Lowercase-hex SHA-256, the spelling every other evidence record uses.
[[nodiscard]] std::string hexDigest(std::span<const std::byte> bytes) {
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(bytes));
    return std::string{digest.data(), digest.size()};
}
[[nodiscard]] std::string hexDigest(std::string_view text) {
    return hexDigest(std::as_bytes(std::span{text.data(), text.size()}));
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

/// The command-buffer recorder `OffscreenTarget::renderAndRead()` calls, once, inside the pass.
struct Recording {
    rnd::UiRenderer*         renderer{nullptr};
    const mdux::draw::DrawList* list{nullptr};
    std::optional<rnd::RenderError> error;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto& recording = *static_cast<Recording*>(context);
    if (auto result = recording.renderer->record(commandBuffer, *recording.list); !result.has_value()) {
        recording.error = result.error();
    }
}

/**
 * @brief Whether `node` is a golden node whose *content* this scenario drives with live data.
 *
 * A capture frame binds the pressure reading, the classifier state, the `patient-id` field and the
 * ECG trace (`recordMonitorFrame()`), so those nodes render content the static screen gate never
 * bound. Golden `Bounds` still applies - the node fills its declared box - but golden `ColorHash`
 * does not: the static baseline verified a NumericDisplay's default face, and a NumericDisplay
 * showing "12.0 mmHg" paints digit glyphs whose edges are a legitimate third colour a
 * ground-and-tint blend cannot be. ADR-021 decision 2: the rendered obligations are the screen's,
 * minus the tint check where the interaction changed the content.
 */
[[nodiscard]] bool contentIsSceneDriven(const ms::ScreenPackage& screen, std::string_view nodeId) {
    if (nodeId == mx::kPressureNode || nodeId == mx::kStatusNode || nodeId == mx::kPatientNode) {
        return true;
    }
    const ms::CompiledNode* node = screen.find(nodeId);
    return node != nullptr && std::holds_alternative<ms::SignalTraceSpec>(node->payload);
}

/// True for a rendered obligation the scenario gate must not raise on a scene-driven node.
[[nodiscard]] bool skipRenderedObligation(const ms::ScreenPackage& screen, std::string_view nodeId, std::string_view check) {
    return check == mdux::verify::spell(mdux::verify::CvCheck::ColorHash) && contentIsSceneDriven(screen, nodeId);
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

}  // namespace

std::vector<Obligation> enumerateObligations(const ms::CompiledScenario&               scenario,
                                             std::span<const std::string_view>         locales,
                                             const ms::ScreenPackage&                  screen,
                                             std::span<const mv::GoldenEntry>          goldens) {
    std::vector<Obligation> obligations;

    // The rendered obligations for one frame: exactly what the screen gate enumerates for this
    // screen, minus the render-scope loop (this driver scopes per capture per locale instead).
    const mdux::tools::verify::PlanResult plan = mdux::tools::verify::enumerate(screen, goldens);

    for (const std::string_view locale : locales) {
        // binding: one per Expect step
        for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
            const ms::ScenarioStep& step = scenario.steps[i];
            if (step.kind != ms::StepKind::Expect) {
                continue;
            }
            obligations.push_back(Obligation{.kind       = ObligationKind::Binding,
                                             .scope      = std::string{locale},
                                             .capture    = {},
                                             .stepIndex  = i,
                                             .expectKind = std::string{ms::toWire(step.expect.kind)},
                                             .nodeId     = {},
                                             .check      = {}});
        }

        // rendered: every golden/text check, per declared capture (minus the tint check on a node
        // whose content the scenario drives - see `contentIsSceneDriven()`).
        for (const std::string_view capture : scenario.captureNames) {
            for (const mdux::tools::verify::Obligation& item : plan.obligations) {
                if (item.scope != locale) {
                    continue;  // `enumerate()` already produced one obligation per approved locale
                }
                if (skipRenderedObligation(screen, item.nodeId, item.check)) {
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
        }

        // capture: one per declared marker
        for (const std::string_view capture : scenario.captureNames) {
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
    RunState state = RunState::Passed;

    for (const Obligation& obligation : obligations) {
        std::size_t matches = 0;
        bool        held    = false;
        for (const Outcome& outcome : outcomes) {
            if (obligationOf(outcome) == obligation) {
                ++matches;
                held = outcome.held;
            }
        }
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
            report(diagnostics,
                   {},
                   "VSC010",
                   std::format("{} obligation (scope '{}', capture '{}', step {}, node '{}', check '{}') produced no outcome",
                               toWire(obligation.kind),
                               obligation.scope,
                               obligation.capture,
                               obligation.stepIndex,
                               obligation.nodeId,
                               obligation.check));
            state = RunState::ChecksFailed;
            continue;
        }
        if (matches > 1) {
            report(diagnostics,
                   {},
                   "VSC011",
                   std::format("{} obligation (scope '{}', capture '{}', step {}, node '{}', check '{}') has {} outcomes",
                               toWire(obligation.kind),
                               obligation.scope,
                               obligation.capture,
                               obligation.stepIndex,
                               obligation.nodeId,
                               obligation.check,
                               matches));
            state = RunState::ChecksFailed;
        }
        if (!held) {
            // One line per obligation that did not hold, so a CI log is actionable.
            std::string detail;
            switch (obligation.kind) {
                case ObligationKind::Rendered:
                    detail = std::format("capture '{}', scope '{}', node '{}', check '{}'", obligation.capture, obligation.scope, obligation.nodeId, obligation.check);
                    break;
                case ObligationKind::Binding:
                    detail = std::format("scope '{}', expect step {} ({})", obligation.scope, obligation.stepIndex, obligation.expectKind);
                    break;
                case ObligationKind::Capture:
                    detail = std::format("capture '{}', scope '{}'", obligation.capture, obligation.scope);
                    break;
            }
            std::string finding;
            for (const Outcome& outcome : outcomes) {
                if (obligationOf(outcome) == obligation) {
                    finding = obligation.kind == ObligationKind::Rendered ? std::string{mv::spell(outcome.finding)} : "did not hold";
                }
            }
            report(diagnostics, {}, "VSC101", std::format("{} obligation did not hold: {}: {}", toWire(obligation.kind), detail, finding));
            state = RunState::ChecksFailed;
        }
    }
    return state;
}

namespace {

/// Everything the per-capture callback and the post-replay pass share for one locale leg.
struct LegContext {
    RunResult*                   result{nullptr};
    const RunOptions*            options{nullptr};
    const mx::BoundScreen*       bound{nullptr};
    rnd::UiRenderer*             renderer{nullptr};
    rnd::OffscreenTarget*        target{nullptr};
    VkQueue                      queue{VK_NULL_HANDLE};
    mv::RenderScope              scope{mv::RenderScope::localeFree()};
    std::string                  scopeTag;
    std::span<const mv::GoldenEntry> goldens;
    std::string_view             scenarioId;
    mx::FrameStorage*            storage{nullptr};
    std::set<std::string>        invoked;         ///< markers whose frame the replay handed us
    std::set<std::string>        renderFailed;    ///< markers whose capture render did not complete
    bool                         structuralFailure{false};
    std::string                  structuralMessage;
};

/// Renders one capture frame and discharges its rendered obligations. A render or evaluation failure
/// is recorded on the leg and never silently swallowed.
void captureFrame(LegContext& leg, const mx::ScenarioCaptureContext& ctx) {
    const std::string marker{ctx.name};
    leg.invoked.insert(marker);

    auto list = mx::recordMonitorFrame(leg.bound->screen, leg.bound->textBinding, leg.bound->imageBinding, *leg.storage, ctx.state, ctx.clock);
    if (!list) {
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008",
               std::format("capture '{}' ({}): the frame could not be recorded: {}", marker, leg.scopeTag, ms::describe(list.error())));
        return;
    }

    Recording recording{.renderer = leg.renderer, .list = &*list, .error = std::nullopt};
    auto      pixels = leg.target->renderAndRead(leg.queue, mx::kClear, recordFrame, &recording);
    if (recording.error.has_value() || !pixels.has_value()) {
        const std::string reason = recording.error.has_value() ? std::string{rnd::describe(*recording.error)}
                                                               : std::string{rnd::describe(pixels.error())};
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008", std::format("capture '{}' ({}): render/readback failed: {}", marker, leg.scopeTag, reason));
        return;
    }
    ++leg.result->renderCount;

    const auto extent = leg.bound->surface();
    const auto frame  = mv::FramebufferView::createPacked(*pixels, extent.width, extent.height);
    if (!frame.has_value()) {
        leg.renderFailed.insert(marker);
        report(leg.result->diagnostics, {}, "VSC008",
               std::format("capture '{}' ({}): readback cannot form a framebuffer: {}", marker, leg.scopeTag, mv::describe(frame.error())));
        return;
    }

    auto evaluated = mdux::tools::verify::evaluateFrame(leg.bound->screen, leg.goldens, leg.scope, &leg.bound->textBinding, dejavuUiAtlas(), *frame);
    if (evaluated.failure.has_value()) {
        leg.structuralFailure = true;
        leg.structuralMessage = std::format("capture '{}' ({}): {}", marker, leg.scopeTag, *evaluated.failure);
        return;
    }
    for (const mdux::tools::verify::Outcome& o : evaluated.outcomes) {
        if (skipRenderedObligation(leg.bound->screen, o.nodeId, o.check)) {
            continue;  // the scenario drives this node's content; its tint check is not an obligation
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

    // Diagnostic attachments (ADR-021 decision 3): the PNG and the per-backend digest. Neither is
    // committed; a failure to write either is a warning, never a verdict.
    const std::string stem = std::format("{}.{}.{}", leg.scenarioId, marker, leg.scopeTag);
    leg.result->captureDigests.push_back(RunResult::CaptureDigest{
        .capture = marker,
        .scope   = leg.scopeTag,
        .sha256  = hexDigest(std::as_bytes(std::span{pixels->data(), pixels->size()}))});

    if (!leg.options->frameImageDirectory.empty()) {
        std::error_code created;
        std::filesystem::create_directories(leg.options->frameImageDirectory, created);
        if (created) {
            warn(leg.result->diagnostics, leg.options->frameImageDirectory, "VSC009", "cannot create the frame image directory: " + created.message());
        } else {
            const std::filesystem::path path = leg.options->frameImageDirectory / (stem + ".frame.png");
            const auto                  encoded =
                mdux::tools::verify::encodePng(*pixels, static_cast<std::uint32_t>(extent.width), static_cast<std::uint32_t>(extent.height));
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

/// Runs one approved locale: loads its committed packages, replays, and records every outcome.
/// Returns false only on a structural impossibility (it has set `result.state` and reported); a
/// check that ran and did not hold is a recorded outcome, not a false return.
[[nodiscard]] bool runLeg(RunResult&                  result,
                          const RunOptions&           options,
                          const ms::CompiledScenario& scenario,
                          std::span<const mv::GoldenEntry> goldens,
                          std::string_view             scenarioId,
                          mx::Locale                   locale,
                          HeadlessDevice&              device,
                          rnd::OffscreenTarget&        target) {
    const std::string scopeTag{mx::localeTag(locale)};

    auto bound = mx::BoundScreen::load(locale);
    if (!bound) {
        report(result.diagnostics, {}, "VSC006", std::format("the committed packages for locale '{}' were refused", scopeTag));
        result.state = RunState::CouldNotRun;
        return false;
    }

    const rnd::VulkanRenderContext context{.device           = device.device(),
                                           .physicalDevice   = device.physicalDevice(),
                                           .renderPass       = target.renderPass(),
                                           .subpass          = 0,
                                           .queue            = device.queue(),
                                           .queueFamilyIndex = device.family(),
                                           .viewport         = bound->surface()};
    auto renderer = mx::makeRenderer(context, *bound);
    if (!renderer.has_value()) {
        report(result.diagnostics, {}, "VSC007", std::format("verification renderer creation failed for locale '{}': {}", scopeTag, rnd::describe(renderer.error())));
        result.state = RunState::CouldNotRun;
        return false;
    }

    mx::FrameStorage storage{bound->screen.budget};

    LegContext leg{.result     = &result,
                   .options    = &options,
                   .bound      = bound.get(),
                   .renderer   = &*renderer,
                   .target     = &target,
                   .queue      = device.queue(),
                   .scope      = mv::RenderScope::forLocale(scopeTag),
                   .scopeTag   = scopeTag,
                   .goldens    = goldens,
                   .scenarioId = scenarioId,
                   .storage    = &storage};

    std::array<ms::StepOutcome, ms::maxScenarioExpectations> outcomeStorage{};
    const ms::ReplayReport replay = mx::replayMonitorScenario(scenario,
                                                              bound->screen,
                                                              bound->font,
                                                              outcomeStorage,
                                                              [&leg](const mx::ScenarioCaptureContext& ctx) { captureFrame(leg, ctx); });

    if (leg.structuralFailure) {
        report(result.diagnostics, {}, "VSC008", leg.structuralMessage);
        result.state = RunState::CouldNotRun;
        return false;
    }

    // A batch larger than the queue, or the runner's storage overrunning ours, is a structural
    // mismatch between the scenario and this build - not a check that did not hold.
    if (replay.fault == ms::ReplayFault::QueueTooSmall || replay.fault == ms::ReplayFault::OutcomeStorageFull
        || replay.fault == ms::ReplayFault::MalformedScenario) {
        report(result.diagnostics, {}, "VSC005",
               std::format("replay of locale '{}' could not complete: {} (step {})", scopeTag, ms::describe(replay.fault), replay.faultStep));
        result.state = RunState::CouldNotRun;
        return false;
    }

    // binding outcomes: one per Expect step, from the runner's own per-step record.
    for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
        const ms::ScenarioStep& step = scenario.steps[i];
        if (step.kind != ms::StepKind::Expect) {
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
                                          .expectKind = std::string{ms::toWire(step.expect.kind)},
                                          .nodeId     = {},
                                          .check      = {},
                                          .held       = found != nullptr && found->held,
                                          .finding    = mv::Finding::Held,
                                          .profile    = {}});
        if (found == nullptr) {
            report(result.diagnostics, {}, "VSC012",
                   std::format("locale '{}': the replay produced no outcome for expect step {}", scopeTag, i));
        }
    }

    // capture outcomes: one per declared marker.
    for (const std::string_view capture : scenario.captureNames) {
        const std::string marker{capture};
        const bool        held = leg.invoked.contains(marker) && !leg.renderFailed.contains(marker);
        result.outcomes.push_back(Outcome{.kind       = ObligationKind::Capture,
                                          .scope      = scopeTag,
                                          .capture    = marker,
                                          .stepIndex  = 0,
                                          .expectKind = {},
                                          .nodeId     = {},
                                          .check      = {},
                                          .held       = held,
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
    const std::filesystem::path dir         = normalize(scenarioDirectory);
    const auto                  scenarioPath = dir / "scenario.json";
    const auto                  scenarioText = readText(scenarioPath);
    if (!scenarioText.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC001", "cannot read scenario.json");
        return result;
    }

    // The reviewed artifact is the JSON; the generated module is a mechanical rendering of it and
    // carries the same `static_assert(scenario.validate())`. Guard the two agree on identity, then
    // hold the `constexpr` form.
    const ms::CompiledScenario scenario = generatedScenario::package();
    if (dir.filename() != std::string_view{scenario.id} || scenarioText->find(std::string{"\"id\": \""} + std::string{scenario.id} + "\"") == std::string::npos) {
        report(result.diagnostics, scenarioPath, "VSC002",
               std::format("scenario.json does not name '{}' - this build verifies only the committed '{}' scenario", scenario.id, scenario.id));
        return result;
    }
    if (const auto valid = scenario.validate(); !valid.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC003", std::string{ms::describe(valid.error())});
        return result;
    }
    result.inputs.push_back({.role = "scenarioPackage", .id = std::string{scenario.id}, .locale = {}, .sha256 = hexDigest(*scenarioText)});

    // The screen bundle the scenario is scripted against.
    const std::filesystem::path screenDir  = options.artifactRoot / "screen" / std::string{scenario.screenId};
    const auto                  screenText = readText(screenDir / "package.json");
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
    const ms::ScreenPackage diskScreen = screenRead.document.package();
    if (mdux::tools::medui::writePackage(diskScreen) != *screenText || diskScreen.id != scenario.screenId) {
        report(result.diagnostics, screenDir / "package.json", "VSC004", "the screen package is non-canonical or its id does not match the scenario");
        return result;
    }
    result.inputs.push_back({.role = "screenPackage", .id = std::string{scenario.screenId}, .locale = {}, .sha256 = hexDigest(*screenText)});

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

    // The approved locales - the manifest, which this run may not narrow.
    std::vector<std::string_view> localeTags;
    std::vector<mx::Locale>       locales;
    for (const auto& approval : diskScreen.approvedTextPackages) {
        localeTags.push_back(approval.locale);
        if (approval.locale == "fr-FR") {
            locales.push_back(mx::Locale::FrFr);
        } else if (approval.locale == "en-US") {
            locales.push_back(mx::Locale::EnUs);
        } else {
            report(result.diagnostics, screenDir / "package.json", "VSC004",
                   std::format("the scenario verifier links no committed packages for approved locale '{}'", approval.locale));
            return result;
        }
    }
    if (locales.empty()) {
        report(result.diagnostics, scenarioPath, "VSC003", "the screen the scenario names approves no locale");
        return result;
    }

    // Per-locale committed digests, for the artifact's `inputs`.
    result.inputs.push_back({.role = "fontPackage", .id = "dejavu-ui", .locale = {}, .sha256 = hexDigest(mx::asText(dejavuUiPackageJson()))});
    result.inputs.push_back({.role = "imagePackage", .id = "brand-mark", .locale = {}, .sha256 = hexDigest(mx::asText(brandMarkPackageJson()))});
    for (const mx::Locale locale : locales) {
        const std::string tag{mx::localeTag(locale)};
        const auto        packageJson = locale == mx::Locale::FrFr ? endoscopeTextFrFrPackageJson() : endoscopeTextEnUsPackageJson();
        result.inputs.push_back({.role = "textPackage", .id = "endoscope-monitor-" + tag, .locale = tag, .sha256 = hexDigest(mx::asText(packageJson))});
    }

    result.obligations = enumerateObligations(scenario, localeTags, diskScreen, goldenViews);

    HeadlessDevice device;
    if (!device.available()) {
        report(result.diagnostics, scenarioPath, "VSC005", "verification run could not be made: " + std::string{device.reason()},
               "Install a Vulkan 1.3 implementation; automatic Linux legs use lavapipe and macOS uses MoltenVK.");
        result.state = RunState::NoRenderDevice;
        return result;
    }
    result.backend = device.backendName();

    const mdux::core::Extent2D extent{.width = diskScreen.surfaceWidth, .height = diskScreen.surfaceHeight};
    auto                       target = rnd::OffscreenTarget::create(device.device(), device.physicalDevice(), extent, device.family());
    if (!target.has_value()) {
        report(result.diagnostics, scenarioPath, "VSC007", "verification run could not create its offscreen target: " + std::string{rnd::describe(target.error())});
        return result;
    }

    for (const mx::Locale locale : locales) {
        if (!runLeg(result, options, scenario, goldenViews, scenario.id, locale, device, *target)) {
            return result;
        }
    }

    result.state = reconcile(result.obligations, result.outcomes, result.diagnostics);

    // The diagnostic digest manifest (ADR-021 decision 3): the backend-specific baseline, never
    // committed and never byte-compared.
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
