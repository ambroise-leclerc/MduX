/**
 * @file ReadingTests.cpp
 * @brief BDD scenarios for `mdux.medui.reading` and the screen runtime's reading binding (#258).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-010 No on-device text shaping (decision 4, as amended by #258)
 *
 * Two halves, tested apart for the reason the module is split apart. The pattern half reads glyph
 * rectangles back and checks numbers - which digit went where, what a pattern's envelope bounds,
 * which alphabet a slot character belongs to. The binding half checks the join: what
 * `ReadingBinding::create()` refuses once, and what `render()` composes for a node that has a value
 * against one that does not.
 *
 * Three scenarios carry this issue's acceptance rather than ordinary coverage:
 *
 * - `reading-refuses-an-oversized-value` - a value with more digits than its slots is refused, not
 *   truncated. A truncating implementation shows `9.9` for `109.9` in a box whose unit says mmHg,
 *   which is the single worst thing this module could do and the one that looks most like a reading.
 * - `reading-envelope-bounds-every-value` - what `measurePattern()` certifies at build time really
 *   does bound what the runtime draws, checked by drawing the extremes rather than by argument.
 *   ADR-010 decision 4's amendment rests on exactly that implication.
 * - `screen-reading-overflowing-its-node-is-refused` - the runtime re-check that makes a
 *   host-supplied pattern safe when the table the device holds is not the table the compiler
 *   measured.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace core = mdux::core;
namespace draw = mdux::draw;
namespace font = mdux::font;

/// Every glyph in the fixture font is this size, so an extent is countable by hand.
constexpr std::uint32_t glyphWidth  = 8;
constexpr std::uint32_t glyphHeight = 10;

/// One em per glyph at ten pixels per em, so the pen advances exactly 10px per position and a
/// pattern of n characters has its last glyph starting at 10(n-1). Chosen so every expected extent
/// in this file is arithmetic a reader can do in their head rather than a number copied from a run.
constexpr std::uint16_t advanceUnits = 1000;

/**
 * @brief A font package carrying the digits and the literals these patterns need.
 *
 * A real `mdux::font::FontPackage` that passes `validate()`; what is synthetic is the vocabulary and
 * the round metrics, not the format. Digits share one advance because `validate()` requires it - the
 * tabular-figure rule from #161 - which is also what stops a clock jittering as its seconds change.
 */
[[nodiscard]] font::FontPackage fixtureFont() {
    font::FontPackage package;
    package.id         = "reading-ui";
    package.unitsPerEm = 1000;
    package.pixelSize  = 10;
    package.locales    = {"en-US"};
    package.atlas =
        font::AtlasMetrics{.path = "atlas.bin", .width = 128, .height = 128, .byteLength = 128 * 128, .sha256 = std::string(64, 'a'), .occupancyPercent = 50};

    const auto append = [&package](char32_t codePoint, bool blank) {
        const auto slot = static_cast<std::uint32_t>(package.glyphs.size());
        package.glyphs.push_back(font::GlyphRecord{.codePoint       = codePoint,
                                                   .glyphIndex      = static_cast<std::uint16_t>(slot + 1),
                                                   .advanceWidth    = advanceUnits,
                                                   .leftSideBearing = 0,
                                                   .x               = (slot % 8) * glyphWidth,
                                                   .y               = (slot / 8) * glyphHeight,
                                                   .width           = blank ? 0 : glyphWidth,
                                                   .height          = blank ? 0 : glyphHeight,
                                                   .bitmapOriginX   = 0,
                                                   .bitmapOriginY   = blank ? 0 : static_cast<std::int32_t>(glyphHeight)});
    };

    append(U' ', true);
    for (char32_t point = U'-'; point <= U':'; ++point) {
        // U+002D to U+003A: the hyphen, the point, the solidus, the ten digits and the colon - every
        // literal a clock rendering or a numeric template in this file needs, in one contiguous run.
        append(point, false);
    }
    append(U'H', false);
    append(U'g', false);
    append(U'm', false);

    package.restrictedCharset = {
        font::CharsetRange{.first = U' ', .last = U' '},
        font::CharsetRange{.first = U'-', .last = U':'},
        font::CharsetRange{.first = U'H', .last = U'H'},
        font::CharsetRange{.first = U'g', .last = U'g'},
        font::CharsetRange{.first = U'm', .last = U'm'}
    };
    return package;
}

const font::FontPackage& theFont() {
    static const font::FontPackage package = fixtureFont();
    return package;
}

/**
 * @brief The text binding a reading needs, for the font it carries rather than for its words.
 *
 * `ReadingBinding` explains why the metrics come from here: `needsTextPackageApproval()` puts
 * `NumericDisplay` and `Clock` among the components whose screen must list an approved text package,
 * so a screen with a reading on it always has a `TextBinding` to be had - and its font package is
 * one whose identity `create()` authenticated.
 *
 * The package below carries one run nothing on these screens names. That is the point: this fixture
 * is a *font* carrier, and it is built through the real `create()` rather than around it so the
 * suite exercises the join a device performs instead of a shortcut only a test has.
 */
struct FontCarrier {
    mdux::text::TextPackage  package;
    std::array<std::byte, 6> sidecar{};
    std::string              canonical;
    ms::TextPackageApproval  approval{};

