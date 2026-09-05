/**
 * @file FieldTests.cpp
 * @brief BDD scenarios for `mdux.medui.field` and the screen runtime's text-input binding (#260).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-010 No on-device text shaping (decision 4, as amended by #258 and #260)
 *
 * Two halves, tested apart for the reason the module is split apart. The grid half reads glyph
 * rectangles back and checks numbers - which cell a character landed in, what a field's envelope
 * bounds, where a caret stands. The binding half checks the join: what `TextInputBinding::create()`
 * refuses once, and what `render()` draws for a node that has a value against one that does not.
 *
 * The fixture font is built so the fixed pitch is *visible* in the numbers: `i` is narrow, `W` is
 * wide, and both occupy a cell of the same width. A proportional pen would put the second character
 * of `iW` somewhere else, and several scenarios below would fail if one were introduced.
 *
 * Three scenarios carry this issue's acceptance rather than ordinary coverage:
 *
 * - `medui-field-envelope-bounds-every-value` - what `measureField()` certifies at build time really
 *   does bound what the runtime draws, checked by drawing values rather than by arguing it follows.
 *   The compile-time check the budget stage performs rests on exactly that implication.
 * - `medui-field-refuses-an-oversized-value` - a value longer than the field is refused, not
 *   truncated. A truncated identifier is a different identifier and looks like a whole one.
 * - `medui-field-refuses-a-glyph-the-package-lacks` - no fallback, which is what makes the charset a
 *   bound on what can be displayed rather than a hint.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.field;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace draw = mdux::draw;
namespace font = mdux::font;

/// Ten pixels per em at ten pixels, so an advance of 1000 units is a pitch of exactly 10px and every
/// expected coordinate below is arithmetic a reader can do in their head.
constexpr std::uint16_t wideAdvance   = 1000;
constexpr std::uint16_t narrowAdvance = 400;

/**
 * @brief A font whose permitted glyphs have deliberately unequal advances.
 *
 * `W` is the widest thing the charset admits, so it is what sets the cell. `i` is narrow, which is
 * what makes the grid observable: on a proportional pen the character after an `i` would move left,
 * and on this grid it does not.
 */
[[nodiscard]] font::FontPackage fixtureFont() {
    font::FontPackage package;
    package.id                     = "field-ui";
    package.unitsPerEm             = 1000;
    package.pixelSize              = 10;
    package.locales                = {"en-US"};
    package.atlas.path             = "atlas.bin";
    package.atlas.width            = 32;
    package.atlas.height           = 32;
    package.atlas.byteLength       = 32 * 32;
    package.atlas.sha256           = std::string(64, 'a');
    package.atlas.occupancyPercent = 25;
    package.glyphs                 = {
        {.codePoint       = U' ',
         .glyphIndex      = 1,
         .advanceWidth    = narrowAdvance,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 0,
         .height          = 0,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 0},
        // A glyph whose bitmap starts *behind* its pen. Not a contrivance: `J`, `T`, `Y`, `_` and
        // `j` all carry `bitmapOriginX = -1` in the committed `dejavu-ui` package, so a field whose
        // value begins with one is an ordinary patient identifier rather than an edge case.
        {.codePoint       = U'J',
         .glyphIndex      = 4,
         .advanceWidth    = narrowAdvance,
         .leftSideBearing = 0,
         .x               = 11,
         .y               = 0,
         .width           = 3,
         .height          = 6,
         .bitmapOriginX   = -1,
         .bitmapOriginY   = 6},
        {.codePoint       = U'W',
         .glyphIndex      = 2,
         .advanceWidth    = wideAdvance,
         .leftSideBearing = 0,
         .x               = 0,
         .y               = 0,
         .width           = 9,
         .height          = 8,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 8},
        {.codePoint       = U'i',
         .glyphIndex      = 3,
         .advanceWidth    = narrowAdvance,
         .leftSideBearing = 0,
         .x               = 9,
         .y               = 0,
         .width           = 2,
         .height          = 6,
         .bitmapOriginX   = 0,
         .bitmapOriginY   = 6},
    };
    package.restrictedCharset = {
        {.first = U' ', .last = U' '},
        {.first = U'J', .last = U'J'},
        {.first = U'W', .last = U'W'},
        {.first = U'i', .last = U'i'}
    };
    return package;
}

const font::FontPackage& theFont() {
    static const font::FontPackage package = fixtureFont();
    return package;
}

