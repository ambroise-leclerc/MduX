/**
 * @file ButtonTests.cpp
 * @brief BDD scenarios for `Button` and `CriticalButton`: the face, the word, and the press (#261).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Two things a button is, and one thing it is traced to.
 *
 * **A face.** Until #261 both buttons were visited, counted in `FrameStats::deferred` and left
 * undrawn, on the ground that a button's appearance has a part nothing in this project had decided.
 * What decided it is in `Screen.cppm` under "Why a button's rectangle is its face", and these
 * scenarios are that decision made checkable: the whole rectangle, in the one token the node
 * carries, with the label's word over it at full tint when a locale is bound.
 *
 * **A hit target.** `resolvePress()` resolves a surface coordinate against that same rectangle. Four
 * of the scenarios below carry this issue's acceptance rather than ordinary coverage:
 *
 * - `medui-press-carries-the-requirement` - a resolved critical press hands the caller the
 *   requirement id the node is traced to, in the same value as the action. That is what makes a
 *   safety-critical action traceable from the screen rather than by a convention nobody can check.
 * - `medui-press-refuses-an-unimplemented-event` - a `CriticalButton` naming no member of the closed
 *   `SystemEvent` set refuses the press. #17 names the failure this closes: a screen that can name
 *   any system event can name one nothing implements, *worst discovered on the press of a critical
 *   button*. Returning `NoOp` would be that discovery deferred forever.
 * - `medui-press-refuses-an-untraced-critical-control` - the same, for a critical control with no
 *   requirement. Both are compile errors already; a screen assembled by hand at run time met no
 *   `static_assert`, and a press is the wrong place to find out.
 * - `medui-screen-requirements-reach-the-traceability-matrix` - the requirement a button declares,
 *   read off the compiled screen with `requirementOf()` and exported by #35's
 *   `traceabilityMatrix()`, gap row included.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.font.schema;
import mdux.governance;
import mdux.governance.compliance;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace draw = mdux::draw;
namespace font = mdux::font;
namespace gov  = mdux::governance;
namespace json = mdux::evidence::json;

/// A two-glyph font: a blank and a 4x6 block six pixels above its baseline, so every ink box below
/// is arithmetic a reader can do by hand rather than a number copied out of a run. `StatusTests`'
/// fixture, deliberately unchanged - the placement rule under test is the same one.
[[nodiscard]] font::FontPackage fixtureFont() {
    font::FontPackage package;
    package.id                     = "button-ui";
    package.unitsPerEm             = 1000;
    package.pixelSize              = 10;
    package.locales                = {"en-US"};
    package.atlas.path             = "atlas.bin";
    package.atlas.width            = 8;
    package.atlas.height           = 8;
    package.atlas.byteLength       = 64;
    package.atlas.sha256           = std::string(64, 'a');
    package.atlas.occupancyPercent = 25;
    package.glyphs                 = {
        {.codePoint       = U' ',
         .glyphIndex      = 3,
         .advanceWidth    = 250,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 0,
         .height          = 0,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 0},
        {.codePoint       = U'A',
         .glyphIndex      = 4,
         .advanceWidth    = 700,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 4,
         .height          = 6,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 6},
    };
    package.restrictedCharset = {
        {.first = U' ', .last = U' '},
        {.first = U'A', .last = U'A'}
    };
    return package;
}

const font::FontPackage& theFont() {
    static const font::FontPackage package = fixtureFont();
    return package;
}

/// One v1 record, little-endian, as `mdux::text::draw::decodeRecord()` reads it.
void appendRecord(std::vector<std::byte>& out, std::uint16_t index, std::int16_t x, std::int16_t y) {
    const auto emit = [&out](std::uint16_t value) {
        out.push_back(static_cast<std::byte>(value & 0xFFu));
        out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    };
    emit(index);
    emit(std::bit_cast<std::uint16_t>(x));
    emit(std::bit_cast<std::uint16_t>(y));
}

/**
 * @brief The two labels, as a text package a real `TextBinding::create()` accepts.
 *
 * `STR-HALT` is two blocks and `STR-FREEZE` one, so a scenario can say *which* button's word was
 * drawn from the rectangle count alone. Digests are computed from the bytes rather than written out:
 * a fixture carrying a stale one would exercise the rejection path while claiming to be the accepted
 * case.
 */