    FontCarrier() {
        package.header.id         = "reading-text";
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

/// A screen with the carrier's real approval on it, and the binding that joins the two.
///
/// The digest is computed rather than written, so a fixture cannot drift from the bytes above and
/// start exercising the rejection path while claiming to measure the accepted one. That is why the
/// `constexpr` screens below carry a placeholder approval for their `static_assert` and this
/// replaces it: a `Digest` of real bytes is not a constant expression.
[[nodiscard]] ms::ScreenPackage approve(const ms::ScreenPackage& screen) {
    ms::ScreenPackage bound    = screen;
    bound.approvedTextPackages = std::span{&theCarrier().approval, 1};
    return bound;
}

/// The binding `render()` takes its font from. Built once, as a device would at start-up.
[[nodiscard]] ms::TextBinding bindText(const ms::ScreenPackage& screen) {
    auto made = ms::TextBinding::create(screen, theFont(), theCarrier().package, theCarrier().bytes(), theCarrier().sidecar);
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

constexpr core::Rect       node{.x = 20, .y = 30, .width = 200, .height = 40};
constexpr core::ColorRgba8 digits{.r = 33, .g = 184, .b = 107, .a = 255};

/// The box every recorded vertex lies in.
struct VertexBox {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
};

[[nodiscard]] std::optional<VertexBox> boxOf(std::span<const draw::UiVertex> vertices) noexcept {
    if (vertices.empty()) {
        return std::nullopt;
    }
    VertexBox box{.left = vertices[0].x, .top = vertices[0].y, .right = vertices[0].x, .bottom = vertices[0].y};
    for (const draw::UiVertex& vertex : vertices) {
        box.left   = std::min(box.left, vertex.x);
        box.top    = std::min(box.top, vertex.y);
        box.right  = std::max(box.right, vertex.x);
        box.bottom = std::max(box.bottom, vertex.y);
    }
    return box;
}

/// The x of each recorded glyph's left edge, in record order. One quad per glyph, four vertices each.
[[nodiscard]] std::vector<float> glyphLefts(std::span<const draw::UiVertex> vertices) {
    std::vector<float> lefts;
    for (std::size_t index = 0; index + 3 < vertices.size(); index += 4) {
        lefts.push_back(vertices[index].x);
    }
    return lefts;
}

void requireRecorded(core::ResultVoid<ms::ReadingError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
}

[[nodiscard]] ms::ReadingError
requireRefused(core::ResultVoid<ms::ReadingError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: expected a refusal but the reading was recorded", what), where);
    }
    return result.error();
}

// ---------------------------------------------------------------------------
// The pattern model
// ---------------------------------------------------------------------------

const mdux::spec::Register theTwoAlphabetsStayApart{
    "A numeric template's letters are literals, and a clock's are digit slots",
    "evidence-unit",
    [] {
        return speclab::Test("reading-two-alphabets")
            .Given("the character 'H', which appears in both an hour slot and the unit mmHg", [] {})
            .When("it is classified under each pattern kind", [] {})
            .Then("it is a slot for a clock and a literal for a template",
                  [] {
                      // The reason the numeric slot character is `#` rather than a letter. Merging
                      // the two alphabets would draw a digit in the middle of `mmHg`, which is a
                      // wrong reading that still looks like a reading.
                      mdux::spec::Checks checks;
                      checks.expect(ms::isDigitSlot('H', ms::PatternKind::Clock), "'H' is an hour slot in a clock rendering");
                      checks.expect(!ms::isDigitSlot('H', ms::PatternKind::Numeric), "'H' is a literal in a numeric template");
                      checks.expect(ms::isDigitSlot('#', ms::PatternKind::Numeric), "'#' is the numeric digit slot");
                      checks.expect(!ms::isDigitSlot('#', ms::PatternKind::Clock), "'#' is a literal in a clock rendering");

                      checks.expect(ms::slotAt('H', ms::PatternKind::Clock).count == 10, "a clock's 'H' admits ten digits");
                      checks.expect(ms::slotAt('H', ms::PatternKind::Numeric).count == 1, "a template's 'H' admits only itself");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register digitsFillSlotsMostSignificantFirst{
    "A value fills its slots most significant first, zero-padded",
    "evidence-unit",
    [] {
        struct State {
            Scratch                  scratch;
            std::vector<float>       lefts;
            std::optional<VertexBox> box;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-digits-fill-slots")
            .Given("the pattern '##.# mmHg' and the reading 74 tenths",
                   [state] {
                       draw::DrawList list = state->scratch.list();
                       requireRecorded(ms::recordNumeric(list, theFont(), node, "##.# mmHg", 74, digits), "the reading");
                       state->lefts = glyphLefts(list.vertices());
                       state->box   = boxOf(list.vertices());
                   })
            .When("the recorded glyphs are read back", [] {})
            .Then("they are '07.4 mmHg', one pen step apart, at the node's corner",
                  [state] {
                      // Zero-padded rather than right-shifted: a slot that showed nothing would make
                      // `7.4` and `74.0` the same picture at a glance, and the leading zero is what a
                      // fixed-width readout uses to keep the decimal point in one place.
                      //
                      // The space in `mmHg` is blank, so it records no rectangle - `addGlyphRect()`
                      // skips a degenerate extent - which is why eight glyphs are expected from nine
                      // characters.
                      mdux::spec::Checks checks;
                      checks.expect(state->lefts.size() == 8, std::format("nine characters less one blank space is eight glyphs, got {}", state->lefts.size()));
                      if (state->lefts.size() != 8) {
                          checks.raise();
                          return;
                      }
                      // 10px per pen step, ink starting at the node's x, and the space at index 4
                      // skipped - so the glyph after it starts at 50 rather than at 40.
                      const std::array<float, 8> expected{20.0F, 30.0F, 40.0F, 50.0F, 70.0F, 80.0F, 90.0F, 100.0F};
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(state->lefts[index] == expected[index],
                                        std::format("glyph {} starts at {}, expected {}", index, state->lefts[index], expected[index]));
                      }
                      checks.expect(state->box.has_value() && state->box->left == static_cast<float>(node.x), "the ink starts at the node's left edge");
                      checks.expect(state->box.has_value() && state->box->top == static_cast<float>(node.y), "and at its top edge");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theEnvelopeBoundsEveryValue{
    "measurePattern() bounds every reading the pattern can produce",
    "evidence-unit",
    [] {
        struct State {
            Scratch                          scratch;
            std::optional<ms::PatternExtent> envelope;
            std::vector<VertexBox>           drawn;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-envelope-bounds-every-value")
            .Given("the envelope of '##.# mmHg', and every extreme reading it can show",
                   [state] {
                       const auto measured = ms::measurePattern(theFont(), "##.# mmHg", ms::PatternKind::Numeric);
                       if (!measured.has_value()) {
                           throw speclab::core::AssertionFailure(std::format("the pattern was not measurable: {}", ms::describe(measured.error())),
                                                                 std::source_location::current());
                       }
                       state->envelope = *measured;

                       for (const std::int64_t value : {std::int64_t{0}, std::int64_t{1}, std::int64_t{74}, std::int64_t{505}, std::int64_t{999}}) {
                           draw::DrawList list = state->scratch.list();
                           requireRecorded(ms::recordNumeric(list, theFont(), node, "##.# mmHg", value, digits), std::format("the reading {}", value));
                           if (const auto box = boxOf(list.vertices()); box.has_value()) {
                               state->drawn.push_back(*box);
                           }
                       }
                   })
            .When("each drawn reading is compared against the envelope", [] {})
            .Then("none of them exceeds it",
                  [state] {
                      // The implication ADR-010 decision 4's amendment rests on: a node that holds
                      // the envelope holds any reading the pattern will ever draw. Checked by drawing
                      // the extremes rather than by arguing that it follows.
                      mdux::spec::Checks checks;
                      checks.expect(state->drawn.size() == 5, "every reading recorded something");
                      for (const VertexBox& box : state->drawn) {
                          const auto width  = static_cast<std::int64_t>(box.right - box.left);
                          const auto height = static_cast<std::int64_t>(box.bottom - box.top);
                          checks.expect(width <= state->envelope->width,
                                        std::format("a reading is {}px wide against an envelope of {}", width, state->envelope->width));
                          checks.expect(height <= state->envelope->height,
                                        std::format("a reading is {}px tall against an envelope of {}", height, state->envelope->height));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aClockRendersItsFields{
    "A clock draws the fields its format fixes, in the order the contract fixes them",
    "evidence-unit",
    [] {
        struct State {
            Scratch            scratch;
            std::vector<float> timeLefts;
            std::size_t        dateGlyphs{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-clock-renders-its-fields")
            .Given("07:04:09 under both closed formats",
                   [state] {
                       constexpr ms::CivilTime now{.year = 2026, .month = 9, .day = 4, .hour = 7, .minute = 4, .second = 9};

                       draw::DrawList timeList = state->scratch.list();
                       requireRecorded(ms::recordClock(timeList, theFont(), node, ms::ClockFormat::TimeSeconds, now, digits), "the time");
                       state->timeLefts = glyphLefts(timeList.vertices());

                       Scratch        dateScratch;
                       draw::DrawList dateList = dateScratch.list();
                       requireRecorded(ms::recordClock(dateList, theFont(), node, ms::ClockFormat::DateTimeSeconds, now, digits), "the date and time");
                       state->dateGlyphs = dateList.vertices().size() / 4;
                   })
            .When("the recorded glyph counts are compared against the renderings", [] {})
            .Then("each format draws exactly its own shape",
                  [state] {
                      // `HH:MM:SS` is eight characters with no blanks, so eight glyphs.
                      // `YYYY-MM-DD HH:MM:SS` is nineteen with one blank space, so eighteen - and
                      // that second case is the one that would catch a field mapping which read the
                      // slot letters instead of the format, since `M` there is both a month and a
                      // minute.
                      mdux::spec::Checks checks;
                      checks.expect(state->timeLefts.size() == 8, std::format("HH:MM:SS is eight glyphs, got {}", state->timeLefts.size()));
                      checks.expect(state->dateGlyphs == 18, std::format("YYYY-MM-DD HH:MM:SS less its space is eighteen glyphs, got {}", state->dateGlyphs));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aClockFieldPastItsSlotsIsRefused{
    "A clock field with more digits than its slots is refused, not truncated",
    "evidence-unit",
    [] {
        struct State {
            Scratch                                      scratch;
            std::vector<std::optional<ms::ReadingError>> errors;
            std::size_t                                  verticesAfter{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-clock-field-overflow")
            .Given("times whose month, day, hour, minute or second needs three digits",
                   [state] {
                       // The field types do not bound these: month, day, hour, minute and second are
                       // `std::uint8_t`, so 100-255 is representable and `push()` took the low two
                       // digits of it. An hour of 123 drew `23` - a plausible different time, which
                       // is the single output this module is written to refuse.
                       constexpr std::array<ms::CivilTime, 5> overflowing{
                           ms::CivilTime{.year = 2026,   .month = 1,   .day = 1, .hour = 123,   .minute = 0,   .second = 0},
                           ms::CivilTime{.year = 2026,   .month = 1,   .day = 1,   .hour = 0, .minute = 199,   .second = 0},
                           ms::CivilTime{.year = 2026,   .month = 1,   .day = 1,   .hour = 0,   .minute = 0, .second = 100},
                           ms::CivilTime{.year = 2026, .month = 200,   .day = 1,   .hour = 0,   .minute = 0,   .second = 0},
                           ms::CivilTime{.year = 2026,   .month = 1, .day = 255,   .hour = 0,   .minute = 0,   .second = 0}
                       };
                       // The first three are drawn by both formats; the last two only by the date
                       // one, so each is offered to a format that actually renders it.
                       constexpr std::array<ms::ClockFormat, 5> formats{ms::ClockFormat::TimeSeconds,
                                                                        ms::ClockFormat::TimeSeconds,
                                                                        ms::ClockFormat::TimeSeconds,
                                                                        ms::ClockFormat::DateTimeSeconds,
                                                                        ms::ClockFormat::DateTimeSeconds};
                       draw::DrawList                           list = state->scratch.list();
                       for (std::size_t index = 0; index < overflowing.size(); ++index) {
                           const auto result = ms::recordClock(list, theFont(), node, formats[index], overflowing[index], digits);
                           state->errors.push_back(result.has_value() ? std::nullopt : std::optional{result.error()});
                       }
                       state->verticesAfter = list.vertices().size();
                   })
            .When("each is offered to a format that draws that field", [] {})
            .Then("every one is ValueTooLarge and nothing was recorded",
                  [state] {
                      mdux::spec::Checks checks;
                      for (std::size_t index = 0; index < state->errors.size(); ++index) {
                          checks.expect(state->errors[index] == ms::ReadingError::ValueTooLarge,
                                        std::format("overflowing field {} is refused as ValueTooLarge", index));
                      }
                      checks.expect(state->verticesAfter == 0, std::format("a refused clock draws nothing, got {} vertices", state->verticesAfter));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aTimeOnlyClockIgnoresTheYear{
    "A time-only clock is drawn whatever the year field holds",
    "evidence-unit",
    [] {
        struct State {
            Scratch                         scratch;
            std::optional<ms::ReadingError> error;
            std::size_t                     vertices{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-clock-time-only-ignores-year")
            .Given("a TimeSeconds clock whose year is outside four digits",
                   [state] {
                       // `HH:MM:SS` renders no year, so a host that never fills one in - or fills it
                       // with a sentinel - must still get its clock. The year bound used to be
                       // checked for both formats, which refused a perfectly drawable time.
                       constexpr ms::CivilTime now{.year = 70000, .month = 1, .day = 1, .hour = 12, .minute = 34, .second = 56};
                       draw::DrawList          list   = state->scratch.list();
                       const auto              result = ms::recordClock(list, theFont(), node, ms::ClockFormat::TimeSeconds, now, digits);
                       state->error                   = result.has_value() ? std::nullopt : std::optional{result.error()};
                       state->vertices                = list.vertices().size();
                   })
            .When("the frame is inspected", [] {})
            .Then("it is drawn, because the year is not one of its fields",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(!state->error.has_value(), "a time-only clock is not refused for its year");
                      checks.expect(state->vertices > 0, "the time was drawn");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aClockDistinguishesMonthFromMinute{
    "A DateTimeSeconds clock puts the month in the date and the minute in the time",
    "evidence-unit",
    [] {
        struct State {
            Scratch                    scratch;
            std::vector<std::uint32_t> septemberSlots;
            std::vector<std::uint32_t> decemberSlots;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-clock-month-is-not-minute")
            .Given("two times differing only in their month",
                   [state] {
                       // Both fields render through the same `M` in `YYYY-MM-DD HH:MM:SS`, so a rule
                       // that keyed on the letter alone would draw the same glyphs for both of these
                       // - or would swap them. The atlas slot each glyph samples is what tells them
                       // apart, since every digit has its own slot in the fixture font.
                       constexpr ms::CivilTime september{.year = 2026, .month = 9, .day = 4, .hour = 12, .minute = 3, .second = 0};
                       constexpr ms::CivilTime december{.year = 2026, .month = 12, .day = 4, .hour = 12, .minute = 3, .second = 0};

                       const auto slotsOf = [](const ms::CivilTime& now, Scratch& scratch) {
                           draw::DrawList list = scratch.list();
                           requireRecorded(ms::recordClock(list, theFont(), node, ms::ClockFormat::DateTimeSeconds, now, digits), "the clock");
                           std::vector<std::uint32_t>            slots;
                           const std::span<const draw::UiVertex> vertices = list.vertices();
                           for (std::size_t index = 0; index + 3 < vertices.size(); index += 4) {
                               slots.push_back(static_cast<std::uint32_t>(vertices[index].u * 1000.0F));
                           }
                           return slots;
                       };

                       Scratch second;
                       state->septemberSlots = slotsOf(september, state->scratch);
                       state->decemberSlots  = slotsOf(december, second);
                   })
            .When("the two renderings are compared glyph by glyph", [] {})
            .Then("they differ in the month's two positions and nowhere else",
                  [state] {
                      // `YYYY-MM-DD HH:MM:SS` draws Y Y Y Y - M M - D D H H : M M : S S, the blank
                      // space between the date and the time recording nothing. So the date's month is
                      // at drawn positions 5 and 6 - after the four year digits and the first hyphen -
                      // while the minute is at 13 and 14. A rule that keyed on the `M` alone would
                      // move both, or the wrong one, and either shows up here as a different set.
                      mdux::spec::Checks checks;
                      checks.expect(state->septemberSlots.size() == 18 && state->decemberSlots.size() == 18, "both renderings drew eighteen glyphs");
                      if (state->septemberSlots.size() != 18 || state->decemberSlots.size() != 18) {
                          checks.raise();
                          return;
                      }
                      std::vector<std::size_t> differing;
                      for (std::size_t index = 0; index < 18; ++index) {
                          if (state->septemberSlots[index] != state->decemberSlots[index]) {
                              differing.push_back(index);
                          }
                      }
                      const std::vector<std::size_t> expected{5, 6};
                      checks.expect(differing == expected,
                                    std::format("only the date's month differs; differing drawn positions were [{}]",
                                                differing | std::views::transform([](std::size_t at) {
                                                    return std::format("{}", at);
                                                }) | std::views::join_with(std::string{", "})
                                                    | std::ranges::to<std::string>()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register expansionIsDeterministic{
    "The same reading expands to byte-identical buffers",
    "determinism",
    [] {
        struct State {
            Scratch                       first;
            Scratch                       second;
            std::optional<draw::DrawList> a;
            std::optional<draw::DrawList> b;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-expansion-is-deterministic")
            .Given("one reading expanded twice into separate storage",
                   [state] {
                       state->a = state->first.list();
                       state->b = state->second.list();
                       requireRecorded(ms::recordNumeric(*state->a, theFont(), node, "##.# mmHg", 386, digits), "the first expansion");
                       requireRecorded(ms::recordNumeric(*state->b, theFont(), node, "##.# mmHg", 386, digits), "the second expansion");
                   })
            .When("the two buffers are compared", [] {})
            .Then("they are identical",
                  [state] {
                      // The pen is integral throughout, so this is a statement about the integer
                      // arithmetic rather than about a floating-point mode - and it is the property
                      // that lets a device and a baker agree about which column a glyph lands in.
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::equal(state->a->vertices(), state->b->vertices()), "the vertex buffers match");
                      checks.expect(std::ranges::equal(state->a->indices(), state->b->indices()), "the index buffers match");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register pixelConversionIsTheBakersRule{"toPixels() is the baker's half-up rule", "evidence-unit", [] {
                                                              return speclab::Test("reading-to-pixels-half-up")
                                                                  .Given("a font of 1000 units per em at 10 pixels", [] {})
                                                                  .When("pen positions are converted", [] {})
                                                                  .Then("each rounds half up, as the text baker rounds",
                                                                        [] {
                                                                            // Shared with the baker rather than reimplemented, which is what keeps static
                                                                            // and dynamic text in one node on one grid. Checked directly because a
                                                                            // rounding difference is invisible in a rendered frame until two kinds of text
                                                                            // sit beside each other.
                                                                            mdux::spec::Checks checks;
                                                                            checks.expect(ms::toPixels(0, theFont()) == 0, "zero units is zero pixels");
                                                                            checks.expect(ms::toPixels(1000, theFont()) == 10, "one em is ten pixels");
                                                                            checks.expect(ms::toPixels(50, theFont()) == 1, "half a pixel rounds up");
                                                                            checks.expect(ms::toPixels(49, theFont()) == 0, "just under half rounds down");
                                                                            checks.raise();
                                                                        })
                                                                  .Execute();
                                                          }};

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

const mdux::spec::Register refusesAnOversizedValue{
    "A value with more digits than its slots is refused, not truncated",
    "evidence-unit",
    [] {
        struct State {
            Scratch                         scratch;
            std::optional<draw::DrawList>   list;
            std::optional<ms::ReadingError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-refuses-an-oversized-value")
            .Given("a three-slot pattern and a four-digit reading",
                   [state] {
                       state->list  = state->scratch.list();
                       state->error = requireRefused(ms::recordNumeric(*state->list, theFont(), node, "##.#", 1099, digits), "the oversized reading");
                   })
            .When("the list is inspected", [] {})
            .Then("the reading is ValueTooLarge and nothing was drawn",
                  [state] {
                      // This issue's sharpest acceptance. A truncating implementation draws `09.9`
                      // for a reading of 109.9 - a smaller, entirely plausible number, in a box whose
                      // unit says what it means, with nothing on screen to say it was cut. Dropping
                      // the most significant digit is the one failure mode a pressure readout must
                      // not have.
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::ReadingError::ValueTooLarge, "the refusal names the value");
                      checks.expect(state->list->vertices().empty(), "no glyph was recorded");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register refusesMalformedInputs{
    "An empty, over-long, unbakeable or negative reading is refused",
    "evidence-unit",
    [] {
        struct State {
            Scratch                                        scratch;
            std::optional<draw::DrawList>                  list;
            std::array<std::optional<ms::ReadingError>, 5> errors{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("reading-refuses-malformed-inputs")
            .Given("a list",
                   [state] {
                       state->list = state->scratch.list();
                   })
            .When("each malformed input is offered in turn",
                  [state] {
                      state->errors[0] = requireRefused(ms::recordNumeric(*state->list, theFont(), node, "", 1, digits), "an empty pattern");

                      const std::string tooLong(ms::maxPatternLength + 1, '#');
                      state->errors[1] = requireRefused(ms::recordNumeric(*state->list, theFont(), node, tooLong, 1, digits), "an over-long pattern");

                      // 'Z' is outside the fixture font's charset, and ADR-010 leaves the runtime no
                      // fallback: a substitute glyph would be a reading nobody wrote.
                      state->errors[2] = requireRefused(ms::recordNumeric(*state->list, theFont(), node, "##Z", 1, digits), "an unbakeable literal");

                      state->errors[3] = requireRefused(ms::recordNumeric(*state->list, theFont(), node, "##.#", -1, digits), "a negative reading");

                      state->errors[4] = requireRefused(ms::recordNumeric(*state->list, theFont(), node, "mmHg", 1, digits), "a pattern with no digit slot");
                  })
            .Then("each names its own cause and the list is untouched",
                  [state] {
                      mdux::spec::Checks                        checks;
                      constexpr std::array<ms::ReadingError, 5> expected{ms::ReadingError::PatternEmpty,
                                                                         ms::ReadingError::PatternTooLong,
                                                                         ms::ReadingError::GlyphNotInPackage,
                                                                         ms::ReadingError::ValueNegative,
                                                                         ms::ReadingError::NoDigitSlots};
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(state->errors[index] == expected[index], std::format("refusal {} is {}", index, ms::describe(expected[index])));
                      }
                      checks.expect(state->list->vertices().empty(), "no refusal left a glyph behind");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyReadingErrorDescribesItself{"Every ReadingError has its own description", "evidence-unit", [] {
                                                                return speclab::Test("reading-error-descriptions")
                                                                    .Given("every ReadingError enumerator", [] {})
                                                                    .When("each is described", [] {})
                                                                    .Then("each has a unique, non-empty description",
                                                                          [] {
                                                                              constexpr std::array<ms::ReadingError, 8> all{ms::ReadingError::PatternEmpty,
                                                                                                                            ms::ReadingError::PatternTooLong,
                                                                                                                            ms::ReadingError::GlyphNotInPackage,
                                                                                                                            ms::ReadingError::PenMovedBackwards,
                                                                                                                            ms::ReadingError::ValueTooLarge,
                                                                                                                            ms::ReadingError::ValueNegative,
                                                                                                                            ms::ReadingError::NoDigitSlots,
                                                                                                                            ms::ReadingError::ListRejected};
                                                                              std::vector<std::string_view>             seen;
                                                                              mdux::spec::Checks                        checks;
                                                                              for (const ms::ReadingError error : all) {
                                                                                  const std::string_view text = ms::describe(error);
                                                                                  checks.expect(!text.empty(), "a description exists");
                                                                                  checks.expect(std::ranges::find(seen, text) == seen.end(),
                                                                                                "the description is unique");
                                                                                  seen.push_back(text);
                                                                              }
                                                                              checks.raise();
                                                                          })
                                                                    .Execute();
                                                            }};

// ---------------------------------------------------------------------------
// The binding, and what render() composes with it
// ---------------------------------------------------------------------------

constexpr ms::NumericDisplaySpec pressure{.requirement = "REQ-1",
                                          .templateId  = "TPL-PRESSURE-MMHG",
                                          .source      = "PRESSURE",
                                          .colorToken  = "Theme.Colors.ScoreDigits"};
constexpr ms::ClockSpec          wallClock{.format = ms::ClockFormat::TimeSeconds};
constexpr ms::PanelSpec          backdrop{.colorToken = "Theme.Colors.TopbarBackground"};

constexpr std::array<ms::CompiledNode, 3> readingNodes{
    ms::CompiledNode{.id = "backdrop",  .bounds = {0, 0, 300, 20},  .payload = backdrop},
    ms::CompiledNode{.id = "pressure", .bounds = {0, 20, 300, 40},  .payload = pressure},
    ms::CompiledNode{   .id = "clock", .bounds = {0, 60, 300, 40}, .payload = wallClock}
};

constexpr draw::DrawBudget readingBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

/// A placeholder approval so the screen below is a constant expression. `approve()` replaces it with
/// the carrier's real one before any binding is made against it.
constexpr std::array placeholderApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "reading-text", .packageSha256 = {1}}
};

constexpr ms::ScreenPackage readingScreen{.id                   = "readings",
                                          .schemaVersion        = mdux::evidence::kSchemaVersion,
                                          .surfaceWidth         = 300,
                                          .surfaceHeight        = 100,
                                          .approvedTextPackages = placeholderApprovals,
                                          .nodes                = readingNodes,
                                          .budget               = readingBudget};

static_assert(readingScreen.validate().has_value(), "the screen under test must be one a device could hold");

constexpr ms::CivilTime    noon{.year = 2026, .month = 9, .day = 5, .hour = 12, .minute = 0, .second = 0};
constexpr std::string_view clockTint = "Theme.Colors.Neutral";

[[nodiscard]] ms::ReadingBinding
requireBound(core::Result<ms::ReadingBinding, ms::ScreenError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
    return *result;
}

[[nodiscard]] ms::ScreenError
requireUnbound(core::Result<ms::ReadingBinding, ms::ScreenError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: expected a refusal but a binding was made", what), where);
    }
    return result.error();
}

[[nodiscard]] ms::FrameStats
requireFrame(core::Result<ms::FrameStats, ms::ScreenError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
    return *result;
}

const mdux::spec::Register unboundNodesAreUnchanged{
    "Without a binding a NumericDisplay reserves its field and a Clock is deferred",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<ms::FrameStats> stats;
            std::vector<draw::UiVertex>   vertices;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-unbound-readings-unchanged")
            .Given("the screen rendered with no reading binding",
                   [state] {
                       const ms::ScreenPackage screen = approve(readingScreen);
                       draw::DrawList          list   = state->scratch.list();
                       state->stats                   = requireFrame(ms::render(screen, list, bindText(screen)), "the frame");
                       state->vertices.assign(list.vertices().begin(), list.vertices().end());
                   })
            .When("the frame is inspected", [] {})
            .Then("two opaque rectangles are drawn and only the clock is deferred",
                  [state] {
                      // The path every existing caller takes - the committed screen's pixel test and
                      // its `verify` leg among them - so it stays a tested contract rather than a
                      // code path nobody exercises now that a binding exists. The clock is the one
                      // deferral, because it has no token and therefore no field to reserve.
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->rects == 2, std::format("the panel and the pressure field, got {}", state->stats->rects));
                      checks.expect(state->stats->readings == 0, "no reading was drawn");
                      checks.expect(state->stats->deferred == 1, std::format("only the clock is deferred, got {}", state->stats->deferred));
                      const bool opaque = std::ranges::all_of(state->vertices, [](const draw::UiVertex& vertex) {
                          return std::bit_cast<std::array<std::uint8_t, 4>>(vertex.color)[3] == 255;
                      });
                      checks.expect(opaque, "an unbound field is opaque");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBoundReadingDimsItsField{
    "A bound NumericDisplay dims its field and draws its digits at full tint",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<ms::FrameStats> stats;
            std::vector<draw::UiVertex>   vertices;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-bound-reading-dims-its-field")
            .Given("a binding carrying the pressure reading and the time",
                   [state] {
                       static const std::array<ms::ReadingSlot, 1> slots{
                           ms::ReadingSlot{.nodeId = "pressure", .rendering = "##.# mmHg", .value = 137}
                       };
                       const ms::ScreenPackage  screen  = approve(readingScreen);
                       const ms::ReadingBinding binding = requireBound(ms::ReadingBinding::create(screen, slots, &noon, clockTint), "the binding");

                       draw::DrawList list = state->scratch.list();
                       state->stats        = requireFrame(ms::render(screen, list, bindText(screen), {}, {}, binding), "the frame");
                       state->vertices.assign(list.vertices().begin(), list.vertices().end());
                   })
            .When("the alpha of every recorded vertex is read", [] {})
            .Then("exactly one primitive is dimmed and both readings carry the full tint",
                  [state] {
                      // One tint at two coverages, which #257 introduced for a waveform and which
                      // `verify-golden-two-coverage-composition` proves both golden checks admit.
                      // This node is the one that scenario's fixture actually models, since
                      // `insufflation-pressure` is what carries `ColorHash` on the committed screen.
                      const auto  expectedDim = static_cast<std::uint8_t>((255.0F * ms::boundFieldCoverage) + 0.5F);
                      std::size_t dimmed      = 0;
                      std::size_t solid       = 0;
                      for (const draw::UiVertex& vertex : state->vertices) {
                          const auto alpha = std::bit_cast<std::array<std::uint8_t, 4>>(vertex.color)[3];
                          if (alpha == 255) {
                              ++solid;
                          } else if (alpha == expectedDim) {
                              ++dimmed;
                          }
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->readings == 2, std::format("the pressure and the clock were drawn, got {}", state->stats->readings));
                      checks.expect(state->stats->deferred == 0, "nothing is deferred once both are bound");
                      checks.expect(dimmed == 4, std::format("exactly one dimmed rectangle, got {} vertices at alpha {}", dimmed, expectedDim));
                      checks.expect(solid + dimmed == state->vertices.size(), "every vertex is the dimmed field or a full tint");
                      // The panel, the dimmed field, eight glyphs of `##.# mmHg` and eight of
                      // `HH:MM:SS` - the space in the template being blank and recording nothing.
                      checks.expect(state->vertices.size() == 4 * (1 + 1 + 8 + 8),
                                    std::format("the panel, the field and sixteen glyphs, got {} vertices", state->vertices.size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnboundNodeStillReservesItsField{
    "A NumericDisplay the caller has no reading for still reserves its field",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<ms::FrameStats> stats;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-partially-bound-readings")
            .Given("a binding carrying a time but no numeric reading",
                   [state] {
                       const ms::ScreenPackage  screen  = approve(readingScreen);
                       const ms::ReadingBinding binding = requireBound(ms::ReadingBinding::create(screen, {}, &noon, clockTint), "the binding");

                       draw::DrawList list = state->scratch.list();
                       state->stats        = requireFrame(ms::render(screen, list, bindText(screen), {}, {}, binding), "the frame");
                   })
            .When("the frame's statistics are read", [] {})
            .Then("the clock is drawn and the pressure is a reserved field, not a deferral",
                  [state] {
                      // A reading that has not arrived is a normal state. Binding every live node a
                      // screen carries is not a precondition of rendering it.
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->readings == 1, "only the clock was drawn");
                      checks.expect(state->stats->deferred == 0, "the unbound NumericDisplay is a field, not a deferral");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bindingRefusesWhatItCanCheck{
    "A binding refuses an unknown node, a duplicate, a bad pattern and a bad clock tint",
    "evidence-unit",
    [] {
        struct State {
            std::array<std::optional<ms::ScreenError>, 5> errors{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-reading-binding-refusals")
            .Given("a screen carrying one NumericDisplay and one Clock", [] {})
            .When("each malformed slot set is offered",
                  [state] {
                      const std::array<ms::ReadingSlot, 1> unknown{
                          ms::ReadingSlot{.nodeId = "presure", .rendering = "##.#", .value = 1}
                      };
                      state->errors[0] = requireUnbound(ms::ReadingBinding::create(readingScreen, unknown), "a mistyped node id");

                      // A node that exists and is not a NumericDisplay. The same refusal as one that
                      // does not exist at all, because from the caller's side both mean "this slot
                      // will never be drawn" and the fix in both cases is to correct the id.
                      const std::array<ms::ReadingSlot, 1> wrongKind{
                          ms::ReadingSlot{.nodeId = "backdrop", .rendering = "##.#", .value = 1}
                      };
                      state->errors[1] = requireUnbound(ms::ReadingBinding::create(readingScreen, wrongKind), "a node of the wrong kind");

                      const std::array<ms::ReadingSlot, 2> duplicated{
                          ms::ReadingSlot{.nodeId = "pressure", .rendering = "##.#", .value = 1},
                          ms::ReadingSlot{.nodeId = "pressure", .rendering = "###.", .value = 2}
                      };
                      state->errors[2] = requireUnbound(ms::ReadingBinding::create(readingScreen, duplicated), "a duplicated node");

                      const std::array<ms::ReadingSlot, 1> emptyPattern{
                          ms::ReadingSlot{.nodeId = "pressure", .rendering = "", .value = 1}
                      };
                      state->errors[3] = requireUnbound(ms::ReadingBinding::create(readingScreen, emptyPattern), "an empty pattern");

                      // A clock whose tint the governed table does not define. Checked at create()
                      // rather than per frame: it is a property of what the caller assembled.
                      state->errors[4] = requireUnbound(ms::ReadingBinding::create(readingScreen, {}, &noon, "Theme.Colors.NotInTheTable"),
                                                        "an unknown clock tint");
                  })
            .Then("each refusal names its own cause",
                  [state] {
                      constexpr std::array<ms::ScreenError, 5> expected{ms::ScreenError::UnknownReadingNode,
                                                                        ms::ScreenError::UnknownReadingNode,
                                                                        ms::ScreenError::DuplicateReading,
                                                                        ms::ScreenError::MalformedPattern,
                                                                        ms::ScreenError::UnknownColorToken};
                      mdux::spec::Checks                       checks;
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(state->errors[index] == expected[index], std::format("refusal {} is {}", index, ms::describe(expected[index])));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBindingIsNotPortableBetweenScreens{
    "A reading binding built for one screen is refused by another",
    "evidence-unit",
    [] {
        struct State {
            Scratch                        scratch;
            std::optional<ms::ScreenError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-reading-binding-is-not-portable")
            .Given("a binding validated against one screen",
                   [state] {
                       static const std::array<ms::ReadingSlot, 1> slots{
                           ms::ReadingSlot{.nodeId = "pressure", .rendering = "##.# mmHg", .value = 137}
                       };
                       const ms::ScreenPackage  screen  = approve(readingScreen);
                       const ms::ReadingBinding binding = requireBound(ms::ReadingBinding::create(screen, slots), "the binding");

                       // The same nodes under a different id: everything about this screen would let
                       // the binding work, and it is refused anyway, because what was validated was
                       // the pairing rather than the shape.
                       ms::ScreenPackage other = screen;
                       other.id                = "other-readings";

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(other, list, bindText(screen), {}, {}, binding);
                       if (frame.has_value()) {
                           throw speclab::core::AssertionFailure("the foreign screen accepted the binding", std::source_location::current());
                       }
                       state->error = frame.error();
                   })
            .When("the refusal is read", [] {})
            .Then("it names the screen rather than the reading",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::ScreenError::ScreenNotApproved, "the frame is refused as ScreenNotApproved");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aReadingOverflowingItsNodeIsRefused{
    "A reading wider than its node refuses the frame, whatever the compiler measured",
    "evidence-unit",
    [] {
        struct State {
            Scratch                        scratch;
            std::optional<ms::ScreenError> error;
            std::size_t                    kept{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-reading-overflowing-its-node-is-refused")
            .Given("a binding whose pattern is far wider than the node the compiler certified",
                   [state] {
                       // The drift this check exists for. The screen was compiled against a table
                       // saying `TPL-PRESSURE-MMHG` renders as something that fits; this device holds
                       // a table that says otherwise, and nothing in the artifacts can tell them
                       // apart - the compiled node carries the template's *name*. So the frame is
                       // refused on what was actually drawn.
                       static const std::array<ms::ReadingSlot, 1> slots{
                           ms::ReadingSlot{.nodeId = "narrow", .rendering = "##########", .value = 1234567890}
                       };
                       constexpr ms::NumericDisplaySpec                 narrowSpec{.requirement = "REQ-1",
                                                                                   .templateId  = "TPL-PRESSURE-MMHG",
                                                                                   .source      = "PRESSURE",
                                                                                   .colorToken  = "Theme.Colors.ScoreDigits"};
                       static constexpr std::array<ms::CompiledNode, 1> narrowNodes{
                           ms::CompiledNode{.id = "narrow", .bounds = {0, 0, 20, 40}, .payload = narrowSpec}
                       };
                       static constexpr ms::ScreenPackage narrowScreen{.id                   = "narrow",
                                                                       .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                                       .surfaceWidth         = 300,
                                                                       .surfaceHeight        = 100,
                                                                       .approvedTextPackages = placeholderApprovals,
                                                                       .nodes                = narrowNodes,
                                                                       .budget               = readingBudget};
                       static_assert(narrowScreen.validate().has_value(), "the narrow fixture is a screen a device could hold");

                       const ms::ScreenPackage  screen  = approve(narrowScreen);
                       const ms::ReadingBinding binding = requireBound(ms::ReadingBinding::create(screen, slots), "the binding");

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(screen, list, bindText(screen), {}, {}, binding);
                       if (frame.has_value()) {
                           throw speclab::core::AssertionFailure("the overflowing reading was accepted", std::source_location::current());
                       }
                       state->error = frame.error();
                       state->kept  = list.vertices().size();
                   })
            .When("the list is inspected", [] {})
            .Then("the frame is ReadingOverflowsNode and whole rather than partial",
                  [state] {
                      // Whole or absent, including the dimmed field this node had already recorded
                      // before its digits overflowed. A partial frame on a medical display is the
                      // worst outcome available, because it looks like a reading.
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::ScreenError::ReadingOverflowsNode, "the refusal names the node");
                      checks.expect(state->kept == 0, std::format("the frame was rolled back whole, got {} vertices", state->kept));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register whatAGoldenPinsDoesNotVaryWithTheValue{
    "The rectangle and tint a golden pins are the same whatever the reading says",
    "evidence-unit",
    [] {
        struct State {
            Scratch                     first;
            Scratch                     second;
            std::vector<draw::UiVertex> low;
            std::vector<draw::UiVertex> high;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-golden-facts-are-value-invariant")
            .Given("the same screen rendered with two different readings",
                   [state] {
                       const ms::ScreenPackage screen = approve(readingScreen);
                       const ms::TextBinding   text   = bindText(screen);

                       const auto renderWith = [&](std::int64_t value, Scratch& scratch, std::vector<draw::UiVertex>& into) {
                           const std::array<ms::ReadingSlot, 1> slots{
                               ms::ReadingSlot{.nodeId = "pressure", .rendering = "##.# mmHg", .value = value}
                           };
                           const ms::ReadingBinding binding = requireBound(ms::ReadingBinding::create(screen, slots), "the binding");
                           draw::DrawList           list    = scratch.list();
                           static_cast<void>(requireFrame(ms::render(screen, list, text, {}, {}, binding), "the frame"));
                           into.assign(list.vertices().begin(), list.vertices().end());
                       };

                       renderWith(3, state->first, state->low);
                       renderWith(986, state->second, state->high);
                   })
            .When("the two frames are compared", [] {})
            .Then("the field rectangle and its tint are identical, and only the digits differ",
                  [state] {
                      // #16's rule for dynamic kinds, checked against the frame rather than against
                      // the sidecar. `goldens.json` pins this node's bounds and colour and says
                      // nothing about its value - which is only a true description of the screen if
                      // what the runtime draws keeps those two facts constant as the reading moves.
                      // A field that resized itself around its digits, or tinted itself by
                      // magnitude, would make a correct golden describe a screen that no longer
                      // exists.
                      mdux::spec::Checks checks;
                      checks.expect(state->low.size() >= 8 && state->high.size() >= 8, "both frames drew a panel, a field and digits");
                      if (state->low.size() < 8 || state->high.size() < 8) {
                          checks.raise();
                          return;
                      }

                      // Vertices 0-3 are the Row's panel and 4-7 the NumericDisplay's dimmed field:
                      // both recorded before any glyph, in node order.
                      const std::span<const draw::UiVertex> lowField{state->low.data(), 8};
                      const std::span<const draw::UiVertex> highField{state->high.data(), 8};
                      checks.expect(std::ranges::equal(lowField, highField), "the panel and the field are byte-identical across the two readings");

                      // ...and the readings really were different, so the scenario is not passing
                      // because nothing changed.
                      checks.expect(!std::ranges::equal(state->low, state->high), "the two frames differ somewhere, namely in their digits");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
