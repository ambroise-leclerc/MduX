/**
 * @file StatusTests.cpp
 * @brief BDD scenarios for the screen runtime's status binding (#259).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * A `StatusIndicator`'s `states:` list is closed by the time a device holds it: every key was
 * validated against every approved locale and the widest of them measured against the node's box.
 * What the host still supplies is which of them is on screen, and these scenarios are about the one
 * question that follows - what happens to a position the list does not have.
 *
 * Three of them carry this issue's acceptance rather than ordinary coverage:
 *
 * - `medui-status-state-outside-the-list-is-refused` - an index past the end is refused at
 *   `create()`, where a caller can still act on it. Clamping shows a neighbouring state and wrapping
 *   shows the first one; both are a state the device is not in, drawn as confidently as one it is.
 * - `medui-status-frame-rechecks-the-closed-list` - the same refusal from `render()`, over a screen
 *   whose node lost a state after the binding was made. The id check cannot see that substitution,
 *   which is exactly why the list is indexed only after it has been bounded again.
 * - `medui-status-untinted-indicator-is-refused` - a node with no `colors:` is refused rather than
 *   bound, because its states are told apart by nothing a frame carries unless a locale happens to
 *   be bound. An indicator that paints the same rectangle in every state is the failure here that
 *   looks most like a working one.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace draw = mdux::draw;
namespace font = mdux::font;

/// A two-glyph font: a blank and a 4x6 block sitting six pixels above its baseline, so every ink box
/// below is arithmetic a reader can do by hand rather than a number copied out of a run.
[[nodiscard]] font::FontPackage fixtureFont() {
    font::FontPackage package;
    package.id                     = "status-ui";
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
 * @brief The two states' words, as a text package a real `TextBinding::create()` accepts.
 *
 * `STR-OK` is one block and `STR-ALARM` three, so the two states differ in ink as well as in tint
 * and a scenario can say which one was drawn without reading a colour. The digests are computed from
 * the bytes rather than written out: a fixture carrying a stale one would exercise the rejection
 * path while claiming to be the accepted case.
 */
struct StateWords {
    std::vector<std::byte>  sidecar;
    mdux::text::TextPackage package;
    std::string             canonical;
    ms::TextPackageApproval approval{};