struct Labels {
    std::vector<std::byte>  sidecar;
    mdux::text::TextPackage package;
    std::string             canonical;
    ms::TextPackageApproval approval{};

    Labels() {
        appendRecord(sidecar, 1, 0, 0);  // STR-HALT: two blocks, 5px apart
        appendRecord(sidecar, 1, 5, 0);
        appendRecord(sidecar, 1, 0, 0);  // STR-FREEZE: one block at the run's own origin

        package.header.id         = "button-text";
        package.header.kind       = std::string{mdux::text::packageKind};
        package.atlasId           = theFont().id;
        package.locale            = "en-US";
        package.sidecarPath       = "runs.bin";
        package.sidecarByteLength = sidecar.size();
        package.sidecarSha256     = mdux::evidence::sha256(sidecar);

        const auto run = [this](std::string id, std::size_t offset, std::size_t length) {
            const auto slice = std::span<const std::byte>{sidecar}.subspan(offset, length);
            package.runs.push_back(
                mdux::text::TextRun{.id = std::move(id), .byteOffset = offset, .byteLength = length, .sha256 = mdux::evidence::sha256(slice)});
        };
        run("STR-HALT", 0, 12);
        run("STR-FREEZE", 12, 6);

        const auto written = package.write();
        if (!written.has_value()) {
            throw speclab::core::AssertionFailure("the fixture text package does not serialize", std::source_location::current());
        }
        canonical = *written;
        approval  = ms::TextPackageApproval{.locale = package.locale, .packageId = package.header.id, .packageSha256 = mdux::evidence::sha256(bytes())};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span{canonical.data(), canonical.size()});
    }
};

const Labels& theLabels() {
    static const Labels labels;
    return labels;
}

constexpr ms::CriticalButtonSpec halt{.requirement = "REQ-BT-001",
                                      .labelKey    = "STR-HALT",
                                      .colorToken  = "Theme.Colors.Fault",
                                      .onPress     = ms::SystemEvent::TriggerHalt};

constexpr ms::ButtonSpec freeze{.labelKey = "STR-FREEZE", .colorToken = "Theme.Colors.PrimaryAction", .source = "FREEZE", .requirement = "REQ-BT-002"};

/// The same ordinary button with nothing to trace it to, which the dictionary permits.
constexpr ms::ButtonSpec untraced{.labelKey = "STR-FREEZE", .colorToken = "Theme.Colors.PrimaryAction", .source = "FREEZE", .requirement = {}};

constexpr ms::PanelSpec ground{.colorToken = "Theme.Colors.TopbarBackground"};

constexpr draw::DrawBudget testBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

/// A placeholder approval, so these `constexpr` screens are ones `validate()` admits. The scenarios
/// that bind text replace it with the real digest through `approve()`, which cannot be a constant
/// expression because it is a hash of bytes.
constexpr std::array placeholderApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "button-text", .packageSha256 = {1}}
};

constexpr std::array<ms::CompiledNode, 2> controlNodes{
    ms::CompiledNode{  .id = "halt", .bounds = {20, 30, 100, 40},   .payload = halt},
    ms::CompiledNode{.id = "freeze", .bounds = {20, 70, 100, 20}, .payload = freeze}
};

/// A panel drawn *before* the button, which is the order a `Row`'s synthetic background arrives in,
/// and a panel drawn *after* it, which is what an author has to write a `position:` to produce.
constexpr std::array<ms::CompiledNode, 2> underNodes{
    ms::CompiledNode{.id = "backdrop",  .bounds = {0, 0, 200, 100}, .payload = ground},
    ms::CompiledNode{    .id = "halt", .bounds = {20, 30, 100, 40},   .payload = halt}
};
constexpr std::array<ms::CompiledNode, 2> overNodes{
    ms::CompiledNode{    .id = "halt", .bounds = {20, 30, 100, 40},   .payload = halt},
    ms::CompiledNode{.id = "backdrop",  .bounds = {0, 0, 200, 100}, .payload = ground}
};

/// Two controls sharing a rectangle, which only `position:` can produce.
constexpr std::array<ms::CompiledNode, 2> stackedNodes{
    ms::CompiledNode{ .id = "under", .bounds = {20, 30, 100, 40}, .payload = freeze},
    ms::CompiledNode{.id = "on-top", .bounds = {20, 30, 100, 40},   .payload = halt}
};

