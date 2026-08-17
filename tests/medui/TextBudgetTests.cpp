/**
 * @brief BDD scenarios for text-budget and dynamic-charset validation (issue #195).
 * @file TextBudgetTests.cpp
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * The fixture font and the per-locale text packages are built here rather than read from
 * `generated/`, and that is a deliberate difference from the evidence suites. A committed package
 * pins one set of real translations; these scenarios need a *pair* of locales whose widths differ
 * by a known number of pixels, so that "the widest approved translation is what the budget is
 * measured against" is checkable by arithmetic rather than by whichever German string happens to be
 * in the corpus today.
 *
 * The font is still a real `mdux::font::FontPackage` and passes `validate()`; the runs are still
 * real v1 records decoded by the same `mdux::text::draw::decodeRecord()` the device draws with.
 * What is synthetic is the vocabulary, not the format.
 *
 * Every call supplies both locales the fixture font approves, because the stage refuses a subset -
 * a budget measured over some of the approved locales is not a budget. The scenarios that leave one
 * out are the ones asserting that refusal.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.font.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.layout;
import mdux.tools.medui.parser;
import mdux.tools.medui.textbudget;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md   = mdux::tools::medui;
namespace cli  = mdux::tools::cli;
namespace font = mdux::font;
namespace text = mdux::text;

/// Every glyph in the fixture font is this size, so an extent is countable by hand.
constexpr std::uint32_t glyphWidth  = 8;
constexpr std::uint32_t glyphHeight = 10;

/// The pen step the fixture runs are baked at. Not the font's advance: a baker chooses positions,
/// and this suite chooses a round number so that a run of n glyphs is `(n - 1) * 10 + 8` wide.
constexpr std::int16_t penStep = 10;

/// The baseline every fixture run sits on. With `bitmapOriginY == glyphHeight`, ink runs from y 0.
constexpr std::int16_t baseline = 10;

/// Package indices into the fixture font's glyph table, which is sorted by code point.
constexpr std::uint16_t spaceIndex  = 0;   ///< U+0020, blank: an advance and no coverage
constexpr std::uint16_t letterIndex = 12;  ///< U+0041 'A'

/// The id the fixture font publishes, and the id every fixture text package references.
constexpr std::string_view fontId = "fixture-ui";

/**
 * @brief The fixture font: a blank space, the ten digits, `:`, and A-C, all 8x10.
 *
 * The digits share one advance width because `validate()` requires it - the tabular-figure rule
 * from #161 - and this font is built to pass that check rather than around it. It approves two
 * locales, which is what makes "the complete approved set" a testable idea below.
 */
[[nodiscard]] font::FontPackage fixtureFont() {
    font::FontPackage package;
    package.id         = std::string{fontId};
    package.unitsPerEm = 1000;
    package.pixelSize  = 10;
    package.locales    = {"en-US", "de-DE"};
    package.atlas =
        font::AtlasMetrics{.path = "atlas.bin", .width = 64, .height = 64, .byteLength = 64 * 64, .sha256 = std::string(64, 'a'), .occupancyPercent = 50};

    const auto append = [&package](char32_t codePoint, bool blank) {
        const auto slot = static_cast<std::uint32_t>(package.glyphs.size());
        package.glyphs.push_back(font::GlyphRecord{.codePoint       = codePoint,
                                                   .glyphIndex      = static_cast<std::uint16_t>(slot + 1),
                                                   .advanceWidth    = 500,
                                                   .leftSideBearing = 0,
                                                   .x               = (slot % 8) * glyphWidth,
                                                   .y               = (slot / 8) * glyphHeight,
                                                   .width           = blank ? 0 : glyphWidth,
                                                   .height          = blank ? 0 : glyphHeight,
                                                   .bitmapOriginX   = 0,
                                                   .bitmapOriginY   = blank ? 0 : static_cast<std::int32_t>(glyphHeight)});
    };

    append(U' ', true);
    for (char32_t digit = U'0'; digit <= U'9'; ++digit) {
        append(digit, false);
    }
    append(U':', false);
    for (char32_t letter = U'A'; letter <= U'C'; ++letter) {
        append(letter, false);
    }

    package.restrictedCharset = {
        font::CharsetRange{.first = U' ', .last = U' '},
        font::CharsetRange{.first = U'0', .last = U':'},
        font::CharsetRange{.first = U'A', .last = U'C'}
    };
    return package;
}