    StateWords() {
        appendRecord(sidecar, 1, 0, 0);  // STR-OK: one block at the run's own origin
        appendRecord(sidecar, 1, 0, 0);  // STR-ALARM: three blocks, 5px apart
        appendRecord(sidecar, 1, 5, 0);
        appendRecord(sidecar, 1, 10, 0);

        package.header.id         = "status-text";
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
        run("STR-OK", 0, 6);
        run("STR-ALARM", 6, 18);

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

const StateWords& theWords() {
    static const StateWords words;
    return words;
}

constexpr std::array stateKeys{std::string_view{"STR-OK"}, std::string_view{"STR-ALARM"}};
constexpr std::array stateTints{std::string_view{"Theme.Colors.Nominal"}, std::string_view{"Theme.Colors.Fault"}};

constexpr ms::StatusIndicatorSpec tinted{.requirement = "REQ-ST-001", .source = "STATE", .stateKeys = stateKeys, .colorTokens = stateTints};
constexpr ms::StatusIndicatorSpec untinted{.requirement = "REQ-ST-002", .source = "STATE", .stateKeys = stateKeys, .colorTokens = {}};

constexpr ms::PanelSpec ground{.colorToken = "Theme.Colors.TopbarBackground"};

constexpr std::array<ms::CompiledNode, 1> tintedNodes{
    ms::CompiledNode{.id = "status", .bounds = {20, 30, 100, 40}, .payload = tinted}
};
constexpr std::array<ms::CompiledNode, 1> untintedNodes{
    ms::CompiledNode{.id = "status", .bounds = {20, 30, 100, 40}, .payload = untinted}
};
constexpr std::array<ms::CompiledNode, 1> narrowNodes{
    ms::CompiledNode{.id = "status", .bounds = {20, 30, 6, 40}, .payload = tinted}
};
constexpr std::array<ms::CompiledNode, 2> mixedNodes{
    ms::CompiledNode{.id = "ground",  .bounds = {0, 0, 200, 100}, .payload = ground},
    ms::CompiledNode{.id = "status", .bounds = {20, 30, 100, 40}, .payload = tinted}
};

constexpr draw::DrawBudget testBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

/// A placeholder approval, so these `constexpr` screens are ones `validate()` admits. The scenarios
/// that bind text replace it with the real digest through `approve()`, which cannot be a constant
/// expression because it is a hash of bytes.
constexpr std::array placeholderApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "status-text", .packageSha256 = {1}}
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

/// The same screen, one state short: what a binding made against `tintedScreen` must not index.
constexpr std::array                      oneKey{std::string_view{"STR-OK"}};
constexpr std::array                      oneTint{std::string_view{"Theme.Colors.Nominal"}};
constexpr ms::StatusIndicatorSpec         shortened{.requirement = "REQ-ST-001", .source = "STATE", .stateKeys = oneKey, .colorTokens = oneTint};
constexpr std::array<ms::CompiledNode, 1> shortenedNodes{
    ms::CompiledNode{.id = "status", .bounds = {20, 30, 100, 40}, .payload = shortened}
};

constexpr ms::ScreenPackage tintedScreen    = screenOver(tintedNodes, "status");
constexpr ms::ScreenPackage untintedScreen  = screenOver(untintedNodes, "status");
constexpr ms::ScreenPackage narrowScreen    = screenOver(narrowNodes, "status");
constexpr ms::ScreenPackage mixedScreen     = screenOver(mixedNodes, "status-mixed");
constexpr ms::ScreenPackage shortenedScreen = screenOver(shortenedNodes, "status");

static_assert(tintedScreen.validate().has_value(), "the reference screen must be one a device could hold");
static_assert(untintedScreen.validate().has_value(), "a StatusIndicator with no colors: is a screen the schema admits");
static_assert(mixedScreen.validate().has_value(), "and so is one with a panel beside it");

/// The same screen carrying the words fixture's real approval, so `TextBinding::create()` accepts it.
[[nodiscard]] ms::ScreenPackage approve(const ms::ScreenPackage& screen) {
    ms::ScreenPackage bound    = screen;
    bound.approvedTextPackages = std::span{&theWords().approval, 1};
    return bound;
}

/// The binding a state's word is drawn through. Built once, as a device would at start-up.
[[nodiscard]] ms::TextBinding bindText(const ms::ScreenPackage& screen) {
    auto made = ms::TextBinding::create(screen, theFont(), theWords().package, theWords().bytes(), theWords().sidecar);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture text binding is invalid: {}", ms::describe(made.error())),
                                              std::source_location::current());
    }
    return *made;
}

[[nodiscard]] ms::StatusBinding bindStatus(const ms::ScreenPackage& screen, std::span<const ms::StatusSlot> slots) {
    auto made = ms::StatusBinding::create(screen, slots);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture status binding was refused: {}", ms::describe(made.error())),
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

/// The error `StatusBinding::create()` reported, or nullopt when it accepted the slots.
[[nodiscard]] std::optional<ms::ScreenError> refusalOf(const ms::ScreenPackage& screen, std::span<const ms::StatusSlot> slots) {
    auto made = ms::StatusBinding::create(screen, slots);
    return made.has_value() ? std::optional<ms::ScreenError>{} : std::optional{made.error()};
}

}  // namespace