[[nodiscard]] constexpr ms::ScreenPackage screenOver(std::span<const ms::CompiledNode> nodes, std::string_view id) noexcept {
    return ms::ScreenPackage{.id                   = id,
                             .schemaVersion        = mdux::evidence::kSchemaVersion,
                             .surfaceWidth         = 200,
                             .surfaceHeight        = 100,
                             .approvedTextPackages = placeholderApprovals,
                             .nodes                = nodes,
                             .budget               = testBudget};
}

constexpr ms::ScreenPackage controlScreen = screenOver(controlNodes, "controls");
constexpr ms::ScreenPackage underScreen   = screenOver(underNodes, "under");
constexpr ms::ScreenPackage overScreen    = screenOver(overNodes, "over");
constexpr ms::ScreenPackage stackedScreen = screenOver(stackedNodes, "stacked");

static_assert(controlScreen.validate().has_value(), "the reference screen must be one a device could hold");
static_assert(underScreen.validate().has_value(), "and so is a button over a panel");

/// The same screen carrying the labels fixture's real approval, so `TextBinding::create()` accepts it.
[[nodiscard]] ms::ScreenPackage approve(const ms::ScreenPackage& screen) {
    ms::ScreenPackage bound    = screen;
    bound.approvedTextPackages = std::span{&theLabels().approval, 1};
    return bound;
}

/// The binding a label's word is drawn through. Built once, as a device would at start-up.
[[nodiscard]] ms::TextBinding bindText(const ms::ScreenPackage& screen) {
    auto made = ms::TextBinding::create(screen, theFont(), theLabels().package, theLabels().bytes(), theLabels().sidecar);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture text binding is invalid: {}", ms::describe(made.error())),
                                              std::source_location::current());
    }
    return *made;
}

/// Storage a caller sizes once, as a device would.
struct Scratch {
    std::array<draw::UiVertex, 512>   vertices{};
    std::array<draw::Index, 768>      indices{};
    std::array<draw::DrawCommand, 16> commands{};

    [[nodiscard]] draw::DrawList list() {
        auto created = draw::DrawList::create(vertices, indices, commands, testBudget);
        if (!created.has_value()) {
            throw speclab::core::AssertionFailure("the scratch does not satisfy its own budget", std::source_location::current());
        }
        return std::move(*created);
    }
};

/// A screen assembled at run time, which is the only way a payload `validate()` refuses reaches
/// `resolvePress()` at all. Owning, because the nodes it holds are built rather than generated.
struct HandBuilt {
    std::vector<ms::CompiledNode> nodes;
    ms::ScreenPackage             screen{};

    explicit HandBuilt(ms::NodePayload payload)
        : nodes{
              ms::CompiledNode{.id = "control", .bounds = {20, 30, 100, 40}, .payload = std::move(payload)}
    } {
        screen = ms::ScreenPackage{.id                   = "hand-built",
                                   .schemaVersion        = mdux::evidence::kSchemaVersion,
                                   .surfaceWidth         = 200,
                                   .surfaceHeight        = 100,
                                   .approvedTextPackages = placeholderApprovals,
                                   .nodes                = nodes,
                                   .budget               = testBudget};
    }
};

}  // namespace