/// Storage a caller sizes once, as a device would.
struct Scratch {
    std::array<draw::UiVertex, 512>   vertices{};
    std::array<draw::Index, 768>      indices{};
    std::array<draw::DrawCommand, 16> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};
    }

    [[nodiscard]] draw::DrawList list() {
        auto created = draw::DrawList::create(vertices, indices, commands, budget());
        if (!created.has_value()) {
            throw speclab::core::AssertionFailure("the scratch does not satisfy its own budget", std::source_location::current());
        }
        return std::move(*created);
    }
};

constexpr mdux::core::Rect       node{.x = 20, .y = 30, .width = 100, .height = 40};
constexpr mdux::core::ColorRgba8 ink{.r = 10, .g = 20, .b = 30, .a = 255};

/// The bounding box of everything recorded in `list`, in surface pixels.
struct Box {
    bool           found{false};
    mdux::core::Px left{0};
    mdux::core::Px top{0};
    mdux::core::Px right{0};
    mdux::core::Px bottom{0};
};

[[nodiscard]] Box boxOf(const draw::DrawList& list) {
    Box box;
    for (const draw::UiVertex& vertex : list.vertices()) {
        const auto x = static_cast<mdux::core::Px>(vertex.x);
        const auto y = static_cast<mdux::core::Px>(vertex.y);
        if (!box.found) {
            box = Box{.found = true, .left = x, .top = y, .right = x, .bottom = y};
            continue;
        }
        box.left   = std::min(box.left, x);
        box.top    = std::min(box.top, y);
        box.right  = std::max(box.right, x);
        box.bottom = std::max(box.bottom, y);
    }
    return box;
}

// --- the screen side ------------------------------------------------------------------------

constexpr ms::TextInputSpec patientId{.source = "PATIENT_ID", .colorToken = "Theme.Colors.Title", .maxLength = 4, .charset = {}, .requirement = {}};
constexpr ms::PanelSpec     ground{.colorToken = "Theme.Colors.TopbarBackground"};

constexpr std::array<ms::CompiledNode, 2> inputNodes{
    ms::CompiledNode{.id = "ground",  .bounds = {0, 0, 200, 100},    .payload = ground},
    ms::CompiledNode{ .id = "entry", .bounds = {20, 30, 100, 40}, .payload = patientId}
};

constexpr draw::DrawBudget testBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

constexpr std::array placeholderApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "field-text", .packageSha256 = {1}}
};

constexpr ms::ScreenPackage inputScreen{.id                   = "field",
                                        .schemaVersion        = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth         = 200,
                                        .surfaceHeight        = 100,
                                        .approvedTextPackages = placeholderApprovals,
                                        .nodes                = inputNodes,
                                        .budget               = testBudget};

static_assert(inputScreen.validate().has_value(), "the reference screen must be one a device could hold");

/**
 * @brief The text binding a field needs, for the font it carries rather than for its words.
 *
 * `ReadingBinding`'s arrangement, and here for its reason: `needsTextPackageApproval()` puts
 * `TextInput` among the components whose screen must list an approved text package, so a screen with
 * a field on it always has a `TextBinding` to be had - and its font is one whose identity
 * `create()` authenticated. The package below carries one run nothing names; this fixture is a
 * *font* carrier, built through the real `create()` rather than around it.
 */
struct FontCarrier {
    mdux::text::TextPackage  package;
    std::array<std::byte, 6> sidecar{};
    std::string              canonical;
    ms::TextPackageApproval  approval{};

    FontCarrier() {
        package.header.id         = "field-text";
        package.header.kind       = std::string{mdux::text::packageKind};
        package.atlasId           = theFont().id;
        package.locale            = "en-US";
        package.sidecarPath       = "runs.bin";
        package.sidecarByteLength = sidecar.size();
        package.sidecarSha256     = mdux::evidence::sha256(sidecar);
        package.runs.push_back(
            mdux::text::TextRun{.id = "STR-UNUSED", .byteOffset = 0, .byteLength = sidecar.size(), .sha256 = mdux::evidence::sha256(sidecar)});

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

const FontCarrier& theCarrier() {
    static const FontCarrier carrier;
    return carrier;
}

/// The reference screen carrying the carrier's real approval, so `TextBinding::create()` accepts it.
///
/// A `Digest` of real bytes is not a constant expression, which is why the `constexpr` screen above
/// holds a placeholder and this replaces it.
[[nodiscard]] ms::ScreenPackage approvedScreen() {
    ms::ScreenPackage bound    = inputScreen;
    bound.approvedTextPackages = std::span{&theCarrier().approval, 1};
    return bound;
}

/// The binding `render()` takes a field's font from. Built once, as a device would at start-up.
[[nodiscard]] ms::TextBinding textOnlyBinding(const ms::ScreenPackage& screen) {
    auto made = ms::TextBinding::create(screen, theFont(), theCarrier().package, theCarrier().bytes(), theCarrier().sidecar);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::format("the fixture text binding is invalid: {}", ms::describe(made.error())),
                                              std::source_location::current());
    }
    return *made;
}