const mdux::spec::Register aStateOutsideTheListIsRefused{
    "A state outside the closed list is refused, never clamped and never wrapped",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-state-outside-the-list-is-refused")
            .Given("an indicator whose compiled states are exactly two", [] {})
            .When("a slot naming state 2, and one naming a very large state, are offered", [] {})
            .Then("each is StateOutOfRange, and only the two positions the artifact carries are accepted",
                  [] {
                      mdux::spec::Checks checks;

                      for (const std::uint32_t state : {0u, 1u}) {
                          const std::array slots{
                              ms::StatusSlot{.nodeId = "status", .state = state}
                          };
                          checks.expect(!refusalOf(tintedScreen, slots).has_value(), std::format("state {} is one the artifact carries", state));
                      }

                      for (const std::uint32_t state : {2u, 7u, std::numeric_limits<std::uint32_t>::max()}) {
                          const std::array slots{
                              ms::StatusSlot{.nodeId = "status", .state = state}
                          };
                          const auto refused = refusalOf(tintedScreen, slots);
                          checks.expect(refused == ms::ScreenError::StateOutOfRange,
                                        std::format("state {} is refused as StateOutOfRange, got '{}'",
                                                    state,
                                                    refused.has_value() ? ms::describe(*refused) : "accepted"));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theFrameRechecksTheClosedList{
    "A frame bounds the states list again before indexing it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-frame-rechecks-the-closed-list")
            .Given("a binding made against a screen whose indicator then lost a state", [] {})
            .When("the frame is recorded over the shortened screen", [] {})
            .Then("it is refused as StateOutOfRange and nothing is left recorded",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;
                      auto               list = scratch.list();

                      const std::array slots{
                          ms::StatusSlot{.nodeId = "status", .state = 1}
                      };
                      const ms::StatusBinding binding = bindStatus(tintedScreen, slots);

                      // `shortenedScreen` is the substitution `approvedBy()` cannot see: same screen
                      // id, one state fewer. A device assembling its screens from one package never
                      // reaches this, which is why the check is in `render()` rather than only in
                      // `create()` - the runtime indexes the span, so the runtime bounds it.
                      const auto frame = ms::render(shortenedScreen, list, {}, {}, {}, {}, binding);
                      checks.expect(!frame.has_value(), "the frame is refused");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::StateOutOfRange,
                                        std::format("reported as StateOutOfRange, got '{}'", ms::describe(frame.error())));
                      }
                      checks.expect(list.vertices().empty(), std::format("nothing is left recorded, got {} vertices", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUntintedIndicatorIsRefused{"An indicator whose states share one appearance is refused at start-up", "evidence-unit", [] {
                                                            return speclab::Test("medui-status-untinted-indicator-is-refused")
                                                                .Given("a compiled indicator that declares no colors:", [] {})
                                                                .When("a state for it is offered to create()", [] {})
                                                                .Then("it is StatusHasNoTint, and the node itself remains a screen the schema admits",
                                                                      [] {
                                                                          mdux::spec::Checks checks;

                                                                          const std::array slots{
                                                                              ms::StatusSlot{.nodeId = "status", .state = 0}
                                                                          };
                                                                          const auto refused = refusalOf(untintedScreen, slots);
                                                                          checks.expect(refused == ms::ScreenError::StatusHasNoTint,
                                                                                        std::format("refused as StatusHasNoTint, got '{}'",
                                                                                                    refused.has_value() ? ms::describe(*refused) : "accepted"));

                                                                          // The node is legal, and stays drawable by nobody: a screen carrying it renders
                                                                          // exactly as it did before this issue, with the indicator counted as deferred.
                                                                          Scratch    scratch;
                                                                          auto       list  = scratch.list();
                                                                          const auto frame = ms::render(untintedScreen, list);
                                                                          checks.expect(frame.has_value() && frame->deferred == 1,
                                                                                        std::format("the untinted indicator is deferred, got {}",
                                                                                                    frame.has_value() ? std::format("{}", frame->deferred)
                                                                                                                      : "a refusal"));
                                                                          checks.raise();
                                                                      })
                                                                .Execute();
                                                        }};

const mdux::spec::Register slotsAreCheckedAgainstTheScreen{
    "A slot naming no indicator on this screen, or naming one twice, is refused",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-slot-names-are-checked")
            .Given("slots that are wrong in one way each", [] {})
            .When("each is offered to create()", [] {})
            .Then("each reports its own error",
                  [] {
                      mdux::spec::Checks checks;

                      const std::array mistyped{
                          ms::StatusSlot{.nodeId = "statuz", .state = 0}
                      };
                      checks.expect(refusalOf(tintedScreen, mistyped) == ms::ScreenError::UnknownStatusNode, "a mistyped node id is UnknownStatusNode");

                      // A node that exists and is something else - the same refusal, because from the
                      // caller's side both are "this slot will never be drawn".
                      const std::array wrongKind{
                          ms::StatusSlot{.nodeId = "ground", .state = 0}
                      };
                      checks.expect(refusalOf(mixedScreen, wrongKind) == ms::ScreenError::UnknownStatusNode, "a node of another kind is UnknownStatusNode");

                      const std::array twice{
                          ms::StatusSlot{.nodeId = "status", .state = 0},
                          ms::StatusSlot{.nodeId = "status", .state = 1}
                      };
                      checks.expect(refusalOf(tintedScreen, twice) == ms::ScreenError::DuplicateStatus, "two slots for one node are DuplicateStatus");

                      // And the cross-screen substitution every binding in this module closes.
                      const std::array slots{
                          ms::StatusSlot{.nodeId = "status", .state = 0}
                      };
                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(mixedScreen, list, {}, {}, {}, {}, bindStatus(tintedScreen, slots));
                      checks.expect(!frame.has_value() && frame.error() == ms::ScreenError::ScreenNotApproved, "a binding built for another screen is refused");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnboundIndicatorIsDeferred{
    "An indicator with no state bound is deferred rather than painted in a default one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-unbound-is-deferred")
            .Given("a screen whose only live node is an indicator", [] {})
            .When("a frame is recorded with no status binding", [] {})
            .Then("nothing is drawn for it and the frame says so",
                  [] {
                      mdux::spec::Checks checks;
                      Scratch            scratch;
                      auto               list = scratch.list();

                      const auto frame = ms::render(tintedScreen, list);
                      checks.expect(frame.has_value(), "the frame is recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(frame->deferred == 1, std::format("the indicator is deferred, got {}", frame->deferred));
                      checks.expect(frame->states == 0, std::format("no state was drawn, got {}", frame->states));
                      checks.expect(frame->rects == 0, std::format("and no rectangle was recorded, got {}", frame->rects));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBoundStateIsDrawnInItsOwnTint{
    "A bound state paints the node in that state's tint, and another state in another",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-state-tint")
            .Given("an indicator whose two states carry two different tokens", [] {})
            .When("a frame is recorded for each state with no locale bound", [] {})
            .Then("each frame paints the node's whole rectangle in that state's own colour",
                  [] {
                      mdux::spec::Checks checks;

                      const auto tintOf = [&checks](std::uint32_t state) -> std::uint32_t {
                          const std::array slots{
                              ms::StatusSlot{.nodeId = "status", .state = state}
                          };
                          Scratch    scratch;
                          auto       list  = scratch.list();
                          const auto frame = ms::render(tintedScreen, list, {}, {}, {}, {}, bindStatus(tintedScreen, slots));
                          checks.expect(frame.has_value(), std::format("state {} records a frame", state));
                          if (!frame.has_value()) {
                              return 0;
                          }
                          checks.expect(frame->states == 1, std::format("state {} counts one drawn state, got {}", state, frame->states));
                          checks.expect(frame->deferred == 0, std::format("state {} defers nothing, got {}", state, frame->deferred));
                          checks.expect(frame->rects == 1, std::format("state {} records the field alone, got {}", state, frame->rects));

                          const std::span<const draw::UiVertex> vertices = list.vertices();
                          checks.expect(vertices.size() == 4, std::format("state {} records one rectangle, got {} vertices", state, vertices.size()));
                          if (vertices.size() != 4) {
                              return 0;
                          }
                          // The node's whole rectangle, which is what a Bounds golden reads as an
                          // equality: (20, 30) to (120, 70).
                          checks.expect(vertices[0].x == 20.0F && vertices[0].y == 30.0F, "the field starts at the node's corner");
                          checks.expect(vertices[2].x == 120.0F && vertices[2].y == 70.0F, "and ends at its far corner");
                          return vertices[0].color;
                      };

                      // Written out rather than resolved through the function the runtime calls: an
                      // expectation that called it would agree with it whatever it returned.
                      // `Nominal` is {0.13, 0.72, 0.42, 1.0} linear and `Fault` {0.86, 0.20, 0.18, 1.0}.
                      const std::uint32_t nominal = draw::packColor(mdux::core::ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255});
                      const std::uint32_t fault   = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 255});

                      checks.expect(tintOf(0) == nominal, "state 0 is drawn in Theme.Colors.Nominal");
                      checks.expect(tintOf(1) == fault, "state 1 is drawn in Theme.Colors.Fault");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBoundStateDrawsItsOwnWord{
    "With a locale bound, the state on screen draws that state's word over a dimmed field",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-state-word")
            .Given("two states whose words are one block and three", [] {})
            .When("state 1 is bound and a frame is recorded with the locale", [] {})
            .Then("three glyphs are drawn at the node's corner over the field at quarter coverage",
                  [] {
                      mdux::spec::Checks      checks;
                      const ms::ScreenPackage screen = approve(tintedScreen);

                      const std::array slots{
                          ms::StatusSlot{.nodeId = "status", .state = 1}
                      };
                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(screen, list, bindText(screen), {}, {}, {}, bindStatus(screen, slots));

                      checks.expect(frame.has_value(), "the frame is recorded");
                      if (!frame.has_value()) {
                          checks.raise();
                          return;
                      }
                      // The field plus one rectangle per inked glyph. `STR-ALARM` is three blocks, so
                      // four - and `STR-OK`'s single block would be two, which is what makes this
                      // count say *which* state was drawn rather than only that one was.
                      checks.expect(frame->rects == 4, std::format("the field and three glyphs, got {}", frame->rects));
                      checks.expect(frame->states == 1, std::format("one state drawn, got {}", frame->states));

                      const std::span<const draw::UiVertex> vertices = list.vertices();
                      checks.expect(vertices.size() == 16, std::format("four rectangles, got {} vertices", vertices.size()));
                      if (vertices.size() != 16) {
                          checks.raise();
                          return;
                      }

                      // The field, at a quarter of `Fault`'s alpha because a word covers it.
                      const std::uint32_t dimmed = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 64});
                      checks.expect(vertices[0].color == dimmed, "the field dims to boundFieldCoverage under the word");

                      // The ink box in run coordinates is (0, -6), so placing its corner on the node's
                      // corner lands the first glyph at (20, 30) rather than at (20, 24).
                      checks.expect(vertices[4].x == 20.0F && vertices[4].y == 30.0F, "the word's ink starts at the node's corner");
                      const std::uint32_t full = draw::packColor(mdux::core::ColorRgba8{.r = 219, .g = 51, .b = 46, .a = 255});
                      checks.expect(vertices[4].color == full, "and the glyphs are drawn at full tint");

                      // Three glyphs 5px apart, the last starting at 20 + 10.
                      checks.expect(vertices[12].x == 30.0F, std::format("the third glyph is at 30, got {}", vertices[12].x));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aWordThatDoesNotFitRefusesTheFrame{
    "A state's word wider than the node it holds refuses the frame",
    "evidence-unit",
    [] {
        return speclab::Test("medui-status-word-overflowing-its-node")
            .Given("the same indicator in a 6px-wide box", [] {})
            .When("the three-block state is bound and a frame is recorded", [] {})
            .Then("it is TextOverflowsNode and nothing is left recorded",
                  [] {
                      mdux::spec::Checks      checks;
                      const ms::ScreenPackage screen = approve(narrowScreen);

                      const std::array slots{
                          ms::StatusSlot{.nodeId = "status", .state = 1}
                      };
                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(screen, list, bindText(screen), {}, {}, {}, bindStatus(screen, slots));

                      checks.expect(!frame.has_value(), "the frame is refused");
                      if (!frame.has_value()) {
                          checks.expect(frame.error() == ms::ScreenError::TextOverflowsNode,
                                        std::format("reported as TextOverflowsNode, got '{}'", ms::describe(frame.error())));
                      }
                      // Measured before anything is recorded, so the refusal costs no rollback - and
                      // the frame is whole or absent either way, which is what this asserts.
                      checks.expect(list.vertices().empty(), std::format("nothing is left recorded, got {} vertices", list.vertices().size()));

                      // The narrow box still fits the one-block state, so this is a property of the
                      // state on screen rather than of the node.
                      const std::array narrow{
                          ms::StatusSlot{.nodeId = "status", .state = 0}
                      };
                      Scratch    second;
                      auto       secondList = second.list();
                      const auto fits       = ms::render(screen, secondList, bindText(screen), {}, {}, {}, bindStatus(screen, narrow));
                      checks.expect(fits.has_value(), "the state whose word fits still draws");
                      checks.raise();
                  })
            .Execute();
    }};