/// Appends one little-endian v1 run record: package glyph index, pen x, baseline y.
void appendRecord(std::vector<std::byte>& bytes, std::uint16_t packageIndex, std::int16_t x, std::int16_t y) {
    const auto emit16 = [&bytes](std::uint16_t value) {
        bytes.push_back(static_cast<std::byte>(value & 0xFFu));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    };
    emit16(packageIndex);
    emit16(static_cast<std::uint16_t>(x));
    emit16(static_cast<std::uint16_t>(y));
}

/// Bakes one run: the given glyphs, one `penStep` apart, on the fixture baseline.
[[nodiscard]] std::vector<std::byte> bakeRun(std::span<const std::uint16_t> glyphs) {
    std::vector<std::byte> bytes;
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        appendRecord(bytes, glyphs[index], static_cast<std::int16_t>(static_cast<std::int64_t>(index) * penStep), baseline);
    }
    return bytes;
}

/// One approved locale's package and the sidecar its runs address, kept together so a test can
/// hand `checkTextBudgets()` a `LocaleText` without managing two lifetimes.
struct Locale {
    text::TextPackage      package;
    std::vector<std::byte> sidecar;

    [[nodiscard]] md::LocaleText view() const noexcept {
        return md::LocaleText{.package = &package, .sidecar = sidecar};
    }
};

/// Builds one locale's text package from `key -> run bytes`, concatenating the sidecar in order.
///
/// Only the header's id is set: `TextPackage` already defaults its `kind` and `schemaVersion`, and
/// restating them here would be a second opinion about a format this suite does not own.
[[nodiscard]] Locale bakeLocale(std::string locale, std::span<const std::pair<std::string, std::vector<std::byte>>> runs) {
    Locale built;
    built.package.header.id   = std::format("fixture-{}", locale);
    built.package.atlasId     = std::string{fontId};
    built.package.locale      = std::move(locale);
    built.package.sidecarPath = "runs.bin";

    for (const auto& [key, bytes] : runs) {
        built.package.runs.push_back(text::TextRun{.id = key, .byteOffset = built.sidecar.size(), .byteLength = bytes.size(), .sha256 = {}});
        built.sidecar.insert(built.sidecar.end(), bytes.begin(), bytes.end());
    }
    built.package.sidecarByteLength = built.sidecar.size();
    return built;
}

/// One locale whose keys are each the given number of copies of 'A'.
[[nodiscard]] Locale lettersLocale(std::string locale, std::span<const std::pair<std::string, std::size_t>> keys) {
    std::vector<std::pair<std::string, std::vector<std::byte>>> runs;
    for (const auto& [key, count] : keys) {
        const std::vector<std::uint16_t> glyphs(count, letterIndex);
        runs.emplace_back(key, bakeRun(glyphs));
    }
    return bakeLocale(std::move(locale), runs);
}

/// One locale whose single key is `count` copies of 'A'.
[[nodiscard]] Locale letterLocale(std::string locale, std::string key, std::size_t count) {
    const std::array<std::pair<std::string, std::size_t>, 1> keys{
        std::pair{std::move(key), count}
    };
    return lettersLocale(std::move(locale), keys);
}

/**
 * @brief The complete approved locale set: one package per tag the fixture font declares.
 *
 * The stage refuses anything else, so a scenario that is not *about* that refusal builds its
 * locales through this type and hands over `views()`.
 */
struct ApprovedText {
    Locale english;
    Locale german;

    [[nodiscard]] std::array<md::LocaleText, 2> views() const noexcept {
        return {english.view(), german.view()};
    }
};

/// One key in both approved locales, at the requested glyph counts.
[[nodiscard]] ApprovedText approvedText(std::string_view key, std::size_t englishGlyphs, std::size_t germanGlyphs) {
    return ApprovedText{.english = letterLocale("en-US", std::string{key}, englishGlyphs), .german = letterLocale("de-DE", std::string{key}, germanGlyphs)};
}

/// Wraps a component body in a screen whose declared surface matches the build surface.
[[nodiscard]] std::string screenWith(std::string_view body) {
    return std::format("Screen Test {{\n"
                       "    layout: Vertical {{ spacing: 0px; padding: 0px; }}\n"
                       "    surface: 200px, 100px;\n"
                       "{}"
                       "}}\n",
                       body);
}

/// One `Label` of the given box, carrying one text key.
[[nodiscard]] std::string labelScreen(std::int64_t width, std::int64_t height, std::string_view key) {
    return screenWith(std::format("    Label {{ id: title; width: {}px; height: {}px; text: t(\"{}\"); "
                                  "color: Theme.Colors.Title; }}\n",
                                  width,
                                  height,
                                  key));
}

