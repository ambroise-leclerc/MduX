/**
 * @file SchemaTests.cpp
 * @brief BDD scenarios for the governed font package schema (issue #161).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * Two kinds of scenario here, and the distinction is deliberate.
 *
 * The **round trip** is the one that matters most: this module exists so that the baker that
 * writes a package and the runtime that reads one cannot drift apart, and the way to check that
 * is to write a package, read it back, and compare - not to assert that each side independently
 * produces what a test author expected. A writer and a reader can both be wrong in the same way
 * only if they share code, which is the point.
 *
 * The **rejection corpus** pins one `SchemaError` per rule. `validate()` is called on the way out
 * of `write()` and on the way in from `parse()`, so a package that exists anywhere in the system
 * is one that passed - and each rule is worth a distinct code because "invalid package" tells an
 * author nothing about which of two dozen rules they broke.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.font.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace fp = mdux::font;
using fp::SchemaError;

/// A package that passes every rule, which each rejection case then breaks in exactly one way.
/// Built rather than loaded so a scenario's defect is one line of code, not a diff against a blob.
[[nodiscard]] fp::FontPackage validPackage() {
    fp::FontPackage package;
    package.id         = "fixture";
    package.unitsPerEm = 2048;
    package.pixelSize  = 16;
    package.locales    = {"en-US"};

    package.atlas.path             = "atlas.bin";
    package.atlas.width            = 64;
    package.atlas.height           = 64;
    package.atlas.byteLength       = 64u * 64u;
    package.atlas.sha256           = std::string(64, 'a');
    package.atlas.occupancyPercent = 25;

    // The ten decimal digits, so the tabular-figure rule has something to check and the charset
    // has a contiguous run to cover. Deliberately no letters: a case that needs one adds it, and
    // the absence is what makes the "charset names a code point with no glyph" case meaningful.
    std::uint32_t x = 0;
    for (char32_t point = U'0'; point <= U'9'; ++point) {
        package.glyphs.push_back(fp::GlyphRecord{.codePoint       = point,
                                                 .glyphIndex      = static_cast<std::uint16_t>(point),
                                                 .advanceWidth    = 1303,  // shared: tabular
                                                 .leftSideBearing = 7,
                                                 .x               = x,
                                                 .y               = 0,
                                                 .width           = 5,
                                                 .height          = 9,
                                                 .bitmapOriginX   = 0,
                                                 .bitmapOriginY   = 9});
        x += 6;
    }
    package.restrictedCharset = {fp::CharsetRange{.first = U'0', .last = U'9'}};
    return package;
}

}  // namespace

const mdux::spec::Register packageSurvivesARoundTrip{
    "A package written by this module reads back identical through it",
    "evidence-unit",
    [] {
        // The assertion the module exists for. If the writer and the reader disagreed about a
        // field's name, width or units, this is where it shows - and it shows as a difference in
        // the value rather than as an opinion about what the JSON should have looked like.
        struct State {
            fp::FontPackage original;
            std::string     text;
            std::optional<fp::FontPackage> parsed;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("font-schema-round-trip")
            .Given("a valid package with glyphs, a charset and kerning",
                   [state] {
                       state->original = validPackage();
                       state->original.kerning = {fp::KerningPair{.left = U'1', .right = U'2', .adjustment = -40}};
                   })
            .When("it is written and parsed back",
                  [state] {
                      auto text = state->original.write();
                      if (!text.has_value()) {
                          throw speclab::core::AssertionFailure(std::format("write failed: {}", fp::describe(text.error())),
                                                                std::source_location::current());
                      }
                      state->text  = std::move(*text);
                      auto parsed  = fp::FontPackage::parse(state->text);
                      if (!parsed.has_value()) {
                          throw speclab::core::AssertionFailure(std::format("parse failed: {}", fp::describe(parsed.error())),
                                                                std::source_location::current());
                      }
                      state->parsed = std::move(*parsed);
                  })
            .Then("every field survives, and writing it again produces the same bytes",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        a = state->original;
                      const auto&        b = *state->parsed;
                      checks.expect(a.id == b.id && a.unitsPerEm == b.unitsPerEm && a.pixelSize == b.pixelSize,
                                    "identity and metrics survive");
                      checks.expect(a.locales == b.locales, "locales survive");
                      checks.expect(a.atlas.path == b.atlas.path && a.atlas.width == b.atlas.width
                                        && a.atlas.height == b.atlas.height && a.atlas.byteLength == b.atlas.byteLength
                                        && a.atlas.sha256 == b.atlas.sha256
                                        && a.atlas.occupancyPercent == b.atlas.occupancyPercent,
                                    "atlas metrics survive, occupancy included");
                      checks.expect(a.glyphs.size() == b.glyphs.size(), "every glyph survives");
                      if (a.glyphs.size() == b.glyphs.size()) {
                          bool same = true;
                          for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
                              const auto& g = a.glyphs[i];
                              const auto& h = b.glyphs[i];
                              same = same && g.codePoint == h.codePoint && g.glyphIndex == h.glyphIndex
                                     && g.advanceWidth == h.advanceWidth && g.leftSideBearing == h.leftSideBearing
                                     && g.x == h.x && g.y == h.y && g.width == h.width && g.height == h.height
                                     && g.bitmapOriginX == h.bitmapOriginX && g.bitmapOriginY == h.bitmapOriginY;
                          }
                          checks.expect(same, "every glyph field survives, including signed bearings and origins");
                      }
                      checks.expect(b.kerningFor(U'1', U'2') == -40, "a negative kerning adjustment survives its sign");
                      checks.expect(a.restrictedCharset.size() == b.restrictedCharset.size(), "the charset survives");

                      // Byte-level idempotence, which is what the committed artifact depends on.
                      auto again = b.write();
                      checks.expect(again.has_value() && *again == state->text,
                                    "re-writing the parsed package produces identical bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register charsetPermitsExactlyWhatItNames{
    "permits() answers for the charset, and never for a code point outside it",
    "evidence-unit",
    [] {
        // The test the .medui compiler (#15) will apply to every code point a dynamic-text format
        // could produce. A false positive here would let a screen reach a glyph the runtime cannot
        // draw, and the runtime has no fallback - having one would mean mapping code points on
        // device, which is the thing ADR-010 forbids.
        return speclab::Test("font-schema-permits")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("every named point is permitted and every neighbour is not",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         package = validPackage();
                      for (char32_t point = U'0'; point <= U'9'; ++point) {
                          checks.expect(package.permits(point), std::format("U+{:04X} is permitted", static_cast<std::uint32_t>(point)));
                          checks.expect(package.find(point) != nullptr, "and has a glyph, which is what permits() promises");
                      }
                      checks.expect(!package.permits(U'0' - 1), "the point below the range is refused");
                      checks.expect(!package.permits(U'9' + 1), "the point above the range is refused");
                      checks.expect(!package.permits(U'A'), "an unlisted ASCII letter is refused");
                      checks.expect(!package.permits(U'中'), "an unlisted CJK point is refused");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register schemaRejections{
    "validate() emits the right stable code per broken rule",
    "evidence-unit",
    [] {
        struct Case {
            std::string_view              what;
            SchemaError                   expected;
            std::function<void(fp::FontPackage&)> breakIt;
        };

        const std::vector<Case> cases{
            {"an empty id", SchemaError::EmptyId, [](fp::FontPackage& p) { p.id.clear(); }},
            {"no locales", SchemaError::EmptyLocales, [](fp::FontPackage& p) { p.locales.clear(); }},
            {"an empty locale tag", SchemaError::EmptyLocaleTag, [](fp::FontPackage& p) { p.locales = {""}; }},
            {"a duplicated locale", SchemaError::DuplicateLocale,
             [](fp::FontPackage& p) { p.locales = {"en-US", "en-US"}; }},
            {"a zero unitsPerEm", SchemaError::UnsupportedUnitsPerEm, [](fp::FontPackage& p) { p.unitsPerEm = 0; }},
            {"an atlas that is not power-of-two", SchemaError::AtlasNotPowerOfTwo,
             [](fp::FontPackage& p) {
                 p.atlas.width      = 60;
                 p.atlas.byteLength = 60u * 64u;
             }},
            {"an atlas byteLength that is not width * height", SchemaError::AtlasSizeMismatch,
             [](fp::FontPackage& p) { p.atlas.byteLength += 1; }},
            {"an atlas path with a separator", SchemaError::AtlasPathHasSeparator,
             // A package must not be able to name a sidecar outside its own directory.
             [](fp::FontPackage& p) { p.atlas.path = "../atlas.bin"; }},
            {"an atlas path of \"..\"", SchemaError::AtlasPathHasSeparator,
             // Banning only separators let "." and ".." through, and both resolve to a directory.
             [](fp::FontPackage& p) { p.atlas.path = ".."; }},
            {"a Windows drive-relative atlas path", SchemaError::AtlasPathHasSeparator,
             // "C:atlas.bin" is relative to the drive's current directory, not to this package.
             [](fp::FontPackage& p) { p.atlas.path = "C:atlas.bin"; }},
            {"an empty atlas digest", SchemaError::InvalidAtlasDigest,
             // The field exists so a runtime can check the sidecar belongs to the package; an
             // empty one is not an integrity value.
             [](fp::FontPackage& p) { p.atlas.sha256.clear(); }},
            {"an atlas digest of the wrong length", SchemaError::InvalidAtlasDigest,
             [](fp::FontPackage& p) { p.atlas.sha256 = std::string(63, 'a'); }},
            {"an uppercase atlas digest", SchemaError::InvalidAtlasDigest,
             // Case matters: two spellings of one digest would compare unequal as strings.
             [](fp::FontPackage& p) { p.atlas.sha256 = std::string(64, 'A'); }},
            {"a non-hexadecimal atlas digest", SchemaError::InvalidAtlasDigest,
             [](fp::FontPackage& p) { p.atlas.sha256 = std::string(64, 'z'); }},
            {"a code point above U+10FFFF", SchemaError::CodePointOutOfRange,
             // char32_t admits four billion values; only 0x110000 are characters.
             [](fp::FontPackage& p) { p.glyphs.back().codePoint = 0x110000; }},
            {"no glyphs", SchemaError::NoGlyphs, [](fp::FontPackage& p) { p.glyphs.clear(); }},
            {"glyphs out of order", SchemaError::GlyphsNotSorted,
             [](fp::FontPackage& p) { std::swap(p.glyphs[0], p.glyphs[1]); }},
            {"a duplicated code point", SchemaError::DuplicateCodePoint,
             [](fp::FontPackage& p) { p.glyphs[1].codePoint = p.glyphs[0].codePoint; }},
            {"a glyph whose slot leaves the sheet", SchemaError::GlyphOutsideAtlas,
             [](fp::FontPackage& p) { p.glyphs.back().x = p.atlas.width - 1; }},
            {"digits that do not share an advance", SchemaError::TabularFigureMismatch,
             // The safety rule: a numeric field redrawn as its value changes jitters if '1' is
             // narrower than '8', and that defect is invisible in a static screenshot.
             [](fp::FontPackage& p) { p.glyphs[3].advanceWidth += 100; }},
            {"a charset naming a code point with no glyph", SchemaError::CharsetGlyphMissing,
             // permits() would otherwise promise a glyph the runtime cannot find, and it has no
             // fallback by design.
             [](fp::FontPackage& p) { p.restrictedCharset = {fp::CharsetRange{.first = U'0', .last = U'A'}}; }},
            {"a descending charset range", SchemaError::CharsetRangeDescending,
             [](fp::FontPackage& p) { p.restrictedCharset = {fp::CharsetRange{.first = U'9', .last = U'0'}}; }},
            {"overlapping charset ranges", SchemaError::CharsetRangesOverlap,
             [](fp::FontPackage& p) {
                 p.restrictedCharset = {fp::CharsetRange{.first = U'0', .last = U'5'},
                                        fp::CharsetRange{.first = U'3', .last = U'9'}};
             }},
            {"an empty charset", SchemaError::EmptyCharset, [](fp::FontPackage& p) { p.restrictedCharset.clear(); }},
            {"a kerning pair naming an absent glyph", SchemaError::KerningGlyphMissing,
             [](fp::FontPackage& p) { p.kerning = {fp::KerningPair{.left = U'0', .right = U'A', .adjustment = -10}}; }},
            {"a duplicated kerning pair", SchemaError::DuplicateKerningPair,
             [](fp::FontPackage& p) {
                 p.kerning = {fp::KerningPair{.left = U'0', .right = U'1', .adjustment = -10},
                              fp::KerningPair{.left = U'0', .right = U'1', .adjustment = -20}};
             }},
        };

        return speclab::Test("font-schema-rejections")
            .Given("a valid package, broken one rule at a time", [] {})
            .When("each is validated", [] {})
            .Then("each yields exactly the SchemaError identified in the corpus",
                  [&cases] {
                      mdux::spec::Checks checks;
                      // The fixture must itself be valid, or every case below would pass for the
                      // wrong reason.
                      auto baseline = validPackage().validate();
                      checks.expect(baseline.has_value(),
                                    baseline.has_value() ? "the fixture is valid"
                                                         : std::format("the fixture is invalid: {}",
                                                                       fp::describe(baseline.error())));
                      for (const Case& entry : cases) {
                          fp::FontPackage package = validPackage();
                          entry.breakIt(package);
                          auto result = package.validate();
                          checks.expect(!result.has_value(), std::format("{}: validation passed unexpectedly", entry.what));
                          if (!result.has_value()) {
                              checks.expect(result.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, fp::describe(result.error()),
                                                        fp::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register parserRejectsOutOfRangeIntegers{
    "parse() refuses a JSON integer that does not fit its field, rather than narrowing it",
    "evidence-unit",
    [] {
        // The finding this scenario exists for: `parse()` used to `static_cast` every value it
        // read, so `unitsPerEm: 65537` narrowed to 1 and `pixelSize: -1` wrapped to 4294967295 -
        // and both then satisfied validate(). The package parsed successfully as *different data
        // than the JSON contained*, which is exactly the guarantee this module is supposed to
        // make. Range-checking before the cast is what makes "it parsed" mean something.
        //
        // Each case edits one member of an otherwise valid document, so a failure names the field.
        struct Case {
            std::string_view what;
            SchemaError      expected;
            std::string_view member;
            std::string_view value;
        };

        const std::vector<Case> cases{
            {"unitsPerEm above uint16", SchemaError::IntegerOutOfRange, "\"unitsPerEm\"", "65537"},
            {"a negative unitsPerEm", SchemaError::IntegerOutOfRange, "\"unitsPerEm\"", "-1"},
            {"pixelSize above uint32", SchemaError::IntegerOutOfRange, "\"pixelSize\"", "4294967312"},
            {"a negative pixelSize", SchemaError::IntegerOutOfRange, "\"pixelSize\"", "-1"},
        };

        return speclab::Test("font-schema-integer-range")
            .Given("a valid package document, one field at a time replaced with an out-of-range value", [] {})
            .When("each is parsed", [] {})
            .Then("each is refused rather than narrowed",
                  [&cases] {
                      mdux::spec::Checks checks;
                      auto               baseText = validPackage().write();
                      checks.expect(baseText.has_value(), "the fixture writes");
                      if (!baseText.has_value()) {
                          checks.raise();
                          return;
                      }
                      for (const Case& entry : cases) {
                          // Canonical JSON writes `"key":value` with no spaces, so the member can
                          // be located and its value replaced without a JSON editor.
                          const std::string key{entry.member};
                          const auto        at = baseText->find(key + ":");
                          checks.expect(at != std::string::npos, std::format("{}: found {} in the document", entry.what, key));
                          if (at == std::string::npos) {
                              continue;
                          }
                          const auto valueStart = at + key.size() + 1;
                          const auto valueEnd   = baseText->find_first_of(",}", valueStart);
                          std::string edited    = *baseText;
                          edited.replace(valueStart, valueEnd - valueStart, entry.value);

                          auto parsed = fp::FontPackage::parse(edited);
                          checks.expect(!parsed.has_value(), std::format("{}: parsing succeeded unexpectedly", entry.what));
                          if (!parsed.has_value()) {
                              checks.expect(parsed.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, fp::describe(parsed.error()),
                                                        fp::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register committedPackageParses{
    "The committed dejavu-ui package parses and permits exactly its charset",
    "evidence-unit",
    [] {
        // The end-to-end assertion: the artifact this repository ships is one this module accepts.
        // A schema that only ever validated its own fixtures would not have caught a baker writing
        // a field under a different name.
        struct State {
            std::string                    text;
            std::optional<fp::FontPackage> package;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("font-schema-committed-package")
            .Given("generated/font/dejavu-ui/package.json",
                   [state] {
                       const std::filesystem::path path =
                           std::filesystem::path{MDUX_REPO_ROOT} / "generated" / "font" / "dejavu-ui" / "package.json";
                       std::ifstream in{path, std::ios::binary};
                       if (!in) {
                           throw speclab::core::AssertionFailure(std::format("cannot open {}", path.string()),
                                                                 std::source_location::current());
                       }
                       state->text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                   })
            .When("it is parsed",
                  [state] {
                      auto package = fp::FontPackage::parse(state->text);
                      if (!package.has_value()) {
                          throw speclab::core::AssertionFailure(
                              std::format("the committed package does not validate: {}", fp::describe(package.error())),
                              std::source_location::current());
                      }
                      state->package = std::move(*package);
                  })
            .Then("it carries 95 printable-ASCII glyphs with tabular digits",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        package = *state->package;
                      checks.expect(package.id == "dejavu-ui", "id");
                      checks.expect(package.glyphs.size() == 95, std::format("95 glyphs, got {}", package.glyphs.size()));
                      // Every code point, not just the ends: a package missing an interior one
                      // could still have 95 glyphs and pass an endpoint check.
                      bool everyPointPresent = true;
                      char32_t firstMissing  = 0;
                      for (char32_t point = U' '; point <= U'~'; ++point) {
                          if (!package.permits(point) || package.find(point) == nullptr) {
                              everyPointPresent = false;
                              firstMissing      = point;
                              break;
                          }
                      }
                      checks.expect(everyPointPresent,
                                    everyPointPresent ? "every printable-ASCII point is permitted and baked"
                                                      : std::format("U+{:04X} is missing", static_cast<std::uint32_t>(firstMissing)));
                      checks.expect(!package.permits(U'é'), "an accented letter outside the charset is refused");
                      // The tabular rule already passed inside validate(); this pins the value so a
                      // future font swap that quietly broke it is visible in the diff.
                      const auto* zero = package.find(U'0');
                      const auto* eight = package.find(U'8');
                      checks.expect(zero != nullptr && eight != nullptr, "the digits are present");
                      if (zero != nullptr && eight != nullptr) {
                          checks.expect(zero->advanceWidth == eight->advanceWidth,
                                        std::format("'0' and '8' share advance {}", zero->advanceWidth));
                      }
                      const auto* space = package.find(U' ');
                      checks.expect(space != nullptr && space->isBlank() && space->advanceWidth > 0,
                                    "the space is blank but advances");
                      checks.raise();
                  })
            .Execute();
    }};