/// The error `TextInputBinding::create()` reported, or nullopt when it accepted the slots.
[[nodiscard]] std::optional<ms::ScreenError> refusalOf(const ms::ScreenPackage& screen, std::span<const ms::TextInputSlot> slots) {
    auto made = ms::TextInputBinding::create(screen, slots);
    return made.has_value() ? std::optional<ms::ScreenError>{} : std::optional{made.error()};
}

}  // namespace

const mdux::spec::Register aCellIsTheWidestGlyphTheCharsetAdmits{"A cell is as wide as the widest glyph the package's charset admits", "evidence-unit", [] {
                                                                     return speclab::Test("medui-field-cell-width")
                                                                         .Given("a package whose charset admits a 1000-unit W and a 400-unit i", [] {})
                                                                         .When("the cell width is derived", [] {})
                                                                         .Then("it is the wide one, in pixels, and a narrow character does not shrink it",
                                                                               [] {
                                                                                   mdux::spec::Checks checks;

                                                                                   const auto pitch = ms::cellWidth(theFont());
                                                                                   checks.expect(pitch.has_value(),
                                                                                                 "a package with a charset has a cell width");
                                                                                   if (!pitch.has_value()) {
                                                                                       checks.raise();
                                                                                       return;
                                                                                   }
                                                                                   // 1000 units at 10px per 1000 units per em.
                                                                                   checks.expect(*pitch == 10, std::format("the cell is 10px, got {}", *pitch));

                                                                                   // A package whose charset admits nothing has no cell to derive, and says so
                                                                                   // rather than returning a zero that would draw every character on top of the
                                                                                   // last one.
                                                                                   font::FontPackage empty = fixtureFont();
                                                                                   empty.restrictedCharset.clear();
                                                                                   const auto none = ms::cellWidth(empty);
                                                                                   checks.expect(
                                                                                       !none.has_value() && none.error() == ms::FieldError::EmptyCharset,
                                                                                       "a package with no charset is EmptyCharset rather than a zero width");
                                                                                   checks.raise();
                                                                               })
                                                                         .Execute();
                                                                 }};