/// Parses and resolves a screen against the 200x100 build surface these scenarios declare.
[[nodiscard]] md::LayoutResult layoutOf(std::string_view source) {
    md::ParseResult parsed = md::parse(source, "budget.medui");
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure("text budget test source did not parse", std::source_location::current());
    }
    md::LayoutResult resolved = md::resolveLayout(*parsed.screen, "budget.medui", {.surfaceWidth = 200, .surfaceHeight = 100});
    if (!resolved.ok()) {
        throw speclab::core::AssertionFailure("text budget test source did not resolve", std::source_location::current());
    }
    return resolved;
}

/// Finds one registered diagnostic in a budget result.
[[nodiscard]] const cli::Diagnostic* find(const md::TextBudgetResult& result, md::Code code) {
    const std::string_view wanted = md::id(code);
    const auto             found  = std::ranges::find_if(result.diagnostics, [wanted](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == wanted;
    });
    return found == result.diagnostics.end() ? nullptr : &*found;
}

[[nodiscard]] bool mentions(const cli::Diagnostic* diagnostic, std::string_view fragment) {
    return diagnostic != nullptr && diagnostic->message.find(fragment) != std::string::npos;
}

[[nodiscard]] bool mentions(const cli::Diagnostic& diagnostic, std::string_view fragment) {
    return diagnostic.message.find(fragment) != std::string::npos;
}