const mdux::spec::Register aButtonPaintsItsWholeRectangle{
    "With no locale bound a button paints its whole rectangle in its own token, and is not deferred",
    "evidence-unit",
    [] {
        return speclab::Test("medui-button-face-is-drawn")
            .Given("a CriticalButton and a Button carrying two different tokens", [] {})
            .When("a frame is recorded with no text binding", [] {})
            .Then("each node's whole rectangle is filled at full tint and nothing is deferred",
                  [] {
                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(controlScreen, list);

                      mdux::spec::Checks checks;
                      checks.expect(frame.has_value(), "the frame is recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      // The claim that replaces the deferral. A button with no locale bound has no
                      // word to draw and is still not a node this runtime leaves out: its rectangle
                      // is where a press lands and what a `Bounds` golden reads as an equality.
                      checks.expect(frame->deferred == 0, std::format("neither button is deferred, got {}", frame->deferred));
                      checks.expect(frame->rects == 2, std::format("one rectangle each, got {}", frame->rects));

                      const std::span<const draw::UiVertex> vertices = list.vertices();
                      checks.expect(vertices.size() == 8, std::format("two rectangles, got {} vertices", vertices.size()));
                      if (vertices.size() != 8) {
                          checks.raise();
                          return;
                      }

                      // Written out rather than resolved through the function the runtime calls: an
                      // expectation that called it would agree with it whatever it returned.
                      // `Fault` is {0.86, 0.20, 0.18, 1.0} linear and `PrimaryAction`
                      // {0.16, 0.44, 0.86, 1.0}.
                      const std::uint32_t fault   = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 255});
                      const std::uint32_t primary = draw::packColor(mdux::core::ColorRgba8{.r = 41, .g = 112, .b = 219, .a = 255});

                      checks.expect(vertices[0].x == 20.0F && vertices[0].y == 30.0F, "the critical button starts at its node's corner");
                      checks.expect(vertices[2].x == 120.0F && vertices[2].y == 70.0F, "and ends at its far corner");
                      checks.expect(vertices[0].color == fault, "drawn in Theme.Colors.Fault, at full coverage because no word covers it");
                      checks.expect(vertices[4].color == primary, "and the ordinary button in its own Theme.Colors.PrimaryAction");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBoundButtonDrawsItsWordOverItsFace{
    "With a locale bound, a button's label is drawn at full tint over its face at quarter coverage",
    "evidence-unit",
    [] {
        return speclab::Test("medui-button-word-on-its-face")
            .Given("a CriticalButton whose label is two blocks and a Button whose label is one", [] {})
            .When("a frame is recorded with the locale bound", [] {})
            .Then("each face dims to boundFieldCoverage and its own word is drawn on it",
                  [] {
                      const ms::ScreenPackage screen = approve(controlScreen);

                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(screen, list, bindText(screen));

                      mdux::spec::Checks checks;
                      checks.expect(frame.has_value(), "the frame is recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      // Two faces plus one rectangle per inked glyph: `STR-HALT` is two blocks and
                      // `STR-FREEZE` one. The count therefore says *which* label each button drew
                      // rather than only that something was drawn on it.
                      checks.expect(frame->rects == 5, std::format("two faces and three glyphs, got {}", frame->rects));
                      checks.expect(frame->deferred == 0, std::format("nothing deferred, got {}", frame->deferred));

                      const std::span<const draw::UiVertex> vertices = list.vertices();
                      checks.expect(vertices.size() == 20, std::format("five rectangles, got {} vertices", vertices.size()));
                      if (vertices.size() != 20) {
                          checks.raise();
                          return;
                      }

                      // The face, at a quarter of `Fault`'s alpha because a word covers it. A word in
                      // the field's own tint over an opaque field of that tint is invisible, which is
                      // why the dimming is not decoration - see `boundFieldCoverage`.
                      const std::uint32_t dimmedFault = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 64});
                      const std::uint32_t fullFault   = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 255});
                      checks.expect(vertices[0].color == dimmedFault, "the critical button's face dims under its word");
                      checks.expect(vertices[4].color == fullFault, "and its word is drawn at full tint");

                      // The ink box in run coordinates is (0, -6), so placing its corner on the
                      // node's corner lands the first glyph at (20, 30) rather than at (20, 24) -
                      // the `Label` placement rule, reused rather than reinvented.
                      checks.expect(vertices[4].x == 20.0F && vertices[4].y == 30.0F, "the word's ink starts at the node's corner");
                      checks.expect(vertices[8].x == 25.0F, std::format("the second block is 5px along, got {}", vertices[8].x));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aPressResolvesToTheControlUnderIt{
    "A press inside a control resolves to it, and a press outside every control resolves to nothing",
    "evidence-unit",
    [] {
        return speclab::Test("medui-press-hit-test")
            .Given("a CriticalButton occupying (20, 30) to (120, 70)", [] {})
            .When("presses on its corners, just outside them, and away from it are resolved", [] {})
            .Then("the rectangle is half-open on both axes and a miss is nullopt rather than an error",
                  [] {
                      mdux::spec::Checks checks;

                      const auto at = [&checks](std::int32_t x, std::int32_t y) -> std::optional<ms::PressAction> {
                          const auto resolved = ms::resolvePress(controlScreen, x, y);
                          checks.expect(resolved.has_value(), std::format("({}, {}) resolves rather than refusing", x, y));
                          return resolved.has_value() ? *resolved : std::optional<ms::PressAction>{};
                      };

                      const auto hit = at(20, 30);
                      checks.expect(hit.has_value() && hit->nodeId == "halt", "the top-left corner is inside");
                      const auto far = at(119, 69);
                      checks.expect(far.has_value() && far->nodeId == "halt", "and so is the last pixel of the rectangle");

                      // Half-open, exactly as `NodeRect` is everywhere else: a rectangle at x=120 is
                      // the neighbour's first column, and a press there belongs to the neighbour.
                      checks.expect(!at(120, 50).has_value(), "the column past the right edge is outside");
                      checks.expect(!at(19, 30).has_value(), "and so is the column before the left edge");

                      // The two controls are edge to edge, which is what makes the half-open rule
                      // load-bearing rather than a convention: y=70 is the critical button's first
                      // row past the end and the ordinary button's first row, and exactly one of them
                      // may have it. A closed rectangle would give it to both and resolve it to the
                      // one drawn later, which is a control an operator was not aiming at.
                      const auto shared = at(50, 70);
                      checks.expect(shared.has_value() && shared->nodeId == "freeze",
                                    std::format("the shared row belongs to the control below, got '{}'", shared.has_value() ? shared->nodeId : "nothing"));
                      const auto above = at(50, 69);
                      checks.expect(above.has_value() && above->nodeId == "halt", "and the row before it to the control above");

                      // A press well inside the second control, so the scenario cannot pass by
                      // resolving everything to the first control it finds.
                      const auto second = at(50, 75);
                      checks.expect(second.has_value() && second->nodeId == "freeze", "a press on the second control resolves to it");
                      checks.expect(!at(50, 90).has_value(), "and the row past its own bottom edge is outside every control");

                      // Most of a screen is not a button, and that is not an error.
                      checks.expect(!at(0, 0).has_value(), "a press on no control at all is nullopt");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aPressCarriesItsRequirementAndItsAction{
    "A resolved press carries the requirement it is traced to and the action it asks for",
    "evidence-unit",
    [] {
        return speclab::Test("medui-press-carries-the-requirement")
            .Given("a CriticalButton traced to REQ-BT-001 and a Button traced to REQ-BT-002", [] {})
            .When("each is pressed", [] {})
            .Then("the critical press carries a closed system event and the ordinary one a product action name",
                  [] {
                      mdux::spec::Checks checks;

                      const auto critical = ms::resolvePress(controlScreen, 50, 50);
                      checks.expect(critical.has_value() && critical->has_value(), "the critical press resolves");
                      if (critical.has_value() && critical->has_value()) {
                          const ms::PressAction& action = **critical;
                          // The issue's own claim: what performs the action is holding the id the
                          // action is traced to, in the same value, at the moment it performs it.
                          checks.expect(action.requirement == "REQ-BT-001",
                                        std::format("the requirement travels with the press, got '{}'", action.requirement));
                          checks.expect(action.event == ms::SystemEvent::TriggerHalt, "and the action is the closed-set member the node names");
                          checks.expect(action.source.empty(), "a critical control has no product action name");
                      }

                      const auto ordinary = ms::resolvePress(controlScreen, 50, 75);
                      checks.expect(ordinary.has_value() && ordinary->has_value(), "the ordinary press resolves");
                      if (ordinary.has_value() && ordinary->has_value()) {
                          const ms::PressAction& action = **ordinary;
                          checks.expect(action.requirement == "REQ-BT-002", "an ordinary button's optional requirement travels too when it declares one");
                          checks.expect(action.source == "FREEZE", "and its action is the open name its product chose");
                          checks.expect(!action.event.has_value(), "which is not a system event, and is not reported as one");
                      }

                      // The dictionary makes `requirement:` optional on a `Button`, so an empty one
                      // is a legal screen rather than a refusal - the asymmetry with `CriticalButton`
                      // is the whole reason there are two components.
                      const HandBuilt anonymous{untraced};
                      const auto      resolved = ms::resolvePress(anonymous.screen, 50, 50);
                      checks.expect(resolved.has_value() && resolved->has_value(), "a Button with no requirement still resolves");
                      if (resolved.has_value() && resolved->has_value()) {
                          checks.expect((*resolved)->requirement.empty(), "and reports an empty requirement rather than inventing one");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnimplementedEventRefusesThePress{
    "A critical control naming no member of the closed system-event set refuses the press",
    "evidence-unit",
    [] {
        return speclab::Test("medui-press-refuses-an-unimplemented-event")
            .Given("critical controls whose on_press is unspecified and out of range", [] {})
            .When("each is pressed", [] {})
            .Then(
                "each is UnimplementedEvent rather than a press reported as a harmless no-op",
                [] {
                    mdux::spec::Checks checks;

                    // Both are compile errors - `validatePayload()` refuses them and a generated
                    // screen carries a `static_assert` over it - so neither reaches a device
                    // through the normal path. A screen assembled at run time met no such
                    // assertion, and #17 says where the failure is otherwise found: on the press.
                    for (const ms::SystemEvent event : {ms::SystemEvent::Unspecified, static_cast<ms::SystemEvent>(255)}) {
                        const HandBuilt built{
                            ms::CriticalButtonSpec{.requirement = "REQ-BT-001", .labelKey = "STR-HALT", .colorToken = "Theme.Colors.Fault", .onPress = event}
                        };
                        const auto resolved = ms::resolvePress(built.screen, 50, 50);
                        checks.expect(!resolved.has_value(), "the press is refused rather than resolved");
                        if (!resolved.has_value()) {
                            checks.expect(resolved.error() == ms::ScreenError::UnimplementedEvent,
                                          std::format("refused as UnimplementedEvent, got '{}'", ms::describe(resolved.error())));
                        }
                    }

                    // The other half of the claim: the same control with a member of the set
                    // resolves, so the scenario cannot pass because critical presses never do.
                    const HandBuilt ok{halt};
                    checks.expect(ms::resolvePress(ok.screen, 50, 50).has_value(), "and a control naming TriggerHalt resolves");
                    checks.raise();
                })
            .Execute();
    }};

const mdux::spec::Register anUntracedCriticalControlRefusesThePress{
    "A critical control with no requirement refuses the press rather than performing an untraceable action",
    "evidence-unit",
    [] {
        return speclab::Test("medui-press-refuses-an-untraced-critical-control")
            .Given("a CriticalButton whose requirement is empty", [] {})
            .When("it is pressed", [] {})
            .Then("the press is UntracedCriticalControl",
                  [] {
                      const HandBuilt built{
                          ms::CriticalButtonSpec{.requirement = {},
                                                 .labelKey    = "STR-HALT",
                                                 .colorToken  = "Theme.Colors.Fault",
                                                 .onPress     = ms::SystemEvent::TriggerHalt}
                      };
                      const auto resolved = ms::resolvePress(built.screen, 50, 50);

                      mdux::spec::Checks checks;
                      checks.expect(!resolved.has_value(), "an action nothing traces is not one this module hands to a host");
                      if (!resolved.has_value()) {
                          checks.expect(resolved.error() == ms::ScreenError::UntracedCriticalControl,
                                        std::format("refused as UntracedCriticalControl, got '{}'", ms::describe(resolved.error())));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aPressResolvesToWhatTheOperatorCanSee{
    "A press resolves to the node drawn last, and a node that is not a control absorbs it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-press-resolves-to-the-control-on-top")
            .Given("two controls sharing a rectangle, and a panel written before and after a control", [] {})
            .When("the shared rectangle is pressed on each screen", [] {})
            .Then("the last node wins, and a panel over a control takes the press rather than passing it through",
                  [] {
                      mdux::spec::Checks checks;

                      const auto stacked = ms::resolvePress(stackedScreen, 50, 50);
                      checks.expect(stacked.has_value() && stacked->has_value(), "the shared rectangle resolves to a control");
                      if (stacked.has_value() && stacked->has_value()) {
                          // The rule an operator's eyes already apply: they press what they can see.
                          checks.expect((*stacked)->nodeId == "on-top", std::format("the control drawn last wins, got '{}'", (*stacked)->nodeId));
                      }

                      // A `Row`'s synthetic background is emitted *before* its children, so it never
                      // absorbs a press meant for one of them.
                      const auto beneath = ms::resolvePress(underScreen, 50, 50);
                      checks.expect(beneath.has_value() && beneath->has_value() && (*beneath)->nodeId == "halt",
                                    "a panel written before a control does not shadow it");

                      // A panel written after one does, and that is the intended answer: resolving
                      // through it would fire a control the frame does not show.
                      const auto covered = ms::resolvePress(overScreen, 50, 50);
                      checks.expect(covered.has_value() && !covered->has_value(), "a node covering a control absorbs the press instead of passing it through");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The third acceptance: the requirement reaches #35's export
// ---------------------------------------------------------------------------

const mdux::spec::Register screenRequirementsReachTheTraceabilityMatrix{
    "The requirements a screen's controls declare reach the traceability matrix, gaps included",
    "evidence-unit",
    [] {
        return speclab::Test("medui-screen-requirements-reach-the-traceability-matrix")
            .Given("a compliance program whose requirement ids are read off a compiled screen", [] {})
            .When("traceabilityMatrix() is exported over it", [] {})
            .Then("each control's requirement is a row, and the one nothing verifies is an empty row rather than an omission",
                  [] {
                      mdux::spec::Checks checks;

                      // Read off the artifact through the function `requirementOf()`'s own comment
                      // names as the walk a traceability export performs. Nothing here restates the
                      // ids: a screen that stopped carrying them produces no rows below.
                      gov::ComplianceProgram program;
                      program.safetyClass = gov::SafetyClass::B;
                      for (const ms::CompiledNode& node : controlScreen.nodes) {
                          const std::string_view requirement = ms::requirementOf(node);
                          if (requirement.empty()) {
                              continue;
                          }
                          program.requirements.push_back(
                              gov::Requirement{.id                 = std::string{requirement},
                                               .title              = std::format("The {} control is drawn and pressable.", node.id),
                                               .sourceClause       = "IEC 62304:2006 §5.2 Software requirements analysis",
                                               .verificationIntent = "Discharged by the screen's committed golden and pixel evidence."});
                      }
                      checks.expect(program.requirements.size() == 2, std::format("both controls are traced, got {}", program.requirements.size()));

                      // One case, citing the screen's own committed artifacts. The other requirement
                      // is left undischarged deliberately - #35's doctrine is that a gap is a row.
                      program.verificationCases.push_back(gov::VerificationCase{.id            = "VER-BT-001",
                                                                                .requirementId = "REQ-BT-001",
                                                                                .method        = gov::VerificationMethod::Test,
                                                                                .evidenceRefs  = {"generated/screen/endoscope-monitor/verification.json"},
                                                                                .passed        = true});

                      const auto matrix = gov::traceabilityMatrix(program);
                      checks.expect(matrix.has_value(), "the export succeeds over a program carrying a gap");
                      if (!matrix.has_value() || matrix->kind() != json::Value::Kind::Array) {
                          checks.raise();
                          return;
                      }

                      const std::span<const json::Value> rows = matrix->elements();
                      checks.expect(rows.size() == 2, std::format("one row per screen requirement, got {}", rows.size()));
                      if (rows.size() != 2) {
                          checks.raise();
                          return;
                      }

                      const auto idAt = [](const json::Value& row) -> std::string_view {
                          const json::Value* found = row.find("requirement_id");
                          return found == nullptr ? std::string_view{} : found->asString().value_or(std::string_view{});
                      };
                      const auto casesAt = [](const json::Value& row) -> std::size_t {
                          const json::Value* found = row.find("verification_cases");
                          return found == nullptr ? 0U : found->elements().size();
                      };

                      // Sorted by requirement id, which is what makes the export byte-stable.
                      checks.expect(idAt(rows[0]) == "REQ-BT-001", std::format("the critical control's requirement is a row, got '{}'", idAt(rows[0])));
                      checks.expect(idAt(rows[1]) == "REQ-BT-002", std::format("and so is the ordinary control's, got '{}'", idAt(rows[1])));

                      checks.expect(casesAt(rows[0]) == 1, "the discharged requirement names its case");
                      checks.expect(casesAt(rows[1]) == 0, "and the undischarged one is an empty row, which is the question an auditor asks");

                      const json::Value* cases = rows[0].find("verification_cases");
                      if (cases != nullptr && cases->elements().size() == 1) {
                          const json::Value* refs = cases->elements()[0].find("evidence_refs");
                          checks.expect(refs != nullptr && refs->elements().size() == 1
                                            && refs->elements()[0].asString().value_or("") == "generated/screen/endoscope-monitor/verification.json",
                                        "and the evidence it cites is the screen's own committed verification record");
                      }
                      checks.raise();
                  })
            .Execute();
    }};