const mdux::spec::Register theGridDoesNotMoveWithTheValue{
    "Every character lands in its own cell, whatever the characters before it are",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-grid-is-fixed-pitch")
            .Given("a narrow character followed by a wide one, and the reverse", [] {})
            .When("each is recorded into the same field", [] {})
            .Then("the second character starts one pitch from the first in both",
                  [] {
                      mdux::spec::Checks checks;

                      const auto secondCellLeft = [&checks](std::array<char32_t, 2> value) {
                          Scratch    scratch;
                          auto       list     = scratch.list();
                          const auto recorded = ms::recordField(list, theFont(), node, 4, value, std::nullopt, ink);
                          checks.expect(recorded.has_value(), "the value is recorded");
                          if (!recorded.has_value()) {
                              return mdux::core::Px{0};
                          }
                          // Two glyphs, four vertices each, in cell order - so the second cell's
                          // rectangle starts at vertex 4.
                          const std::span<const draw::UiVertex> vertices = list.vertices();
                          checks.expect(vertices.size() == 8, std::format("two glyphs, got {} vertices", vertices.size()));
                          if (vertices.size() != 8) {
                              return mdux::core::Px{0};
                          }
                          return static_cast<mdux::core::Px>(vertices[4].x);
                      };

                      // The property a proportional pen would break: `i` advances 400 units and `W`
                      // 1000, so a pen would put the second character of `iW` four pixels along and
                      // the second of `Wi` ten. On the grid both are one pitch from cell 0.
                      //
                      // Cell 0 is `fieldOriginX()` inside the node rather than on its edge, because
                      // this fixture carries a glyph whose bitmap starts behind its pen - asked of
                      // the module rather than written out, so this scenario keeps testing the grid
                      // rather than a number that happened to be true when it was written.
                      const auto origin = ms::fieldOriginX(theFont());
                      const auto pitch  = ms::cellWidth(theFont());
                      checks.expect(origin.has_value() && pitch.has_value(), "the grid has an origin and a pitch");
                      if (!origin.has_value() || !pitch.has_value()) {
                          checks.raise();
                          return;
                      }
                      const auto secondCell = static_cast<mdux::core::Px>(node.x + *origin + *pitch);

                      const mdux::core::Px afterNarrow = secondCellLeft({U'i', U'W'});
                      const mdux::core::Px afterWide   = secondCellLeft({U'W', U'i'});
                      checks.expect(afterNarrow == secondCell, std::format("the character after a narrow one is at {}, got {}", secondCell, afterNarrow));
                      checks.expect(afterWide == secondCell, std::format("the character after a wide one is at {}, got {}", secondCell, afterWide));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theEnvelopeBoundsEveryValue{
    "measureField() bounds every value the field can ever display",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-envelope-bounds-every-value")
            .Given("a four-cell field and the envelope the compiler would measure it by", [] {})
            .When("the extreme values are drawn into it", [] {})
            .Then("everything recorded lies inside that envelope, placed at the node's corner",
                  [] {
                      // The implication the build-time check rests on, checked by drawing rather
                      // than by argument. A compiler that signed a box using this envelope would be
                      // signing a claim about frames, so the frames are what this measures.
                      mdux::spec::Checks checks;

                      const auto envelope = ms::measureField(theFont(), 4);
                      checks.expect(envelope.has_value(), "the field has an envelope");
                      if (!envelope.has_value()) {
                          checks.raise();
                          return;
                      }

                      const std::array<std::array<char32_t, 4>, 3> values{
                          std::array<char32_t, 4>{U'W', U'W', U'W', U'W'},
                          std::array<char32_t, 4>{U'i', U'i', U'i', U'i'},
                          std::array<char32_t, 4>{U'W', U'i', U' ', U'W'}
                      };

                      for (const std::array<char32_t, 4>& value : values) {
                          for (const std::optional<std::size_t> caret : {std::optional<std::size_t>{}, std::optional<std::size_t>{4}}) {
                              Scratch    scratch;
                              auto       list     = scratch.list();
                              const auto recorded = ms::recordField(list, theFont(), node, 4, value, caret, ink);
                              checks.expect(recorded.has_value(), "the value is recorded");
                              if (!recorded.has_value()) {
                                  continue;
                              }
                              const Box box = boxOf(list);
                              if (!box.found) {
                                  continue;
                              }
                              checks.expect(box.left >= node.x, std::format("nothing is drawn left of the node, got {}", box.left));
                              checks.expect(box.right <= node.x + static_cast<mdux::core::Px>(envelope->width),
                                            std::format("nothing reaches past the envelope's width, got {} against {}", box.right, envelope->width));
                              checks.expect(box.top >= node.y, std::format("nothing is drawn above the node, got {}", box.top));
                              checks.expect(box.bottom <= node.y + static_cast<mdux::core::Px>(envelope->height),
                                            std::format("nothing reaches past the envelope's height, got {} against {}", box.bottom, envelope->height));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aNegativeBearingStaysInsideTheNode{
    "A glyph whose bitmap starts behind its pen still lands inside the node",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-negative-left-bearing")
            .Given("a value beginning with a glyph whose bitmapOriginX is -1", [] {})
            .When("it is drawn into a field, and a frame is recorded through the screen runtime", [] {})
            .Then("nothing is recorded left of the node, and the frame is not refused",
                  [] {
                      // The regression this scenario exists for: `measureField()` reserves room for
                      // a negative left bearing - its width is `inkRight - inkLeft` - and the
                      // placement did not, so cell 0's ink landed one pixel left of the node. The
                      // screen runtime's own overflow check then refused the *whole frame*, which on
                      // the committed screen means a patient identifier beginning with `J` blanks
                      // the display. The measurement and the placement have to agree about the
                      // origin, and this is what says they do.
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 2> value{U'J', U'W'};

                      Scratch    scratch;
                      auto       list     = scratch.list();
                      const auto recorded = ms::recordField(list, theFont(), node, 4, value, std::optional<std::size_t>{2}, ink);
                      checks.expect(recorded.has_value(), "the value is recorded");

                      const Box box = boxOf(list);
                      checks.expect(box.found, "something was drawn");
                      if (box.found) {
                          checks.expect(box.left >= node.x, std::format("nothing is drawn left of the node's edge {}, got {}", node.x, box.left));
                      }

                      // And through the runtime, where the refusal actually bit: `readingFitsNode()`
                      // rejects a vertex left of the node's origin, so this frame was refused whole.
                      constexpr std::array<char32_t, 2> entryValue{U'J', U'W'};
                      const std::array                  slots{
                          ms::TextInputSlot{.nodeId = "entry", .text = entryValue, .caret = std::optional<std::size_t>{2}}
                      };
                      const ms::ScreenPackage screen  = approvedScreen();
                      auto                    binding = ms::TextInputBinding::create(screen, slots);
                      checks.expect(binding.has_value(), "the binding is made");
                      if (!binding.has_value()) {
                          checks.raise();
                          return;
                      }

                      Scratch    frameScratch;
                      auto       frameList = frameScratch.list();
                      const auto frame     = ms::render(screen, frameList, textOnlyBinding(screen), {}, {}, {}, {}, *binding);
                      checks.expect(frame.has_value(), std::format("the frame is recorded, got '{}'", frame.has_value() ? "" : ms::describe(frame.error())));
                      if (frame.has_value()) {
                          checks.expect(frame->fields == 1, std::format("the field was drawn, got {}", frame->fields));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anOversizedValueIsRefused{
    "A value with more characters than the field has cells is refused, not truncated",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-refuses-an-oversized-value")
            .Given("a three-cell field and a four-character value", [] {})
            .When("it is offered to the field", [] {})
            .Then("nothing is drawn and the refusal names the length",
                  [] {
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 4> tooLong{U'W', U'i', U'W', U'i'};

                      Scratch    scratch;
                      auto       list     = scratch.list();
                      const auto recorded = ms::recordField(list, theFont(), node, 3, tooLong, std::nullopt, ink);

                      checks.expect(!recorded.has_value(), "the value is refused");
                      if (!recorded.has_value()) {
                          checks.expect(recorded.error() == ms::FieldError::TextTooLong,
                                        std::format("reported as TextTooLong, got '{}'", ms::describe(recorded.error())));
                      }
                      // The half that matters: three of the four characters are not drawn instead.
                      // `WiW` in a box labelled with what it is would read as a whole identifier.
                      checks.expect(list.vertices().empty(), std::format("nothing is left recorded, got {} vertices", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aGlyphThePackageLacksIsRefused{
    "A character the font package cannot draw refuses the field rather than substituting one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-refuses-a-glyph-the-package-lacks")
            .Given("a value carrying a character outside the package", [] {})
            .When("it is offered to the field", [] {})
            .Then("it is GlyphNotInPackage and nothing is left recorded",
                  [] {
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 2> unknown{U'W', U'Z'};

                      Scratch    scratch;
                      auto       list     = scratch.list();
                      const auto recorded = ms::recordField(list, theFont(), node, 4, unknown, std::nullopt, ink);

                      checks.expect(!recorded.has_value(), "the value is refused");
                      if (!recorded.has_value()) {
                          checks.expect(recorded.error() == ms::FieldError::GlyphNotInPackage,
                                        std::format("reported as GlyphNotInPackage, got '{}'", ms::describe(recorded.error())));
                      }
                      // Including the `W` that came before it: a field showing the characters up to
                      // the first unknown one is a truncated identifier by another route.
                      checks.expect(list.vertices().empty(), std::format("nothing is left recorded, got {} vertices", list.vertices().size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aGlyphOutsideTheCharsetIsRefused{
    "A glyph the package carries but its charset does not admit is refused",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-charset-is-the-bound")
            .Given("a package whose glyph table reaches past its declared charset", [] {})
            .When("a value naming one of those glyphs is drawn", [] {})
            .Then("it is refused, because the charset is what the cell was sized from",
                  [] {
                      // `FontPackage::validate()` requires every *permitted* code point to have a
                      // glyph; it does not require the glyph table to stop at the charset. So a
                      // package can carry a glyph nothing declared, and `find()` would happily
                      // return it - into a cell whose width came from the charset alone, which that
                      // glyph is under no obligation to fit. Asking the charset rather than the
                      // table is what makes "the restricted charset bounds what can be displayed"
                      // true of the runtime and not only of the compiler.
                      mdux::spec::Checks checks;

                      font::FontPackage narrowed = fixtureFont();
                      narrowed.restrictedCharset = {
                          {.first = U' ', .last = U' '},
                          {.first = U'i', .last = U'i'}
                      };
                      checks.expect(narrowed.find(U'W') != nullptr, "the package still carries the glyph");
                      checks.expect(!narrowed.permits(U'W'), "and its charset no longer admits it");

                      constexpr std::array<char32_t, 1> outside{U'W'};
                      Scratch                           scratch;
                      auto                              list     = scratch.list();
                      const auto                        recorded = ms::recordField(list, narrowed, node, 4, outside, std::nullopt, ink);

                      checks.expect(!recorded.has_value(), "the value is refused");
                      if (!recorded.has_value()) {
                          checks.expect(recorded.error() == ms::FieldError::GlyphNotInPackage,
                                        std::format("reported as GlyphNotInPackage, got '{}'", ms::describe(recorded.error())));
                      }
                      checks.expect(list.vertices().empty(), "and nothing is left recorded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anInkFreeCharsetIsRefusedByBoth{
    "A charset whose every glyph is blank is refused rather than measured as nothing",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-charset-with-no-ink")
            .Given("a package admitting only a blank", [] {})
            .When("a field is measured against it, and a caret drawn into one", [] {})
            .Then("both refuse, rather than one measuring zero and the other drawing nothing",
                  [] {
                      // The silent-absence failure this closes: the measurement used to call such a
                      // package legitimate with a height of zero, and the placement then recorded no
                      // caret and returned success. A caller that asked for a caret got neither a
                      // caret nor a reason.
                      mdux::spec::Checks checks;

                      font::FontPackage blanks = fixtureFont();
                      blanks.restrictedCharset = {
                          {.first = U' ', .last = U' '}
                      };

                      const auto measured = ms::measureField(blanks, 4);
                      checks.expect(!measured.has_value() && measured.error() == ms::FieldError::CharsetHasNoInk,
                                    "the measurement refuses a charset with no ink");

                      Scratch                           scratch;
                      auto                              list = scratch.list();
                      constexpr std::array<char32_t, 1> blank{U' '};
                      const auto                        recorded = ms::recordField(list, blanks, node, 4, blank, std::optional<std::size_t>{1}, ink);
                      checks.expect(!recorded.has_value() && recorded.error() == ms::FieldError::CharsetHasNoInk,
                                    "and the placement refuses it in the same words");
                      checks.expect(list.vertices().empty(), "with nothing left recorded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theCaretStandsOnACellBoundary{
    "A caret stands on a cell boundary, and one past the last cell is where a full field puts it",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-caret-position")
            .Given("a four-cell field holding two characters", [] {})
            .When("the caret is placed at 2, at 4, and at 5", [] {})
            .Then("the first two draw a column at their own boundary and the third is refused",
                  [] {
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 2> value{U'W', U'W'};

                      const auto caretLeft = [&](std::size_t caret) {
                          Scratch    scratch;
                          auto       list     = scratch.list();
                          const auto recorded = ms::recordField(list, theFont(), node, 4, value, caret, ink);
                          checks.expect(recorded.has_value(), std::format("a caret at {} is drawn", caret));
                          if (!recorded.has_value()) {
                              return mdux::core::Px{-1};
                          }
                          // Two glyphs then the caret, so the caret's rectangle starts at vertex 8.
                          const std::span<const draw::UiVertex> vertices = list.vertices();
                          checks.expect(vertices.size() == 12, std::format("two glyphs and a caret, got {} vertices", vertices.size()));
                          if (vertices.size() != 12) {
                              return mdux::core::Px{-1};
                          }
                          return static_cast<mdux::core::Px>(vertices[8].x);
                      };

                      const auto origin = ms::fieldOriginX(theFont());
                      const auto pitch  = ms::cellWidth(theFont());
                      checks.expect(origin.has_value() && pitch.has_value(), "the grid has an origin and a pitch");
                      if (!origin.has_value() || !pitch.has_value()) {
                          checks.raise();
                          return;
                      }
                      const auto boundary = [&](std::int64_t cell) {
                          return static_cast<mdux::core::Px>(node.x + *origin + (cell * *pitch));
                      };

                      checks.expect(caretLeft(2) == boundary(2), std::format("a caret before cell 2 stands at {}, got {}", boundary(2), caretLeft(2)));
                      // `cells` itself is a position - the caret after the last character of a full
                      // field - and it is the one a box has to have reserved room for.
                      checks.expect(caretLeft(4) == boundary(4), std::format("a caret at the field's end stands at {}, got {}", boundary(4), caretLeft(4)));

                      Scratch    scratch;
                      auto       list    = scratch.list();
                      const auto refused = ms::recordField(list, theFont(), node, 4, value, std::optional<std::size_t>{5}, ink);
                      checks.expect(!refused.has_value() && refused.error() == ms::FieldError::CaretOutOfRange,
                                    "a caret past the field's end is CaretOutOfRange");
                      checks.expect(list.vertices().empty(), "and nothing is left recorded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFieldTheRuntimeWillNotDrawIsRefused{
    "A field with no cells, or more than the cap, is refused by measurement and by drawing alike",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-cell-count-bounds")
            .Given("a field of zero cells and one past maxFieldCells", [] {})
            .When("each is measured and each is drawn", [] {})
            .Then("both refuse, with the same error from both entry points",
                  [] {
                      mdux::spec::Checks checks;

                      checks.expect(ms::fieldAccepts(0, 0, std::nullopt).error() == ms::FieldError::NoCells, "a field of no cells is NoCells");
                      checks.expect(ms::fieldAccepts(ms::maxFieldCells + 1, 0, std::nullopt).error() == ms::FieldError::TooManyCells,
                                    "a field past the cap is TooManyCells");

                      // The same answers through the two functions that matter, which is what having
                      // one predicate is for: a compiler asking `measureField()` and a device asking
                      // `recordField()` cannot disagree about which fields exist.
                      const auto measuredZero = ms::measureField(theFont(), 0);
                      checks.expect(!measuredZero.has_value() && measuredZero.error() == ms::FieldError::NoCells, "measureField() refuses zero cells");
                      const auto measuredHuge = ms::measureField(theFont(), ms::maxFieldCells + 1);
                      checks.expect(!measuredHuge.has_value() && measuredHuge.error() == ms::FieldError::TooManyCells, "measureField() refuses the cap");

                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto drawn = ms::recordField(list, theFont(), node, 0, {}, std::nullopt, ink);
                      checks.expect(!drawn.has_value() && drawn.error() == ms::FieldError::NoCells, "recordField() refuses zero cells");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyFieldErrorHasItsOwnDescription{"Every FieldError has its own description", "evidence-unit", [] {
                                                                   return speclab::Test("medui-field-error-descriptions")
                                                                       .Given("the enumerators this module can report", [] {})
                                                                       .When("each is described", [] {})
                                                                       .Then("each is non-empty and no two are the same sentence",
                                                                             [] {
                                                                                 mdux::spec::Checks            checks;
                                                                                 constexpr std::array          errors{ms::FieldError::NoCells,
                                                                                                             ms::FieldError::TooManyCells,
                                                                                                             ms::FieldError::TextTooLong,
                                                                                                             ms::FieldError::CaretOutOfRange,
                                                                                                             ms::FieldError::GlyphNotInPackage,
                                                                                                             ms::FieldError::EmptyCharset,
                                                                                                             ms::FieldError::CharsetHasNoInk,
                                                                                                             ms::FieldError::ListRejected};
                                                                                 std::vector<std::string_view> seen;
                                                                                 for (const ms::FieldError error : errors) {
                                                                                     const std::string_view described = ms::describe(error);
                                                                                     checks.expect(!described.empty(), "the error is described");
                                                                                     checks.expect(std::ranges::find(seen, described) == seen.end(),
                                                                                                   std::format("'{}' is not a repeat", described));
                                                                                     seen.push_back(described);
                                                                                 }
                                                                                 checks.raise();
                                                                             })
                                                                       .Execute();
                                                               }};

const mdux::spec::Register slotsAreCheckedAgainstTheScreen{
    "A slot naming no input, naming one twice, or carrying an oversized value is refused",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-slot-names-are-checked")
            .Given("slots that are wrong in one way each", [] {})
            .When("each is offered to create()", [] {})
            .Then("each reports its own error, at start-up rather than in a frame",
                  [] {
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 2> value{U'W', U'i'};

                      const std::array mistyped{
                          ms::TextInputSlot{.nodeId = "entrie", .text = value, .caret = std::nullopt}
                      };
                      checks.expect(refusalOf(inputScreen, mistyped) == ms::ScreenError::UnknownTextInputNode, "a mistyped node id is UnknownTextInputNode");

                      const std::array wrongKind{
                          ms::TextInputSlot{.nodeId = "ground", .text = value, .caret = std::nullopt}
                      };
                      checks.expect(refusalOf(inputScreen, wrongKind) == ms::ScreenError::UnknownTextInputNode,
                                    "a node of another kind is UnknownTextInputNode");

                      const std::array twice{
                          ms::TextInputSlot{.nodeId = "entry", .text = value, .caret = std::nullopt},
                          ms::TextInputSlot{.nodeId = "entry", .text = value, .caret = std::nullopt}
                      };
                      checks.expect(refusalOf(inputScreen, twice) == ms::ScreenError::DuplicateTextInput, "two slots for one node are DuplicateTextInput");

                      // The node's `max_length` is 4, so a five-character value is one this screen
                      // could never display - reported at the join rather than discovered per frame.
                      constexpr std::array<char32_t, 5> tooLong{U'W', U'W', U'W', U'W', U'W'};
                      const std::array                  oversized{
                          ms::TextInputSlot{.nodeId = "entry", .text = tooLong, .caret = std::nullopt}
                      };
                      checks.expect(refusalOf(inputScreen, oversized) == ms::ScreenError::FieldRefused, "a value past max_length is FieldRefused");

                      const std::array caretPastEnd{
                          ms::TextInputSlot{.nodeId = "entry", .text = value, .caret = std::optional<std::size_t>{5}}
                      };
                      checks.expect(refusalOf(inputScreen, caretPastEnd) == ms::ScreenError::FieldRefused, "a caret past the field is FieldRefused");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnboundInputIsDeferred{"An input with no value bound is deferred rather than drawn as an empty box", "evidence-unit", [] {
                                                        return speclab::Test("medui-field-unbound-is-deferred")
                                                            .Given("a screen carrying a TextInput", [] {})
                                                            .When("a frame is recorded with no text-input binding", [] {})
                                                            .Then("nothing is drawn for it and the frame says so",
                                                                  [] {
                                                                      mdux::spec::Checks checks;
                                                                      Scratch            scratch;
                                                                      auto               list = scratch.list();

                                                                      const auto frame = ms::render(inputScreen, list);
                                                                      checks.expect(frame.has_value(), "the frame is recorded");
                                                                      if (!frame.has_value()) {
                                                                          checks.raise();
                                                                          return;
                                                                      }
                                                                      // An empty box would say the operator's entry was blank; an absent one says
                                                                      // the host has not supplied it. Those are different facts about a patient
                                                                      // identifier, and only one of them is true at start-up.
                                                                      checks.expect(frame->deferred == 1,
                                                                                    std::format("the input is deferred, got {}", frame->deferred));
                                                                      checks.expect(frame->fields == 0,
                                                                                    std::format("no field was drawn, got {}", frame->fields));
                                                                      // The panel beside it still draws, so this is a deferral rather than a frame
                                                                      // that failed.
                                                                      checks.expect(frame->rects == 1,
                                                                                    std::format("the panel is drawn, got {} rectangles", frame->rects));
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register aBindingBuiltForOneScreenIsRefusedByAnother{
    "A binding built for one screen is refused by another",
    "evidence-unit",
    [] {
        return speclab::Test("medui-field-binding-is-screen-specific")
            .Given("a binding made against one screen", [] {})
            .When("it is offered to a screen with a different id", [] {})
            .Then("the frame is refused and nothing is left recorded",
                  [] {
                      mdux::spec::Checks                checks;
                      constexpr std::array<char32_t, 2> value{U'W', U'i'};
                      const std::array                  slots{
                          ms::TextInputSlot{.nodeId = "entry", .text = value, .caret = std::nullopt}
                      };

                      auto binding = ms::TextInputBinding::create(inputScreen, slots);
                      checks.expect(binding.has_value(), "the binding is made");
                      if (!binding.has_value()) {
                          checks.raise();
                          return;
                      }

                      ms::ScreenPackage other = inputScreen;
                      other.id                = "another-screen";

                      Scratch    scratch;
                      auto       list  = scratch.list();
                      const auto frame = ms::render(other, list, {}, {}, {}, {}, {}, *binding);
                      checks.expect(!frame.has_value() && frame.error() == ms::ScreenError::ScreenNotApproved, "the substituted screen is refused");
                      checks.expect(list.vertices().empty(), "and nothing is left recorded");
                      checks.raise();
                  })
            .Execute();
    }};