/// Runs `call` and answers whether it threw the wiring error every gate below is documented to use.
[[nodiscard]] bool throwsWiringError(const std::function<void()>& call) {
    try {
        call();
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

// The governed dynamic-text table these scenarios resolve `format:` and `charset:` against. The
// ranges are namespace-scope so the spans in a `DynamicTextRule` outlive the call that reads them.
constexpr std::array digitsAndColon{
    font::CharsetRange{.first = U'0', .last = U':'}
};
constexpr std::array digitsAndZone{
    font::CharsetRange{.first = U'0', .last = U':'},
    font::CharsetRange{.first = U'Z', .last = U'Z'}
};

/// U+0030..U+0041: the digits and the colon, then the gap the fixture font does not hold, then 'A'.
constexpr std::array digitsThroughLetter{
    font::CharsetRange{.first = U'0', .last = U'A'}
};

/// A single code point one past the last Unicode scalar value, which is not a character at all.
constexpr std::array beyondUnicode{
    font::CharsetRange{.first = static_cast<char32_t>(0x110000), .last = static_cast<char32_t>(0x110000)}
};

}  // namespace

const mdux::spec::Register widestTranslationDrivesTheBudget{
    "The budget is measured against the widest approved translation, not the authoring one",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-widest-locale")
            .Given("a 40px Label whose en-US run is 28px and whose de-DE run is 58px", [] {})
            .When("the resolved screen is checked against both approved locales", [] {})
            .Then("MEDUI-E050 names de-DE, the key, the required width and the available one",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-TITLE", 3, 6);
                      const auto              locales     = approved.views();

                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(labelScreen(40, 20, "STR-TITLE")),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      const cli::Diagnostic* reported = find(result, md::Code::TextBudgetExceeded);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(result.diagnostics.size() == 1, std::format("only the failing locale is reported, got {}", result.diagnostics.size()));
                      checks.expect(mentions(reported, "de-DE"), "the diagnostic names the locale");
                      checks.expect(mentions(reported, "STR-TITLE"), "the diagnostic names the text key");
                      checks.expect(mentions(reported, "58px"), "the diagnostic names the required width");
                      checks.expect(mentions(reported, "40px"), "the diagnostic names the available width");
                      checks.expect(result.measurements.empty(), "no measurement survives a failed budget");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fittingScreenReportsItsWorstCase{
    "A screen that fits every locale reports the worst case for the emitter to budget",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-worst-case-measurement")
            .Given("a 100px Label whose widest approved translation is 58px", [] {})
            .When("the resolved screen is checked", [] {})
            .Then("it compiles and the measurement names de-DE with the per-axis maximum",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-TITLE", 3, 6);
                      const auto              locales     = approved.views();

                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(labelScreen(100, 20, "STR-TITLE")),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      checks.expect(result.ok(), "the screen compiles");
                      checks.expect(result.measurements.size() == 1, std::format("one text field is measured, got {}", result.measurements.size()));
                      if (result.measurements.size() == 1) {
                          const md::TextMeasurement& measured = result.measurements.front();
                          checks.expect(measured.nodeId == "title", "the measurement names the node");
                          checks.expect(measured.field == "text", "the measurement names the field");
                          checks.expect(measured.textKey == "STR-TITLE", "the measurement names the key");
                          checks.expect(measured.locale == "de-DE", "the widest locale is recorded");
                          checks.expect(measured.extent == md::TextExtent{58, 10},
                                        std::format("the extent is 58x10, got {}x{}", measured.extent.width, measured.extent.height));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register widestLocaleWinsRegardlessOfOrder{
    "The widest translation is recorded whichever position it holds in the input",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-worst-case-not-overwritten")
            .Given("the 58px de-DE package first and the 28px en-US package second", [] {})
            .When("both are measured for the same key", [] {})
            .Then("the recorded worst case is still 58px from de-DE",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-TITLE", 3, 6);

                      // Reversed on purpose: the maximum has to come from a comparison, not from
                      // whichever package the caller happened to list last.
                      const std::array<md::LocaleText, 2> locales{approved.german.view(), approved.english.view()};

                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(labelScreen(100, 20, "STR-TITLE")),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      checks.expect(result.ok(), "the screen compiles");
                      checks.expect(result.measurements.size() == 1, "the field is measured");
                      if (result.measurements.size() == 1) {
                          checks.expect(result.measurements.front().locale == "de-DE", "the widest locale is recorded");
                          checks.expect(result.measurements.front().extent.width == 58,
                                        std::format("the worst case is 58px, got {}px", result.measurements.front().extent.width));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register blankGlyphsAreNotInk{
    "A leading blank glyph does not widen the measured extent",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-blank-glyphs-skipped")
            .Given("a run that opens with a space at the pen origin and three letters after it", [] {})
            .When("a 30px Label carrying that run is measured", [] {})
            .Then("the ink measures 28px and the box is accepted",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();

                      // Records at x = 0, 10, 20, 30: a blank space, then three letters. Counting
                      // the space as ink would measure from x 0 and make this 38px, which a 30px
                      // box would reject - so the acceptance below is the assertion.
                      const std::array<std::uint16_t, 4>                                  glyphs{spaceIndex, letterIndex, letterIndex, letterIndex};
                      const std::array<std::pair<std::string, std::vector<std::byte>>, 1> englishRuns{
                          std::pair{std::string{"STR-TITLE"}, bakeRun(glyphs)}
                      };
                      const ApprovedText approved{.english = bakeLocale("en-US", englishRuns), .german = letterLocale("de-DE", "STR-TITLE", 1)};
                      const auto         locales = approved.views();

                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(labelScreen(30, 20, "STR-TITLE")),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      checks.expect(result.ok(), "the screen compiles");
                      checks.expect(result.measurements.size() == 1, "the field is measured");
                      if (result.measurements.size() == 1) {
                          checks.expect(
                              result.measurements.front().extent == md::TextExtent{28, 10},
                              std::format("the ink is 28x10, got {}x{}", result.measurements.front().extent.width, result.measurements.front().extent.height));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register heightIsBudgetedToo{"A box shorter than its translation is rejected on the vertical axis", "evidence-unit", [] {
                                                   return speclab::Test("medui-textbudget-height-exceeded")
                                                       .Given("a Label 8px tall carrying a run whose ink is 10px tall", [] {})
                                                       .When("the resolved screen is checked", [] {})
                                                       .Then("MEDUI-E050 names the height it needs and the height it has",
                                                             [] {
                                                                 mdux::spec::Checks      checks;
                                                                 const font::FontPackage fontPackage = fixtureFont();
                                                                 const ApprovedText      approved    = approvedText("STR-TITLE", 2, 2);
                                                                 const auto              locales     = approved.views();

                                                                 const md::TextBudgetResult result = md::checkTextBudgets(
                                                                     layoutOf(labelScreen(100, 8, "STR-TITLE")),
                                                                     "budget.medui",
                                                                     {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                                                                 const cli::Diagnostic* reported = find(result, md::Code::TextBudgetExceeded);
                                                                 checks.expect(!result.ok(), "the screen is rejected");
                                                                 checks.expect(mentions(reported, "height"), "the diagnostic names the axis that failed");
                                                                 checks.expect(mentions(reported, "10px"), "the diagnostic names the required height");
                                                                 checks.expect(mentions(reported, "8px"), "the diagnostic names the available height");
                                                                 checks.raise();
                                                             })
                                                       .Execute();
                                               }};

const mdux::spec::Register listValuedTextIsMeasuredPerElement{
    "Every element of a list-valued text field is measured, not only the first",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-text-key-list")
            .Given("a StatusIndicator whose states are a 28px key and a 58px key", [] {})
            .When("a box that fits both, and then one that fits only the first, are checked", [] {})
            .Then("both keys are measured, and the second is what the narrow box rejects",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();

                      const std::array<std::pair<std::string, std::size_t>, 2> keys{
                          std::pair{   std::string{"STR-OK"}, std::size_t{3}},
                          std::pair{std::string{"STR-ALARM"}, std::size_t{6}}
                      };
                      const ApprovedText approved{.english = lettersLocale("en-US", keys), .german = lettersLocale("de-DE", keys)};
                      const auto         locales = approved.views();

                      const auto states = [](std::int64_t width) {
                          return screenWith(std::format("    StatusIndicator {{ id: status; width: {}px; height: 20px; "
                                                        "requirement: \"REQ-1\"; source: \"STATE\"; "
                                                        "states: [t(\"STR-OK\"), t(\"STR-ALARM\")]; }}\n",
                                                        width));
                      };

                      const md::TextBudgetResult fits = md::checkTextBudgets(layoutOf(states(100)),
                                                                             "budget.medui",
                                                                             {.font = &fontPackage, .locales = locales, .dynamicText = {}});
                      checks.expect(fits.ok(), std::format("the wide box compiles, got {} diagnostics", fits.diagnostics.size()));
                      checks.expect(fits.measurements.size() == 2, std::format("both list elements are measured, got {}", fits.measurements.size()));
                      if (fits.measurements.size() == 2) {
                          checks.expect(fits.measurements[0].textKey == "STR-OK" && fits.measurements[1].textKey == "STR-ALARM",
                                        "the measurements follow the authored list order");
                          checks.expect(fits.measurements[1].field == "states", "a list element is attributed to its field");
                      }

                      const md::TextBudgetResult narrow = md::checkTextBudgets(layoutOf(states(40)),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});
                      checks.expect(!narrow.ok(), "the narrow box is rejected");
                      checks.expect(mentions(find(narrow, md::Code::TextBudgetExceeded), "STR-ALARM"), "the second element is the one that does not fit");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register diagnosticsFollowNodeAndLocaleOrder{
    "Diagnostics accumulate in node order, then in the input order of the locales",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-diagnostic-order")
            .Given("two 10px Labels, each too narrow for both approved translations", [] {})
            .When("the resolved screen is checked", [] {})
            .Then("the four diagnostics arrive title/en-US, title/de-DE, subtitle/en-US, subtitle/de-DE",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();

                      const std::array<std::pair<std::string, std::size_t>, 2> keys{
                          std::pair{   std::string{"STR-TITLE"}, std::size_t{3}},
                          std::pair{std::string{"STR-SUBTITLE"}, std::size_t{3}}
                      };
                      const ApprovedText approved{.english = lettersLocale("en-US", keys), .german = lettersLocale("de-DE", keys)};
                      const auto         locales = approved.views();

                      const std::string source = screenWith(
                          "    Label { id: title; width: 10px; height: 20px; text: t(\"STR-TITLE\"); color: Theme.Colors.Title; }\n"
                          "    Label { id: subtitle; width: 10px; height: 20px; text: t(\"STR-SUBTITLE\"); color: Theme.Colors.Title; }\n");

                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      checks.expect(result.diagnostics.size() == 4, std::format("every failing pair is reported, got {}", result.diagnostics.size()));
                      if (result.diagnostics.size() == 4) {
                          checks.expect(mentions(result.diagnostics[0], "STR-TITLE") && mentions(result.diagnostics[0], "en-US"),
                                        "the first node's first locale comes first");
                          checks.expect(mentions(result.diagnostics[1], "STR-TITLE") && mentions(result.diagnostics[1], "de-DE"),
                                        "locales follow their input order within a field");
                          checks.expect(mentions(result.diagnostics[2], "STR-SUBTITLE") && mentions(result.diagnostics[2], "en-US"),
                                        "the second node follows the first");
                          checks.expect(mentions(result.diagnostics[3], "STR-SUBTITLE") && mentions(result.diagnostics[3], "de-DE"),
                                        "and its locales follow the same order");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register dynamicTextCannotEscapeTheCharset{
    "A format that can produce an unbaked character is MEDUI-E053",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-charset-escape")
            .Given("a Clock whose format can emit U+005A, which the font package does not hold", [] {})
            .When("the format is resolved against the governed dynamic-text table", [] {})
            .Then("MEDUI-E053 names the escaping code point",
                  [] {
                      mdux::spec::Checks                       checks;
                      const font::FontPackage                  fontPackage = fixtureFont();
                      const ApprovedText                       approved    = approvedText("STR-UNUSED", 1, 1);
                      const auto                               locales     = approved.views();
                      const std::array<md::DynamicTextRule, 1> table{
                          md::DynamicTextRule{.name = "HH_MM_TZ", .produces = digitsAndZone}
                      };

                      const std::string          source = screenWith("    Clock { id: now; width: 100px; height: 20px; format: HH_MM_TZ; }\n");
                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = table});

                      const cli::Diagnostic* reported = find(result, md::Code::CharsetEscape);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(mentions(reported, "HH_MM_TZ"), "the diagnostic names the dynamic-text source");
                      checks.expect(mentions(reported, "U+005A"), "the diagnostic names the code point that escapes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register charsetWalkCrossesRangeBoundaries{
    "A produced range spanning a gap in the charset is caught at the gap",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-charset-gap")
            .Given("a source producing U+0030..U+0041, which the font holds either side of a gap", [] {})
            .When("the produced range is walked against the restricted charset", [] {})
            .Then("MEDUI-E053 names U+003B, the first code point in the gap",
                  [] {
                      mdux::spec::Checks                       checks;
                      const font::FontPackage                  fontPackage = fixtureFont();
                      const ApprovedText                       approved    = approvedText("STR-UNUSED", 1, 1);
                      const auto                               locales     = approved.views();
                      const std::array<md::DynamicTextRule, 1> table{
                          md::DynamicTextRule{.name = "DIGITS_TO_A", .produces = digitsThroughLetter}
                      };

                      const std::string          source = screenWith("    Clock { id: now; width: 100px; height: 20px; format: DIGITS_TO_A; }\n");
                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = table});

                      const cli::Diagnostic* reported = find(result, md::Code::CharsetEscape);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(mentions(reported, "U+003B"), "the walk stops at the first code point the charset omits");
                      checks.expect(result.diagnostics.size() == 1, std::format("one diagnostic per field, got {}", result.diagnostics.size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register rangesBeyondUnicodeAreReported{
    "A produced range reaching past the last Unicode scalar value is reported, not clamped",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-charset-beyond-unicode")
            .Given("a governed entry declaring U+110000, which is not a character", [] {})
            .When("the entry is walked", [] {})
            .Then("MEDUI-E053 says so rather than silently measuring only the part that is",
                  [] {
                      mdux::spec::Checks                       checks;
                      const font::FontPackage                  fontPackage = fixtureFont();
                      const ApprovedText                       approved    = approvedText("STR-UNUSED", 1, 1);
                      const auto                               locales     = approved.views();
                      const std::array<md::DynamicTextRule, 1> table{
                          md::DynamicTextRule{.name = "OUT_OF_RANGE", .produces = beyondUnicode}
                      };

                      const std::string          source = screenWith("    Clock { id: now; width: 100px; height: 20px; format: OUT_OF_RANGE; }\n");
                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = table});

                      const cli::Diagnostic* reported = find(result, md::Code::CharsetEscape);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(mentions(reported, "U+110000"), "the diagnostic names the offending code point");
                      checks.expect(mentions(reported, "not a Unicode scalar value"), "and says what is wrong with it");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unresolvedDynamicTextFailsClosed{
    "A dynamic-text source with no governed entry fails closed",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-unbounded-dynamic-source")
            .Given("a Clock naming a format the governed table does not define", [] {})
            .When("the format is resolved", [] {})
            .Then("MEDUI-E053 says what it produces is not bounded, rather than passing it over",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-UNUSED", 1, 1);
                      const auto              locales     = approved.views();

                      const std::string          source = screenWith("    Clock { id: now; width: 100px; height: 20px; format: HH_MM; }\n");
                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = {}});

                      const cli::Diagnostic* reported = find(result, md::Code::CharsetEscape);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(mentions(reported, "HH_MM"), "the diagnostic names the unresolved source");
                      checks.expect(mentions(reported, "not bounded"), "the diagnostic says why it cannot pass");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register boundedDynamicTextIsAccepted{
    "A format inside the restricted charset compiles, and an unnamed charset is not an error",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-charset-accepted")
            .Given("a Clock producing digits and a colon, and a TextInput with no charset field", [] {})
            .When("the screen is checked against the font package's restricted charset", [] {})
            .Then("neither component is reported",
                  [] {
                      mdux::spec::Checks                       checks;
                      const font::FontPackage                  fontPackage = fixtureFont();
                      const ApprovedText                       approved    = approvedText("STR-UNUSED", 1, 1);
                      const auto                               locales     = approved.views();
                      const std::array<md::DynamicTextRule, 1> table{
                          md::DynamicTextRule{.name = "HH_MM", .produces = digitsAndColon}
                      };

                      const std::string          source = screenWith("    Clock { id: now; width: 100px; height: 20px; format: HH_MM; }\n"
                                                                     "    TextInput { id: entry; width: 100px; height: 20px; source: \"OPERATOR_NOTE\"; "
                                                                     "max_length: 16; color: Theme.Colors.Title; }\n");
                      const md::TextBudgetResult result = md::checkTextBudgets(layoutOf(source),
                                                                               "budget.medui",
                                                                               {.font = &fontPackage, .locales = locales, .dynamicText = table});

                      checks.expect(result.ok(), std::format("the screen compiles, got {} diagnostics", result.diagnostics.size()));
                      checks.expect(result.measurements.empty(), "a screen with no text key measures nothing");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register incompleteApprovedSetIsRefused{
    "A budget measured over some of the approved locales is refused as a wiring error",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-approved-set")
            .Given("a font package approving en-US and de-DE", [] {})
            .When("a caller supplies a subset, a duplicate, or a locale the font does not approve", [] {})
            .Then("each is std::logic_error rather than a screen certified against a set nobody approved",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-TITLE", 3, 6);
                      const md::LayoutResult  resolved    = layoutOf(labelScreen(100, 20, "STR-TITLE"));

                      const auto check = [&](std::span<const md::LocaleText> locales) {
                          return throwsWiringError([&] {
                              [[maybe_unused]] const md::TextBudgetResult ignored =
                                  md::checkTextBudgets(resolved, "budget.medui", {.font = &fontPackage, .locales = locales, .dynamicText = {}});
                          });
                      };

                      // The omission this check exists for: the 58px German translation is the one
                      // that overflows, and leaving it out is how a screen passes without it.
                      const std::array<md::LocaleText, 1> englishOnly{approved.english.view()};
                      checks.expect(check(englishOnly), "an approved locale that was not supplied is refused");

                      const std::array<md::LocaleText, 2> duplicated{approved.english.view(), approved.english.view()};
                      checks.expect(check(duplicated), "the same locale supplied twice is refused");

                      const Locale                        unapproved = letterLocale("fr-FR", "STR-TITLE", 3);
                      const std::array<md::LocaleText, 3> extra{approved.english.view(), approved.german.view(), unapproved.view()};
                      checks.expect(check(extra), "a locale the font package does not approve is refused");

                      const auto complete = approved.views();
                      checks.expect(!check(complete), "the complete approved set is accepted");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unwalkableFontCharsetIsRefused{
    "A font package whose charset table cannot be walked is refused rather than walked",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-font-charset-walkable")
            .Given("a font package with a charset range ending at the type maximum, and one that descends", [] {})
            .When("a screen naming a dynamic-text source is checked against each", [] {})
            .Then("both are std::logic_error, and the walk cannot loop",
                  [] {
                      mdux::spec::Checks checks;

                      // Without the up-front check this scenario would hang rather than fail: the
                      // walk advances to `covering->last + 1`, which wraps to zero for a range
                      // ending at the type maximum and selects the same range forever. A hang is
                      // the one failure a test cannot report, which is why the check is up front
                      // rather than a guard inside the loop.
                      font::FontPackage wrapping = fixtureFont();
                      wrapping.restrictedCharset.push_back(font::CharsetRange{.first = U'X', .last = static_cast<char32_t>(0xFFFFFFFF)});

                      font::FontPackage descending = fixtureFont();
                      descending.restrictedCharset.push_back(font::CharsetRange{.first = U'Y', .last = U'X'});

                      const ApprovedText                       approved = approvedText("STR-UNUSED", 1, 1);
                      const auto                               locales  = approved.views();
                      const std::array<md::DynamicTextRule, 1> table{
                          md::DynamicTextRule{.name = "HH_MM", .produces = digitsAndColon}
                      };
                      const md::LayoutResult resolved = layoutOf(screenWith("    Clock { id: now; width: 100px; height: 20px; format: HH_MM; }\n"));

                      const auto check = [&](const font::FontPackage& fontPackage) {
                          return throwsWiringError([&] {
                              [[maybe_unused]] const md::TextBudgetResult ignored =
                                  md::checkTextBudgets(resolved, "budget.medui", {.font = &fontPackage, .locales = locales, .dynamicText = table});
                          });
                      };

                      checks.expect(check(wrapping), "a charset range past the last Unicode scalar value is refused");
                      checks.expect(check(descending), "a descending charset range is refused");
                      checks.expect(!wrapping.validate().has_value() && !descending.validate().has_value(),
                                    "FontPackage::validate() rejects both too - this stage guards the packages it is handed unvalidated");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register malformedPackagesAreWiringErrors{
    "A miswired stage throws rather than emitting a diagnostic an author cannot act on",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-gates-throw")
            .Given("a null font, a truncated sidecar, a foreign atlas, a partial record and a missing key", [] {})
            .When("the budget check runs on each", [] {})
            .Then("every one is std::logic_error",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const md::LayoutResult  resolved    = layoutOf(labelScreen(100, 20, "STR-TITLE"));

                      const auto check = [&](std::span<const md::LocaleText> locales) {
                          return throwsWiringError([&] {
                              [[maybe_unused]] const md::TextBudgetResult ignored =
                                  md::checkTextBudgets(resolved, "budget.medui", {.font = &fontPackage, .locales = locales, .dynamicText = {}});
                          });
                      };

                      checks.expect(throwsWiringError([&] {
                                        [[maybe_unused]] const md::TextBudgetResult ignored =
                                            md::checkTextBudgets(resolved, "budget.medui", {.font = nullptr, .locales = {}, .dynamicText = {}});
                                    }),
                                    "a null font package is a wiring error, not a diagnostic");

                      // A sidecar one byte short of what its package declares. The selected run
                      // still fits inside what was handed over, which is exactly why the check is
                      // against the declared length rather than against this run's end.
                      ApprovedText truncated = approvedText("STR-TITLE", 3, 6);
                      truncated.english.sidecar.pop_back();
                      const auto truncatedViews = truncated.views();
                      checks.expect(check(truncatedViews), "a sidecar shorter than the package declares is refused");

                      ApprovedText foreign            = approvedText("STR-TITLE", 3, 6);
                      foreign.english.package.atlasId = "another-font";
                      const auto foreignViews         = foreign.views();
                      checks.expect(check(foreignViews), "a package baked against a different font is refused");

                      // 17 bytes is not a whole number of 6-byte records, so the run cannot be
                      // enumerated - the failure `mdux.text.schema` documents as the one byte
                      // identity cannot detect once it reaches a device.
                      ApprovedText partial                             = approvedText("STR-TITLE", 3, 6);
                      partial.english.package.runs.front().byteLength -= 1;
                      partial.english.package.sidecarByteLength       -= 1;
                      partial.english.sidecar.pop_back();
                      const auto partialViews = partial.views();
                      checks.expect(check(partialViews), "a run whose byte length is not a whole number of records is refused");

                      const ApprovedText missing      = approvedText("STR-OTHER", 3, 6);
                      const auto         missingViews = missing.views();
                      checks.expect(check(missingViews), "an unmeasurable key means analyze() was bypassed");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fixturePackagesAreRealArtifacts{
    "The fixture font and text packages satisfy their own schemas",
    "evidence-unit",
    [] {
        return speclab::Test("medui-textbudget-fixtures-validate")
            .Given("the synthetic font package and both approved locales", [] {})
            .When("each is validated by the module that owns its format", [] {})
            .Then("all three pass, so the measurements above are taken from legal artifacts",
                  [] {
                      mdux::spec::Checks      checks;
                      const font::FontPackage fontPackage = fixtureFont();
                      const ApprovedText      approved    = approvedText("STR-TITLE", 3, 6);

                      checks.expect(fontPackage.validate().has_value(), "the fixture font is a valid font package");
                      checks.expect(approved.english.package.validate().has_value(), "the fixture en-US package is valid");
                      checks.expect(approved.german.package.validate().has_value(), "the fixture de-DE package is valid");
                      checks.expect(fontPackage.permits(U'A') && !fontPackage.permits(U'Z'),
                                    "the restricted charset holds the letters the runs use and not the one they must not");
                      checks.raise();
                  })
            .Execute();
    }};
